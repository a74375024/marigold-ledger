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
  e.lang = spanish ? "es" : "en";
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
  // The competition after " · " and a club's initials and year aren't the side.
  CHECK(pairKey("Harbor vs Valley \xc2\xb7 Cup Women") == "harbor|valley");
  CHECK(pairKey("VfL Harbor v TSG 1899 Valley") == "harbor|valley");
  CHECK(pairKey("FC vs Valley") == "fc|valley");  // nothing left: as written
}

TEST_CASE("team variants: a name without its bracketed place") {
  SportsTeam club = team("9", "Central Harbor (North Bay)");
  std::vector<std::string> variants = teamVariants(club, false);
  CHECK(std::find(variants.begin(), variants.end(), "central harbor") != variants.end());
  // Not for colleges, nor a short or generic word.
  SportsTeam school = team("10", "St. Harbor (IL) Gulls");
  std::vector<std::string> schoolVariants = teamVariants(school, true);
  CHECK(std::find(schoolVariants.begin(), schoolVariants.end(), "st harbor") == schoolVariants.end());
  std::vector<std::string> generic = teamVariants(team("11", "City (East)"), false);
  CHECK(std::find(generic.begin(), generic.end(), "city") == generic.end());
}

TEST_CASE("a club's women's side never meets its men's game") {
  SportsGame men = game("SOC", "Soccer", "m1", team("h1", "Harbor Rovers"), team("v1", "Valley United FC"),
                        utc(2026, 11, 6, 23, 0));
  SportsGame women = game("SOCW", "Soccer", "w1", team("h2", "Harbor Rovers Women"), team("v2", "Valley Women"),
                          utc(2026, 11, 6, 20, 0));
  TeamIndex index({men, women});
  auto any = [](int) { return true; };
  CHECK(index.matches("Harbor Rovers vs Valley United FC", any) == std::vector<int>{0});
  CHECK(index.matches("Harbor Rovers Women vs. Valley Women", any) == std::vector<int>{1});
  // Without the women's game, the women's feed matches nothing.
  TeamIndex menOnly({men});
  CHECK(menOnly.matches("Harbor Rovers Women vs. Valley United FC Women", any).empty());
}

TEST_CASE("race weekend talk and analysis are shows; a session isn't") {
  CHECK(isStudioShow("C\xc3\xb3" "digo F1 \xc2\xb7 GP de Harbor \xc2\xb7 F1"));
  CHECK(isStudioShow("An\xc3\xa1lise ao GP de Harbor \xc2\xb7 DAZN F1 Debrief"));
  CHECK(isStudioShow("GP de Harbor - Previo Carrera"));
  CHECK(isStudioShow("Harbor GP Analysis"));
  CHECK_FALSE(isStudioShow("GP de Harbor - Carrera"));
  CHECK_FALSE(isStudioShow("Formula 1: Harbor: Race"));
}

