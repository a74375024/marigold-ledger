// End to end: US cable networks reach My Lineup whatever way a provider
// marks its US channels (Xtream "US|" groups, "USA:" names, M3U group
// words, "|US|" / "US -" / "(US)" names, or no marking at all), through the
// real build and lineup with a set-up lineup in one market.
#include <algorithm>
#include <functional>
#include <set>

#include "catalog/helpers.hpp"
#include "core/catalog/admission.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/lineup.hpp"
#include "core/catalog/work.hpp"
#include "core/guide/names.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::channel;
using testdata::utc;

namespace {

const std::vector<std::string> kCable = {"ESPN",       "ESPN2",   "CNN",    "Fox News", "TNT",     "TBS",
                                         "USA Network", "HGTV",   "Food Network", "Discovery Channel",
                                         "History",    "AMC",     "FX",     "Bravo",    "Nickelodeon",
                                         "Disney Channel", "Cartoon Network", "HBO", "MSNBC", "Comedy Central"};

struct Format {
  const char* what;
  std::string server;  // "xt:1" or "m3u:p"
  std::function<std::string(const std::string&)> name;
  std::function<std::string(const std::string&)> group;
};

// The lineup's channel names for a list made in `f`, plus a few non-US
// channels some providers mix in.
std::set<std::string> lineupNames(const Format& f, std::vector<Channel> extra = {}) {
  std::vector<Channel> list;
  int n = 0;
  for (const std::string& net : kCable) list.push_back(channel(std::to_string(++n), f.name(net), f.group(net), f.server));
  for (Channel& c : extra) list.push_back(std::move(c));
  std::vector<Channel> annotated = catalog::annotated(list);
  CatalogResult cat = build(annotated, utc(2026, 10, 26, 18, 0));
  LineupInput in;
  in.infos = cat.entries;
  in.channels = annotated;
  for (size_t i = 0; i < annotated.size(); ++i) in.indexByID[annotated[i].id] = i;
  in.prefs.configured = true;
  in.prefs.markets = {{"New York", std::string("Template:New York City TV"), {"WNBC", "WCBS", "WABC", "WNYW"}}};
  in.otherGroup = "Other";
  LineupOutput out = lineup(in);
  std::set<std::string> names;
  for (const Channel& e : out.lineupEntries) names.insert(e.displayName());
  return names;
}

void checkAll(const Format& f, const std::set<std::string>& names) {
  for (const std::string& net : kCable) {
    std::string label = std::string(f.what) + ": " + net;
    INFO(label);
    CHECK(names.count(net) == 1);
  }
}

}  // namespace

TEST_CASE("US cable networks join My Lineup however the provider marks US channels") {
  std::vector<Format> formats = {
      {"Xtream, US| group", "xt:1", [](const std::string& n) { return n + " HD"; },
       [](const std::string&) { return std::string("US| ENTERTAINMENT"); }},
      {"Xtream, USA| group and USA: name", "xt:1", [](const std::string& n) { return "USA: " + n; },
       [](const std::string&) { return std::string("USA| GENERAL"); }},
      {"M3U, USA group word", "m3u:p", [](const std::string& n) { return n; },
       [](const std::string&) { return std::string("USA Entertainment"); }},
      {"M3U, US| group", "m3u:p", [](const std::string& n) { return n + " FHD"; },
       [](const std::string&) { return std::string("US| Channels"); }},
      {"M3U, |US| name", "m3u:p", [](const std::string& n) { return "|US| " + n; },
       [](const std::string&) { return std::string("Entertainment"); }},
      {"M3U, US - name", "m3u:p", [](const std::string& n) { return "US - " + n; },
       [](const std::string&) { return std::string("Cable"); }},
      {"M3U, (US) suffix", "m3u:p", [](const std::string& n) { return n + " (US)"; },
       [](const std::string&) { return std::string("North America"); }},
      {"M3U, unmarked", "m3u:p", [](const std::string& n) { return n; },
       [](const std::string&) { return std::string("Entertainment"); }},
  };
  for (const Format& f : formats) {
    // Mixed lists: a few UK and Canadian channels marked as such.
    std::vector<Channel> extra = {channel("x1", "UK: BBC One", "UK| General", f.server),
                                  channel("x2", "UK: Sky Sports Main Event", "UK| Sports", f.server),
                                  channel("x3", "CA: TSN 1", "CA| Sports", f.server)};
    std::set<std::string> names = lineupNames(f, extra);
    checkAll(f, names);
    INFO(std::string(f.what));
    CHECK_FALSE(names.count("BBC One"));
  }
}

