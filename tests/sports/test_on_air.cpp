// Build 141: a tile is live only when what it offers is on air (a golf
// round under way whose feeds all start later is "later", at the first
// feed's time), and tennis sides follow the broadcast's order (the feeds'
// when they name both players, else the draw's first-listed player), with
// sets and flags following the names. Every team and player is invented.
#include "core/catalog/catalog.hpp"
#include "core/sports/board.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/schedule.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::board;
using sportsdata::eventOf;
using sportsdata::feed;
using sportsdata::game;
using sportsdata::source;
using sportsdata::team;
using sportsdata::utc;
using State = SportsEvent::State;

namespace {

// Thursday 2026-11-05 16:45 UTC: 8:45 AM Pacific, 11:45 AM Eastern.
const Seconds kNow = utc(2026, 11, 5, 16, 45);

ChannelFor channels() {
  return [](const EventSource& s) -> std::optional<ChannelLabel> {
    return ChannelLabel{s.channelID, "Channel " + s.channelID, "Channel " + s.channelID, {}};
  };
}

Input baseInput(std::vector<SportsEvent> events, Seconds now = kNow) {
  Input in;
  in.events = std::move(events);
  in.now = now;
  in.zone = *time::Zone::named("America/Los_Angeles");
  in.channelFor = channels();
  return in;
}

const Tile& tile(const Board& b, std::string_view id) {
  auto it = b.tiles.find(std::string(id));
  if (it == b.tiles.end()) FAIL("no tile " << id);
  return it->second;
}

LiveStatus status(LivePhase phase, const std::string& detail) {
  LiveStatus s;
  s.phase = phase;
  s.detail = detail;
  return s;
}

EventSource feedFrom(const std::string& id, std::optional<Seconds> from) {
  EventSource s = feed(id);
  s.from = from;
  return s;
}

EventSource lineup(const std::string& id) {
  return source(id, EventSource::Kind::lineup, false, station_rank::lineup, "In My Lineup");
}

EventSource showing(const std::string& id, Seconds from, Seconds until) {
  EventSource s = lineup(id);
  s.from = from;
  s.until = until;
  return s;
}

// Round 1 of a tournament, from 11:00 UTC (the schedule's round start).
SportsGame golfRound() {
  SportsTeam side = team("g501", "Harbor Bay Championship");
  SportsGame g = game("PGA", "Golf", "501", side, side, utc(2026, 11, 5, 11, 0), {"Golf Net"});
  g.golf = true;
  return g;
}

bool hasRow(const Board& b, std::string_view id) {
  for (const Row& r : b.rows)
    if (r.id == id) return true;
  return false;
}

}  // namespace

TEST_CASE("a golf round in play whose feeds all start later is later, at the first feed's time") {
  SportsGame g = golfRound();
  // The exports' case: the round in play by the scoreboard at 8:45 AM
  // Pacific; the provider's feeds at noon and 1 PM; the network's channel
  // with no listing for it.
  Input in = baseInput(
      {eventOf(g, {lineup("e|golfnet"), feedFrom("xt|g1", utc(2026, 11, 5, 21, 0)), feedFrom("xt|g2", utc(2026, 11, 5, 20, 0))})});
  in.statuses["PGA|501"] = status(LivePhase::live, "Round 1");
  Board b = build(in);
  const Tile& t = tile(b, "sev|PGA|501");
  CHECK(t.state == State::upcoming);
  CHECK(t.pill == Pill::none);
  CHECK(t.statusText == "12:00 PM");
  CHECK(t.statusDetail == "in 3 h");
  CHECK(b.events.at("sev|PGA|501").start == utc(2026, 11, 5, 20, 0));
  CHECK_FALSE(hasRow(b, "live"));
  CHECK(b.hero.chip != HeroChip::live);

  // Once the first feed is on, live again (the scoreboard's round).
  in.now = utc(2026, 11, 5, 20, 1);
  Board on = build(in);
  CHECK(tile(on, "sev|PGA|501").state == State::live);
  CHECK(tile(on, "sev|PGA|501").statusText == "Round 1");

  // A channel whose guide listing shows the round now: on air.
  Input listed = baseInput({eventOf(g, {showing("e|golfnet", kNow - 1800, kNow + 1800), feedFrom("xt|g1", kNow + 3 * 3600)})});
  listed.statuses["PGA|501"] = status(LivePhase::live, "Round 1");
  CHECK(tile(build(listed), "sev|PGA|501").state == State::live);
  // Its listing later today: later, at the listing's start.
  Input later = baseInput({eventOf(g, {showing("e|golfnet", kNow + 1800, kNow + 5400), feedFrom("xt|g1", kNow + 3 * 3600)})});
  later.statuses["PGA|501"] = status(LivePhase::live, "Round 1");
  Board laterBoard = build(later);
  CHECK(tile(laterBoard, "sev|PGA|501").state == State::upcoming);
  CHECK(laterBoard.events.at("sev|PGA|501").start == kNow + 1800);

  // A feed with no time may be showing it: live, as before.
  Input untimed = baseInput({eventOf(g, {feedFrom("xt|g0", std::nullopt), feedFrom("xt|g1", kNow + 3 * 3600)})});
  untimed.statuses["PGA|501"] = status(LivePhase::live, "Round 1");
  CHECK(tile(build(untimed), "sev|PGA|501").state == State::live);
  // Only the network's channel, nothing timed: can't tell, live as before.
  Input network = baseInput({eventOf(g, {lineup("e|golfnet")})});
  network.statuses["PGA|501"] = status(LivePhase::live, "Round 1");
  CHECK(tile(build(network), "sev|PGA|501").state == State::live);
}

