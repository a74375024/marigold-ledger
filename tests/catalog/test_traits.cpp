// Channel traits: clean names, quality, source tags, West feeds, country and
// market, call signs, kinds, cluster keys, categories and scope.
#include "catalog/helpers.hpp"
#include "core/catalog/catalog.hpp"
#include "core/util/strings.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::channel;

TEST_CASE("cleanName drops provider decorations") {
  CHECK(cleanName("US: ESPN FHD") == "ESPN");
  CHECK(cleanName("AT&T: A&E ᴿᴬᵂ") == "A&E");
  CHECK(cleanName("(US) (A) Fox News HD") == "Fox News HD");
  CHECK(cleanName("UK| Sky Thing Main") == "Sky Thing Main");
  CHECK(cleanName("USA  HBO EAST") == "HBO EAST");
  CHECK(cleanName("[Denver] NBC 9") == "NBC 9");
  CHECK(cleanName("Dream Channel (720p29 @ 3.2Mbps)") == "Dream Channel");
  CHECK(cleanName("Dream Channel (RK)") == "Dream Channel");
  CHECK(cleanName("Dream Channel ⱽᴵᴾ Backup") == "Dream Channel");
  CHECK(cleanName("PL VIP: Kanał Testowy") == "Kanał Testowy");
  // Nothing left: the name as it was.
  CHECK(cleanName("(US)") == "(US)");
}

TEST_CASE("coreKey and coreMatch") {
  CHECK(coreKey("A&E").key == "a and e");
  CHECK(coreKey("Disney+ HD").key == "disney plus");
  auto west = coreKey("HBO Pacific 4K");
  CHECK(west.key == "hbo");
  CHECK(west.west);
  CHECK(coreKey("ESPN East FHD").key == "espn");
  REQUIRE(coreMatch("A&E"));
  CHECK(coreMatch("A&E")->name == "A&E");
  CHECK(coreMatch("ESPN2 HD")->name == "ESPN2");
  CHECK(coreMatch("NAT GEO WILD HD US")->name == "Nat Geo Wild");
  CHECK(coreMatch("US CNN")->name == "CNN");  // leading US marker
  CHECK(coreMatch("CNN")->category == LineupCategory::news);
  CHECK_FALSE(coreMatch("Dream Channel"));
  CHECK_FALSE(coreMatch("US"));
}

TEST_CASE("a core network: quality, source tag, cluster key") {
  ChannelTraits t = traitsFor(channel("1", "US: ESPN FHD"));
  CHECK(t.quality == 2);
  CHECK(t.sourceTag == "us:");
  CHECK(t.country == std::string("US"));
  CHECK(t.market == std::string("US"));
  CHECK(t.core == std::string("ESPN"));
  CHECK(t.clusterKey == "core:ESPN");
  CHECK(t.displayName == "ESPN");
  CHECK(t.category == LineupCategory::sports);
  CHECK(t.kind == ChannelKind::channel);
  CHECK(t.scope == "national");
  CHECK_FALSE(t.isWest);
}

TEST_CASE("quality tokens, first listed wins") {
  CHECK(traitsFor(channel("1", "US: Dream Channel 4K")).quality == 3);
  CHECK(traitsFor(channel("1", "US: Dream Channel UHD HD")).quality == 3);
  CHECK(traitsFor(channel("1", "US: Dream Channel 1080")).quality == 2);
  CHECK(traitsFor(channel("1", "US: Dream Channel 720")).quality == 1);
  CHECK(traitsFor(channel("1", "US: Dream Channel SD")).quality == 0);
  CHECK(traitsFor(channel("1", "US: Dream Channel")).quality == 1);
}

TEST_CASE("source tags") {
  CHECK(traitsFor(channel("1", "Dream Channel (RK)")).sourceTag == "(rk)");
  CHECK(traitsFor(channel("1", "Dream Channel (Prime)")).sourceTag == "(prime)");
  CHECK(traitsFor(channel("1", "USA | Dream Channel")).sourceTag == "usa |");
  CHECK(traitsFor(channel("1", "MX: Canal Prueba")).sourceTag == "mx:");
  CHECK(traitsFor(channel("1", "Dream Channel")).sourceTag.empty());
}

TEST_CASE("West feeds cluster apart") {
  ChannelTraits core = traitsFor(channel("1", "US: HBO West"));
  CHECK(core.isWest);
  CHECK(core.clusterKey == "core:HBO~west");
  CHECK(core.displayName == "HBO West");
  ChannelTraits plain = traitsFor(channel("2", "US: Dream Channel Pacific"));
  CHECK(plain.isWest);
  CHECK(plain.clusterKey == "n:US:dreamchannel~west");
  CHECK(traitsFor(channel("3", "US: Dream Channel")).clusterKey == "n:US:dreamchannel");
}

