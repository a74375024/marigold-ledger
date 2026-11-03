#include <algorithm>
#include <string>
#include <vector>

#include "core/sports/leagues.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/ncaa.hpp"
#include "core/sports/scores.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::sports;

namespace {

const League& d3Women() { return *leagueByShort("D3WSO"); }

// A page as the scoreboard serves it: the day's games in its settings.
std::string page(const std::string& games) {
  return "<html><head><script type=\"application/json\" data-drupal-selector=\"drupal-settings-json\">"
         "{\"scoreboard\":{\"sportCode\":\"WSO\",\"initialGames\":[" +
         games + "]}}</script></head><body></body></html>";
}

std::string contest(const char* id, const char* state, const char* display, const char* period, const char* clock,
                    const char* home, const char* homeScore, const char* away, const char* awayScore) {
  return std::string("{\"contestId\":") + id + ",\"gameState\":\"" + state + "\",\"statusCodeDisplay\":\"" + display +
         "\",\"currentPeriod\":\"" + period + "\",\"contestClock\":\"" + clock +
         "\",\"finalMessage\":\"\",\"startTimeEpoch\":1793203200,\"roundDescription\":\"\",\"teams\":[" +
         "{\"isHome\":true,\"seoname\":\"h-" + id + "\",\"nameShort\":\"" + home + "\",\"score\":" + homeScore + "}," +
         "{\"isHome\":false,\"seoname\":\"a-" + id + "\",\"nameShort\":\"" + away + "\",\"score\":" + awayScore + "}]}";
}

}  // namespace

TEST_CASE("ncaa: a school's poll rank and conference (Build 127)") {
  std::string games =
      R"({"contestId":9,"gameState":"P","statusCodeDisplay":"pre","startTimeEpoch":1793203200,"teams":[)"
      R"({"isHome":true,"seoname":"harbor","nameShort":"Harbor","teamRank":7,"conferenceSeo":"little-east"},)"
      R"({"isHome":false,"seoname":"valley","nameShort":"Valley","teamRank":null,"conferenceSeo":"sunyac"}]})";
  auto parsed = ncaa::parseGames(page(games), *leagueByShort("D3MBB"));
  REQUIRE(parsed);
  REQUIRE(parsed->size() == 1);
  const SportsGame& g = parsed->front();
  CHECK(g.home.rank == std::optional<int>(7));
  CHECK_FALSE(g.away.rank);
  CHECK(g.home.conference == std::string("ncaa:little-east"));
  CHECK(g.away.conference == std::string("ncaa:sunyac"));
  CHECK(conferenceName("D3MBB", *g.away.conference) == std::string("SUNYAC"));
}

TEST_CASE("ncaa: the page for a sport, division and day") {
  CHECK(ncaa::pageUrl(d3Women(), "20261028") == "https://www.ncaa.com/scoreboard/soccer-women/d3/2026/10/28/all-conf");
  CHECK(ncaa::pageUrl(d3Women(), "").empty());
  CHECK(d3Women().ncaaPage);
  CHECK(leagueByShort("D2FH")->group == "Field Hockey");
}

