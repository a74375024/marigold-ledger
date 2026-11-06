// Build 145: the scores of what you're looking at go first. The leagues of
// the sport open on the board or in the in-player guide, of the game
// playing and of your teams' games keep their own pace (RoundInput::focus);
// the others share it, and slow down first past the hourly pace. The
// in-player guide holds full pace over a playing channel; with no guide
// open a channel playing keeps the background's pace. The day's caps stay.
//
// The replay: a busy afternoon (the Nov 6 board's 17 leagues with games on
// at once, tennis live), 10 hours from noon Eastern, every request answered
// live after a second. Run with MARIGOLD_SCORES_TABLE=1 for its table.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "core/sports/scores.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::scores;
using sportsdata::game;
using sportsdata::team;
using sportsdata::utc;

namespace {

// The leagues with games in their window on the Nov 6 board.
const std::vector<std::string> kBusy = {"NCAAWV", "NCAAM", "SOC",  "NCAAW", "NCAAWH", "NCAAMH", "WTA", "ATP", "NBA",
                                        "NCAAF",  "NHL",   "CRIC", "EPL",   "PGA",    "F1",     "LPGA", "DPWT"};

SportsGame liveGame(const std::string& league, Seconds start, const std::string& id = "1") {
  const League* l = leagueByShort(league);
  REQUIRE(l);
  SportsGame g = game(league, l->group, id, team(league + "h", "Home " + league), team(league + "a", "Away " + league),
                      start);
  if (l->tennis) g.tennis = true;
  if (l->racing) g.racing = true;
  if (l->golf) g.golf = true;
  return g;
}

RoundInput input(const std::vector<SportsGame>& games, Seconds now, bool watching = false) {
  RoundInput in;
  in.games = games;
  for (const SportsGame& g : games) in.watchableListed.insert(g.id);
  in.watching = watching;
  in.now = now;
  in.zone = time::Zone::usEastern();
  return in;
}

// Each league's game, live.
Statuses liveFor(const std::string& league, const std::vector<SportsGame>& games) {
  Statuses s;
  for (const SportsGame& g : games) {
    if (g.leagueShort != league) continue;
    LiveStatus live;
    live.phase = LivePhase::live;
    live.detail = "Live";
    s[g.id] = live;
  }
  return s;
}

// Who looks: the board on screen (nothing playing), the in-player guide over
// a playing channel, or a channel playing with no guide open.
enum class Look { board, guide, playing };

struct Replay {
  std::map<std::string, std::vector<Seconds>> asked;  // league -> request times
  std::vector<Seconds> all;
  Seconds cappedAt = 0;  // the day's cap reached (0: never)
  int today = 0;
};

// `build145`: the screens report what's open (the focus: Tennis, and the
// match playing under the guide) and the guide holds full pace. Without it,
// as before Build 145: no focus, and the guide held nothing.
Replay replay(Look look, bool build145, Seconds from, Seconds hours, LiveScores::DailyCount daily = {}) {
  std::vector<SportsGame> games;
  for (const std::string& l : kBusy) games.push_back(liveGame(l, from - 1800));
  LiveScores scores(daily);
  scores.start(Holder::background, true);
  if (look == Look::board || look == Look::guide) scores.start(Holder::board, true);
  if (look == Look::guide && build145) scores.start(Holder::guide, true);
  Replay out;
  Seconds t = from, roundAt = from;
  const Seconds end = from + hours * 3600;
  while (true) {
    t = std::max(t, roundAt);
    if (t >= end) break;
    RoundInput in = input(games, t, look != Look::board);
    if (build145) {
      in.focus.leagues = {"ATP", "WTA"};  // Tennis open
      if (look == Look::guide) in.focus.leagues.insert("ATP");  // an ATP match playing
    }
    scores.beginRound(in);
    while (auto r = scores.nextRequest(t)) {
      t += 1;  // the spacing and the answer
      out.asked[r->league.shortName].push_back(t);
      out.all.push_back(t);
      scores.onParsed(true, 20'000, liveFor(r->league.shortName, games), t);
    }
    out.today = scores.requestsToday(t, time::Zone::usEastern());
    if (!out.cappedAt && out.today >= kDailyCap) out.cappedAt = t;
    roundAt = t + scores.waitSeconds();
  }
  return out;
}

int inHour(const Replay& r, Seconds from, int hour) {
  return static_cast<int>(std::count_if(r.all.begin(), r.all.end(), [&](Seconds t) {
    return t >= from + hour * 3600 && t < from + (hour + 1) * 3600;
  }));
}

// Seconds between one league's requests over the first `hours`: the median
// and the longest.
std::pair<Seconds, Seconds> gaps(const Replay& r, const std::string& league, Seconds from, Seconds hours) {
  std::vector<Seconds> d;
  auto it = r.asked.find(league);
  if (it == r.asked.end()) return {0, 0};
  const std::vector<Seconds>& at = it->second;
  for (size_t i = 1; i < at.size(); ++i)
    if (at[i - 1] >= from && at[i] <= from + hours * 3600) d.push_back(at[i] - at[i - 1]);
  if (d.empty()) return {0, 0};
  std::sort(d.begin(), d.end());
  return {d[d.size() / 2], d.back()};
}

const Seconds kNoon = utc(2026, 11, 7, 17, 0);  // a Saturday, noon Eastern

}  // namespace

TEST_CASE("scores focus: a busy afternoon, tennis looked at (Build 145 before and after)") {
  struct Row {
    const char* name;
    Look look;
  };
  const Row rows[] = {{"(a) board, Tennis selected", Look::board},
                      {"(b) in-player column over a channel", Look::guide},
                      {"(c) playing, no guide open", Look::playing}};
  const bool table = std::getenv("MARIGOLD_SCORES_TABLE") != nullptr;
  if (table)
    std::printf("%-38s %-6s %12s %10s %8s %8s %8s %10s %6s\n", "case", "build", "ATP median", "ATP max", "hour 1",
                "hour 2", "hour 3", "cap at", "day");
  for (const Row& row : rows) {
    Replay before = replay(row.look, false, kNoon, 10);
    Replay after = replay(row.look, true, kNoon, 10);
    // The first two hours (before any cap).
    auto [beforeMedian, beforeMax] = gaps(before, "ATP", kNoon, 2);
    auto [afterMedian, afterMax] = gaps(after, "ATP", kNoon, 2);
    if (table) {
      for (const auto* r : {&before, &after}) {
        auto [median, longest] = r == &before ? std::make_pair(beforeMedian, beforeMax) : std::make_pair(afterMedian, afterMax);
        char cap[32] = "never";
        if (r->cappedAt) std::snprintf(cap, sizeof cap, "%.1f h", static_cast<double>(r->cappedAt - kNoon) / 3600);
        std::printf("%-38s %-6s %10llds %9llds %8d %8d %8d %10s %6d\n", row.name, r == &before ? "144" : "145",
                    static_cast<long long>(median), static_cast<long long>(longest), inHour(*r, kNoon, 0),
                    inHour(*r, kNoon, 1), inHour(*r, kNoon, 2), cap, r->today);
      }
    }
    // The day's cap holds whatever is looked at.
    CHECK(before.today <= kDailyCap);
    CHECK(after.today <= kDailyCap);
    CHECK(static_cast<int>(after.all.size()) <= kDailyCap);
    switch (row.look) {
      case Look::board:
        // Before: tennis waited its turn among 17 leagues (5-8 minutes).
        CHECK(beforeMedian >= 300);
        // After: about once a minute (80 s past the hourly pace; a round's
        // few seconds aside).
        CHECK(afterMedian <= 90);
        CHECK(afterMax <= 2 * kLiveInterval);
        break;
      case Look::guide:
        // Before: the guide counted as watching: the background's pace.
        CHECK(beforeMedian >= kBackgroundEvery);
        CHECK(afterMedian <= 90);
        CHECK(afterMax <= 2 * kLiveInterval);
        break;
      case Look::playing:
        // Nothing changes: the background's pace, within its share of the day.
        CHECK(before.all == after.all);
        CHECK(afterMedian >= kBackgroundEvery);
        CHECK(after.today <= kBackgroundDailyCap);
        break;
    }
    if (row.look != Look::playing) {
      // Past the hourly pace the others slow down first: tennis keeps about
      // its minute while the hour's requests come down from the first
      // burst to the pace (as before Build 145).
      CHECK(inHour(after, kNoon, 1) < inHour(after, kNoon, 0));
      CHECK(inHour(after, kNoon, 1) <= kHourlyPace + kHourlyPace / 10);
      CHECK(inHour(after, kNoon, 2) <= kHourlyPace + kHourlyPace / 10);
      auto [pacedMedian, pacedMax] = gaps(after, "ATP", kNoon + 3600, 1);
      CHECK(pacedMedian <= 90);
      CHECK(pacedMax <= 2 * kLiveInterval);
      // The others still asked, at least every half hour.
      auto [nbaMedian, nbaMax] = gaps(after, "NBA", kNoon, 2);
      (void)nbaMedian;
      CHECK(nbaMax <= kCappedWait + 60);
      // Once capped, nothing more today.
      if (after.cappedAt) {
        CHECK(std::none_of(after.all.begin(), after.all.end(), [&](Seconds t) { return t > after.cappedAt; }));
      }
    }
  }
}

TEST_CASE("scores focus: the leagues looked at keep their pace and leave the others' spread") {
  Seconds now = utc(2026, 11, 7, 18, 0);
  std::vector<SportsGame> games;
  for (const char* l : {"ATP", "NBA", "NHL", "NFL", "EPL"}) games.push_back(liveGame(l, now - 600));
  auto round = [&](LiveScores& s, const RoundInput& in) {
    s.beginRound(in);
    while (auto r = s.nextRequest(in.now)) s.onParsed(true, 1000, liveFor(r->league.shortName, games), in.now);
    return s.waitSeconds();
  };
  // Nothing looked at: five leagues share the pace (60 s x 2.5), as before.
  LiveScores plain;
  plain.start(Holder::board, true);
  CHECK(round(plain, input(games, now)) == 150);
  // ATP looked at: once a minute; the four others spread over four (x 2).
  LiveScores focus;
  focus.start(Holder::board, true);
  RoundInput in = input(games, now);
  in.focus.leagues = {"ATP"};
  CHECK(round(focus, in) == kLiveInterval);
  in.now = now + kLiveInterval;
  focus.beginRound(in);
  std::vector<std::string> asked;
  while (auto r = focus.nextRequest(in.now)) {
    asked.push_back(r->league.shortName);
    focus.onParsed(true, 1000, liveFor(r->league.shortName, games), in.now);
  }
  CHECK(asked == std::vector<std::string>{"ATP"});
  in.now = now + 2 * kLiveInterval;
  focus.beginRound(in);
  asked.clear();
  while (auto r = focus.nextRequest(in.now)) {
    asked.push_back(r->league.shortName);
    focus.onParsed(true, 1000, liveFor(r->league.shortName, games), in.now);
  }
  CHECK(asked == std::vector<std::string>{"ATP", "EPL", "NBA", "NFL", "NHL"});  // 120 s = 60 x 4/2

  // A league not in its window isn't looked at however it's asked for.
  LiveScores none;
  none.start(Holder::board, true);
  RoundInput off = input(games, now);
  off.focus.leagues = {"MLB"};
  CHECK(round(none, off) == 150);

  // A league just asked at the spread pace, then looked at: due at its own
  // pace from its last answer, not the spread's.
  LiveScores later;
  later.start(Holder::board, true);
  CHECK(round(later, input(games, now)) == 150);
  RoundInput look = input(games, now + kLiveInterval);
  look.focus.leagues = {"ATP"};
  later.beginRound(look);
  auto first = later.nextRequest(now + kLiveInterval);
  REQUIRE(first);
  CHECK(first->league.shortName == "ATP");
  CHECK(later.awaits("ATP", now + kLiveInterval));
  later.onParsed(true, 1000, liveFor("ATP", games), now + kLiveInterval);
  CHECK_FALSE(later.nextRequest(now + kLiveInterval));  // the others wait their spread
  CHECK_FALSE(later.awaits("NBA", now + kLiveInterval));
}

TEST_CASE("scores focus: your teams' games, the background, the guide closing mid-round") {
  Seconds now = utc(2026, 11, 7, 18, 0);
  std::vector<SportsGame> games;
  for (const char* l : {"ATP", "NBA", "NHL", "NFL", "EPL"}) games.push_back(liveGame(l, now - 600));
  // An NHL team followed (My Sports' "LEAGUE|teamID"): its league keeps its
  // pace. The same id in another league, or a key that merely starts like
  // the league, isn't it.
  auto firstWait = [&](std::vector<std::string> teams) {
    LiveScores s;
    s.start(Holder::board, true);
    RoundInput in = input(games, now);
    in.focus.teams = std::move(teams);
    s.beginRound(in);
    while (auto r = s.nextRequest(now)) s.onParsed(true, 1000, liveFor(r->league.shortName, games), now);
    return s.waitSeconds();
  };
  CHECK(firstWait({"NHL|NHLh"}) == kLiveInterval);
  CHECK(firstWait({"NHL|NHLa"}) == kLiveInterval);
  CHECK(firstWait({"MLB|NHLh"}) == 150);   // another league's team
  CHECK(firstWait({"NH|NHLh"}) == 150);    // not the league
  CHECK(firstWait({"NHL|NHL"}) == 150);    // not the team
  CHECK(firstWait({"NHL|"}) == 150);
  CHECK(firstWait({}) == 150);
  // A followed team without a game in the window: nothing looked at.
  SportsGame tonight = liveGame("MLS", now + 5 * 3600);
  games.push_back(tonight);
  CHECK(firstWait({"MLS|MLSh"}) == 150);

  // The background's rounds leave the focus out (watching, no guide).
  LiveScores bg;
  bg.start(Holder::background, true);
  bg.start(Holder::board, true);
  RoundInput watching = input(games, now, true);
  watching.focus.leagues = {"ATP"};
  bg.beginRound(watching);
  int sent = 0;
  while (auto r = bg.nextRequest(now)) {
    ++sent;
    bg.onParsed(true, 1000, liveFor(r->league.shortName, games), now);
  }
  CHECK(sent == 5);
  CHECK(bg.waitSeconds() == kBackgroundEvery);
  // The guide opens a minute later: it looks now, ATP (looked at) due at its
  // own pace from its last answer, the others at the spread's.
  watching.now = now + kLiveInterval;
  CHECK(bg.start(Holder::guide, true));
  bg.beginRound(watching);
  auto tennis = bg.nextRequest(watching.now);
  REQUIRE(tennis);
  CHECK(tennis->league.shortName == "ATP");
  bg.onParsed(true, 1000, liveFor("ATP", games), watching.now);
  CHECK_FALSE(bg.nextRequest(watching.now));
  CHECK(bg.waitSeconds() == kLiveInterval);

  // The guide over a playing channel: full pace, and closed mid-round the
  // rest of the round waits for the background's pace.
  LiveScores guide;
  guide.start(Holder::background, true);
  guide.start(Holder::board, true);
  guide.start(Holder::guide, true);
  RoundInput over = input(games, now, true);
  over.focus.leagues = {"ATP"};
  guide.beginRound(over);
  auto r1 = guide.nextRequest(now);
  REQUIRE(r1);
  guide.onParsed(true, 1000, liveFor(r1->league.shortName, games), now);
  CHECK_FALSE(guide.stop(Holder::guide));  // the background still holds
  CHECK_FALSE(guide.nextRequest(now));
  CHECK(guide.waitSeconds() == kBackgroundEvery);
  // Then the background's rounds: one at most every kBackgroundEvery.
  over.now = now + kBackgroundEvery;
  guide.beginRound(over);
  int rest = 0;
  while (auto r = guide.nextRequest(over.now)) {
    ++rest;
    guide.onParsed(true, 1000, liveFor(r->league.shortName, games), over.now);
  }
  CHECK(rest >= 4);
  CHECK(guide.waitSeconds() == kBackgroundEvery);
  // Without the background, closing ends the round too (the board under a
  // playing channel asks nothing).
  LiveScores alone;
  alone.start(Holder::board, true);
  alone.start(Holder::guide, true);
  alone.beginRound(over);
  REQUIRE(alone.nextRequest(over.now));
  alone.onParsed(true, 1000, {}, over.now);
  alone.stop(Holder::guide);
  CHECK_FALSE(alone.nextRequest(over.now));
  CHECK(alone.waitSeconds() == kWatchingWait);
}

TEST_CASE("scores focus: past the hourly pace, the others slow down first") {
  Seconds now = utc(2026, 11, 7, 18, 0);
  std::vector<SportsGame> games;
  for (const char* l : {"ATP", "WTA", "NBA", "NHL", "NFL", "EPL"}) games.push_back(liveGame(l, now - 600));
  // 120 answers in the last hour already (three other leagues looked at,
  // each once a minute for 40 minutes).
  auto paced = [&](std::set<std::string> focus) {
    LiveScores s;
    s.start(Holder::board, true);
    std::vector<SportsGame> other = {liveGame("MLS", now - 2500), liveGame("MLB", now - 2500),
                                     liveGame("WNBA", now - 2500)};
    int warmed = 0;
    for (int i = 0; i < 40; ++i) {
      RoundInput warm = input(other, now - 2400 + i * kLiveInterval);
      warm.focus.leagues = {"MLS", "MLB", "WNBA"};
      s.beginRound(warm);
      while (auto r = s.nextRequest(warm.now)) {
        ++warmed;
        s.onParsed(true, 1000, liveFor(r->league.shortName, other), warm.now);
      }
    }
    REQUIRE(warmed == kHourlyPace);
    RoundInput in = input(games, now);
    in.focus.leagues = std::move(focus);
    s.beginRound(in);
    while (auto r = s.nextRequest(now)) s.onParsed(true, 1000, liveFor(r->league.shortName, games), now);
    return s;
  };
  // Nothing looked at: as before, every league at least kPacedInterval.
  LiveScores plain = paced({});
  CHECK(plain.waitSeconds() == kPacedInterval);
  // Tennis looked at (two tours): the four others keep a request every
  // half hour each (8 an hour); the tours share the other 112 (every 64 s
  // each, their minute stretched last).
  LiveScores tennis = paced({"ATP", "WTA"});
  CHECK(tennis.waitSeconds() == 64);
  CHECK_FALSE(tennis.awaits("ATP", now + 63));
  CHECK(tennis.awaits("ATP", now + 64));
  CHECK_FALSE(tennis.awaits("NBA", now + kLiveInterval));
  CHECK_FALSE(tennis.awaits("NBA", now + kCappedWait - 1));
  CHECK(tennis.awaits("NBA", now + kCappedWait));
  // One league looked at: the other five share the other half of the hour
  // (60 requests: each every 5 minutes).
  LiveScores one = paced({"ATP"});
  CHECK(one.waitSeconds() == kLiveInterval);
  CHECK_FALSE(one.awaits("NBA", now + kPacedInterval - 1));
  CHECK(one.awaits("NBA", now + kPacedInterval));
}
