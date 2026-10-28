// My Lineup with the channel directory: a channel it knows is one row under
// the directory's id and name whatever the provider calls it, stations are
// still told by their call signs, a language or another country moves a
// channel to that market, a regional channel is regional, a closed one
// isn't suggested, and the user's choices made under a row's old id still
// count.
#include <algorithm>

#include "catalog/helpers.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/directory.hpp"
#include "core/catalog/lineup.hpp"
#include "core/catalog/work.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::channel;
using testdata::utc;

namespace {

directory::Entry listed(const char* id, const char* name, std::vector<std::string> aliases = {},
                        const char* list = "pay") {
  directory::Entry e;
  e.id = id;
  e.name = name;
  e.aliases = std::move(aliases);
  e.language = "en";
  e.list = list;
  return e;
}

directory::Data sampleDirectory() {
  directory::Data data;
  data.made = "2026-10-28";
  data.entries = {
      listed("TV One (American TV channel)", "TV One"),
      listed("Racer Network", "Racer Network", {"MAVTV"}),
      listed("Grit (TV network)", "Grit", {"Grit TV", "GritXtra"}),
      listed("Bounce TV", "Bounce TV"),
      listed("iptv:Bounce.us", "Bounce", {}, "free"),
      listed("Blaze Media", "Blaze Media", {"BlazeTV"}),
      listed("El Rey Network", "El Rey Network", {"El Rey"}, "defunct"),
      listed("Cine Latino", "Cine Latino"),
      listed("Marigold Valley Sports", "Marigold Valley Sports"),
      listed("Roar (TV network)", "Roar"),
      listed("CBS", "CBS", {}, "broadcast"),
      listed("WGN-TV", "WGN-TV", {"WGN"}, "local"),
  };
  data.entries[6].closed = "2020-12-31";
  data.entries[7].language = "es";
  data.entries[8].list = "regional sports";
  data.entries[8].places = {"fresno", "modesto"};
  data.elsewhere = {{"arydigital", "PK:ur"}, {"cbcnews", "CA:en"}};
  return data;
}

struct Built {
  directory::Data data = sampleDirectory();
  directory::Index index{data};
  CatalogResult result;
  explicit Built(const std::vector<Channel>& list) {
    result = build(list, utc(2026, 10, 28, 18, 0), {}, &index);
  }
  const EntryInfo* entryOf(const std::string& itemId) const {
    auto it = result.entryOf.find("xt:1|" + itemId);
    if (it == result.entryOf.end()) return nullptr;
    for (const EntryInfo& e : result.entries)
      if (e.id == it->second) return &e;
    return nullptr;
  }
};

}  // namespace

TEST_CASE("directory lineup: a known channel is one row, under the directory's id and name") {
  Built b({channel("1", "US: TV ONE HD", "US| ENTERTAINMENT"), channel("2", "TV: ONE TV ᴿᴬᵂ", "US| TV"),
           channel("3", "US: MAV TV", "US| SPORT"), channel("4", "US: Racer Network", "US| SPORT"),
           channel("5", "US: BLAZE TV", "US| ENTERTAINMENT")});
  const EntryInfo* one = b.entryOf("1");
  REQUIRE(one);
  // "ONE TV" isn't a name the directory has, but it was the same row before.
  CHECK(b.entryOf("2") == one);
  CHECK(one->id == "e|d:TV One (American TV channel)");
  CHECK(one->name == "TV One");
  CHECK(one->directoryId == "TV One (American TV channel)");
  CHECK(std::find(one->formerIds.begin(), one->formerIds.end(), "e|n:US:one") != one->formerIds.end());
  // A channel's former name joins it.
  REQUIRE(b.entryOf("3"));
  CHECK(b.entryOf("3") == b.entryOf("4"));
  CHECK(b.entryOf("3")->name == "Racer Network");
  // A list row that names the channel's owner.
  REQUIRE(b.entryOf("5"));
  CHECK(b.entryOf("5")->name == "BlazeTV");
}

TEST_CASE("directory lineup: its name and more is another channel, and the lists come before the free tier") {
  Built b({channel("1", "US: GRIT", "US| ENTERTAINMENT"), channel("2", "RK: GRIT XTRA ᴿᴬᵂ", "US| ROKU"),
           channel("3", "US: BOUNCE TV", "US| ENTERTAINMENT"), channel("4", "USA: Bounce", "United States")});
  REQUIRE(b.entryOf("1"));
  CHECK(b.entryOf("1")->id == "e|d:Grit (TV network)");
  CHECK(b.entryOf("2") != b.entryOf("1"));
  CHECK(b.entryOf("1")->category == LineupCategory::entertainment);
  REQUIRE(b.entryOf("3"));
  CHECK(b.entryOf("3") == b.entryOf("4"));
  CHECK(b.entryOf("3")->id == "e|d:Bounce TV");
}

