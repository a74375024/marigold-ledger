// Build 177, Skip break: a game's breaks from its play-by-play
// (sports::plays). The thresholds per sport (timeouts, period ends, the
// gap, the trim, the shortest break, halftime), reading the summary's two
// shapes (football's drives, the others' plays) from small handmade bodies
// in the documented shape (no answer from the real source is in the repo),
// and the fetches' politeness. No network.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/sports/leagues.hpp"
#include "core/sports/plays.hpp"
#include "core/util/time.hpp"

using namespace marigold;
using namespace marigold::sports::plays;

namespace {

constexpr Seconds kT0 = 1'794'000'000;  // some afternoon

Play play(Seconds at, Kind kind = Kind::Plain, int period = 1) { return {kT0 + at, period, kind}; }

Break closed(Seconds from, Seconds to, bool halftime = false) { return {kT0 + from, kT0 + to, halftime, false}; }

const sports::League& league(const char* shortName) {
  const sports::League* l = sports::leagueByShort(shortName);
  REQUIRE(l);
  return *l;
}

// Football, by drive (T0 18:00:00Z); one play with no time, the half's
// last play again in the drive under way.
const char* kFootballBody = R"({
  "header": {"id": "401"},
  "drives": {
    "previous": [
      {"id": "1", "description": "6 plays", "plays": [
        {"id": "10", "type": {"id": "53", "text": "Kickoff"}, "text": "Kick 65 yards", "period": {"number": 1}, "wallclock": "2026-11-15T18:00:00Z"},
        {"id": "11", "type": {"id": "5", "text": "Rush"}, "text": "Run for 4", "period": {"number": 1}, "wallclock": "2026-11-15T18:00:40Z"},
        {"id": "12", "type": {"id": "21", "text": "Timeout"}, "text": "Timeout #1 by HAR at 12:10.", "period": {"number": 1}, "wallclock": "2026-11-15T18:01:20Z"},
        {"id": "13", "type": {"id": "24", "text": "Pass Reception"}, "text": "Pass for 12", "period": {"number": 1}, "wallclock": "2026-11-15T18:03:30Z"},
        {"id": "14", "type": {"id": "67", "text": "Passing Touchdown"}, "text": "Pass for 20, TOUCHDOWN", "scoringPlay": true, "period": {"number": 1}, "wallclock": "2026-11-15T18:04:10Z"},
        {"id": "15", "type": {"id": "61", "text": "Extra Point Good"}, "text": "Kick is good", "scoringPlay": true, "period": {"number": 1}, "wallclock": "2026-11-15T18:04:40Z"}
      ]},
      {"id": "2", "plays": [
        {"id": "20", "type": {"text": "Kickoff"}, "text": "Kick", "period": {"number": 1}, "wallclock": "2026-11-15T18:07:20Z"},
        {"id": "21", "type": {"text": "Rush"}, "text": "Run", "period": {"number": 1}, "wallclock": "2026-11-15T18:08:00Z"},
        {"id": "22", "type": {"text": "Rush"}, "text": "Run", "period": {"number": 1}, "wallclock": "2026-11-15T18:10:40Z"},
        {"id": "23", "type": {"id": "2", "text": "End Period"}, "text": "END QUARTER 1", "period": {"number": 1}, "wallclock": "2026-11-15T18:11:20Z"},
        {"id": "24", "type": {"text": "Pass Incompletion"}, "text": "Incomplete", "period": {"number": 2}, "wallclock": "2026-11-15T18:13:40Z"},
        {"id": "25", "type": {"text": "Pass Reception"}, "text": "Pass for 5", "period": {"number": 2}, "wallclock": "2026-11-15T18:14:20Z"},
        {"id": "26", "type": {"id": "75", "text": "Two-minute warning"}, "text": "Two-Minute Warning", "period": {"number": 2}, "wallclock": "2026-11-15T18:15:00Z"},
        {"id": "27", "type": {"id": "21", "text": "Timeout"}, "text": "Timeout #2 by VAL at 01:52.", "period": {"number": 2}, "wallclock": "2026-11-15T18:16:40Z"},
        {"id": "28", "type": {"text": "Rush"}, "text": "Run", "period": {"number": 2}, "wallclock": "2026-11-15T18:18:20Z"},
        {"id": "29", "type": {"id": "2", "text": "End Period"}, "text": "END QUARTER 2", "period": {"number": 2}, "wallclock": "2026-11-15T18:19:00Z"},
        {"id": "2a", "type": {"id": "65", "text": "End of Half"}, "text": "END OF HALF", "period": {"number": 2}, "wallclock": "2026-11-15T18:19:05Z"}
      ]}
    ],
    "current": {"id": "3", "plays": [
      {"id": "2a", "type": {"id": "65", "text": "End of Half"}, "text": "END OF HALF", "period": {"number": 2}, "wallclock": "2026-11-15T18:19:05Z"},
      {"id": "30", "type": {"text": "Kickoff"}, "text": "Kick", "period": {"number": 3}, "wallclock": "2026-11-15T18:39:05Z"},
      {"id": "31", "type": {"text": "Rush"}, "text": "Run", "period": {"number": 3}, "wallclock": "2026-11-15T18:39:40Z"},
      {"id": "32", "type": {"text": "Rush"}, "text": "Run", "period": {"number": 3}}
    ]}
  },
  "boxscore": {"teams": []}
})";

