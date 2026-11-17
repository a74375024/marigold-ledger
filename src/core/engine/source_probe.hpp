// Build 166 (Test all sources): one probe of one copy, read over
// HTTP without the player. The stream is read for a few seconds: the time to
// its first byte and to its first key picture, and the picture's size and
// frame rate from the H.264 / HEVC sequence header in the MPEG-TS (the
// picture check's reader, media::TsVideoProbe). A copy that answers with an
// HLS playlist is followed: a master's highest-bandwidth variant, then the
// segment a player starts with (about three from a live playlist's end),
// read the same way. No key picture within the time allowed is a failure.
//
// Runs on a worker (Pool::fetchNow, one connection at a time). The request
// is the one the player gets (the same link and headers); nothing here logs,
// and `why` never carries a link.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/media/ts_ad_markers.hpp"
#include "core/net/http.hpp"

namespace marigold::engine::probe {

// No key picture this long after the request: failed.
constexpr uint32_t kTimeoutMs = 12000;
// After the first key picture, at most this long more for the frame rate.
constexpr uint32_t kRateWaitMs = 4000;
// A live stream is never read past this much.
constexpr size_t kMaxBytes = 24u * 1024u * 1024u;
// Playlists followed (master, media) before giving up.
constexpr int kMaxHops = 4;

struct Request {
  std::string url;                   // secret: never logged
  std::vector<std::string> headers;  // "Name: value", as the player gets them
};

struct Outcome {
  bool ok = false;       // a key picture came
  bool refused = false;  // the provider refused the connection (refusal())
  bool aborted = false;  // stopped from outside (the viewer started watching)
  bool hls = false;      // the copy answered with a playlist
  uint32_t firstByteMs = 0;  // after the request (0: nothing came)
  uint32_t pictureMs = 0;    // the first key picture, after the request
  int width = 0, height = 0, fps = 0;
  std::string why;  // for the log when it failed: "http 404", "no picture in 12 s" (never a link)
  // Build 177: what the stream read said of SCTE-35 ad markers (nothing
  // read: ads.read() false).
  media::scte35::Tally ads;
};

// Milliseconds from any thread (a steady clock).
using Clock = std::function<uint32_t()>;
Clock steadyClock();

// Reads one copy on the calling worker thread; `abort` (may be null) stops
// it at the next piece of data.
Outcome run(http::Pool& pool, const Request& req, const Clock& clock, const std::atomic<bool>* abort,
            uint32_t timeoutMs = kTimeoutMs);

// Build 174: `headers` as the player sends them to a provider: the copy's
// own, with the player's agent (http::kPlayerAgent) where they name none
// and no compressed body asked for (the relay asks none). For the app's
// own reads of a stream (an archive's above all), so the provider answers
// them as it answers the player.
std::vector<std::string> asPlayer(std::vector<std::string> headers);

// An answer that refuses the connection: HTTP 403, 429, 451 or 458, a
// connection refused, or a body that says the connections are used up
// ("max connections").
bool refusal(int status, std::string_view error, std::string_view body);

}  // namespace marigold::engine::probe
