// The main loop's cost, once a minute in the app log (Build 120): the
// slowest step and how many took over 50 ms, each split into the event
// pump, the store's tick, the screen's tick and the draw; the frames drawn;
// the screen on top; and how often the loop woke, and did work, while the
// app was hidden. Only while the app is shown and something happened (more
// than the clock's redraw). Counts and ms only.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace marigold::report {

class LoopStats {
 public:
  enum Part { Events, Store, Screen, Draw, kParts };
  using Times = std::array<float, kParts>;  // ms in each part of one step
  static constexpr uint32_t kEveryMs = 60000;
  static constexpr float kSlowMs = 50;

  // One step of the loop: the ms spent in each part; whether it drew a
  // frame; whether the app was hidden, and whether the step did anything
  // (drew, or the store, a worker's result or the screen had something).
  void step(const Times& ms, bool drew, bool hidden, bool worked);
  // Every step: once a minute, the line to log ("" when there is none),
  // and counting starts again (what happened while hidden waits for the
  // next line). `screen` names the screen on top.
  std::string line(uint32_t now, const char* screen);

 private:
  struct Counts {
    int steps = 0, frames = 0, slow = 0;
    float worst = 0;
    Times worstSplit{};   // the slowest step's parts
    Times partWorst{};    // each part's slowest
    std::array<int, kParts> partSlow{};  // steps where that part alone took over 50 ms
    int hiddenWakeups = 0, hiddenWork = 0;
  };
  Counts c_;
  uint32_t since_ = 0;
  bool started_ = false;
  bool hidden_ = false;  // the last step's
};

}  // namespace marigold::report
