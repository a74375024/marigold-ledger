// The game schedule: scoreboard parsing (lenient: missing fields, numbers
// as strings), notes, fight cards, race weekends, tennis, request shapes,
// the fetch state machine with its request budget, and the saved cache.
#include <set>

#include "core/sports/my_sports.hpp"
#include "core/sports/profiles.hpp"
#include "core/sports/schedule.hpp"
#include "core/util/json.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::schedule;
using sportsdata::utc;

namespace {

const League& nfl() { return *leagueByShort("NFL"); }
const League& epl() { return *leagueByShort("EPL"); }

// Two NFL games: one complete and lenient (numbers as numbers, "#" colors,
// a logos array), one postponed; plus odd events that must be skipped.
const char* kScoreboard = R"JSON({
  "season": {"type": 2, "year": 2026},
  "week": {"number": "8"},
  "events": [
    {"id": 401001, "date": "2026-10-25T17:00Z", "week": {"number": 8},
     "season": {"slug": "regular-season"},
     "competitions": [{
       "competitors": [
         {"homeAway": "home", "curatedRank": {"current": 99},
          "team": {"id": "1", "displayName": "Harbor City Gulls", "shortDisplayName": "Gulls", "name": "Gulls",
                   "abbreviation": "HCG", "location": "Harbor City", "color": "#00274c", "alternateColor": "ffcb05",
                   "logos": [{"href": "https://example.invalid/hcg.png"}], "conferenceId": "5"}},
         {"homeAway": "away", "curatedRank": {"current": "5"},
          "team": {"id": 2, "displayName": "Team A", "abbreviation": "TMA", "logo": "https://example.invalid/a.png",
                   "color": "not-a-color", "conferenceId": 8}}
       ],
       "venue": {"fullName": "Gull Field", "address": {"city": "Harbor City"}},
       "broadcasts": [{"market": "away", "names": ["KTVA"]}, {"market": "national", "names": ["FOX", "fox"]}],
       "geoBroadcasts": [{"media": {"shortName": "NFL+"}, "market": {"type": "National"}}],
       "notes": [{"headline": "Team A leads series 3-1"}, {"headline": "NFL Harbor Game"}],
       "status": {"type": {"name": "STATUS_SCHEDULED", "completed": false}}
     }]},
    {"id": "401002", "date": "2026-10-26T00:20:00Z",
     "competitions": [{
       "competitors": [
         {"homeAway": "home", "team": {"id": "3", "displayName": "River Town Otters", "abbreviation": "RTO"}},
         {"homeAway": "away", "team": {"id": "4", "name": "Valley Hawks", "abbreviation": "null"}}
       ],
       "venue": {"fullName": "Otter Dome, River Town", "address": {"city": "River Town"}},
       "status": {"type": {"name": "STATUS_POSTPONED"}}
     }]},
    {"id": "401003", "date": "2026-10-25T17:00Z", "competitions": []},
    {"id": "401004", "date": "not a date", "competitions": [{"competitors": [{}, {}]}]},
    {"id": "401005", "date": "2026-10-25T17:00Z",
     "competitions": [{"competitors": [{"homeAway": "home", "team": {"id": "5", "displayName": "Lone Team"}}]}]},
    "not an object"
  ]
})JSON";

}  // namespace

TEST_CASE("scoreboard games: teams, colors, ranks, venue, week and networks") {
  auto doc = parseScoreboard(kScoreboard, nfl());
  REQUIRE(doc);
  CHECK(doc->week == 8);
  CHECK(doc->seasonType == 2);
  REQUIRE(doc->games.size() == 2);
  const SportsGame& g = doc->games[0];
  CHECK(g.id == "NFL|401001");
  CHECK(g.league == "NFL");
  CHECK(g.leagueShort == "NFL");
  CHECK(g.start == utc(2026, 10, 25, 17, 0));
  CHECK(g.home.name == "Harbor City Gulls");
  CHECK(g.home.shortName == "HCG");
  CHECK(g.home.alternates == std::vector<std::string>{"Gulls"});
  CHECK(g.home.badge == "https://example.invalid/hcg.png");
  // Build 127: a college team's conference (its group id, as text).
  CHECK(g.home.conference == std::string("5"));
  CHECK(g.away.conference == std::string("8"));
  CHECK(doc->games[1].home.conference == std::nullopt);
  CHECK(teamFromJson(toJson(g.away)) == std::optional<SportsTeam>(g.away));
  CHECK(g.home.color == "00274C");
  CHECK(g.home.altColor == "FFCB05");
  CHECK_FALSE(g.home.rank);  // 99 = unranked
  CHECK(g.away.id == "2");   // a number read as text
  CHECK(g.away.rank == 5);   // "5" read as a number
  CHECK_FALSE(g.away.color);
  CHECK(g.away.badge == "https://example.invalid/a.png");
  CHECK(g.venue == "Gull Field, Harbor City");
  CHECK(g.weekLabel == "Week 8");
  // National first, repeats dropped (case-insensitive).
  CHECK(g.networks == std::vector<std::string>{"FOX", "NFL+", "KTVA"});
  // The result note is skipped; "NFL Harbor Game" -> "Harbor".
  CHECK(g.note == "Harbor");
  CHECK_FALSE(g.postponed);
  CHECK(g.title() == "Team A at Harbor City Gulls");

  const SportsGame& p = doc->games[1];
  CHECK(p.postponed);
  CHECK(p.away.name == "Valley Hawks");      // "name" when there's no displayName
  CHECK_FALSE(p.away.shortName);             // "null" is no value
  CHECK(p.venue == "Otter Dome, River Town");  // the city already in the name
  CHECK_FALSE(p.weekLabel);
  // A Sunday 8:20 PM Eastern NFL game is "Sunday Night".
  CHECK(p.note == "Sunday Night");
}

TEST_CASE("bodies without an events list are unreadable") {
  CHECK_FALSE(parseScoreboard("{\"leagues\": []}", nfl()));
  CHECK_FALSE(parseScoreboard("not json", nfl()));
  CHECK_FALSE(parseScoreboard("", nfl()));
  auto empty = parseScoreboard("{\"events\": []}", nfl());
  REQUIRE(empty);
  CHECK(empty->games.empty());
}

TEST_CASE("dates: minutes without seconds, or full ISO 8601") {
  League l = epl();
  auto doc = parseScoreboard(
      R"({"events":[{"id":"9","date":"2026-10-24T14:00:00+01:00","competitions":[{"competitors":[
        {"homeAway":"home","team":{"id":"1","displayName":"Harbor City FC"}},
        {"homeAway":"away","team":{"id":"2","displayName":"Team A"}}]}]}]})",
      l);
  REQUIRE(doc);
  REQUIRE(doc->games.size() == 1);
  CHECK(doc->games[0].start == utc(2026, 10, 24, 13, 0));
  CHECK(doc->games[0].homeFirst);
  CHECK(doc->games[0].title() == "Harbor City FC vs Team A");
}

