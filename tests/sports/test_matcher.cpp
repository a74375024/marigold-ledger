// The matcher: names, team index, fight cards, and matching schedule games
// with provider event feeds and guide listings (clock offsets, replays,
// day-less titles, venues, Spanish feeds, station folding, stable ids).
#include <algorithm>

#include "core/sports/matcher.hpp"
#include "core/sports/network_map.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::matcher;
using sportsdata::feed;
using sportsdata::game;
using sportsdata::team;
using sportsdata::utc;

namespace {

const SportsEvent* find(const std::vector<SportsEvent>& events, const std::string& id) {
  for (const SportsEvent& e : events) {
    if (e.id == id) return &e;
  }
  return nullptr;
}

const SportsEvent* findGame(const std::vector<SportsEvent>& events, const std::string& gameID) {
  for (const SportsEvent& e : events) {
    if (e.game && e.game->id == gameID) return &e;
  }
  return nullptr;
}

ProviderEvent provider(const std::string& id, const std::string& title, const std::string& league,
                       std::optional<Seconds> start, std::vector<std::string> feeds, const std::string& family = "fam",
                       bool spanish = false, bool guessed = false) {
  ProviderEvent e;
  e.id = id;
  e.title = title;
  e.league = league;
  e.start = start;
  e.feeds = std::move(feeds);
  e.family = family;
  e.spanish = spanish;
  e.guessed = guessed;
  return e;
}

std::vector<std::string> ids(const std::vector<EventSource>& sources) {
  std::vector<std::string> out;
  for (const EventSource& s : sources) out.push_back(s.channelID);
  return out;
}

}  // namespace

TEST_CASE("normalized: folded, lower-case words padded with spaces") {
  CHECK(normalized("Texas A&M Aggies") == " texas a and m aggies ");
  CHECK(normalized("  S\xC3\xA3o Paulo FC!! ") == " sao paulo fc ");
  CHECK(normalized("NFL 12: Team A @ Team B") == " nfl 12 team a team b ");
  CHECK(normalized("") == "  ");
}

TEST_CASE("looksLikeGame: two sides, or a dash after a network prefix") {
  CHECK(looksLikeGame("Team A vs Team B"));
  CHECK(looksLikeGame("Team A vs. Team B"));
  CHECK(looksLikeGame("Team A v Team B"));
  CHECK(looksLikeGame("Team A @ Team B"));
  CHECK(looksLikeGame("Team A at Harbor City Gulls"));
  CHECK(looksLikeGame("NCAAF 02: Team A - Team B"));
  CHECK_FALSE(looksLikeGame("A very long programme title here: Team A - Team B"));
  CHECK_FALSE(looksLikeGame("Harbor City Highlights"));
}

TEST_CASE("pairKey and matchupKey: either team order, prefixes and brackets dropped") {
  CHECK(pairKey("Team A vs Harbor City Gulls") == "harbor city gulls|team a");
  CHECK(pairKey("Harbor City Gulls @ Team A") == "harbor city gulls|team a");
  CHECK(pairKey("NFL: Team A vs Team B (7:30 PM ET)") == "team a|team b");
  CHECK(pairKey("Team A") == std::nullopt);
  CHECK(pairKey("vs Team A") == std::nullopt);
  CHECK(matchupKey("The Big Show") == " the big show ");
}

TEST_CASE("team variants: full name, alternates, nickname; places only for colleges") {
  SportsTeam gulls = team("1", "Harbor City Gulls", std::string("HCG"), std::string("Harbor City"));
  gulls.alternates = {"Gulls"};
  CHECK(teamVariants(gulls, false) == std::vector<std::string>{"harbor city gulls", "gulls"});
  SportsTeam school = team("2", "Lakeside Owls", std::nullopt, std::string("Lakeside"));
  CHECK(teamVariants(school, true) == std::vector<std::string>{"lakeside owls", "lakeside", "owls"});
  // Generic words never stand alone ("City", "United").
  SportsTeam club = team("3", "Harbor United");
  CHECK(teamVariants(club, false) == std::vector<std::string>{"harbor united"});
}