TEST_CASE("ncaa: school names written out, and the names channels use") {
  CHECK(ncaa::writtenOut("Harbor St.") == "Harbor State");
  CHECK(ncaa::writtenOut("Southern Me.") == "Southern Maine");
  CHECK(ncaa::writtenOut("Mt. St. Harbor") == "Mount Saint Harbor");
  CHECK(ncaa::writtenOut("Cal St. Dom. Hills") == "Cal State Dominguez Hills");
  CHECK(ncaa::writtenOut("Cal St. San Marcos") == "Cal State San Marcos");
  CHECK(ncaa::writtenOut("Eastern Conn. St.") == "Eastern Connecticut State");
  CHECK(ncaa::writtenOut("Wis.-Harbor") == "Wisconsin-Harbor");
  CHECK(ncaa::writtenOut("Harbor Col.") == "Harbor College");
  CHECK(ncaa::writtenOut("Harbor (MA)") == "Harbor (MA)");
  std::vector<std::string> other = ncaa::otherNames("Harbor (MA)");
  CHECK(std::find(other.begin(), other.end(), "Harbor") != other.end());
  other = ncaa::otherNames("Harbor St.");
  CHECK(std::find(other.begin(), other.end(), "Harbor State") != other.end());
  CHECK(std::find(other.begin(), other.end(), "Harbor") != other.end());
  other = ncaa::otherNames("N.C. Harbor");
  CHECK(std::find(other.begin(), other.end(), "NC Harbor") != other.end());
  // States and words the pages shorten.
  CHECK(ncaa::writtenOut("Southern N.H.") == "Southern New Hampshire");
  CHECK(ncaa::writtenOut("Western N.M.") == "Western New Mexico");
  CHECK(ncaa::writtenOut("American Int'l") == "American International");
  CHECK(ncaa::writtenOut("Tex. A&M Int'l") == "Texas A&M International");
  CHECK(ncaa::writtenOut("San Fran. St.") == "San Francisco State");
  CHECK(ncaa::writtenOut("Western New Eng.") == "Western New England");
  CHECK(ncaa::writtenOut("Chris. Newport") == "Christopher Newport");
  CHECK(ncaa::writtenOut("East. Mennonite") == "Eastern Mennonite");
  // "St." before a bracket, or after a state, is State; else Saint.
  CHECK(ncaa::writtenOut("Wayne St. (MI)") == "Wayne State (MI)");
  CHECK(ncaa::writtenOut("Mont. St. Billings") == "Montana State Billings");
  CHECK(ncaa::writtenOut("Mt. St. Joseph") == "Mount Saint Joseph");
  CHECK(ncaa::writtenOut("St. Joseph's (L.I.)") == "Saint Joseph's (L.I.)");
}

TEST_CASE("ncaa: a game with no time set, and the names channels use for a school") {
  std::string body = page(
      "{\"contestId\":7,\"gameState\":\"P\",\"statusCodeDisplay\":\"pre\",\"currentPeriod\":\"\","
      "\"contestClock\":\"\",\"finalMessage\":\"\",\"startTimeEpoch\":1793682000,\"startTime\":\"00:00\","
      "\"hasStartTime\":false,\"roundDescription\":\"\",\"teams\":["
      "{\"isHome\":true,\"seoname\":\"coast-guard\",\"nameShort\":\"Coast Guard\",\"score\":null},"
      "{\"isHome\":false,\"seoname\":\"mount-holyoke\",\"nameShort\":\"Mount Holyoke\",\"score\":null}]}," +
      contest("8", "P", "pre", "", "", "Assumption", "null", "Southern N.H.", "null"));
  auto games = ncaa::parseGames(body, d3Women());
  REQUIRE(games);
  REQUIRE(games->size() == 2);
  const SportsGame& tba = (*games)[0];
  CHECK(tba.isDayOnly());
  CHECK(tba.start == 1793682000);  // that day's midnight, US Eastern
  const std::vector<std::string>& guard = tba.home.alternates;
  CHECK(std::find(guard.begin(), guard.end(), "Coast Guard Academy") != guard.end());
  CHECK(std::find(guard.begin(), guard.end(), "USCGA") != guard.end());
  CHECK_FALSE((*games)[1].isDayOnly());  // a time set (no "hasStartTime" at all: one too)
  CHECK((*games)[1].away.name == "Southern New Hampshire");
  // Saved and read back.
  auto back = gameFromJson(toJson(tba));
  REQUIRE(back);
  CHECK(back->isDayOnly());
  CHECK(*back == tba);
}