TEST_CASE("notes: never a result, league prefix and Game dropped, night games, stages") {
  League l = nfl();
  Seconds afternoon = utc(2026, 10, 25, 17, 0);
  CHECK(note({"World Series - Game 1"}, std::nullopt, l, afternoon) == "World Series \xC2\xB7 Game 1");
  CHECK(note({"Team A wins 24-17"}, std::nullopt, l, afternoon) == std::nullopt);
  CHECK(note({"Series tied 2 - 2"}, std::nullopt, l, afternoon) == std::nullopt);
  CHECK(note({"A headline far too long to fit on a small tile tag"}, std::nullopt, l, afternoon) == std::nullopt);
  // Monday 8:15 PM Eastern (00:15 UTC Tuesday).
  CHECK(note({}, std::nullopt, l, utc(2026, 10, 27, 0, 15)) == "Monday Night");
  CHECK(note({}, std::nullopt, l, utc(2026, 10, 23, 0, 15)) == "Thursday Night");
  CHECK(note({}, std::nullopt, l, afternoon) == std::nullopt);
  CHECK(note({}, std::string("league-phase"), epl(), afternoon) == "League Phase");
  CHECK(note({}, std::string("regular-season"), epl(), afternoon) == std::nullopt);
  // Not an NFL game: no night tag.
  CHECK(note({}, std::nullopt, epl(), utc(2026, 10, 27, 0, 15)) == std::nullopt);
}

TEST_CASE("abbreviations: the schedule's, else initials") {
  SportsTeam t = sportsdata::team("1", "Harbor City Gulls", std::string("hcg"));
  CHECK(t.abbreviation() == "HCG");
  t.shortName = std::string("TOOLONG");
  CHECK(t.abbreviation() == "HCG");
  t.shortName.reset();
  t.name = "Team";
  CHECK(t.abbreviation() == "TEA");
  t.name = "Team A 2";
  CHECK(t.abbreviation() == "TA");
  // Hyphens split words, apostrophes don't; a run of capitals stays whole.
  t.name = "Harbor-Valley";
  CHECK(t.abbreviation() == "HV");
  t.name = "D'Harbor";
  CHECK(t.abbreviation() == "DHA");
  t.name = "UHarbor Valley";
  CHECK(t.abbreviation() == "UHV");
  t.name = "UHarbor Coast";
  CHECK(t.abbreviation() == "UHC");
  // Never three letters that spell a rude word.
  t.name = "Assumption";
  CHECK(t.abbreviation() == "ASN");
}

TEST_CASE("fight cards: one game per part, by segment or by time") {
  League ufc = *leagueByShort("UFC");
  const char* body = R"JSON({"events":[
    {"id":"600001","name":"UFC 333: Stone vs. River","date":"2026-10-24T22:00Z","competitions":[
      {"startDate":"2026-10-24T22:00Z","cardSegment":{"description":"Early Prelims"},
       "competitors":[{"athlete":{"id":"11","displayName":"Ann One"}},{"athlete":{"id":"12","displayName":"Bea Two"}}],
       "status":{"type":{"completed":true}}},
      {"startDate":"2026-10-25T00:00Z","cardSegment":{"description":"Prelims"},
       "broadcasts":[{"market":"national","names":["Network P"]}],
       "competitors":[{"athlete":{"id":"13","displayName":"Cy Three"}},{"athlete":{"id":"14","displayName":"Di Four"}}]},
      {"startDate":"2026-10-25T02:00Z","cardSegment":"Main Card",
       "broadcasts":[{"market":"national","names":["Network M"]}],
       "venue":{"fullName":"Fight Hall","address":{"city":"Harbor City"}},
       "competitors":[{"athlete":{"id":"15","displayName":"Max Stone","flag":{"href":"https://example.invalid/f/500/aus.png"}}},
                      {"athlete":{"id":"16","displayName":"Lee River"}}]},
      {"startDate":"2026-10-25T02:00Z","cardSegment":"Main Card",
       "competitors":[{"athlete":{"id":"17","displayName":"Eve Five"}},{"athlete":{"id":"18","displayName":"Flo Six"}}]}
    ]},
    {"id":"600002","name":"UFC Fight Night: Tall vs. Short","date":"2026-10-31T23:00Z","competitions":[
      {"startDate":"2026-10-31T23:00Z","competitors":[{"athlete":{"displayName":"G One"}},{"athlete":{"displayName":"H Two"}}]},
      {"startDate":"2026-11-01T02:00Z","competitors":[{"athlete":{"displayName":"Jo Tall"}},{"athlete":{"displayName":"Kim Short"}}]}
    ]}
  ]})JSON";
  auto cards = parseFightCards(body, ufc);
  REQUIRE(cards);
  REQUIRE(cards->games.size() == 5);
  const SportsGame& early = cards->games[0];
  CHECK(early.id == "UFC|600001|early");
  CHECK(early.customTitle == "UFC 333 Early Prelims");
  CHECK(early.cardKey == "ufc|333|early");
  CHECK(early.completed);
  CHECK(early.homeFirst);
  const SportsGame& main = cards->games[2];
  CHECK(main.id == "UFC|600001|main");
  CHECK(main.customTitle == "UFC 333: Stone vs. River");
  // The headline bout is the one the card is named after.
  CHECK(main.home.name == "Max Stone");
  CHECK(main.home.id == "f15");
  CHECK(main.home.country == "AUS");
  CHECK(main.home.badge == "https://example.invalid/f/500/aus.png");
  CHECK(main.away.name == "Lee River");
  CHECK(main.networks == std::vector<std::string>{"Network M"});
  CHECK(main.venue == "Fight Hall, Harbor City");
  CHECK(main.start == utc(2026, 10, 25, 2, 0));
  // No segments: the later time is the main card, the earlier the prelims.
  CHECK(cards->games[3].id == "UFC|600002|prelims");
  CHECK(cards->games[3].cardKey == "ufc||prelims");
  CHECK(cards->games[4].id == "UFC|600002|main");
  CHECK(cards->summary.find("2 cards, 5 parts") == 0);
}

