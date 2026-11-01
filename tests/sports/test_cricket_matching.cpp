// Cricket in Live Events: a first team never matches its A, U19 or women's
// side; a feed is pinned to a match only inside the match's days; provider
// titles in the "live: A vs B _ Cricket (A vs B)" shape read cleanly; and a
// cricket channel's live match ("Live: T20 Cricket", "3rd T20I.") joins the
// one match in play as a guide source, ahead of the provider's feeds.
#include <algorithm>

#include "core/catalog/catalog.hpp"
#include "core/sports/board.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/store.hpp"
#include "core/sports/titles.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::matcher;
using catalog::EntryInfo;
using catalog::LineupCategory;
using sportsdata::feed;
using sportsdata::game;
using sportsdata::source;
using sportsdata::team;
using sportsdata::utc;

namespace {

// Sunday 2026-11-01 20:00 UTC (1:00 PM Pacific).
const Seconds kNow = utc(2026, 11, 1, 20, 0);

SportsGame cricket(const std::string& id, SportsTeam home, SportsTeam away, Seconds start,
                   std::optional<std::string> note = std::nullopt, std::optional<Seconds> end = std::nullopt) {
  SportsGame g = game("CRIC", "Cricket", id, std::move(home), std::move(away), start);
  g.homeFirst = true;
  g.note = std::move(note);
  g.end = end;
  return g;
}

ProviderEvent provider(const std::string& id, const std::string& title, std::optional<Seconds> start,
                       std::vector<std::string> feeds) {
  ProviderEvent e;
  e.id = id;
  e.title = title;
  e.league = "Cricket";
  e.start = start;
  e.feeds = std::move(feeds);
  e.family = "flsp";
  return e;
}

GuideListing willow(const std::string& id, const std::string& title, Seconds start, Seconds end,
                    std::optional<std::string> detail, bool inLineup = true) {
  GuideListing l;
  l.entryID = id;
  l.inLineup = inLineup;
  l.title = title;
  l.start = start;
  l.end = end;
  l.detail = std::move(detail);
  l.rank = inLineup ? station_rank::lineup : station_rank::elsewhere;
  l.cricket = true;
  return l;
}

BuildInput input(std::vector<SportsGame> games) {
  BuildInput in;
  in.games = std::move(games);
  in.now = kNow;
  in.zone = time::Zone::usEastern();
  return in;
}

const SportsEvent* findGame(const std::vector<SportsEvent>& events, const std::string& gameID) {
  for (const SportsEvent& e : events) {
    if (e.game && e.game->id == gameID) return &e;
  }
  return nullptr;
}

// The game's sources (the game must be there).
const std::vector<sports::EventSource>& sourcesOf(const std::vector<SportsEvent>& events, const std::string& gameID) {
  static const std::vector<sports::EventSource> kNone;
  const SportsEvent* game = findGame(events, gameID);
  REQUIRE(game != nullptr);
  return game ? game->sources : kNone;
}

const SportsEvent* findEvent(const std::vector<SportsEvent>& events, const std::string& id) {
  for (const SportsEvent& e : events) {
    if (e.id == id) return &e;
  }
  return nullptr;
}

std::vector<std::string> ids(const std::vector<EventSource>& sources) {
  std::vector<std::string> out;
  for (const EventSource& s : sources) out.push_back(s.channelID);
  return out;
}

// National sides as the scoreboard names them: the place doubles as the
// team's location (a loose name once the sport is known).
SportsTeam side(const std::string& id, const std::string& name, const std::string& abbr, const std::string& place) {
  return team(id, name, abbr, place);
}

}  // namespace

