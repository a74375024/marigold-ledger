// Screens that sleep until something on them changes (Build 120): the
// moments they wake for, and how often the loop draws in each idle case
// (a fake clock driving the same rules as the app's loop).
#include <algorithm>
#include <cstdint>
#include <functional>

#include "core/input/countdown.hpp"
#include "core/input/preview_pace.hpp"
#include "core/input/wake.hpp"
#include "doctest.h"

namespace wake = marigold::wake;
using marigold::preview::Pace;

namespace {

// The app's loop on a fake clock with no events: each step the screen
// ticks (true: draw now; `due`: its next moment), then the loop sleeps as
// sleepMs() says, or draws (one frame, 16 ms) without sleeping.
struct Run {
  int frames = 0, wakeups = 0;
};
Run simulate(uint32_t from, uint32_t until, const std::function<bool(uint32_t, wake::Due&)>& tick) {
  Run r;
  for (uint32_t now = from; now < until;) {
    wake::Due due;
    bool draw = tick(now, due);
    wake::Loop loop;
    loop.now = now;
    loop.animating = draw;
    loop.due = due.get();
    ++r.wakeups;
    if (draw) {
      ++r.frames;
      now += 16;
    } else {
      now += static_cast<uint32_t>(std::max(1, wake::sleepMs(loop)));
    }
  }
  return r;
}

// Before Build 120 the same cases drew every frame (60 a second).
int everyFrame(uint32_t ms) { return static_cast<int>(ms / 16); }

}  // namespace

TEST_CASE("wake: the earliest moment due") {
  wake::Due due;
  CHECK(due.get() == 0);
  CHECK_FALSE(due.reached(1000));
  due.at(0);  // nothing
  CHECK(due.get() == 0);
  due.at(5000);
  due.at(3000);
  due.at(4000);
  CHECK(due.get() == 3000);
  CHECK_FALSE(due.reached(2999));
  CHECK(due.reached(3000));
  CHECK(due.reached(3500));
  due.clear();
  CHECK(due.get() == 0);
}

TEST_CASE("wake: a spinner moves once a step, eight a second") {
  wake::Steps s;
  CHECK(s.moved(1000));  // the first look always draws
  CHECK_FALSE(s.moved(1001));
  CHECK_FALSE(s.moved(1124));
  CHECK(s.moved(1125));
  CHECK(s.next(1125) == 1250);
  CHECK(s.next(1249) == 1250);
  wake::Due due;
  CHECK_FALSE(s.tick(1200, due));
  CHECK(due.get() == 1250);
  int moves = 0;
  for (uint32_t t = 2000; t < 3000; t += 16) moves += s.moved(t) ? 1 : 0;
  CHECK(moves == 8);
  wake::Steps pulse(100);
  CHECK(pulse.next(250) == 300);
}

TEST_CASE("wake: how long the loop sleeps") {
  wake::Loop l;
  l.now = 10000;
  CHECK(wake::sleepMs(l) == 500);  // idle: the clock and workers still show
  l.animating = true;
  CHECK(wake::sleepMs(l) == 0);
  l.animating = false;
  l.dirty = true;
  CHECK(wake::sleepMs(l) == 0);
  l.okHeld = true;  // as before: OK held looks every 50 ms, even with a redraw asked for
  CHECK(wake::sleepMs(l) == 50);
  l.okHeld = l.dirty = false;
  l.due = 10040;
  CHECK(wake::sleepMs(l) == 40);
  l.due = 9000;  // already due
  CHECK(wake::sleepMs(l) == 0);
  l.due = 20000;  // far off: still at most half a second
  CHECK(wake::sleepMs(l) == 500);
  l.poll = true;  // a channel opening is looked at every frame...
  CHECK(wake::sleepMs(l) == 16);
  l.due = 10005;
  CHECK(wake::sleepMs(l) == 5);
  l.animating = true;  // ... and drawing still never waits
  CHECK(wake::sleepMs(l) == 0);
}

TEST_CASE("wake: the guide's corner preview rests asleep, then tunes") {
  // Focus lands on a channel at 1000: the preview's rest ends at due().
  Pace pace(false);
  bool tuned = false;
  uint32_t tunedAt = 0, restEnds = 0;
  Run r = simulate(1000, 1000 + 2100, [&](uint32_t ms, wake::Due& due) {
    switch (pace.step("ch1", tuned, ms)) {
      case Pace::Step::Wait:
        restEnds = pace.due();
        due.at(restEnds);
        return false;
      case Pace::Step::Tune:
        tuned = true;
        tunedAt = ms;
        return true;  // (the app draws the corner as the channel opens)
      case Pace::Step::Idle:
        return false;
    }
    return false;
  });
  REQUIRE(tuned);
  CHECK(tunedAt == restEnds);  // tuned exactly when the rest ended, not later
  CHECK(restEnds > 1000);
  CHECK(r.frames == 1);
  CHECK(r.wakeups <= 8);
  CHECK(everyFrame(2100) == 131);
}

TEST_CASE("wake: a spinner draws eight frames a second, not sixty") {
  // "Updating your channel list…" for 4 s, or a channel opening.
  wake::Steps spinner;
  Run r = simulate(5000, 9000, [&](uint32_t ms, wake::Due& due) { return spinner.tick(ms, due); });
  CHECK(r.frames == 32);
  CHECK(r.wakeups == 64);  // each step: the frame, and one look before sleeping
  CHECK(everyFrame(4000) == 250);
}

TEST_CASE("wake: a pause or a few seconds' message draws once, as it ends") {
  // A typed number's 1.5 s, the player's controls left 12 s, a note or a
  // notice's few seconds: one frame, at the moment it changes.
  for (uint32_t wait : {1500u, 2000u, 2500u, 3500u, 12000u}) {
    CAPTURE(wait);
    const uint32_t from = 40000, until = from + wait;
    bool shown = true;
    uint32_t endedAt = 0;
    Run r = simulate(from, until + 1000, [&](uint32_t ms, wake::Due& due) {
      if (shown && ms >= until) {
        shown = false;
        endedAt = ms;
        return true;
      }
      if (shown) due.at(until);
      return false;
    });
    CHECK(endedAt == until);
    CHECK(r.frames == 1);
    CHECK(r.wakeups <= static_cast<int>(wait / 500 + 4));
  }
}

TEST_CASE("wake: the banner's countdown draws once a second") {
  const uint32_t from = 70000, until = from + 5000;
  int second = 0;
  Run r = simulate(from, until + 500, [&](uint32_t ms, wake::Due& due) {
    int now = ms < until ? marigold::countdown::shown(until, ms) : 0;
    bool changed = now != second;
    second = now;
    due.at(marigold::countdown::nextChange(until, ms));
    return changed;
  });
  CHECK(r.frames == 7);  // the first instant, then 5, 4, 3, 2, 1 and gone
}
