// Today first (Build 125): every league's requests for today and tomorrow
// go first and their games are saved at once (one batch: the board is made
// from them while the rest of the week comes in); the days after tomorrow
// keep yesterday's copy meanwhile, and are asked for on their own when the
// app closed before they came. The other calendars come after today's part,
// one request after every two of the scoreboard's, over two seconds apart,
// and their games are published with the schedule's.
#include <set>

#include "core/sports/calendars.hpp"
#include "core/sports/schedule.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::schedule;
using sportsdata::utc;

namespace {

std::string oneGame(const std::string& id, const std::string& date) {
  return R"({"events":[{"id":")" + id + R"(","date":")" + date +
         R"(","competitions":[{"competitors":[
          {"homeAway":"home","team":{"id":"1","displayName":"Harbor City Gulls","abbreviation":"HCG"}},
          {"homeAway":"away","team":{"id":"2","displayName":"Team A","abbreviation":"TMA"}}]}]}]})";
}

Context picks(Seconds now, std::vector<std::string> leagues) {
  Context ctx;
  ctx.prefs.configured = true;
  ctx.prefs.leagues = std::move(leagues);
  ctx.now = now;
  ctx.zone = time::Zone::usEastern();
  return ctx;
}

SportsGame saved(const std::string& league, const std::string& id, Seconds start) {
  return sportsdata::game(league, league == "NCAAM" ? "College Basketball" : league, id,
                          sportsdata::team("1", "Harbor"), sportsdata::team("2", "Valley"), start);
}

std::set<std::string> ids(const std::vector<SportsGame>& games) {
  std::set<std::string> out;
  for (const SportsGame& g : games) out.insert(g.id);
  return out;
}

}  // namespace