// Basketball's plays at the top (T0 00:10:00Z); one with no time.
const char* kBasketballBody = R"({
  "header": {"id": "402"},
  "plays": [
    {"id": "1", "type": {"id": "615", "text": "Jumpball"}, "text": "Jump ball", "period": {"number": 1}, "clock": {"displayValue": "12:00"}, "wallclock": "2026-11-15T00:10:00Z"},
    {"id": "2", "type": {"text": "Driving Layup Shot"}, "text": "makes layup", "scoringPlay": true, "period": {"number": 1}, "wallclock": "2026-11-15T00:10:40Z"},
    {"id": "3", "type": {"text": "Full Timeout"}, "text": "Harbor Full timeout", "period": {"number": 1}, "wallclock": "2026-11-15T00:11:10Z"},
    {"id": "4", "type": {"text": "Jump Shot"}, "text": "misses jumper", "period": {"number": 1}, "wallclock": "2026-11-15T00:13:40Z"},
    {"id": "5", "type": {"id": "412", "text": "End Period"}, "text": "End of the 1st Quarter", "period": {"number": 1}, "wallclock": "2026-11-15T00:14:20Z"},
    {"id": "6", "type": {"text": "Jump Shot"}, "text": "makes jumper", "scoringPlay": true, "period": {"number": 2}, "wallclock": "2026-11-15T00:16:40Z"},
    {"id": "7", "type": {"text": "Free Throw - 1 of 1"}, "text": "makes free throw", "scoringPlay": true, "period": {"number": 2}, "wallclock": "2026-11-15T00:17:10Z"},
    {"id": "8", "type": {"id": "412", "text": "End Period"}, "text": "End of the 2nd Quarter", "period": {"number": 2}, "wallclock": "2026-11-15T00:17:50Z"},
    {"id": "9", "type": {"text": "Jump Shot"}, "text": "misses", "period": {"number": 3}, "wallclock": "2026-11-15T00:31:10Z"},
    {"id": "10", "type": {"text": "Defensive Rebound"}, "text": "rebound", "period": {"number": 3}, "wallclock": "2026-11-15T00:31:40Z"},
    {"id": "11", "type": {"text": "Jump Shot"}, "text": "misses", "period": {"number": 3}, "wallclock": "2026-11-15T00:32:40Z"},
    {"id": "12", "type": {"text": "Jump Shot"}, "text": "misses", "period": {"number": 3}, "wallclock": "2026-11-15T00:34:00Z"},
    {"id": "13", "type": {"text": "Substitution"}, "text": "enters the game", "period": {"number": 3}}
  ]
})";

Seconds at(int h, int m, int s) { return time::secondsFromCivil({2026, 11, 15, h, m, s}); }

}  // namespace

