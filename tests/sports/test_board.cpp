// The board view model: row order (Your teams, Live now with whip-arounds
// first, sports in My Sports or the fixed order, More events, Time not
// listed, Event channels), groups inside a row, which games a row shows,
// every status text and footer rule, sides, fight cards, the hero, Where to
// Watch, the details card, the sports column and the in-player event guide,
// with games in US Eastern time and viewers in other zones.
#include <algorithm>

#include "core/sports/board.hpp"
#include "core/sports/race_data.hpp"
#include "core/util/strings.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::board;
using sportsdata::eventOf;
using sportsdata::feed;
using sportsdata::game;
using sportsdata::plainEvent;
using sportsdata::source;
using sportsdata::team;
using sportsdata::utc;
using State = SportsEvent::State;

namespace {

// Saturday 2026-10-24 22:00 UTC: 6:00 PM in New York.
const Seconds kNow = utc(2026, 10, 24, 22, 0);

ChannelFor channels() {
  return [](const EventSource& s) -> std::optional<ChannelLabel> {
    if (s.channelID == "gone") return std::nullopt;
    std::string name = s.channelID == "e|fox13" ? "FOX 13 Harbor" : "Channel " + s.channelID;
    return ChannelLabel{s.channelID, name, s.channelID == "e|fox13" ? "FOX 13" : name,
                        {"https://example.invalid/logo/" + s.channelID + ".png"}};
  };
}

Input baseInput(std::vector<SportsEvent> events) {
  Input in;
  in.events = std::move(events);
  in.now = kNow;
  in.zone = time::Zone::usEastern();
  in.channelFor = channels();
  return in;
}

std::vector<std::string> rowIDs(const Board& b) {
  std::vector<std::string> out;
  for (const Row& r : b.rows) out.push_back(r.id);
  return out;
}

// string_view parameters: a reference parameter bound to a temporary
// string would trip -Wdangling-reference at every call.
const Row& row(const Board& b, std::string_view id) {
  for (const Row& r : b.rows) {
    if (r.id == id) return r;
  }
  FAIL("no row " << id);
  return b.rows.front();
}

std::vector<std::string> groupLabels(const Row& r) {
  std::vector<std::string> out;
  for (const Group& g : r.groups) out.push_back(g.label);
  return out;
}

const Tile& tile(const Board& b, std::string_view id) {
  auto it = b.tiles.find(std::string(id));
  if (it == b.tiles.end()) FAIL("no tile " << id);
  return it->second;
}

EventSource lineup(const std::string& id, int rank = station_rank::lineup) {
  return source(id, EventSource::Kind::lineup, false, rank, "In My Lineup");
}
EventSource station(const std::string& id, int rank = station_rank::elsewhere, bool spanish = false) {
  return source(id, EventSource::Kind::guide, spanish, rank, "In your channels");
}

SportsGame nflAt(const std::string& id, Seconds start, std::vector<std::string> networks = {"FOX"}) {
  return game("NFL", "NFL", id, team("h" + id, "Harbor City Gulls", std::string("HCG")),
              team("a" + id, "Team Alpha", std::string("TMA")), start, std::move(networks),
              std::string("Gull Field, Harbor City"));
}

LiveStatus status(LivePhase phase, const std::string& detail) {
  LiveStatus s;
  s.phase = phase;
  s.detail = detail;
  return s;
}

}  // namespace

