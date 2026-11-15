// The lineup reference data: its index (names, other names, feeds, call
// signs) and what it decides in the lineup build: one row per channel under
// its name, coast feeds, stations left to their call signs, renamed and
// shut-down channels, and channels nothing knows.
#include <algorithm>

#include "catalog/helpers.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/directory.hpp"
#include "core/catalog/lineup.hpp"
#include "core/catalog/reference.hpp"
#include "core/catalog/work.hpp"
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
    {"i": "tennis-channel", "n": "Tennis Channel", "g": "sports"},
    {"i": "paramount-plus-with-showtime", "n": "Paramount+ with Showtime", "a": ["Showtime", "SHO"], "g": "movies",
     "k": "premium", "f": {"east": [], "west": ["Showtime West"]}},
    {"i": "espn", "n": "ESPN", "g": "sports"},
    {"i": "espn2", "n": "ESPN2", "g": "sports"},
    {"i": "nbcsn", "n": "NBCSN", "g": "sports"},
    {"i": "nbc-sports-bay-area", "n": "NBC Sports Bay Area", "g": "sports", "k": "regional",
     "m": ["San Francisco-Oakland-San Jose"], "f": {"plus": ["NBC Sports Bay Area Plus"]}},
    {"i": "nbc-sports-chicago", "n": "NBC Sports Chicago", "a": ["CSN Chicago"], "g": "sports", "k": "regional",
     "m": ["Chicago"], "x": 1, "f": {"plus": ["NBC Sports Chicago Plus"]}},
    {"i": "spectrum-sportsnet", "n": "Spectrum SportsNet", "a": ["SportsNet LA Lakers"], "g": "sports",
     "k": "regional", "m": ["Los Angeles"]},
    {"i": "spectrum-sportsnet-la", "n": "Spectrum SportsNet LA", "a": ["SportsNet LA Dodgers", "Spectrum SportsNet LA Dodgers"],
     "g": "sports",
     "k": "regional", "m": ["Los Angeles"]},
    {"i": "mlb-network", "n": "MLB Network", "g": "sports"},
    {"i": "mlb-network-alternate", "n": "MLB Network Alternate", "a": ["MLB Network Alt."], "g": "sports",
     "p": "mlb-network"},
    {"i": "nhl-network", "n": "NHL Network", "g": "sports"},
    {"i": "nhl-network-alternate", "n": "NHL Network Alternate", "g": "sports", "p": "nhl-network"},
    {"i": "nfl-network", "n": "NFL Network", "g": "sports"},
    {"i": "nba-tv", "n": "NBA TV", "g": "sports"},
    {"i": "fs2", "n": "FS2", "a": ["Fox Sports 2"], "g": "sports"},
    {"i": "angels-tv", "n": "Angels Broadcast Television", "a": ["FanDuel Sports Network West"], "g": "sports",
     "k": "regional", "m": ["Los Angeles"]},
    {"i": "tudn", "n": "TUDN", "a": ["Univision TUDN"], "g": "spanish", "l": "es"}
  ],
  "gone": [{"n": "HBO Family", "p": "hbo"}, "ThrillerMAX", "Cinemax ThrillerMAX"],
  "stations": [
    {"c": "WBOC-TV", "n": "CBS", "m": "Salisbury", "v": "16.1"},
    {"c": "WBOC-LD", "n": "TELEMUNDO", "m": "Salisbury", "k": "LP", "v": "42", "l": "es", "w": "WBOC-TV"},
    {"c": "KBMT", "n": "ABC", "m": "Beaumont-Port Arthur", "v": "12", "s": [{"n": "NBC", "v": "12.2"}]},
    {"c": "WANF", "n": "INDEPENDENT", "m": "Atlanta", "v": "46", "f": ["WGCL"]},
    {"c": "KVEA", "n": "TELEMUNDO", "m": "Los Angeles", "v": "52", "l": "es"},
    {"c": "KSTS", "n": "TELEMUNDO", "m": "San Francisco-Oakland-San Jose", "v": "48", "l": "es"},
    {"c": "WNJU", "n": "TELEMUNDO", "m": "New York", "v": "47", "l": "es"},
    {"c": "WKTB-CD", "n": "TELEMUNDO", "m": "Atlanta", "v": "47", "l": "es", "k": "A"},
    {"c": "WTCL-LD", "n": "TELEMUNDO", "m": "Cleveland-Akron", "v": "6", "l": "es", "k": "LP"},
    {"c": "WUVP-DT", "n": "UNIVISION", "m": "Philadelphia", "v": "65", "l": "es"},
    {"c": "KMEX-DT", "n": "UNIVISION", "m": "Los Angeles", "v": "34", "l": "es"},
    {"c": "WGBC", "n": "FOX", "m": "Meridian", "v": "30", "s": [{"n": "NBC", "v": "30.2"}]}
  ]
})";