TEST_CASE("today first: today's and tomorrow's requests, saved at once; the other days after") {
  // Monday noon in New York.
  Seconds now = utc(2026, 10, 26, 16, 0);
  Saved yesterday;
  yesterday.games = {saved("NCAAM", "today", now + 2 * 3600), saved("NCAAM", "thursday", now + 3 * 86'400),
                     saved("NCAAM", "lastnight", now - 16 * 3600)};
  yesterday.fetchedAt = now - 86'400;
  yesterday.schema = kSchema;
  yesterday.schools = std::vector<SportsTeam>{sportsdata::team("50", "Harbor State")};
  yesterday.schoolsFetchedAt = now - 86'400;
  Schedule schedule(yesterday);
  Context ctx = picks(now, {"NFL", "NCAAM"});
  REQUIRE(schedule.refreshIfNeeded(ctx));
  CHECK(schedule.fetching());
  CHECK(schedule.busy());
  // Today's part: the NFL's month of today, the college days of today and
  // tomorrow.
  std::vector<std::string> today;
  while (schedule.fetching()) {
    auto p = schedule.nextRequest();
    REQUIRE(p);
    if (!schedule.fetching()) {
      // That was the first of the rest (today's part just saved).
      CHECK_FALSE(p->today);
      // November's listing: no game of it today or tomorrow.
      CHECK(p->request.url.find("dates=202611") != std::string::npos);
      schedule.onResponse(200, R"({"events":[]})");
      break;
    }
    CHECK(p->today);
    today.push_back(p->query);
    if (p->league.shortName == "NFL") schedule.onResponse(200, oneGame("7", "2026-10-27T00:15Z"));
    else schedule.onResponse(200, R"({"events":[]})");
  }
  CHECK(today == std::vector<std::string>{"dates=202610", "dates=20261026", "dates=20261027"});
  // Saved at once, once.
  CHECK(schedule.takeBatch());
  CHECK_FALSE(schedule.takeBatch());
  CHECK(schedule.takeDirty());
  CHECK(schedule.saved().fetchedAt == now);
  CHECK(schedule.saved().restPending);
  // Today's college game the answer didn't list is gone; Thursday's stays
  // until Thursday is asked for; last night's final stays.
  CHECK(ids(schedule.saved().games) == std::set<std::string>{"NFL|7", "NCAAM|thursday", "NCAAM|lastnight"});
  CHECK(ids(schedule.games(ctx)) == std::set<std::string>{"NFL|7", "NCAAM|thursday", "NCAAM|lastnight"});
  // The rest of the week.
  int rest = 1;
  while (auto p = schedule.nextRequest()) {
    CHECK_FALSE(p->today);
    CHECK(p->waitMs == kRequestSpacingMs);
    rest += 1;
    schedule.onResponse(200, R"({"events":[]})");
  }
  CHECK(rest == 4);  // November, Wednesday to Friday
  auto done = schedule.takeFinished();
  REQUIRE(done);
  CHECK(done->succeeded);
  CHECK_FALSE(schedule.busy());
  CHECK_FALSE(schedule.saved().restPending);
  CHECK(ids(schedule.saved().games) == std::set<std::string>{"NFL|7", "NCAAM|lastnight"});
  CHECK(schedule.saved().requestCount == 7);
  CHECK(schedule.saved().fetchedAt == now);
}

TEST_CASE("today first: the days after tomorrow on their own when the app closed before them") {
  Seconds now = utc(2026, 10, 26, 16, 0);
  Saved halfway;
  halfway.games = {saved("NCAAM", "tonight", now + 3 * 3600), saved("NCAAM", "thursday", now + 3 * 86'400),
                   saved("NFL", "lastweek", now - 6 * 86'400), saved("NFL", "nextweek", now + 7 * 86'400)};
  halfway.fetchedAt = now - 600;
  halfway.schema = kSchema;
  halfway.leagueSet = std::vector<std::string>{"NCAAM", "NFL"};
  halfway.restPending = true;
  halfway.schools = std::vector<SportsTeam>{sportsdata::team("50", "Harbor State")};
  halfway.schoolsFetchedAt = now - 600;
  // Round trip: the flag is saved.
  auto back = savedFromJson(toJson(halfway));
  REQUIRE(back);
  CHECK(back->restPending);
  Schedule schedule(*back);
  Context ctx = picks(now, {"NFL", "NCAAM"});
  REQUIRE(schedule.refreshIfNeeded(ctx));
  // Nothing for the board to wait for.
  CHECK_FALSE(schedule.fetching());
  std::vector<std::string> asked;
  while (auto p = schedule.nextRequest()) {
    asked.push_back(p->query);
    schedule.onResponse(200, R"({"events":[]})");
  }
  CHECK(asked == std::vector<std::string>{"dates=202611", "dates=20261028", "dates=20261029", "dates=20261030"});
  REQUIRE(schedule.takeFinished());
  // Tonight's game and last week's (not asked for again) stay; Thursday's and
  // next week's (asked for, not listed) are gone.
  CHECK(ids(schedule.saved().games) == std::set<std::string>{"NCAAM|tonight", "NFL|lastweek"});
  CHECK_FALSE(schedule.saved().restPending);
  // Then nothing more today.
  CHECK_FALSE(schedule.refreshIfNeeded(ctx));
}

TEST_CASE("today first: nothing came and the network is down: the rest waits") {
  Seconds now = utc(2026, 10, 26, 16, 0);
  Schedule schedule;
  Context ctx = picks(now, {"NCAAM"});
  ctx.seen.calendars["4403"] = now;
  REQUIRE(schedule.refreshIfNeeded(ctx));
  int asked = 0;
  while (auto p = schedule.nextRequest()) {
    asked += 1;
    schedule.onResponse(0, "");
  }
  CHECK(asked == 2);  // today and tomorrow, then no more
  auto done = schedule.takeFinished();
  REQUIRE(done);
  CHECK_FALSE(done->succeeded);
  CHECK(schedule.saved().failures.value_or(0) == 0);
}

TEST_CASE("calendars with the schedule: after today's part, between the other days, published with it") {
  Seconds now = utc(2026, 11, 2, 16, 0);
  Schedule schedule;
  Context ctx = picks(now, {"EPL"});
  ctx.seen.calendars["4403"] = now - 3600;
  ctx.seen.calendars["4422"] = now - 3600;
  // Not seen: not asked for.
  REQUIRE(schedule.activeCalendars(ctx).size() == 2);
  REQUIRE(schedule.refreshIfNeeded(ctx));
  struct Sent {
    bool calendar;
    bool today;
    int waitMs;
  };
  std::vector<Sent> sent;
  std::string event = R"({"events":[{"idEvent":"55","strEvent":"Landskrona vs Örebro","strHomeTeam":"Landskrona",
    "strAwayTeam":"Örebro","strTimestamp":"2026-11-02T18:05:00","strSeason":"2026","intRound":"29"}]})";
  while (auto p = schedule.nextRequest()) {
    bool calendar = p->kind == Schedule::Pending::Kind::calendar;
    CHECK(calendar == (p->request.url.find("thesportsdb.com") != std::string::npos));
    sent.push_back({calendar, p->today, p->waitMs});
    if (calendar && p->ask.key == "4403") schedule.onResponse(200, event);
    else if (calendar) schedule.onResponse(200, R"({"events":null})");
    else schedule.onResponse(200, R"({"events":[]})");
  }
  REQUIRE(schedule.takeFinished());
  // Today's part has none; the first calendar request comes right after it.
  size_t first = 0;
  while (first < sent.size() && sent[first].today) ++first;
  REQUIRE(first < sent.size());
  CHECK(first > 0);
  CHECK(sent[first].calendar);
  int calendarRequests = 0;
  int sinceLast = -1;  // ms waited since the last calendar request was sent
  for (size_t i = first; i < sent.size(); ++i) {
    if (sinceLast >= 0) sinceLast += sent[i].waitMs;
    if (sent[i].calendar) {
      calendarRequests += 1;
      if (sinceLast >= 0) CHECK(sinceLast >= calendars::kRequestSpacingMs);
      sinceLast = 0;
    }
  }
  // Two next events, the Superettan round, and the one after it.
  CHECK(calendarRequests == 4);
  CHECK(schedule.saved().calendars.requestCount == 4);
  // Published with the schedule; its source never named.
  auto games = schedule.games(ctx);
  auto it = std::find_if(games.begin(), games.end(), [](const SportsGame& g) { return g.id == "SWE2|55"; });
  REQUIRE(it != games.end());
  CHECK(it->league == "Soccer");
  CHECK(it->note == std::optional<std::string>("Superettan"));
  // Soccer unpicked: not looked up, not published.
  Context other = picks(now, {"NFL"});
  other.seen = ctx.seen;
  CHECK(ids(schedule.games(other)).count("SWE2|55") == 0);
  // Saved with the schedule.
  auto back = savedFromJson(toJson(schedule.saved()));
  REQUIRE(back);
  CHECK(Schedule(*back).games(ctx).size() == games.size());
  // Already looked at today: opening again asks nothing.
  CHECK_FALSE(schedule.refreshIfNeeded(ctx));
  // Off: nothing.
  Context off = ctx;
  off.enabled = false;
  CHECK(schedule.games(off).empty());
  CHECK(schedule.activeCalendars(off).empty());
}

TEST_CASE("calendars with the schedule: one newly seen is looked up on its own the same day") {
  Seconds now = utc(2026, 11, 2, 16, 0);
  Schedule schedule;
  Context ctx = picks(now, {"NFL"});
  REQUIRE(schedule.refreshIfNeeded(ctx));
  while (auto p = schedule.nextRequest()) schedule.onResponse(200, R"({"events":[]})");
  REQUIRE(schedule.takeFinished());
  CHECK_FALSE(schedule.refreshIfNeeded(ctx));
  // The CFL named in a feed: asked for now, only it.
  ctx.seen.calendars["4405"] = now;
  REQUIRE(schedule.leaguesChanged(ctx));
  std::vector<std::string> asked;
  while (auto p = schedule.nextRequest()) {
    asked.push_back(p->request.url);
    schedule.onResponse(200, R"({"events":null})");
  }
  CHECK(asked == std::vector<std::string>{"https://www.thesportsdb.com/api/v1/json/123/eventsnextleague.php?id=4405"});
  REQUIRE(schedule.takeFinished());
  CHECK_FALSE(schedule.leaguesChanged(ctx));
}
