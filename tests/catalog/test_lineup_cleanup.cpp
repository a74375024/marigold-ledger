// My Lineup clean-up: one channel spelled two ways is one entry, a
// provider's codes don't hide a national network, event-only feeds are
// events, channels in other languages belong to their market, and
// out-of-town stations, regional channels and regional sports networks
// only join the lineup in their area.
#include <algorithm>

#include "catalog/helpers.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/lineup.hpp"
#include "core/guide/names.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::channel;
using testdata::utc;

namespace {

CatalogResult built(const std::vector<Channel>& list, const std::vector<guide::Station>& stations = {}) {
  return build(list, utc(2026, 10, 28, 18, 0), stations);
}

// The entries holding the channels `ids` (by item id), in order.
std::vector<const EntryInfo*> entriesOf(const CatalogResult& r, const std::vector<std::string>& ids) {
  std::vector<const EntryInfo*> out;
  for (const std::string& id : ids) {
    auto it = r.entryOf.find("xt:1|" + id);
    if (it == r.entryOf.end()) it = r.entryOf.find("jf:1|" + id);
    const EntryInfo* found = nullptr;
    if (it != r.entryOf.end()) {
      for (const EntryInfo& e : r.entries)
        if (e.id == it->second) found = &e;
    }
    out.push_back(found);
  }
  return out;
}

}  // namespace

TEST_CASE("one channel spelled two ways is one entry") {
  const char* pairs[][2] = {
      {"US: UP TV HD", "AT&T: UPTV ᴿᴬᵂ"},
      {"US: FYI HD", "USA: FYI NETWORK"},
      {"US: BABY TV HD", "AT&T: BABYTV ᴿᴬᵂ"},
      {"US: THE COWBOY CHANNEL", "US: Cowboy Channel"},
      {"US: Willow Cricket Extra", "US: Willow Cricket Xtra"},
  };
  for (const auto& pair : pairs) {
    CAPTURE(pair[0]);
    CatalogResult r = built({channel("1", pair[0], "US| ENTERTAINMENT"), channel("2", pair[1], "US| ENTERTAINMENT")});
    auto entries = entriesOf(r, {"1", "2"});
    REQUIRE(entries[0]);
    CHECK(entries[0] == entries[1]);
    CHECK(entries[0]->members.size() == 2);
    // The first one seen keeps its id.
    CHECK(entries[0]->id == "e|" + r.traits["xt:1|1"].clusterKey);
  }
  // Two channels stay two.
  CatalogResult r = built({channel("1", "US: Willow Cricket", "US| SPORT"), channel("2", "US: Willow Cricket Extra", "US| SPORT"),
                           channel("3", "US: Nick Jr", "US| KIDS"), channel("4", "US: MeTV", "US| ENTERTAINMENT"),
                           channel("5", "US: MeTV Toons", "US| ENTERTAINMENT")});
  auto entries = entriesOf(r, {"1", "2", "4", "5"});
  CHECK(entries[0] != entries[1]);
  CHECK(entries[2] != entries[3]);
}

TEST_CASE("a provider's codes don't hide a national network") {
  auto coreOf = [](const char* name, const char* group = "US Locals & Regional") {
    return traitsFor(channel("1", name, std::string(group), "jf:1"));
  };
  CHECK(coreOf("(US) (SP) The Movie Channel (TMC) (East)").core == std::string("The Movie Channel"));
  CHECK(coreOf("US Game Show Network (GSN) (A)").core == std::string("Game Show Network"));
  CHECK(coreOf("US National Geographic (NGCHD) (H)").core == std::string("National Geographic"));
  CHECK(coreOf("US: HALLMARK M & M HD").core == std::string("Hallmark Movies & Mysteries"));
  CHECK(coreOf("US: HALLMARK MOVIES/MYSTERIES").core == std::string("Hallmark Movies & Mysteries"));
  CHECK(coreOf("US: VH 1 HD").core == std::string("VH1"));
  CHECK(coreOf("US: TENNIS HD").core == std::string("Tennis Channel"));
  CHECK(coreOf("US (English) beIN SPORTS (S)").core == std::string("beIN Sports"));
  // A feed from a West city is the West feed.
  ChannelTraits miami = coreOf("(US) (H) [Miami] Oxygen True Crime (OXYGEN)");
  ChannelTraits seattle = coreOf("(US) (H) [Seattle] Oxygen True Crime (OXYGENP)");
  CHECK(miami.core == std::string("Oxygen"));
  CHECK_FALSE(miami.isWest);
  CHECK(seattle.clusterKey == "core:Oxygen~west");
  // Not a channel's code: "(SP)" is the provider's source tag, not Spanish.
  CHECK(coreOf("(US) (SP) The Movie Channel Extra (TMC) (East)").market == std::string("US"));
  // The code isn't shown, and a West version says so.
  CatalogResult r = built({channel("1", "(US) (H) [Miami] ROAR (ROARTV)", std::string("US Locals & Regional"), "jf:1"),
                           channel("2", "(US) (H) [Seattle] ROAR (ROARTV)", std::string("US Locals & Regional"), "jf:1")});
  auto entries = entriesOf(r, {"1", "2"});
  REQUIRE(entries[0]);
  REQUIRE(entries[1]);
  CHECK(entries[0]->name == "ROAR");
  CHECK(entries[1]->name == "ROAR West");
  CHECK(entries[1]->id == coastPartnerId(entries[0]->id));
}