TEST_CASE("cricket squads: the qualifier at the end of a side's name") {
  CHECK(squadOf("New Zealand A") == std::make_pair(std::string("new zealand"), std::string("a")));
  CHECK(squadOf("India U19") == std::make_pair(std::string("india"), std::string("u19")));
  CHECK(squadOf("India U-19") == std::make_pair(std::string("india"), std::string("u19")));
  CHECK(squadOf("Harbor Under 19") == std::make_pair(std::string("harbor"), std::string("u19")));
  CHECK(squadOf("Australia Women") == std::make_pair(std::string("australia"), std::string("women")));
  CHECK(squadOf("Australia Women's") == std::make_pair(std::string("australia"), std::string("women")));
  CHECK(squadOf("Australia Women U19") == std::make_pair(std::string("australia"), std::string("women u19")));
  CHECK(squadOf("Prime Minister's XI") == std::make_pair(std::string("prime minister s"), std::string("xi")));
  CHECK(squadOf("Valley Emerging")->second == "emerging");
  CHECK(squadOf("Harbor Development")->second == "development");
  CHECK(squadOf("Harbor FC Reserves")->second == "reserves");
  CHECK_FALSE(squadOf("New Zealand"));
  CHECK_FALSE(squadOf("Texas A&M Aggies"));
  CHECK_FALSE(squadOf("A"));
  CHECK_FALSE(squadOf("Harbor Hawks"));

  // A squad's ways all carry the qualifier; never the first team's name or
  // the qualifier alone.
  std::vector<std::string> a = teamVariants(side("1", "New Zealand A", "NZA", "New Zealand"), false);
  CHECK(std::find(a.begin(), a.end(), "new zealand a") != a.end());
  CHECK(std::find(a.begin(), a.end(), "new zealand") == a.end());
  CHECK(std::find(a.begin(), a.end(), "a") == a.end());
  std::vector<std::string> w = teamVariants(team("2", "Brisbane Heat Women"), false);
  CHECK(std::find(w.begin(), w.end(), "brisbane heat women") != w.end());
  CHECK(std::find(w.begin(), w.end(), "heat women") != w.end());
  CHECK(std::find(w.begin(), w.end(), "women") == w.end());
}