TEST_CASE("race weekends: one game per session, names tidied") {
  CHECK(raceName("Sponsor United States Grand Prix 2026") == "United States GP");
  CHECK(raceName("Formula 1 Australian Grand Prix 2026") == "Australian GP");
  CHECK(raceName("Formula 1 Gran Premio de Monaco") == "Monaco GP");
  CHECK(raceName("Harbor 500") == "Harbor 500");
  League f1 = *leagueByShort("F1");
  const char* body = R"JSON({"events":[{"id":"700","name":"Harbor Grand Prix",
    "circuit":{"fullName":"Harbor Circuit","address":{"city":"Harbor City","country":"usa"}},
    "competitions":[
      {"id":"1","date":"2026-10-23T18:00Z","type":{"abbreviation":"FP1"},"status":{"type":{"state":"post"}}},
      {"id":"2","date":"2026-10-24T21:00Z","type":{"abbreviation":"Qual"}},
      {"id":"3","date":"2026-10-25T19:00Z","type":{"abbreviation":"Race"},"broadcasts":[{"market":"national","names":["Network R"]}]}
    ]}]})JSON";
  auto weekends = parseRaceWeekends(body, f1);
  REQUIRE(weekends);
  REQUIRE(weekends->size() == 3);
  CHECK((*weekends)[0].id == "F1|700|1");
  CHECK((*weekends)[0].cardKey == "f1|700|fp1");
  CHECK((*weekends)[0].customTitle == "Harbor GP \xC2\xB7 Practice 1");
  CHECK((*weekends)[0].completed);
  CHECK((*weekends)[0].isRacing());
  CHECK((*weekends)[0].home.country == "USA");
  CHECK((*weekends)[0].venue == "Harbor Circuit, Harbor City");
  CHECK((*weekends)[1].cardKey == "f1|700|qual");
  CHECK((*weekends)[2].networks == std::vector<std::string>{"Network R"});
  CHECK(weekendKey((*weekends)[2]) == "F1|700");
}

TEST_CASE("tennis: matches between players and pairs, round and place tag") {
  CHECK(tennisRound(std::string("Quarterfinal")) == "QF");
  CHECK(tennisRound(std::string("Qualifying 1st Round")) == "Q1");
  CHECK(tennisRound(std::string("2nd Round")) == "R2");
  CHECK(tennisRound(std::string("Round of 16")) == "R16");
  CHECK(tennisRound(std::string("Final")) == "F");
  CHECK(tennisRound(std::nullopt) == std::nullopt);
  CHECK(tennisEventName("Toray Harbor Open Tennis", std::nullopt) == "Harbor Open");
  CHECK(tennisEventName("Harbor Open presentado por Banco", std::nullopt) == "Harbor Open");
  CHECK(tennisEventName("Harbor Bay Ladies Open", std::nullopt) == "Harbor Bay Open");
  CHECK(tennisEventName("BBVA Open Internacional De Harbor", std::string("Harbor")) == "Harbor Open");
  CHECK(tennisEventName("Swiss Indoors Harbor", std::nullopt) == "Swiss Indoors Harbor");
  League atp = *leagueByShort("ATP");
  const char* body = R"JSON({"events":[{"id":"80","shortName":"Harbor Open","groupings":[{"competitions":[
    {"id":"1","date":"2026-10-24T12:00Z","round":{"displayName":"Semifinal"},
     "venue":{"court":"Center Court","address":{"city":"Harbor City, USA"}},
     "competitors":[
       {"homeAway":"home","seed":"3","athlete":{"id":"9","displayName":"Ann Player","flag":{"href":"https://example.invalid/c/ita.png"}}},
       {"homeAway":"away","athlete":{"id":"10","displayName":"Bea Racket"}}]},
    {"id":"2","date":"2026-10-24T14:00Z",
     "competitors":[
       {"roster":{"athletes":[{"lastName":"One"},{"displayName":"Cy Twoson"}]}},
       {"roster":{"athletes":[{"lastName":"Three","flag":{"href":"https://example.invalid/c/fra.png"}},
                              {"lastName":"Four","flag":{"href":"https://example.invalid/c/ger.png"}}]}}]},
    {"id":"3","date":"2026-10-24T16:00Z",
     "competitors":[{"athlete":{"displayName":"TBD"}},{"athlete":{"displayName":"Bea Racket"}}]}
  ]}]}]})JSON";
  auto matches = parseTennisMatches(body, atp);
  REQUIRE(matches);
  REQUIRE(matches->size() == 2);
  const SportsGame& sf = (*matches)[0];
  CHECK(sf.id == "ATP|80|1");
  // The draw's first-listed player (home) first, as broadcasts list them.
  CHECK(sf.customTitle == "Player vs Racket");
  CHECK(sf.note == "Harbor Open \xC2\xB7 SF");
  CHECK(sf.venue == "Center Court");
  CHECK(sf.home.rank == 3);
  CHECK(sf.home.country == "ITA");
  CHECK(sf.home.flags == std::vector<std::string>{"https://example.invalid/c/ita.png"});
  CHECK(sf.away.flags.empty());
  CHECK(sf.home.alternates == std::vector<std::string>{"Player Ann"});
  CHECK(sf.networks.empty());
  CHECK(sf.isTennis());
  const SportsGame& doubles = (*matches)[1];
  CHECK(doubles.home.name == "One / Twoson");
  CHECK(doubles.away.name == "Three / Four");
  CHECK(doubles.home.flags.empty());  // one partner without a flag: none
  CHECK(doubles.away.flags == std::vector<std::string>{"https://example.invalid/c/fra.png", "https://example.invalid/c/ger.png"});
  CHECK_FALSE(doubles.away.country);  // partners from two countries: no single chip
  CHECK(doubles.note == "Harbor Open");
}