TEST_CASE("plays 177: the rules per sport") {
  CHECK(rulesFor(league("NFL"))->halftimeAfter == kFootball.halftimeAfter);
  CHECK(rulesFor(league("NCAAF"))->gap == kFootball.gap);
  CHECK(rulesFor(league("NBA"))->halftimeAfter == 2);
  CHECK(rulesFor(league("NCAAM"))->halftimeAfter == 1);
  CHECK(rulesFor(league("NHL"))->halftimeAfter == 0);
  // The spec's thresholds, for each sport.
  for (const Rules& r : {kFootball, kBasketball, kCollegeHalves, kHockey}) {
    CHECK(r.gap == 75);
    CHECK(r.anyGap == 150);
    CHECK(r.trim == 10);
    CHECK(r.shortest == 45);
  }
  // No play-by-play read: baseball, soccer.
  CHECK_FALSE(rulesFor(league("MLB")));
  CHECK_FALSE(rulesFor(league("EPL")));
  CHECK(byDrive(league("NFL")));
  CHECK_FALSE(byDrive(league("NBA")));
}

TEST_CASE("plays 177: what a play says") {
  CHECK(kindOf("Timeout", "Timeout #1 by HAR at 12:10.", false) == Kind::Timeout);
  CHECK(kindOf("Full Timeout", "", false) == Kind::Timeout);
  CHECK(kindOf("Official Timeout", "", false) == Kind::Timeout);
  CHECK(kindOf("End Period", "END QUARTER 1", false) == Kind::PeriodEnd);
  CHECK(kindOf("", "End of the 3rd Quarter", false) == Kind::PeriodEnd);
  CHECK(kindOf("End of Half", "", false) == Kind::HalfEnd);
  CHECK(kindOf("Two-minute warning", "", false) == Kind::Warning);
  CHECK(kindOf("Pass Reception", "The Replay Official reviewed the pass", false) == Kind::Stoppage);
  CHECK(kindOf("Rush", "Run for 2. J.Harbor was injured during the play.", false) == Kind::Stoppage);
  CHECK(kindOf("Stoppage", "Icing", false) == Kind::Stoppage);
  CHECK(kindOf("Passing Touchdown", "Pass for 20", true) == Kind::Score);
  CHECK(kindOf("End Game", "", false) == Kind::GameEnd);
  CHECK(kindOf("Rush", "Run to the end of the line", false) == Kind::Plain);
}

TEST_CASE("plays 177: a timeout and 75 s is a break; 74 s isn't") {
  std::vector<Break> b = detect({play(0, Kind::Timeout), play(74)}, kFootball);
  CHECK(b.empty());
  b = detect({play(0, Kind::Timeout), play(75)}, kFootball);
  REQUIRE(b.size() == 1);
  // Trimmed 10 s off each end: 55 s.
  CHECK(b[0] == closed(10, 65));
  CHECK(b[0].length() == 55);
  // The end of a period, a score, the warning, an injury or a review: the
  // same.
  for (Kind k : {Kind::PeriodEnd, Kind::Score, Kind::Warning, Kind::Stoppage, Kind::HalfEnd}) {
    CHECK(detect({play(0, k), play(80)}, kBasketball).size() == 1);
    CHECK(detect({play(0, k), play(70)}, kBasketball).empty());
  }
}

TEST_CASE("plays 177: any gap of 150 s is a break") {
  CHECK(detect({play(0), play(149)}, kHockey).empty());
  std::vector<Break> b = detect({play(0), play(150)}, kHockey);
  REQUIRE(b.size() == 1);
  CHECK(b[0] == closed(10, 140));
  // A game's end begins nothing.
  CHECK(detect({play(0, Kind::GameEnd), play(100)}, kHockey).empty());
}

TEST_CASE("plays 177: a break too short once trimmed is left out") {
  Rules r = kFootball;
  r.gap = 60;
  CHECK(detect({play(0, Kind::Timeout), play(64)}, r).empty());  // 44 s
  CHECK(detect({play(0, Kind::Timeout), play(65)}, r).size() == 1);  // 45 s
}

TEST_CASE("plays 177: two breaks with one play between are one; plays in any order") {
  // A second timeout 90 s after the first, the next play 90 s after it.
  std::vector<Break> b = detect({play(180), play(0, Kind::Timeout), play(90, Kind::Timeout)}, kBasketball);
  REQUIRE(b.size() == 1);
  CHECK(b[0] == closed(10, 170));
  // Two plays between: two breaks.
  b = detect({play(0, Kind::Timeout), play(90), play(100, Kind::Timeout), play(200)}, kBasketball);
  CHECK(b.size() == 2);
}