TEST_CASE("cricket squads: a first team never matches its A, U19 or women's side") {
  SportsGame aTeams = cricket("10", side("1", "New Zealand A", "NZA", "New Zealand"),
                              side("2", "India A", "INDA", "India"), utc(2026, 10, 31, 21, 30));
  std::vector<SportsGame> onlyA = {aTeams};
  TeamIndex index(onlyA);
  auto any = [](int) { return true; };
  // The senior sides' feed: not the A match, even with the sport known (the
  // places count as names then).
  CHECK(index.matches("live: India vs New Zealand _ Cricket", any, any).empty());
  CHECK(index.matches("India vs New Zealand", any, any).empty());
  CHECK(index.matches("New Zealand A vs India A", any, any) == std::vector<int>{0});

  SportsGame senior = cricket("11", side("3", "India", "IND", "India"), side("4", "New Zealand", "NZ", "New Zealand"),
                              utc(2026, 11, 4, 2, 0), std::string("2nd T20I"));
  std::vector<SportsGame> both = {aTeams, senior};
  TeamIndex two(both);
  CHECK(two.matches("India vs New Zealand", any, any) == std::vector<int>{1});
  CHECK(two.matches("India A vs New Zealand A", any, any) == std::vector<int>{0});
  CHECK(two.matches("INDIA A VS NEW ZEALAND A", any, any) == std::vector<int>{0});

  // Only the senior match scheduled: an A, U19 or XI title isn't it.
  std::vector<SportsGame> seniorOnly = {senior};
  TeamIndex first(seniorOnly);
  CHECK(first.matches("India vs New Zealand, 2nd T20I", any, any) == std::vector<int>{0});
  CHECK(first.matches("India A vs New Zealand A", any, any).empty());
  CHECK(first.matches("India U19 vs New Zealand U19", any, any).empty());
  CHECK(first.matches("India U-19 vs New Zealand U-19", any, any).empty());
  CHECK(first.matches("India Women vs New Zealand Women", any, any).empty());
  CHECK(first.matches("India vs New Zealand _ Women`s Cricket", any, any).empty());
  CHECK(first.matches("Prime Minister's XI vs India", any, any).empty());

  // A women's side by its first team's name only when the title says Women.
  SportsGame women = cricket("12", team("5", "Brisbane Heat Women"), team("6", "Sydney Thunder Women"),
                             utc(2026, 11, 1, 7, 40));
  std::vector<SportsGame> wbbl = {women};
  TeamIndex womenIndex(wbbl);
  CHECK(womenIndex.matches("live: Brisbane Heat vs Sydney Thunder _ Women`s Cricket (Heat vs Thunder)", any, any) ==
        std::vector<int>{0});
  CHECK(womenIndex.matches("Brisbane Heat Women vs Sydney Thunder Women", any, any) == std::vector<int>{0});
  CHECK(womenIndex.matches("Brisbane Heat vs Sydney Thunder", any, any).empty());

  // Other sports: a club's U19 side isn't the club; a women's college game is
  // still found by a title that says Women's.
  SportsGame clubs = game("EPL", "Soccer", "20", team("7", "Harbor FC", std::string("HFC")),
                          team("8", "Valley United", std::string("VAL")), utc(2026, 11, 1, 15, 0), {}, std::nullopt, true);
  SportsGame ncaaw = game("NCAAW", "College Basketball", "21", team("9", "Harbor Hawks", std::string("HAR")),
                          team("10", "Valley Owls", std::string("VLY")), utc(2026, 11, 1, 23, 0));
  std::vector<SportsGame> others = {clubs, ncaaw};
  TeamIndex otherIndex(others);
  CHECK(otherIndex.matches("Harbor FC U19 vs Valley United U19", any, any).empty());
  CHECK(otherIndex.matches("Harbor FC Reserves vs Valley United Reserves", any, any).empty());
  CHECK(otherIndex.matches("Harbor FC JV vs Valley United JV", any, any).empty());
  CHECK(otherIndex.matches("Harbor FC vs Valley United", any, any) == std::vector<int>{0});
  // "A-League" after a club is the league, not its A side.
  CHECK(otherIndex.matches("Harbor FC vs Valley United A-League", any, any) == std::vector<int>{0});
  CHECK(otherIndex.matches("Women's College Basketball: Harbor Hawks vs Valley Owls", any, any) ==
        std::vector<int>{1});
}

TEST_CASE("cricket feeds: a slot days after the A match is its own tile, not the match live now") {
  // A four-day A-team match, under way; the provider's slot for the senior
  // sides two days on (5:00 PM Pacific on the 3rd).
  SportsGame aTeams = cricket("10", side("1", "New Zealand A", "NZA", "New Zealand"),
                              side("2", "India A", "INDA", "India"), utc(2026, 10, 31, 21, 30), std::nullopt,
                              utc(2026, 11, 3, 21, 30));
  Seconds slot = utc(2026, 11, 4, 1, 0);
  BuildInput in = input({aTeams});
  in.events = {provider("438", "live: India vs New Zealand _ Cricket", slot, {"flsp|438"})};
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* match = findGame(out->events, "CRIC|10");
  REQUIRE(match);
  CHECK(match->sources.empty());
  const SportsEvent* tile = findEvent(out->events, "438");
  REQUIRE(tile);
  CHECK_FALSE(tile->game);
  CHECK(tile->title == "India vs New Zealand");
  CHECK(tile->start == slot);
  CHECK(tile->state(kNow) == SportsEvent::State::upcoming);

  // With the senior match scheduled then, the slot is that match's feed.
  SportsGame senior = cricket("11", side("3", "India", "IND", "India"), side("4", "New Zealand", "NZ", "New Zealand"),
                              slot, std::string("2nd T20I"));
  in.games.push_back(senior);
  out = build(in);
  REQUIRE(out);
  CHECK(sourcesOf(out->events, "CRIC|10").empty());
  CHECK(ids(sourcesOf(out->events, "CRIC|11")) == std::vector<std::string>{"flsp|438"});
}

