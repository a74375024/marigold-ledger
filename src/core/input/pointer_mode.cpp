#include "core/input/pointer_mode.hpp"

namespace marigold::pointer {

bool Mode::key(uint32_t ms) {
  bool was = active_;
  active_ = false;
  keyed_ = true;
  keyMs_ = ms;
  anchored_ = false;
  return was;
}

bool Mode::hidden() {
  bool was = active_;
  active_ = false;
  anchored_ = false;
  return was;
}

bool Mode::moved(float x, float y, uint32_t ms) {
  if (active_) return true;
  if (keyed_ && ms - keyMs_ < kQuietMs) {
    anchored_ = false;
    return false;
  }
  // A new stretch starts here when there is none or the last one is too
  // old: slow drift never adds up.
  if (!anchored_ || ms - anchorMs_ > kMoveWindowMs) {
    anchored_ = true;
    anchorX_ = x;
    anchorY_ = y;
    anchorMs_ = ms;
    return false;
  }
  // In a straight line: a trembling hand goes back and forth.
  float dx = x - anchorX_, dy = y - anchorY_;
  if (dx * dx + dy * dy <= kMovePx * kMovePx) return false;
  active_ = true;
  anchored_ = false;
  return true;
}

bool Mode::click(uint32_t ms) {
  if (active_) return true;
  key(ms);
  return false;
}

bool Mode::shownByKey(uint32_t ms) const { return !active_ && keyed_ && ms - keyMs_ < kQuietMs; }

}  // namespace marigold::pointer