TEST_CASE("event-only feeds are events, one family per brand") {
  CatalogResult r = built({channel("1", "(US) (A) NBC Sports 1", std::string("US Locals & Regional"), "jf:1"),
                           channel("2", "(US) (A) NBC Sports 2", std::string("US Locals & Regional"), "jf:1"),
                           channel("3", "US: NBC Sports 4K UHD (Event Only)", std::string("US Sports"), "jf:1"),
                           channel("4", "US: TENNIS CHANNEL PLUS 9", std::string("US| SPORT")),
                           channel("5", "US: NBC Sports Bay Area", std::string("US Sports"), "jf:1")});
  CHECK(r.traits["jf:1|1"].kind == ChannelKind::event);
  CHECK(r.traits["jf:1|3"].familyKey == std::string("feeds:nbc sports"));
  CHECK(r.traits["xt:1|4"].familyKey == std::string("feeds:tennis channel plus"));
  CHECK(r.traits["jf:1|5"].kind == ChannelKind::channel);
  auto nbc = std::find_if(r.families.begin(), r.families.end(),
                          [](const EventFamily& f) { return f.id == "fam|feeds:nbc sports"; });
  REQUIRE(nbc != r.families.end());
  CHECK(nbc->name == "NBC Sports");
  CHECK(nbc->feeds.size() == 3);
  CHECK(r.entryOf.count("jf:1|1") == 0);  // not a My Lineup channel
}

TEST_CASE("a news channel's plain copy beside one marked English is the home language's") {
  CatalogResult r = built({channel("1", "AT&T: FRANCE 24 ᴿᴬᵂ", std::string("US| AT&T")),
                           channel("2", "AT&T: FRANCE 24 (ENGLISH) ᴿᴬᵂ", std::string("US| AT&T")),
                           channel("3", "US: AL JAZEERA", std::string("US| NEWS")),
                           channel("4", "US: AL JAZEERA ENGLISH", std::string("US| NEWS")),
                           channel("5", "US: DW", std::string("US| NEWS"))});
  CHECK(r.traits["xt:1|1"].market.value_or("none") == "FR");
  CHECK(r.traits["xt:1|1"].country.value_or("none") == "US");
  CHECK(r.traits["xt:1|2"].market == std::optional<std::string>("US"));
  CHECK(r.traits["xt:1|3"].market == std::optional<std::string>("AE"));
  CHECK(r.traits["xt:1|4"].market == std::optional<std::string>("US"));
  // No English copy listed: the plain one is the English feed, as usual.
  CHECK(r.traits["xt:1|5"].market == std::optional<std::string>("US"));
  // Another server's English copy says nothing about this one's.
  CatalogResult other = built({channel("1", "US: FRANCE 24", std::string("US| NEWS")),
                               channel("2", "US: FRANCE 24 (ENGLISH)", std::string("US| NEWS"), "xt:2")});
  CHECK(other.traits["xt:1|1"].market == std::optional<std::string>("US"));
}

