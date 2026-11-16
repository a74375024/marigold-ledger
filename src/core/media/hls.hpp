// Build 166 (Test all sources): just enough of an HLS playlist to
// reach a picture. A master playlist lists variants (the one a TV on a good
// line ends up on is the highest bandwidth); a media playlist lists
// segments (a live one is joined about three segments from its end, where
// players start). Links inside are resolved against the playlist's own.
// Nothing here does I/O or logs; links never leave the caller.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace marigold::media::hls {

// The body starts like a playlist ("#EXTM3U", after a byte-order mark or
// blank space).
bool looksLikePlaylist(std::string_view head);

struct Variant {
  std::string uri;
  uint64_t bandwidth = 0;
  int width = 0, height = 0;
  double frameRate = 0;
};

struct Segment {
  std::string uri;
  double seconds = 0;
};

struct Playlist {
  bool master = false;
  std::vector<Variant> variants;  // master
  std::vector<Segment> segments;  // media
  bool ended = false;             // #EXT-X-ENDLIST (not live)
  bool fragmentedMp4 = false;     // #EXT-X-MAP: the segments aren't MPEG-TS
  bool encrypted = false;         // #EXT-X-KEY with a method other than NONE
};

Playlist parse(std::string_view text);

// The variant to follow: the highest bandwidth (ties: the most lines);
// null when there is none.
const Variant* pickVariant(const Playlist& p);
// The segment a player would start with: about three from the end of a live
// playlist, the first of an ended one; null when there is none.
const Segment* pickSegment(const Playlist& p);

// `ref` (absolute, scheme-relative, root-relative or relative) against the
// link of the playlist it came from.
std::string resolve(std::string_view base, std::string_view ref);

}  // namespace marigold::media::hls
