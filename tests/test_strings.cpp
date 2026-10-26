#include "core/util/strings.hpp"

#include "doctest.h"

using namespace marigold::str;

TEST_CASE("folding and case") {
  CHECK(foldDiacritics("Telemundo Pacífico") == "Telemundo Pacifico");
  CHECK(foldDiacritics("ÑUÑOA Łódź Œuvre") == "NUNOA Lodz OEuvre");
  CHECK(foldDiacritics("Canal+ Sport") == "Canal+ Sport");
  CHECK(lowerAscii("ESPN FHD") == "espn fhd");
  CHECK(iequals("Fox", "FOX"));
  CHECK(icontains("US: Fox Sports 1", "sports"));
}

TEST_CASE("split, trim, replace") {
  auto parts = split("a||b|c", '|');
  REQUIRE(parts.size() == 3);
  CHECK(parts[1] == "b");
  CHECK(split("a||b", '|', true).size() == 3);
  CHECK(trim("  x y \t") == "x y");
  CHECK(replaceAll("a&b&c", "&", " and ") == "a and b and c");
}

TEST_CASE("utf8 and hashing") {
  CHECK(utf8Length("Pacífico") == 8);
  CHECK(utf8Valid("Pacífico"));
  CHECK_FALSE(utf8Valid(std::string("\xff")));
  CHECK(fnv1a64("") == 1469598103934665603ull);
  CHECK(hex64(255) == "00000000000000ff");
}
