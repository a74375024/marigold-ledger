// The lineup reference data: its index (names, other names, feeds, call
// signs) and what it decides in the lineup build: one row per channel under
// its name, coast feeds, stations left to their call signs, renamed and
// shut-down channels, and channels nothing knows.
#include <algorithm>

#include "catalog/helpers.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/reference.hpp"
#include "core/logos/logos.hpp"
#include "core/util/gunzip.hpp"
#include "core/util/strings.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::channel;
using testdata::utc;

namespace {

const char* kSample = R"({
  "made": "2026-10-30",
  "channels": [
    {"i": "hbo", "n": "HBO", "g": "movies", "k": "premium", "f": {"east": [], "west": []}},
    {"i": "hbo-hits", "n": "HBO Hits", "a": ["HBO2", "HBO 2"], "g": "movies", "k": "premium", "p": "hbo",
     "f": {"east": [], "west": []}},
    {"i": "bravo", "n": "Bravo", "g": "entertainment", "f": {"east": ["NBC BRAVO"], "west": []}},
    {"i": "mgm-plus", "n": "MGM+", "g": "movies", "f": {"east": ["Epix"], "west": ["Epix West"]}},
    {"i": "cinemax", "n": "Cinemax", "g": "movies", "k": "premium"},
    {"i": "cowboy", "n": "The Cowboy Channel", "g": "sports"},
    {"i": "bounce", "n": "Bounce", "g": "entertainment"},
    {"i": "ms-now", "n": "MS NOW", "a": ["MSNBC"], "g": "news"},
    {"i": "yes-network", "n": "YES Network", "g": "sports", "k": "regional", "m": ["New York"],
     "f": {"alternate": ["YES Network Overflow"]}},
    {"i": "goltv", "n": "GolTV", "g": "spanish", "l": "es", "x": 1},
    {"i": "cbs", "n": "CBS", "g": "entertainment", "k": "broadcast"},
    {"i": "tennis-channel", "n": "Tennis Channel", "g": "sports"}
  ],
  "gone": ["HBO Family", "ThrillerMAX", "Cinemax ThrillerMAX"],
  "stations": [
    {"c": "WBOC-TV", "n": "CBS", "m": "Salisbury", "v": "16.1"},
    {"c": "WBOC-LD", "n": "TELEMUNDO", "m": "Salisbury", "k": "LP", "v": "42", "l": "es", "w": "WBOC-TV"},
    {"c": "KBMT", "n": "ABC", "m": "Beaumont-Port Arthur", "v": "12", "s": [{"n": "NBC", "v": "12.2"}]},
    {"c": "WANF", "n": "INDEPENDENT", "m": "Atlanta", "v": "46", "f": ["WGCL"]}
  ]
})";

struct Built {
  reference::Data data = *reference::fromJson(kSample);
  reference::Index index{data};
  CatalogResult result;
  explicit Built(const std::vector<Channel>& list) {
    result = build(list, utc(2026, 10, 30, 18, 0), {}, nullptr, nullptr, &index);
  }
  const EntryInfo* entryOf(const std::string& itemId) const {
    auto it = result.entryOf.find("xt:1|" + itemId);
    if (it == result.entryOf.end()) return nullptr;
    for (const EntryInfo& e : result.entries)
      if (e.id == it->second) return &e;
    return nullptr;
  }
};

Channel withGuide(Channel c, const char* epgId) {
  c.epgId = std::string(epgId);
  return c;
}

}  // namespace

TEST_CASE("reference: reads its channels, stations and shut-down names, also packed") {
  auto data = reference::fromJson(kSample);
  REQUIRE(data);
  CHECK(data->made == "2026-10-30");
  REQUIRE(data->channels.size() == 12);
  const reference::Channel& hits = data->channels[1];
  CHECK(hits.name == "HBO Hits");
  CHECK(hits.category == LineupCategory::movies);
  CHECK(hits.parent == "hbo");
  CHECK(hits.feed("west"));
  CHECK_FALSE(hits.feed("plus"));
  CHECK(data->channels[8].markets == std::vector<std::string>{"New York"});
  CHECK(data->channels[9].closed);
  CHECK(data->channels[9].language == "es");
  REQUIRE(data->stations.size() == 4);
  CHECK(data->stations[2].subChannels.size() == 1);
  CHECK(data->stations[2].subChannels[0].number == "12.2");
  CHECK(data->stations[1].simulcasts == "WBOC-TV");

  std::string packed;
  gz::Deflate deflate([&](std::string_view chunk) {
    packed.append(chunk);
    return true;
  });
  REQUIRE(deflate.feed(kSample));
  REQUIRE(deflate.finish());
  auto unpacked = reference::fromBytes(packed);
  REQUIRE(unpacked);
  CHECK(unpacked->channels.size() == 12);
  CHECK_FALSE(reference::fromBytes("not json"));
}

