// The heavy, pure parts of Live TV that run on worker threads (LiveTVWork in
// tvOS): tidying raw channel names, telling a real channel-list change from
// renamed event slots, merging guide sources, and working out My Lineup.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/catalog/lineup.hpp"
#include "core/catalog/types.hpp"
#include "core/guide/outside.hpp"
#include "core/model/channel.hpp"
#include "core/util/time.hpp"

namespace marigold::catalog {

// MARK: Log text

// "1.2 MB" / "340 KB".
std::string sizeText(int64_t bytes);
// "45 s" / "12 min" / "3 h" since `then`.
std::string ageText(time::Seconds then, time::Seconds now);

// MARK: Channel names

// Fills in each channel's provider group (its tag, or a name prefix shared
// by 3+ channels) and short name ("US: NBC 4" -> "NBC 4").
std::vector<Channel> annotated(const std::vector<Channel>& list);

// The event slots renamed since `old`, when nothing else changed (same
// channels, groups and regular names); nullopt for a real change that needs
// the full classification.
std::optional<std::vector<Channel>> renamedEventSlots(
    const std::vector<Channel>& old, const std::vector<Channel>& fresh,
    const std::unordered_map<std::string, ChannelTraits>& traits);

// MARK: Guide listings

constexpr double kGuideHours = 3.0;

// The guide grid starts at the last half hour (in `zone`, Calendar.current
// in tvOS).
time::Seconds windowStart(time::Seconds date, const time::Zone& zone = time::Zone::device());

// One channel's listing from every source: the provider's full guide and
// short guide (or the server's), then outside listings for gaps. A null
// source is absent.
std::vector<Program> listing(const std::string& id, const std::vector<Program>* server,
                             const std::vector<guide::RawProgram>* provider,
                             const std::vector<guide::RawProgram>* outside,
                             time::Seconds windowStart);

// Picks whichever listing covers more of the next three hours (the first
// wins unless the other covers 15 minutes more), tops it up from the other
// where it has gaps (overlaps of 5 minutes or less don't count), and takes
// genre tags from the other where its own programmes have none.
std::vector<Program> bestListing(const std::vector<Program>* server,
                                 const std::vector<Program>* outside, time::Seconds windowStart);

// Channel id -> its listing, shared between channels and snapshots.
using ProgramList = std::shared_ptr<const std::vector<Program>>;
using ProgramLists = std::unordered_map<std::string, ProgramList>;

// Every channel's merged listing (after a guide source loads or the window
// moves on). Channels with nothing are left out. Channels with no listing of
// their own from the server whose provider and outside schedules are the same
// lists share one result (its programme ids name the first such channel).
ProgramLists mergedListings(const std::set<std::string>& ids,
                            const std::unordered_map<std::string, std::vector<Program>>& server,
                            const guide::SharedPrograms& provider,
                            const guide::SharedPrograms& outside, time::Seconds windowStart);

// MARK: My Lineup

struct LineupInput {
  std::vector<EntryInfo> infos;
  std::vector<Channel> channels;
  std::unordered_map<std::string, size_t> indexByID;
  LineupPrefs prefs;
  // Raw channel ids that have any guide data.
  std::set<std::string> guideIDs;
  std::string otherGroup;
  // The coast whose East/West versions are suggested (resolveCoast(prefs)).
  Coast coast = Coast::both;
  // Marigold: the store's live list and rows, shared rather than copied on
  // the UI thread (`infos`, `channels` and `indexByID` are then left empty),
  // with the raw ids that count as favourites (the channels' own flags are
  // then not used).
  std::shared_ptr<const std::vector<EntryInfo>> sharedInfos;
  std::shared_ptr<const std::vector<Channel>> sharedChannels;
  std::optional<std::unordered_set<std::string>> favoriteIDs;
};

struct LineupOutput {
  std::set<std::string> lineupIDs;
  std::vector<Channel> entries;
  std::unordered_map<std::string, size_t> entryIndex;
  std::vector<Channel> lineupEntries;
  std::vector<Channel> browseEntries;
  std::vector<std::pair<std::string, int>> groups;  // name, count
};

// Lineup membership and the entry list, in guide order: neighbourhoods
// (Local, News, Sports, Entertainment...), locals by their broadcast
// number, everything else A-Z.
LineupOutput lineup(const LineupInput& input);

}  // namespace marigold::catalog
