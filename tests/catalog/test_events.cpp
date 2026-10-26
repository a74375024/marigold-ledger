// Event titles: every start-time format, provider zones, decorations,
// deduplication keys and league detection.
#include "catalog/helpers.hpp"
#include "core/catalog/catalog.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::utc;
using testdata::wednesdayNoon;

namespace {

ParsedEvent parse(const std::string& text, std::optional<std::string> market = std::nullopt) {
  return parseEventDetailed(text, wednesdayNoon(), market);
}

}  // namespace

TEST_CASE("day month time and a zone abbreviation or offset") {
  auto edt = parse("Team A vs Team B Fri 23 Oct 18:00 EDT (US)");
  CHECK(edt.title == "Team A vs Team B");
  CHECK(edt.start == utc(2026, 10, 23, 22, 0));
  CHECK_FALSE(edt.guessed);
  CHECK(parse("Team A vs Team B Fri 23 Oct 02:05 CEST (SE)").start == utc(2026, 10, 23, 0, 5));
  CHECK(parse("Team A vs Team B Fri 23 Oct 19:40 +03 (SA)").start == utc(2026, 10, 23, 16, 40));
  CHECK(parse("Team A vs Team B 23 Oct 19:40 AEDT").start == utc(2026, 10, 23, 8, 40));
  // An unknown zone word is left for the later formats.
  auto unknown = parse("Team A vs Team B 23 Oct 19:40 XYZ");
  CHECK(unknown.title.find("XYZ") != std::string::npos);
}

TEST_CASE("ISO and day-month-year dates in GMT") {
  auto iso = parse("Team A v Team B | 2026-10-18 | 15:30 (GMT)");
  CHECK(iso.title == "Team A v Team B");
  CHECK(iso.start == utc(2026, 10, 18, 15, 30));
  CHECK(parse("Team A v Team B 22-10-2026 | 00:00 (UTC)").start == utc(2026, 10, 22, 0, 0));
}

TEST_CASE("month day 24-hour: UK time, or the provider's zone") {
  auto uk = parse("Team A v Team B Oct 22 20:45");
  CHECK(uk.title == "Team A v Team B");
  CHECK(uk.start == utc(2026, 10, 22, 19, 45));  // BST
  CHECK(parse("Team A v Team B Oct 22 20:45", std::string("ES")).start ==
        utc(2026, 10, 22, 18, 45));
  // After the clocks go back (25 October in Europe).
  CHECK(parse("Team A v Team B Oct 27 20:45").start == utc(2026, 10, 27, 20, 45));
}

TEST_CASE("bracketed and start: timestamps are Eastern (or the provider's zone)") {
  auto paren = parse("Team A vs Team B (2026 10 21 19:30:55)");
  CHECK(paren.title == "Team A vs Team B");
  CHECK(paren.start == utc(2026, 10, 21, 23, 30));
  CHECK(parse("Team A vs Team B (2026-10-22 15:00:25)").start == utc(2026, 10, 22, 19, 0));
  CHECK(parse("Team A vs Team B (2026 10 21 19:30:55)", std::string("UK")).start ==
        utc(2026, 10, 21, 18, 30));
  auto range = parse("start:2026 10 17 20:25:00 stop:2026 10 17 23:00:00 | Team A vs Team B");
  CHECK(range.title == "Team A vs Team B");
  CHECK(range.start == utc(2026, 10, 18, 0, 25));
  // Placeholder years are not dates.
  CHECK_FALSE(parse("Slot (2099 01 01 00:00:00)").start);
}

TEST_CASE("day/month or month/day, the nearer one") {
  auto dm = parse("Team A vs Team B - 22/10 11:30");
  CHECK(dm.title == "Team A vs Team B");
  CHECK(dm.start == utc(2026, 10, 22, 15, 30));
  CHECK(parse("Team A vs Team B 10/22 11:30").start == utc(2026, 10, 22, 15, 30));
  // 05/10: 5 October is nearer than 10 May.
  CHECK(parse("Team A vs Team B 05/10 11:30").start == utc(2026, 10, 5, 15, 30));
  CHECK_FALSE(parse("Team A vs Team B 05/10 25:30").start);
}

