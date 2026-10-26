// ChannelCatalog.build: event families and their thresholds, entries
// (clusters of variants) and their order, events, and rename-only refreshes.
#include <algorithm>

#include "catalog/helpers.hpp"
#include "core/catalog/catalog.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::channel;
using testdata::utc;
using testdata::wednesdayNoon;

namespace {

const EntryInfo* entryNamed(const CatalogResult& r, const std::string& name) {
  for (const EntryInfo& e : r.entries) {
    if (e.name == name) return &e;
  }
  return nullptr;
}

const EventFamily* family(const CatalogResult& r, const std::string& id) {
  for (const EventFamily& f : r.families) {
    if (f.id == id) return &f;
  }
  return nullptr;
}

std::vector<Channel> numbered(const std::string& head, int count, const std::string& group,
                              int withSeparator, const std::string& prefix = "") {
  std::vector<Channel> list;
  for (int i = 1; i <= count; ++i) {
    std::string name = head + " " + std::to_string(i);
    if (i <= withSeparator) name += ": Team " + std::to_string(i) + " vs Team X";
    list.push_back(channel(prefix + head + std::to_string(i), name, group, "jf:1"));
  }
  return list;
}

}  // namespace

TEST_CASE("families: 15+ numbered feeds always count") {
  CatalogResult r = build(numbered("Slot", 15, "Misc", 0), wednesdayNoon());
  for (const auto& [id, t] : r.traits) {
    CHECK(t.familyKey == std::string("slot #"));
    CHECK(t.kind == ChannelKind::event);
  }
  REQUIRE(r.families.size() == 1);
  CHECK(r.families[0].id == "fam|slot #");
  CHECK(r.families[0].name == "Slot");
  CHECK(r.families[0].feeds.size() == 15);
  CHECK(r.entries.empty());
}

TEST_CASE("families: 14 bare numbered feeds are not a numbered family") {
  CatalogResult r = build(numbered("Slot", 14, "Misc", 0), wednesdayNoon());
  for (const auto& [id, t] : r.traits) CHECK(t.familyKey != std::string("slot #"));
  // Not a family, so they read as plain channels (tvOS kept them as event
  // slots of a "grp:" family; Marigold reads the name again).
  CHECK_FALSE(family(r, "fam|grp:misc"));
  for (const auto& [id, t] : r.traits) CHECK(t.kind == ChannelKind::channel);
}

TEST_CASE("a network channel ending in a number is a channel, not an event slot") {
  std::vector<Channel> list = {channel("1", "US: Telemundo 47"), channel("2", "US: Univision 41"),
                               channel("3", "US: Fox 23")};
  CatalogResult r = build(list, wednesdayNoon());
  for (const auto& [id, t] : r.traits) {
    CHECK(t.kind == ChannelKind::channel);
    CHECK_FALSE(t.familyKey);
  }
  CHECK(r.families.empty());
  CHECK(r.entries.size() == 3);
}

TEST_CASE("families: 5+ feeds with at least 40% named NN: ...") {
  CatalogResult two = build(numbered("Feed", 5, "Misc", 2), wednesdayNoon());
  CHECK(family(two, "fam|feed #"));
  CatalogResult one = build(numbered("Feed", 5, "Misc", 1), wednesdayNoon());
  CHECK_FALSE(family(one, "fam|feed #"));
  CatalogResult four = build(numbered("Feed", 4, "Misc", 4), wednesdayNoon());
  CHECK_FALSE(family(four, "fam|feed #"));
  CHECK(family(four, "fam|grp:misc"));
}

TEST_CASE("numbered regular channels stay channels when they are locals or core") {
  std::vector<Channel> list = {channel("1", "US: ESPN 2"), channel("2", "US: NBC 4", "US Locals"),
                               channel("3", "US: Fox 23 (KXYZ)")};
  CatalogResult r = build(list, wednesdayNoon());
  CHECK(r.traits["xt:1|1"].kind == ChannelKind::channel);
  CHECK(r.traits["xt:1|1"].core == std::string("ESPN2"));
  CHECK(r.traits["xt:1|2"].kind == ChannelKind::channel);
  CHECK(r.traits["xt:1|3"].kind == ChannelKind::channel);
  CHECK(r.entries.size() == 3);
}

