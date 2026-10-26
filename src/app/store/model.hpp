// What the screens read: immutable snapshots the store publishes. A new
// snapshot replaces the old one whole (shared_ptr swap on the UI thread), so
// a screen can hold one while drawing without locks or copies.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/util/time.hpp"

namespace marigold::app {

// One guide row: a real-world channel, with its feeds as backups.
struct GuideChannel {
  enum class Style : uint8_t { Named, LogoOnly, Local };
  std::string id;        // entry id ("e|..."), stable across refreshes
  std::string name;      // "ESPN", "WNBC"
  std::string subtitle;  // "NBC 4" for locals, the category otherwise
  std::string logoUrl;
  std::string section;   // neighborhood title ("Local", "Sports"...)
  std::string group;     // the provider's group
  int sectionRank = 0;   // guide order of the neighborhood
  std::optional<int> number;
  int feeds = 1;
  bool west = false;
  bool favorite = false;
  bool inLineup = false;
  // In All Channels' countries (or in My Lineup / a favorite). Another
  // country's sports channels are kept for Live Events only.
  bool browsable = true;
  bool radio = false;
  Style style = Style::Named;
};

// One programme on one guide row.
struct Listing {
  std::string title;
  std::string episode;
  std::string overview;
  time::Seconds start = 0, end = 0;
  uint8_t genres = 0;  // model::GenreFlag bits
  bool live = false;
  bool premiere = false;
  bool outside = false;  // from an outside guide, not the provider
  bool isOn(time::Seconds t) const { return start <= t && t < end; }
};

struct Channels {
  uint64_t version = 0;
  std::vector<GuideChannel> all;      // every visible entry, guide order
  std::vector<int> lineup;            // My Lineup, guide order
  std::vector<int> browse;            // every channel not in a hidden group
  std::vector<int> every;             // 0..all.size()-1 (a provider group, hidden ones too)
  std::vector<int> favorites;
  std::vector<int> recent;            // most recent first
  std::unordered_map<std::string, int> byId;
  size_t rawChannels = 0;             // provider channels before grouping
  bool lineupConfigured = false;

  const GuideChannel* find(const std::string& id) const {
    auto it = byId.find(id);
    return it == byId.end() ? nullptr : &all[static_cast<size_t>(it->second)];
  }
};

struct Listings {
  uint64_t version = 0;
  // Entry id -> programmes sorted by start, no overlaps. Shared, so a new
  // snapshot copies pointers, not programmes.
  std::unordered_map<std::string, std::shared_ptr<const std::vector<Listing>>> byChannel;
  // Rows that were asked for and came back empty ("No guide info").
  std::unordered_map<std::string, bool> empty;

  const std::vector<Listing>* forChannel(const std::string& id) const {
    auto it = byChannel.find(id);
    return it == byChannel.end() || !it->second ? nullptr : it->second.get();
  }
};

// Loading progress for the guide's status line and the status screens.
struct StoreStatus {
  enum class Phase : uint8_t { NoAccounts, Loading, Ready, Failed };
  Phase phase = Phase::NoAccounts;
  std::string line;     // "Loading channels from <name>…", "Building guide…"
  std::string problem;  // shown on the failure screen; never a link
};

// What the player needs to tune an entry: its feeds, best first. The URLs
// carry logins: they stay in memory and are never logged or drawn.
struct Feed {
  std::string channelId;  // raw provider channel id
  std::string url;        // secret
  std::string headers;    // "Name: value" lines, secret
  std::string label;      // "FHD · West feed · <account name>" (safe to show)
  std::string quality;    // "FHD", for the feed chip
  int loadMode = 0;
  // Emby: the server opens the stream first (store().resolve()).
  bool needsOpen = false;
};

}  // namespace marigold::app
