#include <string>

#include "core/sports/leagues.hpp"
#include "core/sports/refusals.hpp"
#include "core/sports/schedule.hpp"
#include "core/sports/scores.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using sportsdata::utc;

TEST_CASE("refusals: which source, and until when") {
  CHECK(refusals::sourceOf("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard") == "espn.com");
  CHECK(refusals::sourceOf("https://site.web.api.espn.com/apis/v2/scoreboard/header?sport=cricket") == "espn.com");
  CHECK(refusals::sourceOf(*leagueByShort("D3WSO")) == "ncaa.com");
  CHECK(refusals::sourceOf(*leagueByShort("NFL")) == "espn.com");
  time::Zone et = time::Zone::usEastern();
  Seconds now = utc(2026, 10, 28, 16, 0);  // noon in New York
  CHECK_FALSE(refusals::until(200, 0, now, et));
  CHECK_FALSE(refusals::until(500, 0, now, et));
  CHECK(refusals::until(429, 0, now, et) == utc(2026, 10, 29, 4, 0));    // the next midnight there
  CHECK(refusals::until(403, 60, now, et) == utc(2026, 10, 29, 4, 0));   // a short wait: still the day
  CHECK(refusals::until(429, 2 * 86'400, now, et) == now + 2 * 86'400);  // a longer one is kept
}

TEST_CASE("refusals: live scores leave a refusing source alone, others go on") {
  Seconds now = utc(2026, 10, 28, 16, 0);
  scores::LiveScores live;
  REQUIRE(live.start(scores::Holder::board, true));
  scores::RoundInput in;
  in.now = now;
  in.zone = time::Zone::usEastern();
  in.games = {sportsdata::game("NFL", "NFL", "1", sportsdata::team("1", "A"), sportsdata::team("2", "B"), now - 600),
              sportsdata::game("NHL", "NHL", "2", sportsdata::team("3", "C"), sportsdata::team("4", "D"), now - 600)};
  live.beginRound(in);
  auto first = live.nextRequest(now);
  REQUIRE(first);
  live.onResponse(429, "", now, 0);
  // The other league of the same source isn't asked this round.
  CHECK_FALSE(live.nextRequest(now));
  CHECK(live.refusedUntil().at("espn.com") > now);
  // Nor in the next one, later today.
  live.beginRound(in);
  CHECK_FALSE(live.nextRequest(now + 3600));
}

TEST_CASE("refusals: a schedule fetch skips the refusing source, saved, not a failure") {
  Seconds now = utc(2026, 10, 26, 16, 0);
  schedule::Schedule s;
  schedule::Context ctx;
  ctx.prefs.configured = true;
  ctx.prefs.leagues = {"NFL", "NBA"};
  ctx.now = now;
  ctx.zone = time::Zone::usEastern();
  REQUIRE(s.refreshIfNeeded(ctx));
  auto first = s.nextRequest();
  REQUIRE(first);
  s.onResponse(403, "", 0);
  CHECK_FALSE(s.nextRequest());  // NBA is on the same source
  auto done = s.takeFinished();
  REQUIRE(done);
  CHECK(s.saved().refusedUntil.at("espn.com") > now);
  CHECK_FALSE(s.saved().failures.value_or(0) > 0);
  // Kept across launches.
  auto back = schedule::savedFromJson(schedule::toJson(s.saved()));
  REQUIRE(back);
  CHECK(back->refusedUntil == s.saved().refusedUntil);
}
