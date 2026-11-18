// Build 181: the viewer's copy moves (catalog.hpp, moveCopies).
#include <algorithm>

#include "core/catalog/catalog.hpp"

namespace marigold::catalog {
namespace {

using EntriesAt = std::unordered_map<std::string, size_t>;

EntriesAt entriesAt(const CatalogResult& r) {
  EntriesAt at;
  at.reserve(r.entries.size());
  for (size_t i = 0; i < r.entries.size(); ++i) at.emplace(r.entries[i].id, i);
  return at;
}

// The copy is in the list and the entry is there, and the copy isn't in it.
bool pending(const CatalogResult& r, const EntriesAt& at, const std::string& copy, const std::string& entry) {
  if ((!r.entryOf.count(copy) && !r.traits.count(copy)) || !at.count(entry)) return false;
  auto now = r.entryOf.find(copy);
  return now == r.entryOf.end() || now->second != entry;
}

}  // namespace

bool copiesToMove(const CatalogResult& result, const std::map<std::string, std::string>& moves) {
  if (moves.empty()) return false;
  EntriesAt at = entriesAt(result);
  for (const auto& [copy, entry] : moves)
    if (pending(result, at, copy, entry)) return true;
  return false;
}

bool moveCopies(CatalogResult& result, const std::map<std::string, std::string>& moves) {
  if (moves.empty()) return false;
  EntriesAt at = entriesAt(result);
  bool moved = false;
  for (const auto& [copy, entry] : moves) {
    if (!pending(result, at, copy, entry)) continue;
    EntryInfo& to = result.entries[at.at(entry)];
    auto was = result.entryOf.find(copy);
    if (was != result.entryOf.end()) {
      auto from = at.find(was->second);
      if (from != at.end()) {
        std::vector<std::string>& m = result.entries[from->second].members;
        m.erase(std::remove(m.begin(), m.end(), copy), m.end());
      }
    }
    if (std::find(to.members.begin(), to.members.end(), copy) == to.members.end()) to.members.push_back(copy);
    result.entryOf[copy] = entry;
    moved = true;
  }
  return moved;
}

}  // namespace marigold::catalog