TEST_CASE("cricket feeds: a match over several days takes a feed only at one of its days' play") {
  // Days start at 21:30 UTC, the 31st to the 3rd.
  SportsGame test = cricket("30", team("1", "Harbor"), team("2", "Valley"), utc(2026, 10, 31, 21, 30),
                            std::string("1st Test"), utc(2026, 11, 3, 21, 30));
  auto matched = [&](Seconds at) {
    BuildInput in = input({test});
    in.events = {provider("p", "Cricket: Harbor vs Valley", at, {"xt|p"})};
    auto out = build(in);
    REQUIRE(out);
    return !sourcesOf(out->events, "CRIC|30").empty();
  };
  CHECK(matched(utc(2026, 11, 1, 21, 15)));        // day 2, before play
  CHECK(matched(utc(2026, 11, 2, 2, 0)));          // day 2, after tea
  CHECK(matched(utc(2026, 11, 3, 21, 30)));        // the last day
  CHECK_FALSE(matched(utc(2026, 11, 2, 12, 0)));   // overnight between days
  CHECK_FALSE(matched(utc(2026, 11, 5, 21, 30)));  // two days after the last

  // A one-day match with an end: up to its end, not into the next day.
  SportsGame odi = cricket("31", team("3", "Coast"), team("4", "Ridge"), utc(2026, 11, 1, 8, 0), std::string("2nd ODI"),
                           utc(2026, 11, 1, 16, 0));
  BuildInput in = input({odi});
  in.events = {provider("q1", "Cricket: Coast vs Ridge", utc(2026, 11, 1, 15, 0), {"xt|q1"}),
               provider("q2", "Cricket: Coast vs Ridge", utc(2026, 11, 2, 1, 0), {"xt|q2"})};
  auto out = build(in);
  REQUIRE(out);
  CHECK(ids(sourcesOf(out->events, "CRIC|31")) == std::vector<std::string>{"xt|q1"});
}