TEST_CASE("a Grand Prix whose race has run, or that isn't this weekend's, is a replay") {
  Seconds now = utc(2026, 11, 2, 17, 0);
  SportsGame race = game("F1", "Motorsport", "800|5", team("r800", "Mexico City GP"), team("r800", "Mexico City GP"),
                         utc(2026, 11, 1, 20, 0));
  race.racing = true;
  race.cardKey = "f1|800|race";
  race.completed = true;
  BuildInput in;
  in.games = {race};
  in.now = now;
  in.zone = time::Zone::usEastern();
  in.events = {
      provider("m1", "2026 Formula 1: Mexico City Grand Prix", "Motorsport", utc(2026, 11, 3, 0, 30), {"f|1"}),
      provider("m2", "Live \xc2\xb7 Storbritanniens Grand Prix \xc2\xb7 Formel 1 Kvalifikation 2026", "Other",
               std::nullopt, {"f|2"}),
      provider("m3", "Formula 1: Las Vegas Grand Prix Preview", "Motorsport", utc(2026, 11, 19, 20, 0), {"f|3"}),
      provider("m4", "MotoGP: Grand Prix of Harbor", "Motorsport", utc(2026, 11, 2, 18, 0), {"f|4"}),
  };
  auto out = build(in);
  REQUIRE(out);
  CHECK_FALSE(find(out->events, "m1"));
  REQUIRE(out->stats.hidden.count("f|1"));
  CHECK(out->stats.hidden.at("f|1") == "a replay: the race already ran");
  CHECK_FALSE(find(out->events, "m2"));
  CHECK(find(out->events, "m3"));  // dated well ahead: not a replay
  CHECK(find(out->events, "m4"));  // not F1
  // Before the race, the weekend's own feeds stay.
  in.now = utc(2026, 10, 31, 12, 0);
  in.events = {provider("m5", "2026 Formula 1: Mexico City Grand Prix", "Motorsport", utc(2026, 10, 31, 18, 0), {"f|5"})};
  out = build(in);
  REQUIRE(out);
  CHECK(find(out->events, "m5"));
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
  // TSN's SportsCentre, by name and as "SC".
  CHECK(isStudioShow("SPORTSCENTRE B"));
  CHECK(isStudioShow("SC with Jay Onrait D"));
  CHECK(isStudioShow("SC: Top Characters in Sports A"));
  CHECK_FALSE(isStudioShow("SC Freiburg vs Union Berlin"));
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
  // Build 132: the viewer's languages (US Spanish picked: Spanish).
  in.languages = {"es"};
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

TEST_CASE("a match over several days takes feeds on any of its days") {
  SportsGame test = game("CRIC", "Cricket", "78", team("1", "Harbor"), team("2", "India"), utc(2026, 10, 24, 4, 0), {});
  test.end = utc(2026, 10, 28, 4, 0);
  BuildInput in;
  in.games = {test};
  in.now = utc(2026, 10, 26, 6, 0);
  in.zone = time::Zone::usEastern();
  in.events = {provider("p", "Cricket: Harbor vs India - 1st Test, Day 3", "Cricket", utc(2026, 10, 26, 4, 30), {"xt|c"})};
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* e = findGame(out->events, "CRIC|78");
  REQUIRE(e);
  CHECK(ids(e->sources) == std::vector<std::string>{"xt|c"});
}

TEST_CASE("feeds that aren't events: shows and replays of team sports, fight cards that are over") {
  Fixture f;
  BuildInput in = f.input();
  // Next Saturday's card is on the schedule; last Saturday's isn't any more.
  SportsGame card = game("UFC", "Fighting", "600|main", team("f1", "Max Stone"), team("f2", "Lee River"),
                         f.now + 6 * 86'400);
  card.cardKey = "ufc|334|main";
  in.games.push_back(card);
  Seconds at = f.now + 1800;
  in.events = {
      // Shows about a team sport: no two sides, no game.
      provider("s1", "NBA Today", "NBA", at, {"x|1"}),
      provider("s2", "NFL Insiders", "NFL", at, {"x|2"}),
      provider("s3", "College Football Live", "College Football", at, {"x|3"}),
      provider("s4", "Premier League Tonight", "Soccer", at, {"x|4"}),
      // An old game: a matchup marked as a classic or replay.
      provider("r1", "Desert Foxes vs Mountain Goats (Classic)", "NFL", at, {"x|5"}),
      provider("r2", "Replay: Lakeside vs Harbor State", "College Football", at, {"x|6"}),
      // A game the schedule doesn't have (a smaller conference): kept.
      provider("g1", "Lakeside vs Harbor State", "College Football", at, {"x|7"}),
      // A whip-around's feed shows live games: kept.
      provider("w1", "NFL RedZone", "NFL", at, {"x|8"}),
      // Sports without two sides keep their titles.
      provider("t1", "PGA Tour Live", "Golf", at, {"x|9"}),
      // Last week's card, and a promotion with no schedule (kept).
      provider("c1", "UFC 333 Prelims", "Fighting", at, {"x|10"}),
      provider("c2", "UFC 333: Main Card (Replay)", "Fighting", std::nullopt, {"x|11"}),
      provider("c3", "PFL 9 Prelims", "Fighting", at, {"x|12"}),
  };
  auto out = build(in);
  REQUIRE(out);
  for (const char* id : {"s1", "s2", "s3", "s4", "r1", "r2", "c1", "c2"}) {
    INFO(std::string(id));
    CHECK_FALSE(find(out->events, id));
  }
  for (const char* id : {"g1", "w1", "t1", "c3"}) {
    INFO(std::string(id));
    CHECK(find(out->events, id));
  }
  CHECK(out->stats.shows == 7);
  CHECK(out->stats.pastCards == 1);
  // Each left-out feed says why (the Live Events report).
  CHECK(out->stats.hidden.at("x|1") == "a show: a team sport with no matchup");
  CHECK(out->stats.hidden.at("x|5") == "a replay, classic or highlights");
  CHECK(out->stats.hidden.at("x|10") == "a fight card that's over");
  CHECK_FALSE(out->stats.hidden.count("x|7"));
}

TEST_CASE("titles with no day: leftovers of a feed dated before today, and the rest of their group") {
  Fixture f;
  BuildInput in = f.input();
  Seconds tonight = utc(2026, 10, 25, 22, 0);
  // Last Thursday's dated feed, which the channel list no longer shows.
  in.pastEvents = {provider("past|1", "Matchroom Boxing: Stone vs River", "", utc(2026, 10, 22, 18, 0), {"p|1"}),
                   provider("past|2", "Old Card: Ash vs Elm", "", utc(2026, 10, 21, 18, 0), {"p|2"})};
  in.events = {
      // The same fight, day-less ("UK 5:00pm"): a leftover; and the other
      // slot of that group, which the provider stopped updating too.
      provider("b1", "STONE vs RIVER UK", "Fighting", tonight, {"b|1"}, "boxing #", false, true),
      provider("b2", "BOLT vs CRANE", "Fighting", tonight, {"b|2"}, "boxing #", false, true),
      // A group with a day-less title that is dated today elsewhere: current,
      // so its other day-less titles stay, but not one that is a leftover.
      provider("c1", "Wolf vs Bear", "Fighting", tonight, {"c|1"}, "ppv #", false, true),
      provider("c2", "Wolf vs Bear", "Fighting", tonight + 60, {"c|2"}, "ppv dated #"),
      provider("c3", "ASH vs ELM", "Fighting", tonight, {"c|3"}, "ppv #", false, true),
      provider("c4", "Lark vs Finch", "Fighting", tonight, {"c|4"}, "ppv #", false, true),
      // Another group, nothing known about it: stays.
      provider("d1", "Pine vs Cedar", "Fighting", tonight, {"d|1"}, "other #", false, true),
  };
  auto out = build(in);
  REQUIRE(out);
  for (const char* id : {"b1", "b2", "c3"}) {
    INFO(std::string(id));
    CHECK_FALSE(find(out->events, id));
  }
  for (const char* id : {"c4", "d1"}) {
    INFO(std::string(id));
    CHECK(find(out->events, id));
  }
  CHECK(std::none_of(out->events.begin(), out->events.end(),
                     [](const SportsEvent& e) { return e.id.rfind("past|", 0) == 0; }));
}

TEST_CASE("college football: a day-less title pairing a school with one it doesn't play this week is a leftover") {
  Fixture f;
  BuildInput in = f.input();
  Seconds saturday = utc(2026, 10, 31, 16, 0), tonight = utc(2026, 10, 25, 22, 0);
  in.games.push_back(game("NCAAF", "College Football", "c1", team("L", "Lakeside Lakers", std::string("LKS"), std::string("Lakeside")),
                          team("H", "Harbor State Herons", std::string("HBS"), std::string("Harbor State")), saturday));
  in.games.push_back(game("NCAAF", "College Football", "c2", team("M", "Mesa Miners", std::string("MESA"), std::string("Mesa")),
                          team("P", "Plains Pumas", std::string("PLNS"), std::string("Plains")), saturday));
  in.events = {
      // Last week's slots: two schools of this week's schedule, or one named
      // by its abbreviation.
      provider("l1", "Lakeside @ Mesa", "College Football", tonight, {"n|1"}, "ncaaf #", false, true),
      provider("l2", "LKS at Coastal", "College Football", tonight, {"n|2"}, "ncaaf #", false, true),
      provider("l3", "Plains @ Harbor State", "College Football", tonight, {"n|3"}, "ncaaf #", false, true),
      // A misspelt school in that same group of leftovers.
      provider("l4", "Coastal at Lakesyde", "College Football", tonight, {"n|4"}, "ncaaf #", false, true),
      // Schools the schedule doesn't cover, in another group: stays.
      provider("k1", "Coastal @ Riverside", "College Football", tonight, {"d|1"}, "d3 #", false, true),
      // The game itself, dated.
      provider("g1", "Lakeside vs Harbor State", "College Football", saturday, {"n|5"}, "ncaaf dated #"),
  };
  auto out = build(in);
  REQUIRE(out);
  for (const char* id : {"l1", "l2", "l3", "l4"}) {
    INFO(std::string(id));
    CHECK_FALSE(find(out->events, id));
  }
  CHECK(find(out->events, "k1"));
  const SportsEvent* g = findGame(out->events, "NCAAF|c1");
  REQUIRE(g);
  CHECK(ids(g->sources) == std::vector<std::string>{"n|5"});
}

TEST_CASE("a feed under the wrong sport's name goes to the game at its time; an unknown sport's isn't a replay") {
  Fixture f;
  BuildInput in = f.input();
  Seconds at = utc(2026, 10, 26, 0, 0);
  in.games.push_back(game("NHL", "NHL", "h1", team("C", "Carolina Storm", std::string("CAR")),
                          team("B", "Boston Bruisers", std::string("BOS")), at));
  in.events = {
      // "NCAA Football: BOS vs. CAR • WWE NXT (BOS vs. CAR)" from a channel
      // listing several events: hockey, at the game's time.
      provider("x1", "BOS vs. CAR", "College Football", at + 20 * 60, {"e|1"}, "espn+ #"),
      // The same two teams' other sport, days before the game: its own tile.
      provider("x2", "Boston Bruisers vs Carolina Storm", "", at - 3 * 86'400, {"e|2"}, "espn+ #"),
  };
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* g = findGame(out->events, "NHL|h1");
  REQUIRE(g);
  std::vector<std::string> feeds = ids(g->sources);
  CHECK(std::find(feeds.begin(), feeds.end(), "e|1") != feeds.end());
  CHECK(find(out->events, "x2"));
}

TEST_CASE("tennis: a tournament's feed is each of that day's matches") {
  Seconds noon = utc(2026, 10, 28, 3, 0);
  auto match = [&](const std::string& league, const std::string& id, const std::string& tournament,
                   const std::string& venue, Seconds start) {
    SportsGame g = game(league, "Tennis", id, team(id + "a", "Ann Player"), team(id + "b", "Bea Racket"), start);
    g.tennis = true;
    g.tournament = tournament;
    g.venue = venue;
    return g;
  };
  std::vector<SportsGame> games = {
      match("WTA", "1", "Harbor Pan Pacific Open Tennis", "Tokyo, Japan", noon),
      match("WTA", "2", "Harbor Pan Pacific Open Tennis", "Tokyo, Japan", noon + 2 * 3600),
      match("WTA", "3", "Valley Open", "Valley City, Somewhere", noon + 3600),
      match("ATP", "4", "Coast Indoors", "Coastville, Elsewhere", noon),
      match("WTA", "5", "Harbor Pan Pacific Open Tennis", "Tokyo, Japan", noon + 3 * 86'400),  // another day
  };
  std::vector<int> all = {0, 1, 2, 3, 4};
  CHECK(tennisLevel("WTA", "Harbor Pan Pacific Open Tennis") == std::optional<std::string>("500"));
  CHECK_FALSE(tennisLevel("WTA", "Valley Open"));
  // By its level: the only WTA 500 that day.
  CHECK(tennisTargets("Day 3 WTA 500", "", noon - 3600, noon, all, games) == std::vector<int>{0, 1});
  // By its name or city.
  CHECK(tennisTargets("Tennis: Valley Open", "", std::nullopt, noon, all, games) == std::vector<int>{2});
  CHECK(tennisTargets("WTA Tokyo Day 3", "", std::nullopt, noon, all, games) == std::vector<int>{0, 1});
  // The only ATP event.
  CHECK(tennisTargets("ATP Tennis", "", std::nullopt, noon, all, games) == std::vector<int>{3});
  // A level nothing on has, two WTA events and no level, or the players named: none.
  CHECK(tennisTargets("Day 3 WTA 1000", "", std::nullopt, noon, all, games).empty());
  CHECK(tennisTargets("WTA Tennis", "", std::nullopt, noon, all, games).empty());
  CHECK(tennisTargets("Player vs Racket", "", std::nullopt, noon, all, games).empty());
}

namespace {

// Saturday's semifinals in Vienna and Basel, and Tokyo's: courts for
// venues, as the tours' feed has them.
struct TennisDay {
  Seconds start = utc(2026, 10, 31, 13, 0);
  std::vector<SportsGame> games;
  TennisDay() {
    auto match = [&](const std::string& league, const std::string& id, const std::string& tournament,
                     const std::string& court, Seconds at) {
      SportsGame g = game(league, "Tennis", id, team(id + "a", "Ann Player"), team(id + "b", "Bea Racket"), at);
      g.tennis = true;
      g.tournament = tournament;
      g.venue = court;
      return g;
    };
    games = {
        match("ATP", "v1", "Erste Bank Open", "Centre Court", start - 3600),  // under way
        match("ATP", "v2", "Erste Bank Open", "Centre Court", start + 2 * 3600),
        match("ATP", "b1", "Swiss Indoors Basel", "Center Court", start + 1800),
        match("WTA", "t1", "Toray Pan Pacific Open", "Center Court", start),
        match("ATP", "v3", "Erste Bank Open", "Centre Court", start + 24 * 3600),  // the final, tomorrow
        match("ATP", "v4", "Erste Bank Open", "Centre Court", start - 3 * 3600),   // over
    };
    games[5].completed = true;
  }
};

}  // namespace

TEST_CASE("tennis: a round for the second side, cities in their local names") {
  TennisDay day;
  std::vector<int> all = {0, 1, 2, 3, 4, 5};
  Seconds at = day.start - 3600;
  CHECK(tennisTargets("ATP 500 Tennis: Vienna vs Semifinals", "", at, at, all, day.games) ==
        std::vector<int>{5, 0, 1});
  CHECK(tennisTargets("ATP 500 Tennis: Basel vs Semifinals", "", at, at, all, day.games) == std::vector<int>{2});
  CHECK(tennisTargets("ATP Tennis: Wien", "", at, at, all, day.games) == std::vector<int>{5, 0, 1});
  CHECK(tennisTargets("ATP Tennis: Basilea", "", at, at, all, day.games) == std::vector<int>{2});
  CHECK(tennisTargets("ATP 500 VIENNA", "", at, at, all, day.games) == std::vector<int>{5, 0, 1});
  CHECK(tennisTargets("Tokyo-WTA Semifinals", "", at, at, all, day.games, true) == std::vector<int>{3});
  // Players, a court or no tournament named: none.
  CHECK(tennisTargets("ATP 500 Tennis: Vienna vs Racket", "", at, at, all, day.games).empty());
  CHECK(tennisTargets("Live: Center Court Live", "", at, at, all, day.games).empty());
  CHECK(tennisTargets("ATP Tennis", "", at, at, all, day.games, true).empty());
  CHECK(namesTennisEvent("Center Court Live Vienna-ATP & Basel-ATP Semifinals"));
  CHECK_FALSE(namesTennisEvent("Tennis Channel Live"));
  CHECK_FALSE(namesTennisEvent("Live: Center Court Live"));
}

TEST_CASE("tennis: a channel's coverage of named tournaments, after the matches' own feeds") {
  TennisDay day;
  BuildInput in;
  in.games = day.games;
  in.now = day.start + 1800;
  in.events = {provider("p1", "ATP 500 Tennis: Vienna vs Semifinals", "Tennis", day.start - 3600, {"xt|v"}),
               provider("p2", "ATP Tennis: Wien", "Tennis", day.start - 3600, {"xt|w"})};
  auto coverage = [&](const std::string& id, const std::string& title, Seconds start, Seconds end) {
    GuideListing l;
    l.entryID = id;
    l.inLineup = true;
    l.title = title;
    l.start = start;
    l.end = end;
    l.rank = station_rank::lineup;
    l.tennisCoverage = true;
    return l;
  };
  in.listings = {
      coverage("e|tennis", "Center Court Live Vienna-ATP & Basel-ATP Semifinals", day.start - 1800,
               day.start + 4 * 3600),
      // Tonight, after the semifinals: nothing on then.
      coverage("e|later", "Center Court Live Vienna-ATP Semifinals", day.start + 7 * 3600, day.start + 10 * 3600),
      // No tournament named.
      coverage("e|bare", "Live: Center Court Live", day.start - 1800, day.start + 4 * 3600),
  };
  auto out = build(in);
  REQUIRE(out);
  // Vienna: its feeds first, then the channel.
  for (const char* id : {"ATP|v1", "ATP|v2"}) {
    const SportsEvent* vienna = findGame(out->events, id);
    REQUIRE(vienna);
    CHECK(ids(vienna->sources) == std::vector<std::string>{"xt|v", "xt|w", "e|tennis"});
    CHECK(vienna->sources[2].kind == EventSource::Kind::team);
    CHECK(vienna->sources[2].detail == "Tournament coverage");
  }
  const SportsEvent* basel = findGame(out->events, "ATP|b1");
  REQUIRE(basel);
  CHECK(ids(basel->sources) == std::vector<std::string>{"e|tennis"});
  // Not named, not on during the listing, or over: nothing.
  CHECK(findGame(out->events, "WTA|t1")->sources.empty());
  CHECK(ids(findGame(out->events, "ATP|v3")->sources).empty());
  std::vector<std::string> over = ids(findGame(out->events, "ATP|v4")->sources);
  CHECK(std::find(over.begin(), over.end(), "e|tennis") == over.end());
  // Never a tile of its own.
  for (const SportsEvent& e : out->events) CHECK(e.game);
}

TEST_CASE("shows: titles that name no teams, a network and a sport, old years") {
  CHECK(isTeamSportShow("The Harbor Collective", "Basketball"));
  CHECK(isTeamSportShow("Hawk Talk with Coach Harbor", "Football"));
  CHECK_FALSE(isTeamSportShow("Harbor Hawks vs Valley Bears", "Football"));
  CHECK_FALSE(isTeamSportShow("Harbor Bowl", "College Football"));
  CHECK_FALSE(isTeamSportShow("Harbor Series Game 4", "MLB"));
  CHECK_FALSE(isTeamSportShow("Harbor Open", "Golf"));  // not a team sport
  CHECK(isStudioShow("Baseball Tonight"));
  CHECK(isStudioShow("HarborWrestling Radio Live"));
  CHECK(isStudioShow("Race Day Live"));
  CHECK_FALSE(isStudioShow("Tonight: Harbor vs Valley"));
  CHECK(isStudioShow("HPL Auction"));
  CHECK(isNetworkAndSport("ESPN B\xC3\xA9isbol"));
  CHECK(isNetworkAndSport("Fox Soccer"));
  CHECK_FALSE(isNetworkAndSport("Harbor Soccer"));
  Seconds now = utc(2026, 10, 28, 12, 0);
  CHECK(namesPastYear("2024 Round 5: Harbor City", now));
  CHECK_FALSE(namesPastYear("Harbor v Valley PREM Rugby Round 5 2026/27", now));
  CHECK_FALSE(namesPastYear("2025-26 Harbor Cup", now));
  CHECK_FALSE(namesPastYear("Harbor City GP 2026", now));
}

TEST_CASE("team names: channels' spellings the schedule writes another way") {
  std::vector<SportsGame> games = {
      sportsdata::game("BUN", "Soccer", "1", sportsdata::team("1", "Hamburg SV"),
                       sportsdata::team("2", "Harbor Kickers"), utc(2026, 11, 1, 16, 30))};
  TeamIndex index(games);
  auto all = [](int) { return true; };
  CHECK(index.matches("Harbor Kickers vs. Hamburger SV", all) == std::vector<int>{0});
  // A club by its first word, when no other club of the sport has it.
  std::vector<SportsGame> rugby = {
      sportsdata::game("T14", "Rugby", "1", sportsdata::team("1", "Harborville Herault"),
                       sportsdata::team("2", "Valleyton Olympique"), utc(2026, 10, 31, 15, 35)),
      sportsdata::game("T14", "Rugby", "2", sportsdata::team("3", "Coveland Rouge"),
                       sportsdata::team("4", "Coveland Bleu"), utc(2026, 10, 31, 15, 35))};
  TeamIndex clubs(rugby);
  CHECK(clubs.matches("Harborville vs Valleyton", all) == std::vector<int>{0});
  CHECK(clubs.matches("Coveland vs Valleyton", all).empty());  // two clubs start "Coveland"
}

TEST_CASE("replays: a date in the title that's gone by") {
  Seconds now = utc(2026, 10, 28, 12, 0);  // a Wednesday
  CHECK(namesPastDate("Harbor vs Valley 9 Feb", now));
  CHECK(namesPastDate("Harbor v Valley Feb 9", now));
  CHECK(namesPastDate("Harbor vs Valley Sun 6th", now));  // no Sunday the 6th near
  CHECK(namesPastDate("LIGA \xC2\xB7 Harbor vs Valley \xC2\xB7 \xC2\xB7 5.28", now));
  CHECK_FALSE(namesPastDate("Harbor vs Valley Sat 31st", now));
  CHECK_FALSE(namesPastDate("Harbor vs Valley 28 Oct", now));
  CHECK_FALSE(namesPastDate("Harbor vs Valley Nov 7", now));
  CHECK_FALSE(namesPastDate("Harbor 500 May 2026", now));
  CHECK_FALSE(namesPastDate("Harbor vs Valley 7:30", now));
  // Late December: early January is next week.
  CHECK_FALSE(namesPastDate("Harbor vs Valley 3 Jan", utc(2026, 12, 28, 12, 0)));
}

TEST_CASE("team names: schools by their short names") {
  SportsTeam home = sportsdata::team("1", "Massachusetts Minutewomen");
  home.location = "Massachusetts";
  SportsTeam away = sportsdata::team("2", "Wisconsin-Harbor Hawks");
  away.location = "Wisconsin-Harbor";
  std::vector<SportsGame> games = {sportsdata::game("NCAAFH", "Field Hockey", "1", home, away, utc(2026, 10, 30, 18, 0))};
  TeamIndex index(games);
  auto all = [](int) { return true; };
  CHECK(index.matches("UW-Harbor vs UMass", all) == std::vector<int>{0});
}

TEST_CASE("a feed of no known sport: the schools' game at the feed's time") {
  Seconds friday = utc(2026, 10, 30, 23, 0);
  BuildInput in;
  in.games = {sportsdata::game("SOC", "Soccer", "1", sportsdata::team("1", "Harbor State"),
                               sportsdata::team("2", "Valley Tech"), friday - 86'400),
              sportsdata::game("NCAAWV", "Volleyball", "2", sportsdata::team("3", "Harbor State"),
                               sportsdata::team("4", "Valley Tech"), friday)};
  in.now = friday - 3 * 3600;
  in.zone = time::Zone::usEastern();
  in.events = {provider("f1", "Harbor State vs Valley Tech", "Other", friday, {"xt|1"})};
  auto out = build(in);
  REQUIRE(out);
  CHECK(std::any_of(out->events.begin(), out->events.end(), [](const SportsEvent& e) {
    return e.game && e.game->id == "NCAAWV|2" && !e.sources.empty();
  }));
}

TEST_CASE("a high-school feed named only by a school is left out") {
  Fixture f;
  BuildInput in = f.input();
  Seconds at = f.now + 1800;
  ProviderEvent school = provider("hs1", "CARROLLTON", "", at, {"hs|1"});
  ProviderEvent game = provider("hs2", "Carrollton vs Swan Valley", "", at, {"hs|2"});
  ProviderEvent named = provider("hs3", "GHSA Softball State Championship", "", at, {"hs|3"});
  school.highSchool = game.highSchool = named.highSchool = true;
  in.events = {school, game, named};
  auto out = build(in);
  REQUIRE(out);
  CHECK_FALSE(find(out->events, "hs1"));
  CHECK(out->stats.hidden.at("hs|1") == "a school's name alone: no sport or opponent");
  CHECK(find(out->events, "hs2"));
  CHECK(find(out->events, "hs3"));
}

// MARK: Build 125: tiles from the schedule

TEST_CASE("one tile per scheduled game, every feed that names it a source") {
  Fixture f;
  BuildInput in = f.input();
  const Seconds kickoff = f.nflGame.start;
  in.events = {
      provider("a", "NFL 12: Team Alpha @ Harbor City Gulls", "NFL", kickoff, {"xt|1"}),
      provider("b", "TMA vs HCG", "NFL", kickoff + 1800, {"xt|2"}, "other"),
      provider("c", "Harbor City Gulls v Team Alpha", "", kickoff + 3600, {"xt|3"}, "third"),
      // Only "1:00 PM" given: the day a guess.
      provider("d", "Team Alpha at Harbor City Gulls 1:00 PM", "NFL", kickoff, {"xt|4"}, "slots", false, true),
  };
  auto out = build(in);
  REQUIRE(out);
  int tiles = 0;
  for (const SportsEvent& e : out->events)
    if (!e.game) ++tiles;
  CHECK(tiles == 0);  // no feed-only copies
  const SportsEvent* tile = findGame(out->events, "NFL|1");
  REQUIRE(tile);
  std::vector<std::string> sources = ids(tile->sources);
  std::sort(sources.begin(), sources.end());
  CHECK(sources == std::vector<std::string>{"xt|1", "xt|2", "xt|3", "xt|4"});
  CHECK(out->stats.matchedEvents == 4);
}

TEST_CASE("a day-guessed slot takes its scheduled game's day and time, under any sport's label") {
  Fixture f;
  // Monday night's game, 8:15 PM Eastern; the slot was posted on Sunday,
  // so its day is guessed as Sunday.
  SportsGame monday = game("NFL", "NFL", "9", team("9", "Lake Pikes", std::string("LKP")),
                           team("10", "Mountain Goats", std::string("MTG")), utc(2026, 10, 27, 1, 15));
  const Seconds sundayGuess = utc(2026, 10, 26, 1, 15);
  for (const char* league : {"NFL", "Soccer"}) {
    INFO(league);
    BuildInput in = f.input();
    in.games.push_back(monday);
    in.events = {provider("m", "MNF Mountain Goats at Lake Pikes", league, sundayGuess, {"xt|m"}, "nfl slots",
                          false, true)};
    auto out = build(in);
    REQUIRE(out);
    const SportsEvent* tile = findGame(out->events, "NFL|9");
    REQUIRE(tile);
    CHECK(tile->start == monday.start);
    CHECK(ids(tile->sources) == std::vector<std::string>{"xt|m"});
    CHECK(tile->id == "m");  // the game's tile, under the slot's id: no copy of its own
    CHECK(out->stats.guessedAnySport == (std::string(league) == "Soccer" ? 1 : 0));
  }
  // Two games of other sports at that clock time: neither is picked.
  BuildInput in = f.input();
  in.games.push_back(monday);
  SportsGame other = monday;
  other.id = "NHL|9";
  other.league = other.leagueShort = "NHL";
  other.start = monday.start + 86'400;
  in.games.push_back(other);
  in.events = {provider("m", "MNF Mountain Goats at Lake Pikes", "Soccer", sundayGuess, {"xt|m"}, "nfl slots",
                        false, true)};
  auto out = build(in);
  REQUIRE(out);
  CHECK(out->stats.guessedAnySport == 0);
  const SportsEvent* unpicked = findGame(out->events, "NFL|9");
  REQUIRE(unpicked);
  CHECK(unpicked->sources.empty());
}

TEST_CASE("a day-guessed slot no schedule has that was there on an earlier day is a leftover") {
  Fixture f;
  BuildInput in = f.input();
  const Seconds today = utc(2026, 10, 25, 4, 0);  // midnight Eastern
  const Seconds yesterday = today - 86'400;
  const Seconds tonight = utc(2026, 10, 25, 23, 0);
  auto seen = std::make_shared<SlotsSeen>();
  (*seen)[slotKey("STONE vs RIVER UK")] = {yesterday - 3 * 86'400, yesterday};
  (*seen)[slotKey("Lark vs Finch")] = {today, today};
  (*seen)[slotKey("Harbor Prep vs Valley Prep")] = {yesterday, yesterday};
  (*seen)[slotKey("Gone vs Forgotten")] = {today - 12 * 86'400, today - 11 * 86'400};
  in.seen = seen;
  ProviderEvent school = provider("hs", "Harbor Prep vs Valley Prep", "Football", tonight, {"hs|1"}, "nfhs", false,
                                  true);
  school.highSchool = true;
  in.events = {
      // The same text as days ago, no game: a leftover.
      provider("old", "Stone vs River UK", "Fighting", tonight, {"b|1"}, "boxing", false, true),
      // First seen today, or never: stays (a tile only with the switch on).
      provider("new", "Lark vs Finch", "Fighting", tonight, {"b|2"}, "boxing", false, true),
      provider("fresh", "Pine vs Cedar", "Fighting", tonight, {"b|3"}, "boxing", false, true),
      // Dated: not a day-guessed slot.
      provider("dated", "Oak vs Ash", "Fighting", tonight, {"b|4"}, "boxing"),
      // High school: no schedule has it; shown only when picked.
      school,
  };
  auto out = build(in);
  REQUIRE(out);
  CHECK_FALSE(find(out->events, "old"));
  CHECK(out->stats.hidden.at("b|1") == kLeftoverNotScheduled);
  CHECK(out->stats.seenBefore == 1);
  for (const char* id : {"new", "fresh", "dated", "hs"}) {
    INFO(id);
    CHECK(find(out->events, id));
  }
  // Noted for the days after: first seen kept, last seen today; the
  // dated title isn't noted, and one unseen for 10 days is dropped.
  REQUIRE(static_cast<bool>(out->seen));
  const SlotsSeen& noted = *out->seen;
  CHECK(noted.at(slotKey("Stone vs River UK")).first == yesterday - 3 * 86'400);
  CHECK(noted.at(slotKey("Stone vs River UK")).last == today);
  CHECK(noted.at(slotKey("Pine vs Cedar")).first == today);
  CHECK_FALSE(noted.count(slotKey("Oak vs Ash")));
  CHECK_FALSE(noted.count(slotKey("Gone vs Forgotten")));
  // Nothing seen before: nothing hidden this way.
  in.seen.reset();
  out = build(in);
  REQUIRE(out);
  CHECK(find(out->events, "old"));
  CHECK(out->stats.seenBefore == 0);
}

TEST_CASE("slots seen: kept to the most recent, written and read back") {
  const time::Zone zone = time::Zone::usEastern();
  const Seconds now = utc(2026, 10, 25, 15, 0);
  SlotsSeen before;
  for (uint64_t i = 0; i < kSlotsSeenMost; ++i)
    before[i + 1] = {now - 9 * 86'400, now - 9 * 86'400 + static_cast<Seconds>(i)};
  SlotsSeen after = noteSlotsSeen(before, {"Harbor vs Valley", "Harbor  vs  VALLEY"}, now, zone);
  CHECK(after.size() == kSlotsSeenMost);
  CHECK(after.count(slotKey("Harbor vs Valley")));
  CHECK_FALSE(after.count(1));  // the least recently seen went
  CHECK(after.count(kSlotsSeenMost));
  // The same slots on the same day: the same record.
  CHECK(noteSlotsSeen(after, {"Harbor vs Valley"}, now + 3600, zone) == after);
  SlotsSeen back = slotsSeenFromJson(toJson(after));
  CHECK(back == after);
  CHECK(slotsSeenFromJson(json::Value()).empty());
}

TEST_CASE("a feed the rosters read as two teams joins their scheduled game at about its time") {
  Fixture f;
  SportsTeam coruna = team("s1", "Harbor Coruna Club");
  SportsTeam rovers = team("s2", "Riverside Rovers");
  SportsGame match = game("ESP", "Soccer", "7", coruna, rovers, utc(2026, 10, 25, 20, 0), {}, std::nullopt, true);
  BuildInput in = f.input();
  in.games.push_back(match);
  // The league's roster knows the club by another name too.
  SportsTeam known = coruna;
  known.alternates = {"Coruna Deportivo"};
  SportsTeam knownRovers = rovers;
  knownRovers.alternates = {"Riverside RFC"};
  in.rosters = {RosterLeague{"ESP", "Soccer", false, {known, knownRovers}}};
  in.events = {
      provider("x1", "CORUNA DEPORTIVO vs RIVERSIDE ROVERS", "", match.start - 600, {"xt|1"}, "dazn"),
      provider("x2", "Riverside Rovers vs Harbor Coruna Club", "Soccer", match.start, {"xt|2"}, "soccer"),
      // The same two a day later: a replay, not this game's source.
      provider("x3", "Coruna Deportivo vs Riverside Rovers", "", match.start + 30 * 3600, {"xt|3"}, "replays"),
      // ...written another way by another group: the same tile as x3.
      provider("x4", "Riverside RFC v Coruna Deportivo", "", match.start + 30 * 3600 + 1200, {"xt|4"},
               "cups"),
  };
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* tile = findGame(out->events, "ESP|7");
  REQUIRE(tile);
  std::vector<std::string> sources = ids(tile->sources);
  std::sort(sources.begin(), sources.end());
  CHECK(sources == std::vector<std::string>{"xt|1", "xt|2"});
  CHECK(out->stats.byTeams == 1);
  CHECK_FALSE(find(out->events, "x1"));
  const SportsEvent* replay = find(out->events, "x3");
  REQUIRE(replay);
  CHECK_FALSE(replay->game.has_value());
  CHECK(ids(replay->sources) == std::vector<std::string>{"xt|3", "xt|4"});
  CHECK_FALSE(find(out->events, "x4"));
}

// MARK: Build 145: games with no time set; "Surname-Surname" tennis slots

namespace {

// Saturday 14 November, 10 AM Eastern; ESPN lists Valley Tech at Harbor
// State with no time: 05:00Z, midnight Eastern of the day.
struct TbdDay {
  Seconds now = utc(2026, 11, 14, 15, 0);
  SportsGame tbd = [] {
    SportsGame g = game("NCAAF", "College Football", "401", team("10", "Harbor State Gulls", std::string("HSG")),
                        team("11", "Valley Tech Hawks", std::string("VTH")), utc(2026, 11, 14, 5, 0), {"ABC"});
    g.timeTbd = true;
    return g;
  }();
  BuildInput input(std::vector<SportsGame> games) const {
    BuildInput in;
    in.games = std::move(games);
    in.now = now;
    in.zone = time::Zone::usEastern();
    return in;
  }
};

}  // namespace

TEST_CASE("Build 145: a game with no time set takes a feed anywhere on its day; the tile keeps its listed time") {
  TbdDay d;
  BuildInput in = d.input({d.tbd});
  in.now = utc(2026, 11, 13, 22, 0);  // the evening before
  in.events = {
      // 7:30 PM Eastern that day: the game, 14.5 hours after the placeholder.
      provider("on", "Valley Tech Hawks vs Harbor State Gulls", "College Football", utc(2026, 11, 15, 0, 30), {"xt|1"}),
      // The next day: another day's slot, a replay.
      provider("next", "Valley Tech Hawks vs Harbor State Gulls", "College Football", utc(2026, 11, 16, 0, 30),
               {"xt|2"}),
      // The day before, at 6 PM: another day too.
      provider("before", "Valley Tech Hawks vs Harbor State Gulls", "College Football", utc(2026, 11, 13, 23, 0),
               {"xt|3"}),
  };
  auto out = build(in);
  REQUIRE(out);
  const SportsEvent* tile = findGame(out->events, "NCAAF|401");
  REQUIRE(tile);
  CHECK(ids(tile->sources) == std::vector<std::string>{"xt|1"});
  CHECK(tile->start == d.tbd.start);  // the time shown stays as listed
  REQUIRE(tile->game);
  CHECK(tile->game->isTimeTbd());
  CHECK(out->stats.replays == 2);
  CHECK(out->stats.hidden["xt|2"] == "a replay: its game is on another day");
  CHECK(out->stats.hidden["xt|3"] == "a replay: its game is on another day");

  // Without the mark the placeholder is a time: the 7:30 PM feed is a replay
  // (as it was before Build 145).
  SportsGame unmarked = d.tbd;
  unmarked.timeTbd.reset();
  in.games = {unmarked};
  out = build(in);
  REQUIRE(out);
  CHECK(findGame(out->events, "NCAAF|401")->sources.empty());
  CHECK(out->stats.hidden["xt|1"] == "a replay: its game is on another day");

  // Once ESPN sets the time (2 PM), the usual 3 hours around it.
  SportsGame set = d.tbd;
  set.timeTbd.reset();
  set.start = utc(2026, 11, 14, 19, 0);
  in.games = {set};
  out = build(in);
  REQUIRE(out);
  CHECK(findGame(out->events, "NCAAF|401")->sources.empty());  // 5.5 hours after it
  in.events[0].start = utc(2026, 11, 14, 20, 30);
  out = build(in);
  CHECK(ids(findGame(out->events, "NCAAF|401")->sources) == std::vector<std::string>{"xt|1"});
}

TEST_CASE("Build 145: a game with no time set: day-less slots that day, and no clock offset learned from it") {
  TbdDay d;
  // "3:30PM" with no day, read as today's: the game whose time isn't set,
  // though its placeholder is ten hours gone.
  BuildInput in = d.input({d.tbd});
  in.events = {provider("g", "Valley Tech Hawks at Harbor State Gulls 3:30PM", "College Football",
                        utc(2026, 11, 14, 20, 30), {"xt|1"}, "slots", false, true)};
  auto out = build(in);
  REQUIRE(out);
  CHECK(ids(findGame(out->events, "NCAAF|401")->sources) == std::vector<std::string>{"xt|1"});
  CHECK(out->stats.guessedMatched == 1);
  // Not the next day's: no game then.
  in.now = utc(2026, 11, 15, 15, 0);
  in.events[0].start = utc(2026, 11, 15, 20, 30);
  out = build(in);
  REQUIRE(out);
  CHECK(findGame(out->events, "NCAAF|401")->sources.empty());

  // Three games with no time and their feeds at noon Eastern: no "+7h"
  // clock for the group (the placeholders aren't times).
  SportsGame b = d.tbd, c = d.tbd;
  b.id = "NCAAF|402";
  b.home = team("12", "River Town Otters", std::string("RTO"));
  b.away = team("13", "Lake City Pikes", std::string("LCP"));
  c.id = "NCAAF|403";
  c.home = team("14", "Desert Foxes", std::string("DSF"));
  c.away = team("15", "Coast Crabs", std::string("CCR"));
  in = d.input({d.tbd, b, c});
  const Seconds noon = utc(2026, 11, 14, 17, 0);
  in.events = {
      provider("e1", "Valley Tech Hawks vs Harbor State Gulls", "College Football", noon, {"eu|1"}, "eu"),
      provider("e2", "Lake City Pikes vs River Town Otters", "College Football", noon, {"eu|2"}, "eu"),
      provider("e3", "Coast Crabs vs Desert Foxes", "College Football", noon, {"eu|3"}, "eu"),
  };
  out = build(in);
  REQUIRE(out);
  CHECK(out->stats.offsets.empty());
  CHECK(ids(findGame(out->events, "NCAAF|402")->sources) == std::vector<std::string>{"eu|2"});
}

TEST_CASE("Build 145: a tennis match with no time set, at home and abroad") {
  auto match = [](const std::string& id, const std::string& tournament, Seconds start, bool tbd) {
    SportsGame g = game("ATP", "Tennis", id, team(id + "a", "Ann Shelby"), team(id + "b", "Lu Van Haren"), start);
    g.tennis = true;
    g.tournament = tournament;
    if (tbd) g.timeTbd = true;
    return g;
  };
  // The semifinal on Saturday 7 November, time not set (05:00Z): a slot at
  // 3 PM Eastern that day is it.
  SportsGame sf = match("1", "Harbor Masters", utc(2026, 11, 7, 5, 0), true);
  BuildInput in;
  in.now = utc(2026, 11, 7, 12, 0);
  in.zone = time::Zone::usEastern();
  in.games = {sf};
  in.events = {provider("p", "Ann Shelby vs Lu Van Haren", "Tennis", utc(2026, 11, 7, 20, 0), {"xt|1"})};
  auto out = build(in);
  REQUIRE(out);
  CHECK(ids(findGame(out->events, "ATP|1")->sources) == std::vector<std::string>{"xt|1"});
  // Before Build 145 (no mark): 15 hours from the placeholder, a replay.
  in.games[0].timeTbd.reset();
  out = build(in);
  REQUIRE(out);
  CHECK(out->stats.hidden["xt|1"] == "a replay: its game is on another day");

  // Abroad: a final in the Far East on the 8th, local time (05:00Z); its
  // 11:00 start there is 10 PM the evening before in New York: within a
  // tennis match's window of that day.
  SportsGame final = match("2", "Harbor Open", utc(2026, 11, 8, 5, 0), true);
  in.games = {final};
  in.now = utc(2026, 11, 8, 1, 0);
  in.events = {provider("f", "Ann Shelby vs Lu Van Haren", "Tennis", utc(2026, 11, 8, 3, 0), {"xt|2"}),
               provider("g", "Ann Shelby vs Lu Van Haren", "Tennis", utc(2026, 11, 9, 13, 0), {"xt|3"})};
  out = build(in);
  REQUIRE(out);
  CHECK(ids(findGame(out->events, "ATP|2")->sources) == std::vector<std::string>{"xt|2"});
  CHECK(out->stats.hidden["xt|3"] == "a replay: its game is on another day");  // the day after

  // A tournament's feed on the final's day takes it too (the day's play).
  std::vector<SportsGame> games = {final};
  CHECK(tennisTargets("ATP Tennis: Harbor Open Final", "", utc(2026, 11, 8, 13, 0), in.now, {0}, games) ==
        std::vector<int>{0});
  CHECK(tennisTargets("ATP Tennis: Harbor Open", "", utc(2026, 11, 9, 13, 0), in.now, {0}, games).empty());
}

TEST_CASE("Build 145: \"Surname-Surname\" is the one match of those two; hyphenated names stay whole") {
  const Seconds at = utc(2026, 11, 6, 13, 0);
  auto match = [&](const std::string& id, const std::string& home, const std::string& away, Seconds start) {
    SportsGame g = game("ATP", "Tennis", id, team(id + "a", home), team(id + "b", away), start);
    g.tennis = true;
    g.tournament = "Harbor Masters";
    g.venue = "Harbor City, Somewhere";
    return g;
  };
  std::vector<SportsGame> games = {
      match("1", "Tom Pauling", "Ben Shelby", at + 600),
      match("2", "Ray Hijano", "Lu Van Haren", at + 8400),
      match("3", "Cole / Nakano", "Harris / Skov", at + 300),  // doubles, first on
      match("4", "Rae Auger-Allard", "Cas Rudd", at + 21'600),
      match("5", "Hal / Herbin", "Arno / Oliva", at + 13'500),
  };
  const std::vector<int> all = {0, 1, 2, 3, 4};
  const std::vector<int> day = {2, 0, 1, 4, 3};  // the day's matches by start
  CHECK(tennisTargets("ATP: Shelby-Pauling, kvartfinale, Harbor", "", at, at, all, games) == std::vector<int>{0});
  CHECK(tennisTargets("ATP: Pauling-Shelby, Harbor", "", at, at, all, games) == std::vector<int>{0});
  CHECK(tennisTargets("ATP: Hijano-Haren, Harbor", "", at, at, all, games) == std::vector<int>{1});
  // A hyphenated surname, three names joined, initials, two players of
  // different matches, a place: the day's matches, as before.
  CHECK(tennisTargets("ATP: Auger-Allard, kvartfinale, Harbor", "", at, at, all, games) == day);
  CHECK(tennisTargets("ATP: Auger-Allard-Rudd, Harbor", "", at, at, all, games) == day);
  CHECK(tennisTargets("ATP: Herbin H-P, Harbor", "", at, at, all, games) == day);
  CHECK(tennisTargets("ATP: Rudd-Pauling, Harbor", "", at, at, all, games) == day);
  CHECK(tennisTargets("Center Court-Harbor-ATP Quarterfinals", "", at, at, all, games, true) == day);
  // The doubles pairs aren't two surnames.
  CHECK(tennisTargets("ATP: Cole-Harris, Harbor", "", at, at, all, games) == day);

  // As a feed: only that match's tile gets it.
  BuildInput in;
  in.now = at;
  in.zone = time::Zone::usEastern();
  in.games = games;
  in.events = {provider("dk", "ATP: Shelby-Pauling, kvartfinale, Harbor", "Tennis", at, {"dk|1"})};
  auto out = build(in);
  REQUIRE(out);
  CHECK(ids(findGame(out->events, "ATP|1")->sources) == std::vector<std::string>{"dk|1"});
  for (const char* other : {"ATP|2", "ATP|3", "ATP|4", "ATP|5"}) {
    const SportsEvent* tile = findGame(out->events, other);
    CHECK((!tile || tile->sources.empty()));
  }
}
