// What is playing, decided once for every platform: one entry (a guide
// channel or a game), its feeds, the flip list it was picked from and the
// previous channel; when a tune counts as watched; the backup that took
// over; why a feed failed; the playback log. The guide preview and full
// screen share it, so OK on the previewed channel just grows the same
// picture (no second tune).
//
// Failover follows the tvOS rules in core/playback/recovery: no first frame
// in 20 s or an error moves to the next feed; a feed that played and then
// froze is reconnected first, then the next feed takes over; one round
// through the feeds, then a safe message. Emby streams are opened on the
// server one at a time.
//
// The platform's video player sits behind Player: start a feed, stop, and
// report its flags. Where the picture goes on screen is the platform's.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/engine/host.hpp"
#include "core/engine/model.hpp"
#include "core/playback/recovery.hpp"
#include "core/report/playback_log.hpp"

namespace marigold::engine {

class Store;

// The player's flags, read every tick.
struct PlayerState {
  bool picture = false;    // frames are coming (or the decoder reported in)
  bool buffering = false;
  bool failed = false;
  bool ended = false;
  std::string error;       // already safe to log
  double position = 0, bufferEnd = 0;
};

class Player {
 public:
  virtual ~Player() = default;
  // A new entry is being tuned: forget what was learned about the last one.
  virtual void beginTune() {}
  // A new attempt starts (a feed, or a reconnect): whatever the last
  // attempt left is over. For a server-opened feed this comes before the
  // server answers.
  virtual void attempt() {}
  // Starts this feed, through whatever relay the platform needs; `asGiven`:
  // a stream the provider's server opened for us (Emby), played as it is.
  virtual void play(const Feed& feed, bool asGiven) = 0;
  // The current attempt failed before any picture. A platform with another
  // way to try the same feed (webOS: without its local relay) does so and
  // says what it did, for the log; nullopt = nothing else to try.
  struct Retry {
    std::string event, detail;
  };
  virtual std::optional<Retry> retryAnotherWay() { return std::nullopt; }
  // The provider stream was closed (a relay serving it can stop).
  virtual void streamClosed() {}
  virtual void stop() = 0;
  virtual PlayerState state() const = 0;
};

class Session {
 public:
  // Back into the UI: a short message to show, something on screen changed,
  // and a feed's picture came up.
  struct Hooks {
    std::function<void(const std::string&)> toast;
    std::function<void()> changed;
    std::function<void()> picture;
  };

  Session(Store& store, Host host, std::unique_ptr<Player> player, Hooks hooks);
  virtual ~Session() = default;
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Starts `entryId` (or keeps it if it is already playing). `flip` is the
  // list CH+/CH- and Up/Down move through in the player. A tune in the
  // guide corner is a preview: it is not "watched" (Recent, previous
  // channel) until it goes full screen.
  bool tune(const std::string& entryId, const std::vector<std::string>& flip = {});
  // A game, starting with the source `via` (an entry or a raw feed) picked
  // in Where to Watch; the others stay as backups.
  bool tuneEventVia(const std::string& eventId, const std::string& via, const std::vector<std::string>& flip = {});
  // Plays a specific feed of the current entry (Sources panel).
  bool useFeed(size_t index);
  // A race camera: these feeds, while the race stays the entry (the event
  // guide, Up / Down and the cameras panel keep working). Not remembered as
  // the game's source.
  bool tuneCamera(std::vector<Feed> feeds);
  // The raw channel playing now ("" when none).
  std::string playingChannel() const;
  void stop();

  // Moves through the flip list: +1 next, -1 previous. Returns the entry.
  std::string flip(int step);
  // The channel `step` away from `from` (empty: the one playing) in the flip
  // list, without tuning it: the banner shows it while the viewer presses on.
  std::string flipTarget(const std::string& from, int step) const;
  // Back to the previous channel (cable LAST).
  std::string previous();

  // Full screen or a preview. Going full screen makes the tune "watched".
  void setShowingFull(bool full);
  bool full() const { return full_; }

  // Every loop: drives the recovery rules. True while something is pending
  // (opening, a note on screen).
  bool tick(uint32_t ms);

  const std::string& entry() const { return entry_; }
  const std::string& previousEntry() const { return previous_; }
  const std::vector<Feed>& feeds() const { return feeds_; }
  const std::vector<std::string>& flipList() const { return flip_; }
  size_t feedIndex() const { return rec_.feedIndex(); }
  bool active() const { return !entry_.empty(); }
  bool ready() const { return rec_.sawFirstFrame(); }
  bool failed() const { return rec_.phase() == playback::Recovery::Phase::Failed; }
  const std::string& failMessage() const { return failMessage_; }
  // The small note at the top right ("Reconnecting…"); empty = none.
  const std::string& note() const { return rec_.note(); }
  uint32_t tunedAt() const { return tunedAt_; }
  // Why a feed last failed this session ("no picture after 10 s", "stopped
  // after 4 s", "this TV can't play it"), with when; nothing when it worked
  // since.
  struct FeedProblem {
    time::Seconds at = 0;
    std::string why;
  };
  std::optional<FeedProblem> feedProblem(const std::string& channelId) const;
  // A backup took over by itself (the banner's "Back on …"): the feed that
  // stopped and the one now playing, and when (ms).
  struct BackupNotice {
    size_t from = 0, to = 0;
    uint32_t at = 0;
  };
  const std::optional<BackupNotice>& backupNotice() const { return backup_; }
  void dismissBackupNotice() { backup_.reset(); }
  // This tune has shown a picture (a feed switch then reads "Switching to
  // another feed").
  bool hadPicture() const { return hadPicture_; }
  // Options › Help › Export Playback Log.
  const report::PlaybackLog& log() const { return log_; }
  void clearLog() { log_.clear(); }
  // A "memory" line in the log: the app's memory now, after `what` ("channels
  // sorted"; empty for the regular reading every few minutes).
  void logMemory(std::string_view what = {});

 private:
  bool begin(const std::string& entryId, std::vector<Feed> feeds, const std::vector<std::string>& flip);
  void apply(const playback::Actions& actions);
  void playFeed(size_t feed, uint64_t token);
  void watched();
  void rememberGame();
  void logEvent(std::string_view event, std::string_view detail = {});
  void saveLog(uint32_t ms);
  uint32_t ms() const { return host_.ms(); }

  Store& store_;
  Host host_;
  std::unique_ptr<Player> player_;
  Hooks hooks_;
  playback::Recovery rec_;
  std::string entry_, previous_, lastWatched_;
  std::vector<std::string> flip_;
  std::vector<Feed> feeds_;
  bool full_ = false;
  std::string failMessage_;
  uint32_t tunedAt_ = 0;
  // Player flags seen last tick, to report changes only.
  bool sawPicture_ = false, sawBuffering_ = false, sawFailed_ = false, sawEnded_ = false;
  bool camera_ = false;  // a race camera is playing (not the game's own source)
  bool hadPicture_ = false;
  uint32_t pictureAt_ = 0;  // ms the current feed's picture came up
  size_t lastFailed_ = 0;
  std::optional<BackupNotice> backup_;
  std::map<std::string, FeedProblem> problems_;  // by raw channel id
  report::PlaybackLog log_;
  int session_ = 0;
  uint64_t logSaved_ = 0;
  uint32_t logSaveAt_ = 0;
  uint32_t memoryAt_ = 0;
};

}  // namespace marigold::engine