TEST_CASE("Build 145: a game or match ESPN has no time for (timeValid false) is marked, its date kept") {
  // Texas at LSU as ESPN lists it on 6 November: 05:00Z, midnight Eastern
  // of the 14th, time to be set.
  const char* body = R"JSON({"events":[
    {"id":"401856885","date":"2026-11-14T05:00Z","competitions":[{"timeValid":false,"dateValid":true,
     "competitors":[{"homeAway":"home","team":{"id":"99","displayName":"LSU Tigers"}},
                    {"homeAway":"away","team":{"id":"251","displayName":"Texas Longhorns"}}],
     "status":{"type":{"name":"STATUS_SCHEDULED"}}}]},
    {"id":"401856886","date":"2026-11-14T17:00Z","competitions":[{"timeValid":true,
     "competitors":[{"homeAway":"home","team":{"id":"1","displayName":"Home U"}},
                    {"homeAway":"away","team":{"id":"2","displayName":"Away U"}}]}]},
    {"id":"401856887","date":"2026-11-14T20:00Z","competitions":[{
     "competitors":[{"homeAway":"home","team":{"id":"3","displayName":"Third U"}},
                    {"homeAway":"away","team":{"id":"4","displayName":"Fourth U"}}]}]},
    {"id":"401856888","date":"2026-11-14T20:00Z","competitions":[{"timeValid":"false",
     "competitors":[{"homeAway":"home","team":{"id":"5","displayName":"Fifth U"}},
                    {"homeAway":"away","team":{"id":"6","displayName":"Sixth U"}}]}]}]})JSON";
  auto doc = parseScoreboard(body, *leagueByShort("NCAAF"));
  REQUIRE(doc);
  REQUIRE(doc->games.size() == 4);
  const SportsGame& tbd = doc->games[0];
  CHECK(tbd.isTimeTbd());
  CHECK(tbd.start == utc(2026, 11, 14, 5, 0));  // the listed date, as is
  CHECK_FALSE(tbd.isDayOnly());
  CHECK_FALSE(doc->games[1].isTimeTbd());  // a time set
  CHECK_FALSE(doc->games[1].timeTbd);
  CHECK_FALSE(doc->games[2].isTimeTbd());  // no "timeValid" at all
  CHECK_FALSE(doc->games[3].isTimeTbd());  // not a boolean: not read as one
  // Saved and read back.
  auto back = gameFromJson(toJson(tbd));
  REQUIRE(back);
  CHECK(back->isTimeTbd());
  CHECK(*back == tbd);
  SportsGame set = tbd;
  set.timeTbd.reset();
  CHECK(set != tbd);
  CHECK_FALSE(gameFromJson(toJson(set))->timeTbd);

  // The Paris semifinal before its time was set, and one with it.
  const char* tennis = R"JSON({"events":[{"id":"13-2026","shortName":"Paris Masters","groupings":[{"competitions":[
    {"id":"186504","date":"2026-11-07T05:00Z","timeValid":false,"round":{"displayName":"Semifinal"},
     "competitors":[{"homeAway":"home","athlete":{"id":"1","displayName":"Ben Shelton"}},
                    {"homeAway":"away","athlete":{"id":"2","displayName":"Luca Van Assche"}}]},
    {"id":"186505","date":"2026-11-07T16:00Z","timeValid":true,"round":{"displayName":"Semifinal"},
     "competitors":[{"homeAway":"home","athlete":{"id":"3","displayName":"Carlos Alcaraz"}},
                    {"homeAway":"away","athlete":{"id":"4","displayName":"Casper Ruud"}}]}]}]}]})JSON";
  auto matches = parseTennisMatches(tennis, *leagueByShort("ATP"));
  REQUIRE(matches);
  REQUIRE(matches->size() == 2);
  CHECK((*matches)[0].isTimeTbd());
  CHECK((*matches)[0].start == utc(2026, 11, 7, 5, 0));
  CHECK((*matches)[0].customTitle == "Shelton vs Assche");  // the tile as before
  CHECK_FALSE((*matches)[1].isTimeTbd());
}

TEST_CASE("keeping: games after the cutoff, race weekends together") {
  SportsGame early = sportsdata::game("NFL", "NFL", "1", sportsdata::team("1", "A"), sportsdata::team("2", "B"),
                                      utc(2026, 10, 20, 0, 0));
  SportsGame late = sportsdata::game("NFL", "NFL", "2", sportsdata::team("1", "A"), sportsdata::team("2", "B"),
                                     utc(2026, 10, 25, 0, 0));
  SportsGame fp1 = sportsdata::game("F1", "Motorsport", "7|1", sportsdata::team("r", "GP"), sportsdata::team("r", "GP"),
                                    utc(2026, 10, 20, 0, 0));
  fp1.racing = true;
  SportsGame race = fp1;
  race.id = "F1|7|3";
  race.start = utc(2026, 10, 25, 0, 0);
  auto kept = keeping({early, late, fp1, race}, utc(2026, 10, 22, 0, 0));
  REQUIRE(kept.size() == 3);
  CHECK(kept[0].id == "NFL|2");
  CHECK(kept[1].id == "F1|7|1");  // its weekend's race is still ahead
}

TEST_CASE("requests: the public scoreboard, the limit form and the extra query") {
  sources::Request r = scoreboardRequest(nfl(), "dates=202610", false);
  CHECK(r.url == "https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard?dates=202610&limit=500");
  CHECK(r.timeoutSeconds == 20);
  CHECK(r.method == "GET");
  CHECK(r.headers.empty());
  League cfb = *leagueByShort("NCAAF");
  CHECK(scoreboardRequest(cfb, "", true).url ==
        "https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard?groups=80");
  CHECK(scoreboardRequest(cfb, "week=9&seasontype=2", true).url ==
        "https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard?week=9&seasontype=2&groups=80");
  CHECK(schoolsRequest().url ==
        "https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/teams?limit=1000");
  CHECK(teamsRequest(*leagueByShort("MLB")).url ==
        "https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/teams?limit=1000");
  // A league's teams come back like the schools.
  auto teams = parseTeams(R"({"sports":[{"leagues":[{"teams":[)"
                          R"({"team":{"id":"10","displayName":"Testville Owls","abbreviation":"TVO","name":"Owls","location":"Testville"}},)"
                          R"({"team":{"id":"11","displayName":"Sample City Hawks","abbreviation":"SCH","name":"Hawks","location":"Sample City"}}]}]}]})");
  REQUIRE(teams);
  CHECK(teams->size() == 2);
}

TEST_CASE("queries: a month or two, day by day, or this week") {
  time::Zone et = time::Zone::usEastern();
  // 2026-10-26: two days back is October, eight ahead November.
  Seconds now = utc(2026, 10, 26, 16, 0);
  CHECK(queriesFor(nfl(), now, et) == std::vector<std::string>{"dates=202610", "dates=202611"});
  // Rugby: the 31st on its own too (its October listing leaves it out).
  CHECK(queriesFor(*leagueByShort("T14"), now, et) ==
        std::vector<std::string>{"dates=202610", "dates=202611", "dates=20261031"});
  CHECK(queriesFor(nfl(), utc(2026, 10, 10, 16, 0), et) == std::vector<std::string>{"dates=202610"});
  CHECK(queriesFor(*leagueByShort("UFC"), utc(2026, 10, 10, 16, 0), et) ==
        std::vector<std::string>{"dates=202610", ""});
  // Day by day in the viewer's zone: 02:00 UTC is still the 25th in New York.
  // Build 125: five days (Thursday's and Friday's games from Monday).
  CHECK(queriesFor(*leagueByShort("NCAAM"), utc(2026, 10, 26, 2, 0), et) ==
        std::vector<std::string>{"dates=20261025", "dates=20261026", "dates=20261027", "dates=20261028",
                                 "dates=20261029"});
  CHECK(queriesFor(*leagueByShort("NCAAF"), now, et) == std::vector<std::string>{""});
}