TEST_CASE("team index: both teams on separate words; abbreviations only in capitals") {
  Seconds start = utc(2026, 10, 25, 17, 0);
  std::vector<SportsGame> games = {
      game("NFL", "NFL", "1", team("1", "Harbor City Gulls", std::string("HCG"), std::string("Harbor City")),
           team("2", "Team Alpha", std::string("TMA"), std::string("Alphaville")), start),
      game("NCAAF", "College Football", "2", team("3", "Lakeside State Owls", std::string("LSU")),
           team("4", "Lakeside Owls"), start),
  };
  TeamIndex index(games);
  auto all = [](int) { return true; };
  CHECK(index.matches("Team Alpha at Harbor City Gulls", all) == std::vector<int>{0});
  CHECK(index.matches("TMA @ HCG tonight", all) == std::vector<int>{0});
  CHECK(index.matches("tma @ hcg tonight", all).empty());  // lower case: not abbreviations
  CHECK(index.matches("TMA @ Harbor City Gulls", all) == std::vector<int>{0});
  // In an all-capitals title no abbreviation counts on its own.
  CHECK(index.matches("TMA @ HCG", all).empty());
  // City names only when the league is known.
  CHECK(index.matches("Alphaville vs Harbor City", all).empty());
  CHECK(index.matches("Alphaville vs Harbor City", all, all) == std::vector<int>{0});
  // The filter decides which games count.
  CHECK(index.matches("Team Alpha at Harbor City Gulls", [](int g) { return g != 0; }).empty());
  // The longest name wins its words: "lakeside state owls" isn't "lakeside owls".
  CHECK(index.matches("Lakeside State Owls vs Harbor City Gulls", all).empty());
}

TEST_CASE("team index from a list: one team named, or a pair of schools") {
  std::vector<SportsTeam> teams = {team("10", "Valley Hawks"), team("11", "River Town Otters"),
                                   team("12", "Lakeside Owls", std::nullopt, std::string("Lakeside"))};
  TeamIndex index = TeamIndex::fromTeams(teams, true);
  CHECK(index.single("NBA Valley Hawks HD", "NBA")->id == "10");
  CHECK(index.single("NBA Hawks", "NBA")->id == "10");   // nickname next to the league
  CHECK_FALSE(index.single("Hawks TV", "NBA"));           // nickname alone
  CHECK_FALSE(index.single("Valley Hawks vs River Town Otters", "NBA"));  // two teams
  auto pair = index.pair("Volleyball: Lakeside vs River Town Otters");
  REQUIRE(pair);
  CHECK(pair->first.id == "12");
  CHECK(pair->second.id == "11");
  CHECK_FALSE(index.pair("Lakeside vs Somewhere Else"));
}

TEST_CASE("fight keys: promotion, number, part; shows around the fights are not") {
  auto k = fightKey("UFC 333: FIGHT PASS PRELIMS");
  REQUIRE(k);
  CHECK(k->org == "ufc");
  CHECK(k->number == "333");
  CHECK(k->part == "prelims");
  CHECK(k->fightPass);
  auto es = fightKey("(ESPA\xC3\x91OL) UFC 333: Stone vs. River PRELIMINARES");
  REQUIRE(es);
  CHECK(es->part == "prelims");
  CHECK(fightKey("PFL 07 Main Card")->number == "7");
  CHECK(fightKey("UFC Fight Night: Main Event")->number.empty());
  CHECK(fightKey("Prelims UFC 333 Early Prelims")->part == "early");
  CHECK_FALSE(fightKey("UFC 333 Weigh-Ins"));
  CHECK_FALSE(fightKey("UFC 333 Press Conference"));
  CHECK_FALSE(fightKey("Boxing: Team A vs Team B"));
}