TEST_CASE("plays 177: halftime") {
  // Football and basketball in quarters: the end of the second.
  CHECK(detect({play(0, Kind::PeriodEnd, 2), play(900, Kind::Plain, 3)}, kFootball)[0].halftime);
  CHECK_FALSE(detect({play(0, Kind::PeriodEnd, 1), play(200, Kind::Plain, 2)}, kFootball)[0].halftime);
  CHECK(detect({play(0, Kind::PeriodEnd, 2), play(900, Kind::Plain, 3)}, kBasketball)[0].halftime);
  // Men's college basketball, in halves: the end of the first.
  CHECK(detect({play(0, Kind::PeriodEnd, 1), play(900, Kind::Plain, 2)}, kCollegeHalves)[0].halftime);
  CHECK_FALSE(detect({play(0, Kind::PeriodEnd, 2), play(300, Kind::Plain, 3)}, kCollegeHalves)[0].halftime);
  // Hockey: intermissions, no halftime.
  CHECK_FALSE(detect({play(0, Kind::PeriodEnd, 1), play(1000, Kind::Plain, 2)}, kHockey)[0].halftime);
  // Where the data says so (the end of a half), in any sport.
  CHECK(detect({play(0, Kind::HalfEnd, 1), play(900, Kind::Plain, 2)}, kHockey)[0].halftime);
}

TEST_CASE("plays 177: a game still on can end in a break that runs to live") {
  // Its last play a timeout 80 s before the plays were read: open.
  std::vector<Break> b = detect({play(0), play(30, Kind::Timeout)}, kFootball, true, kT0 + 110);
  REQUIRE(b.size() == 1);
  CHECK(b[0].open);
  CHECK(b[0].from == kT0 + 40);
  // Only 30 s: not (yet) a break.
  CHECK(detect({play(0), play(30, Kind::Timeout)}, kFootball, true, kT0 + 60).empty());
  // A plain last play: once the any-gap has passed.
  CHECK(detect({play(0), play(30)}, kFootball, true, kT0 + 170).empty());
  CHECK(detect({play(0), play(30)}, kFootball, true, kT0 + 180).size() == 1);
  // Over: nothing open.
  CHECK(detect({play(0), play(30, Kind::Timeout)}, kFootball, false, kT0 + 110).empty());
}

TEST_CASE("plays 177: football's drives") {
  Parsed p = parse(kFootballBody, true);
  REQUIRE(p.ok);
  // 20 plays (the half's last one twice: once), one with no time.
  CHECK(p.seen == 20);
  CHECK(p.plays.size() == 19);
  std::vector<Break> b = detect(p.plays, kFootball);
  Seconds t0 = at(18, 0, 0);
  auto br = [&](Seconds from, Seconds to, bool half = false) { return Break{t0 + from, t0 + to, half, false}; };
  REQUIRE(b.size() == 6);
  CHECK(b[0] == br(90, 200));     // after the timeout
  CHECK(b[1] == br(290, 430));    // after the extra point (a score)
  CHECK(b[2] == br(490, 630));    // a gap of 160 s, no timeout
  CHECK(b[3] == br(690, 810));    // the end of the first quarter
  CHECK(b[4] == br(910, 1090));   // the two-minute warning, then a timeout: one break
  CHECK(b[5] == br(1155, 2335, true));  // halftime
  // Read with the other shape first: the same.
  Parsed q = parse(kFootballBody, false);
  CHECK(q.plays.size() == 19);
}

TEST_CASE("plays 177: basketball's plays") {
  Parsed p = parse(kBasketballBody, false);
  REQUIRE(p.ok);
  CHECK(p.seen == 13);
  CHECK(p.plays.size() == 12);
  std::vector<Break> b = detect(p.plays, kBasketball);
  Seconds t0 = at(0, 10, 0);
  REQUIRE(b.size() == 3);
  CHECK(b[0] == Break{t0 + 80, t0 + 210, false, false});    // a full timeout
  CHECK(b[1] == Break{t0 + 270, t0 + 390, false, false});   // the end of the first quarter
  CHECK(b[2] == Break{t0 + 480, t0 + 1260, true, false});   // halftime
  // Men's college rules on the same plays: the end of the second period
  // isn't halftime there.
  CHECK_FALSE(detect(p.plays, kCollegeHalves)[2].halftime);
}

