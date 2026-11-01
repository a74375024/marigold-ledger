// The player banner's "Hides in N s" (Build 119): 5 s with or without a
// description, drawn again only when the number changes or it ends.
#include "core/input/countdown.hpp"
#include "doctest.h"

namespace countdown = marigold::countdown;

TEST_CASE("countdown: the seconds shown, and when they next change") {
  const uint32_t from = 12345, until = from + 5000;
  CHECK(countdown::shown(until, from) == 6);  // the first instant (as before: shown as 5)
  CHECK(countdown::shown(until, from + 1) == 5);
  CHECK(countdown::shown(until, from + 1000) == 5);
  CHECK(countdown::shown(until, from + 1001) == 4);
  CHECK(countdown::shown(until, until - 1) == 1);
  CHECK(countdown::shown(until, until) == 1);
  CHECK(countdown::shown(until, until + 300) == 1);

  // Waking at each change: one draw a second, the last as it ends, and
  // the number the same all the way up to each wake.
  uint32_t ms = from;
  int wakes = 0;
  while (uint32_t next = countdown::nextChange(until, ms)) {
    REQUIRE(next > ms);
    REQUIRE(next <= until);
    for (uint32_t t = ms; t < next; t += 7) CHECK(countdown::shown(until, t) == countdown::shown(until, ms));
    if (next < until) CHECK(countdown::shown(until, next) == countdown::shown(until, ms) - 1);
    ms = next;
    ++wakes;
  }
  CHECK(ms == until);
  CHECK(wakes == 6);  // the first instant's 6 (shown as 5), then 5, 4, 3, 2, 1
  CHECK(countdown::nextChange(until, until) == 0);
  CHECK(countdown::nextChange(until, until + 5) == 0);
  // From partway through a second.
  CHECK(countdown::nextChange(until, from + 250) == from + 1001);
  CHECK(countdown::nextChange(until, until - 400) == until);
}
