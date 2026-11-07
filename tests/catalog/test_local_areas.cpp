// Build 148: with stations of two or more picked areas in My Lineup, each
// area is a neighborhood of its own (home first, then the order picked); one
// area keeps Local exactly as before.
#include <string>
#include <vector>

#include "catalog/helpers.hpp"
#include "core/catalog/lineup.hpp"
#include "core/catalog/reference.hpp"
#include "core/catalog/work.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::channel;

namespace {

struct Station {
  const char* id;
  const char* sign;
  int number;
  LineupCategory category = LineupCategory::local;
};

MarketChoice market(const std::string& name, std::vector<std::string> signs) {
  MarketChoice m;
  m.name = name;
  m.callsigns = std::move(signs);
  return m;
}

// Made-up areas (no reference stations of their own) and their stations.
const MarketChoice kHarbor = market("Harbor City\xe2\x80\x93" "Bayside", {"WHAA", "WHBB-TV", "WSHR"});
const MarketChoice kRiver = market("River Town", {"WRAA", "WRBB", "WSHR"});
const MarketChoice kHill = market("Hill Valley-Dale", {"KHAA"});

LineupInput input(const std::vector<Station>& stations, std::vector<MarketChoice> markets) {
  LineupInput in;
  for (const Station& s : stations) {
    EntryInfo e;
    e.id = std::string("e|cs:") + s.id;
    e.name = s.id;
    e.category = s.category;
    if (s.sign[0]) e.callsign = s.sign;
    if (s.number) e.virtualNumber = s.number;
    e.market = "US";
    e.members = {std::string("xt:1|") + s.id};
    in.infos.push_back(e);
    in.channels.push_back(channel(s.id, s.id));
  }
  for (size_t i = 0; i < in.channels.size(); ++i) in.indexByID[in.channels[i].id] = i;
  in.prefs.configured = true;
  in.prefs.markets = std::move(markets);
  in.otherGroup = "Other";
  return in;
}

// Each entry's id and neighborhood, in guide order ("WHAA:Harbor City").
std::vector<std::string> orderOf(const LineupOutput& out) {
  std::vector<std::string> rows;
  for (const Channel& c : out.entries) {
    const std::string* area = out.areaSection(c.id);
    rows.push_back(c.id.substr(5) + ":" + (area ? *area : std::string("-")));
  }
  return rows;
}

const std::vector<Station> kStations = {
    {"WHBB", "WHBB-DT", 4, LineupCategory::local},  // Harbor City's 4
    {"WRAA", "WRAA", 2, LineupCategory::local},     // River Town's 2
    {"WRBB", "WRBB", 4, LineupCategory::local},     // River Town's 4
    {"WHAA", "WHAA", 7, LineupCategory::local},     // Harbor City's 7
    {"WOUT", "WOUT", 3, LineupCategory::local},     // in no picked area
    {"NEWS", "", 0, LineupCategory::news},
};

}  // namespace

TEST_CASE("local areas: short names, home first, a station in two areas") {
  CHECK(shortMarketName("San Francisco\xe2\x80\x93Oakland\xe2\x80\x93San Jose") == "San Francisco");
  CHECK(shortMarketName("Hill Valley-Dale") == "Hill Valley");
  CHECK(shortMarketName("New York") == "New York");

  LineupPrefs prefs;
  prefs.markets = {kHarbor};
  // One area: nothing to tell apart.
  CHECK(localAreas(prefs).names.empty());
  CHECK(localAreas(prefs).of(std::string("WHAA")) == -1);

  prefs.markets = {kHarbor, kRiver};
  LocalAreas areas = localAreas(prefs);
  CHECK(areas.names == std::vector<std::string>{"Harbor City", "River Town"});
  CHECK(areas.of(std::string("WHBB")) == 0);
  CHECK(areas.of(std::string("WHBB-DT")) == 0);
  CHECK(areas.of(std::string("WRAA")) == 1);
  CHECK(areas.of(std::string("WOUT")) == -1);
  CHECK(areas.of(std::nullopt) == -1);
  // In both: home's.
  CHECK(areas.of(std::string("WSHR")) == 0);

  // Home is River Town: it comes first and keeps the shared station.
  prefs.homeMarket = kRiver.name;
  areas = localAreas(prefs);
  CHECK(areas.names == std::vector<std::string>{"River Town", "Harbor City"});
  CHECK(areas.of(std::string("WSHR")) == 0);
  CHECK(areas.of(std::string("WHAA")) == 1);

  // Home is in neither of the station's areas: the first picked has it.
  prefs.markets = {kHill, kHarbor, kRiver};
  prefs.homeMarket = kHill.name;
  areas = localAreas(prefs);
  CHECK(areas.names == std::vector<std::string>{"Hill Valley", "Harbor City", "River Town"});
  CHECK(areas.of(std::string("WSHR")) == 1);
  // A home that isn't picked (any more): the first picked is home.
  prefs.homeMarket = "Nowhere";
  CHECK(localAreas(prefs).names.front() == "Hill Valley");

  // Two areas of one short name stay apart (the second by its whole name).
  prefs.markets = {market("Twin City-North", {"WTAA"}), market("Twin City-South", {"WTBB"})};
  prefs.homeMarket.reset();
  CHECK(localAreas(prefs).names == std::vector<std::string>{"Twin City", "Twin City-South"});
}

