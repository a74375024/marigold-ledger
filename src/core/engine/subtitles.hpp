// The captions of the stream playing, and what the player shows of them.
//
// They come one of two ways: read from the stream itself (the LG relay hands
// every byte on its way to the TV's player to bytes(), on the relay's
// thread), or decoded by the platform's player and handed over as tracks and
// lines (Android). Either way the player screen draws them, the same on
// every platform; this only decides what is on screen now.
//
// Read from the stream, captions arrive as far ahead of the picture as the
// player's buffer: tick() holds each one back until the player's own clock
// reaches its frame, so it shows with the picture it belongs to. The clock
// is the player's playback position (webOS currentTime), carried on between
// its reports while it plays and held while it waits for data, and mapped
// onto the stream's media time where the first picture showed. Without a
// position, the buffer's lead stands in: the newest picture read, less how
// far the player's buffer runs ahead of what it shows.
//
// Caption timing (setDelay) moves them later, or earlier: as far as the
// stream read so far allows here, and for a player's own lines (Android)
// only later.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/captions/cea608.hpp"
#include "core/captions/ts_captions.hpp"

namespace marigold::engine {

struct CaptionTrack {
  std::string id;     // "cc1".."cc4" (from the stream), else the player's own
  std::string label;  // "English", "Español"
  bool operator==(const CaptionTrack& o) const { return id == o.id && label == o.label; }
};

// "en" / "eng" / "English" -> "English"; "" when unknown.
std::string languageName(const std::string& codeOrName);
// A 608 caption service's name: CC1 is the channel's own language, CC3 the
// second one US stations send ("English" / "Español").
std::string captionServiceLabel(int channel, const std::string& language);

// Caption timing, ms (+ later): "On time", "0.5 s earlier", "1.5 s later".
std::string captionTimingLabel(int ms);

// What the player says of its clock, every frame (tick()).
struct PlayerClock {
  bool showing = false;        // the picture is up
  bool running = false;        // playing: not paused, not waiting for data
  int64_t positionMs = -1;     // its playback position (webOS currentTime); -1 unknown
  int64_t positionAgeMs = 0;   // how long ago it said so
  int64_t leadMs = -1;         // how far its buffer runs ahead of the picture; -1 unknown
  // It says its position once the picture shows (webOS): until it does (2 s
  // at most), captions wait rather than go by the buffer's lead.
  bool reportsPosition = false;
};

// How captions were timed since the last look, for the playback log
// (counts and ms only).
struct CaptionTiming {
  std::string clock;           // "currentTime", "buffer", "default" (from the stream); "player"
  int samples = 0;             // half seconds with captions on
  int64_t aheadMedian = 0, aheadMin = 0, aheadMax = 0;  // stream read ahead of the picture, ms
  int64_t errorMs = -1;        // the player's reports against the clock carried on, median; -1 none
  int jumps = 0;               // timeline jumps carried across
  int delayMs = 0;             // the Caption timing applied
};
// The playback log's line: "clock currentTime; ahead median 2400 ms (min
// 1800, max 3100); error ±120 ms; jumps 1; setting −1.0 s".
std::string captionTimingLog(const CaptionTiming& t);

class Subtitles {
 public:
  Subtitles();
  ~Subtitles();
  Subtitles(const Subtitles&) = delete;
  Subtitles& operator=(const Subtitles&) = delete;

  // A new stream. `session`: the relay session its bytes will carry (0: it
  // doesn't come through the relay). `language`: the channel's own, naming
  // the caption services ("English" when unknown).
  void begin(unsigned session, const std::string& language);
  void end();

  // The relay's thread: the stream as it reaches the player. Cheap; bytes of
  // another session are ignored.
  void bytes(const uint8_t* data, size_t n, unsigned session);

  // From a player that decodes captions itself. `known`: the player lists a
  // track only when the stream has one (tvOS); ExoPlayer lists CC1 for any
  // TS stream, so there captions count once text arrives.
  void playerTracks(std::vector<CaptionTrack> tracks, bool known);
  void playerLines(const std::vector<std::string>& lines);

