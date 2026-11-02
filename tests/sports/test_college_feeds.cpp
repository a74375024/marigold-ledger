// Small-college provider feeds ("volleyball:  Adelphi vs American
// International _ Women`s (Adelphi vs AIC) (2026-11-03 19:00:00)") joined to
// the games of the NCAA's scoreboard pages: the pages' short names, games
// with no time set, placeholder times, the side of the sport and schools
// that play several sports at once.
#include <algorithm>
#include <string>
#include <vector>

#include "core/sports/leagues.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/ncaa.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::matcher;
using sportsdata::utc;

namespace {

// Monday 2 November 2026, noon US Eastern.
const Seconds kNow = utc(2026, 11, 2, 17, 0);

std::string page(const std::string& games) {
  return "<html><head><script type=\"application/json\" data-drupal-selector=\"drupal-settings-json\">"
         "{\"scoreboard\":{\"initialGames\":[" +
         games + "]}}</script></head><body></body></html>";
}

struct Side {
  const char* seo;
  const char* name;
};

std::string contest(int id, Seconds start, Side home, Side away, bool timeSet = true) {
  auto team = [](bool isHome, Side s) {
    return std::string("{\"isHome\":") + (isHome ? "true" : "false") + ",\"seoname\":\"" + s.seo +
           "\",\"nameShort\":\"" + s.name + "\",\"score\":null}";
  };
  return "{\"contestId\":" + std::to_string(id) +
         ",\"gameState\":\"P\",\"statusCodeDisplay\":\"pre\",\"currentPeriod\":\"\",\"contestClock\":\"\","
         "\"finalMessage\":\"\",\"startTimeEpoch\":" +
         std::to_string(start) + ",\"hasStartTime\":" + (timeSet ? "true" : "false") +
         ",\"roundDescription\":\"\",\"teams\":[" + team(true, home) + "," + team(false, away) + "]}";
}

std::vector<SportsGame> gamesOf(const char* league, const std::vector<std::string>& contests) {
  std::string all;
  for (const std::string& c : contests) all += (all.empty() ? "" : ",") + c;
  auto games = ncaa::parseGames(page(all), *leagueByShort(league));
  return games ? *games : std::vector<SportsGame>{};
}

ProviderEvent feed(const std::string& id, const std::string& title, const std::string& league, Seconds start) {
  ProviderEvent e;
  e.id = id;
  e.title = title;
  e.league = league;
  e.start = start;
  e.feeds = {"flo|" + id};
  e.family = "Flo";
  return e;
}

// The game (by id) whose tile took the feed, or "" when none did.
std::string gameOf(const BuildResult& out, const std::string& feedID) {
  for (const SportsEvent& e : out.events) {
    for (const EventSource& s : e.sources) {
      if (s.channelID == "flo|" + feedID) return e.game ? e.game->id : "";
    }
  }
  return "";
}

BuildInput input(std::vector<SportsGame> games, std::vector<ProviderEvent> events) {
  BuildInput in;
  in.games = std::move(games);
  std::stable_sort(in.games.begin(), in.games.end(),
                   [](const SportsGame& a, const SportsGame& b) { return a.start < b.start; });
  in.events = std::move(events);
  in.now = kNow;
  in.zone = time::Zone::usEastern();
  return in;
}

}  // namespace

