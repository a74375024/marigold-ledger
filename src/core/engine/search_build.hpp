// Making Search's index (store_search.cpp) in its two parts, on a worker:
// what each is made from is copied or shared on the UI thread first, so
// nothing here reads the store.
//
//   guide()  the channels with their other names, and the guide's shows;
//            made again when the channels or the listings change (the
//            channels are kept when only the listings did).
//   board()  the board's games, every team of the saved team lists, the
//            leagues and the event channel groups; made again when the board, the team lists or My
//            Sports change (the teams are kept while the lists are, the
//            groups' lineup and favourite marks while the channels are).
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/engine/events_model.hpp"
#include "core/engine/model.hpp"
#include "core/engine/search.hpp"
#include "core/model/channel.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/network_map.hpp"
#include "core/sports/types.hpp"

namespace marigold::engine::search_build {

struct GuideInput {
  std::shared_ptr<const Channels> channels;
  std::shared_ptr<const Listings> listings;
  std::shared_ptr<const catalog::CatalogResult> catalog;
  sports::StationMarkets markets;
  time::Seconds now = 0;
  // Names and numbers only (no other names, no shows): Search opened
  // before any index was made.
  bool quick = false;
  // The part made before from the same channels, catalog and markets: its
  // channels are kept.
  std::shared_ptr<const search::Index> previous;
};
std::shared_ptr<search::Index> guide(const GuideInput& in);

// Whether an event channel group is yours: one of its feeds still in the
// channel list is a channel of an entry in My Lineup, or a favourite. By
// group id (the catalog's first group with that id, as Store::familyFeeds()
// finds it).
struct FamilyMarks {
  bool inLineup = false, favorite = false;
};
using FamilyMarksById = std::unordered_map<std::string, FamilyMarks>;
std::shared_ptr<const FamilyMarksById> familyMarks(const catalog::CatalogResult& catalog,
                                                   const std::vector<Channel>& list, const Channels& channels);

struct BoardInput {
  std::shared_ptr<const EventsBoard> events;
  // What the board knows of each tile beyond the snapshot, in board order
  // (each tile once, the families row left out).
  struct Tile {
    const EventTile* tile = nullptr;  // in `events`
    std::vector<std::string> sideNames;  // full names, short names
    std::string leagueShort;
    std::vector<std::string> teams;  // home and away team keys
    std::string customTitle;
  };
  std::vector<Tile> tiles;
  // The families row's event channel groups: id, name, and whether one of
  // the group's channels is in My Lineup or Favorites.
  struct Family {
    std::string id, name;
    bool inLineup = false, favorite = false;
  };
  std::vector<Family> families;
  // With `catalog` (the store's builds), the groups' marks are worked out
  // here, not taken from `families`: `marks` when made before from the same
  // catalog, list and channels, else from these snapshots.
  std::shared_ptr<const catalog::CatalogResult> catalog;
  std::shared_ptr<const std::vector<Channel>> list;
  std::shared_ptr<const Channels> channels;
  std::shared_ptr<const FamilyMarksById> marks;
  sports::MySportsPrefs mySports;
  // The saved team lists by league (when `teams` is null).
  struct League {
    std::string id, name;
    bool college = false, picked = true;
    std::vector<sports::SportsTeam> teams;
  };
  std::vector<League> leagues;
  // The teams made before from the same lists, without their games.
  std::shared_ptr<const std::vector<search::TeamEntry>> teams;
  time::Seconds now = 0;
};
struct BoardPart {
  std::shared_ptr<search::Index> index;
  std::shared_ptr<const std::vector<search::TeamEntry>> teams;  // to keep for the next one
  std::shared_ptr<const FamilyMarksById> marks;                 // the same (with a catalog)
};
BoardPart board(const BoardInput& in);

}  // namespace marigold::engine::search_build
