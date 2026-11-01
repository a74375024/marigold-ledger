// Logos decoded and prepared two at a time (Build 119): a fast scroll
// leaves the other cores to the UI thread. The jobs wait their turn in the
// order asked; a cancelled pool's jobs are forgotten.
#include <string>
#include <vector>

#include "core/logos/prepare_gate.hpp"
#include "doctest.h"

using marigold::logos::PrepareGate;

TEST_CASE("prepare gate: two at once, the rest in the order asked") {
  PrepareGate gate;
  std::vector<int> started;
  for (int i = 0; i < 5; ++i) gate.run([&started, i] { started.push_back(i); });
  CHECK(started == std::vector<int>{0, 1});
  CHECK(gate.full());
  CHECK(gate.running() == 2);
  CHECK(gate.waiting() == 3);
  gate.ended();  // one done: the next starts
  CHECK(started == std::vector<int>{0, 1, 2});
  CHECK(gate.running() == 2);
  gate.ended();
  gate.ended();
  CHECK(started == std::vector<int>{0, 1, 2, 3, 4});
  gate.ended();
  CHECK_FALSE(gate.full());
  CHECK(gate.running() == 1);
  gate.ended();
  gate.ended();  // never below none
  CHECK(gate.running() == 0);
  CHECK(gate.waiting() == 0);
}

TEST_CASE("prepare gate: never more than two, however the jobs end") {
  PrepareGate gate;
  int running = 0, most = 0, done = 0;
  std::vector<int> order;
  for (int i = 0; i < 40; ++i) {
    gate.run([&] {
      ++running;
      most = std::max(most, running);
    });
    // Every third request, one running job ends.
    if (i % 3 == 2 && running > 0) {
      --running;
      ++done;
      gate.ended();
    }
  }
  while (running > 0) {
    --running;
    ++done;
    gate.ended();
  }
  CHECK(most == PrepareGate::kMax);
  CHECK(done == 40);
  CHECK(gate.take().maxRunning == PrepareGate::kMax);
}

TEST_CASE("prepare gate: a cancelled pool's jobs are forgotten; the timing log's numbers") {
  PrepareGate gate;
  int started = 0;
  for (int i = 0; i < 4; ++i) gate.run([&started] { ++started; });
  gate.reset();
  CHECK(gate.running() == 0);
  CHECK(gate.waiting() == 0);
  gate.ended();  // a late end from before: nothing starts
  CHECK(started == 2);
  gate.run([&started] { ++started; });
  CHECK(started == 3);
  gate.took(1.5);
  gate.took(4.25);
  gate.took(2.0);
  PrepareGate::Stats s = gate.take();
  CHECK(s.prepared == 3);
  CHECK(s.maxMs == doctest::Approx(4.25));
  CHECK(s.maxRunning == 2);
  s = gate.take();  // since the last line only
  CHECK(s.prepared == 0);
  CHECK(s.maxMs == doctest::Approx(0.0));
  CHECK(s.maxRunning == 0);
}
