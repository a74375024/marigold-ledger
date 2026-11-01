// Channel admission (Marigold, not in tvOS): the TV keeps at most
// kMaxChannels channels, and a cut must never leave half a provider group
// behind. Whole groups are admitted by rank (lower first; the viewer's
// countries before the rest), then in provider order; a group that would
// cross the cap is skipped, and later smaller groups may still fit. The
// same rule on every device: what is least useful goes first.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "core/model/channel.hpp"

namespace marigold::catalog {

constexpr size_t kMaxChannels = 30000;

struct Admission {
  std::vector<Channel> channels;  // admitted, in original order
  size_t admittedGroups = 0;
  size_t cutGroups = 0;   // left out for room
  size_t leftOut = 0;     // left out by rank (another country's non-sports group)
};

// A group's place in the queue: lower ranks first; within a rank, lower
// `within` first, then provider order. A negative rank leaves the group
// out whatever the room.
struct GroupPlace {
  int rank = 0;
  int within = 0;
};

// Groups are Channel::group, falling back to the first tag; channels with
// neither (or an empty one) together count as one group.
// `rank`: a group's place from its name and its channels (`members`, in
// provider order, never empty); null: all 0.
using GroupRank = std::function<GroupPlace(const std::string& group, const std::vector<const Channel*>& members)>;
Admission admitGroups(const std::vector<Channel>& channels, size_t cap = kMaxChannels,
                      const GroupRank& rank = nullptr);

// The rank for the viewer's countries (`wanted`, e.g. {"US"}; empty: no
// preference, every group 0 but the free ones below): their groups 0,
// groups with no country 1,
// another country's sports and event groups 2 (Live Events finds games on
// them; those with more games first, the ones mostly of empty slots last),
// and another country's other groups left out. Only a country in the
// group's own name leaves a group out (a mixed "Entertainment" group stays).
// Free streaming and 24/7 groups (freeStreamingGroup()) come after all of
// those (3) unless the viewer has free channels on (`freeOn`), when they
// keep their country's rank.
GroupRank countryRank(const std::set<std::string>& wanted, bool freeOn = true);

// A group of free streaming or 24/7 channels, by cheap signals only (no
// build has run yet): its name ("US| ROKU", "US| 24/7 COMEDY", "Pluto TV";
// not a free service's pay-per-view group), or three in four of its
// channels named as copies from a free service or as loops ("(PLUTO USA)
// ...", "RK: ...", "24/7 ..."). A group holding a server favourite never is.
bool freeStreamingGroup(const std::string& group, const std::vector<const Channel*>& members);

// A hash of everything a channel list holds (order included): the same
// download gives the same value, so an unchanged list needs no new sort.
// `also`: more that decides the sort (the countries admission keeps, and
// whether free channels are on).
uint64_t fingerprint(const std::vector<Channel>& channels, const std::set<std::string>& also = {});

}  // namespace marigold::catalog