TEST_CASE("live only when on air: the scoreboard's word on a match stands, the clock's alone doesn't") {
  SportsGame nfl = game("NFL", "NFL", "9", team("h", "Harbor City Gulls"), team("a", "Team Alpha"), kNow - 3600);
  // The scoreboard says the game is being played: a feed dated later is the one that's off
  // (a tennis match watched at 6-4 4-4 while its slot said it started hours later).
  Input played = baseInput({eventOf(nfl, {feedFrom("xt|n", kNow + 4 * 3600)})});
  played.statuses["NFL|9"] = status(LivePhase::live, "Q2 1:00");
  CHECK(tile(build(played), "sev|NFL|9").state == State::live);
  // No scoreboard: on by the clock, but its one way to watch starts later.
  Board clock = build(baseInput({eventOf(nfl, {feedFrom("xt|n", kNow + 2 * 3600)})}));
  CHECK(tile(clock, "sev|NFL|9").state == State::upcoming);
  CHECK(clock.events.at("sev|NFL|9").start == kNow + 2 * 3600);
  // A station's listing that ended with nothing to come: as the clock says (an overrun).
  Board over = build(baseInput({eventOf(nfl, {showing("e|s", kNow - 3600, kNow - 600)})}));
  CHECK(tile(over, "sev|NFL|9").state == State::live);

  CHECK(onAirFrom(eventOf(nfl, {showing("e|s", kNow - 3600, kNow - 600)}), kNow) == std::nullopt);
  CHECK(onAirFrom(eventOf(nfl, {feedFrom("a", kNow + 600), feedFrom("b", kNow + 300)}), kNow) == kNow + 300);
  CHECK(onAirFrom(eventOf(nfl, {feedFrom("a", kNow + 600), feedFrom("b", kNow - 300)}), kNow) == std::nullopt);
  CHECK(onAirFrom(eventOf(nfl, {feedFrom("a", kNow + 600), lineup("e|net")}), kNow) == std::nullopt);
  CHECK(onAirFrom(eventOf(nfl), kNow) == std::nullopt);
}

