// Sport profiles: the per-sport values every sports rule reads. These pin
// them, so a change to a sport's length, clock or naming is a deliberate
// edit of its entry.
#include <doctest.h>

#include <set>
#include <string>

#include "core/sports/board.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/profiles.hpp"
#include "sports/helpers.hpp"

using namespace marigold::sports;
using Clock = Profile::Clock;
using Tile = Profile::Tile;

namespace {

SportsGame part(const std::string& group, const std::string& league, const std::string& cardKey, bool racing = false) {
  SportsGame g;
  g.id = league + "|1|x";
  g.league = group;
  g.leagueShort = league;
  g.cardKey = cardKey;
  if (racing) g.racing = true;
  return g;
}

}  // namespace

TEST_CASE("profiles: typical lengths by sport") {
  CHECK(lengthOf("NFL", nullptr) == 12'960);
  CHECK(lengthOf("College Football", nullptr) == 12'960);
  CHECK(lengthOf("Football", nullptr) == 12'960);
  CHECK(lengthOf("MLB", nullptr) == 11'160);
  CHECK(lengthOf("Baseball", nullptr) == 11'160);
  for (const char* l : {"NBA", "WNBA", "College Basketball", "Basketball", "NHL", "Hockey"})
    CHECK(lengthOf(l, nullptr) == 9'360);
  CHECK(lengthOf("Soccer", nullptr) == 7'560);
  CHECK(lengthOf("MLS", nullptr) == 7'560);
  CHECK(lengthOf("Golf", nullptr) == 4 * 3600);
  CHECK(lengthOf("Fighting", nullptr) == 5 * 3600);
  CHECK(lengthOf("Motorsport", nullptr) == 4 * 3600);
  for (const char* l : {"Tennis", "MiLB", "Cricket", "Curling", "Other", ""}) CHECK(lengthOf(l, nullptr) == 9'000);
}

TEST_CASE("profiles: parts with lengths of their own") {
  SportsGame prelims = part("Fighting", "UFC", "ufc|333|prelims");
  SportsGame early = part("Fighting", "UFC", "ufc|333|early");
  SportsGame main = part("Fighting", "UFC", "ufc|333|main");
  SportsGame card = part("Fighting", "UFC", "ufc|333|card");
  CHECK(lengthOf("Fighting", &prelims) == 9'000);
  CHECK(lengthOf("Fighting", &early) == 9'000);
  CHECK(lengthOf("Fighting", &main) == 4 * 3600);
  CHECK(lengthOf("Fighting", &card) == 5 * 3600);

  SportsGame race = part("Motorsport", "F1", "f1|monaco|race", true);
  SportsGame quali = part("Motorsport", "F1", "f1|monaco|qual", true);
  SportsGame practice = part("Motorsport", "F1", "f1|monaco|fp1", true);
  CHECK(lengthOf("Motorsport", &race) == 8'280);
  CHECK(lengthOf("Motorsport", &quali) == 4'500);
  CHECK(lengthOf("Motorsport", &practice) == 3'960);
  // A NASCAR Cup race, whatever its session.
  SportsGame cup = part("Motorsport", "NASCAR", "nascar|daytona|race", true);
  CHECK(lengthOf("Motorsport", &cup) == 13'500);
  // The league decides: the same game listed as another sport.
  CHECK(lengthOf("Other", &race) == 9'000);
}

TEST_CASE("profiles: lookups by league within a group") {
  CHECK(profile("Motorsport", "F1").podium);
  CHECK_FALSE(profile("Motorsport", "NASCAR").podium);
  CHECK_FALSE(profile("Motorsport").podium);
  CHECK(profile("Motorsport", "IndyCar").league == "Motorsport");  // the group's entry
  CHECK(profile("Curling").league == "Other");
  CHECK(profile("Tennis").tile == Tile::Players);
  CHECK(profile("Fighting").tile == Tile::Card);
  CHECK(profile("Motorsport", "F1").tile == Tile::RaceWeekend);
  CHECK(profile("NFL").tile == Tile::Teams);
}

TEST_CASE("profiles: timeline, matching and naming") {
  CHECK(profile("NFL").overrun == 30 * 60);
  CHECK(profile("Golf").listingCap == 8 * 3600);
  CHECK(profile("Tennis").matchWindow == 8 * 3600);
  CHECK(profile("NBA").matchWindow == 3 * 3600);
  CHECK(matcher::completeLeagues() == std::set<std::string>{"NFL", "NBA", "WNBA", "NHL", "MLB"});
  CHECK(profile("Soccer").homeFirst);
  CHECK(profile("MLS").homeFirst);
  CHECK(profile("Fighting").homeFirst);
  CHECK_FALSE(profile("NFL").homeFirst);
  CHECK_FALSE(profile("Tennis").homeFirst);
  CHECK_FALSE(profile("Motorsport").homeFirst);
}

TEST_CASE("profiles: clocks") {
  CHECK(profile("Soccer").clock == Clock::Minutes);
  CHECK(profile("MLB").clock == Clock::Innings);
  CHECK(profile("Baseball").clock == Clock::Innings);
  CHECK(profile("MiLB").clock == Clock::Periods);
  CHECK(profile("NFL").quarterSeconds == 900);
  CHECK(profile("NBA").quarterSeconds == 720);
  CHECK(profile("WNBA").quarterSeconds == 600);
  CHECK(profile("College Basketball").periods == 2);
  CHECK(profile("NHL").periods == 3);
  LiveStatus s;
  s.phase = LivePhase::live;
  s.detail = "Q3 6:00";
  CHECK(*board::clockProgress(s, "NBA") == doctest::Approx((2 + 1 - 360.0 / 720) / 4));
  CHECK(*board::clockProgress(s, "NFL") == doctest::Approx((2 + 1 - 360.0 / 900) / 4));
  s.detail = "2nd 10:00";
  CHECK(*board::clockProgress(s, "NHL") == doctest::Approx((1 + 1 - 600.0 / 1200) / 3));
}

TEST_CASE("profiles: icons and chips") {
  CHECK(board::symbol("NFL") == "football.fill");
  CHECK(board::symbol("College Basketball") == "basketball.fill");
  CHECK(board::symbol("MiLB") == "baseball.fill");
  CHECK(board::symbol("MLS") == "soccerball");
  CHECK(board::symbol("Golf") == "figure.golf");
  CHECK(board::symbol("Curling") == "sportscourt.fill");
  CHECK(matcher::shortLeague("College Basketball") == "NCAAB");
  CHECK(matcher::shortLeague("Fighting") == "FIGHT");
  CHECK(matcher::shortLeague("Field Hockey") == "FIELD H.");
  CHECK(matcher::shortLeague("Curling") == "CURLING");
  CHECK(matcher::shortLeague("") == "EVENT");
}