TEST_CASE("country from Xtream group prefix, group name, name prefix") {
  CHECK(traitsFor(channel("1", "Canal Prueba", "MX| Canales")).country == std::string("MX"));
  CHECK(traitsFor(channel("1", "Thing", "GB| News")).country == std::string("UK"));
  // "4K|" blocks say nothing, and stop the search.
  CHECK_FALSE(traitsFor(channel("1", "UK: Thing", "4K| Sports")).country);
  // The Xtream code is taken as written, except that three-letter group
  // codes are the same countries (Marigold; tvOS kept "USA").
  CHECK(traitsFor(channel("1", "Thing", "USA| Movies")).country == std::string("US"));
  CHECK(traitsFor(channel("1", "Thing", "MEX| Canales")).country == std::string("MX"));
  CHECK(traitsFor(channel("1", "ESPN", "USA| Sports")).core == std::string("ESPN"));
  // Not Xtream: the group's name, then name prefixes.
  CHECK(traitsFor(channel("1", "Thing", "Mexico TV", "jf:1")).country == std::string("MX"));
  CHECK(traitsFor(channel("1", "(Pluto USA) Crime Show", std::nullopt, "jf:1")).country ==
        std::string("US"));
  CHECK(traitsFor(channel("1", "(Pluto France) Cuisine", std::nullopt, "jf:1")).country ==
        std::string("FRANCE"));
  CHECK(traitsFor(channel("1", "MEX: Canal Cinco", std::nullopt, "jf:1")).country ==
        std::string("MX"));
  CHECK(traitsFor(channel("1", "(VT) Kenh Thu", std::nullopt, "jf:1")).country ==
        std::string("VN"));
  CHECK(traitsFor(channel("1", "GB | Thing", std::nullopt, "jf:1")).country == std::string("UK"));
  CHECK(traitsFor(channel("1", "USA Thing", std::nullopt, "jf:1")).country == std::string("US"));
  CHECK(traitsFor(channel("1", "Thing", "USA Movies", "jf:1")).country == std::string("US"));
  CHECK_FALSE(traitsFor(channel("1", "ZZ: Thing", std::nullopt, "jf:1")).country);
  CHECK_FALSE(traitsFor(channel("1", "Thing", std::nullopt, "jf:1")).country);
}

TEST_CASE("markets: US Spanish and other countries") {
  ChannelTraits es = traitsFor(channel("1", "US: Telemundo Pacífico"));
  CHECK(es.market == std::string("US-ES"));
  CHECK(es.category == LineupCategory::spanish);
  CHECK_FALSE(es.isWest);  // "pacífico" is not "pacific"
  CHECK(traitsFor(channel("2", "US: Dream Channel", "US| Latino")).market == std::string("US-ES"));
  ChannelTraits mx = traitsFor(channel("3", "Canal Prueba", "MX| Canales"));
  CHECK(mx.market == std::string("MX"));
  CHECK(mx.category == LineupCategory::international);
  CHECK_FALSE(mx.core);
  // Never merged across countries.
  CHECK(mx.clusterKey == "n:MX:canalprueba");
  CHECK(traitsFor(channel("4", "Dream Channel", std::nullopt, "jf:1")).clusterKey ==
        "n:?:dreamchannel");
  // A core name outside the US is not a core network.
  CHECK_FALSE(traitsFor(channel("5", "UK: CNN")).core);
}

TEST_CASE("call signs: candidates and which are real") {
  auto c = callsignCandidates("US: ABC 7 (KGO) KABC-DT KXYZ WGN");
  REQUIRE(c.size() == 3);
  CHECK(c[0].sign == "KGO");  // three letters, in parentheses
  CHECK(c[0].inParens);
  CHECK(c[1].sign == "KABC");  // suffix
  CHECK(c[2].sign == "KXYZ");
  CHECK_FALSE(c[2].inParens);  // WGN: three letters, bare: not a candidate
  CHECK(callsignCandidates("KGO-TV")[0].sign == "KGO");
  CHECK(callsignCandidates("XKABC").empty());

  // A local group makes them real.
  ChannelTraits local = traitsFor(channel("1", "US: NBC 4 (WNBC)", "US Locals"));
  CHECK(local.callsign == std::string("WNBC"));
  CHECK(local.clusterKey == "cs:WNBC|NBC");
  CHECK(local.scope == "local");
  CHECK(local.category == LineupCategory::local);
  // Unknown and no local group: no call sign.
  CHECK_FALSE(traitsFor(channel("2", "US: KXYZ Weather")).callsign);
  // The station directory makes it real.
  std::vector<Channel> list = {channel("2", "US: KXYZ Weather")};
  CallsignContext context(list, {{"KXYZ", "ABC", {"springfield"}, "8"}});
  ChannelTraits known = traitsFor(list[0], context);
  CHECK(known.callsign == std::string("KXYZ"));
  CHECK(known.clusterKey == "cs:KXYZ|ABC");  // the sign's usual network
  // Never for other countries.
  std::vector<Channel> uk = {channel("3", "UK: KXYZ Weather")};
  CHECK_FALSE(traitsFor(uk[0], CallsignContext(uk, {{"KXYZ", "ABC", {"x"}, ""}})).callsign);
}

