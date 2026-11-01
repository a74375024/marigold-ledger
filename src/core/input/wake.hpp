// When the main loop next has something to draw (Build 120). A screen that
// only waits (a preview's rest, a typed number's pause, a message's few
// seconds) or moves in steps (a spinner's dots, eight a second) sleeps
// until then instead of drawing every frame: its tick() is true only when
// something on screen changes, and its wakeAt() says when that next is.
#pragma once

#include <algorithm>
#include <cstdint>

namespace marigold::wake {

// ui::spinner's dots move on every 125 ms.
constexpr uint32_t kSpinnerStepMs = 125;
// Asleep at most this long (the clock and finished requests still show).
constexpr uint32_t kIdleMs = 500;
// A channel opening is looked at this often, as when the loop drew every
// frame (its picture is noticed as fast); nothing is drawn for it.
constexpr uint32_t kPollMs = 16;
// OK held: how soon a long press is noticed.
constexpr uint32_t kHoldPollMs = 50;

// The earliest of several moments (app ms; 0 = none).
class Due {
 public:
  void clear() { at_ = 0; }
  // Something is due at `ms` (0: nothing).
  void at(uint32_t ms) {
    if (ms && (!at_ || ms < at_)) at_ = ms;
  }
  uint32_t get() const { return at_; }
  // The moment has come.
  bool reached(uint32_t now) const { return at_ && now >= at_; }

 private:
  uint32_t at_ = 0;
};

// Something drawn that changes only every `every` ms (a spinner): moved()
// is true once in each step, next() is when the following one starts.
class Steps {
 public:
  explicit Steps(uint32_t every = kSpinnerStepMs) : every_(every) {}
  bool moved(uint32_t ms) {
    uint32_t step = ms / every_ + 1;  // never 0, so the first call moves
    if (step == step_) return false;
    step_ = step;
    return true;
  }
  uint32_t next(uint32_t ms) const { return (ms / every_ + 1) * every_; }
  // Each tick while it shows: moved(), with the next step put in `due`.
  bool tick(uint32_t ms, Due& due) {
    due.at(next(ms));
    return moved(ms);
  }

 private:
  uint32_t every_;
  uint32_t step_ = 0;
};

// One turn of the main loop: what it knows before it waits for events.
struct Loop {
  uint32_t now = 0;
  bool animating = false;  // drawing every frame (a slide, a fade)
  bool okHeld = false;     // OK is down, not yet a long press
  bool dirty = false;      // a redraw is asked for
  bool poll = false;       // a channel is opening or recovering
  uint32_t due = 0;        // the screen's (or the app's) next moment, 0 none
};

// How long the loop may wait for an event, ms (0: not at all).
inline int sleepMs(const Loop& l) {
  if (l.animating || (l.dirty && !l.okHeld)) return 0;
  uint32_t ms = l.okHeld ? kHoldPollMs : kIdleMs;
  if (l.poll) ms = std::min(ms, kPollMs);
  if (l.due) ms = std::min(ms, l.due > l.now ? l.due - l.now : 0);
  return static_cast<int>(ms);
}

}  // namespace marigold::wake