TEST_CASE("college feeds: the pages' short names, and games with no time set") {
  // Tuesday 3 and Wednesday 4 November, US Eastern (UTC-5).
  const Seconds tue7pm = utc(2026, 11, 4, 0, 0), tue6pm = utc(2026, 11, 3, 23, 0);
  const Seconds tueMidnight = utc(2026, 11, 3, 5, 0);
  const Seconds wed6pm = utc(2026, 11, 4, 23, 0), wed7pm = utc(2026, 11, 5, 0, 0);
  std::vector<SportsGame> games = gamesOf(
      "D2WVB", {contest(1, tue7pm, {"american-intl", "American Int'l"}, {"adelphi", "Adelphi"}),
                contest(2, tue7pm, {"assumption", "Assumption"}, {"southern-nh", "Southern N.H."}),
                contest(3, wed7pm, {"purdue-northwest", "Purdue Northwest"}, {"wayne-st-mi", "Wayne St. (MI)"})});
  for (SportsGame& g : gamesOf(
           "D3WVB", {contest(4, tueMidnight, {"coast-guard", "Coast Guard"}, {"mount-holyoke", "Mount Holyoke"}, false),
                     contest(5, tue6pm, {"castleton", "VTSU Castleton"}, {"mcla", "MCLA"}),
                     contest(6, wed6pm, {"randolph", "Randolph"}, {"roanoke", "Roanoke"}),
                     contest(7, wed7pm, {"randolph-macon", "Randolph-Macon"}, {"regent", "Regent"}),
                     contest(8, wed7pm, {"case", "CWRU"}, {"bethany-wv", "Bethany (WV)"}),
                     contest(9, wed6pm, {"greensboro", "Greensboro"}, {"brevard", "Brevard"})})) {
    games.push_back(std::move(g));
  }
  REQUIRE(games.size() == 9);
  BuildInput in = input(
      games,
      {feed("a", "volleyball:  Adelphi vs American International _ Women`s (Adelphi vs AIC)", "Volleyball",
            tue7pm + 30),
       feed("b", "volleyball:  Southern New Hampshire vs Assumption _ Women`s (SNHU vs Assumption)", "Volleyball",
            tue7pm + 45),
       feed("c", "volleyball:  Mount Holyoke vs Coast Guard Academy _ Women`s (Mount Holyoke vs USCGA)", "Volleyball",
            tue7pm + 25),
       feed("d", "volleyball:  Mass_Liberal Arts vs VTSU_Castleton _ Women`s (Mass_Liberal Arts vs VTSU_Castleton)",
            "Volleyball", tue6pm + 65),
       feed("e", "volleyball:  Roanoke vs Randolph _ Women`s (Roanoke vs Randolph)", "Volleyball", wed6pm + 10),
       feed("f", "volleyball:  Regent University vs Randolph_Macon College _ Women`s (Regent vs Randolph_Macon)",
            "Volleyball", wed7pm + 20),
       feed("g", "volleyball:  Bethany (WV) vs Case Western Reserve _ Women`s (Bethany vs CWRU)", "Volleyball",
            wed7pm + 110),
       feed("h", "volleyball:  Brevard College vs Greensboro College _ Women`s (Brevard College vs Greensboro)",
            "Volleyball", wed6pm - 15 * 60),
       feed("i", "volleyball:  Wayne State (MI) vs Purdue Northwest _ Women`s", "Volleyball", wed7pm + 300)});
  auto out = build(in);
  REQUIRE(out);
  CHECK(gameOf(*out, "a") == "D2WVB|1");  // American Int'l
  CHECK(gameOf(*out, "b") == "D2WVB|2");  // Southern N.H.
  CHECK(gameOf(*out, "c") == "D3WVB|4");  // Coast Guard (Academy), no time set
  CHECK(gameOf(*out, "d") == "D3WVB|5");  // MCLA
  CHECK(gameOf(*out, "e") == "D3WVB|6");  // Randolph, not Randolph-Macon
  CHECK(gameOf(*out, "f") == "D3WVB|7");  // Randolph-Macon, not Randolph
  CHECK(gameOf(*out, "g") == "D3WVB|8");  // CWRU
  CHECK(gameOf(*out, "h") == "D3WVB|9");
  CHECK(gameOf(*out, "i") == "D2WVB|3");  // Wayne St. (MI)
  // The game with no time set shows at its feed's.
  auto tba = std::find_if(out->events.begin(), out->events.end(),
                          [](const SportsEvent& e) { return e.game && e.game->id == "D3WVB|4"; });
  REQUIRE(tba != out->events.end());
  CHECK(tba->start == tue7pm + 25);
}

