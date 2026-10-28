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

TEST_CASE("markets: the built-in and the list page's spellings share a key") {
  using marigold::catalog::markets::nameKey;
  CHECK(nameKey("San Francisco-Oakland-San Jose") == nameKey("San Francisco\xe2\x80\x93Oakland\xe2\x80\x93San Jose"));
  CHECK(nameKey("Washington, DC (Hagerstown)") == nameKey("Washington, D.C. (Hagerstown)"));
  CHECK(nameKey("Dallas-Ft. Worth") != nameKey("Houston"));
}