TEST_CASE("cricket titles: the live marker, the sport tail and the short-name matchup go") {
  using titles::tidy;
  CHECK(tidy("live:  Zimbabwe vs Afghanistan _ Cricket (Zimbabwe vs Afghanistan)", "Cricket") ==
        "Zimbabwe vs Afghanistan");
  CHECK(tidy("live: Zimbabwe vs Afghanistan _ Cricket", "Cricket") == "Zimbabwe vs Afghanistan");
  CHECK(tidy("live: Hobart Hurricanes vs Sydney Sixers _ Women`s Cricket (Hurricanes vs Sixers)", "Cricket") ==
        "Hobart Hurricanes vs Sydney Sixers (Women's)");
  // The same shape in other sports.
  CHECK(tidy("live: Southern Harbor vs Valley State _ Field Hockey (SHU vs VSU)", "Field Hockey") ==
        "Southern Harbor vs Valley State");
  CHECK(tidy("swimming: Harbor College vs Valley Tech _ Men`s Water Polo (Harbor vs Valley)", "Other") ==
        "Harbor College vs Valley Tech (Men's)");
  CHECK(tidy("hoops: Harbor College vs Valley Tech _ Women`s (Harbor College vs Valley Tech)", "Basketball") ==
        "Harbor College vs Valley Tech (Women's)");
  CHECK(tidy("volleyball: Harbor vs Valley (NY) _ Women`s (Harbor vs Valley", "Volleyball") ==
        "Harbor vs Valley (NY) (Women's)");
  // A tag naming a sport with an icon of its own stays; an aside that isn't
  // the matchup stays.
  CHECK(tidy("wrestling: Harbor vs. Valley _ Women`s (Mat 1)", "Fighting") ==
        "wrestling: Harbor vs. Valley (Women's) (Mat 1)");
  // Other tails are left to the tile's reading; only a live marker goes.
  CHECK(tidy("live: Harbor Field Hockey Championship _ Game 3 (Harbor Field Hockey Champ. _ G3)", "Field Hockey") ==
        "Harbor Field Hockey Championship _ Game 3 (Harbor Field Hockey Champ. _ G3)");
  CHECK(tidy("fc: Harbor Women`s Soccer Championship _ Game 5", "Soccer") ==
        "fc: Harbor Women`s Soccer Championship _ Game 5");
  CHECK(tidy("Rugby: Harbor vs Valley - Six Nations", "Rugby") == "Rugby: Harbor vs Valley - Six Nations");
  CHECK(tidy("LIVE: Harbor vs Valley", "Soccer") == "Harbor vs Valley");

  // The tile: the matchup, Women's as its tag, nothing under it.
  titles::Title women = titles::read("live: Hobart Hurricanes vs Sydney Sixers _ Women`s Cricket (Hurricanes vs Sixers)",
                                     "Cricket");
  CHECK(women.name == "Hobart Hurricanes vs Sydney Sixers");
  CHECK(women.stage == "Women's");
  CHECK_FALSE(women.subtitle);
  REQUIRE(women.sides);
  CHECK(women.sides->second == "Sydney Sixers");
  titles::Title men = titles::read("live: Zimbabwe vs Afghanistan _ Cricket", "Cricket");
  CHECK(men.name == "Zimbabwe vs Afghanistan");
  CHECK_FALSE(men.stage);
  CHECK_FALSE(men.subtitle);

  // A feed no match took: its tile, Where to Watch and the details card say
  // the tidy title; women's cricket keeps its tag.
  BuildInput in = input({});
  in.events = {provider("51", "live: Zimbabwe vs Afghanistan _ Cricket", kNow - 3600, {"flsp|51"}),
               provider("3", "live: Hobart Hurricanes vs Sydney Sixers _ Women`s Cricket (Hurricanes vs Sixers)",
                        kNow + 3600, {"flsp|3"})};
  auto out = build(in);
  REQUIRE(out);
  REQUIRE(findEvent(out->events, "51"));
  CHECK(findEvent(out->events, "51")->title == "Zimbabwe vs Afghanistan");
  REQUIRE(findEvent(out->events, "3"));
  board::Input b;
  b.events = out->events;
  b.now = kNow;
  b.zone = time::Zone::usEastern();
  board::Board shown = board::build(b);
  const board::Tile& tile = shown.tiles.at("3");
  CHECK(tile.title == "Hobart Hurricanes vs Sydney Sixers");
  CHECK(tile.tag == findEvent(out->events, "3")->leagueShort + " \xC2\xB7 Women's");
  CHECK(board::detailCard(*findEvent(out->events, "51"), kNow, b.zone, b.settings, nullptr).title ==
        "Zimbabwe vs Afghanistan");
}