TEST_CASE("US locals marked, cable channels not: the cable networks still join") {
  Format cable{"M3U, unmarked cable next to marked locals", "m3u:p", [](const std::string& n) { return n; },
               [](const std::string&) { return std::string("Entertainment"); }};
  std::vector<Channel> extra;
  for (int i = 0; i < 30; ++i)
    extra.push_back(channel("l" + std::to_string(i), "US: Local " + std::to_string(i), "US| LOCALS", "m3u:p"));
  extra.push_back(channel("l-nbc", "US: NBC 4 New York (WNBC)", "US| LOCALS", "m3u:p"));
  std::set<std::string> names = lineupNames(cable, extra);
  checkAll(cable, names);
}

namespace {

// My Lineup for one market from a small list (stations from the directory).
std::set<std::string> marketLineup(const std::vector<Channel>& list, const std::vector<guide::Station>& stations,
                                   const MarketChoice& market, const std::set<std::string>& guideIds = {}) {
  std::vector<Channel> annotated = catalog::annotated(list);
  CatalogResult cat = build(annotated, utc(2026, 10, 26, 18, 0), stations);
  LineupInput in;
  in.infos = cat.entries;
  in.channels = annotated;
  for (size_t i = 0; i < annotated.size(); ++i) in.indexByID[annotated[i].id] = i;
  in.prefs.configured = true;
  in.prefs.markets = {market};
  in.guideIDs = guideIds;
  in.otherGroup = "Other";
  LineupOutput out = lineup(in);
  std::set<std::string> ids;
  for (const std::string& e : out.lineupIDs) {
    for (const EntryInfo& info : cat.entries)
      if (info.id == e) ids.insert(info.members.begin(), info.members.end());
  }
  return ids;
}

}  // namespace

TEST_CASE("regional sports networks join only their home market") {
  std::vector<Channel> list = {channel("yes", "US: YES Network", "US| SPORTS"),
                               channel("sny", "US: SNY", "US| SPORTS"),
                               channel("msg", "MSG HD", "US| SPORTS"),
                               channel("nbcsca", "US: NBC Sports California", "US| SPORTS"),
                               channel("nbcsba", "US: NBC Sports Bay Area", "US| SPORTS"),
                               channel("spec", "US: Spectrum SportsNet LA", "US| SPORTS"),
                               channel("fdsocal", "US: FanDuel Sports Network SoCal", "US| SPORTS"),
                               channel("marquee", "US: Marquee Sports Network", "US| SPORTS"),
                               channel("chsn", "US: Chicago Sports Network", "US| SPORTS")};
  auto ny = marketLineup(list, {}, {"New York", std::nullopt, {}});
  CHECK(ny == std::set<std::string>{"xt:1|yes", "xt:1|sny", "xt:1|msg"});
  auto la = marketLineup(list, {}, {"Los Angeles", std::nullopt, {}});
  CHECK(la == std::set<std::string>{"xt:1|spec", "xt:1|fdsocal"});  // not NBC Sports California
  auto sf = marketLineup(list, {}, {"San Francisco–Oakland–San Jose", std::nullopt, {}});
  CHECK(sf == std::set<std::string>{"xt:1|nbcsca", "xt:1|nbcsba"});
  auto chi = marketLineup(list, {}, {"Chicago", std::nullopt, {}});
  CHECK(chi == std::set<std::string>{"xt:1|marquee", "xt:1|chsn"});
}

