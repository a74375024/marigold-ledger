// A countdown in whole seconds ("Hides in 3 s" on the player's banner):
// what it says at a moment, and when it next says something else, so the
// screen sleeps in between instead of drawing every frame (Build 119).
#pragma once

#include <algorithm>
#include <cstdint>

namespace marigold::countdown {

// The seconds shown at `ms` for a countdown ending at `until` (app ms),
// counting the last one: 5 for the whole of the first second of five, 1
// in the last one and after.
inline int shown(uint32_t until, uint32_t ms) { return static_cast<int>((until > ms ? until - ms : 0) / 1000) + 1; }

// When shown() next changes, or the countdown ends (whichever is first);
// 0 once it has ended.
inline uint32_t nextChange(uint32_t until, uint32_t ms) {
  if (ms >= until) return 0;
  return std::min(ms + (until - ms) % 1000 + 1, until);
}

}  // namespace marigold::countdown