TEST_CASE("fight targets: a named part, the part at its time, or the whole night") {
  Seconds early = utc(2026, 10, 24, 22, 0), prelims = utc(2026, 10, 25, 0, 0), main = utc(2026, 10, 25, 2, 0);
  auto part = [&](const std::string& key, Seconds start) {
    SportsGame g = game("UFC", "Fighting", "600|" + key, team("f1", "Max Stone"), team("f2", "Lee River"), start);
    g.cardKey = "ufc|333|" + key;
    return g;
  };
  std::vector<SportsGame> games = {part("early", early), part("prelims", prelims), part("main", main)};
  std::map<std::string, std::vector<int>> cards = {{"ufc|333", {0, 1, 2}}};
  auto key = [](const std::string& title) { return *fightKey(title); };
  CHECK(fightTargets(key("UFC 333 Main Card"), main, cards, games) == std::vector<int>{2});
  CHECK(fightTargets(key("UFC 333 Main Card"), std::nullopt, cards, games) == std::vector<int>{2});
  // "Fight Pass Prelims" are the early prelims.
  CHECK(fightTargets(key("UFC 333 Fight Pass Prelims"), early, cards, games) == std::vector<int>{0});
  // "Prelims" listed at the early prelims' start go there.
  CHECK(fightTargets(key("UFC 333 Prelims"), early + 10 * 60, cards, games) == std::vector<int>{0});
  // No part named, starting by the first part: the whole night.
  CHECK(fightTargets(key("UFC 333"), early - 3600, cards, games) == std::vector<int>{0, 1, 2});
  CHECK(fightTargets(key("UFC 333"), main + 600, cards, games) == std::vector<int>{2});
  CHECK(fightTargets(key("UFC 333"), std::nullopt, cards, games).empty());
  CHECK(fightTargets(key("UFC 333"), main + 5 * 3600, cards, games).empty());
  CHECK(fightTargets(key("UFC 334"), main, cards, games).empty());
}

TEST_CASE("venues in guide descriptions; generic sports titles") {
  auto v = venueOf("From Gull Field in Harbor City, Ore. The Gulls host Team A.");
  REQUIRE(v);
  CHECK(v->first == "gull field");
  CHECK(v->second == "harbor city");
  CHECK(venueOf("Live from the Otter Dome in River Town.")->first == "otter dome");
  CHECK_FALSE(venueOf("Highlights of the week."));
  CHECK(isGenericSportsTitle("NBA Basketball"));
  CHECK(isGenericSportsTitle("F\xC3\xBA" "tbol Mexicano"));
  CHECK_FALSE(isGenericSportsTitle("Evening News"));
  CHECK(isStudioShow("NFL Countdown"));
  CHECK_FALSE(isStudioShow("Countdown: Team A vs Team B"));
  CHECK(shortLeague("College Football") == "NCAAF");
  CHECK(shortLeague("") == "EVENT");
  CHECK(shortLeague("Golf") == "GOLF");
}

namespace {

struct Fixture {
  Seconds now = utc(2026, 10, 25, 15, 0);  // Sunday 11 AM Eastern
  SportsGame nflGame = game("NFL", "NFL", "1", team("1", "Harbor City Gulls", std::string("HCG")),
                            team("2", "Team Alpha", std::string("TMA")), utc(2026, 10, 25, 17, 0), {"FOX"},
                            std::string("Gull Field, Harbor City"));
  SportsGame nbaGame = game("NBA", "NBA", "2", team("3", "River Town Otters", std::string("RTO")),
                            team("4", "Valley Hawks", std::string("VLH")), utc(2026, 10, 25, 23, 30), {},
                            std::string("Otter Dome, River Town"));
  BuildInput input() const {
    BuildInput in;
    in.games = {nflGame, nbaGame};
    in.now = now;
    in.zone = time::Zone::usEastern();
    return in;
  }
};

}  // namespace

TEST_CASE("provider feeds go to the scheduled game within 3 hours; further away is a replay") {
  Fixture f;
  BuildInput in = f.input();
  in.events = {
      provider("ev1", "NFL 12: Team Alpha @ Harbor City Gulls", "NFL", f.nflGame.start, {"xt|101"}),
      provider("ev2", "NFL 13: Team Alpha @ Harbor City Gulls (Spanish)", "NFL", f.nflGame.start + 1800, {"xt|102"},
               "fam2", true),
      provider("ev3", "Replay: Team Alpha vs Harbor City Gulls", "NFL", f.nflGame.start + 2 * 86'400 - 3600,
               {"xt|103"}),
  };
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* tile = findGame(out->events, "NFL|1");
  REQUIRE(tile);
  // The tile takes the first provider event's id (stable across rebuilds).
  CHECK(tile->id == "ev1");
  CHECK(ids(tile->sources) == std::vector<std::string>{"xt|101", "xt|102"});
  CHECK(tile->sources[0].detail == "Provider event channel");
  CHECK(tile->family == "fam");
  CHECK(out->stats.replays == 1);
  CHECK(out->stats.matchedEvents == 2);
  CHECK_FALSE(find(out->events, "ev3"));
  // The game with nothing showing it is still there (dimmed).
  const SportsEvent* nba = findGame(out->events, "NBA|2");
  REQUIRE(nba);
  CHECK(nba->id == "sev|NBA|2");
  CHECK(nba->sources.empty());
  // By start.
  CHECK(out->events.front().id == "ev1");
}