struct Built {
  reference::Data data = *reference::fromJson(kSample);
  reference::Index index{data};
  CatalogResult result;
  explicit Built(const std::vector<Channel>& list, const directory::Index* directory = nullptr) {
    result = build(list, utc(2026, 10, 30, 18, 0), {}, directory, nullptr, &index);
  }
  const EntryInfo* row(const std::string& id) const {
    for (const EntryInfo& e : result.entries)
      if (e.id == id) return &e;
    return nullptr;
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
  REQUIRE(data->channels.size() == 29);
  const reference::Channel& hits = data->channels[1];
  CHECK(hits.name == "HBO Hits");
  CHECK(hits.category == LineupCategory::movies);
  CHECK(hits.parent == "hbo");
  CHECK(hits.feed("west"));
  CHECK_FALSE(hits.feed("plus"));
  CHECK(data->channels[8].markets == std::vector<std::string>{"New York"});
  CHECK(data->channels[9].closed);
  CHECK(data->channels[9].language == "es");
  REQUIRE(data->stations.size() == 12);
  CHECK(data->stations[2].subChannels.size() == 1);
  CHECK(data->stations[2].subChannels[0].number == "12.2");
  CHECK(data->stations[1].simulcasts == "WBOC-TV");
  // Shut-down names, with the channel they belonged to or alone.
  REQUIRE(data->gone.size() == 3);
  CHECK(data->gone[0].name == "HBO Family");
  CHECK(data->gone[0].parent == "hbo");
  CHECK(data->gone[1].name == "ThrillerMAX");
  CHECK(data->gone[1].parent.empty());

  std::string packed;
  gz::Deflate deflate([&](std::string_view chunk) {
    packed.append(chunk);
    return true;
  });
  REQUIRE(deflate.feed(kSample));
  REQUIRE(deflate.finish());
  auto unpacked = reference::fromBytes(packed);
  REQUIRE(unpacked);
  CHECK(unpacked->channels.size() == 29);
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
  // Build 161: "Epix West" says its coast, so only a name that says West
  // finds it: "EPIX HD" is the East feed's "Epix".
  reference::Match epix = index.match("EPIX HD");
  REQUIRE(epix.channel);
  CHECK(epix.channel->name == "MGM+");
  REQUIRE(epix.feed);
  CHECK(epix.feed->name == "east");
  REQUIRE(index.match("EPIX WEST HD").feed);
  CHECK(index.match("EPIX WEST HD").feed->name == "west");
  CHECK(index.match("EPIX (WEST)").feed->name == "west");
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

TEST_CASE("reference lineup: the leagues' free channels aren't their networks") {
  Built b({channel("1", "TV: THE MLB CHANNEL ᴿᴬᵂ", "US| TV"), channel("2", "US: MLB NETWORK HD", "US| SPORTS"),
           channel("3", "US| The NBA Channel FHD", "US| TV"), channel("4", "US: NBA TV HD", "US| SPORTS"),
           channel("5", "US: NFL CHANNEL", "US| TV"), channel("6", "US: NFL NETWORK HD", "US| SPORTS"),
           channel("7", "US: MLB NETWORK RAW", "US| SPORTS")});
  for (const char* id : {"1", "2", "3", "4", "5", "6", "7"}) REQUIRE(b.entryOf(id));
  CHECK(b.entryOf("2")->id == "e|r:mlb-network");
  CHECK(b.entryOf("7") == b.entryOf("2"));
  CHECK(b.entryOf("1") != b.entryOf("2"));
  CHECK(b.entryOf("4")->id == "e|r:nba-tv");
  CHECK(b.entryOf("3") != b.entryOf("4"));
  CHECK(b.entryOf("6")->id == "e|r:nfl-network");
  CHECK(b.entryOf("5") != b.entryOf("6"));
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

TEST_CASE("reference lineup: a US channel nothing knows is other; show loops are 24/7, known feeds the channel's") {
  // Build 153: the reference data is the backbone. Nothing knowing a US
  // channel no longer makes it free streaming: it's "other", out of the
  // guide unless the viewer shows those.
  Built b({channel("1", "AT&T: LOVE KILLS ᴿᴬᵂ", "US| AT&T"), channel("2", "US: CHEERS 4K", "US| CINEMA TV SHOWS"),
           channel("3", "TV: TENNIS CHANNEL 3", "US| TV"), channel("4", "TV: ABC7 NEW YORK", "US| TV"),
           channel("5", "US: SOME SPORTS FEED", "US| SPORTS"), channel("6", "RK: LOVE KILLS", "US| ROKU")});
  REQUIRE(b.entryOf("1"));
  // A copy in a free service's group says what it is.
  CHECK(b.entryOf("1") == b.entryOf("6"));
  CHECK(b.entryOf("1")->kind == ChannelKind::fast);
  CHECK(b.entryOf("1")->category == LineupCategory::free);
  REQUIRE(b.entryOf("2"));
  CHECK(b.entryOf("2")->kind == ChannelKind::loop247);
  CHECK(b.entryOf("2")->category == LineupCategory::free);
  // A known channel's numbered feed: its own row, named and filed as the channel.
  REQUIRE(b.entryOf("3"));
  CHECK(b.entryOf("3")->kind == ChannelKind::channel);
  CHECK(b.entryOf("3")->id == "e|r:tennis-channel~x3");
  CHECK(b.entryOf("3")->name == "Tennis Channel 3");
  CHECK(b.entryOf("3")->category == LineupCategory::sports);
  REQUIRE(b.entryOf("4"));
  CHECK(b.entryOf("4")->kind == ChannelKind::channel);
  CHECK(b.entryOf("4")->category != LineupCategory::other);
  REQUIRE(b.entryOf("5"));
  CHECK(b.entryOf("5")->kind == ChannelKind::channel);
  CHECK(b.entryOf("5")->category == LineupCategory::other);
}

TEST_CASE("reference lineup: other rows stay out of the guide unless shown, added or a favourite") {
  Built b({channel("1", "US: BOUNCE", "US| ENTERTAINMENT"), channel("2", "US: SOME SPORTS FEED", "US| SPORTS"),
           channel("3", "US: ANOTHER FEED", "US| SPORTS")});
  REQUIRE(b.entryOf("2"));
  REQUIRE(b.entryOf("2")->category == LineupCategory::other);
  LineupInput in;
  in.infos = b.result.entries;
  for (const char* id : {"1", "2", "3"}) in.channels.push_back(channel(id, "x", "US| SPORTS"));
  for (size_t i = 0; i < in.channels.size(); ++i) in.indexByID[in.channels[i].id] = i;
  in.prefs.configured = true;
  in.prefs.added.insert(b.entryOf("3")->id);
  auto has = [](const LineupOutput& o, const std::string& id) {
    return std::any_of(o.entries.begin(), o.entries.end(), [&](const Channel& c) { return c.id == id; });
  };
  LineupOutput hidden = lineup(in);
  CHECK(has(hidden, b.entryOf("1")->id));
  CHECK_FALSE(has(hidden, b.entryOf("2")->id));
  CHECK(has(hidden, b.entryOf("3")->id));  // the viewer added it
  CHECK_FALSE(hidden.lineupIDs.count(b.entryOf("2")->id));
  in.showOther = true;
  LineupOutput shown = lineup(in);
  CHECK(has(shown, b.entryOf("2")->id));
  // Shown, never suggested.
  CHECK_FALSE(shown.lineupIDs.count(b.entryOf("2")->id));
}

TEST_CASE("single-show channels in a provider's TV shows group are 24/7; real networks there never") {
  const std::string shows = "US| CINEMA TV SHOWS ᴴᴰ/ᴿᴬᵂ ⁶⁰ᶠᵖˢ";
  std::vector<Channel> list = {
      channel("1", "US: BLUE BLOODS 4K", shows), channel("2", "US: CHEERS 4K", shows),
      channel("3", "US: LAW & ORDER 4K", shows),
      // Real networks: a core one, one the reference data knows, one only
      // the directory knows (loosely), a station by its network's name.
      channel("4", "US: COMEDY CENTRAL 4K", shows), channel("5", "US: PARAMOUNT NETWORK", shows),
      channel("6", "US: BOUNCE 4K", shows), channel("7", "US: DREAM TEAM 4K", shows),
      channel("8", "US: FOX 5 TESTVILLE", shows),
      // A sports group's "series" isn't a show's.
      channel("10", "WORLD SERIES CAM", "MLB| WORLD SERIES"),
      // A group named for series.
      channel("11", "BONANZA", "Classic TV Series")};
  directory::Data dirData;
  directory::Entry bluebloods;
  bluebloods.id = "iptv:BlueBloods.us";
  bluebloods.name = "Blue Bloods";
  bluebloods.list = "free";  // iptv-org's free tier has single-show channels
  directory::Entry dream;
  dream.id = "The Dream Team Channel";
  dream.name = "The Dream Team Channel";
  dream.list = "pay";
  dirData.entries = {bluebloods, dream};
  directory::Index directory(dirData);
  reference::Data refData = *reference::fromJson(kSample);
  reference::Index reference(refData);
  // As an Xtream list comes: the category is the first tag.
  for (Channel& c : list) c.tags = {*c.group};
  std::vector<Channel> annotated = catalog::annotated(list);
  CatalogResult r = build(annotated, utc(2026, 10, 30, 18, 0), {}, &directory, nullptr, &reference);
  auto kindOf = [&](const std::string& id) {
    const std::string& e = r.entryOf.at("xt:1|" + id);
    return std::find_if(r.entries.begin(), r.entries.end(), [&](const EntryInfo& x) { return x.id == e; })->kind;
  };
  for (const char* id : {"1", "2", "3", "11"}) {
    INFO(id);
    CHECK(kindOf(id) == ChannelKind::loop247);
  }
  for (const char* id : {"4", "5", "6", "7", "8", "10"}) {
    INFO(id);
    CHECK(kindOf(id) == ChannelKind::channel);
  }

  // 24/7 follows the Free setting: off, the shows stay out of My Lineup.
  LineupInput in;
  in.infos = r.entries;
  in.channels = annotated;
  for (size_t i = 0; i < annotated.size(); ++i) in.indexByID[annotated[i].id] = i;
  in.prefs.configured = true;
  in.prefs.categories.erase(LineupCategory::free);
  in.prefs.requireGuide = false;
  in.otherGroup = "Other";
  auto inLineup = [&](const LineupOutput& out, const std::string& id) {
    return out.lineupIDs.count(r.entryOf.at("xt:1|" + id)) > 0;
  };
  LineupOutput off = lineup(in);
  CHECK_FALSE(inLineup(off, "1"));
  CHECK_FALSE(inLineup(off, "2"));
  CHECK(inLineup(off, "4"));
  in.prefs.categories.insert(LineupCategory::free);
  LineupOutput on = lineup(in);
  CHECK(inLineup(on, "1"));
  CHECK(inLineup(on, "2"));
}

TEST_CASE("reference lineup: without the data the rules decide as before") {
  CatalogResult r = build({channel("1", "AT&T: LOVE KILLS ᴿᴬᵂ", "US| AT&T"), channel("2", "US: HBO 2 HD", "US| PREMIUM")},
                          utc(2026, 10, 30, 18, 0));
  for (const EntryInfo& e : r.entries) {
    CHECK_FALSE(str::startsWith(e.id, "e|r:"));
    CHECK(e.kind == ChannelKind::channel);
  }
}

// MARK: Build 161: shut-down and duplicate channels

namespace {

// A lineup of `b`'s rows over its own channels (as the store hands them).
LineupInput lineupOf(const Built& b, const std::vector<Channel>& list) {
  LineupInput in;
  in.infos = b.result.entries;
  in.channels = list;
  for (size_t i = 0; i < in.channels.size(); ++i) in.indexByID[in.channels[i].id] = i;
  in.prefs.configured = true;
  return in;
}

bool listed(const LineupOutput& o, const std::string& id) {
  return std::any_of(o.entries.begin(), o.entries.end(), [&](const Channel& c) { return c.id == id; });
}

}  // namespace

TEST_CASE("Build 161: a shut-down row stays out of the lineup unless it has listings, was added or is a favourite") {
  std::vector<Channel> list = {channel("1", "US: HBO FAMILY HD", "US| PREMIUM"), channel("2", "US: GOL TV HD", "US| SPORTS"),
                               channel("3", "US: HBO HD", "US| PREMIUM")};
  Built b(list);
  const EntryInfo* family = b.entryOf("1");
  const EntryInfo* gol = b.entryOf("2");
  REQUIRE(family);
  REQUIRE(gol);
  REQUIRE_FALSE(family->closed.empty());
  REQUIRE_FALSE(gol->closed.empty());
  LineupInput in = lineupOf(b, list);
  // No listings: out of the guide, All Channels, Search and channel up/down.
  LineupOutput none = lineup(in);
  CHECK_FALSE(listed(none, family->id));
  CHECK_FALSE(listed(none, gol->id));
  CHECK(listed(none, b.entryOf("3")->id));
  CHECK(std::none_of(none.browseEntries.begin(), none.browseEntries.end(),
                     [&](const Channel& c) { return c.id == family->id; }));
  // A copy with listings (GolTV still shows matches): shown, never suggested.
  in.guideIDs.insert("xt:1|2");
  LineupOutput withGuide = lineup(in);
  CHECK(listed(withGuide, gol->id));
  CHECK_FALSE(withGuide.lineupIDs.count(gol->id));
  CHECK_FALSE(listed(withGuide, family->id));
  // Added by the viewer: shown, and in My Lineup.
  in.prefs.added.insert(family->id);
  LineupOutput added = lineup(in);
  CHECK(listed(added, family->id));
  CHECK(added.lineupIDs.count(family->id));
  // A favourite: the same.
  in.prefs.added.clear();
  in.favoriteIDs = std::unordered_set<std::string>{"xt:1|1"};
  LineupOutput favorite = lineup(in);
  CHECK(listed(favorite, family->id));
  // Removed by the viewer, no listings: out again.
  in.favoriteIDs.reset();
  in.prefs.removed.insert(family->id);
  CHECK_FALSE(listed(lineup(in), family->id));
  // Another country's channel of a shut-down US channel's name isn't it.
  Built abroad({channel("1", "UK: GOL TV HD", "UK| SPORTS")});
  REQUIRE(abroad.entryOf("1"));
  CHECK(abroad.entryOf("1")->closed.empty());
}

TEST_CASE("Build 161: a shut-down channel's parent; its copy joins only the parent's row") {
  auto data = *reference::fromJson(kSample);
  reference::Index index(data);
  REQUIRE(index.goneParent("HBO FAMILY HD"));
  CHECK(index.goneParent("HBO FAMILY HD")->id == "hbo");
  CHECK_FALSE(index.goneParent("ThrillerMAX"));
  CHECK_FALSE(index.goneParent("HBO"));
  // The core list has its closing date (was suggested until Build 161).
  REQUIRE(coreMatch("HBO Family"));
  CHECK(coreMatch("HBO Family")->closed == "2025-08-15");
  // Filed under another channel's listings: stays apart, closed.
  Built elsewhere({withGuide(channel("1", "US: CINEMAX HD", "US| PREMIUM"), "Cinemax.us"),
                   withGuide(channel("2", "US: HBO FAMILY HD", "US| PREMIUM"), "Cinemax.us")});
  REQUIRE(elsewhere.entryOf("2"));
  CHECK(elsewhere.entryOf("2") != elsewhere.entryOf("1"));
  CHECK_FALSE(elsewhere.entryOf("2")->closed.empty());
  // Under HBO's listings: HBO's last copy.
  Built under({withGuide(channel("1", "US: HBO HD", "US| PREMIUM"), "HBO.us"),
               withGuide(channel("2", "US: HBO FAMILY HD", "US| PREMIUM"), "HBO.us")});
  REQUIRE(under.entryOf("2"));
  CHECK(under.entryOf("2") == under.entryOf("1"));
  CHECK(under.entryOf("1")->id == "e|r:hbo");
  CHECK(under.entryOf("1")->members.back() == "xt:1|2");
}

TEST_CASE("Build 161: a station's own network decides its row; sister stations stay apart") {
  Built b({channel("1", "US: NBC (KVEA) TELEMUNDO (D)", "US| LOCALS"),
           channel("2", "US: TELEMUNDO 52 (KVEA) LOS ANGELES", "US| LATINO"),
           channel("3", "US: WGBC NBC 30", "US| LOCALS"), channel("4", "US: WGBC FOX 30", "US| LOCALS")});
  const EntryInfo* kvea = b.entryOf("1");
  REQUIRE(kvea);
  CHECK(kvea->id == "e|cs:KVEA|TELEMUNDO");
  CHECK(b.entryOf("2") == kvea);
  // The id the viewer's choices may be under.
  CHECK(std::find(kvea->formerIds.begin(), kvea->formerIds.end(), "e|cs:KVEA|NBC") != kvea->formerIds.end());
  REQUIRE(b.entryOf("3"));
  REQUIRE(b.entryOf("4"));
  CHECK(b.entryOf("3")->id == "e|cs:WGBC|NBC");
  CHECK(b.entryOf("4")->id == "e|cs:WGBC|FOX");
}

TEST_CASE("Build 161: a bracketed place or team is part of which channel it is") {
  Built b({channel("1", "|US| NBC SPORTS HD [BAY AREA]", "|US| SPORTS"),
           channel("2", "|US| NBC SPORTS HD [BAY AREA PLUS]", "|US| SPORTS"),
           channel("3", "|US| NBC SPORTS HD [CHICAGO]", "|US| SPORTS"),
           channel("4", "US: NBC SPORTS NETWORK", "US| SPORTS"), channel("5", "|US| NBCSN", "|US| SPORTS"),
           channel("6", "|US| SPECTRUM SPORTSNET [LA DODGERS]", "|US| SPORTS"),
           channel("7", "US: SPORTSNET LA LAKERS HD", "US| SPORTS"),
           channel("8", "|US| ESPN [SLING]", "|US| SPORTS"), channel("9", "|US| NBC SPORTS HD [CHICAGO PLUS]", "|US| SPORTS")});
  REQUIRE(b.entryOf("1"));
  CHECK(b.entryOf("1")->id == "e|r:nbc-sports-bay-area");
  CHECK(b.entryOf("2")->id == "e|r:nbc-sports-bay-area~plus");
  // NBCSN is live again (relaunched 2025): only its own copies.
  const EntryInfo* nbcsn = b.entryOf("5");
  REQUIRE(nbcsn);
  CHECK(nbcsn->id == "e|r:nbcsn");
  CHECK(b.entryOf("4") == nbcsn);
  CHECK(nbcsn->members.size() == 2);
  CHECK(nbcsn->closed.empty());
  // NBC Sports Chicago shut down: its own row, closed.
  REQUIRE(b.entryOf("3"));
  CHECK(b.entryOf("3")->id == "e|r:nbc-sports-chicago");
  CHECK_FALSE(b.entryOf("3")->closed.empty());
  CHECK(b.entryOf("9")->id == "e|r:nbc-sports-chicago~plus");
  // The Dodgers' channel isn't the Lakers'.
  CHECK(b.entryOf("6")->id == "e|r:spectrum-sportsnet-la");
  CHECK(b.entryOf("7")->id == "e|r:spectrum-sportsnet");
  // A provider's tag in brackets isn't.
  CHECK(b.entryOf("8")->id == "e|r:espn");
}

TEST_CASE("Build 161: stations of different places with one number stay apart") {
  Built b({channel("1", "|US| ABC 7 [BUFFALO]", "|US| LOCALS"), channel("2", "|US| ABC 7 [EL PASO]", "|US| LOCALS")});
  REQUIRE(b.entryOf("1"));
  REQUIRE(b.entryOf("2"));
  CHECK(b.entryOf("1") != b.entryOf("2"));
  CHECK(b.entryOf("1")->id == "e|n:US:abc7buffalo");
  // Picks made under the row they shared still count.
  CHECK(std::find(b.entryOf("1")->formerIds.begin(), b.entryOf("1")->formerIds.end(), "e|n:US:abc7") !=
        b.entryOf("1")->formerIds.end());
}

TEST_CASE("Build 161: a directory row that is a reference channel by an old name joins it") {
  directory::Data dirData;
  auto entry = [](const char* id, std::vector<std::string> aliases, const char* list = "pay") {
    directory::Entry e;
    e.id = id;
    e.name = id;
    e.aliases = std::move(aliases);
    e.list = list;
    return e;
  };
  dirData.entries = {entry("NBC Sports Bay Area", {"CSN Bay Area", "Comcast SportsNet Bay Area"}),
                     entry("Fox Sports 2", {"Fuel TV"}),
                     entry("Cinemax", {"Max", "Cinemax Reel Life"}),
                     entry("Prime Sports", {"Fox Sports West", "FanDuel Sports Network West"}),
                     entry("The Weather Channel", {"Jennifer Lopez"}),
                     entry("iptv:FanDuelSportsNetworkOhioPlus.us", {}, "free"),
                     entry("iptv:SomeFreeChannel.us", {}, "free")};
  dirData.entries[5].name = "FanDuel Sports Network Ohio Plus";
  dirData.entries[6].name = "Some Free Channel";
  directory::Index dir(dirData);
  Built b({channel("1", "US: CSN BAY AREA HD", "US| NEWS"), channel("2", "TV: FUEL TV", "US| TV"),
           channel("3", "|US| MAX", "|US| MOVIES"), channel("4", "Jennifer Lopez", "|US| BEST ACTORS"),
           channel("5", "US: FOX SPORTS WEST HD", "US| SPORTS"), channel("6", "US: FOX SPORTS", "US| SPORTS"),
           channel("7", "FanDuel Sports Network Ohio Plus", "US| AT&T"), channel("8", "Some Free Channel", "US| AT&T")},
          &dir);
  CHECK(b.entryOf("1")->id == "e|r:nbc-sports-bay-area");
  CHECK(b.entryOf("2")->id == "e|r:fs2");
  CHECK(std::find(b.entryOf("2")->formerIds.begin(), b.entryOf("2")->formerIds.end(), "e|d:Fox Sports 2") !=
        b.entryOf("2")->formerIds.end());
  // A brand alone, and a page title among the directory's other names: no.
  CHECK(b.entryOf("3")->id == "e|d:Cinemax");
  CHECK(b.entryOf("4")->id == "e|d:The Weather Channel");
  // "Fox Sports West" says its coast: that name, no West feed of anything;
  // "FOX SPORTS" alone isn't it.
  CHECK(b.entryOf("5")->id == "e|r:angels-tv");
  CHECK(b.entryOf("6") != b.entryOf("5"));
  CHECK_FALSE(str::startsWith(b.entryOf("6")->id, "e|d:Prime Sports"));
  // A regional sports network's old stream in the directory's free tier
  // isn't free streaming: other. Another free-tier channel still is.
  CHECK(b.entryOf("7")->category == LineupCategory::other);
  CHECK(b.entryOf("7")->kind == ChannelKind::channel);
  CHECK(b.entryOf("8")->category == LineupCategory::free);
  CHECK(regionalSportsBrand("Bally Sports Ohio Plus"));
  CHECK(regionalSportsBrand("NBC Sports Chicago Plus"));
  CHECK(regionalSportsBrand("Root Sports Northwest Plus"));
  CHECK(regionalSportsBrand("AT&T SportsNet Rocky Mountain"));
  CHECK(regionalSportsBrand("Fox Sports Ohio"));
  CHECK_FALSE(regionalSportsBrand("Fox Sports 2"));
  CHECK_FALSE(regionalSportsBrand("NBC Sports Network"));
  CHECK_FALSE(regionalSportsBrand("FanDuel TV"));
}

TEST_CASE("Build 161: only stations and network feeds escape the channel list by a network's name") {
  Built b({channel("1", "US: FOX SPORTS TENNESSEE HD", "US| SPORTS"), channel("2", "TV: WNBA ON ION 1", "US| TV"),
           channel("3", "TV: UNIVISION EAST", "US| TV"), channel("4", "US: NBC LOS ANGELES NEWS", "US| NEWS"),
           channel("5", "US-S| CBS News Philadelphia FHD", "US-S| NEWS"), channel("6", "US: FOX 5 TESTVILLE", "US| LOCALS"),
           channel("7", "US: PARAMOUNT+ ORIGINAL 1", "US| MOVIES"), channel("8", "|US| TELEMUNDO 47", "|US| LATINO")});
  CHECK(b.entryOf("1")->category == LineupCategory::other);
  CHECK(b.entryOf("2")->category == LineupCategory::other);
  CHECK(b.entryOf("3")->category == LineupCategory::spanish);
  CHECK(b.entryOf("4")->category == LineupCategory::news);
  CHECK(b.entryOf("5")->category == LineupCategory::news);
  CHECK(b.entryOf("6")->category == LineupCategory::local);
  // A premium-looking name the list doesn't have.
  CHECK(b.entryOf("7")->category == LineupCategory::other);
  CHECK(b.entryOf("8")->category == LineupCategory::spanish);
}

TEST_CASE("Build 161: overflow feeds: numbered sister channels, alternates, and dark feeds") {
  std::vector<Channel> list = {channel("1", "|US| ESPN 3 ᴴᴰ", "|US| SPORTS"), channel("2", "TV: TENNIS CHANNEL 2", "US| TV"),
                               channel("3", "US: MLB NETWORK ALT", "US| SPORTS"),
                               channel("4", "US| NHL NETWORK HD [ALT] (S)", "|US| SLING"),
                               channel("5", "AT&T: NFL NETWORK ALT", "US| AT&T"), channel("6", "US: MLB NETWORK HD", "US| SPORTS"),
                               channel("7", "US: NHL NETWORK HD", "US| SPORTS")};
  Built b(list);
  // "ESPN 3" beside ESPN2: another channel, not ESPN's overflow.
  CHECK(b.entryOf("1")->id != "e|r:espn~x3");
  CHECK(b.entryOf("1")->category == LineupCategory::other);
  CHECK(b.entryOf("2")->id == "e|r:tennis-channel~x2");
  CHECK(b.entryOf("3")->id == "e|r:mlb-network-alternate");
  CHECK(b.entryOf("4")->id == "e|r:nhl-network-alternate");
  CHECK(b.entryOf("5")->id == "e|r:nfl-network~xalt");
  CHECK(b.entryOf("6")->id == "e|r:mlb-network");
  CHECK(b.entryOf("7")->id == "e|r:nhl-network");
  CHECK(isOverflowId("e|r:tennis-channel~x2"));
  CHECK(isOverflowId("e|r:bein-sports~x5~west"));
  CHECK_FALSE(isOverflowId("e|r:hbo~west"));
  CHECK_FALSE(isOverflowId("e|r:yes-network~alternate"));
  // Suggested only with a real programme, not "SIGN OFF".
  LineupInput in = lineupOf(b, list);
  in.prefs.requireGuide = false;
  in.guideIDs = {"xt:1|2"};
  const std::string tennis2 = b.entryOf("2")->id;
  CHECK_FALSE(lineup(in).lineupIDs.count(tennis2));
  in.liveGuideIDs = {"xt:1|2"};
  CHECK(lineup(in).lineupIDs.count(tennis2));
  CHECK_FALSE(realProgramme("SIGN OFF"));
  CHECK_FALSE(realProgramme("Sign-Off"));
  CHECK_FALSE(realProgramme("Off Air"));
  CHECK_FALSE(realProgramme("To Be Announced"));
  CHECK_FALSE(realProgramme(""));
  CHECK(realProgramme("Live: Serie A"));
  CHECK(realProgramme("Signing Day Special"));
}

TEST_CASE("Build 161: an East or unmarked copy never joins a West row") {
  Built b({channel("1", "|US| SHOWTIME EAST HD", "|US| MOVIES"), channel("2", "|US| SHOWTIME", "|US| MOVIES"),
           channel("3", "US: SHOWTIME WEST HD", "US| MOVIES"), channel("4", "TV: PARAMOUNT+ WITH SHOWTIME WEST", "US| TV"),
           channel("5", "US: MGM+ HD", "US| MOVIES"), channel("6", "US: EPIX WEST", "US| MOVIES")});
  CHECK(b.entryOf("1")->id == "e|r:paramount-plus-with-showtime");
  CHECK(b.entryOf("2")->id == "e|r:paramount-plus-with-showtime");
  CHECK(b.entryOf("3")->id == "e|r:paramount-plus-with-showtime~west");
  CHECK(b.entryOf("4") == b.entryOf("3"));
  CHECK(b.entryOf("5")->id == "e|r:mgm-plus");
  CHECK(b.entryOf("6")->id == "e|r:mgm-plus~west");
  // The keys leave the coast out; the coast a name says is read apart.
  CHECK(directory::exactKey("Showtime West") == "showtime");
  CHECK(directory::nameCoast("Showtime West") == directory::NameCoast::west);
  CHECK(directory::nameCoast("Max East") == directory::NameCoast::east);
  CHECK(directory::nameCoast("A&E (West)") == directory::NameCoast::west);
  CHECK(directory::nameCoast("Fox College Sports Pacific") == directory::NameCoast::west);
  CHECK(directory::nameCoast("Showtime") == directory::NameCoast::none);
  // "Alt" is part of a name now.
  CHECK(directory::exactKey("MLB Network Alt.") == "mlbnetworkalt");
}

TEST_CASE("Build 161: a US Spanish station without a call sign is its market's") {
  std::vector<Channel> list = {channel("1", "|US| TELEMUNDO 48 [SAN FRANCISCO]", "|US| LATINO"),
                               channel("2", "|US| TELEMUNDO 47", "|US| LATINO"),
                               channel("3", "|US| UNIVISION 65 [NEW JERSEY]", "|US| LATINO"),
                               channel("4", "|US| TELEMUNDO 47 [CLEVELAND]", "|US| LATINO"),
                               channel("5", "|US| UNIVISION LOS ANGELES", "|US| LATINO"),
                               channel("6", "|US| TELEMUNDO", "|US| LATINO"),
                               channel("7", "|US| TELEMUNDO 47 [ATLANTA]", "|US| LATINO")};
  Built b(list);
  CHECK(b.entryOf("1")->callsign == std::string("KSTS"));
  CHECK(b.entryOf("1")->id == "e|cs:KSTS|TELEMUNDO");
  // Two Telemundo stations are on 47: neither.
  CHECK_FALSE(b.entryOf("2")->callsign);
  CHECK(b.entryOf("3")->callsign == std::string("WUVP"));
  // Cleveland's Telemundo isn't on 47.
  CHECK_FALSE(b.entryOf("4")->callsign);
  CHECK(b.entryOf("4") != b.entryOf("2"));
  CHECK(b.entryOf("5")->callsign == std::string("KMEX"));
  CHECK(b.entryOf("7")->callsign == std::string("WKTB"));
  // The national feed stays the network's.
  CHECK_FALSE(b.entryOf("6")->callsign);

  // A San Francisco viewer with US Spanish channels: the market's station,
  // never another market's.
  LineupInput in = lineupOf(b, list);
  in.prefs.countries = {"US", "US-ES"};
  in.prefs.requireGuide = false;
  MarketChoice sf;
  sf.name = "San Francisco-Oakland-San Jose";
  sf.callsigns = {"KSTS"};
  in.prefs.markets = {sf};
  for (const Channel& c : list) in.guideIDs.insert(c.id);
  LineupOutput out = lineup(in);
  CHECK(out.lineupIDs.count(b.entryOf("1")->id));
  CHECK_FALSE(out.lineupIDs.count(b.entryOf("2")->id));
  CHECK_FALSE(out.lineupIDs.count(b.entryOf("3")->id));
  CHECK_FALSE(out.lineupIDs.count(b.entryOf("4")->id));
  CHECK_FALSE(out.lineupIDs.count(b.entryOf("5")->id));
  CHECK(out.lineupIDs.count(b.entryOf("6")->id));
  // In New York, "TELEMUNDO 47" is the market's station on 47 (WNJU).
  MarketChoice ny;
  ny.name = "New York";
  ny.callsigns = {"WNJU"};
  in.prefs.markets = {ny};
  CHECK(lineup(in).lineupIDs.count(b.entryOf("2")->id));
}

TEST_CASE("Build 161: the built-in reference data") {
  const reference::Index& index = reference::builtInIndex();
  if (index.data().empty()) return;  // built without it
  auto closed = [&](const char* id) { return index.channel(id) && index.channel(id)->closed; };
  CHECK(closed("circle"));
  CHECK(closed("nbc-sports-chicago"));
  CHECK(closed("nbc-sports-northwest"));
  CHECK(closed("fox-college-sports-atlantic"));
  CHECK_FALSE(closed("nbcsn"));
  CHECK(index.match("Fuel TV").channel == index.channel("fs2"));
  CHECK(index.match("CSN Bay Area").channel == index.channel("nbc-sports-bay-area"));
  CHECK(index.match("Univision TUDN").channel == index.channel("tudn"));
  CHECK(index.match("Max East").channel == index.channel("cinemax"));
  CHECK_FALSE(index.match("Max").channel);
  CHECK(index.match("Cinemax 5 Star Max").channel == index.channel("cinemax-classics"));
  REQUIRE(index.goneParent("HBO Family"));
  CHECK(index.goneParent("HBO Family")->id == "hbo");
  // Showtime: no East feed's name sends a copy West.
  CHECK_FALSE(index.match("Showtime").feed);
}

TEST_CASE("logos: the library's names split where the channel's run together") {
  CHECK(logos::logoSetFile("BYUtv") == std::string("byu-tv-us.png"));
  CHECK(logos::logoSetFile("MeTV") == std::string("me-tv-us.png"));
  CHECK(logos::logoSetFile("MeTV Toons") == std::string("me-tv-toons-us.png"));
}
