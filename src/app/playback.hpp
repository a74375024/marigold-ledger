// What is playing: one entry (a guide channel or a game), its feeds, the
// flip list it was picked from, and the previous channel. The guide preview
// and full screen share it, so OK on the previewed channel just grows the
// same picture (no second tune).
//
// Failover follows the tvOS rules, ported to core/playback/recovery: no
// first frame in 20 s or an error moves to the next feed; a feed that played
// and then froze is reconnected first, then the next feed takes over; one
// round through the feeds, then a safe message. Emby streams are opened on
// the server one at a time. HLS feeds play through the local TS proxy.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "app/store/model.hpp"
#include "core/playback/recovery.hpp"
#include "core/report/playback_log.hpp"
#include "platform/gfx.hpp"
#include "platform/ts_proxy.hpp"

namespace marigold::app {

class App;

class Playback {
 public:
  explicit Playback(App& app);

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
  // Back to the previous channel (cable LAST).
  std::string previous();

  void setFull();
  void setCorner(gfx::Rect r);
  bool full() const { return full_; }

  // UI thread, every loop: drives the recovery rules. True while something
  // is pending (opening, a window re-apply, a note on screen).
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
  // Why a feed last failed this session ("no picture after 20 s", "stopped
  // after 4 s"), with when; nothing when it worked since.
  struct FeedProblem {
    time::Seconds at = 0;
    std::string why;
  };
  std::optional<FeedProblem> feedProblem(const std::string& channelId) const;
  // A backup took over by itself (the corner card): the feed that stopped
  // and the one now playing, and when (ms).
  struct BackupNotice {
    size_t from = 0, to = 0;
    uint32_t at = 0;
  };
  const std::optional<BackupNotice>& backupNotice() const { return backup_; }
  void dismissBackupNotice() { backup_.reset(); }
  // This tune has shown a picture (a feed switch then reads "Switching to…").
  bool hadPicture() const { return hadPicture_; }
  // Options › Help › Export Playback Log.
  const report::PlaybackLog& log() const { return log_; }
  void clearLog() { log_.clear(); }

 private:
  bool begin(const std::string& entryId, std::vector<Feed> feeds, const std::vector<std::string>& flip);
  void apply(const playback::Actions& actions);
  void playFeed(size_t feed, uint64_t token);
  bool retryDirect();
  void watched();
  void rememberGame();
  gfx::Rect window() const;
  void scheduleReapply();

  App& app_;
  playback::Recovery rec_;
  platform::TsProxy proxy_;
  std::string entry_, previous_, lastWatched_;
  std::vector<std::string> flip_;
  std::vector<Feed> feeds_;
  bool full_ = false;
  std::string failMessage_;
  uint32_t tunedAt_ = 0;
  gfx::Rect corner_{};
  uint32_t reapplyAt_[3] = {0, 0, 0};
  bool reapplyOnReady_ = false;
  // Player flags seen last tick, to report changes only.
  bool sawPicture_ = false, sawBuffering_ = false, sawFailed_ = false, sawEnded_ = false;
  // The current attempt went through the TS proxy; feeds (raw channel ids)
  // the proxy failed on play directly for the rest of this tune.
  bool viaProxy_ = false;
  size_t playingFeed_ = 0;
  std::set<std::string> proxyFailed_;
  bool camera_ = false;
  bool hadPicture_ = false;
  uint32_t pictureAt_ = 0;   // ms the current feed's picture came up
  size_t lastFailed_ = 0;
  std::optional<BackupNotice> backup_;
  std::map<std::string, FeedProblem> problems_;  // by raw channel id  // a race camera is playing (not the game's own source)
  report::PlaybackLog log_;
  int session_ = 0;
  uint64_t logSaved_ = 0;
  uint32_t logSaveAt_ = 0;
  void logEvent(std::string_view event, std::string_view detail = {});
  void saveLog(uint32_t ms);
};

}  // namespace marigold::app
