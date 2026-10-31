#include "core/input/preview_pace.hpp"

namespace marigold::preview {

Pace::Step Pace::step(const std::string& id, bool playing, uint32_t ms) {
  if (id.empty()) {
    candidate_.clear();
    return Step::Idle;
  }
  if (candidate_ != id) {
    candidate_ = id;
    since_ = ms;
    // Moving on from where focus landed lifts the hold.
    if (!held_.empty() && held_ != id) held_.clear();
  }
  if (held_ == id || playing || (once_ && tried_ == id)) return Step::Idle;
  if (ms - since_ >= kDelayMs && ms - last_ >= kGapMs) {
    last_ = ms;
    tried_ = id;
    return Step::Tune;
  }
  return Step::Wait;
}

}  // namespace marigold::preview