TEST_CASE("college feeds: the side of the sport, placeholder times, and schools playing two sports at once") {
  // Tuesday 3 November: both soccer sides' games with no time set yet.
  const Seconds tueMidnight = utc(2026, 11, 3, 5, 0);
  const Seconds fri7pm = utc(2026, 11, 7, 0, 0), satNoon = utc(2026, 11, 7, 17, 0);
  std::vector<SportsGame> games =
      gamesOf("D3MSO", {contest(1, tueMidnight, {"schreiner", "Schreiner"}, {"howard-payne", "Howard Payne"}, false)});
  for (const auto& [league, contests] : std::vector<std::pair<const char*, std::vector<std::string>>>{
           {"D3WSO", {contest(2, tueMidnight, {"howard-payne", "Howard Payne"}, {"schreiner", "Schreiner"}, false)}},
           {"D2WVB",
            {contest(3, fri7pm, {"texas-womans", "Texas Woman's"}, {"texas-tyler", "UT Tyler"}),
             contest(4, satNoon, {"saint-michaels", "Saint Michael's"}, {"american-intl", "American Int'l"})}},
           {"D3WVB", {contest(5, satNoon, {"cms", "CMS"}, {"caltech", "Caltech"})}}}) {
    for (SportsGame& g : gamesOf(league, contests)) games.push_back(std::move(g));
  }
  REQUIRE(games.size() == 5);
  // 05:56 Eastern: a time the provider puts on a game whose time isn't set.
  const Seconds tuePlaceholder = utc(2026, 11, 3, 10, 56), friPlaceholder = utc(2026, 11, 6, 10, 56);
  BuildInput in = input(
      games, {feed("men", "fc:  Howard Payne vs Schreiner _ Men`s (ASC Men`s Soccer Champ. _ G1)", "Soccer",
                   tuePlaceholder),
              feed("women", "fc:  Schreiner vs Howard Payne _ Women`s (ASC Women`s Soccer Champ. _ G1)", "Soccer",
                   tuePlaceholder + 10),
              feed("tyler", "volleyball:  UT Tyler vs Texas Woman`s _ Women`s (UT Tyler vs Texas Woman`s)",
                   "Volleyball", friPlaceholder),
              // The two schools' field hockey game isn't on the schedule; their
              // volleyball game at the same time isn't it.
              feed("hockey", "live:  American International vs Saint Michael`s _ Field Hockey (AIC vs Saint Michael`s)",
                   "Field Hockey", satNoon + 315),
              feed("volley", "volleyball:  American International vs Saint Michael`s _ Women`s", "Volleyball",
                   satNoon + 230),
              // Men's water polo is never the women's volleyball game.
              feed("polo", "swimming:  Caltech vs Claremont M_S _ Men`s Water Polo (Caltech vs CMS)", "Other",
                   satNoon + 240)});
  auto out = build(in);
  REQUIRE(out);
  CHECK(gameOf(*out, "men") == "D3MSO|1");
  CHECK(gameOf(*out, "women") == "D3WSO|2");
  CHECK(gameOf(*out, "tyler") == "D2WVB|3");
  CHECK(gameOf(*out, "hockey").empty());
  CHECK(gameOf(*out, "volley") == "D2WVB|4");
  CHECK(gameOf(*out, "polo").empty());
  // A placeholder time in no known sport: not enough to go on.
  out = build(input(games, {feed("other", "UT Tyler vs Texas Woman`s", "Other", friPlaceholder)}));
  REQUIRE(out);
  CHECK(gameOf(*out, "other").empty());
}

TEST_CASE("college feeds: a game both a page and the other scoreboards have goes to theirs") {
  const Seconds at = utc(2026, 11, 2, 22, 0);
  std::vector<SportsGame> games =
      gamesOf("D2WBB", {contest(1, at, {"kennesaw-st", "Kennesaw St."}, {"shorter", "Shorter"})});
  SportsGame espn = sportsdata::game("NCAAW", "College Basketball", "401", sportsdata::team("ks", "Kennesaw State Owls"),
                                     sportsdata::team("sh", "Shorter Hawks"), at);
  espn.home.location = "Kennesaw State";
  espn.away.location = "Shorter";
  games.push_back(espn);
  REQUIRE(games.size() == 2);
  BuildInput in = input(games, {feed("x", "Shorter vs. Kennesaw State Nov 02 5:00PM ET", "College Basketball", at + 20)});
  auto out = build(in);
  REQUIRE(out);
  CHECK(gameOf(*out, "x") == "NCAAW|401");
}

TEST_CASE("team index: \"Academy\" after a school's name is part of it, after a club's its youth side") {
  const Seconds at = utc(2026, 11, 7, 17, 0);
  std::vector<SportsGame> games = {
      sportsdata::game("D3WVB", "Volleyball", "1", sportsdata::team("ncaa:harbor", "Harbor Guard"),
                       sportsdata::team("ncaa:valley", "Valley"), at),
      sportsdata::game("MLS", "MLS", "2", sportsdata::team("c1", "Harbor City FC"),
                       sportsdata::team("c2", "Valley United"), at)};
  TeamIndex index(games);
  auto all = [](int) { return true; };
  CHECK(index.matches("Harbor Guard Academy vs Valley _ Women`s", all) == std::vector<int>{0});
  CHECK(index.matches("Harbor City FC Academy vs Valley United Academy", all).empty());
}
