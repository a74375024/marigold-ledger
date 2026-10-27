// The game schedule: scoreboard parsing (lenient: missing fields, numbers
// as strings), notes, fight cards, race weekends, tennis, request shapes,
// the fetch state machine with its request budget, and the saved cache.
#include <set>

#include "core/sports/my_sports.hpp"
#include "core/sports/schedule.hpp"
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
                   "logos": [{"href": "https://example.invalid/hcg.png"}]}},
         {"homeAway": "away", "curatedRank": {"current": "5"},
          "team": {"id": 2, "displayName": "Team A", "abbreviation": "TMA", "logo": "https://example.invalid/a.png",
                   "color": "not-a-color"}}
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
  CHECK(sf.customTitle == "Racket vs Player");
  CHECK(sf.note == "SF \xC2\xB7 Harbor City");
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
  CHECK(r.url == "https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard?dates=202610&limit=1000");
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
  CHECK(queriesFor(nfl(), utc(2026, 10, 10, 16, 0), et) == std::vector<std::string>{"dates=202610"});
  CHECK(queriesFor(*leagueByShort("UFC"), utc(2026, 10, 10, 16, 0), et) ==
        std::vector<std::string>{"dates=202610", ""});
  // Day by day in the viewer's zone: 02:00 UTC is still the 25th in New York.
  CHECK(queriesFor(*leagueByShort("NCAAM"), utc(2026, 10, 26, 2, 0), et) ==
        std::vector<std::string>{"dates=20261025", "dates=20261026", "dates=20261027"});
  CHECK(queriesFor(*leagueByShort("NCAAF"), now, et) == std::vector<std::string>{""});
}

TEST_CASE("active leagues: the standard set until My Sports is set up, then picks only") {
  MySportsPrefs prefs;
  SportFeedsSeen seen;
  Seconds now = sportsdata::saturday();
  std::set<std::string> shorts;
  for (const League& l : activeLeagues(prefs, seen, now)) shorts.insert(l.shortName);
  CHECK(shorts.count("NFL"));
  CHECK(shorts.count("EPL"));
  CHECK(shorts.count("F1"));
  CHECK_FALSE(shorts.count("WNBA"));
  CHECK_FALSE(shorts.count("ATP"));
  prefs.configured = true;
  prefs.leagues = {"NBA", "S:Fighting"};
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now)) shorts.insert(l.shortName);
  CHECK(shorts == std::set<std::string>{"NBA", "UFC", "PFL"});
  // NASCAR feeds seen in the last two weeks: its schedule too.
  seen.nascar = now - 86'400;
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now)) shorts.insert(l.shortName);
  CHECK(shorts == std::set<std::string>{"NBA", "UFC", "PFL", "NASCAR"});
  seen.nascar = now - 15 * 86'400;
  CHECK(activeLeagues(prefs, seen, now).size() == 3);
  // A sport picked as a whole: its leagues. IndyCar only with Motorsport.
  prefs.leagues = {"S:Rugby", "S:Volleyball", "S:Motorsport"};
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now)) shorts.insert(l.shortName);
  CHECK(shorts.count("6N"));
  CHECK(shorts.count("URC"));
  CHECK(shorts.count("NCAAWV"));
  CHECK(shorts.count("NCAAMV"));
  CHECK(shorts.count("IRL"));
  CHECK_FALSE(shorts.count("PLL"));
  prefs.leagues = {"NBA"};
  shorts.clear();
  for (const League& l : activeLeagues(prefs, seen, now)) shorts.insert(l.shortName);
  CHECK_FALSE(shorts.count("IRL"));
  CHECK(leagueByShort("NCAASB"));
  CHECK(leagueByShort("NCAASB")->path == "baseball/college-softball");
  CHECK(sportLeagues("S:Lacrosse").size() == 3);
  CHECK(sportLeagues("S:Nothing").empty());
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
  // "Golf" isn't F1.
  SportFeedsSeen other;
  std::optional<Seconds> otherChecked;
  CHECK_FALSE(noteSportFeeds(other, otherChecked,
                             {sportsdata::liveEvent("4", "Golf: Final Round", "Golf", now, {"g"})}, now));
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
  CHECK(first->request.url.find("dates=202610&limit=1000") != std::string::npos);
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
  CHECK(week->request.url.find("scoreboard?groups=80&limit=1000") != std::string::npos);
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

TEST_CASE("published games: active leagues, finished ones for a day and a half, by start") {
  Seconds now = utc(2026, 10, 26, 16, 0);
  Saved saved;
  auto g = [&](const std::string& league, const std::string& id, Seconds start) {
    return sportsdata::game(league, league, id, sportsdata::team("1", "A"), sportsdata::team("2", "B"), start);
  };
  saved.games = {g("NFL", "2", now + 7200), g("NFL", "1", now - 3600), g("NFL", "old", now - 40 * 3600),
                 g("NHL", "3", now + 3600)};
  saved.fetchedAt = now - 600;
  Schedule schedule(saved);
  Published p = schedule.publish(nflOnly(now));
  REQUIRE(p.games.size() == 2);
  CHECK(p.games[0].id == "NFL|1");
  CHECK(p.games[1].id == "NFL|2");
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