TEST_CASE("channels in another language belong to its market") {
  auto marketOf = [](const char* name, const char* group, const char* server = "xt:1") {
    return traitsFor(channel("1", name, std::string(group), server)).market.value_or("");
  };
  CHECK(marketOf("US (FR) France 24 (S)", "United States", "jf:1") == "FR");
  CHECK(marketOf("AT&T: FRANCE 24 (ENGLISH) ᴿᴬᵂ", "US| AT&T") == "US");
  CHECK(marketOf("AT&T: AASTHA TV ᴿᴬᵂ", "US| AT&T") == "IN");
  CHECK(marketOf("AT&T: ARY DIGITAL ᴿᴬᵂ", "US| AT&T") == "PK");
  CHECK(marketOf("AT&T: MEXICANAL ᴿᴬᵂ", "US| AT&T") == "US-ES");
  CHECK(marketOf("US: ANTENA 3 INTERNACIONAL", "US| MOVIES") == "US-ES");
  CHECK(marketOf("AT&T: CINEMA DINAMITA ᴿᴬᵂ", "US| AT&T") == "US-ES");
  CHECK(marketOf("US (Spanish) beIN SPORTS en EspaÃ±ol (S)", "US Sports", "jf:1") == "US-ES");
  CHECK(marketOf("USA  NICK JR.TOO", "United States", "jf:1") == "UK");
  CHECK(marketOf("US: SKY NEWS ARABIA", "US| NEWS") == "AE");
  CHECK(marketOf("US: ZEE TV", "US| ENTERTAINMENT") == "IN");
  CHECK(marketOf("US: Zeta Channel", "US| ENTERTAINMENT") == "US");
  // Not suggested for US English.
  LineupPrefs prefs;
  prefs.configured = true;
  CatalogResult r = built({channel("1", "AT&T: ARY DIGITAL ᴿᴬᵂ", std::string("US| AT&T"))});
  REQUIRE(r.entries.size() == 1);
  CHECK(r.entries[0].category == LineupCategory::international);
  CHECK_FALSE(suggests(r.entries[0], prefs, true, r.entries[0].name));
  prefs.countries.insert("PK");
  CHECK(suggests(r.entries[0], prefs, true, r.entries[0].name));
}

TEST_CASE("out-of-town stations under other names") {
  CHECK(networkWord("PBS12") == std::string("PBS"));
  CHECK(networkWord("ABC7 BAY AREA SAN FRANCISCO") == std::string("ABC"));
  CHECK(networkWord("MY 40 Greenville (WMYA)") == std::string("MYTV"));
  CHECK(networkWord("MYNET   KY   PADUCAH (WDKA)") == std::string("MYTV"));
  CHECK_FALSE(networkWord("My Little Channel"));
  CHECK_FALSE(networkWord("NBCSN"));
  // MyNetworkTV stations: their call signs, so only their market's lineup.
  CatalogResult r = built({channel("1", "US: MYNET   KY   PADUCAH (WDKA)", std::string("United States"), "jf:1"),
                           channel("2", "US MY 49 (WDKA) Paducah (A)", std::string("United States"), "jf:1"),
                           channel("3", "US: MY 40 Greenville (WMYA)", std::string("USA Premium"), "jf:1")});
  auto entries = entriesOf(r, {"1", "2", "3"});
  REQUIRE(entries[0]);
  CHECK(entries[0] == entries[1]);
  CHECK(entries[0]->callsign == std::string("WDKA"));
  CHECK(entries[2]->callsign == std::string("WMYA"));
  CHECK(entries[2]->category == LineupCategory::local);
  // WGN: alone in a locals group, and with its number elsewhere.
  r = built({channel("1", "US WGN (A)", std::string("US Locals & Regional"), "jf:1"),
             channel("2", "US: WGN9", std::string("US| ENTERTAINMENT"))});
  entries = entriesOf(r, {"1", "2"});
  REQUIRE(entries[0]);
  CHECK(entries[0] == entries[1]);
  CHECK(entries[0]->callsign == std::string("WGN"));
  LineupPrefs sf;
  sf.configured = true;
  sf.markets = {{"San Francisco–Oakland–San Jose", std::nullopt, {"KGO", "KPIX"}}};
  CHECK_FALSE(suggests(*entries[0], sf, true, entries[0]->name));
  // "ABC7 Bay Area" is KGO, with the station directory.
  std::vector<guide::Station> stations = {{"KGO", "ABC", {"san", "francisco"}, "7"}};
  r = built({channel("1", "TV: ABC7 BAY AREA SAN FRANCISCO ᴿᴬᵂ", std::string("US| TV")),
             channel("2", "US: ABC 7 (KGO) SAN FRANCISCO HD", std::string("US| ABC"))},
            stations);
  entries = entriesOf(r, {"1", "2"});
  REQUIRE(entries[0]);
  CHECK(entries[0] == entries[1]);
  CHECK(suggests(*entries[0], sf, true, entries[0]->name));
}