TEST_CASE("AM/PM formats are US Eastern, whatever the provider") {
  auto dotted = parse("Team A vs Team B (10.21 7:30PM ET)");
  CHECK(dotted.title == "Team A vs Team B");
  CHECK(dotted.start == utc(2026, 10, 21, 23, 30));
  auto ordinal = parse("Team A vs Team B Wed 21st Oct 8:00PM ET");
  CHECK(ordinal.title == "Team A vs Team B");
  CHECK(ordinal.start == utc(2026, 10, 22, 0, 0));
  auto monthDay = parse("Team A vs Team B Oct 21 2:30AM ET");
  CHECK(monthDay.title == "Team A vs Team B");
  CHECK(monthDay.start == utc(2026, 10, 21, 6, 30));
  CHECK(parse("Team A vs Team B Oct 22 08:15 PM", std::string("UK")).start ==
        utc(2026, 10, 23, 0, 15));
  CHECK(parse("Team A vs Team B Nov. 2, 12:00PM").start == utc(2026, 11, 2, 17, 0));  // EST
}

TEST_CASE("European day.month in brackets and long dates are UK time") {
  CHECK(parse("Team A vs Team B [23.09 16:04]").start == utc(2026, 9, 23, 15, 4));
  auto longDate = parse("Team A vs Team B Thursday, 22 October 2026 17:45");
  CHECK(longDate.title == "Team A vs Team B");
  CHECK(longDate.start == utc(2026, 10, 22, 16, 45));
}

TEST_CASE("a clock time alone: today in Eastern, guessed") {
  auto clock = parse("NFL: Team A at Team B 8:20pm");
  CHECK(clock.title == "NFL: Team A at Team B");
  CHECK(clock.start == utc(2026, 10, 22, 0, 20));
  CHECK(clock.guessed);
  CHECK(parse("Team A vs Team B 730PM").start == utc(2026, 10, 21, 23, 30));
  CHECK(parse("Team A vs Team B 12PM").start == utc(2026, 10, 21, 16, 0));
  CHECK(parse("Team A vs Team B 12AM").start == utc(2026, 10, 21, 4, 0));
  CHECK(parse("Team A vs Team B 8.30 PM ET").start == utc(2026, 10, 22, 0, 30));
}

TEST_CASE("a clock time with a named day near today") {
  auto named = parse("02:00PM UFC 333 Main Card (OCT 24)");
  CHECK(named.title == "UFC 333 Main Card");
  CHECK(named.start == utc(2026, 10, 24, 18, 0));
  CHECK_FALSE(named.guessed);
  // Too far from today: the day is kept in the title and the time is today's.
  auto far = parse("Team A vs Team B 7PM MAR 28");
  CHECK(far.start == utc(2026, 10, 21, 23, 0));
  CHECK(far.guessed);
  CHECK(far.title.find("MAR 28") != std::string::npos);
}