TEST_CASE("call sign context: network word next to a sign in parentheses") {
  std::vector<Channel> list = {channel("1", "US: FOX 2 (KTVU)"), channel("2", "US: KTVU Plus")};
  CallsignContext context(list, {});
  CHECK(context.known.count("KTVU"));
  CHECK(context.networkOf.at("KTVU") == "FOX");
  // Sister stations stay apart: the network word in the name wins.
  CHECK(traitsFor(channel("3", "US: CBS (KTVU)"), context).clusterKey == "cs:KTVU|CBS");
  CHECK(traitsFor(list[1], context).clusterKey == "cs:KTVU|FOX");
}

TEST_CASE("network + city resolves to a station") {
  std::vector<guide::Station> stations = {{"KTVU", "FOX", {"san", "francisco"}, "2"},
                                          {"KPIX", "CBS", {"san", "francisco"}, "5"}};
  std::vector<Channel> list = {channel("1", "US: FOX 2 LOCAL SAN FRANCISCO")};
  CallsignContext context(list, stations);
  ChannelTraits t = traitsFor(list[0], context);
  CHECK(t.callsign == std::string("KTVU"));
  CHECK(t.category == LineupCategory::local);
  // A national-sounding name doesn't.
  CHECK_FALSE(traitsFor(channel("2", "US: FOX Sports San Francisco"), context).callsign);
}

TEST_CASE("networkWord and virtualNumber") {
  CHECK(networkWord("US: NBC 4") == std::string("NBC"));
  CHECK(networkWord("TMO Puerto Rico") == std::string("TELEMUNDO"));
  CHECK(networkWord("MyNetworkTV 20") == std::string("MYTV"));
  CHECK(networkWord("Ind 11") == std::string("INDEPENDENT"));
  CHECK(networkWord("univision 14") == std::string("UNIVISION"));
  CHECK_FALSE(networkWord("FOXY Channel"));
  CHECK(virtualNumber("NBC 11 News") == 11);
  CHECK(virtualNumber("telemundo 47") == 47);
  CHECK_FALSE(virtualNumber("NBC Sports"));
  CHECK_FALSE(virtualNumber("NBC 123"));
}

TEST_CASE("kinds") {
  auto kind = [](const std::string& name, const std::string& group = "") {
    return kindFor(name, str::lower(name), str::lower(group), false);
  };
  CHECK(kind("#### PRIME ####") == ChannelKind::separator);
  CHECK(kind("== Sports ==") == ChannelKind::separator);
  CHECK(kind("Movies ###") == ChannelKind::separator);
  CHECK(kind("NO EVENT STREAMING") == ChannelKind::placeholder);
  CHECK(kind("Slot tvg-id=abc") == ChannelKind::placeholder);
  CHECK(kind("ENDED | Team A vs Team B") == ChannelKind::placeholder);
  CHECK(kind("NCAAF 16: USA NETWORK") == ChannelKind::event);
  CHECK(kindFor("NCAAF 16: USA NETWORK", "ncaaf 16: usa network", "", true) == ChannelKind::event);
  CHECK(kindFor("ESPN", "espn", "ppv", true) == ChannelKind::channel);
  CHECK(kind("Team A vs Team B", "PPV Events") == ChannelKind::event);
  CHECK(kind("Fighter Night 12", "PPV") == ChannelKind::event);
  CHECK(kind("Abc", "League Pass") == ChannelKind::event);  // too short a name in an event group
  CHECK(kind("LIVE | Team A vs Team B") == ChannelKind::event);
  CHECK(kind("Event 12") == ChannelKind::event);
  CHECK(kind("NCAAF 16 Team A at Team B 3:30PM") == ChannelKind::event);
  CHECK(kind("Sitcom Classics 24/7") == ChannelKind::loop247);
  CHECK(kind("Sitcom Classics", "24/7 Channels") == ChannelKind::loop247);
  CHECK(kind("(Pluto) Crime Show") == ChannelKind::fast);
  CHECK(kind("Crime Show", "Roku Channels") == ChannelKind::fast);
  CHECK(kind("Samsung TV: Crime Show") == ChannelKind::fast);
  CHECK(kind("iHeart Hits") == ChannelKind::radio);
  CHECK(kind("Hits", "Radio") == ChannelKind::radio);
  CHECK(kind("Dream Channel") == ChannelKind::channel);
  CHECK(isEventGroup("us| espn+ ppv"));
  CHECK(isEventGroup("football"));
  CHECK_FALSE(isEventGroup("us entertainment"));
}

