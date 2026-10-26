// End to end: US cable networks reach My Lineup whatever way a provider
// marks its US channels (Xtream "US|" groups, "USA:" names, M3U group
// words, "|US|" / "US -" / "(US)" names, or no marking at all), through the
// real build and lineup with a set-up lineup in one market.
#include <algorithm>
#include <functional>
#include <set>

#include "catalog/helpers.hpp"
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
