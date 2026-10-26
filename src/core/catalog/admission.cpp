#include "core/catalog/admission.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>

#include "core/catalog/catalog.hpp"

namespace marigold::catalog {
namespace {

const std::string& groupKey(const Channel& c) {
  static const std::string kNone;
  if (c.group && !c.group->empty()) return *c.group;
  if (!c.tags.empty() && !c.tags.front().empty()) return c.tags.front();
  return kNone;
}

}  // namespace

Admission admitGroups(const std::vector<Channel>& channels, size_t cap, const GroupRank& rank) {
  // Group sizes, in first-seen (provider) order.
  std::unordered_map<std::string, size_t> indexOf;
  std::vector<size_t> sizes;
  std::vector<int> ranks;
  std::vector<size_t> groupOf(channels.size());
  for (size_t i = 0; i < channels.size(); ++i) {
    const std::string& key = groupKey(channels[i]);
    auto [it, inserted] = indexOf.try_emplace(key, sizes.size());
    if (inserted) {
      sizes.push_back(0);
      ranks.push_back(rank ? rank(key, channels[i]) : 0);
    }
    sizes[it->second] += 1;
    groupOf[i] = it->second;
  }
  // By rank, then provider order.
  std::vector<size_t> order(sizes.size());
  for (size_t g = 0; g < order.size(); ++g) order[g] = g;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ranks[a] < ranks[b]; });
  Admission out;
  std::vector<bool> admitted(sizes.size(), false);
  size_t total = 0;
  for (size_t g : order) {
    if (ranks[g] < 0) {
      out.leftOut += 1;
    } else if (sizes[g] <= cap - total) {
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

GroupRank countryRank(const std::set<std::string>& wanted) {
  return [wanted](const std::string& group, const Channel& first) {
    if (wanted.empty()) return 0;
    Channel byGroup;  // the group's name alone ("UK| GENERAL" on Xtream)
    byGroup.serverId = first.serverId;
    byGroup.group = group;
    std::optional<std::string> groupCountry = markedCountry(byGroup);
    if (groupCountry && !wanted.count(*groupCountry) && !sportsGroup(group)) return -1;
    std::optional<std::string> country = groupCountry ? groupCountry : markedCountry(first);
    if (!country) return 1;
    return wanted.count(*country) ? 0 : 2;
  };
}

uint64_t fingerprint(const std::vector<Channel>& channels) {
  uint64_t h = 0xcbf29ce484222325ull;
  auto mix = [&h](unsigned char c) { h = (h ^ c) * 0x100000001b3ull; };
  auto text = [&mix](const std::string& t) {
    for (char c : t) mix(static_cast<unsigned char>(c));
    mix(0xff);
  };
  auto opt = [&](const std::optional<std::string>& t) {
    mix(t ? 1 : 0);
    if (t) text(*t);
  };
  for (const Channel& c : channels) {
    text(c.id);
    text(c.serverId);
    text(c.itemId);
    text(c.name);
    opt(c.number);
    text(c.imageUrl);
    mix(c.isFavorite ? 1 : 0);
    mix(static_cast<unsigned char>(c.tags.size()));
    for (const std::string& t : c.tags) text(t);
    mix(c.isRadio ? 1 : 0);
    opt(c.group);
    text(c.shortName);
    opt(c.epgId);
    mix(0xfe);
  }
  return h;
}

}  // namespace marigold::catalog
