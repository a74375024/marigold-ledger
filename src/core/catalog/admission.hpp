// Channel admission (Marigold, not in tvOS): the TV keeps at most
// kMaxChannels channels, and a cut must never leave half a provider group
// behind. Whole groups are admitted by rank (lower first; the viewer's
// countries before the rest), then in provider order; a group that would
// cross the cap is skipped, and later smaller groups may still fit.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "core/model/channel.hpp"

namespace marigold::catalog {

constexpr size_t kMaxChannels = 30000;

struct Admission {
  std::vector<Channel> channels;  // admitted, in original order
  size_t admittedGroups = 0;
  size_t cutGroups = 0;
};

// Groups are Channel::group, falling back to the first tag; channels with
// neither (or an empty one) together count as one group.
// `rank`: a group's rank from its name and its first channel (null: all 0).
using GroupRank = std::function<int(const std::string& group, const Channel& first)>;
Admission admitGroups(const std::vector<Channel>& channels, size_t cap = kMaxChannels,
                      const GroupRank& rank = nullptr);

}  // namespace marigold::catalog