TEST_CASE("active leagues: the defaults until My Sports is set up, then picks only") {
  MySportsPrefs prefs;
  SportFeedsSeen seen;
  Seconds now = sportsdata::saturday();
  std::set<std::string> shorts;
  for (const League& l : activeLeagues(prefs, seen, now, {})) shorts.insert(l.shortName);
  CHECK(shorts.count("NFL"));
  CHECK(shorts.count("EPL"));
  CHECK(shorts.count("UCL"));
  // Build 127: only the default picks; the European leagues, fight cards
  // and racing aren't among them (racing while its feeds are seen).
  CHECK_FALSE(shorts.count("LIGA"));
  CHECK_FALSE(shorts.count("UEL"));
  CHECK_FALSE(shorts.count("UFC"));
  CHECK_FALSE(shorts.count("F1"));
  // Defaults that aren't standard leagues only while your channels show
  // their games, so the defaults never ask for more than before.
  CHECK_FALSE(shorts.count("WNBA"));
  CHECK_FALSE(shorts.count("NCAAWV"));
  CHECK_FALSE(shorts.count("ATP"));
  seen.groups["WNBA"] = now;
  seen.racing = now;
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now, {})) shorts.insert(l.shortName);
  CHECK(shorts.count("WNBA"));
  CHECK(shorts.count("F1"));
  seen = SportFeedsSeen();
  prefs.configured = true;
  prefs.leagues = {"NBA", "S:Fighting"};
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now, {})) shorts.insert(l.shortName);
  CHECK(shorts == std::set<std::string>{"NBA", "UFC", "PFL"});
  // NASCAR feeds seen in the last two weeks: its schedule too.
  seen.nascar = now - 86'400;
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now, {})) shorts.insert(l.shortName);
  CHECK(shorts == std::set<std::string>{"NBA", "UFC", "PFL", "NASCAR"});
  seen.nascar = now - 15 * 86'400;
  CHECK(activeLeagues(prefs, seen, now, {}).size() == 3);
  // A sport picked as a whole: its leagues. IndyCar only with Motorsport.
  prefs.leagues = {"S:Rugby", "S:Volleyball", "S:Motorsport"};
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now, {})) shorts.insert(l.shortName);
  CHECK(shorts.count("6N"));
  CHECK(shorts.count("URC"));
  CHECK(shorts.count("NCAAWV"));
  CHECK(shorts.count("NCAAMV"));
  CHECK(shorts.count("IRL"));
  CHECK_FALSE(shorts.count("PLL"));
  prefs.leagues = {"NBA"};
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now, {})) shorts.insert(l.shortName);
  CHECK_FALSE(shorts.count("IRL"));
  CHECK(leagueByShort("NCAASB"));
  CHECK(leagueByShort("NCAASB")->path == "baseball/college-softball");
  CHECK(sportLeagues("S:Lacrosse").size() == 3);
  CHECK(sportLeagues("S:Nothing").empty());
  // Build 127: the all-soccer listing comes with the national teams or
  // every other competition (the defaults have USA), not with a league;
  // a league no longer brings the European competitions.
  prefs.configured = false;
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now, {})) shorts.insert(l.shortName);
  CHECK(shorts.count(kSoccerAll));
  prefs.configured = true;
  prefs.leagues = {"EPL"};
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now, {})) shorts.insert(l.shortName);
  CHECK(shorts == std::set<std::string>{"EPL"});
  for (const char* pick : {kUSAPick, kMexicoPick, kSoccerAllPick}) {
    prefs.leagues = {"EPL", pick};
    shorts.clear();
    for (const League& l : activeLeagues(prefs, seen, now, {})) shorts.insert(l.shortName);
    CHECK(shorts == std::set<std::string>{"EPL", kSoccerAll});
  }
  prefs.leagues = {"NBA"};
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now, {})) shorts.insert(l.shortName);
  CHECK_FALSE(shorts.count(kSoccerAll));
  // Hidden ids are never looked up, picked or seen.
  prefs.leagues = {"NBA", "EPL", "S:Golf"};
  prefs.hidden = {"EPL", "S:Golf"};
  seen.golf = now;
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now, {})) shorts.insert(l.shortName);
  CHECK(shorts == std::set<std::string>{"NBA"});
}

TEST_CASE("active leagues unconfigured: the defaults for the lineup's countries") {
  MySportsPrefs prefs;
  SportFeedsSeen seen;
  Seconds now = sportsdata::saturday();
  auto shortsFor = [&](const std::set<std::string>& countries) {
    std::set<std::string> shorts;
    for (const League& l : activeLeagues(prefs, seen, now, countries)) shorts.insert(l.shortName);
    return shorts;
  };
  std::set<std::string> english = shortsFor({"US"});
  CHECK(english == std::set<std::string>{"NFL", "NCAAF", "NBA", "MLB", "NHL", "MLS", "EPL", "UCL", kSoccerAll});
  // Liga MX and Mexico come with the all-soccer listing; cricket while its
  // feeds are seen.
  CHECK(shortsFor({"US", "US-ES"}) == english);
  seen.cricket = now;
  CHECK(shortsFor({"US", "IN"}).count("CRIC"));
  // The same leagues a set-up viewer with the defaults gets, minus those
  // your channels haven't shown.
  MySportsPrefs set = defaultPicks({"US"});
  set.configured = true;
  std::set<std::string> picked;
  for (const League& l : activeLeagues(set, seen, now, {})) picked.insert(l.shortName);
  for (const std::string& s : english) CHECK(picked.count(s));
  CHECK(picked.count("WNBA"));
  CHECK(picked.count("NCAAM"));
  CHECK(picked.count("NWSL"));
  CHECK(picked.count("NCAAWV"));
}

TEST_CASE("sport feeds seen: F1, NASCAR and tennis, at most every 6 hours") {
  SportFeedsSeen seen;
  std::optional<Seconds> checked;
  Seconds now = sportsdata::saturday();
  std::vector<catalog::LiveEvent> events = {
      sportsdata::liveEvent("1", "F1: Harbor Grand Prix", "Motorsport", now, {"f1"}),
      sportsdata::liveEvent("2", "Ann Player vs Bea Racket", "Tennis", now, {"t1"}),
  };
  CHECK(noteSportFeeds(seen, checked, events, now));
  CHECK(seen.racing == now);
  CHECK(seen.tennis == now);
  CHECK_FALSE(seen.nascar);
  // Within 6 hours nothing is looked at again.
  events.push_back(sportsdata::liveEvent("3", "NASCAR Cup: Harbor 500", "Motorsport", now, {"n1"}));
  CHECK_FALSE(noteSportFeeds(seen, checked, events, now + 3600));
  CHECK_FALSE(seen.nascar);
  CHECK(noteSportFeeds(seen, checked, events, now + 7 * 3600));
  CHECK(seen.nascar == now + 7 * 3600);
  // Seen again, nothing new.
  CHECK_FALSE(noteSportFeeds(seen, checked, events, now + 14 * 3600));
  // "Golf" isn't F1, but golf feeds turn the golf tours on.
  SportFeedsSeen other;
  std::optional<Seconds> otherChecked;
  CHECK(noteSportFeeds(other, otherChecked,
                       {sportsdata::liveEvent("4", "Golf: Final Round", "Golf", now, {"g"})}, now));
  CHECK_FALSE(other.racingSeen(now));
  CHECK(other.golfSeen(now));
  MySportsPrefs open;
  std::set<std::string> tours;
  for (const League& l : activeLeagues(open, other, now, {})) tours.insert(l.shortName);
  CHECK(tours.count("PGA"));
  CHECK(tours.count("LPGA"));
}