TEST_CASE("local areas: the reference data's stations of a real area count") {
  const reference::Index& index = reference::builtInIndex();
  if (index.stationsIn("New York").empty() || index.stationsIn("Indianapolis").empty()) return;  // built without
  LineupPrefs prefs;
  prefs.markets = {market("New York", {}), market("Indianapolis", {})};
  LocalAreas areas = localAreas(prefs);
  CHECK(areas.of(std::string("WNBC")) == 0);
  CHECK(areas.of(std::string("WABC")) == 0);  // the reference's WABC-TV, bare
  CHECK(areas.of(std::string("WRTV")) == 1);
  CHECK(areas.of(std::string("WTHR")) == 1);
  CHECK(areas.of(std::string("KGO")) == -1);
}

TEST_CASE("lineup: one area with stations keeps Local exactly as before") {
  // One picked area.
  LineupInput in = input(kStations, {kHarbor});
  in.prefs.added = {"e|cs:WHBB", "e|cs:WHAA", "e|cs:WOUT"};
  LineupOutput out = lineup(in);
  CHECK(orderOf(out) ==
        std::vector<std::string>{"WRAA:-", "WOUT:-", "WHBB:-", "WRBB:-", "WHAA:-", "NEWS:-"});
  CHECK(out.areaNames.empty());
  CHECK(out.areaOf.empty());

  // Two picked, but My Lineup holds only one's stations (the other's
  // removed): the same, number order across the list.
  LineupInput two = input(kStations, {kHarbor, kRiver});
  two.prefs.added = {"e|cs:WHBB", "e|cs:WHAA", "e|cs:WOUT"};
  two.prefs.removed = {"e|cs:WRAA", "e|cs:WRBB"};
  LineupOutput same = lineup(two);
  CHECK(orderOf(same) == orderOf(out));
  CHECK(same.areaNames.empty());

  // A second area's station that has no channel in the list doesn't count.
  two.prefs.removed = {"e|cs:WRBB"};
  two.infos[1].members = {"xt:1|gone"};
  CHECK(lineup(two).areaNames.empty());
}

TEST_CASE("lineup: two areas with stations, each its own neighborhood, home first") {
  LineupInput in = input(kStations, {kHarbor, kRiver});
  // A local of no picked area added by hand, and one the area lists as
  // WHBB-TV (suggestions go by the list's own spelling).
  in.prefs.added = {"e|cs:WOUT", "e|cs:WHBB"};
  LineupOutput out = lineup(in);
  CHECK(out.areaNames == std::vector<std::string>{"Harbor City", "River Town"});
  // Harbor City (home: the first picked) in number order, then River Town,
  // then Local's own station, then the rest.
  CHECK(orderOf(out) == std::vector<std::string>{"WHBB:Harbor City", "WHAA:Harbor City", "WRAA:River Town",
                                               "WRBB:River Town", "WOUT:-", "NEWS:-"});
  // Number 4 twice: the first with it is home's.
  std::string first4;
  for (const Channel& c : out.entries) {
    if (c.id == "e|cs:WHBB" || c.id == "e|cs:WRBB") {
      first4 = c.id;
      break;
    }
  }
  CHECK(first4 == "e|cs:WHBB");
  // The lineup and browse lists follow the same order.
  REQUIRE(out.lineupEntries.size() == 5);
  CHECK(out.lineupEntries[0].id == "e|cs:WHBB");
  CHECK(out.lineupEntries[2].id == "e|cs:WRAA");
  CHECK(out.lineupEntries[4].id == "e|cs:WOUT");
  CHECK(out.areaSection("e|cs:NEWS") == nullptr);
  CHECK(out.areaSection("e|cs:WOUT") == nullptr);

  // Home changed: River Town first, and its 4 is the first 4.
  in.prefs.homeMarket = kRiver.name;
  LineupOutput home = lineup(in);
  CHECK(home.areaNames == std::vector<std::string>{"River Town", "Harbor City"});
  CHECK(orderOf(home) == std::vector<std::string>{"WRAA:River Town", "WRBB:River Town", "WHBB:Harbor City",
                                                "WHAA:Harbor City", "WOUT:-", "NEWS:-"});
}

TEST_CASE("lineup: three areas, and a station in two of them") {
  std::vector<Station> stations = kStations;
  stations.push_back({"KHAA", "KHAA", 5, LineupCategory::local});
  stations.push_back({"WSHR", "WSHR", 1, LineupCategory::local});  // Harbor City's and River Town's
  // Not a local (another neighborhood): never an area's, whatever its sign.
  stations.push_back({"WHSP", "WHAA", 9, LineupCategory::spanish});
  LineupInput in = input(stations, {kRiver, kHill, kHarbor});
  in.prefs.homeMarket = kHill.name;
  LineupOutput out = lineup(in);
  CHECK(out.areaNames == std::vector<std::string>{"Hill Valley", "River Town", "Harbor City"});
  // Home has no part in WSHR: it goes with River Town, picked before Harbor City.
  CHECK(orderOf(out) == std::vector<std::string>{"KHAA:Hill Valley", "WSHR:River Town", "WRAA:River Town",
                                               "WRBB:River Town", "WHBB:Harbor City", "WHAA:Harbor City",
                                               "WOUT:-", "NEWS:-", "WHSP:-"});
  // Home Harbor City: it has WSHR.
  in.prefs.homeMarket = kHarbor.name;
  out = lineup(in);
  CHECK(orderOf(out) == std::vector<std::string>{"WSHR:Harbor City", "WHBB:Harbor City", "WHAA:Harbor City",
                                               "WRAA:River Town", "WRBB:River Town", "KHAA:Hill Valley",
                                               "WOUT:-", "NEWS:-", "WHSP:-"});
}
