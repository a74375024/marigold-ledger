// Maps a game's TV networks to the viewer's channels (SportsNetworkMap and
// RegionalNetworks at the end of LiveTVStore.swift), and how stations are
// ranked on a tile (LiveTVStore.StationRank, stationRank, stationDetail,
// StationMarkets). Pure; the channel index is built once per channel list
// and reused by every rebuild.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/catalog/lineup.hpp"
#include "core/catalog/types.hpp"
#include "core/model/channel.hpp"
#include "core/sports/types.hpp"

namespace marigold::sports {

namespace matcher {
struct RosterLeague;
}

// Station ranks, best first. Lower is listed first on a tile.
namespace station_rank {
constexpr int lineupHome = 0;
constexpr int lineup = 1;
constexpr int home = 2;
constexpr int market = 3;
constexpr int elsewhere = 4;
// Ranks that count as "your local station".
inline bool isLocal(int rank) { return rank == home || rank == market; }
}  // namespace station_rank

// Call signs of your markets, for ranking stations: the home market (picked
// in Settings, else the first) and the others. Base call signs ("KTVU" for
// "KTVU-DT"), upper-cased.
struct StationMarkets {
  std::set<std::string> home;
  std::set<std::string> others;
  bool operator==(const StationMarkets& o) const { return home == o.home && others == o.others; }
};

// The market Live Events treats as home: the one picked, else the first.
std::optional<std::string> homeMarketName(const catalog::LineupPrefs& prefs);
StationMarkets stationMarkets(const catalog::LineupPrefs& prefs);
// Live Events' home city button: the next of your markets (nullopt with
// fewer than two). The caller saves it as prefs.homeMarket and re-reads the
// guide's game listings, since station ranks are worked out there.
std::optional<std::string> nextHomeMarket(const catalog::LineupPrefs& prefs);

// A station's rank: My Lineup (home first), then your home market, your
// other markets, anywhere else.
int stationRank(const catalog::EntryInfo& info, const StationMarkets& markets, bool inLineup);
// How a station shows on a tile, by rank: "In My Lineup", "Your local
// station", "In your channels".
std::string stationDetail(int rank);

// Regional sports networks under their many names: the schedule writes
// "FDSN Sun" or "NBCS-BA"; channel lists write "FanDuel Sports Sun", "BALLY
// SPORTS SUN (MIAMI) HD" or the older "FOX SPORTS SUN HD". All map to one key.
namespace regional {
// "fd-sun", "nbcs-ba", "yes"... or nullopt.
std::optional<std::string> key(std::string_view name);
// A station's call sign written in its channel name: "(KTVU)", "(PBS
// WKYU)", "SALT LAKE CITY, UT KJZZ IND 14", "IND KFAA".
std::optional<std::string> callsignInName(std::string_view name);
// "KJZZ" from "KJZZ", "KJZZ-TV", "kjzz-dt" or "KUNP 16": a station call sign.
std::optional<std::string> callsign(std::string_view name);
// Stations the schedule names by their brand instead of a call sign
// ("Arizona's Family CBS5" -> "KPHO").
std::optional<std::string> stationBrand(std::string_view name);
}  // namespace regional

namespace network_map {

// "core:FS1" / "net:FOX" / "rsn:fd-sun" / "call:KJZZ" for a network name in
// the schedule, or nullopt (streaming services).
std::optional<std::string> networkKey(std::string_view name);

// Your channels by what they carry: "core:FS1", "net:FOX", "rsn:fd-sun",
// "call:KJZZ", "team:NBA|14", "card:ufc|main".
struct ChannelIndex {
  struct Item {
    std::string id;
    bool inLineup = false;
    bool spanish = false;
    int rank = station_rank::elsewhere;
    bool operator==(const Item& o) const {
      return id == o.id && inLineup == o.inLineup && spanish == o.spanish && rank == o.rank;
    }
  };
  std::map<std::string, std::vector<Item>> byKey;
  int regional = 0;
  int callsigns = 0;
  int teamChannels = 0;
  int fightChannels = 0;

  std::string summary() const;
};

// `lineup`: My Lineup's entry ids; `lineupEntries`, `browse` and `all` are
// entry channels (id = entry id, group = provider group). From `all`, only
// sports channels of unknown country count (they aren't in the browse
// list). Entries in a hidden group are skipped.
ChannelIndex channelIndex(const std::set<std::string>& lineup, const std::vector<Channel>& lineupEntries,
                          const std::vector<Channel>& browse, const std::vector<Channel>& all,
                          const std::unordered_map<std::string, catalog::EntryInfo>& entryInfo,
                          const std::set<std::string>& hidden,
                          const std::vector<matcher::RosterLeague>& rosters,
                          const StationMarkets& markets);

// What the schedule's broadcaster names found among your channels.
struct SourceStats {
  int national = 0;
  int regional = 0;
  int station = 0;
  int team = 0;
  int fightCards = 0;
  int split = 0;
  // Broadcaster names that aren't a known network or station.
  std::map<std::string, int> unknown;
  // Known networks none of your channels carry.
  std::map<std::string, int> missing;

  std::string summary() const;
};

struct NetworkSources {
  // Game id -> the channels airing its networks and its teams' channels.
  std::map<std::string, std::vector<EventSource>> sources;
  SourceStats stats;
};

// For each game, the channels you have airing its networks (national,
// regional, local station) and its teams' own channels: My Lineup first,
// then the rest (two at most of each; three of your own markets' stations
// for a broadcast network). A broadcast network with several games at once
// (regional coverage) waits for a guide listing to say which one a station has.
NetworkSources networkSources(const std::vector<SportsGame>& games, const ChannelIndex& index);

}  // namespace network_map

}  // namespace marigold::sports
