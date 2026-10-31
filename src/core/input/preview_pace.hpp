// When the corner preview tunes (the guide, Live Events and Search): after
// focus rests on something for kDelayMs, and at most once every kGapMs, so
// scrolling past rows or tiles tunes nothing. Live Events and Search try
// each item once (a failed feed isn't retried while focus stays there);
// Search also skips the first item focus lands on from its keyboard until
// focus moves, and with the pointer the rest counts from its last move.
#pragma once

#include <cstdint>
#include <string>

namespace marigold::preview {

constexpr uint32_t kDelayMs = 2000;
constexpr uint32_t kGapMs = 3000;

class Pace {
 public:
  explicit Pace(bool oncePerItem) : once_(oncePerItem) {}

  enum class Step {
    Idle,  // nothing to do (nothing previewable, already playing, tried)
    Wait,  // resting: keep ticking
    Tune,  // tune `id` now
  };
  // Each tick: the focused item when it may preview ("" when it may not),
  // and whether the corner already plays it.
  Step step(const std::string& id, bool playing, uint32_t ms);
  // Focus just landed on `id` from elsewhere: no preview there until it
  // moves on.
  void hold(const std::string& id) { held_ = id; }
  // The pointer moved: the rest starts again from now.
  void moved(uint32_t ms) { since_ = ms; }
  // When focus came to the item it is on.
  uint32_t since() const { return since_; }
  // Forget the items tried (a new list).
  void reset() {
    tried_.clear();
    candidate_.clear();
    held_.clear();
  }

 private:
  bool once_;
  std::string candidate_, tried_, held_;
  uint32_t since_ = 0, last_ = 0;
};

}  // namespace marigold::preview
