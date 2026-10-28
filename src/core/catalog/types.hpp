// What the catalog learns from a raw channel list (ChannelCatalog in tvOS):
// lineup categories, channel kinds, per-channel traits, entries (real-world
// channels with their variants), event families and live events.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/util/time.hpp"

namespace marigold::catalog {

// What kind of channel an entry is, for building a lineup. Declared in guide
// order (LineupCategory.allCases).
enum class LineupCategory : uint8_t {
  local,
  news,
  sports,
  entertainment,
  movies,
  kids,
  music,
  spanish,
  free,
  international,
  radio,
};

constexpr std::array<LineupCategory, 11> kAllCategories = {
    LineupCategory::local,         LineupCategory::news,    LineupCategory::sports,
    LineupCategory::entertainment, LineupCategory::movies,  LineupCategory::kids,
    LineupCategory::music,         LineupCategory::spanish, LineupCategory::free,
    LineupCategory::international, LineupCategory::radio};

// Saved form ("local", "news"...), the Swift raw value.
const char* rawValue(LineupCategory c);
std::optional<LineupCategory> categoryFromRaw(std::string_view raw);
const char* title(LineupCategory c);
const char* detail(LineupCategory c);
// Cable-style number blocks for My Lineup.
int numberBlock(LineupCategory c);
// SF Symbol name the tvOS UI used; kept as an icon key.
const char* symbol(LineupCategory c);
// Short name for the genre filter ("Local", "Movies & Premium"...).
const char* genreTitle(LineupCategory c);
// Position in kAllCategories.
int rank(LineupCategory c);
const std::set<LineupCategory>& defaultOnCategories();

// What a raw channel is, before anything is matched.
enum class ChannelKind : uint8_t {
  channel,
  // An event slot (PPV, ESPN+, league pass...): Live Events, never My Lineup.
  event,
  // "NO EVENT STREAMING", "tvg-id=...", "ENDED |...": hidden.
  placeholder,
  // "#### PRIME ####" headings some providers put in their lists: hidden.
  separator,
  // 24/7 loops of one show or genre.
  loop247,
  // Free ad-supported streaming channels (Pluto, Roku, Prime, Tubi...).
  fast,
  radio,
};

const char* rawValue(ChannelKind k);
std::optional<ChannelKind> kindFromRaw(std::string_view raw);

// Everything learned from one channel's name and provider group.
struct ChannelTraits {
  std::string clusterKey;
  std::string displayName;
  int quality = 1;        // 0 SD, 1 HD, 2 FHD, 3 UHD
  std::string sourceTag;  // "(a)", "us:", "(rk)"... which upstream it came from
  std::optional<std::string> callsign;
  bool isWest = false;
  std::optional<std::string> country;  // "US", "UK", "MX"... when known
  LineupCategory category = LineupCategory::entertainment;
  std::optional<std::string> familyKey;  // numbered event family, e.g. "us (espn+ #)"
  std::optional<int> familyNumber;
  std::optional<std::string> eventText;  // event part of the name, if any
  bool hasSeparator = false;             // "NN:" / "NN |" style event slot naming
  ChannelKind kind = ChannelKind::channel;
  // "US", "US-ES" (Spanish-language US), "MX", "UK"...; empty when unknown.
  std::optional<std::string> market;
  // Core US network this is ("CNN"), if any.
  std::optional<std::string> core;
  // "national", "local" (call sign), "team", or "regional:<place>".
  std::string scope = "national";
  // The channel directory's entry it matched (catalog/directory.hpp), and
  // when that channel shut down ("2025-03-06", "yes"); empty: none.
  std::string directoryId;
  std::string closed;
  // The cluster key the rules before the directory gave it, when the
  // directory gave another (its entry's earlier id).
  std::string formerKey;
};

// A real-world channel: one guide entry with its variants as backups.
struct EntryInfo {
  std::string id;  // "e|<cluster key>"
  std::string name;
  std::vector<std::string> members;  // raw channel ids, best first
  LineupCategory category = LineupCategory::entertainment;
  std::optional<std::string> callsign;
  std::optional<std::string> country;
  std::optional<int> virtualNumber;  // e.g. 11 for "NBC 11"
  ChannelKind kind = ChannelKind::channel;
  std::optional<std::string> market;
  std::optional<std::string> core;
  std::string scope = "national";
  std::string directoryId;  // the channel directory's entry, if any
  std::string closed;       // when it shut down, per the directory
  // Ids this entry had before the directory joined or renamed rows: the
  // user's choices made under them (added, removed, pinned) still apply.
  std::vector<std::string> formerIds;
};

// One live event, deduplicated across event families.
struct LiveEvent {
  std::string id;
  std::string title;
  std::string league;
  std::optional<time::Seconds> start;
  std::vector<std::string> feeds;  // raw channel ids, best first
  std::string family;
  std::optional<std::string> market;
  // The title gave only a clock time ("8:20pm"), so the day is a guess
  // (today). Matching against the schedule decides the real day.
  std::optional<bool> startGuessed;

  bool operator==(const LiveEvent& o) const { return id == o.id; }
};

// A numbered family of event feeds ("ESPN PLUS 00-110").
struct EventFamily {
  std::string id;
  std::string name;
  std::vector<std::string> feeds;  // raw channel ids, by feed number
  bool hasNames = false;           // feed names carry event titles
  std::string league;
  std::optional<std::string> market;
};

// ChannelCatalog.Result.
struct CatalogResult {
  std::unordered_map<std::string, ChannelTraits> traits;
  std::vector<EntryInfo> entries;
  std::unordered_map<std::string, std::string> entryOf;
  std::vector<EventFamily> families;
  std::vector<LiveEvent> events;
};

}  // namespace marigold::catalog
