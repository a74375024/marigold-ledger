// The built-in market list (used when the Wikipedia list cannot be read).
#include "core/catalog/markets.hpp"
#include "doctest.h"

TEST_CASE("markets: the built-in list has every US market, biggest first") {
  const auto& list = marigold::catalog::markets::builtinMarkets();
  CHECK(list.size() == 210);
  CHECK(list.front().name == "New York");
  CHECK(list[9].name == "San Francisco-Oakland-San Jose");
  // Suggestions work on it like on the page's list.
  auto picks = marigold::catalog::markets::suggested(list, {"FOX 2 San Francisco", "KTVU San Francisco", "NBC Bay Area"});
  REQUIRE_FALSE(picks.empty());
  CHECK(picks.front().name == "San Francisco-Oakland-San Jose");
}
