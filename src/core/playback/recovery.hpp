// Playback recovery: what to do when a live feed won't start or stops.
//
// Port of the tvOS LiveTVStore "Playback & recovery" section (tune,
// startFeed, armFirstFrameWatchdog, playerStatusChanged, beginTrouble,
// scheduleStartupFailure, startupFailed, recover, moveToNext, allowRecovery,
// giveUp, switchToFeed) plus the frozen-picture and dry-source rules of
// Legacy's live stall watchdog (vendor/legacy/src/livestall.h).
//
// Live TV alone decides how a stream recovers; the player only reports what
// it sees. Starting: up to 20 s for a picture. A failed start on an Xtream
// feed is retried (twice) after a pause, because a provider's connection
// limit usually means the previous stream hasn't let go yet; otherwise the
// next feed is tried. Playing: a stall shows a small "Buffering…" note after
// a moment, and only a stall that lasts 10 s, an error or the stream ending
// (6 s to get over it alone) triggers recovery: reconnect the same feed
// first, then the next feed, all inside the open player. At most 6 recovery
// actions per 180 s, and one round through the feeds; then it stops and says
// so with a message that never contains a link.
//
// Emby / Jellyfin hold ONE open stream at a time: every open first closes
// whatever is open, opens run strictly one after another (a new one waits
// until the previous open has reported back), and a tune that has been
// superseded (fast Up / Down) never opens anything.
//
// No timers or threads: every input takes a monotonic time in ms and returns
// the actions to perform now; tick() returns the ones that fell due. The
// object is used from the UI thread only.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace marigold::playback {

using Ms = int64_t;

// Thresholds, named after the Swift constants (seconds there, ms here).
inline constexpr Ms kStartupTimeoutMs = 20000;    // startupTimeout: no picture
// Marigold: an Xtream picture comes within 6 s (the playback log: 161 starts,
// the slowest 5.7 s), and a start that hangs comes back on the retry, so
// Xtream waits 10 s; Emby / Jellyfin open on the server first and keep 20.
inline constexpr Ms kXtreamStartupTimeoutMs = 10000;
inline constexpr Ms kStallNoteDelayMs = 3000;     // stallNoteDelay: "Buffering…"
inline constexpr Ms kStallTimeoutMs = 10000;      // stallTimeout
inline constexpr int kMaxRecoveries = 6;          // maxRecoveries
inline constexpr Ms kRecoveryWindowMs = 180000;   // recoveryWindow
inline constexpr Ms kXtreamRetryDelayMs = 2000;   // xtreamRetryDelay
inline constexpr Ms kErrorGraceMs = 6000;         // errorGrace
inline constexpr Ms kStartupErrorGraceMs = 3000;  // startupErrorGrace
inline constexpr int kXtreamStartRetries = 2;     // xtreamStartRetries
inline constexpr Ms kSettleMs = 350;              // a burst of flips settles on the last
inline constexpr Ms kXtreamReconnectDelayMs = 1000;
inline constexpr Ms kLongRunMs = 60000;           // played a while: one more reconnect
inline constexpr Ms kFreshRunMs = 120000;         // a healthy run earns fresh reconnects
// Legacy livestall.h: the buffer end not growing for 15 s with at most 2 s
// left ahead proves the source stopped sending (NV_LS_SEMDADO_MS,
// NV_LS_RESERVA_S); position steps under 0.05 s are noise.
inline constexpr Ms kDrySourceMs = 15000;
inline constexpr double kDryReserveS = 2.0;
inline constexpr double kPositionStepS = 0.05;

enum class FeedKind {
  Xtream,    // direct link; cheap to reopen
  Emby,      // Emby / Jellyfin: the server opens (and must close) a stream
  Playlist,  // M3U link; direct like Xtream (see recovery.cpp)
};

// How long a feed of this kind has to show a first picture.
Ms startupTimeout(FeedKind kind);
// Marigold: a player error that means this TV can't play the stream at all.
// webOS 601 "Resource Allocation Error": the resource manager has no decoder
// setup for the stream's format (DTS audio on 2020-21 LG sets, an odd
// codec / size / frame rate), so retrying it is pointless and the next
// version is tried at once. Not 203 "AV Type Not Founded": that is data the
// pipeline can't recognise (an error page, a stream the server hadn't opened
// yet), which a retry or a later try can fix.
bool cannotPlayHere(std::string_view message);

struct FeedInfo {
  std::string id;  // stable feed id (never a URL)
  FeedKind kind = FeedKind::Xtream;
};

struct TuneOptions {
  size_t start = 0;   // feed to start with
  // Surfing an event family: the feeds are different events, so a failure
  // "skips to the next feed" and no backup toast is shown.
  bool surf = false;
};

struct Action {
  enum class Kind {
    // Open feed `feed` (Emby: open it on the server first) and hand it to the
    // player, replacing whatever it plays. Report back with streamHanded()
    // or openFailed() with `token`.
    Play,
    // The same, for the feed that is already playing (a reconnect).
    Reconnect,
    // Close the stream open on a server, if any. Always comes right before
    // Play / Reconnect, and after GiveUp and stop().
    CloseStream,
    // Stop the player and show the "can't play" card with `text` (safe).
    GiveUp,
    // The small note over the player: `text`, or hide it when empty.
    Note,
    // A short toast (`text`), e.g. "Playing a backup feed (2 of 3)".
    Toast,
    // Source reliability: feed `feed` worked (`worked`) or failed (`text`
    // says why; `unplayable` when this TV can't play it).
    RecordFeed,
    // A playback log line: `text` is the event, `detail` the rest; `feed` is
    // the feed it is about (npos for none). Never contains a link.
    Log,
  };
  static constexpr size_t npos = static_cast<size_t>(-1);

