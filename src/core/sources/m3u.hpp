// M3U playlist accounts: one playlist link per account (new in Marigold; the
// tvOS app has none, Legacy Nuvio has two C parsers this follows in spirit).
//
// The parser is fed the body in chunks, cut anywhere, and keeps one line
// (at most kMaxLineBytes) plus the entry being read. Each entry becomes a
// Channel whose id is "m3u:<account>|<item>", where <item> is a hash of
// (tvg-id, name, group), de-duplicated in playlist order. It never comes from
// the stream link: playlist links carry the login, so links (and per-channel
// request headers) go to a separate side table the caller keeps as a secret.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/model/channel.hpp"
#include "core/sources/common.hpp"

namespace marigold::sources::m3u {

// Legacy LTV_M3U_LINHA: a longer line is dropped whole (never cut inside a link).
constexpr size_t kMaxLineBytes = 4096;
// Entries read before the parser stops (the catalog admits far fewer).
constexpr size_t kMaxEntries = 500000;
// tvg-shift beyond a day is ignored.
constexpr int kMaxShiftMinutes = 24 * 60;

// Side table entry for one channel. Secret: the link carries the login.
struct StreamInfo {
  std::string url;
  // Request headers for this channel, "Name: value" (from #EXTVLCOPT
  // http-user-agent / http-referrer / http-origin, or a Kodi-style
  // "link|User-Agent=...&Referer=..." suffix).
  std::vector<std::string> headers;
  // Per-entry tvg-shift, in minutes.
  std::optional<int> tvgShiftMinutes;
};

struct PlaylistHeader {
  bool sawExtM3u = false;
  // url-tvg / x-tvg-url / tvg-url (the first of a comma-separated list).
  // Secret: guide links can carry the login too.
  std::optional<std::string> guideUrl;
  // tvg-shift for the whole playlist, in minutes.
  std::optional<int> tvgShiftMinutes;
};

// Attribute `name` (ASCII case-insensitive) of an #EXTINF / #EXTM3U line:
// name="value", name='value' or name=value. nullopt when absent.
std::optional<std::string> attribute(std::string_view line, std::string_view name);

class Parser {
 public:
  // Return false to stop reading.
  using OnChannel = std::function<bool(Channel&& channel, StreamInfo&& stream)>;

  Parser(std::string accountId, OnChannel onChannel);

  // False once reading stopped (not a playlist, the callback said stop, or
  // the entry cap was reached).
  bool feed(std::string_view chunk);
  // nullopt when the playlist was read (even with no channels).
  std::optional<Error> finish();

  const PlaylistHeader& header() const { return header_; }
  size_t channels() const { return channels_; }
  // Entries that couldn't be used: a link with no #EXTINF, a link that
  // isn't absolute, or an entry hit by a line over kMaxLineBytes.
  size_t skipped() const { return skipped_; }
  size_t longLines() const { return longLines_; }
  bool truncated() const { return truncated_; }

 private:
  struct Pending {
    bool info = false;
    std::string name, tvgId, tvgName, logo, group, number, extGroup;
    bool radio = false;
    std::optional<int> shift;
    std::vector<std::pair<std::string, std::string>> headers;
  };

  void line(std::string_view text);
  void longLine();
  void entry(std::string_view link);
  std::string itemIdFor(const std::string& tvgId, const std::string& name, const std::string& group);

  std::string sourceId_;
  OnChannel onChannel_;
  PlaylistHeader header_;
  Pending pending_;
  std::string line_;
  bool dropping_ = false;
  bool firstLine_ = true;
  bool stopped_ = false;
  bool truncated_ = false;
  std::optional<Error> error_;
  std::unordered_set<uint64_t> seen_;                 // ids handed out
  std::unordered_map<uint64_t, uint32_t> copies_;     // entries per (tvg-id, name, group)
  size_t entries_ = 0;
  size_t channels_ = 0;
  size_t skipped_ = 0;
  size_t longLines_ = 0;
};

struct Playlist {
  std::vector<Channel> channels;
  // Channel itemId -> link and headers. Secret.
  std::unordered_map<std::string, StreamInfo> streams;
  PlaylistHeader header;
  size_t skipped = 0;
};

// Whole-body convenience (tests, small playlists).
Result<Playlist> parsePlaylist(std::string accountId, std::string_view text);

}  // namespace marigold::sources::m3u