TEST_CASE("row order without My Sports: live, the fixed sport order, then A-Z, untimed, event channels") {
  SportsGame nfl = nflAt("1", kNow + 2 * 3600);
  SportsGame nba = game("NBA", "NBA", "2", team("3", "River Town Otters"), team("4", "Valley Hawks"), kNow - 1800);
  SportsGame epl = game("EPL", "Soccer", "3", team("5", "Harbor United"), team("6", "Riverside Rovers"),
                        kNow + 86'400, {}, std::nullopt, true);
  std::vector<SportsEvent> events = {
      eventOf(nfl, {lineup("e|fox13")}),
      eventOf(nba, {station("e|espn")}),
      eventOf(epl, {feed("xt|1")}),
      plainEvent("rugby", "Rugby: Harbor Sharks vs Coast Kings", "Rugby", kNow + 3600, {feed("xt|2")}),
      plainEvent("golf", "Golf: Final Round", "Golf", kNow + 3600, {feed("xt|3")}),
      plainEvent("slot", "Mystery Event", "", std::nullopt, {feed("xt|4")}),
      plainEvent("empty", "Event 4: No Event Scheduled", "", kNow, {feed("xt|5")}),
  };
  Input in = baseInput(events);
  catalog::EventFamily family;
  family.id = "fam1";
  family.name = "Event Pass";
  family.feeds = {"xt|1", "xt|2", "xt|3"};
  family.league = "Soccer";
  in.families = {family};
  Board b = build(in);
  CHECK(rowIDs(b) == std::vector<std::string>{"live", "sport|NFL", "sport|NBA", "sport|Soccer", "sport|Golf",
                                              "sport|Rugby", "untimed", "families"});
  CHECK(row(b, "live").title == "Live now");
  CHECK(row(b, "untimed").title == "Time not listed");
  const Row& fam = row(b, "families");
  CHECK(fam.title == "Event channels");
  REQUIRE(fam.families.size() == 1);
  CHECK(fam.families[0].countText == "3 feeds");
  CHECK(fam.families[0].leagueChip == "SOCCER");
  CHECK(fam.families[0].hint == "Up/Down to surf");
  CHECK(fam.summary == "\xC2\xB7  1");
  // Placeholders aren't events.
  CHECK_FALSE(b.tiles.count("empty"));
  // The NBA row: one live game.
  const Row& nbaRow = row(b, "sport|NBA");
  CHECK(nbaRow.live == 1);
  CHECK(nbaRow.summary == "\xC2\xB7  1  \xC2\xB7  1 live");
  CHECK(row(b, "live").summary == "\xC2\xB7  1");  // the live row doesn't repeat it
  auto column = sportsColumn(b);
  CHECK(column[0].title == "Live now");
  CHECK_FALSE(column[0].liveText);
  CHECK(column[2].liveText == "1 live");
  CHECK(column[2].count == 1);
  CHECK(b.emptyMessage.empty());
  CHECK(build(baseInput({})).emptyMessage == "No upcoming events found in your channels right now.");
}

TEST_CASE("row order with My Sports: your teams, picked sports in order, everything else in More events") {
  SportsGame nfl = nflAt("1", kNow + 2 * 3600);
  SportsGame nba = game("NBA", "NBA", "2", team("3", "River Town Otters"), team("4", "Valley Hawks"), kNow - 1800);
  SportsGame nhl = game("NHL", "NHL", "3", team("5", "Lake Pikes"), team("6", "Mountain Goats"), kNow - 1800);
  SportsGame nhl2 = game("NHL", "NHL", "4", team("7", "Coast Crabs"), team("8", "Desert Foxes"), kNow + 3600);
  Input in = baseInput({eventOf(nfl, {lineup("e|fox13")}), eventOf(nba, {station("e|espn")}),
                        eventOf(nhl, {station("e|nhl")}), eventOf(nhl2, {}),
                        plainEvent("golf", "Golf: Final Round", "Golf", kNow + 3600, {feed("xt|3")})});
  in.prefs.configured = true;
  in.prefs.leagues = {"NBA", "NFL"};
  in.prefs.teams = {teamKey("NFL", "h1")};
  Board b = build(in);
  CHECK(rowIDs(b) == std::vector<std::string>{"teams", "live", "sport|NBA", "sport|NFL", "more"});
  CHECK(row(b, "teams").title == "Your teams");
  CHECK(row(b, "teams").mixed);
  // Live now: picked sports only.
  CHECK(row(b, "live").eventIDs == std::vector<std::string>{"sev|NBA|2"});
  // More events: what you can watch of the rest (the NHL game no channel has is left out).
  const Row& more = row(b, "more");
  CHECK(more.title == "More events");
  CHECK(more.eventIDs == std::vector<std::string>{"sev|NHL|3", "golf"});
  CHECK(more.live == 1);
  CHECK(tile(b, "sev|NFL|1").favorite);
  // A mixed row names each tile's league when it has no tag.
  CHECK(row(b, "more").groups[0].tiles[0].leagueChip == "NHL");
}

TEST_CASE("groups inside a row and which games a row shows") {
  std::vector<SportsEvent> events;
  auto add = [&](const std::string& id, Seconds start, std::vector<EventSource> sources) {
    SportsEvent e = eventOf(nflAt(id, start), std::move(sources));
    events.push_back(e);
    return e.id;
  };
  std::string live = add("live", kNow - 3600, {lineup("e|1")});
  std::string ended = add("ended", kNow - 4 * 3600 - 1800, {lineup("e|2")});  // over 3.6 h + 30 min ago: gone
  std::string earlier = add("earlier", kNow - 3 * 3600 - 1800 - 1200, {lineup("e|3")});  // may have ended
  std::string soon = add("soon", kNow + 1800, {lineup("e|4")});
  std::string today = add("today", kNow + 3 * 3600, {lineup("e|5")});       // 9 PM Eastern
  std::string tomorrow = add("tomorrow", kNow + 86'400, {lineup("e|6")});
  std::string monday = add("monday", kNow + 2 * 86'400, {lineup("e|7")});
  std::string far = add("far", kNow + 9 * 86'400, {lineup("e|8")});         // past 8 days
  std::string unwatchable = add("none", kNow + 3600, {});
  std::string final = add("final", kNow - 2 * 3600, {lineup("e|9")});
  SportsEvent postponed = eventOf(nflAt("pp", kNow + 7200), {lineup("e|10")});
  postponed.game->postponed = true;
  events.push_back(postponed);
  events.push_back(plainEvent("show", "NFL Countdown", "NFL", kNow - 600, {feed("xt|s")}));
  Input in = baseInput(events);
  in.statuses["NFL|final"] = status(LivePhase::final, "Final");
  in.finalSeen["NFL|final"] = kNow - 10 * 60;
  Board b = build(in);
  const Row& r = row(b, "sport|NFL");
  CHECK(r.grouped);
  CHECK(groupLabels(r) == std::vector<std::string>{"Live", "Final", "Earlier", "Starting Soon", "Later Today",
                                                   "Tomorrow", "Monday", "Shows", "Postponed",
                                                   "Not in your channels"});
  CHECK(std::count(r.eventIDs.begin(), r.eventIDs.end(), ended) == 0);
  CHECK(std::count(r.eventIDs.begin(), r.eventIDs.end(), far) == 0);
  CHECK(r.groups[0].tiles[0].id == live);
  // The studio show is never Live now.
  CHECK(row(b, "live").eventIDs == std::vector<std::string>{live});
  CHECK(tile(b, "show").studio);
  CHECK(tile(b, "show").tag == "STUDIO");
  CHECK(tile(b, "show").pill == Pill::onNow);
  CHECK(tile(b, "show").pillText == "ON NOW");
  // Finished over 30 minutes ago: off the board.
  in.finalSeen["NFL|final"] = kNow - 40 * 60;
  Board later = build(in);
  const Row& laterRow = row(later, "sport|NFL");
  CHECK(std::count(laterRow.eventIDs.begin(), laterRow.eventIDs.end(), "sev|NFL|final") == 0);
  // Games not in your channels only with the setting on.
  in.settings.showsUnwatchable = false;
  Board hidden = build(in);
  CHECK(groupLabels(row(hidden, "sport|NFL")).back() == "Postponed");
  CHECK_FALSE(hidden.tiles.count(unwatchable));
}

TEST_CASE("status texts: ends ~, started, an upcoming time and how soon, due, was, ended") {
  SportsGame liveGame = nflAt("1", kNow - 3600);
  SportsGame clockGame = nflAt("2", kNow - 3600);
  SportsGame earlier = nflAt("3", kNow - 4 * 3600 + 600);
  SportsGame soon = nflAt("4", kNow + 25 * 60);
  SportsGame later = nflAt("5", kNow + 2 * 3600 + 10 * 60);
  SportsGame late = nflAt("6", kNow - 15 * 60);
  SportsGame pp = nflAt("7", kNow + 3600);
  pp.postponed = true;
  SportsGame fin = nflAt("8", kNow - 3 * 3600);
  SportsEvent withListing = eventOf(liveGame, {lineup("e|1")});
  withListing.listingEnd = *withListing.start + 4 * 3600;  // 9:00 PM Eastern
  Input in = baseInput({withListing, eventOf(clockGame, {lineup("e|2")}), eventOf(earlier, {lineup("e|3")}),
                        eventOf(soon, {lineup("e|4")}), eventOf(later, {lineup("e|5")}), eventOf(late, {lineup("e|6")}),
                        eventOf(pp, {lineup("e|7")}), eventOf(fin, {lineup("e|8")})});
  LiveStatus q3 = status(LivePhase::live, "Q3 4:12");
  q3.bases = std::vector<bool>{true, false, false};
  in.statuses["NFL|2"] = q3;
  in.statuses["NFL|6"] = status(LivePhase::pre, "10/24 - 5:45 PM EDT");
  in.statuses["NFL|8"] = status(LivePhase::final, "Final/OT");
  in.finalSeen["NFL|8"] = utc(2026, 10, 24, 21, 52);
  Board b = build(in);
  const Tile& live = tile(b, withListing.id);
  CHECK(live.pill == Pill::live);
  CHECK(live.pillText == "LIVE");
  CHECK(live.statusText == "ends ~9:00");
  REQUIRE(live.progress);
  CHECK(*live.progress == doctest::Approx(3600.0 / 12'960));
  const Tile& clock = tile(b, "sev|NFL|2");
  CHECK(clock.statusText == "Q3 4:12");
  CHECK(clock.bases == std::vector<bool>{true, false, false});
  // From the game clock: a quarter of 900 s, third quarter, 4:12 left.
  CHECK(*clock.progress == doctest::Approx((2 + 1 - 252.0 / 900) / 4));
  const Tile& maybe = tile(b, "sev|NFL|3");
  CHECK(maybe.state == State::mayHaveEnded);
  CHECK(maybe.pill == Pill::likelyOver);
  CHECK(maybe.pillText == "Likely over");
  CHECK(maybe.statusText == "started 2:10");
  const Tile& s = tile(b, "sev|NFL|4");
  CHECK(s.pill == Pill::none);
  CHECK(s.statusText == "6:25 PM");
  CHECK(s.statusDetail == "in 25 m");
  CHECK(tile(b, "sev|NFL|5").statusDetail == "in 2 h");
  const Tile& due = tile(b, "sev|NFL|6");
  CHECK(due.state == State::upcoming);  // the scoreboard says not started
  CHECK(due.pill == Pill::notStarted);
  CHECK(due.pillText == "Not started");
  CHECK(due.statusText == "due 5:45");
  CHECK_FALSE(due.progress);
  const Tile& was = tile(b, "sev|NFL|7");
  CHECK(was.pill == Pill::postponed);
  CHECK(was.statusText == "was 7:00 PM");
  const Tile& ended = tile(b, "sev|NFL|8");
  CHECK(ended.state == State::finished);
  CHECK(ended.pillText == "Final/OT");
  CHECK(ended.statusText == "ended 5:52");
  // 24-hour clocks.
  in.settings.twelveHour = false;
  Board h24 = build(in);
  CHECK(tile(h24, "sev|NFL|4").statusText == "18:25");
  CHECK(tile(h24, "sev|NFL|8").statusText == "ended 17:52");
  // Not in your channels: an outlined LIVE pill, dimmed.
  Input none = baseInput({eventOf(nflAt("9", kNow - 600))});
  Board noneBoard = build(none);
  const Tile& dim = tile(noneBoard, "sev|NFL|9");
  CHECK(dim.pill == Pill::liveOutlined);
  CHECK_FALSE(dim.watchable);
  // Delayed.
  Input delayed = baseInput({eventOf(nflAt("10", kNow - 600), {lineup("e|1")})});
  delayed.statuses["NFL|10"] = status(LivePhase::delayed, "Rain Delay");
  CHECK(tile(build(delayed), "sev|NFL|10").pillText == "DELAYED");
  // Suspended, to resume later (the scores say so for sports that resume).
  LiveStatus halted = status(LivePhase::delayed, "Suspended - Rain");
  halted.suspended = true;
  delayed.statuses["NFL|10"] = halted;
  Board halt = build(delayed);
  const Tile& suspended = tile(halt, "sev|NFL|10");
  CHECK(suspended.pill == Pill::delayed);
  CHECK(suspended.pillText == "SUSPENDED");
  CHECK(suspended.state == State::live);
}

TEST_CASE("times in the viewer's zone: an Eastern evening game seen from London") {
  // 8:30 PM Eastern Saturday is 1:30 AM Sunday in London.
  SportsGame g = nflAt("1", utc(2026, 10, 25, 0, 30));
  Input in = baseInput({eventOf(g, {lineup("e|1")})});
  Board ny = build(in);
  CHECK(tile(ny, "sev|NFL|1").statusText == "8:30 PM");
  CHECK(groupLabels(row(ny, "sport|NFL")) == std::vector<std::string>{"Later Today"});
  in.zone = time::Zone::london();
  Board london = build(in);
  CHECK(tile(london, "sev|NFL|1").statusText == "1:30 AM");
  CHECK(groupLabels(row(london, "sport|NFL")) == std::vector<std::string>{"Tomorrow"});
  // Sydney: 11:30 AM Sunday, and it's already Sunday morning there.
  in.zone = *time::Zone::named("Australia/Sydney");
  Board sydney = build(in);
  CHECK(tile(sydney, "sev|NFL|1").statusText == "11:30 AM");
  CHECK(groupLabels(row(sydney, "sport|NFL")) == std::vector<std::string>{"Later Today"});
}

TEST_CASE("footers: local station, lineup, Spanish, backup feeds, streaming, feeds only, not yours") {
  auto footerOf = [](std::vector<EventSource> sources, std::vector<std::string> networks = {"FOX"},
                     std::vector<catalog::EventFamily> families = {}) {
    SportsEvent e = eventOf(nflAt("1", kNow + 3600, std::move(networks)), std::move(sources));
    e.family = std::string("fam1");
    Input in = baseInput({e});
    in.families = std::move(families);
    Board b = build(in);
    return tile(b, e.id);
  };
  Tile local = footerOf({lineup("e|fox13", station_rank::lineupHome), feed("xt|1")});
  CHECK(local.footer == "Your local station");
  CHECK(local.channelName == "FOX 13 Harbor");
  CHECK(local.channelDisplay == "FOX 13");
  CHECK(local.channelLogos == std::vector<std::string>{"https://example.invalid/logo/e|fox13.png"});
  CHECK(local.inLineup);
  CHECK(footerOf({station("e|kstu", station_rank::home)}).footer == "Your local station");
  CHECK(footerOf({lineup("e|fox13")}).footer == "In your lineup");
  CHECK(footerOf({station("e|a"), station("e|b", station_rank::elsewhere, true)}).footer == "Also in Spanish");
  CHECK(footerOf({station("e|a"), feed("xt|1"), feed("xt|2")}).footer == "+ 2 backup feeds");
  CHECK(footerOf({station("e|a")}, {"FOX", "Peacock"}).footer == "Also streaming on Peacock");
  CHECK(footerOf({station("e|a")}).footer.empty());
  // A team's channel is named only when no feed has the game itself.
  Tile team = footerOf({source("e|team", EventSource::Kind::team), feed("xt|1")});
  CHECK_FALSE(team.channelName);
  CHECK(team.footerPill == "Event feed");
  CHECK(team.footer == "1 feed");
  CHECK(footerOf({source("e|team", EventSource::Kind::team)}).channelName == "Channel e|team");
  // Feeds only: the event channel family's name (when short) and a count.
  catalog::EventFamily family;
  family.id = "fam1";
  family.name = "Event Pass";
  Tile feeds = footerOf({feed("xt|1"), feed("xt|2")}, {"FOX"}, {family});
  CHECK(feeds.footerPill == "Event Pass");
  CHECK_FALSE(feeds.footerPillOutlined);
  CHECK(feeds.footer == "2 feeds");
  CHECK(footerOf({feed("xt|1"), feed("xt|2", true)}).footer == "Also in Spanish");
  // Nothing you have: the network, outlined.
  Tile outlined = footerOf({});
  CHECK(outlined.footerPill == "FOX");
  CHECK(outlined.footerPillOutlined);
  CHECK(outlined.footer == "Not in your channels");
  CHECK(footerOf({}, {"NBA League Pass"}).footer == "Streaming only");
  CHECK(footerOf({}, {}).footer == "Not in your channels");
  CHECK(footerOf({source("gone", EventSource::Kind::guide)}, {}).footer == "Not in your channels");
}

TEST_CASE("sides: short names, abbreviations, colors, scores, possession, ranks; soccer home first") {
  SportsGame g = nflAt("1", kNow - 3600);
  g.home.color = "FFCB05";
  g.home.altColor = "00274C";
  g.home.badge = "https://example.invalid/hcg.png";
  g.away.rank = 7;
  Input in = baseInput({eventOf(g, {lineup("e|1")})});
  LiveStatus s = status(LivePhase::live, "Q2 1:00");
  s.homeScore = "17";
  s.awayScore = "21";
  s.possession = "h1";
  s.situation = "3rd & 7 at HCG 35";
  s.redZone = true;
  in.statuses["NFL|1"] = s;
  Board b = build(in);
  const Tile& t = tile(b, "sev|NFL|1");
  CHECK(t.layout == Tile::Layout::sides);
  REQUIRE(t.sides.size() == 2);
  const SideLine& top = t.sides[0];
  const SideLine& bottom = t.sides[1];
  CHECK(top.name == "Alpha");  // away on top, its nickname
  CHECK(top.abbreviation == "TMA");
  CHECK(top.rank == 7);
  CHECK(top.score == "21");
  CHECK(top.leads == true);
  CHECK_FALSE(top.home);
  CHECK(bottom.name == "Gulls");
  CHECK(bottom.home);
  CHECK(bottom.possession);
  CHECK(bottom.leads == false);
  CHECK(bottom.lightColor);
  CHECK(bottom.logoUrl == "https://example.invalid/hcg.png");
  CHECK(t.situation == "3rd & 7 at HCG 35");
  CHECK(t.redZone);
  CHECK(t.tag == "RED ZONE");
  CHECK(t.tagAlert);
  // Scores off: the clock only.
  in.settings.showsScores = false;
  Board quiet = build(in);
  CHECK_FALSE(tile(quiet, "sev|NFL|1").sides[0].score);
  CHECK_FALSE(tile(quiet, "sev|NFL|1").situation);
  CHECK(tile(quiet, "sev|NFL|1").statusText == "Q2 1:00");
  // Soccer lists the home side first.
  SportsGame soccer = game("EPL", "Soccer", "9", team("5", "Harbor United"), team("6", "Riverside Rovers"),
                           kNow + 3600, {}, std::nullopt, true);
  Board sb = build(baseInput({eventOf(soccer, {lineup("e|1")})}));
  CHECK(tile(sb, "sev|EPL|9").sides[0].name == "Harbor United");
  CHECK(tile(sb, "sev|EPL|9").sides[0].home);
  // An event with no game: sides from "A vs B", or a title and subtitle.
  Board plain = build(baseInput({plainEvent("p1", "Rugby: Harbor Sharks vs Coast Kings", "Rugby", kNow + 3600,
                                            {feed("x")}),
                                 plainEvent("p2", "F1: Harbor Grand Prix", "Motorsport", kNow + 3600, {feed("y")}),
                                 plainEvent("p3", "Golf Championship Final Round", "Golf", kNow + 3600, {feed("z")})}));
  CHECK(tile(plain, "p1").sides.size() == 2);
  CHECK(tile(plain, "p1").sides[0].name == "Harbor Sharks");
  CHECK(tile(plain, "p2").layout == Tile::Layout::plain);
  CHECK(tile(plain, "p2").title == "Harbor Grand Prix");
  CHECK(tile(plain, "p2").subtitle == "F1");
  CHECK(tile(plain, "p2").icon == "flag.checkered");
  CHECK(tile(plain, "p3").subtitle == "Golf");
}

TEST_CASE("fight cards become one tile: parts, the part on now, what Select plays") {
  auto part = [](const std::string& key, Seconds start, std::vector<EventSource> sources) {
    SportsGame g = game("UFC", "Fighting", "600|" + key, team("f1", "Max Stone"), team("f2", "Lee River"), start);
    g.cardKey = "ufc|333|" + key;
    g.customTitle = key == "main" ? "UFC 333: Stone vs. River" : "UFC 333 Prelims";
    SportsEvent e = eventOf(g, std::move(sources));
    return e;
  };
  SportsEvent prelims = part("prelims", kNow - 3600, {feed("xt|p")});
  SportsEvent main = part("main", kNow + 3600, {feed("xt|m")});
  Input in = baseInput({prelims, main});
  LiveStatus p = status(LivePhase::live, "fight 3 of 5");
  p.boutsDone = 2;
  p.boutsTotal = 5;
  p.boutA = "Ann One";
  p.boutB = "Bea Two";
  in.statuses["UFC|600|prelims"] = p;
  in.statuses["UFC|600|main"] = status(LivePhase::pre, "");
  Board b = build(in);
  REQUIRE(b.cards.count("card|UFC|600"));
  CHECK(b.cards.at("card|UFC|600").size() == 2);
  CHECK_FALSE(b.tiles.count(prelims.id));
  const Tile& t = tile(b, "card|UFC|600");
  CHECK(t.layout == Tile::Layout::card);
  CHECK(t.state == State::live);
  REQUIRE(t.parts.size() == 2);
  CHECK(t.parts[0].label == "Prelims");
  CHECK(t.parts[0].current);
  CHECK(t.parts[0].fill == doctest::Approx(0.4));
  CHECK(t.parts[0].text == "Prelims 5:00");
  CHECK(t.parts[1].label == "Main");
  CHECK(t.parts[1].fill == 0);
  CHECK(t.partLabel == "Prelims \xC2\xB7 fight 3 of 5");
  CHECK(t.statusText == "Prelims \xC2\xB7 fight 3 of 5");
  CHECK(t.boutLine == "Now: One vs. Two");
  CHECK(t.title == "UFC 333");
  CHECK(t.footerPill == "Event feed");
  CHECK(t.footer == "1 feed");
  CHECK(t.selectWatches);
  CHECK(t.playID == prelims.id);  // the part on now
  CHECK(matches(b, "card|UFC|600", main.id));
  CHECK(cardCurrent(b, "card|UFC|600", in.statuses, kNow)->id == prelims.id);
  WherePanel where = wherePanel(b, b.events.at("card|UFC|600"), channels());
  REQUIRE(where.sections.size() == 2);
  CHECK(where.sections[0].title == "Prelims");
  CHECK(where.sections[1].title == "Main Card");
  CHECK(where.sections[1].playEventID == main.id);
}

TEST_CASE("whip-arounds lead Live now when enough games are on") {
  Seconds sunday = utc(2026, 10, 25, 18, 0);  // 2 PM Eastern
  SportsGame g1 = nflAt("1", sunday - 3600);
  SportsGame g2 = nflAt("2", sunday - 3000);
  std::vector<WhipChannel> whips = whipChannels({[] {
                                                  Channel c;
                                                  c.id = "e|rz";
                                                  c.name = "NFL RedZone HD";
                                                  return c;
                                                }()},
                                                {"e|rz"});
  REQUIRE(whips.size() == 1);
  CHECK(whips[0].whip.title == "NFL RedZone");
  Input in = baseInput({eventOf(g1, {lineup("e|1")}), eventOf(g2, {lineup("e|2")})});
  in.now = sunday;
  in.whips = whips;
  LiveStatus rz = status(LivePhase::live, "Q2 5:00");
  rz.redZone = true;
  rz.possession = "a2";
  in.statuses["NFL|2"] = rz;
  Board b = build(in);
  CHECK(row(b, "live").eventIDs.front() == "whip|NFL");
  const Tile& w = tile(b, "whip|NFL");
  CHECK(w.tag == "WHIP-AROUND");
  CHECK(w.title == "NFL RedZone");
  CHECK(w.subtitle == "Red zone: Alpha");
  CHECK(w.footer == "2 games live");
  CHECK(groupLabels(row(b, "sport|NFL")).front() == "Whip-around");
  // The hero shows a game, not the whip-around.
  CHECK(b.heroID == std::optional<std::string>("sev|NFL|1"));
  // RedZone only on Sunday afternoons (Eastern).
  in.now = sunday - 86'400;
  in.events = {eventOf(nflAt("1", in.now - 3600), {lineup("e|1")})};
  CHECK_FALSE(build(in).tiles.count("whip|NFL"));
  CHECK(whipNamed("MLB Strike Zone")->minimumLive == 2);
  CHECK(whipNamed("Bundesliga Konferenz")->group == "Soccer");
  CHECK_FALSE(whipNamed("Harbor News"));
}

TEST_CASE("the hero: the first live game, its score and where it's on") {
  SportsGame g = nflAt("1", kNow - 3600);
  g.weekLabel = "Week 8";
  Input in = baseInput({eventOf(g, {lineup("e|fox13"), feed("xt|1"), feed("xt|2")})});
  LiveStatus s = status(LivePhase::live, "Q3 4:12");
  s.homeScore = "17";
  s.awayScore = "24";
  s.situation = "1st & 10";
  in.statuses["NFL|1"] = s;
  Board b = build(in);
  REQUIRE(b.heroID);
  const Hero& h = b.hero;
  CHECK_FALSE(h.empty);
  CHECK(h.leagueLine == "NFL  \xC2\xB7  Week 8");
  CHECK(h.title == "Alpha 24  \xE2\x80\x93  17 Gulls");
  CHECK(h.chip == HeroChip::live);
  CHECK(h.statusText == "Q3 4:12   \xC2\xB7   1st & 10");
  CHECK(h.whereText == "On FOX 13 Harbor, in your lineup   \xC2\xB7   2 backup feeds from your provider");
  CHECK(h.clockText == "6:00 PM");
  // Upcoming: the time, how soon, the venue.
  Board later = build(baseInput({eventOf(nflAt("2", kNow + 90 * 60), {station("e|9")})}));
  CHECK(later.hero.chip == HeroChip::later);
  CHECK(later.hero.statusText == "7:30 PM   \xC2\xB7   Starts in 1 h 30 m   \xC2\xB7   Gull Field, Harbor City");
  CHECK(later.hero.whereText == "On Channel e|9");
  // Nothing to show.
  CHECK(build(baseInput({})).hero.empty);
}

TEST_CASE("Where to Watch and the details card") {
  SportsGame g = nflAt("1", kNow + 26 * 3600);  // Sunday 8 PM Eastern
  SportsEvent e = eventOf(g, {lineup("e|fox13"), station("e|tele", station_rank::elsewhere, true), feed("xt|1"),
                              feed("xt|2", true)});
  e.moreStations = 3;
  Board b = build(baseInput({e}));
  WherePanel where = wherePanel(b, e, channels(), [](const std::string& id) { return id == "xt|1" ? "1080p" : ""; });
  CHECK(where.title == "Alpha at Gulls");
  REQUIRE(where.sections.size() == 3);
  CHECK(where.sections[0].title == "Your channels");
  CHECK(where.sections[0].items[0].name == "FOX 13 Harbor");
  CHECK(where.sections[1].title == "Spanish");
  CHECK(where.sections[1].items.size() == 2);
  CHECK(where.sections[2].title == "Event feeds from your provider");
  CHECK(where.sections[2].items[0].detail == "Provider event channel \xC2\xB7 1080p");
  CHECK(where.moreStations == "3 more stations in your channels are showing it");
  CHECK(where.hint == "Whichever you pick, the others stay as automatic backups.");
  DetailCard card = detailCard(e, kNow, time::Zone::usEastern(), Settings(), channels());
  CHECK(card.summary ==
        "Tomorrow 8:00 PM  \xC2\xB7  Gull Field, Harbor City  \xC2\xB7  On FOX 13 Harbor  \xC2\xB7  2 feeds from your provider");
  CHECK(card.watchLabel == "Watch FOX 13 Harbor Now");
  CHECK(card.canRemind);
  CHECK_FALSE(card.notInChannels);
  SportsEvent tonight = eventOf(nflAt("2", kNow + 2 * 3600));
  DetailCard none = detailCard(tonight, kNow, time::Zone::usEastern(), Settings(), channels());
  CHECK(none.summary.rfind("Tonight 8:00 PM", 0) == 0);
  CHECK(none.summary.find("On FOX") != std::string::npos);
  CHECK(none.notInChannels ==
        "On FOX \xC2\xB7 not in your channels. If one of your channels or event feeds picks it up, it'll show here.");
  CHECK_FALSE(none.whereToWatch);
  DetailCard monday = detailCard(eventOf(nflAt("3", kNow + 50 * 3600)), kNow, time::Zone::usEastern(), Settings(), channels());
  CHECK(monday.summary.rfind("Monday 8:00 PM", 0) == 0);
}

TEST_CASE("the in-player event guide: the sport of the game on screen, under its groups") {
  SportsGame live1 = nflAt("1", kNow - 3600);
  SportsGame live2 = nflAt("2", kNow - 1800);
  SportsGame later = nflAt("3", kNow + 1800);
  SportsGame none = nflAt("4", kNow + 3600);
  SportsGame nba = game("NBA", "NBA", "5", team("3", "River Town Otters"), team("4", "Valley Hawks"), kNow - 600);
  Input in = baseInput({eventOf(live1, {lineup("e|fox13")}), eventOf(live2, {feed("xt|2")}),
                        eventOf(later, {station("e|9")}), eventOf(none), eventOf(nba, {station("e|espn")}),
                        plainEvent("slot", "Mystery", "", std::nullopt, {feed("xt|s")})});
  in.statuses["NFL|1"] = status(LivePhase::live, "Q2 3:00");
  catalog::EventFamily fam;
  fam.id = "fam";
  fam.name = "Pass";
  in.families = {fam};
  Board b = build(in);
  std::vector<std::string> rows;
  for (const Row* r : guideRows(b)) rows.push_back(r->id);
  CHECK(rows == std::vector<std::string>{"live", "sport|NFL", "sport|NBA"});
  CHECK(guideRowID(b, std::string("sev|NFL|2")) == "sport|NFL");
  CHECK(guideRowID(b, std::nullopt) == "live");
  CHECK(guideEvents(b, "sport|NFL") == std::vector<std::string>{"sev|NFL|1", "sev|NFL|2", "sev|NFL|3"});
  CHECK(flipEvents(b, "sev|NFL|2", kNow) == std::vector<std::string>{"sev|NFL|1", "sev|NFL|2"});
  EventGuide guide = eventGuide(b, std::nullopt, std::string("sev|NFL|2"), Settings(), time::Zone::usEastern());
  CHECK(guide.rowID == "sport|NFL");
  CHECK(guide.heading == "Live Events  \xC2\xB7  NFL  \xC2\xB7  6:00 PM");
  REQUIRE(guide.items.size() == 5);
  CHECK(guide.items[0].header == "Live");
  CHECK(guide.items[1].game->id == "sev|NFL|1");
  CHECK(guide.items[1].game->pillText == "LIVE");
  CHECK(guide.items[1].game->statusText == "Q2 3:00");
  CHECK(guide.items[1].game->whereText == "FOX 13 Harbor  \xC2\xB7  In your lineup");
  CHECK(guide.items[1].game->sidesLayout);
  CHECK(guide.items[2].game->watching);
  CHECK(guide.items[2].game->whereText == "Event feed  \xC2\xB7  1 feed");
  CHECK(guide.items[3].header == "Starting Soon");
  CHECK(guide.items[4].game->statusText == "6:30 PM  \xC2\xB7  in 30 m");
  CHECK(guide.focusID == "sev|NFL|2");
  REQUIRE(guide.sports.size() == 3);
  CHECK(guide.sports[1].origin);
  CHECK(guide.sports[1].count == 3);  // games you can watch
  CHECK(guide.sports[1].liveText == "2 live");
  CHECK(guide.hint.find("Select: switch") != std::string::npos);
  // Other ways to watch a game.
  auto sources = guideSources(b, "sev|NFL|1", in.statuses, channels());
  REQUIRE(sources.size() == 1);
  CHECK(sources[0].name == "FOX 13 Harbor");
  CHECK(sources[0].detail == "In My Lineup");
  // A row with nothing to switch to.
  Board empty = build(baseInput({eventOf(none)}));
  EventGuide nothing = eventGuide(empty, std::string("sport|NFL"), std::nullopt, Settings(), time::Zone::usEastern());
  CHECK(nothing.items.empty());
  CHECK(nothing.emptyMessage == "Nothing else on in your channels right now.");
  CHECK(containsKey(b, "sport|NFL|sev|NFL|1"));
  CHECK(containsKey(b, "families|fam"));
  CHECK_FALSE(containsKey(b, "sport|NBA|sev|NFL|1"));
}

TEST_CASE("cameras and tennis: hooks for later milestones") {
  SportsEvent race = plainEvent("race", "Harbor 500", "Motorsport", kNow - 600, {feed("xt|r")});
  SportsEvent cam = plainEvent("cam", "Harbor 500: No. 5 Driver Name", "Motorsport", kNow - 600, {feed("xt|c")});
  SportsEvent lone = plainEvent("lone", "Other Race: No. 9 Someone Else", "Motorsport", kNow - 600, {feed("xt|l")});
  Input in = baseInput({race, cam, lone});
  in.isCameraName = [](std::string_view t) { return t.find("No. ") != std::string_view::npos; };
  in.cameraRace = [](std::string_view t) -> std::optional<std::string> {
    auto cut = t.find(": No");
    return cut == std::string_view::npos ? std::nullopt : std::optional<std::string>(str::lower(t.substr(0, cut)));
  };
  Board b = build(in);
  CHECK(b.tiles.count("race"));
  CHECK_FALSE(b.tiles.count("cam"));  // its race has a tile: it goes to the cameras panel
  CHECK(b.tiles.count("lone"));       // no tile for its race: it stays
  SportsGame match = game("ATP", "Tennis", "80|1", team("p1", "Ann Player"), team("p2", "Bea Racket"), kNow + 3600);
  match.tennis = true;
  match.venue = "Center Court";
  match.customTitle = "Racket vs Player";
  SportsGame unwatched = match;
  unwatched.id = "ATP|80|2";
  Board t = build(baseInput({eventOf(match, {feed("xt|t")}), eventOf(unwatched)}));
  CHECK(tile(t, "sev|ATP|80|1").footer == "Center Court");
  CHECK_FALSE(t.tiles.count("sev|ATP|80|2"));  // tennis only shows matches you can watch
}

TEST_CASE("tennis flags and race podiums") {
  SportsTeam ann = team("p1", "Ann Player");
  ann.country = std::string("ESP");
  ann.flags = {"https://example.invalid/flags/esp.png"};
  SportsTeam bea = team("p2", "Bea Racket");  // no flag: the badge, as before
  SportsGame match = game("ATP", "Tennis", "80|1", ann, bea, kNow + 3600);
  match.tennis = true;
  SportsTeam pairA = team("d1", "One / Two");
  pairA.flags = {"https://example.invalid/flags/fra.png", "https://example.invalid/flags/ger.png"};
  SportsGame doubles = game("ATP", "Tennis", "80|3", pairA, team("d2", "Three / Four"), kNow + 3600);
  doubles.tennis = true;
  Board b = build(baseInput({eventOf(match, {feed("xt|t")}), eventOf(doubles, {feed("xt|d")})}));
  const Tile& t = tile(b, "sev|ATP|80|1");
  REQUIRE(t.sides.size() == 2);
  const SideLine& a = t.sides[0].fullName == "Ann Player" ? t.sides[0] : t.sides[1];
  const SideLine& z = t.sides[0].fullName == "Bea Racket" ? t.sides[0] : t.sides[1];
  CHECK(a.flags == std::vector<std::string>{"https://example.invalid/flags/esp.png"});
  CHECK(a.country == "ESP");  // the chip until the flag loads
  CHECK(z.flags.empty());
  const Tile& d = tile(b, "sev|ATP|80|3");
  const SideLine& pair = d.sides[0].fullName == "One / Two" ? d.sides[0] : d.sides[1];
  CHECK(pair.flags.size() == 2);
  // Not tennis: no flags even when a team carries some.
  SportsTeam rams = team("a", "Harbor Rams");
  rams.flags = {"https://example.invalid/flags/usa.png"};
  SportsGame nfl = game("NFL", "NFL", "1", rams, team("b", "Bay Seals"), kNow + 3600);
  Board n = build(baseInput({eventOf(nfl, {feed("xt|n")})}));
  CHECK(tile(n, "sev|NFL|1").sides[0].flags.empty());

  // A race weekend with scores on: the latest finished session's top 3.
  auto part = [](const std::string& kind, Seconds start) {
    SportsGame g = game("F1", "Motorsport", "ev1|" + kind, team("gp", "Grand Prix Alpha"),
                        team("gp", "Grand Prix Alpha"), start);
    g.cardKey = "f1|ev1|" + kind;
    g.racing = true;
    return eventOf(g, {feed("xt|f1" + kind)});
  };
  race_data::Saved race;
  race.sessions = {race_data::Session{11, "fp1", kNow - 26 * 3600, std::nullopt},
                   race_data::Session{12, "qual", kNow - 3 * 3600, std::nullopt}};
  race.drivers = {race_data::RaceDriver{4, "DRA", "Driver Alpha", "Team Alpha", std::string("FF8000"), std::nullopt},
                  race_data::RaceDriver{16, "DRB", "Driver Bravo", "Team Beta", std::nullopt, std::nullopt}};
  race.results["12"] = {16, 4};
  Input weekend =
      baseInput({part("fp1", kNow - 26 * 3600), part("qual", kNow - 3 * 3600), part("race", kNow + 20 * 3600)});
  weekend.podium = race_data::RaceData(race).podiumHook();
  Board w = build(weekend);
  REQUIRE(w.cards.count("card|F1|ev1"));
  const Tile& r = tile(w, "card|F1|ev1");
  REQUIRE(r.podium.size() == 2);
  CHECK(r.podium[0].code == "DRB");
  CHECK(r.podium[1].color == std::string("FF8000"));
  CHECK(r.podiumLabel == std::string("Qualifying"));
  CHECK(w.watchableRaceWeekends == std::vector<std::string>{"card|F1|ev1"});
  weekend.settings.showsScores = false;
  CHECK(tile(build(weekend), "card|F1|ev1").podium.empty());
}

TEST_CASE("formatting helpers") {
  time::Zone et = time::Zone::usEastern();
  CHECK(clockText(utc(2026, 10, 24, 16, 5), et, true) == "12:05 PM");
  CHECK(clockText(utc(2026, 10, 24, 4, 5), et, true) == "12:05 AM");
  CHECK(clockText(utc(2026, 10, 24, 4, 5), et, false) == "00:05");
  CHECK(shortClockText(utc(2026, 10, 24, 23, 45), et, true) == "7:45");
  CHECK(untilText(kNow + 30, kNow) == "in 1 m");
  CHECK(untilText(kNow + 59 * 60, kNow) == "in 59 m");
  CHECK(untilText(kNow + 90 * 60, kNow) == "in 2 h");
  CHECK(untilText(kNow + 6 * 3600, kNow).empty());
  CHECK(untilText(kNow - 60, kNow).empty());
  CHECK(*clockProgress(status(LivePhase::live, "67'"), "Soccer") == doctest::Approx(67.0 / 95));
  CHECK(*clockProgress(status(LivePhase::live, "Bot 8th"), "MLB") == doctest::Approx((7 + 0.5 + 0.25) / 9));
  CHECK(*clockProgress(status(LivePhase::live, "Halftime"), "NFL") == 0.5);
  CHECK(*clockProgress(status(LivePhase::live, "OT 2:00"), "NHL") == 1);
  CHECK(*clockProgress(status(LivePhase::live, "2nd 10:00"), "College Basketball") ==
        doctest::Approx((1 + 1 - 600.0 / 1200) / 2));
  CHECK_FALSE(clockProgress(status(LivePhase::live, "Live"), "NFL"));
  CHECK(shortName(team("1", "Harbor City Red Sox"), false) == "Red Sox");
  CHECK(shortName(team("1", "Harbor United"), true) == "Harbor United");
  CHECK(shortName(team("1", "One / Two"), false) == "One / Two");
  CHECK(isStreaming("Peacock"));
  CHECK(isStreaming("Prime Video Channels"));
  CHECK_FALSE(isStreaming("FOX"));
  CHECK(symbol("Soccer") == "soccerball");
  CHECK_FALSE(isLight(std::string("00274C")));
}

TEST_CASE("soccer knockouts on the tile: penalties beside the score, the winner leads") {
  SportsGame g = game("UCL", "Soccer", "9", team("h9", "Harbor United", std::string("HBU")),
                      team("a9", "Valley Rovers", std::string("VAR")), kNow - 2 * 3600, {}, std::nullopt, true);
  Input in = baseInput({eventOf(g, {lineup("e|9")})});
  LiveStatus s = status(LivePhase::live, "Pens");
  s.homeScore = "1";
  s.awayScore = "1";
  s.homePens = 4;
  s.awayPens = 3;
  s.situation = std::string("Penalties");
  in.statuses["UCL|9"] = s;
  Board b = build(in);
  const Tile& t = tile(b, "sev|UCL|9");
  REQUIRE(t.sides.size() == 2);
  // Soccer: the home side on top.
  CHECK(t.sides[0].name == "Harbor United");
  CHECK(t.sides[0].pens == 4);
  CHECK(t.sides[1].pens == 3);
  CHECK(t.sides[0].leads == true);
  CHECK(t.sides[1].leads == false);
  CHECK(t.situation == "Penalties");
  // Scores off: no score, no shootout.
  in.settings.showsScores = false;
  Board quiet = build(in);
  const Tile& q = tile(quiet, "sev|UCL|9");
  REQUIRE(q.sides.size() == 2);
  CHECK_FALSE(q.sides[0].pens);
  CHECK_FALSE(q.situation);
}

TEST_CASE("new sports on the board: rugby home first, a race's top three") {
  SportsGame rugby = game("6N", "Rugby", "1", team("h1", "Ireland", std::string("IRE")), team("a1", "France", std::string("FRA")),
                          kNow - 3600, {}, std::nullopt, true);
  Input in = baseInput({eventOf(rugby, {lineup("e|1")})});
  LiveStatus s = status(LivePhase::live, "64'");
  s.homeScore = "24";
  s.awayScore = "17";
  in.statuses["6N|1"] = s;
  Board b = build(in);
  const Tile& t = tile(b, "sev|6N|1");
  REQUIRE(t.sides.size() == 2);
  CHECK(t.sides[0].name == "Ireland");  // home first
  CHECK(t.sides[0].score == "24");
  REQUIRE(t.progress);
  CHECK(*t.progress == doctest::Approx(64.0 / 85));

  SportsGame race = game("NASCAR", "Motorsport", "9|1", team("gp", "Harbor 500"), team("gp", "Harbor 500"), kNow - 4 * 3600);
  race.racing = true;
  race.cardKey = "nascar|9|race";
  Input r = baseInput({eventOf(race, {lineup("e|2")})});
  LiveStatus done = status(LivePhase::final, "Final");
  done.top3 = {"BEL", "GIB", "RED"};
  r.statuses["NASCAR|9|1"] = done;
  r.finalSeen["NASCAR|9|1"] = kNow - 600;
  Board rb = build(r);
  const Tile& rt = tile(rb, "sev|NASCAR|9|1");
  REQUIRE(rt.podium.size() == 3);
  CHECK(rt.podium[0].code == "BEL");
  CHECK(rt.podiumLabel == std::string("Race"));
  // Scores off: no result.
  r.settings.showsScores = false;
  Board quiet = build(r);
  CHECK(tile(quiet, "sev|NASCAR|9|1").podium.empty());
}

TEST_CASE("golf on the board: the leaderboard with scores on, the name without") {
  SportsTeam side = team("g401", "Harbor Championship");
  SportsGame g = game("PGA", "Golf", "401", side, side, kNow - 3600);
  g.golf = true;
  g.note = std::string("Harbor Champ.");
  Input in = baseInput({eventOf(g, {lineup("e|1")})});
  LiveStatus s = status(LivePhase::live, "Round 3");
  s.leaders = {{"1", "S. Harbor", "https://example.invalid/f/irl.png", "-12", "14"},
               {"T2", "M. Valley", "https://example.invalid/f/esp.png", "-10", "F"}};
  in.statuses["PGA|401"] = s;
  Board b = build(in);
  const Tile& t = tile(b, "sev|PGA|401");
  REQUIRE(t.leaders.size() == 2);
  CHECK(t.leaders[1].pos == "T2");
  CHECK(t.tag == std::string("Harbor Champ."));
  CHECK(t.statusText == "Round 3");
  CHECK(t.state == State::live);
  in.settings.showsScores = false;
  Board quiet = build(in);
  const Tile& q = tile(quiet, "sev|PGA|401");
  CHECK(q.leaders.empty());
  CHECK(q.title == "Harbor Championship");
  CHECK_FALSE(q.tag);  // the name is the title, not also the tag
}