TEST_CASE("categories from names and groups") {
  auto cat = [](const std::string& name, std::optional<std::string> group = std::nullopt) {
    return traitsFor(channel("1", name, std::move(group))).category;
  };
  CHECK(cat("US: Dream News Now") == LineupCategory::news);
  CHECK(cat("US: Dream Sports Plus") == LineupCategory::entertainment);
  CHECK(cat("US: Dream Thing", "US| Sports") == LineupCategory::sports);
  CHECK(cat("US: Dream Golf Live") == LineupCategory::sports);
  CHECK(cat("US: Toon Planet") == LineupCategory::kids);
  CHECK(cat("US: Movie Vault") == LineupCategory::movies);
  CHECK(cat("US: Music Box") == LineupCategory::music);
  CHECK(cat("US: Formula 1 Onboard") == LineupCategory::sports);
  CHECK(cat("US: Dream Thing") == LineupCategory::entertainment);
  CHECK(cat("US: Hits FM") == LineupCategory::radio);
  CHECK(cat("US: Sitcoms 24/7") == LineupCategory::free);
}

TEST_CASE("scope: team, regional, national") {
  ChannelTraits team = traitsFor(channel("1", "US: NFL Testville Tigers"));
  CHECK(team.scope == "team");
  ChannelTraits regional = traitsFor(channel("2", "US: NBC Sports Boston"));
  CHECK(regional.scope == "regional:boston");
  CHECK(regional.category == LineupCategory::sports);
  ChannelTraits city = traitsFor(channel("3", "US: FOX 5 [SAN DIEGO]"));
  CHECK(city.scope == "regional:san diego");
  CHECK(city.category == LineupCategory::local);  // network word, not a national name
  ChannelTraits unknown = traitsFor(channel("4", "US: CBS Testville"));
  CHECK(unknown.scope == "regional:?");
  CHECK(unknown.category == LineupCategory::local);
  CHECK(traitsFor(channel("5", "US: Dream Channel")).scope == "national");
  CHECK(traitsFor(channel("6", "UK: NBC Sports Boston")).scope == "national");
}

TEST_CASE("familyParts") {
  auto f = familyParts("ESPN+ 48: Team A vs Team B");
  REQUIRE(f);
  CHECK(f->key == "espn+ #");
  CHECK(f->number == 48);
  CHECK(f->rest == std::string("Team A vs Team B"));
  CHECK(f->separator);
  auto paren = familyParts("US (ESPN+ 006) | The Morning Show");
  REQUIRE(paren);
  CHECK(paren->key == "us (espn+ #)");
  CHECK(paren->number == 6);
  CHECK(paren->separator);
  auto bare = familyParts("PPV 03");
  REQUIRE(bare);
  CHECK(bare->key == "ppv #");
  CHECK_FALSE(bare->rest);
  CHECK_FALSE(bare->separator);
  auto words = familyParts("NCAAF 02 Florida @ Texas 12PM");
  REQUIRE(words);
  CHECK(words->key == "ncaaf #");
  CHECK(words->number == 2);
  CHECK(words->rest == std::string("Florida @ Texas 12PM"));
  CHECK(words->separator);
  // Word-prefix form needs an event in the rest.
  CHECK_FALSE(familyParts("Channel 4 Testville Tonight"));
  CHECK_FALSE(familyParts("Dream Channel"));
}

TEST_CASE("trailingNumber, bareCallsign, isPlaceholder") {
  CHECK(trailingNumber("PPV 0012 HD") == 12);
  CHECK(trailingNumber("Slot 7: Team A vs Team B") == 7);
  CHECK_FALSE(trailingNumber("No number"));
  CHECK(bareCallsign("(US) (Prime) KAGS") == std::string("KAGS"));
  CHECK(bareCallsign("USA: WITI (FOX)") == std::string("WITI"));
  CHECK_FALSE(bareCallsign("WWE Network"));
  CHECK_FALSE(bareCallsign("KING 5"));
  CHECK(isPlaceholder("No events today"));
  CHECK(isPlaceholder("12."));
  CHECK(isPlaceholder("--"));
  CHECK(isPlaceholder("ab"));
  CHECK_FALSE(isPlaceholder("Team A vs Team B"));
}