TEST_CASE("Spanish feeds after English unless the viewer prefers Spanish") {
  Fixture f;
  BuildInput in = f.input();
  in.events = {
      provider("es", "ESPN+ 12: Team Alpha vs Harbor City Gulls", "NFL", f.nflGame.start, {"xt|es"}, "fam", true),
      provider("en", "NFL 4: Team Alpha vs Harbor City Gulls", "NFL", f.nflGame.start, {"xt|en"}),
  };
  auto out = build(in);
  REQUIRE(out);
  CHECK(ids(findGame(out->events, "NFL|1")->sources) == std::vector<std::string>{"xt|en", "xt|es"});
  in.preferSpanish = true;
  out = build(in);
  CHECK(ids(findGame(out->events, "NFL|1")->sources) == std::vector<std::string>{"xt|es", "xt|en"});
}

TEST_CASE("a group's whole-hour clock offset is learned and corrected") {
  Fixture f;
  SportsGame other = game("NFL", "NFL", "3", team("5", "Mountain Goats", std::string("MTG")),
                          team("6", "Lake Pikes", std::string("LKP")), utc(2026, 10, 25, 20, 25));
  SportsGame third = game("NFL", "NFL", "4", team("7", "Desert Foxes", std::string("DSF")),
                          team("8", "Coast Crabs", std::string("CCR")), utc(2026, 10, 26, 0, 20));
  BuildInput in = f.input();
  in.games.push_back(other);
  in.games.push_back(third);
  // The "eu" group writes times 5 hours late.
  in.events = {
      provider("e1", "Team Alpha vs Harbor City Gulls", "NFL", f.nflGame.start + 5 * 3600, {"eu|1"}, "eu"),
      provider("e2", "Mountain Goats vs Lake Pikes", "NFL", other.start + 5 * 3600, {"eu|2"}, "eu"),
      provider("e3", "Desert Foxes vs Coast Crabs", "NFL", third.start + 5 * 3600, {"eu|3"}, "eu"),
  };
  auto out = build(in);
  REQUIRE(out);
  REQUIRE(out->stats.offsets.size() == 1);
  CHECK(out->stats.offsets[0] == "eu +5h");
  CHECK(ids(findGame(out->events, "NFL|1")->sources) == std::vector<std::string>{"eu|1"});
  CHECK(ids(findGame(out->events, "NFL|4")->sources) == std::vector<std::string>{"eu|3"});
}

TEST_CASE("a day-less title goes to that matchup's next game, at its clock time") {
  Fixture f;
  BuildInput in = f.input();
  // Only "1:00 PM" was given (the day a guess): 17:00 UTC tomorrow.
  in.events = {provider("g1", "Team Alpha @ Harbor City Gulls 1:00 PM", "NFL", f.nflGame.start + 86'400, {"xt|9"},
                        "slots", false, true)};
  auto out = build(in);
  REQUIRE(out);
  CHECK(ids(findGame(out->events, "NFL|1")->sources) == std::vector<std::string>{"xt|9"});
  CHECK(out->stats.guessedMatched == 1);
}

