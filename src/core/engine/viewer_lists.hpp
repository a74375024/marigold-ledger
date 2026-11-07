// The viewer's lists (favourites, recents) kept by row id.
#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/catalog/catalog.hpp"

namespace marigold::engine {

// Build 150: a row whose id changed (a renamed channel, or new matching
// rules: its entry keeps the old ids in formerIds) takes the viewer's
// favourites, removed server favourites and recents with it, as My Lineup's
// choices and pinned feeds already did; a favourite made under the old id
// stayed out of Favorites. An old id that is still a row's own id stays.
// `entryAt`: entry id -> index in `entries`. True when anything moved.
bool carryFormerIds(const std::vector<catalog::EntryInfo>& entries,
                    const std::unordered_map<std::string, size_t>& entryAt, std::set<std::string>& favorites,
                    std::set<std::string>& unfavorited, std::vector<std::string>& recents);

}  // namespace marigold::engine
