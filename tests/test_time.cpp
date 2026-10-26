#include "core/util/time.hpp"

#include "doctest.h"

using namespace marigold::time;

TEST_CASE("civil date round trip") {
  CHECK(daysFromCivil(1970, 1, 1) == 0);
  CHECK(weekday(1970, 1, 1) == 4);
  CHECK(weekday(2026, 10, 26) == 1);  // a Monday
  Civil c = civilFromSeconds(secondsFromCivil({2026, 2, 28, 23, 59, 30}));
  CHECK(c.year == 2026);
  CHECK(c.month == 2);
  CHECK(c.day == 28);
  CHECK(c.second == 30);
  CHECK(daysInMonth(2028, 2) == 29);
  CHECK(civilFromSeconds(-1).year == 1969);
}

TEST_CASE("US Eastern daylight time") {
  Zone ny = Zone::usEastern();
  // 2026: DST from Sunday March 8 to Sunday November 1.
  CHECK(ny.offsetAt(secondsFromCivil({2026, 7, 1, 12, 0, 0})) == -4 * 3600);
  CHECK(ny.offsetAt(secondsFromCivil({2026, 12, 1, 12, 0, 0})) == -5 * 3600);
  // 8:15 PM EDT on October 26 is 00:15 UTC on October 27.
  Seconds t = ny.fromLocal({2026, 10, 26, 20, 15, 0});
  Civil u = civilFromSeconds(t);
  CHECK(u.day == 27);
  CHECK(u.hour == 0);
  CHECK(u.minute == 15);
  // The night DST ends, 1:30 happens twice: the earlier (EDT) one is chosen.
  Seconds amb = ny.fromLocal({2026, 11, 1, 1, 30, 0});
  CHECK(civilFromSeconds(amb).hour == 5);
}

TEST_CASE("Europe and southern hemisphere") {
  Zone london = Zone::london();
  CHECK(london.offsetAt(secondsFromCivil({2026, 6, 1, 0, 0, 0})) == 3600);
  CHECK(london.offsetAt(secondsFromCivil({2026, 10, 26, 0, 0, 0})) == 0);
  Zone paris = *Zone::named("Europe/Paris");
  CHECK(paris.offsetAt(secondsFromCivil({2026, 7, 1, 0, 0, 0})) == 7200);
  Zone sydney = *Zone::named("Australia/Sydney");
  CHECK(sydney.offsetAt(secondsFromCivil({2026, 1, 15, 0, 0, 0})) == 11 * 3600);
  CHECK(sydney.offsetAt(secondsFromCivil({2026, 7, 15, 0, 0, 0})) == 10 * 3600);
  CHECK(Zone::named("America/Mexico_City")->offsetAt(0) == -6 * 3600);
  CHECK_FALSE(Zone::named("Mars/Olympus"));
}
