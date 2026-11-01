// The main loop's once-a-minute line (Build 120).
#include <string>

#include "core/report/loop_stats.hpp"
#include "doctest.h"

using marigold::report::LoopStats;

namespace {

LoopStats::Times parts(float events, float store, float screen, float draw) { return {events, store, screen, draw}; }

bool has(const std::string& line, const std::string& part) { return line.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("loop stats: once a minute, the slowest step and its parts") {
  LoopStats s;
  CHECK(s.line(1000, "guide").empty());  // starts counting
  s.step(parts(0.5f, 1.0f, 0.2f, 4.0f), true, false, true);
  s.step(parts(0.1f, 62.0f, 0.3f, 5.0f), true, false, true);  // a slow store tick
  s.step(parts(0.2f, 0.1f, 0.1f, 0), false, false, false);
  s.step(parts(0.1f, 0.1f, 0.1f, 51.0f), true, false, true);   // a slow draw
  CHECK(s.line(30000, "guide").empty());  // not a minute yet
  std::string line = s.line(61000, "guide");
  CAPTURE(line);
  CHECK(has(line, "guide: 3 frames in 4 steps"));
  CHECK(has(line, "slowest 67.4 ms (events 0.1, store 62.0, screen 0.3, draw 5.0)"));
  CHECK(has(line, "2 over 50 ms (events 0, store 1, screen 0, draw 1)"));
  CHECK(has(line, "each part's slowest: events 0.5, store 62.0, screen 0.3, draw 51.0 ms"));
  CHECK(has(line, "hidden: 0 wakeups, 0 with work"));
  // Counting started again.
  CHECK(s.line(122000, "guide").empty());  // nothing happened
}

TEST_CASE("loop stats: a quiet minute (the clock's redraw) says nothing") {
  LoopStats s;
  s.line(0, "guide");
  for (int i = 0; i < 2; ++i) s.step(parts(0, 0, 0, 3), true, false, true);
  for (int i = 0; i < 100; ++i) s.step(parts(0, 0.1f, 0, 0), false, false, false);
  CHECK(s.line(60000, "guide").empty());
}

TEST_CASE("loop stats: wakeups while hidden go in the next line once shown") {
  LoopStats s;
  s.line(0, "player");
  for (int i = 0; i < 30; ++i) s.step(parts(0, 0.2f, 0, 0), i % 10 == 0, true, i % 10 == 0);
  CHECK(s.line(60000, "player").empty());  // hidden: no line
  for (int i = 0; i < 5; ++i) s.step(parts(0, 0.2f, 0, 0), false, true, false);
  s.step(parts(0.2f, 0.1f, 0.1f, 6), true, false, true);  // shown again
  std::string line = s.line(120000, "player");
  CAPTURE(line);
  CHECK(has(line, "player: 1 frames in 1 steps"));
  CHECK(has(line, "hidden: 35 wakeups, 3 with work"));
  s.step(parts(0, 0, 0, 3), true, false, true);
  s.step(parts(0, 0, 0, 3), true, false, true);
  s.step(parts(0, 0, 0, 3), true, false, true);
  CHECK(has(s.line(180000, "player"), "hidden: 0 wakeups"));
}
