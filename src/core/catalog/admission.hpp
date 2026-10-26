// Channel admission (Marigold, not in tvOS): the TV keeps at most
// kMaxChannels channels, and a cut must never leave half a provider group
// behind. Whole groups are admitted by rank (lower first; the viewer's
// countries before the rest), then in provider order; a group that would
// cross the cap is skipped, and later smaller groups may still fit.
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

// Groups are Channel::group, falling back to the first tag; channels with
// neither (or an empty one) together count as one group.
// `rank`: a group's rank from its name and its first channel (null: all 0);
// a negative rank leaves the group out whatever the room.
using GroupRank = std::function<int(const std::string& group, const Channel& first)>;
Admission admitGroups(const std::vector<Channel>& channels, size_t cap = kMaxChannels,
                      const GroupRank& rank = nullptr);

// The rank for the viewer's countries (`wanted`, e.g. {"US"}; empty: no
// preference, every group 0): their groups 0, groups with no country 1,
// another country's sports and event groups 2 (Live Events finds games on
// them), and another country's other groups left out. Only a country in the
// group's own name leaves a group out (a mixed "Entertainment" group stays).
GroupRank countryRank(const std::set<std::string>& wanted);

// A hash of everything a channel list holds (order included): the same
// download gives the same value, so an unchanged list needs no new sort.
uint64_t fingerprint(const std::vector<Channel>& channels);

}  // namespace marigold::catalog