TEST_CASE("locals named by call sign and network, outside a locals group") {
  // "WAAA ABC 7" style in a general group: three-letter signs the
  // directory knows, and a sign before its network that it doesn't.
  std::vector<guide::Station> stations = {{"KGO", "ABC", {"san", "francisco"}, "7"},
                                          {"WLS", "ABC", {"chicago"}, "7"}};
  std::vector<Channel> list = {channel("kgo", "KGO ABC 7", "Entertainment", "m3u:p"),
                               channel("wls", "WLS ABC 7", "Entertainment", "m3u:p"),
                               channel("wqqq", "WQQQ MyNetworkTV 9", "Entertainment", "m3u:p"),
                               channel("espn", "ESPN", "Sports", "m3u:p")};
  auto sf = marketLineup(list, stations, {"San Francisco–Oakland–San Jose", std::nullopt, {"KGO"}});
  CHECK(sf.count("m3u:p|kgo"));
  CHECK_FALSE(sf.count("m3u:p|wls"));
  auto chi = marketLineup(list, stations, {"Chicago", std::nullopt, {"WLS"}});
  CHECK(chi.count("m3u:p|wls"));
  CHECK_FALSE(chi.count("m3u:p|kgo"));
  auto ny = marketLineup(list, stations, {"New York", std::nullopt, {"WQQQ"}});
  CHECK(ny.count("m3u:p|wqqq"));
  CHECK(ny.count("m3u:p|espn"));
}

TEST_CASE("over the channel cap: the viewer's countries first, then unmarked groups") {
  // The provider lists Europe first; a cap that fits about half the list.
  std::vector<Channel> list;
  auto add = [&](const std::string& group, int count) {
    for (int i = 0; i < count; ++i) list.push_back(channel(group + std::to_string(i), "Ch " + std::to_string(i), group));
  };
  add("UK| SPORTS", 40);
  add("DE| SOCCER PPV", 30);
  add("US| ENTERTAINMENT", 25);
  add("US| NBC", 20);
  add("United States", 10);  // a group word, not a prefix
  add("PPV Live Events", 10);  // no country
  add("4K| UHD", 5);  // a number prefix is not a country
  Admission a = admitGroups(list, 80, countryRank({"US"}));
  std::set<std::string> groups;
  for (const Channel& c : a.channels) groups.insert(*c.group);
  CHECK(groups.count("US| ENTERTAINMENT"));
  CHECK(groups.count("US| NBC"));
  CHECK(groups.count("United States"));
  CHECK(groups.count("PPV Live Events"));
  CHECK(groups.count("4K| UHD"));
  CHECK_FALSE(groups.count("UK| SPORTS"));  // 70 taken, 40 more don't fit
  CHECK(a.channels.size() == 70);
  CHECK(a.cutGroups == 2);  // UK| SPORTS, DE| SOCCER PPV: sports, but no room
  // Admitted channels keep the provider's order.
  CHECK(*a.channels.front().group == "US| ENTERTAINMENT");
}

TEST_CASE("other countries: sports and event groups kept, the rest left out") {
  std::vector<Channel> list;
  auto add = [&](const std::string& group, const std::string& name) {
    list.push_back(channel(group + name, name, group));
  };
  add("UK| GENERAL", "BBC One");
  add("UK| SKY SPORTS", "Sky Sports Main Event");
  add("DE| KINDER", "KiKA");
  add("DE| SOCCER PPV", "Bundesliga 01");
  add("CA| TSN+ PPV", "TSN+ 03");
  add("SE| SVENSK HOCKEY PPV", "SHL 2");
  add("FR| FRANCE SPORT VIP", "beIN Sports 1");
  add("IT| CINEMA", "Rai Movie");
  add("US| ENTERTAINMENT", "USA Network");
  add("Entertainment", "UK: BBC Two");  // a mixed group: its name has no country
  add("PPV Live Events", "Event 01");
  Admission a = admitGroups(list, kMaxChannels, countryRank({"US"}));
  std::set<std::string> groups;
  for (const Channel& c : a.channels) groups.insert(*c.group);
  CHECK(groups == std::set<std::string>{"UK| SKY SPORTS", "DE| SOCCER PPV", "CA| TSN+ PPV", "SE| SVENSK HOCKEY PPV",
                                        "FR| FRANCE SPORT VIP", "US| ENTERTAINMENT", "Entertainment",
                                        "PPV Live Events"});
  CHECK(a.leftOut == 3);  // UK| GENERAL, DE| KINDER, IT| CINEMA
  CHECK(a.cutGroups == 0);
  // A country the viewer checked keeps all its groups.
  Admission withUK = admitGroups(list, kMaxChannels, countryRank({"US", "UK"}));
  CHECK(withUK.leftOut == 2);
  // No countries chosen: everything.
  CHECK(admitGroups(list, kMaxChannels, countryRank({})).channels.size() == list.size());
}

