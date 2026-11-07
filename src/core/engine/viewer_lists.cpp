#include "core/engine/viewer_lists.hpp"

#include <algorithm>

namespace marigold::engine {

bool carryFormerIds(const std::vector<catalog::EntryInfo>& entries,
                    const std::unordered_map<std::string, size_t>& entryAt, std::set<std::string>& favorites,
                    std::set<std::string>& unfavorited, std::vector<std::string>& recents) {
  std::unordered_map<std::string, std::string> now;
  for (const catalog::EntryInfo& e : entries)
    for (const std::string& former : e.formerIds)
      if (!entryAt.count(former)) now.emplace(former, e.id);
  if (now.empty()) return false;
  bool changed = false;
  auto carrySet = [&](std::set<std::string>& ids) {
    std::vector<std::string> add;
    for (auto it = ids.begin(); it != ids.end();) {
      auto to = now.find(*it);
      if (to == now.end()) {
        ++it;
        continue;
      }
      add.push_back(to->second);
      it = ids.erase(it);
      changed = true;
    }
    ids.insert(add.begin(), add.end());
  };
  carrySet(favorites);
  carrySet(unfavorited);
  std::vector<std::string> out;
  out.reserve(recents.size());
  for (const std::string& id : recents) {
    auto to = now.find(id);
    if (to != now.end()) changed = true;
    const std::string& use = to == now.end() ? id : to->second;
    if (std::find(out.begin(), out.end(), use) == out.end()) out.push_back(use);
  }
  recents = std::move(out);
  return changed;
}

}  // namespace marigold::engine