TEST_CASE("regional channels and regional sports networks by their area") {
  auto places = [](const char* name) { return regionalSportsPlaces(name).value_or(std::vector<std::string>{}); };
  CHECK(places("FOX SPORTS NORTH HD") == std::vector<std::string>{"minneapolis", "paul"});
  CHECK(places("FOX SPORTS PRIME TICKET HD") == std::vector<std::string>{"los angeles"});
  CHECK(places("FOX SPORTS SOUTH EAST HD") == std::vector<std::string>{"atlanta"});
  CHECK(places("SPORTSNET LA DODGERS HD") == std::vector<std::string>{"los angeles"});
  CHECK(places("NBC SPORTS PHILADELFIA") == std::vector<std::string>{"philadelphia"});
  CHECK(places("Gulf Coast Sports and Entertainment Network") == std::vector<std::string>{"mobile", "pensacola"});
  CHECK_FALSE(regionalSportsPlaces("Fox Sports Racing"));
  CHECK(regionalChannelPlaces("NECN HD") == std::vector<std::string>{"boston"});
  CHECK(regionalChannelPlaces("Spectrum OC16") == std::vector<std::string>{"honolulu"});
  CHECK(regionalChannelPlaces("Spectrum News 1") == std::vector<std::string>{"?"});
  CHECK_FALSE(regionalChannelPlaces("CNN"));
  ChannelTraits necn = traitsFor(channel("1", "US: NECN HD", std::string("US| NEWS")));
  CHECK(necn.scope == "regional:boston");
  CHECK(necn.category == LineupCategory::news);
  CHECK(traitsFor(channel("1", "US: FOX SPORTS NORTH HD", std::string("US| SPORT"))).scope == "regional:minneapolis|paul");

  // In its area: a regional sports network joins before its guide loads;
  // its overflow channels and networks it doesn't know only with one.
  LineupPrefs sf;
  sf.configured = true;
  sf.markets = {{"San Francisco–Oakland–San Jose", std::nullopt, {"KGO"}}};
  auto entry = [](const char* name, const char* scope) {
    EntryInfo e;
    e.id = std::string("e|n:US:") + name;
    e.name = name;
    e.category = LineupCategory::sports;
    e.market = "US";
    e.scope = scope;
    return e;
  };
  EntryInfo main = entry("NBC Sports Bay Area", "regional:san francisco|oakland|san jose");
  EntryInfo overflow = entry("NBC Sports California Plus 2", "regional:san francisco|oakland|san jose|sacramento");
  EntryInfo closed = entry("PAC 12 BAY AREA", "regional:bay area");
  CHECK(suggests(main, sf, false, main.name));
  CHECK_FALSE(suggests(overflow, sf, false, overflow.name));
  CHECK(suggests(overflow, sf, true, overflow.name));
  CHECK_FALSE(suggests(closed, sf, false, closed.name));
  CHECK(suggests(closed, sf, true, closed.name));
  EntryInfo north = entry("FOX SPORTS NORTH HD", "regional:minneapolis|paul");
  CHECK_FALSE(suggests(north, sf, true, north.name));
}

TEST_CASE("premium channels that don't say so, free streaming ones in a cable group, loops and overflow feeds") {
  CatalogResult r = built({channel("1", "US: 5 STARMAX", std::string("US Movies")),
                           channel("2", "US: ACTIONMAX", std::string("US Movies")),
                           channel("3", "US: MOVIEPLEX", std::string("US| ENTERTAINMENT")),
                           channel("4", "US SHO x BET (S)", std::string("US Movies"), "jf:1"),
                           channel("5", "AT&T: HORROR MACHINE ᴿᴬᵂ", std::string("US| AT&T")),
                           channel("6", "AT&T: TRAVELXP HD ᴿᴬᵂ", std::string("US| AT&T")),
                           channel("7", "CINEBOX: FAMILY", std::string("US Movies")),
                           channel("8", "(US) NBC Sports Network 2 (Feed) (NBCSN)", std::string("United States"), "jf:1"),
                           channel("9", "AT&T: TBN ᴿᴬᵂ", std::string("US| AT&T"))});
  auto entryOf = [&](const std::string& id) -> const EntryInfo* {
    auto it = r.entryOf.find(id);
    if (it == r.entryOf.end()) return nullptr;
    for (const EntryInfo& e : r.entries)
      if (e.id == it->second) return &e;
    return nullptr;
  };
  for (const char* id : {"xt:1|1", "xt:1|2", "xt:1|3", "jf:1|4"}) {
    REQUIRE(entryOf(id));
    CHECK(entryOf(id)->category == LineupCategory::movies);
  }
  for (const char* id : {"xt:1|5", "xt:1|6", "xt:1|7"}) {
    REQUIRE(entryOf(id));
    CHECK(entryOf(id)->category == LineupCategory::free);
  }
  CHECK(r.traits["jf:1|8"].kind == ChannelKind::event);
  CHECK(r.entryOf.count("jf:1|8") == 0);
  // A channel on the air and on cable isn't free streaming for being free.
  REQUIRE(entryOf("xt:1|9"));
  CHECK(entryOf("xt:1|9")->category == LineupCategory::entertainment);
}