namespace {

std::string oneGame(const std::string& id, const std::string& date) {
  return R"({"week":{"number":9},"season":{"type":2},"events":[{"id":")" + id + R"(","date":")" + date +
         R"(","competitions":[{"competitors":[
          {"homeAway":"home","team":{"id":"1","displayName":"Harbor City Gulls","abbreviation":"HCG"}},
          {"homeAway":"away","team":{"id":"2","displayName":"Team A","abbreviation":"TMA"}}]}]}]})";
}

Context nflOnly(Seconds now) {
  Context ctx;
  ctx.prefs.configured = true;
  ctx.prefs.leagues = {"NFL"};
  ctx.now = now;
  ctx.zone = time::Zone::usEastern();
  return ctx;
}

}  // namespace

TEST_CASE("a fetch asks only picked leagues, learns the request form, and saves") {
  Seconds now = utc(2026, 10, 26, 16, 0);
  Schedule schedule;
  Context ctx = nflOnly(now);
  REQUIRE(schedule.refreshIfNeeded(ctx));
  CHECK(schedule.fetching());
  // Busy: a second refresh doesn't start another.
  CHECK_FALSE(schedule.refreshIfNeeded(ctx));
  auto first = schedule.nextRequest();
  REQUIRE(first);
  CHECK(first->league.shortName == "NFL");
  CHECK(first->waitMs == 0);
  CHECK(first->request.url.find("dates=202610&limit=500") != std::string::npos);
  // One at a time: nothing more until the answer.
  CHECK_FALSE(schedule.nextRequest());
  schedule.onResponse(200, oneGame("1", "2026-10-27T00:15Z"));
  auto second = schedule.nextRequest();
  REQUIRE(second);
  CHECK(second->waitMs == kRequestSpacingMs);
  CHECK(second->request.url.find("dates=202611") != std::string::npos);
  schedule.onResponse(200, oneGame("2", "2026-11-02T18:00Z"));
  CHECK_FALSE(schedule.nextRequest());
  auto done = schedule.takeFinished();
  REQUIRE(done);
  CHECK(done->succeeded);
  CHECK_FALSE(done->recheck);
  CHECK(schedule.takeDirty());
  const Saved& saved = schedule.saved();
  CHECK(saved.fetchedAt == now);
  CHECK(saved.schema == kSchema);
  CHECK(saved.leagueForm.at("NFL") == "limit");
  CHECK(saved.requestCount == 2);
  CHECK(saved.requestDay == "2026-10-26");
  CHECK(saved.leagueSet == std::vector<std::string>{"NFL"});
  REQUIRE(saved.games.size() == 2);
  CHECK(saved.rosters.at("NFL").size() == 2);
  CHECK(saved.rosters.at("NFL")[0].name == "Harbor City Gulls");  // A-Z
  // Already fetched today: Live Events opening again asks nothing.
  CHECK_FALSE(schedule.refreshIfNeeded(nflOnly(now + 3600)));
  // A newly picked league is looked up on its own.
  Context more = nflOnly(now + 3600);
  more.prefs.leagues = {"NFL", "NHL"};
  REQUIRE(schedule.leaguesChanged(more));
  int nhlRequests = 0;
  while (auto nhl = schedule.nextRequest()) {
    CHECK(nhl->league.shortName == "NHL");
    nhlRequests += 1;
    schedule.onResponse(200, "{\"events\":[]}");
  }
  CHECK(nhlRequests == 2);
  CHECK(schedule.takeFinished()->succeeded);
  // "only" fetches don't move fetchedAt.
  CHECK(schedule.saved().fetchedAt == now);
  CHECK(schedule.saved().leagueSet == std::vector<std::string>{"NFL", "NHL"});
}

TEST_CASE("a 400 retries the plain form, which is remembered") {
  Seconds now = utc(2026, 10, 10, 16, 0);
  Schedule schedule;
  REQUIRE(schedule.refreshIfNeeded(nflOnly(now)));
  auto first = schedule.nextRequest();
  REQUIRE(first);
  schedule.onResponse(400, "");
  auto retry = schedule.nextRequest();
  REQUIRE(retry);
  CHECK(retry->waitMs == kRequestSpacingMs);
  CHECK(retry->request.url == "https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard?dates=202610");
  schedule.onResponse(200, oneGame("1", "2026-10-11T17:00Z"));
  CHECK_FALSE(schedule.nextRequest());
  CHECK(schedule.saved().leagueForm.at("NFL") == "plain");
  // Next time the plain form goes first.
  REQUIRE(schedule.refreshNow(nflOnly(now + 60)));
  auto plain = schedule.nextRequest();
  REQUIRE(plain);
  CHECK(plain->request.url.find("limit") == std::string::npos);
}

TEST_CASE("college football: this week, then next week from the answer") {
  Seconds now = utc(2026, 10, 26, 16, 0);
  Schedule schedule;
  Context ctx = nflOnly(now);
  ctx.prefs.leagues = {"NCAAF"};
  REQUIRE(schedule.refreshIfNeeded(ctx));
  auto week = schedule.nextRequest();
  REQUIRE(week);
  CHECK(week->request.url.find("scoreboard?groups=80&limit=500") != std::string::npos);
  schedule.onResponse(200, oneGame("5", "2026-10-31T16:00Z"));
  auto next = schedule.nextRequest();
  REQUIRE(next);
  CHECK(next->request.url.find("week=10&seasontype=2&groups=80") != std::string::npos);
  schedule.onResponse(200, "{\"events\":[]}");
  // College: the school list (logos) is asked for once.
  auto schools = schedule.nextRequest();
  REQUIRE(schools);
  CHECK(schools->kind == Schedule::Pending::Kind::schools);
  schedule.onResponse(200, R"({"sports":[{"leagues":[{"teams":[
    {"team":{"id":"50","displayName":"Harbor State Gulls","location":"Harbor State"}},
    {"team":{"id":"51","displayName":"Lakeside College Owls","location":"Lakeside"}}]}]}]})");
  CHECK_FALSE(schedule.nextRequest());
  REQUIRE(schedule.saved().schools);
  CHECK(schedule.saved().schools->size() == 2);
  CHECK(schedule.saved().schoolsFetchedAt == now);
}