TEST_CASE("plays 177: a body without play-by-play, or without times") {
  CHECK_FALSE(parse("{\"header\":{}}", false).ok);
  CHECK_FALSE(parse("not json", true).ok);
  Parsed p = parse(R"({"plays":[{"id":"1","type":{"text":"Timeout"},"period":{"number":1}},
                                 {"id":"2","type":{"text":"Rush"},"period":{"number":1}}]})",
                   false);
  CHECK(p.ok);
  CHECK(p.seen == 2);
  CHECK(p.plays.empty());
  CHECK(detect(p.plays, kFootball).empty());
}

TEST_CASE("plays 177: the summary request") {
  sources::Request r = summaryRequest(league("NFL"), "401");
  CHECK(r.url == "https://site.api.espn.com/apis/site/v2/sports/football/nfl/summary?event=401");
  CHECK(r.headers.empty());
}

TEST_CASE("plays 177: fetched once a final, while a replay asks") {
  Fetch f;
  Seconds now = kT0;
  CHECK_FALSE(f.next(now));  // nobody asked
  f.want({"sev|NFL|401", "NFL", "401", false}, now);
  auto ask = f.next(now);
  REQUIRE(ask);
  CHECK(ask->request.url == "https://site.api.espn.com/apis/site/v2/sports/football/nfl/summary?event=401");
  CHECK_FALSE(ask->live);
  CHECK_FALSE(f.next(now));  // one out at a time
  CHECK(f.done(ask->eventId, 200, std::vector<Break>{closed(0, 100)}, now + 2, 0, time::Zone::utc()));
  REQUIRE(static_cast<bool>(f.breaks("sev|NFL|401")));
  CHECK(f.breaks("sev|NFL|401")->list.size() == 1);
  CHECK(f.breaks("sev|NFL|401")->league == "NFL");
  // A final: never again.
  for (Seconds s = 0; s < 3600; s += 10) {
    f.want({"sev|NFL|401", "NFL", "401", false}, now + s);
    CHECK_FALSE(f.next(now + s));
  }
  CHECK(f.sentToday(now + 3600) == 1);
}

TEST_CASE("plays 177: a live game's again every 2.5 min while its replay asks") {
  Fetch f;
  Seconds now = kT0;
  f.want({"g", "NBA", "402", true}, now);
  auto ask = f.next(now);
  REQUIRE(ask);
  CHECK(ask->live);
  f.done("g", 200, std::vector<Break>{}, now, 0, time::Zone::utc());
  REQUIRE(static_cast<bool>(f.breaks("g")));
  CHECK(f.breaks("g")->list.empty());
  f.want({"g", "NBA", "402", true}, now + Fetch::kRefresh - 1);
  CHECK_FALSE(f.next(now + Fetch::kRefresh - 1));
  f.want({"g", "NBA", "402", true}, now + Fetch::kRefresh);
  CHECK(f.next(now + Fetch::kRefresh));
  f.done("g", 200, std::vector<Break>{closed(0, 60)}, now + Fetch::kRefresh, 0, time::Zone::utc());
  // The replay stopped asking (caught up, another channel): nothing more.
  CHECK_FALSE(f.next(now + 2 * Fetch::kRefresh + Fetch::kWantedFor + 1));
  // Read while live, now final: once more.
  f.want({"g", "NBA", "402", false}, now + 3 * Fetch::kRefresh);
  CHECK(f.next(now + 3 * Fetch::kRefresh));
  f.done("g", 200, std::vector<Break>{closed(0, 60)}, now + 3 * Fetch::kRefresh, 0, time::Zone::utc());
  f.want({"g", "NBA", "402", false}, now + 5 * Fetch::kRefresh);
  CHECK_FALSE(f.next(now + 5 * Fetch::kRefresh));
}

TEST_CASE("plays 177: failures back off (2, then 5 min)") {
  Fetch f;
  Seconds now = kT0;
  auto ask = [&](Seconds t) {
    f.want({"g", "NHL", "403", false}, t);
    return f.next(t);
  };
  REQUIRE(ask(now));
  f.done("g", 0, std::nullopt, now, 0, time::Zone::utc());  // no answer
  CHECK_FALSE(static_cast<bool>(f.breaks("g")));
  CHECK_FALSE(ask(now + 119));
  REQUIRE(ask(now + 120));
  f.done("g", 500, std::nullopt, now + 120, 0, time::Zone::utc());
  CHECK_FALSE(ask(now + 120 + 299));
  REQUIRE(ask(now + 120 + 300));
  // A 200 that couldn't be read is a failure too.
  f.done("g", 200, std::nullopt, now + 420, 0, time::Zone::utc());
  CHECK_FALSE(static_cast<bool>(f.breaks("g")));
  CHECK_FALSE(ask(now + 420 + 899));
  CHECK(ask(now + 420 + 900));
}

