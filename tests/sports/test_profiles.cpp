// Sport profiles: the per-sport values every sports rule reads. These pin
// them, so a change to a sport's length, clock or naming is a deliberate
// edit of its entry.
#include <doctest.h>

#include <optional>
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
  CHECK(lengthOf("MiLB", nullptr) == 11'160);
  CHECK(lengthOf("Cricket", nullptr) == 12'600);
  for (const char* l : {"NBA", "WNBA", "College Basketball", "Basketball", "NHL", "Hockey"})
    CHECK(lengthOf(l, nullptr) == 9'360);
  CHECK(lengthOf("Soccer", nullptr) == 7'560);
  CHECK(lengthOf("MLS", nullptr) == 7'560);
  CHECK(lengthOf("Golf", nullptr) == 4 * 3600);
  CHECK(lengthOf("Fighting", nullptr) == 5 * 3600);
  CHECK(lengthOf("Motorsport", nullptr) == 4 * 3600);
  for (const char* l : {"Tennis", "Rugby", "Curling", "Other", ""}) CHECK(lengthOf(l, nullptr) == 9'000);
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
  CHECK(profile("NFL").listingCap == 8 * 3600);
  CHECK(profile("Golf").listingCap == 12 * 3600);
  CHECK(profile("Cricket").listingCap == 12 * 3600);
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
  CHECK(profile("MiLB").clock == Clock::Innings);
  CHECK(profile("Fighting").clock == Clock::None);
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
  CHECK(board::symbol("Football") == "football.fill");
  CHECK(board::symbol("Basketball") == "basketball.fill");
  CHECK(matcher::shortLeague("College Basketball") == "NCAAB");
  CHECK(matcher::shortLeague("Fighting") == "FIGHT");
  CHECK(matcher::shortLeague("Field Hockey") == "FIELD H.");
  CHECK(matcher::shortLeague("Curling") == "CURLING");
  CHECK(matcher::shortLeague("") == "EVENT");
}

namespace {

constexpr Seconds kStart = 1'800'000'000;

SportsEvent eventIn(const std::string& league, std::optional<SportsGame> game = std::nullopt) {
  SportsEvent e;
  e.id = "e";
  e.league = league;
  e.start = kStart;
  e.game = std::move(game);
  return e;
}

SportsGame tennisMatch(const std::string& tour, const std::string& tournament, const std::string& a,
                       const std::string& b) {
  SportsGame g;
  g.id = tour + "|1|2";
  g.league = "Tennis";
  g.leagueShort = tour;
  g.tennis = true;
  g.tournament = tournament;
  g.home.name = a;
  g.away.name = b;
  g.start = kStart;
  return g;
}

SportsGame soccerMatch(const std::string& league, std::optional<std::string> note = std::nullopt) {
  SportsGame g;
  g.id = league + "|1";
  g.league = league == "MLS" ? "MLS" : "Soccer";
  g.leagueShort = league;
  g.homeFirst = true;
  g.note = std::move(note);
  g.start = kStart;
  return g;
}

using State = SportsEvent::State;

}  // namespace

TEST_CASE("profiles: golf and cricket stay live as long as the listing, up to 12 hours") {
  SportsEvent golf = eventIn("Golf");
  golf.listingEnd = kStart + 10 * 3600;
  CHECK(golf.state(kStart + 9 * 3600) == State::live);
  CHECK(golf.state(kStart + 10 * 3600 + 60) == State::mayHaveEnded);
  golf.listingEnd = kStart + 13 * 3600;  // past the cap: the usual length
  CHECK(golf.state(kStart + 4 * 3600 + 10 * 60) == State::mayHaveEnded);
  SportsEvent cricket = eventIn("Cricket");
  CHECK(cricket.state(kStart + 3 * 3600) == State::live);  // a T20 without a listing
  cricket.listingEnd = kStart + 8 * 3600;
  CHECK(cricket.state(kStart + 7 * 3600) == State::live);
  // Other sports still stop at 8 hours.
  SportsEvent nfl = eventIn("NFL");
  nfl.listingEnd = kStart + 9 * 3600;
  CHECK(nfl.state(kStart + 5 * 3600) == State::finished);
}

TEST_CASE("profiles: men's Grand Slam singles run best of five") {
  SportsGame slam = tennisMatch("ATP", "Wimbledon", "A. Player", "B. Player");
  CHECK(lengthOf("Tennis", &slam) == 4 * 3600);
  SportsGame women = tennisMatch("WTA", "Wimbledon", "C. Player", "D. Player");
  CHECK(lengthOf("Tennis", &women) == 9'000);
  SportsGame doubles = tennisMatch("ATP", "US Open", "A. One / B. Two", "C. Three / D. Four");
  CHECK(lengthOf("Tennis", &doubles) == 9'000);
  SportsGame masters = tennisMatch("ATP", "Miami Open", "A. Player", "B. Player");
  CHECK(lengthOf("Tennis", &masters) == 9'000);
  CHECK(isGrandSlam("Roland Garros"));
  CHECK(isGrandSlam("Australian Open"));
  CHECK_FALSE(isGrandSlam("Open 13"));
  // Live until 4 h, then "may have ended" for an hour (a long match runs on).
  SportsEvent e = eventIn("Tennis", slam);
  CHECK(e.state(kStart + 3 * 3600 + 50 * 60) == State::live);
  CHECK(e.state(kStart + 4 * 3600 + 50 * 60) == State::mayHaveEnded);
  CHECK(e.state(kStart + 5 * 3600 + 10 * 60) == State::finished);
}

TEST_CASE("profiles: a soccer knockout may run into extra time and penalties") {
  // A league match: "may have ended" from 2.1 h for half an hour.
  SportsEvent league = eventIn("Soccer", soccerMatch("EPL"));
  CHECK(league.state(kStart + 2 * 3600 + 20 * 60) == State::mayHaveEnded);
  CHECK(league.state(kStart + 2 * 3600 + 40 * 60) == State::finished);
  // A cup match, or a round the schedule names: an hour.
  SportsEvent cup = eventIn("Soccer", soccerMatch("UCL"));
  CHECK(cup.state(kStart + 2 * 3600 + 50 * 60) == State::mayHaveEnded);
  CHECK(cup.state(kStart + 3 * 3600 + 10 * 60) == State::finished);
  SportsEvent playoff = eventIn("MLS", soccerMatch("MLS", std::string("Round One · Playoffs")));
  CHECK(playoff.state(kStart + 2 * 3600 + 50 * 60) == State::mayHaveEnded);
  SportsEvent leg = eventIn("Soccer", soccerMatch("LMX", std::string("Quarterfinal · 2nd Leg")));
  CHECK(leg.state(kStart + 2 * 3600 + 50 * 60) == State::mayHaveEnded);
}

TEST_CASE("profiles: fight cards' progress goes by the time, not a soccer clock") {
  LiveStatus s;
  s.phase = LivePhase::live;
  s.detail = "R2 3:12";
  CHECK_FALSE(board::clockProgress(s, "Fighting"));
}
