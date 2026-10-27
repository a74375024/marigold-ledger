#include "core/engine/host.hpp"

namespace marigold::engine {

FileKind fileKind(const std::string& name) {
  static const char* const kKeep[] = {
      "accounts.json",         // the sources (no passwords)
      "settings.json",
      "lists.json",            // favourites, recents, hidden groups, pinned feeds
      "lineup.json",           // My Lineup
      "reminders.json",
      "my-sports.json",
      "game-sources.json",     // the feed picked for each game
      "followed-drivers.json",
  };
  if (name == "accounts-secrets.json") return FileKind::Secret;
  for (const char* k : kKeep)
    if (name == k) return FileKind::Keep;
  return FileKind::Cache;
}

}  // namespace marigold::engine
