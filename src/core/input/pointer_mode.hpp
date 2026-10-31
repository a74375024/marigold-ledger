// Who is in charge on an LG TV: the Magic Remote's pointer or its keys.
//
// A key press puts the pointer away: from then on hovering moves no focus and
// draws no highlight, and a click is plain OK on the focused item. Only real
// movement brings the pointer back. TVs differ in what they report once the
// keys take over: some hide their cursor and go quiet until the remote moves;
// others keep the cursor up and keep reporting the small motion of a hand
// resting on the remote (and the jolt of the press itself). That motion must
// not count, so the pointer comes back only when it travels more than
// kMovePx in a straight line within kMoveWindowMs, and never in the first
// kQuietMs after a key. Times are the platform's monotonic ms; positions are
// drawable pixels, as the pointer's events carry them.
#pragma once

#include <cstdint>

namespace marigold::pointer {

constexpr float kMovePx = 12.0f;
constexpr uint32_t kMoveWindowMs = 200;
constexpr uint32_t kQuietMs = 350;

class Mode {
 public:
  // The pointer is in charge: hover follows it and a click is at it.
  bool active() const { return active_; }

  // A remote key went down (a repeat too). True when that put the pointer
  // away.
  bool key(uint32_t ms);
  // The system hid its cursor (webOS: the cursor slept, or a key hid it).
  // True when that put the pointer away.
  bool hidden();
  // The pointer is at (x, y). True when it is in charge, so hover follows
  // (it may have just come back).
  bool moved(float x, float y, uint32_t ms);
  // The pointer's button went down. True: a click at the pointer. False: the
  // pointer is away, so this is OK on the focused item, and a key press.
  bool click(uint32_t ms);
  // The system showed its cursor again: true when that is still the jolt of
  // the last key press (ask it to hide the cursor again), false when the
  // hand may be on the remote (leave the cursor up; movement decides).
  bool shownByKey(uint32_t ms) const;

 private:
  bool active_ = false;
  bool keyed_ = false;  // a key has gone down since the start
  uint32_t keyMs_ = 0;
  // Where the current stretch of movement started.
  bool anchored_ = false;
  float anchorX_ = 0, anchorY_ = 0;
  uint32_t anchorMs_ = 0;
};

}  // namespace marigold::pointer