TEST_CASE("free streaming and 24/7 groups: told by their names or by most of their channels") {
  auto group = [](const std::string& name, const std::vector<std::string>& names, bool favorite = false) {
    std::vector<Channel> list;
    for (size_t i = 0; i < names.size(); ++i) list.push_back(channel(name + std::to_string(i), names[i], name));
    if (favorite) list.front().isFavorite = true;
    return list;
  };
  auto isFree = [](const std::vector<Channel>& list) {
    std::vector<const Channel*> members;
    for (const Channel& c : list) members.push_back(&c);
    return freeStreamingGroup(*list.front().group, members);
  };
  CHECK(isFree(group("US| ROKU ᴿᴬᵂ ⁶⁰ᶠᵖˢ", {"RK: BUZZR", "RK: HISTORY & WARFARE"})));
  CHECK(isFree(group("US Roku & Free TV", {"(US) (RK) TheGrio", "Court TV"})));
  CHECK(isFree(group("US| PRIME ᴿᴬᵂ ⁶⁰ᶠᵖˢ", {"PRIME: LOVE KILLS", "PRIME: HBO LATINO"})));
  CHECK(isFree(group("US| 24/7 COMEDY ᴿᴬᵂ ⁶⁰ᶠᵖˢ", {"US: 24/7 SEINFELD", "US: 24/7 FRIENDS"})));
  CHECK(isFree(group("US| 24/7 PPV", {"US: 24/7 BILLY THE EXTERMINATOR"})));
  // Mostly copies named as a free service's (three in four).
  CHECK(isFree(group("Free Mix", {"(PLUTO USA) BUZZR", "(PLUTO USA) Cheers", "(RK) Bob Ross", "Court TV"})));
  // A mixed group stays: its cable channels go where it goes.
  CHECK_FALSE(isFree(group("United States", {"(PLUTO USA) BUZZR", "(PLUTO UK) Cheers", "USA CBS EAST", "USA TNT"})));
  // A free service's pay-per-view group is sports.
  CHECK_FALSE(isFree(group("IT| AMAZON PRIME PPV", {"IT: SERIE A 01", "IT: SERIE A 02"})));
  CHECK_FALSE(isFree(group("US| ENTERTAINMENT", {"US: USA NETWORK", "US: TNT"})));
  // A group holding a server favourite never is.
  CHECK_FALSE(isFree(group("US| PLUTO TV", {"PLUTO: CHEERS", "PLUTO: BUZZR"}, true)));
}

