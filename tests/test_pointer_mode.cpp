#include "core/input/pointer_mode.hpp"

#include "doctest.h"

using marigold::pointer::kMovePx;
using marigold::pointer::kMoveWindowMs;
using marigold::pointer::kQuietMs;
using marigold::pointer::Mode;

namespace {

// Moves the pointer from (x, y) by (dx, dy) in `steps` events 16 ms apart,
// starting at `ms`. True when the pointer was in charge after the last one.
bool sweep(Mode& m, float x, float y, float dx, float dy, int steps, uint32_t ms) {
  bool on = false;
  for (int i = 0; i <= steps; ++i)
    on = m.moved(x + dx * static_cast<float>(i) / static_cast<float>(steps),
                 y + dy * static_cast<float>(i) / static_cast<float>(steps), ms + static_cast<uint32_t>(i) * 16);
  return on;
}

Mode pointing(uint32_t ms) {
  Mode m;
  CHECK(sweep(m, 900, 500, 80, 0, 5, ms));
  return m;
}

}  // namespace

TEST_CASE("the pointer starts away and real movement brings it") {
  Mode m;
  CHECK_FALSE(m.active());
  CHECK_FALSE(m.moved(900, 500, 1000));
  CHECK(sweep(m, 900, 500, 60, 20, 4, 1016));
  CHECK(m.active());
  // In charge, every motion is followed, however small.
  CHECK(m.moved(981, 520, 1200));
}

TEST_CASE("a key press puts the pointer away") {
  Mode m = pointing(1000);
  CHECK(m.key(2000));
  CHECK_FALSE(m.active());
  // Again: nothing changes.
  CHECK_FALSE(m.key(2100));
  CHECK_FALSE(m.hidden());
}

TEST_CASE("the system hiding its cursor puts the pointer away") {
  Mode m = pointing(1000);
  CHECK(m.hidden());
  CHECK_FALSE(m.active());
  CHECK_FALSE(m.moved(981, 500, 1500));
}

TEST_CASE("a resting hand does not bring the pointer back") {
  Mode m = pointing(1000);
  m.key(2000);
  // Tremor around one spot for three seconds: a few pixels back and forth.
  uint32_t ms = 2000 + kQuietMs;
  for (int i = 0; i < 180; ++i, ms += 16) {
    float wobble = (i % 4 < 2 ? 1.0f : -1.0f) * 3.0f;
    CHECK_FALSE(m.moved(980 + wobble, 500 - wobble, ms));
  }
  CHECK_FALSE(m.active());
}

TEST_CASE("slow drift does not add up") {
  Mode m;
  m.key(0);
  // 4 px per 100 ms (40 px a second) keeps starting new stretches.
  uint32_t ms = kQuietMs + 10;
  float x = 500;
  for (int i = 0; i < 30; ++i, ms += 100, x += 4) CHECK_FALSE(m.moved(x, 300, ms));
}

TEST_CASE("just over the threshold within the window brings it back") {
  Mode m;
  m.key(0);
  uint32_t ms = kQuietMs + 100;
  CHECK_FALSE(m.moved(500, 300, ms));
  CHECK_FALSE(m.moved(500 + kMovePx, 300, ms + 50));
  CHECK(m.moved(500 + kMovePx + 1, 300, ms + 100));
}

TEST_CASE("the same distance spread over more than the window does not") {
  Mode m;
  m.key(0);
  uint32_t ms = kQuietMs + 100;
  CHECK_FALSE(m.moved(500, 300, ms));
  CHECK_FALSE(m.moved(500 + kMovePx + 5, 300, ms + kMoveWindowMs + 1));
  CHECK_FALSE(m.active());
}

TEST_CASE("motion right after a key is ignored, however large") {
  Mode m = pointing(1000);
  m.key(2000);
  // The jolt of the press, or a stale motion the TV sends after it.
  CHECK_FALSE(sweep(m, 980, 500, 200, 0, 10, 2000));
  CHECK_FALSE(m.active());
  // ... and it does not count towards the movement after the quiet time.
  CHECK_FALSE(m.moved(1180, 500, 2000 + kQuietMs));
  CHECK(sweep(m, 1180, 500, -40, 0, 4, 2000 + kQuietMs + 16));
}

TEST_CASE("each key press starts the quiet time again") {
  Mode m;
  uint32_t ms = 1000;
  // Arrows a quarter of a second apart, the remote jolting with each.
  for (int i = 0; i < 8; ++i, ms += 250) {
    m.key(ms);
    CHECK_FALSE(sweep(m, 900, 500, 30, 0, 3, ms + 100));
  }
  CHECK_FALSE(m.active());
}

TEST_CASE("a click while the pointer is away is OK on the focused item") {
  Mode m = pointing(1000);
  m.key(2000);
  CHECK_FALSE(m.click(3000));
  CHECK_FALSE(m.active());
  // It counts as a key press: its jolt is quiet too.
  CHECK_FALSE(sweep(m, 900, 500, 40, 0, 3, 3100));
}

TEST_CASE("a click while pointing is a click at the pointer") {
  Mode m = pointing(1000);
  CHECK(m.click(1500));
  CHECK(m.active());
}

TEST_CASE("the system showing its cursor right after a key is the key's jolt") {
  Mode m = pointing(1000);
  m.key(2000);
  CHECK(m.shownByKey(2000 + 40));
  CHECK_FALSE(m.shownByKey(2000 + kQuietMs));
  // While pointing, or before any key, it is the hand.
  Mode fresh;
  CHECK_FALSE(fresh.shownByKey(10));
  Mode on = pointing(1000);
  CHECK_FALSE(on.shownByKey(1100));
}

TEST_CASE("the clock wrapping around does not stick the pointer away") {
  Mode m;
  m.key(0xFFFFFF00u);
  CHECK_FALSE(m.moved(500, 300, 0xFFFFFF00u + 100));
  uint32_t ms = 0xFFFFFF00u + kQuietMs;  // past zero
  CHECK_FALSE(m.moved(500, 300, ms));
  CHECK(m.moved(540, 300, ms + 32));
}
