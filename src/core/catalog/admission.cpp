#include "core/catalog/admission.hpp"

#include <string>
#include <unordered_map>

namespace marigold::catalog {
namespace {

const std::string& groupKey(const Channel& c) {
  static const std::string kNone;
  if (c.group && !c.group->empty()) return *c.group;
  if (!c.tags.empty() && !c.tags.front().empty()) return c.tags.front();
  return kNone;
}

}  // namespace

Admission admitGroups(const std::vector<Channel>& channels, size_t cap) {
  // Group sizes, in first-seen (provider) order.
  std::unordered_map<std::string, size_t> indexOf;
  std::vector<size_t> sizes;
  std::vector<size_t> groupOf(channels.size());
  for (size_t i = 0; i < channels.size(); ++i) {
    auto [it, inserted] = indexOf.try_emplace(groupKey(channels[i]), sizes.size());
    if (inserted) sizes.push_back(0);
    sizes[it->second] += 1;
    groupOf[i] = it->second;
  }
  Admission out;
  std::vector<bool> admitted(sizes.size(), false);
  size_t total = 0;
  for (size_t g = 0; g < sizes.size(); ++g) {
    if (sizes[g] <= cap - total) {
      admitted[g] = true;
      total += sizes[g];
      out.admittedGroups += 1;
    } else {
      out.cutGroups += 1;
    }
  }
  out.channels.reserve(total);
  for (size_t i = 0; i < channels.size(); ++i) {
    if (admitted[groupOf[i]]) out.channels.push_back(channels[i]);
  }
  return out;
}

}  // namespace marigold::catalog
