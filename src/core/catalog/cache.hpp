// The finished channel list (channels plus everything worked out from them),
// saved so Live TV opens straight away and then refreshes in the background
// (LiveTVCatalogCache in tvOS). Two files: a small one with what the first
// screen needs (My Lineup, the chosen countries, favorites, recents, Live
// Events) and the full list. Here it is serialized to and from JSON text;
// the caller does the file I/O (atomically, in a cache folder it may lose).
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/model/channel.hpp"
#include "core/util/json.hpp"
#include "core/util/time.hpp"

namespace marigold::catalog::cache {

// 2: three-letter Xtream group codes read as countries ("USA|" is US).
// 3: a too-small numbered family no longer leaves its channels as events.
constexpr int kVersion = 6;

enum class Part { first, full };

struct Saved {
  int version = kVersion;
  std::string signature;
  time::Seconds savedAt = 0;
  std::vector<Channel> channels;
  CatalogResult catalog;
  std::map<std::string, std::vector<std::string>> xtreamFormats;
};

// "catalog-<fnv-1a of signature, hex>-<first|full>.json": a stable name per
// profile + accounts.
std::string fileName(std::string_view signature, Part part);

std::string toJson(const Saved& saved);
// nullopt when the text doesn't parse, or was saved by another version or
// for another signature.
std::optional<Saved> fromJson(std::string_view text, std::string_view signature);

// The first-screen subset: those entries and families, their channels, and
// the events of those families.
Saved firstPart(const Saved& saved, const std::set<std::string>& firstEntryIDs,
                const std::set<std::string>& firstFamilyIDs);

// The pieces, for other caches.
json::Value toJson(const Channel& channel);
std::optional<Channel> channelFromJson(const json::Value& v);
json::Value toJson(const ChannelTraits& traits);
std::optional<ChannelTraits> traitsFromJson(const json::Value& v);
json::Value toJson(const CatalogResult& result);
std::optional<CatalogResult> catalogFromJson(const json::Value& v);

}  // namespace marigold::catalog::cache