TEST_CASE("guide listings: by title, by venue, by description; stations beyond three folded") {
  Fixture f;
  BuildInput in = f.input();
  auto listing = [&](const std::string& id, const std::string& title, Seconds start, int rank, bool inLineup) {
    GuideListing l;
    l.entryID = id;
    l.inLineup = inLineup;
    l.title = title;
    l.start = start;
    l.end = start + 3 * 3600;
    l.rank = rank;
    return l;
  };
  GuideListing venue = listing("e|kotr", "NBA Basketball", f.nbaGame.start, station_rank::home, false);
  venue.venue = "otter dome";
  venue.city = "river town";
  GuideListing described = listing("e|desc", "NBA Basketball", f.nbaGame.start, station_rank::elsewhere, false);
  described.detail = "The Valley Hawks visit the River Town Otters.";
  in.listings = {
      listing("e|fox13", "NFL Football Team Alpha at Harbor City Gulls", f.nflGame.start - 600, station_rank::lineupHome,
              true),
      listing("e|s1", "Team Alpha at Harbor City Gulls", f.nflGame.start, station_rank::elsewhere, false),
      listing("e|s2", "Team Alpha at Harbor City Gulls", f.nflGame.start, station_rank::elsewhere, false),
      listing("e|s3", "Team Alpha at Harbor City Gulls", f.nflGame.start, station_rank::market, false),
      listing("e|s4", "Team Alpha at Harbor City Gulls", f.nflGame.start, station_rank::elsewhere, false),
      listing("e|pre", "NFL Pregame: Team Alpha at Harbor City Gulls", f.nflGame.start - 3600, 4, false),
      listing("e|late", "Team Alpha at Harbor City Gulls", f.nflGame.start + 3 * 3600, 4, false),
      venue,
      described,
  };
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* nfl = findGame(out->events, "NFL|1");
  REQUIRE(nfl);
  // My Lineup first, then stations by rank; beyond three guide stations folded.
  CHECK(ids(nfl->sources) == std::vector<std::string>{"e|fox13", "e|s3", "e|s1", "e|s2"});
  CHECK(nfl->sources[0].kind == EventSource::Kind::lineup);
  CHECK(nfl->sources[0].detail == "In My Lineup");
  CHECK(nfl->sources[1].detail == "Your local station");
  CHECK(nfl->sources[2].detail == "In your channels");
  CHECK(nfl->moreStations == 1);
  CHECK(nfl->listingEnd == f.nflGame.start + 3 * 3600);
  const SportsEvent* nba = findGame(out->events, "NBA|2");
  REQUIRE(nba);
  CHECK(ids(nba->sources) == std::vector<std::string>{"e|kotr", "e|desc"});
  CHECK(out->stats.venueListings == 1);
  CHECK(out->stats.describedListings == 1);
}

TEST_CASE("feeds no game matched: grouped by matchup or same title, named teams, stale hidden") {
  Fixture f;
  BuildInput in = f.input();
  in.rosters = {RosterLeague{"NFL", "NFL", false, {f.nflGame.home, f.nflGame.away, team("9", "Desert Foxes")}},
                RosterLeague{"EPL", "Soccer", false, {team("20", "Harbor United FC"), team("21", "Riverside Rovers")}}};
  in.schools = {team("50", "Harbor State Gulls", std::nullopt, std::string("Harbor State")),
                team("51", "Lakeside College Owls", std::nullopt, std::string("Lakeside College"))};
  Seconds at = f.now + 3600;
  in.events = {
      // Two feeds of one matchup, either team order: one tile.
      provider("u1", "Rugby: Harbor Sharks vs Coast Kings", "Rugby", at, {"r|1"}),
      provider("u2", "Rugby: Coast Kings @ Harbor Sharks", "Rugby", at + 1800, {"r|2"}),
      // The same plain title from two groups at the same time: one tile.
      provider("p1", "Darts Night", "Darts", at, {"d|1"}, "g1"),
      provider("p2", "Darts Night", "Darts", at + 300, {"d|2"}, "g2"),
      // Two teams of a complete league with no game between them: hidden.
      provider("s1", "Desert Foxes vs Harbor City Gulls", "NFL", at, {"n|1"}),
      // Soccer clubs named in a feed of unknown sport: the tile gets their logos.
      provider("c1", "Harbor United FC vs Riverside Rovers", "Other", at, {"c|1"}),
      // A college volleyball match: school logos.
      provider("v1", "NCAA Volleyball: Harbor State vs Lakeside College", "Volleyball", at, {"v|1"}),
  };
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* rugby = find(out->events, "u1");
  REQUIRE(rugby);
  CHECK(ids(rugby->sources) == std::vector<std::string>{"r|1", "r|2"});
  CHECK_FALSE(find(out->events, "u2"));
  const SportsEvent* darts = find(out->events, "p1");
  REQUIRE(darts);
  CHECK(ids(darts->sources) == std::vector<std::string>{"d|1", "d|2"});
  CHECK(out->stats.mergedPlain == 1);
  CHECK_FALSE(find(out->events, "s1"));
  CHECK(out->stats.stale == 1);
  const SportsEvent* soccer = find(out->events, "c1");
  REQUIRE(soccer);
  CHECK(soccer->league == "Soccer");
  CHECK(soccer->leagueShort == "EPL");
  REQUIRE(soccer->teams);
  CHECK((*soccer->teams)[0].id == "20");
  const SportsEvent* volley = find(out->events, "v1");
  REQUIRE(volley);
  REQUIRE(volley->teams);
  CHECK((*volley->teams)[0].id == "50");
  CHECK((*volley->teams)[1].id == "51");
  CHECK(volley->leagueShort == "VOLLEY");
}