TEST_CASE("reference index: names, other names, loose names and feeds; never a broadcast network") {
  auto data = *reference::fromJson(kSample);
  reference::Index index(data);
  CHECK(index.match("MSNBC").channel->name == "MS NOW");
  CHECK(index.match("HBO 2 HD").channel->name == "HBO Hits");
  // Loosely: without "The" and "Channel".
  CHECK(index.match("COWBOY CHANNEL").channel->name == "The Cowboy Channel");
  // A feed's own name.
  reference::Match east = index.match("NBC BRAVO");
  REQUIRE(east.channel);
  CHECK(east.channel->name == "Bravo");
  CHECK(east.feed->name == "east");
  // "Epix" and "Epix West" are one key: the channel, its coast left to the name.
  reference::Match epix = index.match("EPIX HD");
  REQUIRE(epix.channel);
  CHECK(epix.channel->name == "MGM+");
  CHECK_FALSE(epix.feed);
  CHECK(index.match("YES Network Overflow").feed->name == "alternate");
  // A provider's "CBS" is a station, not the network.
  CHECK_FALSE(index.match("CBS").channel);
  CHECK_FALSE(index.match("Some Other Channel").channel);
  CHECK(index.gone("HBO FAMILY"));
  CHECK(index.gone("GolTV"));
  CHECK_FALSE(index.gone("HBO"));
}

TEST_CASE("reference index: stations by call sign, full power first, and by an old call sign") {
  auto data = *reference::fromJson(kSample);
  reference::Index index(data);
  REQUIRE(index.station("WBOC"));
  CHECK(index.station("WBOC")->callsign == "WBOC-TV");
  CHECK(index.station("WBOC-LD")->callsign == "WBOC-LD");
  CHECK(index.station("kbmt-dt2")->callsign == "KBMT");
  CHECK_FALSE(index.station("WGCL"));
  REQUIRE(index.formerly("WGCL"));
  CHECK(index.formerly("WGCL")->callsign == "WANF");
  CHECK(index.stationsIn("Salisbury").size() == 2);
  CHECK(reference::bareCallsign("wboc-ld") == "WBOC");
}

TEST_CASE("reference lineup: a renamed channel's copies are one row, under its name, and keep their old ids") {
  Built b({channel("1", "US: HBO 2 HD", "US| PREMIUM"), channel("2", "AT&T: HBO HITS ᴿᴬᵂ", "US| AT&T"),
           channel("3", "US: MSNBC HD", "US| NEWS"), channel("4", "TV: MS NOW", "US| TV")});
  const EntryInfo* hits = b.entryOf("1");
  REQUIRE(hits);
  CHECK(b.entryOf("2") == hits);
  CHECK(hits->id == "e|r:hbo-hits");
  CHECK(hits->name == "HBO Hits");
  CHECK(hits->category == LineupCategory::movies);
  // The row's ids before: the user's picks made under them still count.
  CHECK(std::find(hits->formerIds.begin(), hits->formerIds.end(), "e|core:HBO 2") != hits->formerIds.end());
  REQUIRE(b.entryOf("3"));
  CHECK(b.entryOf("3") == b.entryOf("4"));
  CHECK(b.entryOf("3")->name == "MS NOW");
}

TEST_CASE("reference lineup: a network word in front and a coast in brackets") {
  Built b({channel("1", "US: NBC BRAVO (WEST) (D)", "US| NBC"), channel("2", "US: BRAVO HD", "US| ENTERTAINMENT"),
           channel("3", "US: NBC BRAVO (EAST) (D)", "US| NBC"), channel("4", "US: EPIX WEST", "US| MOVIES"),
           channel("5", "US: EPIX HD", "US| MOVIES")});
  const EntryInfo* west = b.entryOf("1");
  REQUIRE(west);
  CHECK(west->id == "e|r:bravo~west");
  CHECK(west->name == "Bravo West");
  // "(WEST)" is no station's call sign.
  CHECK_FALSE(west->callsign);
  REQUIRE(b.entryOf("2"));
  CHECK(b.entryOf("2")->id == "e|r:bravo");
  CHECK(b.entryOf("3") == b.entryOf("2"));
  CHECK(b.entryOf("4")->id == "e|r:mgm-plus~west");
  CHECK(b.entryOf("5")->id == "e|r:mgm-plus");
}

TEST_CASE("reference lineup: stations stay stations; an old call sign is the station that has it now") {
  Built b({channel("1", "US: CBS (WCBS)", "US| LOCALS"), channel("2", "US: CBS (WLNY)", "US| LOCALS"),
           channel("3", "US: CBS 46 (WGCL) ATLANTA HD", "US| CBS"),
           channel("4", "US: NBC 12 (KBMT-DT2) BEAUMONT", "US| NBC")});
  REQUIRE(b.entryOf("1"));
  REQUIRE(b.entryOf("2"));
  CHECK(b.entryOf("1") != b.entryOf("2"));
  CHECK(b.entryOf("1")->category == LineupCategory::local);
  REQUIRE(b.entryOf("3"));
  CHECK(b.entryOf("3")->callsign == std::string("WANF"));
  REQUIRE(b.entryOf("4"));
  CHECK(b.entryOf("4")->callsign == std::string("KBMT"));
  CHECK(b.entryOf("4")->id == "e|cs:KBMT|NBC");
}

