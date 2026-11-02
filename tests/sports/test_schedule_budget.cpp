// The schedule's request budget and the first open's wait (Build 125), on a
// pretend network: every answer comes after kLatencyMs, requests spaced as
// the schedule says. Three viewers: a light one (NFL, NBA, MLB; the
// provider also carries racing, tennis, golf and cricket), the owner's
// picks (US leagues, college, Premier League and Champions League, tennis,
// F1, cricket, boxing; small-college games on the channels; every other
// calendar's competition seen), and every league and sport. Day 1 is a
// cold start, day 2 the next day with day 1's copy saved.
//
// MARIGOLD_BUDGET=1 prints the numbers.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/sports/calendars.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/schedule.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::schedule;
using sportsdata::utc;

namespace {

constexpr int kLatencyMs = 500;

struct Run {
  int requests = 0;
  int scoreboard = 0;
  int ncaa = 0;
  int calendar = 0;
  int today = 0;          // requests until today's part is in
  double todayMs = 0;     // when it was in
  double totalMs = 0;
  double calendarGapMs = 1e9;  // the shortest gap between two calendar requests
};

std::string twoDigits(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }

std::string stamp(Seconds t) {
  time::Civil c = time::civilFromSeconds(t);
  return std::to_string(c.year) + "-" + twoDigits(c.month) + "-" + twoDigits(c.day) + "T" + twoDigits(c.hour) + ":" +
         twoDigits(c.minute) + ":00";
}

// A calendar answer: its next event two days ahead in round 5 (so the
// round is fetched), the round's last event three days ahead (so the next
// round is too), nothing after that.
std::string calendarBody(const std::string& url, Seconds now) {
  auto one = [&](Seconds at) {
    return R"({"events":[{"idEvent":")" + std::to_string(at) +
           R"(","strEvent":"Harbor vs Valley","strHomeTeam":"Harbor","strAwayTeam":"Valley","strSeason":"2026","intRound":"5","strTimestamp":")" +
           stamp(at) + R"("}]})";
  };
  if (url.find("eventsnextleague") != std::string::npos) return one(now + 2 * 86'400);
  if (url.find("&r=5&") != std::string::npos) return one(now + 3 * 86'400);
  return R"({"events":null})";
}

Run simulate(Schedule& s, const Context& ctx) {
  Run r;
  REQUIRE(s.refreshIfNeeded(ctx));
  double t = 0, lastCalendar = -1;
  bool todayIn = false;
  while (auto p = s.nextRequest()) {
    t += p->waitMs;
    const std::string& url = p->request.url;
    bool calendar = url.find("thesportsdb.com") != std::string::npos;
    bool ncaa = url.find("ncaa.com") != std::string::npos;
    if (calendar) {
      if (lastCalendar >= 0) r.calendarGapMs = std::min(r.calendarGapMs, t - lastCalendar);
      lastCalendar = t;
    }
    t += kLatencyMs;
    r.requests += 1;
    r.calendar += calendar;
    r.ncaa += ncaa;
    r.scoreboard += !calendar && !ncaa;
    if (calendar) s.onResponse(200, calendarBody(url, ctx.now));
    else if (ncaa) s.onResponse(404, "");
    else s.onResponse(200, R"({"events":[]})");
    if (!todayIn && !s.fetching()) {
      todayIn = true;
      r.today = r.requests;
      r.todayMs = t;
    }
  }
  if (!todayIn) {
    r.today = r.requests;
    r.todayMs = t;
  }
  r.totalMs = t;
  REQUIRE(s.takeFinished());
  return r;
}

void print(const char* who, int day, const Run& r) {
  if (!std::getenv("MARIGOLD_BUDGET")) return;
  std::printf("%-10s day %d: %3d requests (scoreboard %3d, NCAA %2d, calendars %2d); today's part %3d requests, %5.1f s; "
              "all %5.1f s\n",
              who, day, r.requests, r.scoreboard, r.ncaa, r.calendar, r.today, r.todayMs / 1000, r.totalMs / 1000);
}

Context base(Seconds now) {
  Context ctx;
  ctx.now = now;
  ctx.zone = time::Zone::usEastern();
  ctx.prefs.configured = true;
  return ctx;
}

void seeAll(SportFeedsSeen& seen, Seconds now) {
#ifndef MARIGOLD_BASE
  for (const calendars::Calendar& c : calendars::calendars()) seen.calendars[c.key] = now - 3600;
#else
  (void)seen;
  (void)now;
#endif
}

Context light(Seconds now) {
  Context ctx = base(now);
  ctx.prefs.leagues = {"NFL", "NBA", "MLB"};
  ctx.seen.racing = ctx.seen.tennis = ctx.seen.golf = ctx.seen.cricket = now - 86'400;
  return ctx;
}

Context owner(Seconds now) {
  Context ctx = base(now);
  ctx.prefs.leagues = {"NFL", "NCAAF", "NBA", "NCAAM", "NCAAW", "NHL", "MLB", "EPL", "UCL",
                       "S:Tennis", "S:Motorsport", "S:Cricket", "S:Fighting"};
  ctx.seen.racing = ctx.seen.tennis = ctx.seen.cricket = ctx.seen.golf = now - 86'400;
  for (const char* g : {"College Basketball", "College Football", "Soccer", "Hockey"}) {
    ctx.seen.groups[g] = now - 86'400;
    ctx.seen.colleges[g] = now - 86'400;
  }
  seeAll(ctx.seen, now);
  return ctx;
}

Context everything(Seconds now) {
  Context ctx = base(now);
  for (const League& l : leagues()) ctx.prefs.leagues.push_back(l.shortName);
  for (const OtherSport& s : otherSports()) ctx.prefs.leagues.push_back(s.id);
  ctx.seen.racing = ctx.seen.tennis = ctx.seen.cricket = ctx.seen.golf = ctx.seen.nascar = now - 86'400;
  for (const char* g : {"College Basketball", "College Football", "Soccer", "Hockey", "Volleyball", "Field Hockey",
                        "Basketball", "Football", "Lacrosse", "Softball", "Rugby", "WNBA"}) {
    ctx.seen.groups[g] = now - 86'400;
    ctx.seen.colleges[g] = now - 86'400;
  }
  seeAll(ctx.seen, now);
  return ctx;
}

}  // namespace

