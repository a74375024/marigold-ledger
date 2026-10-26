// The raw data every source produces: channels and guide programmes.
// Mirrors LiveTVChannel / LiveTVProgram of the tvOS app.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/util/time.hpp"

namespace marigold {

// Which kind of account a channel came from.
enum class SourceKind : uint8_t { Xtream, Emby, M3U };

struct Channel {
  // "<serverId>|<itemId>", unique across all sources. Never contains a login.
  std::string id;
  // "xt:<account>", "jf:<server>" or "m3u:<account>".
  std::string serverId;
  // Provider's id: Xtream stream_id, Emby item id, or for M3U a hash of
  // (tvg-id, name, group).
  std::string itemId;
  std::string name;
  std::optional<std::string> number;
  // Logo URL as the provider gave it (may be empty).
  std::string imageUrl;
  bool isFavorite = false;
  // Categories from the server (Emby tags, Xtream category, M3U group-title).
  std::vector<std::string> tags;
  bool isRadio = false;
  // Provider group ("US", "UK Sports"...), from tags or the name's prefix.
  std::optional<std::string> group;
  // Name without the group prefix ("US: NBC 4" -> "NBC 4").
  std::string shortName;
  // XMLTV id the provider gives the channel (Xtream epg_channel_id, tvg-id).
  std::optional<std::string> epgId;

  const std::string& displayName() const { return shortName.empty() ? name : shortName; }
};

// Genre flags (ProgramGenre in tvOS): bit 1 movie, 2 sports, 4 news, 8 kids.
enum GenreFlag : uint8_t { kGenreMovie = 1, kGenreSports = 2, kGenreNews = 4, kGenreKids = 8 };

struct Program {
  std::string id;
  std::string channelId;
  std::string title;
  std::optional<std::string> episodeTitle;
  std::optional<std::string> overview;
  time::Seconds start = 0;
  time::Seconds end = 0;
  uint8_t genres = 0;
  bool isLive = false;
  bool isPremiere = false;
  std::string imageUrl;
  // Came from the free outside listings rather than the provider's guide.
  bool fromOutsideGuide = false;

  // At least one minute, like LiveTVProgram.minutes (a 30 s slot is 1, not 0).
  int minutes() const { return end - start >= 120 ? static_cast<int>((end - start) / 60) : 1; }
  bool isOn(time::Seconds t) const { return start <= t && t < end; }
};

}  // namespace marigold