TEST_CASE("unmatched fight feeds: one tile per card part, named by the English feed") {
  Fixture f;
  BuildInput in = f.input();
  Seconds at = f.now + 7200;
  in.events = {
      provider("f1", "(ESPA\xC3\x91OL) UFC 333 PRELIMINARES", "Fighting", at, {"es|1"}, "es", true),
      provider("f2", "UFC 333 Prelims", "Fighting", at + 600, {"en|1"}),
      provider("f3", "UFC 333 Main Card", "Fighting", at + 7200, {"en|2"}),
  };
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* prelims = find(out->events, "f2");
  REQUIRE(prelims);
  CHECK(prelims->title == "UFC 333 Prelims");
  CHECK(ids(prelims->sources) == std::vector<std::string>{"en|1", "es|1"});
  CHECK(find(out->events, "f3"));
  CHECK_FALSE(find(out->events, "f1"));
}

TEST_CASE("channel sources come first; previous tile ids are kept") {
  Fixture f;
  BuildInput in = f.input();
  EventSource station = sportsdata::source("e|fox13", EventSource::Kind::lineup, false, 0, "In My Lineup");
  in.channelSources["NFL|1"] = {station};
  in.events = {provider("ev1", "Team Alpha @ Harbor City Gulls", "NFL", f.nflGame.start, {"xt|1"})};
  in.previousIDs["NFL|1"] = "old-id";
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* nfl = findGame(out->events, "NFL|1");
  REQUIRE(nfl);
  CHECK(nfl->id == "old-id");
  CHECK(ids(nfl->sources) == std::vector<std::string>{"e|fox13", "xt|1"});
  // A stop request aborts the build.
  in.shouldStop = [] { return std::optional<std::string>("low memory"); };
  CHECK_FALSE(build(in));
}

TEST_CASE("racing feeds go to the session they name, cameras to no tile") {
  Fixture f;
  SportsGame race = game("F1", "Motorsport", "700|3", team("r700", "Harbor GP"), team("r700", "Harbor GP"),
                         utc(2026, 10, 25, 19, 0));
  race.racing = true;
  race.cardKey = "f1|700|race";
  SportsGame qual = race;
  qual.id = "F1|700|2";
  qual.cardKey = "f1|700|qual";
  qual.start = utc(2026, 10, 24, 21, 0);
  BuildInput in = f.input();
  in.games.push_back(qual);
  in.games.push_back(race);
  in.isCameraName = [](std::string_view name) { return name.find("Onboard") != std::string_view::npos; };
  in.events = {
      provider("r1", "Formula 1: Harbor: Race", "Motorsport", race.start - 1800, {"f|1"}),
      provider("r2", "Formula 1: Harbor: Race [Main Broadcast]", "Motorsport", race.start - 1800, {"f|2"}),
      provider("r3", "F1 Onboard Camera 1", "Motorsport", race.start, {"f|3"}),
  };
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* tile = findGame(out->events, "F1|700|3");
  REQUIRE(tile);
  // The main broadcast first.
  CHECK(ids(tile->sources) == std::vector<std::string>{"f|2", "f|1"});
  CHECK(find(out->events, "r3"));  // a camera stays out of the race tile
}