TEST_CASE("tennis sides in the broadcast's order; sets, flags, hero, card and guide follow") {
  SportsTeam ann = team("p1", "Ann Player");  // home: first in the draw
  ann.flags = {"https://example.invalid/flags/esp.png"};
  SportsTeam bea = team("p2", "Bea Racket");
  bea.flags = {"https://example.invalid/flags/fra.png"};
  SportsGame match = game("ATP", "Tennis", "90|1", ann, bea, kNow - 1800);
  match.tennis = true;
  match.customTitle = "Player vs Racket";
  LiveStatus s = status(LivePhase::live, "Set 2");
  s.homeSets = std::vector<std::string>{"6", "4"};
  s.awaySets = std::vector<std::string>{"4", "4"};

  // No feed names both: the draw's first-listed player first.
  Input in = baseInput({eventOf(match, {feed("xt|t")})});
  in.statuses["ATP|90|1"] = s;
  Board b = build(in);
  const Tile& t = tile(b, "sev|ATP|90|1");
  REQUIRE(t.sides.size() == 2);
  CHECK(t.sides[0].fullName == "Ann Player");
  CHECK(t.sides[0].sets == std::vector<std::string>{"6", "4"});
  CHECK(t.sides[0].flags == std::vector<std::string>{"https://example.invalid/flags/esp.png"});
  CHECK(t.sides[1].fullName == "Bea Racket");
  CHECK(t.sides[1].sets == std::vector<std::string>{"4", "4"});
  CHECK(t.title == "Player vs Racket");
  CHECK(b.hero.badges.at(0).fullName == "Ann Player");
  CHECK(b.hero.title == "Player vs Racket");

  // The feeds name Bea first: so does the tile, with her sets and flag.
  SportsEvent swapped = eventOf(match, {feed("xt|t")});
  swapped.awayFirst = true;
  Input in2 = baseInput({swapped});
  in2.statuses["ATP|90|1"] = s;
  Board b2 = build(in2);
  const Tile& u = tile(b2, "sev|ATP|90|1");
  REQUIRE(u.sides.size() == 2);
  CHECK(u.sides[0].fullName == "Bea Racket");
  CHECK(u.sides[0].sets == std::vector<std::string>{"4", "4"});
  CHECK(u.sides[0].flags == std::vector<std::string>{"https://example.invalid/flags/fra.png"});
  CHECK(u.sides[1].fullName == "Ann Player");
  CHECK(u.sides[1].sets == std::vector<std::string>{"6", "4"});
  CHECK(u.sides[1].flags == std::vector<std::string>{"https://example.invalid/flags/esp.png"});
  CHECK(u.title == "Racket vs Player");
  CHECK(b2.hero.badges.at(0).fullName == "Bea Racket");
  CHECK(b2.hero.title == "Racket vs Player");
  DetailCard card = detailCard(b2.events.at("sev|ATP|90|1"), kNow, time::Zone::usEastern(), Settings{}, channels());
  CHECK(card.badges.at(0).fullName == "Bea Racket");
  CHECK(card.title == "Racket vs Player");
  // The in-player guide (and the game banner) show the tile's sides.
  EventGuide guide = eventGuide(b2, std::nullopt, std::string("sev|ATP|90|1"), Settings{}, time::Zone::usEastern());
  bool seen = false;
  for (const GuideItem& item : guide.items) {
    if (!item.game || item.game->id != "sev|ATP|90|1") continue;
    seen = true;
    REQUIRE(item.game->sides.size() == 2);
    CHECK(item.game->sides[0].fullName == "Bea Racket");
  }
  CHECK(seen);

  // A team sport keeps its own order (away first in American sports).
  SportsGame nfl = game("NFL", "NFL", "7", team("h", "Harbor City Gulls"), team("a", "Team Alpha"), kNow + 3600);
  CHECK(tile(build(baseInput({eventOf(nfl, {feed("xt|n")})})), "sev|NFL|7").sides[0].fullName == "Team Alpha");
}