  // Every frame while playing (UI thread). True when what is on screen
  // changed.
  bool tick(int64_t nowMs, const PlayerClock& clock);
  // The same with only the buffer's lead (-1 unknown), the player running.
  bool tick(int64_t nowMs, int64_t leadMs, bool showing);

  // Caption timing, ms: + later, - earlier (kept across streams).
  void setDelay(int ms) { delayMs_ = ms; }
  // Since the last call; false when captions weren't on.
  bool takeTiming(CaptionTiming& out);
  // Once a stream's player clock is matched to its pictures: how its
  // position counts ("from the first picture"); "" otherwise or after.
  std::string takeAnchorNote();

  // The stream carries captions (text has been seen, or the player lists a
  // track).
  bool present() const;
  std::vector<CaptionTrack> tracks() const;
  // The track for a language picked earlier ("" = the channel's own), or the
  // first; "" when there are none.
  std::string pick(const std::string& language) const;
  void select(const std::string& trackId);  // "" = none shown
  const std::string& selected() const { return selected_; }
  std::string selectedLabel() const;

  // On screen now, top to bottom.
  const std::vector<captions::Line>& lines() const { return lines_; }
  // Captions read from the stream (all capitals in the US) rather than a
  // player's text track.
  bool fromStream() const { return session_ != 0; }

 private:
  enum class Source { None, Position, Buffer, Default };
  bool streamClock(int64_t nowMs, const PlayerClock& c, int64_t newest, bool haveKey, int64_t keyMedia,
                   int64_t firstPts, Source& source, int64_t& clock);
  void sample(int64_t nowMs, Source source, int64_t ahead);

  mutable std::mutex mu_;  // the relay thread's side: ts_ to keyMedia_
  std::unique_ptr<captions::TsCaptions> ts_;
  std::deque<captions::CcPair> queue_;
  std::vector<captions::CcPair> ready_;  // reused for each chunk
  bool haveMedia_ = false, haveKey_ = false;
  int64_t newest_ = 0;                   // media ms of the newest picture read
  int64_t keyMedia_ = 0, firstPts_ = 0;
  int jumps_ = 0;
  bool sawData_ = false;
  unsigned session_ = 0;  // read on the relay thread under mu_

  // The caption clock (media ms), UI thread.
  int delayMs_ = 0;
  bool haveClock_ = false;
  int64_t clock_ = 0;
  bool anchored_ = false;
  int64_t offset_ = 0;                   // media = position + offset_
  int64_t showingSince_ = -1;            // the picture is up since (waiting for a position)
  std::string anchorNote_;
  int64_t lastPos_ = -1, lastPosAt_ = 0;  // the player's last report, and when
  bool ranSince_ = false;                // it played since that report
  // The playback log's figures since takeTiming().
  int64_t sampleAt_ = -1;
  int sourceCount_[4] = {0, 0, 0, 0};
  std::vector<int64_t> ahead_, errors_;
  int jumpsSeen_ = 0, jumpsLogged_ = 0, positionJumps_ = 0;
  int samples_ = 0;
  // A player's own lines, held for the Caption timing: when each came.
  struct Pending {
    int64_t atMs;
    std::vector<captions::Line> lines;
  };
  std::deque<Pending> pending_;
  int64_t lastNow_ = 0;

  std::string language_ = "English";
  std::vector<captions::Cea608> cc_;  // CC1..CC4
  std::vector<CaptionTrack> playerTracks_;
  bool playerTracksKnown_ = false, sawPlayerText_ = false;
  std::vector<captions::Line> playerLines_;  // as shown (after the Caption timing)
  std::vector<captions::Line> playerLatest_; // as the player gave them last
  std::string selected_;
  std::vector<captions::Line> lines_;
};

}  // namespace marigold::engine