TEST_CASE("doubleheaders: a feed with no time goes to the game on now, or the one it names") {
  Seconds first = utc(2026, 7, 4, 17, 5);    // 1:05 PM Eastern
  Seconds second = utc(2026, 7, 4, 21, 10);  // 5:10 PM
  SportsTeam home = team("10", "Harbor City Gulls", std::string("HCG"));
  SportsTeam away = team("11", "Team Alpha", std::string("TMA"));
  BuildInput in;
  in.games = {game("MLB", "MLB", "g1", home, away, first, {}), game("MLB", "MLB", "g2", home, away, second, {})};
  in.zone = time::Zone::usEastern();
  auto feedFor = [&](const std::string& title) {
    return std::vector<ProviderEvent>{provider("p1", title, "MLB", std::nullopt, {"xt|7"}, "slots", false, true)};
  };
  auto gameOf = [&](Seconds now, const std::string& title) -> std::string {
    in.now = now;
    in.events = feedFor(title);
    auto out = build(in);
    REQUIRE(out);
    for (const SportsEvent& e : out->events)
      if (e.game && !e.sources.empty()) return e.game->id;
    return "";
  };
  // Morning: game 1. During game 1: game 1. Once game 2 has started: game 2,
  // even before game 1 is marked over.
  CHECK(gameOf(utc(2026, 7, 4, 14, 0), "Team Alpha @ Harbor City Gulls") == "MLB|g1");
  CHECK(gameOf(utc(2026, 7, 4, 18, 0), "Team Alpha @ Harbor City Gulls") == "MLB|g1");
  CHECK(gameOf(utc(2026, 7, 4, 21, 30), "Team Alpha @ Harbor City Gulls") == "MLB|g2");
  // A title naming the game.
  CHECK(gameOf(utc(2026, 7, 4, 14, 0), "Team Alpha @ Harbor City Gulls (Game 2)") == "MLB|g2");
  CHECK(gameOf(utc(2026, 7, 4, 21, 30), "Team Alpha @ Harbor City Gulls Gm 1") == "MLB|g1");

  // Not a doubleheader sport: the matchup's next game, as before.
  in.games = {game("NBA", "NBA", "n1", home, away, first, {}), game("NBA", "NBA", "n2", home, away, second, {})};
  in.now = utc(2026, 7, 4, 21, 30);
  in.events = {provider("p2", "Team Alpha @ Harbor City Gulls", "NBA", std::nullopt, {"xt|8"}, "slots", false, true)};
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* n1 = findGame(out->events, "NBA|n1");
  REQUIRE(n1);
  CHECK(ids(n1->sources) == std::vector<std::string>{"xt|8"});
}

TEST_CASE("golf feeds go to the tournament they name, on its tour and day") {
  auto tournament = [](const std::string& tour, const std::string& id, const std::string& name, Seconds start) {
    SportsTeam side = team("g" + id, name);
    SportsGame g = game(tour, "Golf", id, side, side, start, {});
    g.golf = true;
    return g;
  };
  Seconds round = utc(2026, 4, 11, 11, 0);
  BuildInput in;
  in.games = {tournament("PGA", "401", "Harbor Championship", round),
              tournament("LPGA", "501", "Valley Ladies Classic", round)};
  in.now = round + 3600;
  in.zone = time::Zone::usEastern();
  auto target = [&](const std::string& title, std::optional<Seconds> start = std::nullopt) -> std::string {
    in.events = {provider("p", title, "Golf", start, {"xt|g"}, "golf")};
    auto out = build(in);
    REQUIRE(out);
    for (const SportsEvent& e : out->events)
      if (e.game && !e.sources.empty()) return e.game->id;
    return "";
  };
  CHECK(target("Golf: Harbor Championship - Round 3") == "PGA|401");
  CHECK(target("LPGA: Valley Ladies Classic Featured Groups") == "LPGA|501");
  CHECK(target("PGA Tour Live: Featured Holes") == "PGA|401");   // names the tour only
  CHECK(target("Golf: Harbor Championship", round + 3 * 86'400).empty());  // another day
  CHECK(target("Golf: Some Other Open").empty());
}
