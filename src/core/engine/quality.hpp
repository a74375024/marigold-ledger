// Marigold (Build 136): what a feed's picture is, for the Sources and Where
// to Watch rows. Measured when one of its streams played (the playback
// log's "video" line: size and frame rate), else what its name or group
// claims ("FHD", "ᵁᴴᴰ ⁶⁰ᶠᵖˢ"), else nothing: an unmarked feed is no longer
// called "HD". Never "8K", "RAW", "VIP" or a count.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace marigold::engine::quality {

// A measured picture: its lines and frames a second (rounded; 0 unknown).
struct Picture {
  int height = 0;
  int fps = 0;
  bool interlaced = false;
  bool operator==(const Picture& o) const {
    return height == o.height && fps == o.fps && interlaced == o.interlaced;
  }
  bool operator!=(const Picture& o) const { return !(*this == o); }
};

// The picture in a "video" log line ("1920x1080 59.94p · h264 High 8-bit ·
// aac", "1280x720 · h264 · aac"); nullopt without a size.
std::optional<Picture> fromFacts(std::string_view facts);
// "1080p · 60", "720p", "4K · 50"; "" for nothing worth showing (no size,
// or more lines than 4K).
std::string text(const Picture& p);
// What a feed's name, else its provider group, says of its picture: "4K",
// "FHD", "HD" or "SD", then " · 60" for a 50 / 60 fps claim ("60 fps"
// alone). Superscript tags ("ᴴᴰ", "ᵁᴴᴰ", "⁶⁰ᶠᵖˢ") count. "" when neither
// claims anything.
std::string claimed(std::string_view name, std::string_view group = {});

// The last measured picture of each raw channel id, the newest kCap kept
// (least recently measured dropped first). Saved as JSON: ids and numbers
// only, never a link.
class Memory {
 public:
  static constexpr size_t kCap = 2000;

  // True when it changed what is kept.
  bool remember(const std::string& channelId, const Picture& p);
  std::optional<Picture> find(const std::string& channelId) const;
  size_t size() const { return byId_.size(); }

  // {"v":1,"pictures":[["<id>",1080,60,0],...]}, oldest first.
  std::string toJson() const;
  // A saved memory (the newest kCap of it); empty when it doesn't read.
  static Memory fromJson(std::string_view text);

 private:
  struct Kept {
    Picture picture;
    uint64_t seq = 0;
  };
  std::map<std::string, Kept> byId_;
  std::map<uint64_t, std::string> bySeq_;  // oldest first
  uint64_t seq_ = 0;
};

}  // namespace marigold::engine::quality