  Kind kind = Kind::Log;
  size_t feed = npos;
  uint64_t token = 0;
  bool worked = false;
  std::string text;
  std::string detail;
  // RecordFeed, failed: this TV can't play the feed (cannotPlayHere).
  bool unplayable = false;
};
using Actions = std::vector<Action>;

class Recovery {
 public:
  enum class Phase {
    Idle,     // nothing tuned
    Waiting,  // a start is due (settle / retry pause, or an open to finish)
    Opening,  // Play / Reconnect sent, waiting for streamHanded / openFailed
    Playing,  // the stream is in the player (picture or not yet)
    Failed,   // gave up; the card is up
  };

  // A new channel: fresh recovery budget, starts `feeds[options.start]`.
  Actions tune(Ms now, std::vector<FeedInfo> feeds, TuneOptions options = {});
  // The viewer picked a feed in the Sources panel: switch to it in the open
  // player. Recovery then sticks with it (more reconnects) and only leaves
  // it once it stops working altogether.
  Actions pick(Ms now, size_t feed);
  // Leaving the player.
  Actions stop(Ms now);

  // The stream for `token` is open. If isCurrent(token) was true, the app
  // has just handed it to the player; otherwise it must not hand it, and
  // the returned actions close it.
  Actions streamHanded(Ms now, uint64_t token);
  // The stream for `token` could not be opened. `reason` is shown to the
  // viewer if every feed fails; it is scrubbed of links anyway.
  Actions openFailed(Ms now, uint64_t token, std::string_view reason);

  // Player reports, for the stream handed last. Call them on transitions
  // (not every frame), except position(), which is called every frame.
  Actions firstFrame(Ms now);
  // Playback position and, when known, how far the buffer reaches (s; 0 =
  // unknown). Forward motion is new video.
  Actions position(Ms now, double positionS, double bufferEndS = 0);
  Actions bufferingStarted(Ms now);
  Actions bufferingEnded(Ms now);
  Actions playerError(Ms now, std::string_view message);
  Actions ended(Ms now);
  // A pause by the viewer never counts as a stall.
  Actions setPaused(Ms now, bool paused);

  // Every frame: the actions that fell due.
  Actions tick(Ms now);

  // True while `token` is the open the machine is waiting for.
  bool isCurrent(uint64_t token) const { return token != 0 && token == token_ && phase_ == Phase::Opening; }

  Phase phase() const { return phase_; }
  bool active() const { return phase_ != Phase::Idle && phase_ != Phase::Failed; }
  const std::vector<FeedInfo>& feeds() const { return feeds_; }
  size_t feedIndex() const { return index_; }
  bool sawFirstFrame() const { return sawFirstFrame_; }
  const std::string& note() const { return note_; }

 private:
  struct Pending {
    Ms due = 0;
    size_t feed = 0;
    bool reconnect = false;
  };
  struct Trouble {
    Ms since = 0, noteAt = 0, deadline = 0, grace = 0;
    std::string cause;
    bool noted = false;
  };

  void startTune(Ms now, size_t feed, Ms delay, bool inPlace, bool reconnect, bool newRound,
                 std::string_view reason, Actions& out);
  void startNow(Ms now, size_t feed, bool reconnect, Actions& out);
  void playerStarted(Ms now, Actions& out);
  void beginTrouble(Ms since, std::string cause, Ms grace, Actions& out);
  void recoveredByItself(Ms now, Actions& out);
  void scheduleStartupFailure(Ms now, std::string reason);
  void startupFailed(Ms now, const std::string& reason, Actions& out, bool unplayable = false);
  void recover(Ms now, const std::string& reason, Actions& out);
  void moveToNext(Ms now, const std::string& reason, Actions& out);
  bool allowRecovery(Ms now);
  void giveUp(const std::string& message, Actions& out);
  void markTried(size_t feed);
  void clearTimers();
  void setNote(std::string text, Actions& out);
  void log(Actions& out, std::string event, std::string detail = {}, size_t feed = Action::npos) const;
  bool playing() const { return phase_ == Phase::Playing && index_ < feeds_.size(); }

  std::vector<FeedInfo> feeds_;
  size_t index_ = 0;
  bool surf_ = false;
  Phase phase_ = Phase::Idle;
  uint64_t token_ = 0;     // the current tune generation
  uint64_t inFlight_ = 0;  // an open that has not reported back (0 = none)
  std::optional<Pending> pending_;
  bool playerOnScreen_ = false;

  bool sawFirstFrame_ = false;
  Ms handedAt_ = 0;
  std::optional<Ms> firstFrameAt_;
  std::optional<Ms> startupErrorAt_;
  std::string startupErrorReason_;
  std::optional<Trouble> trouble_;

  // Frozen picture / dry source (livestall.h).
  bool havePosition_ = false;
  bool moved_ = false;  // the position advanced on this stream (arms the watchdog)
  double position_ = 0;
  Ms movedAt_ = 0;
  double bufferEnd_ = 0;
  Ms bufferEndAt_ = 0;
  bool paused_ = false;

  int reconnectsOnFeed_ = 0;
  std::vector<Ms> recoveries_;
  std::map<std::string, int> startupRetries_;
  std::optional<std::string> manualFeedId_;
  bool announceBackup_ = false;
  std::vector<bool> tried_;  // feeds tried in this round
  size_t triedCount_ = 0;
  std::string note_;
  std::string lastOpenError_;
};

// Removes links (and login parameters) from a player or server message, as
// the Swift does before logging or showing one.
std::string safeMessage(std::string_view text);

}  // namespace marigold::playback
