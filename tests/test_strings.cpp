#include "core/util/strings.hpp"

#include "core/model/channel.hpp"

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

TEST_CASE("Unicode case mapping like Swift lowercased / uppercased") {
  CHECK(lower("TELEMUNDO PACÍFICO") == "telemundo pacífico");
  CHECK(upper("telemundo pacífico") == "TELEMUNDO PACÍFICO");
  CHECK(lower("ŁÓDŹ ŻAR") == "łódź żar");
  CHECK(upper("łódź") == "ŁÓDŹ");
  CHECK(lower("ΑΘΉΝΑ") == "αθήνα");
  CHECK(upper("москва") == "МОСКВА");
  CHECK(lower("ПЕРВЫЙ Ё") == "первый ё");
  CHECK(upper("straße") == "STRASSE");
  CHECK(lower("ESPN ᴴᴰ") == "espn ᴴᴰ");  // modifier letters have no case pair
  CHECK(lower(std::string("A\xff" "Z")) == std::string("a\xff" "z"));
}

TEST_CASE("trimUnicode is Swift trimmingCharacters") {
  CHECK(trimUnicode("\xC2\xA0 x y \t") == "x y");
  CHECK(trimUnicode(" x\n") == "x\n");
  CHECK(trimUnicode(" x\n", true) == "x");
  CHECK(trimUnicode("| – NBC –:", false, "|-–:,") == "NBC");
  CHECK(trimUnicode("::", false, ":") == "");
  CHECK(trimUnicode("") == "");
  CHECK(trimUnicode("ü") == "ü");
}

TEST_CASE("Program minutes is at least one") {
  marigold::Program p;
  p.start = 1000;
  p.end = 1030;
  CHECK(p.minutes() == 1);
  p.end = 1000 + 3600;
  CHECK(p.minutes() == 60);
  p.end = 900;
  CHECK(p.minutes() == 1);
}