TEST_CASE("failures back off 1 h, 1 h, 6 h, 24 h, then weekly; network trouble doesn't count") {
  Seconds now = utc(2026, 10, 10, 16, 0);
  Schedule schedule(Saved(), "42");
  auto failAll = [&](Seconds at, int status) {
    REQUIRE(schedule.refreshIfNeeded(nflOnly(at)));
    while (auto r = schedule.nextRequest()) schedule.onResponse(status, "");
    auto done = schedule.takeFinished();
    REQUIRE(done);
    CHECK_FALSE(done->succeeded);
  };
  failAll(now, 500);
  CHECK(schedule.saved().failures == 1);
  CHECK(schedule.saved().failedAt == now);
  CHECK_FALSE(schedule.refreshIfNeeded(nflOnly(now + 30 * 60)));
  failAll(now + 3600, 500);
  CHECK(schedule.saved().failures == 2);
  // The third wait is 6 hours.
  CHECK_FALSE(schedule.refreshIfNeeded(nflOnly(now + 3 * 3600)));
  failAll(now + 8 * 3600, 0);  // offline: not counted
  CHECK(schedule.saved().failures == 2);
  // A new build starts the count again.
  Schedule updated(schedule.saved(), "43");
  CHECK(updated.refreshIfNeeded(nflOnly(now + 9 * 3600 + 60)));
}

TEST_CASE("before setup, a league none of your channels showed is asked weekly") {
  Seconds now = utc(2026, 10, 10, 16, 0);
  Saved saved;
  saved.leagueFetched["NBA"] = now - 3 * 86'400;
  saved.leagueFetched["NHL"] = now - 3 * 86'400;
  saved.leagueWatchable["NHL"] = now - 86'400;
  Schedule schedule(saved);
  Context ctx;
  ctx.now = now;
  ctx.zone = time::Zone::usEastern();
  REQUIRE(schedule.refreshIfNeeded(ctx));
  std::set<std::string> asked;
  while (auto r = schedule.nextRequest()) {
    asked.insert(r->league.shortName);
    schedule.onResponse(200, "{\"events\":[]}");
  }
  CHECK(asked.count("NFL"));
  CHECK(asked.count("NHL"));        // watchable last week
  CHECK_FALSE(asked.count("NBA"));  // skipped this week
  schedule.noteWatchable({"NBA"}, now);
  CHECK(schedule.takeDirty());
  CHECK(schedule.saved().leagueWatchable.at("NBA") == now);
  // Not written again within 6 hours.
  schedule.noteWatchable({"NBA"}, now + 3600);
  CHECK_FALSE(schedule.takeDirty());
}

TEST_CASE("published games: active leagues, finished ones for a week, by start") {
  Seconds now = utc(2026, 10, 26, 16, 0);
  Saved saved;
  auto g = [&](const std::string& league, const std::string& id, Seconds start) {
    return sportsdata::game(league, league, id, sportsdata::team("1", "A"), sportsdata::team("2", "B"), start);
  };
  saved.games = {g("NFL", "2", now + 7200), g("NFL", "1", now - 3600), g("NFL", "week", now - 5 * 86'400),
                 g("NFL", "old", now - 8 * 86'400), g("NHL", "3", now + 3600)};
  saved.fetchedAt = now - 600;
  Schedule schedule(saved);
  Published p = schedule.publish(nflOnly(now));
  // Last week's game stays: a leftover slot naming it is told apart.
  REQUIRE(p.games.size() == 3);
  CHECK(p.games[0].id == "NFL|week");
  CHECK(p.games[1].id == "NFL|1");
  CHECK(p.games[2].id == "NFL|2");
  CHECK(p.updatedAt == now - 600);
  Context off = nflOnly(now);
  off.enabled = false;
  CHECK(schedule.publish(off).games.empty());
}

TEST_CASE("saved schedule JSON round trip") {
  Saved saved;
  SportsGame g = sportsdata::game("NFL", "NFL", "1", sportsdata::team("1", "Harbor City Gulls", std::string("HCG")),
                                  sportsdata::team("2", "Team A"), utc(2026, 10, 25, 17, 0), {"FOX"},
                                  std::string("Gull Field"));
  g.note = "Sunday Night";
  g.home.rank = 4;
  g.home.color = "00274C";
  saved.games = {g};
  saved.fetchedAt = 100;
  saved.leagueFetched["NFL"] = 90;
  saved.leagueForm["NFL"] = "plain";
  saved.failures = 1;
  saved.rosters["NFL"] = {g.home, g.away};
  saved.schools = std::vector<SportsTeam>{sportsdata::team("50", "Harbor State")};
  saved.leagueSet = std::vector<std::string>{"NFL"};
  saved.schema = kSchema;
  auto back = savedFromJson(toJson(saved));
  REQUIRE(back);
  REQUIRE(back->games.size() == 1);
  CHECK(back->games[0] == g);
  CHECK(back->fetchedAt == 100);
  CHECK(back->leagueFetched == saved.leagueFetched);
  CHECK(back->leagueForm == saved.leagueForm);
  CHECK(back->failures == 1);
  CHECK(back->rosters.at("NFL").size() == 2);
  CHECK(back->schools->size() == 1);
  CHECK(back->leagueSet == saved.leagueSet);
  CHECK(back->schema == kSchema);
  CHECK_FALSE(savedFromJson("[1,2]"));
}

TEST_CASE("teams to pick: the schedule's, plus schools for college") {
  Saved saved;
  saved.rosters["NCAAF"] = {sportsdata::team("2", "Zeta Tech"), sportsdata::team("1", "alpha state")};
  saved.schools = std::vector<SportsTeam>{sportsdata::team("1", "Alpha State"), sportsdata::team("3", "Beta U")};
  Schedule schedule(saved);
  auto teams = schedule.teams(*leagueByShort("NCAAF"));
  REQUIRE(teams.size() == 3);
  CHECK(teams[0].name == "alpha state");  // the schedule's copy wins
  CHECK(teams[1].name == "Beta U");
  CHECK(teams[2].name == "Zeta Tech");
  CHECK(schedule.teams(nfl()).empty());
}

TEST_CASE("golf: a tournament as its round on now") {
  std::string body = R"({"events":[{"id":"401","name":"Harbor Championship","shortName":"Harbor Champ.","date":"2026-04-09T04:00Z",
    "competitions":[{"status":{"period":3,"type":{"name":"STATUS_IN_PROGRESS","state":"in"}},"competitors":[]}]},
    {"id":"402","name":"Gone Classic","date":"2026-04-09T04:00Z","competitions":[{"status":{"period":0,"type":{"name":"STATUS_CANCELED","state":"post"}}}]}]})";
  auto games = parseGolfTournaments(body, *leagueByShort("PGA"));
  REQUIRE(games);
  REQUIRE(games->size() == 1);  // a cancelled one isn't kept
  const SportsGame& g = games->front();
  CHECK(g.id == "PGA|401");
  CHECK(g.isGolf());
  CHECK(g.home.name == "Harbor Championship");
  CHECK(g.note == "Harbor Champ.");
  CHECK(g.start == utc(2026, 4, 11, 11, 0));  // round 3: two days on, 07:00 Eastern
  CHECK_FALSE(g.completed);
  CHECK(lengthOf("Golf", &g) == 11 * 3600);
  CHECK(lengthOf("Golf", nullptr) == 4 * 3600);  // a golf feed without a schedule
  // Saved and read back.
  auto back = gameFromJson(toJson(g));
  REQUIRE(back);
  CHECK(back->isGolf());
}

