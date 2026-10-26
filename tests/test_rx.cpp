#include "core/util/rx.hpp"

#include "doctest.h"

TEST_CASE("rx test, first and groups") {
  CHECK(marigold::rx::test("\\bESPN\\b", "US: ESPN FHD"));
  CHECK_FALSE(marigold::rx::test("\\bESPN\\b", "ESPNEWS"));
  CHECK(marigold::rx::test("espn", "US: ESPN", true));
  auto m = marigold::rx::first("^([A-Z]{2})\\|\\s*(.+)$", "UK| Sky Sports Main");
  REQUIRE(m);
  CHECK(m->group(1) == "UK");
  CHECK(m->group(2) == "Sky Sports Main");
  CHECK_FALSE(m->has(3));
}

TEST_CASE("rx look-behind and Unicode word boundaries") {
  CHECK(marigold::rx::test("(?<=\\()[A-Z]{3}(?=\\))", "Canal (MEX)"));
  CHECK(marigold::rx::test("\\bTelemundo\\b", "Telemundo Pacífico"));
  CHECK(marigold::rx::test("\\bPacífico\\b", "Telemundo Pacífico"));
}

TEST_CASE("rx replace with templates") {
  CHECK(marigold::rx::replace("NBC 4 HD", "\\s+(HD|FHD)$", "") == "NBC 4");
  CHECK(marigold::rx::replace("a1b22", "(\\d+)", "<$1>") == "a<1>b<22>");
  CHECK(marigold::rx::replace("x", "y", "z") == "x");
}

TEST_CASE("rx all and invalid input") {
  auto ms = marigold::rx::all("\\d+", "1 22 333");
  REQUIRE(ms.size() == 3);
  CHECK(ms[2].group(0) == "333");
  CHECK_FALSE(marigold::rx::valid("(unclosed"));
  CHECK_FALSE(marigold::rx::test("(unclosed", "x"));
  // Invalid UTF-8 in provider names neither crashes nor matches as a letter.
  CHECK(marigold::rx::test("ESPN", std::string("\xff\xfe ESPN")));
}