TEST_CASE("cricket channels: Willow and the rest, and their live matches") {
  CHECK(store::cricketChannelName("US Willow Cricket (S)"));
  CHECK(store::cricketChannelName("USA  Willow Cricket Xtra"));
  CHECK(store::cricketChannelName("Willow"));
  CHECK(store::cricketChannelName("UK: SKY SPORTS CRICKET HD"));
  CHECK(store::cricketChannelName("SuperSport Cricket"));
  CHECK(store::cricketChannelName("Fox Cricket"));
  CHECK_FALSE(store::cricketChannelName("Harbor Sports Network"));
  CHECK_FALSE(store::cricketChannelName("Tennis Channel"));

  CHECK(isLiveCricket("Live: T20 Cricket"));
  CHECK(isLiveCricket("LIVE Test Cricket"));
  CHECK(isLiveCricket("Live: ODI Cricket"));
  CHECK(isLiveCricket("Live Cricket: 2nd ODI"));
  CHECK_FALSE(isLiveCricket("T20 Cricket"));
  CHECK_FALSE(isLiveCricket("Cricket Live"));
  CHECK_FALSE(isLiveCricket("Live: Cricket Highlights"));
  CHECK_FALSE(isLiveCricket("Live: The Cricket Show"));
  CHECK_FALSE(isLiveCricket("Live: Harbor Sports Tonight"));

  std::unordered_map<std::string, EntryInfo> infos;
  auto info = [](const std::string& id, const std::string& name, LineupCategory category) {
    EntryInfo e;
    e.id = id;
    e.name = name;
    e.category = category;
    e.market = std::string("US");
    return e;
  };
  infos["e|willow"] = info("e|willow", "Willow Cricket", LineupCategory::sports);
  infos["e|sports"] = info("e|sports", "Harbor Sports", LineupCategory::sports);
  infos["e|cartoon"] = info("e|cartoon", "Cricket Cartoons", LineupCategory::entertainment);
  auto list = store::listingChannels({"e|willow", "e|sports", "e|cartoon"}, {}, infos, StationMarkets());
  REQUIRE(list.size() == 2);
  const store::ListingChannel& sports = list[0].id == "e|sports" ? list[0] : list[1];
  const store::ListingChannel& cricketChannel = list[0].id == "e|willow" ? list[0] : list[1];
  CHECK(cricketChannel.cricketChannel);
  CHECK(cricketChannel.inLineup);
  CHECK_FALSE(sports.cricketChannel);

  Seconds s = utc(2026, 11, 1, 14, 30), e = s + 4 * 3600;
  auto live = store::listingFor(cricketChannel, "Live: T20 Cricket", std::nullopt, std::string("3rd T20I."), true, s, e);
  REQUIRE(live);
  CHECK(live->cricket);
  CHECK(live->inLineup);
  CHECK(live->detail == "3rd T20I.");
  CHECK(listingFromJson(toJson(*live)) == *live);
  CHECK_FALSE(store::listingFor(cricketChannel, "Cricket Highlights", std::nullopt,
                                std::string("Highlights of the week's cricket."), true, s, e));
  CHECK_FALSE(store::listingFor(cricketChannel, "T20 Cricket", std::nullopt, std::string("3rd T20I."), true, s, e));
  CHECK_FALSE(store::listingFor(sports, "Live: T20 Cricket", std::nullopt, std::string("3rd T20I."), true, s, e));
}

TEST_CASE("cricket channels: a live match joins the one match in play, first in My Lineup") {
  SportsGame t20 = cricket("40", team("1", "Harbor"), team("2", "Valley"), kNow - 3600, std::string("3rd T20I"));
  t20.matchFormat = std::string("T20I");
  BuildInput in = input({t20});
  in.events = {provider("p", "live: Harbor vs Valley _ Cricket", kNow - 3600, {"flsp|p"})};
  in.listings = {willow("e|willow", "Live: T20 Cricket", kNow - 3600 - 900, kNow + 3 * 3600, std::string("3rd T20I."))};
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* match = findGame(out->events, "CRIC|40");
  REQUIRE(match);
  // The cricket channel in My Lineup first, the provider's feed its backup.
  CHECK(ids(match->sources) == std::vector<std::string>{"e|willow", "flsp|p"});
  CHECK(match->sources[0].kind == EventSource::Kind::lineup);
  CHECK(match->sources[0].detail == "In My Lineup");
  CHECK(out->stats.cricketListings == 1);
  CHECK(out->stats.summary().find("1 on cricket channels") != std::string::npos);
  // What plays: the channel first, the feed after it (Where to Watch and
  // the flip list go by the same order).
  store::Resolver resolver;
  resolver.sourceChannel = [](const EventSource& s) -> std::optional<store::ChannelRef> {
    return store::ChannelRef{s.channelID, s.channelID};
  };
  CHECK(store::eventCandidates(*match, std::nullopt, resolver) == std::vector<std::string>{"e|willow", "flsp|p"});

  // Not in My Lineup: a guide station, still ahead of the feed.
  in.listings = {willow("e|willow2", "Live: T20 Cricket", kNow - 3600, kNow + 3 * 3600, std::nullopt, false)};
  out = build(in);
  REQUIRE(out);
  match = findGame(out->events, "CRIC|40");
  CHECK(ids(match->sources) == std::vector<std::string>{"e|willow2", "flsp|p"});
  CHECK(match->sources[0].kind == EventSource::Kind::guide);

  // Before the match, or once it's over: nothing.
  in.listings = {willow("e|willow", "Live: T20 Cricket", kNow + 5 * 3600, kNow + 8 * 3600, std::nullopt)};
  out = build(in);
  CHECK(ids(sourcesOf(out->events, "CRIC|40")) == std::vector<std::string>{"flsp|p"});
  // Another format than the match's: another match.
  in.listings = {willow("e|willow", "Live: Test Cricket", kNow - 3600, kNow + 3 * 3600, std::nullopt)};
  out = build(in);
  CHECK(ids(sourcesOf(out->events, "CRIC|40")) == std::vector<std::string>{"flsp|p"});
  CHECK(out->stats.cricketListings == 0);
}