TEST_CASE("directory lineup: stations are still told by their call signs") {
  Built b({channel("1", "US: CBS KPIX", "US| LOCALS"), channel("2", "US: St. Louis, MO KMOV CBS 4", "US| LOCALS"),
           channel("3", "US: WGN", "US| LOCALS")});
  REQUIRE(b.entryOf("1"));
  REQUIRE(b.entryOf("2"));
  CHECK(b.entryOf("1") != b.entryOf("2"));
  CHECK(b.entryOf("1")->callsign == std::optional<std::string>("KPIX"));
  CHECK(b.entryOf("2")->callsign == std::optional<std::string>("KMOV"));
  REQUIRE(b.entryOf("3"));
  CHECK(b.entryOf("3")->callsign == std::optional<std::string>("WGN"));
}

TEST_CASE("directory lineup: language, another country, regional, closed") {
  Built b({channel("1", "US: CINE LATINO", "US| ENTERTAINMENT"), channel("2", "US: ARY DIGITAL", "US| ENTERTAINMENT"),
           channel("3", "US: CBC NEWS", "US| NEWS"), channel("4", "US: MARIGOLD VALLEY SPORTS", "US| SPORT"),
           channel("5", "US: EL REY", "US| ENTERTAINMENT"), channel("6", "US: ROAR", "US| ENTERTAINMENT")});
  REQUIRE(b.entryOf("1"));
  CHECK(b.entryOf("1")->market == std::optional<std::string>("US-ES"));
  // Only another country's channels have the name, in a language a US
  // English lineup doesn't carry: that country's. English stays.
  REQUIRE(b.entryOf("2"));
  CHECK(b.entryOf("2")->market == std::optional<std::string>("PK"));
  REQUIRE(b.entryOf("3"));
  CHECK(b.entryOf("3")->market == std::optional<std::string>("US"));
  REQUIRE(b.entryOf("4"));
  CHECK(b.entryOf("4")->scope == "regional:fresno|modesto");
  const EntryInfo* rey = b.entryOf("5");
  REQUIRE(rey);
  CHECK(rey->closed == "2020-12-31");
  LineupPrefs prefs;
  prefs.configured = true;
  prefs.categories.insert(LineupCategory::entertainment);
  prefs.requireGuide = false;
  CHECK_FALSE(suggests(*rey, prefs, true, rey->name));
  REQUIRE(b.entryOf("6"));
  CHECK(suggests(*b.entryOf("6"), prefs, true, b.entryOf("6")->name));
}

TEST_CASE("directory lineup: choices made under a row's old id still count") {
  std::vector<Channel> channels = {channel("1", "US: TV ONE HD"), channel("2", "US: EL REY")};
  auto info = [](const std::string& id, const std::string& member, std::vector<std::string> former) {
    EntryInfo e;
    e.id = id;
    e.name = id;
    e.category = LineupCategory::entertainment;
    e.members = {member};
    e.market = "US";
    e.formerIds = std::move(former);
    return e;
  };
  LineupInput input;
  input.infos = {info("e|d:TV One", "xt:1|1", {"e|n:US:one"}), info("e|d:El Rey Network", "xt:1|2", {"e|n:US:elrey"})};
  input.infos[1].closed = "2020-12-31";
  input.channels = channels;
  for (size_t i = 0; i < channels.size(); ++i) input.indexByID[channels[i].id] = i;
  input.prefs.configured = true;
  input.prefs.requireGuide = false;
  input.prefs.categories.insert(LineupCategory::entertainment);
  CHECK(lineup(input).lineupIDs == std::set<std::string>{"e|d:TV One"});
  // Removed before the directory: stays removed.
  input.prefs.removed = {"e|n:US:one"};
  CHECK(lineup(input).lineupIDs.empty());
  // Added by hand before it closed (or before the directory knew): stays.
  input.prefs.added = {"e|n:US:elrey"};
  CHECK(lineup(input).lineupIDs == std::set<std::string>{"e|d:El Rey Network"});
}