TEST_CASE("title decorations and tidying") {
  CHECK(parse("LIVE | Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("NEXT | Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("Team A vs Team B | 8K EXCLUSIVE | US: ESPN+ PPV 951").title == "Team A vs Team B");
  CHECK(parse("LIVE EVENT 01 - Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("NFL | 06 - Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("Team A vs Team B :Paramount+  07").title == "Team A vs Team B");
  CHECK(parse("ESPN+: Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("Team A vs Team B (team b)").title == "Team A vs Team B");
  CHECK(parse("Team A vs Team B ( )").title == "Team A vs Team B");
  CHECK(parse("Team A vs Team B (Replay)").title == "Team A vs Team B (Replay)");
  CHECK(parse("Team A | Team B").title == "Team A · Team B");
  CHECK(parse("Team A vs Team B @").title == "Team A vs Team B");
  CHECK(parse("Sat, 10/24 Team A vs Team B").title == "Team A vs Team B");
  CHECK(parse("  –  Team A vs Team B  ,").title == "Team A vs Team B");
  auto pair = parseEvent("Team A vs Team B Oct 21 2:30AM ET", wednesdayNoon());
  CHECK(pair.first == "Team A vs Team B");
  CHECK(pair.second == utc(2026, 10, 21, 6, 30));
}

TEST_CASE("eventZone by provider country") {
  CHECK(eventZone(std::string("uk"))->rule() == time::Zone::Rule::EU);
  CHECK(eventZone(std::string("AU"))->rule() == time::Zone::Rule::Sydney);
  CHECK(eventZone(std::string("MX"))->standardOffset() == -6 * 3600);
  CHECK(eventZone(std::string("CA"))->rule() == time::Zone::Rule::US);
  CHECK_FALSE(eventZone(std::string("US")));
  CHECK_FALSE(eventZone(std::nullopt));
}

TEST_CASE("eventKey: same game, same half hour") {
  time::Seconds t = utc(2026, 10, 21, 23, 30);
  CHECK(eventKey("Team A vs Team B", t) == eventKey("Team A @ Team B", t));
  CHECK(eventKey("Team A vs. Team B (Spanish)", t) == eventKey("team a v team b", t));
  CHECK(eventKey("NFL: Team A at Team B", t) == eventKey("Team A at Team B", t));
  CHECK(eventKey("Team A vs Team B", t) != eventKey("Team A vs Team B", t + 1800));
  CHECK(eventKey("Team A vs Team B", t) == eventKey("Team A vs Team B", t + 600));
  CHECK(eventKey("Team A vs Team B", std::nullopt) == "teamateamb|0");
}

TEST_CASE("leagueFor: a sport named first, then words, fights before soccer") {
  CHECK(leagueFor("ncaaf #", "", "Football: Team A vs Team B") == "College Football");
  CHECK(leagueFor("grp:us| nfl", "", "Football: Team A vs Team B") == "NFL");
  CHECK(leagueFor("x", "", "Football: Team A vs Team B") == "Football");
  CHECK(leagueFor("x", "", "American Football: Team A vs Team B") == "Football");
  CHECK(leagueFor("x", "", "Basketball: Team A vs Team B") == "Basketball");
  CHECK(leagueFor("x", "WNBA", "Basketball: Team A vs Team B") == "WNBA");
  CHECK(leagueFor("x", "", "Basketball: Men's Team A vs Team B") == "College Basketball");
  CHECK(leagueFor("x", "NHL", "Hockey: Team A vs Team B") == "NHL");
  CHECK(leagueFor("x", "", "Field Hockey: Team A vs Team B") == "Field Hockey");
  CHECK(leagueFor("x", "", "Volleyball:  Team A vs Team B") == "Volleyball");
  CHECK(leagueFor("x", "", "Soccer: Team A vs Team B") == "Soccer");
  CHECK(leagueFor("x", "", "Baseball: Team A vs Team B") == "Baseball");
  CHECK(leagueFor("nba #", "", "Team A vs Team B") == "NBA");
  CHECK(leagueFor("x", "MiLB", "Team A vs Team B") == "MiLB");
  CHECK(leagueFor("ppv #", "", "UFC 333: Fighter A vs Fighter B") == "Fighting");
  CHECK(leagueFor("x", "", "UFC Fight Night: Fighter A vs Fighter B") == "Fighting");
  CHECK(leagueFor("x", "", "ufc_ppv main card") == "Fighting");
  CHECK(leagueFor("x", "", "Boxing Day Special") == "Other");
  CHECK(leagueFor("x", "", "Premier League: Team A vs Team B") == "Soccer");
  CHECK(leagueFor("x", "", "Team A FC vs Team B") == "Soccer");
  CHECK(leagueFor("f1 #", "", "Race Day") == "Motorsport");
  CHECK(leagueFor("x", "", "ATP Finals") == "Tennis");
  CHECK(leagueFor("x", "", "PGA Round 1") == "Golf");
  CHECK(leagueFor("x", "", "PDC Darts") == "Darts");
  CHECK(leagueFor("x", "", "IPL Final") == "Cricket");
  CHECK(leagueFor("x", "", "Rugby Test") == "Rugby");
  CHECK(leagueFor("x", "", "Something Else") == "Other");
}

TEST_CASE("LiveEvent length and live window") {
  LiveEvent e;
  e.league = "NFL";
  e.start = utc(2026, 10, 21, 20, 0);
  CHECK(e.expectedLength() == 3 * 3600 + 45 * 60);
  CHECK(e.isLive(utc(2026, 10, 21, 23, 0)));
  CHECK_FALSE(e.isLive(utc(2026, 10, 21, 23, 45)));
  CHECK_FALSE(e.isLive(utc(2026, 10, 21, 19, 59)));
  e.league = "Fighting";
  CHECK(e.expectedLength() == 5 * 3600);
  e.league = "Golf";
  CHECK(e.expectedLength() == 4 * 3600);
  e.league = "MLB";
  CHECK(e.expectedLength() == 3 * 3600 + 15 * 60);
  e.league = "Curling";
  CHECK(e.expectedLength() == 2 * 3600 + 30 * 60);
  e.start.reset();
  CHECK_FALSE(e.isLive(utc(2026, 10, 21, 20, 0)));
}