TEST_CASE("events from a family, deduplicated and filtered") {
  std::vector<Channel> list = {
      channel("e1", "ESPN+ 01: Team A vs Team B 7:30 PM ET", "US| ESPN+"),
      channel("e2", "ESPN+ 02: Team A @ Team B 7:30PM", "US| ESPN+"),
      channel("e3", "ESPN+ 03: NO EVENT", "US| ESPN+"),
      channel("e4", "ESPN+ 04: Team C vs Team D Oct 30 8:00PM ET", "US| ESPN+"),  // 9 days off
      channel("e5", "ESPN+ 05: Team E vs Team F Oct 20 9:00AM ET", "US| ESPN+"),  // long over
      channel("e6", "ESPN+ 06", "US| ESPN+"),
      channel("e7", "ESPN+ 07: Ended | Team G vs Team H", "US| ESPN+"),
  };
  CatalogResult r = build(list, wednesdayNoon());
  const EventFamily* fam = family(r, "fam|espn+ #");
  REQUIRE(fam);
  CHECK(fam->name == "Espn+");
  CHECK(fam->feeds.size() == 7);
  CHECK(fam->feeds.front() == "xt:1|e1");
  CHECK(fam->market == std::string("US"));
  CHECK(fam->hasNames);  // 4 named of 7: 16 >= 7
  REQUIRE(r.events.size() == 1);
  const LiveEvent& e = r.events[0];
  CHECK(e.title == "Team A vs Team B");
  CHECK(e.start == utc(2026, 10, 21, 23, 30));
  CHECK(e.startGuessed == true);
  CHECK(e.feeds == std::vector<std::string>{"xt:1|e1", "xt:1|e2"});
  CHECK(e.family == "espn+ #");
  CHECK(e.market == std::string("US"));
  CHECK(e.id == "ev|" + eventKey("Team A vs Team B", e.start));
  CHECK(r.traits["xt:1|e3"].kind == ChannelKind::placeholder);
  CHECK(r.entries.empty());
}

TEST_CASE("event slots in an event group form one family per group") {
  std::vector<Channel> list = {
      channel("p1", "Team A vs Team B", "PPV Events", "jf:1"),
      channel("p2", "Fighter A vs Fighter B 9PM", "PPV Events", "jf:1"),
      channel("p3", "NO EVENT STREAMING", "PPV Events", "jf:1"),
      channel("p4", "NO EVENT STREAMING", "Movies", "jf:1"),
  };
  CatalogResult r = build(list, wednesdayNoon());
  const EventFamily* fam = family(r, "fam|grp:ppv events");
  REQUIRE(fam);
  CHECK(fam->name == "PPV Events");
  CHECK(fam->feeds.size() == 3);  // the placeholder in the event group too
  CHECK(r.events.size() == 2);
  // Timed first, then untimed by title.
  CHECK(r.events[0].start);
  CHECK_FALSE(r.events[1].start);
  CHECK_FALSE(r.events[1].startGuessed);
  // A placeholder outside an event group is hidden: no entry, no family.
  CHECK(r.traits["jf:1|p4"].kind == ChannelKind::placeholder);
  CHECK_FALSE(r.traits["jf:1|p4"].familyKey);
  CHECK(r.entries.empty());
}

TEST_CASE("grp family names drop the country prefix") {
  std::vector<Channel> list = {channel("p1", "Team A vs Team B", "US| PPV  EVENTS")};
  CatalogResult r = build(list, wednesdayNoon());
  REQUIRE(r.families.size() == 1);
  CHECK(r.families[0].name == "PPV EVENTS");
  CHECK(r.families[0].league == "Other");
}

TEST_CASE("entries: variants clustered, Xtream first, then favorite, East, quality, short name") {
  std::vector<Channel> list = {
      channel("1", "US: Dream Channel"),
      channel("2", "US: Dream Channel FHD"),
      channel("3", "Dream Channel HD", "US Entertainment", "jf:1"),
      channel("4", "US: Dream Channel West"),
      channel("5", "US: DREAM CHANNEL 4K"),
  };
  list[2].isFavorite = true;
  CatalogResult r = build(list, wednesdayNoon());
  REQUIRE(r.entries.size() == 2);
  const EntryInfo& main = r.entries[0];
  CHECK(main.id == "e|n:US:dreamchannel");
  CHECK(main.members == std::vector<std::string>{"xt:1|5", "xt:1|2", "xt:1|1", "jf:1|3"});
  CHECK(main.name == "Dream Channel");  // mixed case, shortest
  CHECK(main.category == LineupCategory::entertainment);
  CHECK(r.entries[1].id == "e|n:US:dreamchannel~west");
  CHECK(r.entryOf.at("jf:1|3") == main.id);
  CHECK(r.entryOf.at("xt:1|4") == r.entries[1].id);
}