TEST_CASE("matching: a tennis match takes the feeds' order; feeds keep their slot's time") {
  SportsTeam ann = team("p1", "Ann Player");
  ann.alternates = {"Player Ann"};
  SportsTeam bea = team("p2", "Bea Racket");
  bea.alternates = {"Racket Bea"};
  SportsGame match = game("ATP", "Tennis", "90|1", ann, bea, kNow + 3600);
  match.tennis = true;
  match.customTitle = "Player vs Racket";
  CHECK(matcher::sideNamedFirst("Bea Racket vs Ann Player · ATP Harbor", match) == 1);
  CHECK(matcher::sideNamedFirst("ANN PLAYER - BEA RACKET", match) == 0);
  CHECK(matcher::sideNamedFirst("ATP Harbor: Day 4", match) == std::nullopt);
  SportsTeam pairA = team("d1", "One / Twoson");
  pairA.alternates = {"Twoson"};
  SportsTeam pairB = team("d2", "Three / Fourer");
  pairB.alternates = {"Three", "Fourer"};
  SportsGame doubles = game("ATP", "Tennis", "90|2", pairA, pairB, kNow + 3600);
  doubles.tennis = true;
  CHECK(matcher::sideNamedFirst("Three T, Fourer F vs One O, Twoson T", doubles) == 1);

  auto event = [](const std::string& id, const std::string& title, Seconds start) {
    matcher::ProviderEvent e;
    e.id = id;
    e.title = title;
    e.league = "Tennis";
    e.start = start;
    e.feeds = {"xt|" + id};
    e.family = "fam|" + id;
    return e;
  };
  matcher::BuildInput in;
  in.games = {match};
  in.now = kNow;
  in.zone = time::Zone::usEastern();
  // Two feeds name Bea first, one Ann: Bea first.
  in.events = {event("a", "Bea Racket vs Ann Player", kNow + 3600), event("b", "RACKET vs PLAYER \xc2\xb7 ATP Harbor", kNow + 3600),
               event("c", "Ann Player vs Bea Racket", kNow + 3600)};
  auto built = matcher::build(in);
  REQUIRE(built);
  const SportsEvent* found = nullptr;
  for (const SportsEvent& e : built->events)
    if (e.game && e.game->id == "ATP|90|1") found = &e;
  REQUIRE(found);
  CHECK(found->awayFirst);
  CHECK(found->title == "Racket vs Player");
  REQUIRE(found->sources.size() == 3);
  for (const EventSource& s : found->sources) CHECK(s.from == kNow + 3600);

  // A tie, or no feed naming both: the draw's order.
  in.events = {event("a", "Bea Racket vs Ann Player", kNow + 3600), event("c", "Ann Player vs Bea Racket", kNow + 3600)};
  built = matcher::build(in);
  REQUIRE(built);
  for (const SportsEvent& e : built->events) {
    if (!e.game || e.game->id != "ATP|90|1") continue;
    CHECK_FALSE(e.awayFirst);
    CHECK(e.title == "Player vs Racket");
  }
}

TEST_CASE("a source's window and the tennis order survive saving") {
  SportsGame match = game("ATP", "Tennis", "90|1", team("p1", "Ann Player"), team("p2", "Bea Racket"), kNow);
  match.tennis = true;
  SportsEvent e = eventOf(match, {showing("e|t", kNow - 60, kNow + 3600), feedFrom("xt|t", kNow + 600)});
  e.awayFirst = true;
  auto back = eventFromJson(toJson(e));
  REQUIRE(back);
  CHECK(*back == e);
  CHECK(back->awayFirst);
  CHECK(back->sources[0].until == kNow + 3600);
  CHECK(back->sources[1].from == kNow + 600);
  CHECK_FALSE(back->sources[1].until);
}

TEST_CASE("slot times: the tennis service writes Central European time; a slot of only its service tag is empty") {
  const Seconds now = utc(2026, 11, 5, 16, 45);
  // A Paris night match at 19:00: 18:00 UTC (CET), not 19:00 Eastern.
  auto night = catalog::parseEventDetailed("Alpha, Anna vs Bravo, Bea @ Nov 5 19:00 PM - ATP Paris :Tennis  18", now,
                                           std::string("US"));
  CHECK(night.title == "Anna Alpha vs Bea Bravo \xc2\xb7 ATP Paris");
  CHECK(night.start == utc(2026, 11, 5, 18, 0));
  auto day = catalog::parseEventDetailed("Alpha A, Bravo B vs Charlie C, Delta D @ Nov 5 10:30 AM - WTA Harbor :Tennis  01",
                                         now, std::string("US"));
  CHECK(day.start == utc(2026, 11, 5, 9, 30));
  // A country's service still reads its own country's clock.
  CHECK(catalog::parseEventDetailed("Golf: Harbor Bay Championship: Day 1 @ Nov 5 21:00 PM :MAX DK  06", now,
                                    std::string("US"))
            .start == utc(2026, 11, 5, 20, 0));
  // ":Golf  02" with nothing else is an empty slot, not an event called "Golf 02".
  CHECK(catalog::parseEventDetailed(":Golf  02", now, std::string("US")).title.empty());
  CHECK(catalog::parseEventDetailed(":Tennis  40", now, std::string("US")).title.empty());
}