TEST_CASE("budget: today's part first, the rest of the week after, the calendars within their cap") {
  // A Monday afternoon in New York.
  Seconds monday = utc(2026, 11, 2, 18, 0);
  struct Viewer {
    const char* name;
    Context (*context)(Seconds);
  };
  for (const Viewer& v : {Viewer{"light", light}, Viewer{"owner", owner}, Viewer{"everything", everything}}) {
    CAPTURE(v.name);
    Schedule s(Saved(), "test");
    Run day1 = simulate(s, v.context(monday));
    print(v.name, 1, day1);
    Schedule next(s.saved(), "test");
    Run day2 = simulate(next, v.context(monday + 86'400));
    print(v.name, 2, day2);
    for (const Run& r : {day1, day2}) {
      CHECK(r.today <= r.requests);
#ifndef MARIGOLD_BASE
      CHECK(r.calendar <= calendars::kDailyCap);
      // Today's part never has the calendars or the days after tomorrow.
      CHECK(r.todayMs < r.totalMs + 1);
      if (r.calendar > 1) CHECK(r.calendarGapMs >= calendars::kRequestSpacingMs);
#endif
    }
#ifndef MARIGOLD_BASE
    // The second day the calendars' rounds are saved: fewer requests.
    CHECK(day2.calendar <= day1.calendar);
    if (std::string(v.name) != "light") {
      CHECK(day1.calendar > 0);
      // Today's part is a fraction of the whole.
      CHECK(day1.today * 2 < day1.requests);
    } else {
      CHECK(day1.calendar == 0);  // no calendar seen
    }
#endif
  }
}