TEST_CASE("favorites and West break ties among Xtream variants") {
  std::vector<Channel> list = {channel("1", "US: CNN FHD"), channel("2", "US: CNN"),
                               channel("3", "US: CNN Backup")};
  list[2].isFavorite = true;
  CatalogResult r = build(list, wednesdayNoon());
  REQUIRE(r.entries.size() == 1);
  CHECK(r.entries[0].name == "CNN");
  CHECK(r.entries[0].core == std::string("CNN"));
  CHECK(r.entries[0].members == std::vector<std::string>{"xt:1|3", "xt:1|1", "xt:1|2"});
}

TEST_CASE("core entries are named after the network; locals get their virtual number") {
  std::vector<Channel> list = {channel("1", "US: HBO West HD"), channel("2", "USA  HBO EAST"),
                               channel("3", "US: NBC 11 (KXYZ)", "US Locals")};
  CatalogResult r = build(list, wednesdayNoon());
  const EntryInfo* west = entryNamed(r, "HBO West");
  REQUIRE(west);
  CHECK(west->id == "e|core:HBO~west");
  REQUIRE(entryNamed(r, "HBO"));
  const EntryInfo* local = entryNamed(r, "NBC 11 (KXYZ)");
  REQUIRE(local);
  CHECK(local->callsign == std::string("KXYZ"));
  CHECK(local->virtualNumber == 11);
  CHECK(local->scope == "local");
}

TEST_CASE("separators and placeholders are hidden") {
  std::vector<Channel> list = {channel("1", "#### PRIME ####"), channel("2", "NO STREAM"),
                               channel("3", "US: CNN")};
  CatalogResult r = build(list, wednesdayNoon());
  CHECK(r.entries.size() == 1);
  CHECK(r.families.empty());
  CHECK(r.traits.size() == 3);
}

TEST_CASE("refreshEvents re-reads renamed slots only") {
  std::vector<Channel> list = {
      channel("e1", "ESPN+ 01: Team A vs Team B 7:30 PM ET", "US| ESPN+"),
      channel("e2", "ESPN+ 02", "US| ESPN+"),
      channel("e3", "ESPN+ 03", "US| ESPN+"),
      channel("e4", "ESPN+ 04", "US| ESPN+"),
      channel("e5", "ESPN+ 05: Team C vs Team D 9:00 PM ET", "US| ESPN+"),
      channel("g1", "Team X vs Team Y", "PPV Events"),
      channel("c1", "US: CNN"),
  };
  CatalogResult r = build(list, wednesdayNoon());
  REQUIRE(r.events.size() == 3);
  std::vector<Channel> renamed = {
      channel("e2", "ESPN+ 02: Team G vs Team H 10:00 PM ET", "US| ESPN+"),
      channel("e5", "ESPN+ 05: NO EVENT", "US| ESPN+"),
      channel("g1", "Team Z vs Team W 8PM", "PPV Events"), channel("c1", "US: CNN HD")};
  std::vector<Channel> fresh = list;
  fresh[1] = renamed[0];
  fresh[4] = renamed[1];
  fresh[5] = renamed[2];
  refreshEvents(r, fresh, renamed, wednesdayNoon());
  std::vector<std::string> titles;
  for (const LiveEvent& e : r.events) titles.push_back(e.title);
  std::sort(titles.begin(), titles.end());
  CHECK(titles ==
        std::vector<std::string>{"Team A vs Team B", "Team G vs Team H", "Team Z vs Team W"});
  CHECK(r.traits["xt:1|e2"].eventText == std::string("Team G vs Team H 10:00 PM ET"));
  CHECK(r.traits["xt:1|e2"].familyNumber == 2);
  CHECK(r.traits["xt:1|e5"].kind == ChannelKind::placeholder);
  CHECK(r.traits["xt:1|g1"].eventText == std::string("Team Z vs Team W 8PM"));
  // Regular channels are not touched.
  CHECK_FALSE(r.traits["xt:1|c1"].familyKey);
  CHECK(r.entries.size() == 1);
}