TEST_CASE("cricket channels: never a guess between matches in play") {
  SportsGame third = cricket("50", team("1", "Harbor"), team("2", "Valley"), kNow - 3600, std::string("3rd T20I"));
  SportsGame first = cricket("51", team("3", "Coast"), team("4", "Ridge"), kNow - 1800, std::string("1st T20I"));
  SportsGame test = cricket("52", team("5", "Summit"), team("6", "Plains"), kNow - 2 * 86'400 - 3600,
                            std::string("1st Test"), kNow + 86'400 - 3600);
  test.matchFormat = std::string("Test");
  auto linked = [&](const GuideListing& listing, std::vector<SportsGame> games) {
    BuildInput in = input(std::move(games));
    in.listings = {listing};
    auto out = build(in);
    REQUIRE(out);
    std::vector<std::string> found;
    for (const SportsEvent& e : out->events) {
      if (e.game && !e.sources.empty()) found.push_back(e.game->id);
    }
    return found;
  };
  Seconds from = kNow - 3600, to = kNow + 3 * 3600;
  // Two T20s on: the description's match number picks one, else none.
  CHECK(linked(willow("e|w", "Live: T20 Cricket", from, to, std::string("3rd T20I.")), {third, first}) ==
        std::vector<std::string>{"CRIC|50"});
  CHECK(linked(willow("e|w", "Live: T20 Cricket", from, to, std::nullopt), {third, first}).empty());
  CHECK(linked(willow("e|w", "Live: T20 Cricket", from, to, std::string("2nd T20I.")), {third, first}).empty());
  SportsGame anotherThird = cricket("53", team("7", "Delta"), team("8", "Mesa"), kNow - 3600, std::string("3rd T20I"));
  CHECK(linked(willow("e|w", "Live: T20 Cricket", from, to, std::string("3rd T20I.")), {third, anotherThird}).empty());
  // A T20 and a Test on: the format says which. A Test on its third day
  // counts, though it started days ago.
  CHECK(linked(willow("e|w", "Live: Test Cricket", from, to, std::string("1st Test, Day 3.")), {third, test}) ==
        std::vector<std::string>{"CRIC|52"});
  CHECK(linked(willow("e|w", "Live: T20 Cricket", from, to, std::nullopt), {third, test}) ==
        std::vector<std::string>{"CRIC|50"});
  // The description naming the sides settles it too.
  CHECK(linked(willow("e|w", "Live: T20 Cricket", from, to, std::string("Coast v Ridge.")), {third, first}) ==
        std::vector<std::string>{"CRIC|51"});
}