TEST_CASE("over the channel cap: free streaming and 24/7 groups after other countries' sports, unless free is on") {
  std::vector<Channel> list;
  auto add = [&](const std::string& group, const std::string& name, int count) {
    for (int i = 0; i < count; ++i) list.push_back(channel(group + std::to_string(i), name + " " + std::to_string(i), group));
  };
  add("US| ROKU ᴿᴬᵂ ⁶⁰ᶠᵖˢ", "RK: Channel", 20);
  add("US| ENTERTAINMENT", "US: Channel", 20);
  add("US| 24/7 COMEDY ᴿᴬᵂ ⁶⁰ᶠᵖˢ", "US: 24/7 Show", 10);
  add("SE| SVENSK HOCKEY PPV", "LIVE | LULEA VS FROLUNDA | SE: SVENSK HOCKEY PPV", 20);
  add("ES| DAZN PPV", "Live | Real Betis - Sevilla | ES: DAZN PPV", 20);
  add("4K| 24/7 ᵁᴴᴰ ³⁸⁴⁰ᴾ", "4K: 24/7 Loop", 5);
  auto groupsOf = [](const Admission& a) {
    std::set<std::string> groups;
    for (const Channel& c : a.channels) groups.insert(*c.group);
    return groups;
  };
  // Free channels off: the US group and both sports groups; the free
  // groups, whatever their country, wait for room.
  Admission off = admitGroups(list, 65, countryRank({"US"}, false));
  CHECK(groupsOf(off) == std::set<std::string>{"US| ENTERTAINMENT", "SE| SVENSK HOCKEY PPV", "ES| DAZN PPV", "4K| 24/7 ᵁᴴᴰ ³⁸⁴⁰ᴾ"});
  CHECK(off.cutGroups == 2);
  CHECK(off.leftOut == 0);
  // Free channels on: they keep their country's rank; the sports groups wait.
  Admission on = admitGroups(list, 65, countryRank({"US"}, true));
  CHECK(groupsOf(on) == std::set<std::string>{"US| ROKU ᴿᴬᵂ ⁶⁰ᶠᵖˢ", "US| ENTERTAINMENT", "US| 24/7 COMEDY ᴿᴬᵂ ⁶⁰ᶠᵖˢ",
                                              "4K| 24/7 ᵁᴴᴰ ³⁸⁴⁰ᴾ"});
  CHECK(on.cutGroups == 2);
  // No countries chosen: free streaming last all the same.
  Admission any = admitGroups(list, 65, countryRank({}, false));
  CHECK_FALSE(groupsOf(any).count("US| ROKU ᴿᴬᵂ ⁶⁰ᶠᵖˢ"));
  CHECK(admitGroups(list, kMaxChannels, countryRank({}, false)).channels.size() == list.size());
  // Nothing over the cap: nothing cut, whatever the setting.
  CHECK(admitGroups(list, kMaxChannels, countryRank({"US"}, false)).channels.size() == list.size());
}

TEST_CASE("over the channel cap: other countries' sports groups with games before those of empty slots") {
  std::vector<Channel> list;
  auto add = [&](const std::string& group, int games, int empty, const std::string& emptyName) {
    for (int i = 0; i < games; ++i)
      list.push_back(channel(group + "g" + std::to_string(i), "LIVE | TEAM A VS TEAM B | " + std::to_string(i), group));
    for (int i = 0; i < empty; ++i)
      list.push_back(channel(group + "e" + std::to_string(i), emptyName + " " + std::to_string(i), group));
  };
  // In provider order, the emptiest first.
  add("SE| SVENSK HOCKEY PPV", 1, 9, "- NO EVENT STREAMING - | 8K EXCLUSIVE | SE: SVENSK HOCKEY PPV");
  add("MX| VIX PPV", 0, 10, "(MX) (VIX 17) |  (2098-12-31 08:00:01) VIX");
  add("ES| SOCCER PPV", 8, 2, "End | Ended Game | ES: SOCCER PPV");
  add("CA| TSN+ PPV", 10, 0, "");
  add("DE| SOCCER PPV", 5, 5, "NO EVENT STREAMING NOW - | DE: SOCCER PPV");
  Admission a = admitGroups(list, 30, countryRank({"US"}, false));
  std::set<std::string> groups;
  for (const Channel& c : a.channels) groups.insert(*c.group);
  CHECK(groups == std::set<std::string>{"CA| TSN+ PPV", "ES| SOCCER PPV", "DE| SOCCER PPV"});
  CHECK(a.cutGroups == 2);
  // Admitted channels keep the provider's order.
  CHECK(*a.channels.front().group == "ES| SOCCER PPV");
  // The place within the rank: empty slots per thousand.
  GroupRank rank = countryRank({"US"});
  std::vector<const Channel*> se;
  for (const Channel& c : list)
    if (*c.group == "SE| SVENSK HOCKEY PPV") se.push_back(&c);
  GroupPlace place = rank("SE| SVENSK HOCKEY PPV", se);
  CHECK(place.rank == 2);
  CHECK(place.within == 900);
  // A custom rank: ties keep the provider's order.
  Admission tied = admitGroups(list, 20, [](const std::string&, const std::vector<const Channel*>&) {
    return GroupPlace{1, 5};
  });
  CHECK(*tied.channels.front().group == "SE| SVENSK HOCKEY PPV");
  CHECK(tied.channels.size() == 20);
}

