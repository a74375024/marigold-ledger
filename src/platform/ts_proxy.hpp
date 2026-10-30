// The local MPEG-TS proxy (vendored Legacy proxyts.c).
//
// The TV's HLS engine fails on some live channels (data arrives, the decoder
// never starts) that play at once as one continuous .ts. The proxy is an HTTP
// server on 127.0.0.1 inside the app. The player asks it for
// /live.ts?s=<session>, and it looks at the real source:
//   - already continuous TS: a 302 straight to the source, nothing in between;
//   - an HLS playlist: it follows the live playlist, fetches the segments in
//     order and serves the player ONE continuous TS stream (video/mp2t), with
//     the PAT/PMT put back before each segment that lacks them.
// Encrypted (EXT-X-KEY) and fMP4 (EXT-X-MAP) playlists are not converted: the
// session ends and the player sees the stream end.
//
// One session at a time: a new localUrl() ends the previous session and
// cancels its download (an Xtream account often allows one stream). The
// proxy's requests carry the feed's headers (playlist and segments, which
// the TV's own HLS engine leaves out). Its log lines never contain a link.
//
// Legacy keeps the proxy in C statics, so it is one per process: create one
// TsProxy and call it from the UI thread. The proxy's own threads only touch
// the vendored code.
#pragma once

#include <cstddef>
#include <string>

namespace marigold::platform {

class TsProxy {
 public:
  // Whether this platform's player needs it: webOS yes; Android no (its
  // player follows HLS itself), where the rest is a no-op.
  static bool wanted();
  // Starts the local server (a free port on 127.0.0.1). False when it can't
  // listen; localUrl() then returns "" and feeds play directly.
  bool start();
  // Ends the current session (channel change, leaving the player). The
  // server keeps listening.
  void stop();

  // The local link for `source`, or "" when the proxy can't take it (not
  // started, not an http(s) link, a progressive .mp4/.mkv file, a link or
  // headers too long for it). `headers`: "Name: value" lines, '\n'
  // separated, sent with every request to the source. Opens a new session.
  // `passThrough`: a continuous TS source is served through the proxy too
  // (no redirect), so the tap sees it (subtitles).
  std::string localUrl(const std::string& source, const std::string& headers, bool passThrough = false);
  // The session localUrl() opened last.
  unsigned session() const;

  // Sees every byte of a session's stream as it reaches the player, on the
  // proxy's threads. Set once, before the first session.
  struct Tap {
    virtual ~Tap() = default;
    virtual void bytes(const unsigned char* data, size_t n, unsigned session) = 0;
  };
  void setTap(Tap* tap);
  // True when the current session only redirected the player to the source
  // (it was not an HLS playlist): playing the same source directly would
  // repeat the same attempt.
  bool redirected() const;
  // What the source of the current session answered so far (or at its
  // end): the last HTTP status (0: no answer yet), the bytes received, and
  // how often the proxy reopened a continuous source that closed mid-stream.
  // `known` is false when the session didn't pass the stream through.
  struct Source {
    bool known = false;
    int http = 0;
    long long bytes = 0;
    int reopened = 0;
  };
  Source source() const;

  // The variant picked from a master playlist for the next sessions: the
  // nearest to `height` (2160, 1080, 720, 480), or the highest with 0.
  void setPreferredHeight(int height);

  // Pure check behind localUrl(): an http(s) link that is not a .mp4/.mkv file.
  static bool candidate(const std::string& source);
  static bool isLocalUrl(const std::string& url);

  bool running() const { return started_; }

 private:
  bool started_ = false;
};

}  // namespace marigold::platform
