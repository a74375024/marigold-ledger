#include "core/catalog/wiki_data.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;

TEST_CASE("wiki data: markets, their call signs and stations, saved and read back") {
  wiki::Data d;
  d.made = "2026-10-28";
  d.markets = {{"Harbor City–Valley", std::string("Template:Harbor City TV")}, {"Ridge Falls", std::nullopt}};
  d.callsigns[markets::nameKey("Harbor City–Valley")] = {"WHAR", "KVAL"};
  guide::Station s;
  s.callsign = "WHAR";
  s.network = "NBC";
  s.cityTokens = {"harbor", "city"};
  s.channelNumber = "5";
  d.stations = {s};
  auto back = wiki::fromJson(wiki::toJson(d));
  REQUIRE(back);
  CHECK(back->made == "2026-10-28");
  REQUIRE(back->markets.size() == 2);
  CHECK(back->markets[0].templateName == "Template:Harbor City TV");
  CHECK_FALSE(back->markets[1].templateName);
  // Found by either spelling of the market.
  REQUIRE(back->callsignsOf("Harbor City-Valley"));
  CHECK(*back->callsignsOf("Harbor City-Valley") == std::vector<std::string>{"WHAR", "KVAL"});
  CHECK_FALSE(back->callsignsOf("Ridge Falls"));
  CHECK(back->stations == d.stations);
  CHECK_FALSE(wiki::fromJson("not json"));
}