TEST_CASE("channel list fingerprint: same list, same value; any change, another") {
  std::vector<Channel> list = {channel("1", "ESPN", "US| SPORTS"), channel("2", "CNN", "US| NEWS")};
  uint64_t first = fingerprint(list);
  CHECK(fingerprint(list) == first);
  std::vector<Channel> renamed = list;
  renamed[1].name = "CNN HD";
  CHECK(fingerprint(renamed) != first);
  std::vector<Channel> regrouped = list;
  regrouped[0].group = "US| ENTERTAINMENT";
  CHECK(fingerprint(regrouped) != first);
  std::vector<Channel> reordered = {list[1], list[0]};
  CHECK(fingerprint(reordered) != first);
  std::vector<Channel> logo = list;
  logo[0].imageUrl = "logo";
  CHECK(fingerprint(logo) != first);
  // What decides the sort: the countries, and whether free channels are on.
  CHECK(fingerprint(list, {"US", "free:on"}) != fingerprint(list, {"US", "free:off"}));
}

TEST_CASE("free streaming: decided by all of a channel's copies, premium channels never") {
  std::vector<Channel> list = {
      // A free streaming channel whose first copy is in a catch-all group.
      channel("fc1", "AT&T: 50 CENT ACTION", "US| AT&T"), channel("fc2", "RK: 50 CENT ACTION", "US| ROKU"),
      channel("fc3", "TUBI: 50 CENT ACTION", "US| TUBI"),
      // Premium, with a copy under Roku and one under Prime: stays a channel.
      channel("mg1", "AT&T: MGM+ HITS", "US| AT&T"), channel("mg2", "RK: MGM+ HITS", "US| ROKU"),
      channel("hb1", "PRIME: HBO LATINO", "US| PRIME"), channel("hb2", "AT&T: HBO LATINO", "US| AT&T"),
      // A copy under Prime alone says nothing.
      channel("tb1", "AT&T: TBN", "US| AT&T"), channel("tb2", "PRIME: TBN", "US| PRIME"),
      // National networks are never free streaming.
      channel("cn1", "US: CNN", "US| NEWS"), channel("cn2", "PLUTO: CNN", "US| PLUTO TV")};
  std::vector<Channel> annotated = catalog::annotated(list);
  CatalogResult cat = build(annotated, utc(2026, 10, 26, 18, 0));
  auto entryOf = [&](const std::string& id) -> const EntryInfo& {
    const std::string& e = cat.entryOf.at("xt:1|" + id);
    return *std::find_if(cat.entries.begin(), cat.entries.end(), [&](const EntryInfo& x) { return x.id == e; });
  };
  CHECK(entryOf("fc1").kind == ChannelKind::fast);
  CHECK(entryOf("fc1").category == LineupCategory::free);
  CHECK(entryOf("fc1").members.size() == 3);
  CHECK(entryOf("mg1").kind == ChannelKind::channel);
  CHECK(entryOf("hb1").kind == ChannelKind::channel);
  CHECK(entryOf("hb1").category != LineupCategory::free);
  CHECK(entryOf("tb1").kind == ChannelKind::channel);
  CHECK(entryOf("cn1").kind == ChannelKind::channel);
  CHECK(entryOf("cn1").core == std::string("CNN"));

  // With free streaming off, My Lineup leaves 50 Cent Action out.
  LineupInput in;
  in.infos = cat.entries;
  in.channels = annotated;
  for (size_t i = 0; i < annotated.size(); ++i) in.indexByID[annotated[i].id] = i;
  in.prefs.configured = true;
  in.prefs.categories.erase(LineupCategory::free);
  in.prefs.requireGuide = false;
  in.otherGroup = "Other";
  LineupOutput out = lineup(in);
  CHECK_FALSE(out.lineupIDs.count(entryOf("fc1").id));
  CHECK(out.lineupIDs.count(entryOf("cn1").id));
}
