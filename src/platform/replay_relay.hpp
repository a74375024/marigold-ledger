// Build 172: the replay relay. A tiny HTTP server on 127.0.0.1 inside the
// app, on every platform, that hands the player a replay stretch already
// starting at the moment picked when the provider's archive starts the
// stretch earlier (a chunk's start: core/engine/archive_timing.hpp).
//
// The player asks it for /replay.ts?s=<session>; it reads the provider's
// link itself and passes the MPEG-TS on from the moment:
//   - seek: the provider answers byte ranges. The stretch's first picture
//     is read for its clock (a few KB from byte 0), then the stream is
//     asked from about where the moment is (the engine's estimate from the
//     archive's bytes a second), its first picture read again and the
//     range asked again nearer when it is off (at most three times); what
//     is left before the moment is read through;
//   - read through: the stretch is read from its start and dropped up to
//     the moment (bounded in bytes), then passed on.
// Either way the stream passed on starts with its PAT and PMT and a video
// PES at the moment (media::tsclock::TsStartAt), and the session hears
// where it really began (skipped()).
//
// Why a relay in the app rather than each player's own seek: the same code
// on webOS (uMS, through the LG relay as any feed), Android (ExoPlayer),
// Apple TV and Mac (VLCKit); no player is asked for a byte range it might
// drop (VLC passes no such header; ExoPlayer replaces it on its own
// requests), and the archive's ignoring of a range, a redirect or a slow
// read is handled in one place. Its own HTTP client takes http:// links
// only (Xtream archives are plain HTTP); for any other the session plays
// the chunk as it comes and says so.
//
// One session at a time (a new localUrl() ends the last), one connection
// to the provider at a time. Plain POSIX sockets and threads, like
// lan_server; nothing here logs a link.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace marigold::platform {

class ReplayRelay {
 public:
  struct Start {
    int skip = 0;            // seconds into the stretch
    bool seek = false;       // the provider answers byte ranges
    uint64_t startByte = 0;  // about where the moment is
    uint32_t byteRate = 0;   // bytes a second of video (0 not known)
  };

  ReplayRelay() = default;
  ~ReplayRelay();
  ReplayRelay(const ReplayRelay&) = delete;
  ReplayRelay& operator=(const ReplayRelay&) = delete;

  // Listens on 127.0.0.1 (a free port). False when it can't.
  bool start();
  bool running() const { return port_ > 0; }
  // A link it can read: http:// (not https).
  static bool takes(const std::string& source);
  // The local link for a new session reading `source` (`headers`: "Name:
  // value" lines sent to it) from `start`; "" when it can't take it.
  std::string localUrl(const std::string& source, const std::string& headers, const Start& start);
  // Ends the current session (its connection to the provider closes).
  void stop();
  // Where the current session's stream began, in seconds after the
  // stretch's first picture, once it passed anything on (nullopt before).
  std::optional<int> skipped() const;
  // For the tests: how many times the source was opened in the current
  // session, and the last byte range asked of it (0: none).
  int opens() const;
  uint64_t lastRange() const;

  // Bytes dropped while reading through at most: this many a second of
  // the skip and the stretch's rate, with a floor and a cap.
  static constexpr uint64_t kReadThroughCap = 400ull * 1024 * 1024;
  // ... and for at most this long (a slow provider): then it passes on
  // from where it got to (the session gives a stretch started inside this
  // much more time for its picture).
  static constexpr int kReadThroughMs = 12000;

  struct Core;

 private:
  std::shared_ptr<Core> core_;
  std::thread listener_;
  int port_ = 0;
};

}  // namespace marigold::platform