TEST_CASE("reference lineup: spellings join, overflow feeds and regional channels") {
  Built b({channel("1", "US: BOUNCE", "US| ENTERTAINMENT"), channel("2", "AT&T: BOUNCETV ᴿᴬᵂ", "US| AT&T"),
           channel("3", "US: COWBOY CHANNEL", "US| SPORTS"), channel("4", "US: YES NETWORK", "US| SPORTS"),
           channel("5", "US: YES NETWORK OVERFLOW", "US| SPORTS")});
  REQUIRE(b.entryOf("1"));
  CHECK(b.entryOf("2") == b.entryOf("1"));
  CHECK(b.entryOf("1")->name == "Bounce");
  CHECK(b.entryOf("3")->name == "The Cowboy Channel");
  REQUIRE(b.entryOf("4"));
  CHECK(b.entryOf("4")->id == "e|r:yes-network");
  CHECK(b.entryOf("4")->scope == "regional:new york");
  REQUIRE(b.entryOf("5"));
  CHECK(b.entryOf("5")->id == "e|r:yes-network~alternate");
  CHECK(b.entryOf("5")->name == "YES Network Overflow");
}

TEST_CASE("reference lineup: a shut-down channel isn't suggested unless the guide shows what it plays now") {
  Built b({withGuide(channel("1", "US: CINEMAX HD", "US| PREMIUM"), "Cinemax.us"),
           withGuide(channel("2", "US: CINEMAX THRILLERMAX HD", "US| PREMIUM"), "Cinemax.us"),
           withGuide(channel("3", "US: HBO FAMILY HD", "US| PREMIUM"), "HBOFamily.us"),
           channel("4", "US: GOL TV HD", "US| SPORTS")});
  const EntryInfo* cinemax = b.entryOf("1");
  REQUIRE(cinemax);
  // Filed under Cinemax's listings: one of Cinemax's copies, tried last.
  CHECK(b.entryOf("2") == cinemax);
  CHECK(cinemax->members.back() == "xt:1|2");
  CHECK(cinemax->closed.empty());
  REQUIRE(b.entryOf("3"));
  CHECK_FALSE(b.entryOf("3")->closed.empty());
  REQUIRE(b.entryOf("4"));
  CHECK_FALSE(b.entryOf("4")->closed.empty());
}

TEST_CASE("reference lineup: a US channel nothing knows is free streaming; show loops and known feeds aren't") {
  Built b({channel("1", "AT&T: LOVE KILLS ᴿᴬᵂ", "US| AT&T"), channel("2", "US: CHEERS 4K", "US| CINEMA TV SHOWS"),
           channel("3", "TV: TENNIS CHANNEL 3", "US| TV"), channel("4", "TV: ABC7 NEW YORK", "US| TV"),
           channel("5", "US: SOME SPORTS FEED", "US| SPORTS")});
  REQUIRE(b.entryOf("1"));
  CHECK(b.entryOf("1")->kind == ChannelKind::fast);
  CHECK(b.entryOf("1")->category == LineupCategory::free);
  REQUIRE(b.entryOf("2"));
  CHECK(b.entryOf("2")->kind == ChannelKind::channel);
  REQUIRE(b.entryOf("3"));
  CHECK(b.entryOf("3")->kind == ChannelKind::channel);
  REQUIRE(b.entryOf("4"));
  CHECK(b.entryOf("4")->kind == ChannelKind::channel);
  REQUIRE(b.entryOf("5"));
  CHECK(b.entryOf("5")->kind == ChannelKind::channel);
}

TEST_CASE("reference lineup: without the data the rules decide as before") {
  CatalogResult r = build({channel("1", "AT&T: LOVE KILLS ᴿᴬᵂ", "US| AT&T"), channel("2", "US: HBO 2 HD", "US| PREMIUM")},
                          utc(2026, 10, 30, 18, 0));
  for (const EntryInfo& e : r.entries) {
    CHECK_FALSE(str::startsWith(e.id, "e|r:"));
    CHECK(e.kind == ChannelKind::channel);
  }
}

TEST_CASE("logos: the library's names split where the channel's run together") {
  CHECK(logos::logoSetFile("BYUtv") == std::string("byu-tv-us.png"));
  CHECK(logos::logoSetFile("MeTV") == std::string("me-tv-us.png"));
  CHECK(logos::logoSetFile("MeTV Toons") == std::string("me-tv-toons-us.png"));
}