TEST_CASE("plays 177: a refusal leaves the site alone for the rest of the day") {
  Fetch f;
  Seconds noon = time::secondsFromCivil({2026, 11, 15, 12, 0, 0});
  f.want({"g", "NFL", "401", true}, noon);
  REQUIRE(f.next(noon));
  f.done("g", 429, std::nullopt, noon, 0, time::Zone::utc());
  CHECK(f.refusedUntil().at("espn.com") == time::secondsFromCivil({2026, 11, 16, 0, 0, 0}));
  // Another game on the same site: not asked either.
  for (Seconds t = noon; t < noon + 11 * 3600; t += 600) {
    f.want({"h", "NBA", "402", true}, t);
    CHECK_FALSE(f.next(t));
  }
  Seconds tomorrow = time::secondsFromCivil({2026, 11, 16, 0, 1, 0});
  f.want({"h", "NBA", "402", true}, tomorrow);
  CHECK(f.next(tomorrow));
  // A refusal of the score requests holds here too (Store::pumpBreaks).
  Fetch g;
  g.refuse("espn.com", noon + 3600);
  g.want({"g", "NFL", "401", true}, noon);
  CHECK_FALSE(g.next(noon));
  g.want({"g", "NFL", "401", true}, noon + 3600);
  CHECK(g.next(noon + 3600));
}

TEST_CASE("plays 177: requests spaced, capped per day") {
  Fetch f;
  Seconds now = kT0;
  // Game after game: 15 s apart at least.
  f.want({"a", "NFL", "1", false}, now);
  REQUIRE(f.next(now));
  f.done("a", 200, std::vector<Break>{}, now, 0, time::Zone::utc());
  f.want({"b", "NFL", "2", false}, now + 5);
  CHECK_FALSE(f.next(now + 5));
  CHECK(f.next(now + Fetch::kSpacing));
  f.done("b", 200, std::vector<Break>{}, now + Fetch::kSpacing, 0, time::Zone::utc());
  // A day's worth: kDailyCap at most.
  int sent = 2;
  Seconds t = now + 30;
  for (int i = 0; i < 400; ++i, t += 20) {
    std::string id = "x" + std::to_string(i);
    f.want({id, "NBA", id, false}, t);
    if (auto a = f.next(t)) {
      ++sent;
      f.done(id, 200, std::vector<Break>{}, t, 0, time::Zone::utc());
    }
  }
  CHECK(sent == Fetch::kDailyCap);
  // A day later, again.
  f.want({"late", "NBA", "9", false}, now + 86'400 + 60);
  CHECK(f.next(now + 86'400 + 60));
}

TEST_CASE("plays 177: a request whose answer never comes doesn't hold the next one back for good") {
  Fetch f;
  f.want({"a", "NFL", "1", true}, kT0);
  REQUIRE(f.next(kT0));
  f.want({"a", "NFL", "1", true}, kT0 + 60);
  CHECK_FALSE(f.next(kT0 + 60));
  f.want({"a", "NFL", "1", true}, kT0 + Fetch::kLostAfter + 1);
  CHECK(f.next(kT0 + Fetch::kLostAfter + 1));
}

TEST_CASE("plays 177: a few games kept") {
  Fetch f;
  for (int i = 0; i < 10; ++i) {
    Seconds t = kT0 + i * 20;
    std::string id = "g" + std::to_string(i);
    f.want({id, "NFL", id, false}, t);
    REQUIRE(f.next(t));
    f.done(id, 200, std::vector<Break>{closed(0, 60)}, t, 0, time::Zone::utc());
  }
  CHECK_FALSE(static_cast<bool>(f.breaks("g0")));
  CHECK(static_cast<bool>(f.breaks("g9")));
  CHECK(static_cast<bool>(f.breaks("g4")));
}