TEST_CASE("schedule: racing also asks for the coming Sunday (the next race weekend)") {
  League f1;
  f1.mode = League::Mode::current;
  f1.racing = true;
  // Wednesday 28 October 2026: also Sunday 1 November.
  auto q = queriesFor(f1, utc(2026, 10, 28, 15, 0), time::Zone::utc());
  CHECK(q == std::vector<std::string>{"", "dates=20261101"});
  f1.racing = false;
  CHECK(queriesFor(f1, utc(2026, 10, 28, 15, 0), time::Zone::utc()) == std::vector<std::string>{""});
}

TEST_CASE("all-soccer listing: the competition as the tag, a league's own game kept once") {
  std::optional<json::Value> event = json::parse(R"JSON({"id":"77","date":"2026-10-28T17:45Z",
    "season":{"slug":"league-phase"},
    "competitions":[{"altGameNote":"UEFA Women's Champions League, League Phase","competitors":[
      {"homeAway":"home","team":{"id":"1","displayName":"Harbor"}},
      {"homeAway":"away","team":{"id":"2","displayName":"Valley"}}]}]})JSON");
  REQUIRE(event);
  auto game = parseGame(*event, soccerAllLeague());
  REQUIRE(game);
  CHECK(game->note == "UWCL \xC2\xB7 League Phase");
  CHECK(game->tournament == "UEFA Women's Champions League");
  CHECK(soccerCompetitionTag("NCAAW Soccer") == "NCAA Women's");
  CHECK(soccerCompetitionTag("Harbor Cup") == "Harbor Cup");

  Seconds now = utc(2026, 10, 26, 16, 0);
  Saved saved;
  auto g = [&](const std::string& league, const std::string& id) {
    return sportsdata::game(league, "Soccer", id, sportsdata::team("1", "A"), sportsdata::team("2", "B"), now + 3600);
  };
  saved.games = {g("EPL", "10"), g(kSoccerAll, "10"), g(kSoccerAll, "11")};
  Schedule schedule(saved);
  Context ctx = nflOnly(now);
  // Build 127: the all-soccer listing with every other competition picked.
  ctx.prefs.leagues = {"EPL", kSoccerAllPick};
  std::vector<std::string> ids;
  for (const SportsGame& each : schedule.publish(ctx).games) ids.push_back(each.id);
  CHECK(ids == std::vector<std::string>{"EPL|10", std::string(kSoccerAll) + "|11"});
  // A league alone: its own games only.
  ctx.prefs.leagues = {"EPL"};
  ids.clear();
  for (const SportsGame& each : schedule.publish(ctx).games) ids.push_back(each.id);
  CHECK(ids == std::vector<std::string>{"EPL|10"});
}

TEST_CASE("all-soccer listing: each game's soccer pick, by the listing's league id, its name or a national team") {
  auto parsed = [](const std::string& uid, const std::string& note, const std::string& home,
                   const std::string& away) {
    std::optional<json::Value> event = json::parse(R"JSON({"id":"77","uid":")JSON" + uid +
                                                   R"JSON(","date":"2026-10-28T17:45Z","competitions":[{"altGameNote":")JSON" +
                                                   note + R"JSON(","competitors":[
      {"homeAway":"home","team":{"id":"1","displayName":")JSON" + home + R"JSON("}},
      {"homeAway":"away","team":{"id":"2","displayName":")JSON" + away + R"JSON("}}]}]})JSON");
    REQUIRE(event);
    auto game = parseGame(*event, soccerAllLeague());
    REQUIRE(game);
    return game->competition.value_or("");
  };
  CHECK(parsed("s:600~l:700~e:1", "English Premier League", "Harbor", "Valley") == "EPL");
  CHECK(parsed("s:600~l:775~e:2", "UEFA Champions League, League Phase", "Harbor", "Valley") == "UCL");
  CHECK(parsed("s:600~l:760~e:3", "Liga MX", "Harbor", "Valley") == "LMX");
  CHECK(parsed("s:600~l:8301~e:4", "NWSL", "Harbor", "Valley") == "NWSL");
  // No id: the competition's name ("Serie A", not "Brazil Serie A").
  CHECK(parsed("", "Serie A", "Harbor", "Valley") == "SA");
  CHECK(parsed("", "Brazil Serie A", "Harbor", "Valley") == kSoccerAllPick);
  // Any other competition, college soccer too.
  CHECK(parsed("s:600~l:3932~e:5", "Liga de Expansi\xC3\xB3n MX", "Harbor", "Valley") == kSoccerAllPick);
  CHECK(parsed("s:600~l:5499~e:6", "NCAAW Soccer", "Harbor", "Valley") == kSoccerAllPick);
  // National teams, men's or women's, in any competition and either side.
  CHECK(parsed("s:600~l:4000~e:7", "Concacaf Gold Cup, Group A", "United States", "Panama") == kUSAPick);
  CHECK(parsed("s:600~l:3923~e:8", "Women's International Friendly", "Brazil", "United States Women") == kUSAPick);
  CHECK(parsed("s:600~l:4000~e:9", "Concacaf Nations League", "M\xC3\xA9xico", "Jamaica") == kMexicoPick);
  CHECK(parsed("s:600~l:4001~e:10", "FIFA World Cup", "Mexico", "United States") == kUSAPick);
  // Youth sides aren't the team.
  CHECK(parsed("s:600~l:20865~e:11", "FIFA U-17 Women's World Cup", "Mexico U17", "Japan U17") == kSoccerAllPick);

  // A saved game from before Build 127 (no competition): by its tournament.
  SportsGame old = sportsdata::game(kSoccerAll, "Soccer", "12", sportsdata::team("1", "Harbor"),
                                    sportsdata::team("2", "Valley"), utc(2026, 10, 28, 17, 0));
  old.tournament = std::string("UEFA Europa League");
  CHECK(soccerCompetition(old) == "UEL");
  old.tournament = std::string("Copa del Rey");
  CHECK(soccerCompetition(old) == kSoccerAllPick);
  // A league's own game; the small-college pages are any other competition.
  SportsGame epl = old;
  epl.leagueShort = "EPL";
  CHECK(soccerCompetition(epl) == "EPL");
  SportsGame d3 = old;
  d3.leagueShort = "D3WSO";
  CHECK(soccerCompetition(d3) == kSoccerAllPick);
  // The competition is saved with the game.
  SportsGame withComp = old;
  withComp.competition = std::string("LMX");
  CHECK(gameFromJson(toJson(withComp)) == std::optional<SportsGame>(withComp));
}