TEST_CASE("ncaa: games and statuses from a page") {
  std::string body = page(contest("1", "P", "pre", "", "", "Harbor St.", "null", "Valley Col.", "null") + "," +
                          contest("2", "I", "live", "2ND HALF", "61:05", "Cove", "2", "Ridge", "1") + "," +
                          contest("3", "F", "final", "FINAL", "", "Bay", "0", "Point", "3") + "," +
                          contest("4", "O", "postponed", "", "", "Glen", "null", "Dale", "null") + "," +
                          contest("5", "I", "live", "HALFTIME", "", "Ash", "1", "Elm", "1"));
  auto games = ncaa::parseGames(body, d3Women());
  REQUIRE(games);
  REQUIRE(games->size() == 5);
  const SportsGame& first = (*games)[0];
  CHECK(first.id == "D3WSO|1");
  CHECK(first.league == "Soccer");
  CHECK(first.home.name == "Harbor State");
  CHECK(first.away.name == "Valley College");
  CHECK(first.start == 1793203200);
  CHECK(first.note == "Division III \xC2\xB7 Women's");
  CHECK((*games)[2].completed);
  CHECK((*games)[3].postponed);

  scores::Statuses s = ncaa::statuses(body, d3Women());
  CHECK(s.at("D3WSO|1").phase == LivePhase::pre);
  CHECK(s.at("D3WSO|2").phase == LivePhase::live);
  CHECK(s.at("D3WSO|2").detail == "2nd Half 61:05");
  CHECK(s.at("D3WSO|2").homeScore == "2");
  CHECK(s.at("D3WSO|2").awayScore == "1");
  CHECK(s.at("D3WSO|3").phase == LivePhase::final);
  CHECK(s.at("D3WSO|3").detail == "Final");
  CHECK(s.at("D3WSO|3").awayScore == "3");
  CHECK(s.at("D3WSO|4").phase == LivePhase::postponed);
  CHECK(s.at("D3WSO|5").detail == "Halftime");
  // Not a scoreboard page.
  CHECK_FALSE(ncaa::parseGames("<html></html>", d3Women()));
  CHECK(ncaa::statuses("<html></html>", d3Women()).empty());
}

TEST_CASE("leagues of the sports your channels show games of, colleges by sport") {
  MySportsPrefs prefs;
  SportFeedsSeen seen;
  Seconds now = 1793203200;
  auto has = [&](const char* shortName) {
    for (const League& l : activeLeagues(prefs, seen, now, {})) {
      if (l.shortName == shortName) return true;
    }
    return false;
  };
  CHECK_FALSE(has("D3WSO"));
  CHECK_FALSE(has("URC"));
  std::optional<Seconds> checked;
  auto feed = [](const char* title, const char* league) {
    catalog::LiveEvent e;
    e.title = title;
    e.league = league;
    return e;
  };
  std::vector<catalog::LiveEvent> events = {feed("Harbor College vs Valley College", "Soccer"),
                                            feed("Harbor vs Valley", "Rugby"), feed("Cove at Ridge", "Hockey"),
                                            feed("Bay vs Point", "Football"), feed("Harbor Show", "WNBA")};
  CHECK(noteSportFeeds(seen, checked, events, now));
  // Build 127: unconfigured is the default picks: college soccer is every
  // other competition's, and rugby isn't a default.
  CHECK_FALSE(has("D3WSO"));
  CHECK_FALSE(has("D2FH"));  // no college field hockey on your channels
  CHECK_FALSE(has("URC"));
  CHECK(has("NCAAMH"));   // hockey games: college hockey (the NHL is a default)
  CHECK_FALSE(has("D3MHK"));  // but no college named: not Division III hockey
  CHECK(has("FCS"));      // football games: FCS
  CHECK_FALSE(has("D2/D3"));
  CHECK_FALSE(has("WNBA"));  // not a game
  // Saved and read back.
  SportFeedsSeen back = sportFeedsSeenFromJson(toJson(seen));
  CHECK(back == seen);
  // With My Sports set up: only with the sport picked.
  prefs.configured = true;
  prefs.leagues = {"NHL"};
  CHECK(has("NCAAMH"));
  CHECK_FALSE(has("D3WSO"));
  CHECK_FALSE(has("URC"));
  prefs.leagues = {"S:Rugby"};
  CHECK(has("URC"));  // rugby games: the rugby competitions
  // College soccer comes with every other soccer competition, not a league.
  prefs.leagues = {"EPL"};
  CHECK_FALSE(has("D3WSO"));
  prefs.leagues = {kSoccerAllPick};
  CHECK(has("D3WSO"));    // a college soccer game: Divisions II and III soccer
  CHECK_FALSE(has("NCAAMH"));
  // Hidden: never looked up.
  prefs.hidden = {"D3WSO"};
  CHECK_FALSE(has("D3WSO"));
}
