// What is playing, decided once for every platform: one entry (a guide
// channel or a game), its feeds, the flip list it was picked from and what
// Right goes back to; when a tune counts as watched; the backup that took
// over; why a feed failed; the playback log; a pause while the app is in
// the background. The guide preview and full
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
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/engine/host.hpp"
#include "core/engine/model.hpp"
#include "core/engine/quality.hpp"
#include "core/engine/replay.hpp"
#include "core/engine/views.hpp"
#include "core/media/picture.hpp"
#include "core/playback/recovery.hpp"
#include "core/report/log_journal.hpp"
#include "core/report/playback_log.hpp"

namespace marigold::engine {

class Store;

// The player's flags, read every tick.
struct PlayerState {
  bool picture = false;    // a frame was drawn (webOS: the decoder reported in)
  bool playing = false;    // the player's clock runs: the sound plays
  bool buffering = false;
  bool failed = false;
  bool ended = false;
  std::string error;       // already safe to log
  // Before a picture: the stream's data has arrived (`loaded`), on players
  // that report that at all (`reportsLoading`).
  bool loaded = false, reportsLoading = false;
  double position = 0, bufferEnd = 0;
  // The picture check. What the stream itself says, where something reads
  // it (`streamChecked`: the LG relay), and the numbers behind it...
  bool streamChecked = false;
  media::Verdict stream = media::Verdict::Unknown;
  std::string streamDetail;
  // ... and what the player says: the video tracks it lists (-1 not known
  // yet), one it can't decode, its frame counter stopped while its clock
  // runs.
  int videoTracks = -1;
  bool videoUnsupported = false;
  bool framesStalled = false;
  // The picture and sound, for the log ("1920x1080 59.94p · h264 High
  // 8-bit · aac"); "" not known yet.
  std::string facts;
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
  // Something the platform did by itself since the last call, for the log
  // (webOS: its relay reopened a source that closed mid-stream). Every frame.
  virtual std::optional<Retry> note() { return std::nullopt; }
  // The provider stream was closed (a relay serving it can stop).
  virtual void streamClosed() {}
  // The connections to the source a relay closed since the last call (webOS:
  // the relay reopens a continuous source that closes). Every frame.
  struct SourceClosed {
    int64_t lastedMs = 0, bytes = 0;
    bool reopened = false;  // the relay opened it again
  };
  virtual std::vector<SourceClosed> sourceClosed() { return {}; }
  virtual void stop() = 0;
  // Build 165, replay's pause: holds the picture where it is, the stream
  // still open (the session stops it a few seconds later; what the screen
  // then shows is the platform's). False when this player can't: the
  // session stops it at once.
  virtual bool pause() { return false; }
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
  // Writes what the playback log has not saved yet (on this thread).
  virtual ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Starts `entryId` (or keeps it if it is already playing). `flip` is the
  // list CH+/CH- and Up/Down move through in the player. A tune in the
  // guide corner is a preview: it is not "watched" (Recent, what Right goes
  // back to) until it goes full screen.
  bool tune(const std::string& entryId, const std::vector<std::string>& flip = {});
  // A game, starting with the source `via` (an entry or a raw feed) picked
  // in Where to Watch; the others stay as backups.
  bool tuneEventVia(const std::string& eventId, const std::string& via, const std::vector<std::string>& flip = {});
  // Plays a specific feed of the current entry (Sources panel).
  bool useFeed(size_t index);
  // The feed playing, opened again from the start (its backups after it):
  // the app plays it another way now (webOS: subtitles through the relay).
  // `why` goes in the log.
  bool restart(std::string_view why);
  // A race camera: these feeds, while the race stays the entry (the event
  // guide, Up / Down and the cameras panel keep working). Not remembered as
  // the game's source.
  bool tuneCamera(std::vector<Feed> feeds);
  // The raw channel playing now ("" when none).
  std::string playingChannel() const;
  // The source picked in a game's Where to Watch (tuneEventVia's `via`, an
  // entry or a raw feed) while that tune lasts; "" otherwise.
  const std::string& pickedSource() const { return picked_; }
  void stop();

  // Moves through the flip list: +1 next, -1 previous. Returns the entry.
  std::string flip(int step);
  // The channel `step` away from `from` (empty: the one playing) in the flip
  // list, without tuning it: the banner shows it while the viewer presses on.
  std::string flipTarget(const std::string& from, int step) const;
  // Right (cable LAST): back to what was viewed last, a channel or a game,
  // as it played (its source, its flip list, the guide list it came from).
  // Returns the entry tuned, or "" when nothing could be (a toast says why).
  // A game no longer on the board is found again by its schedule game; else
  // its channel plays, with a note that the game has ended.
  std::string previous();
  // Build 159: what Right goes back to. Whatever was left after it showed
  // full screen and drew a picture, however briefly; something that never
  // drew one is skipped. A race camera never changes it.
  struct Viewed {
    std::string entry;    // a guide channel or a game (event id)
    bool game = false;
    // A game's source (Where to Watch's `via`: an entry or a raw feed) and
    // the feed whose picture showed (a raw channel id).
    std::string source, feed;
    std::string channel;  // the guide channel it played on ("" none known)
    std::string gameId;   // a game's schedule game ("" none)
    std::vector<std::string> flip;  // its own flip list
    // The guide list it was picked from (pickedFrom), while it played from
    // that list.
    std::optional<views::Tab> tab;
    // Build 165: left during a replay (or paused): the moment it showed
    // then. Right replays from there while the archive still holds it.
    std::optional<time::Seconds> replayAt;
  };
  // What Right goes to now; null when nothing.
  const Viewed* previousTarget() const;
  // Its entry ("" none).
  std::string previousEntry() const;
  // A game's event id on the board now (found again by its schedule game
  // after a rebuild), "" when it's gone.
  std::string gameNow(const Viewed& v) const;
  // This tune is Right's (it worked).
  bool bouncing() const { return bouncing_; }

  // The guide list the channel playing was picked from (a tune with its
  // flip list just before): the guide in the player opens on it and names
  // it while that flip list stands (views::playerOpen). Right restores it.
  void pickedFrom(views::Tab tab) {
    pickedTab_ = tab;
    pickedFlip_ = flip_;
  }
  views::Tab pickedTab() const { return pickedTab_; }
  const std::vector<std::string>& pickedFlip() const { return pickedFlip_; }

  // Build 159: the app went to the background (or its window was hidden).
  // The recovery stops where it is: no timers fire, nothing is charged to
  // a feed and no failure is logged meanwhile ("paused" in the log).
  void suspend();
  // Back on screen: what was playing (or opening) starts again on the same
  // source, as a fresh start; a game no longer on the board, or a channel
  // gone, ends quietly ("resumed" in the log). Nothing when not paused.
  void resume();
  bool paused() const { return paused_; }

  // Build 165, replay: the channel's own archive. Any of its copies whose
  // Xtream provider keeps catch-up can serve it (catchupCopy()); live stays
  // on the copy that played live. A replay plays a stretch of the archive
  // (up to three hours, or until now and five minutes more) from a whole
  // minute; when a stretch ends behind live the next one opens from where
  // it ended, and reaching live (within a minute) goes live by itself
  // ("You're live"). No picture in 10 s, or an error, tries the next copy
  // with an archive, then says "Replay isn't available right now on this
  // channel" and goes live. The recovery stands aside meanwhile, as in the
  // background: nothing reconnects or switches copies, nothing is charged
  // to a feed or remembered as its source. Every action is one "replay"
  // line in the playback log (never a link).
  //
  // Paused (from live or a replay): the player holds the picture where it
  // can, and the stream is closed a few seconds later, so the provider's
  // connection is free; play resumes the archive from the paused minute.
  struct Replay {
    enum class Phase { Opening, Playing, Paused };
    Phase phase = Phase::Opening;
    size_t feed = 0;          // the copy replaying (feeds())
    uint16_t days = 0;        // the archive its provider says it keeps
    time::Seconds from = 0;   // the stretch playing or opening: its start, to the minute
    int minutes = 0;          // how much of the archive it asks for
    int64_t atMs = 0;         // the moment showing, ms since 1970 (paused: where it stopped)
    uint32_t startedAt = 0;   // ms the stretch was handed to the player
    uint32_t pictureAt = 0;   // ms its picture came (0: not yet)
    uint32_t pausedAt = 0;    // ms the pause began
    bool held = false;        // paused: the player kept the last picture
    bool released = false;    // paused: the stream is closed
    bool fromLive = false;    // paused while live played
    bool quiet = false;       // ... by the Rewind timeline: still live, as far as Right goes
    bool next = false;        // the stretch opening follows one that ended
    bool video = true;        // the channel shows a picture (not radio)
    time::Seconds at() const { return static_cast<time::Seconds>(atMs / 1000); }
  };
  // Why a replay starts from a moment (its log line).
  enum class ReplayWhy { StartOver, Rewind, Back, Resume };
  static constexpr uint32_t kReplayTimeoutMs = 10000;
  // Paused: the stream is closed this long after the picture was held.
  static constexpr uint32_t kReleaseMs = 3000;
  // A replay this close to live is live.
  static constexpr time::Seconds kLiveWithin = 60;
  // The copy a replay plays on: the copy playing live when its provider
  // keeps an archive, else the first of the channel's own copies (in the
  // order they are tried live) whose provider does. Offered once live has
  // drawn a picture (or during a replay), not on a race camera. nullopt:
  // no copy keeps one (the controls look as they always did).
  std::optional<size_t> catchupCopy() const;
  // The days of catch-up that copy keeps (0: no replay here).
  uint16_t catchupDays() const;
  const std::optional<Replay>& replay() const { return replay_; }
  // The moment showing (live: now), and how far behind live it is (0 live).
  time::Seconds replayAt() const;
  time::Seconds behindLive() const;
  // Plays the archive from `from` (taken to the minute, kept inside the
  // archive); within a minute of live, live. False when this channel has
  // no replay.
  bool replayFrom(time::Seconds from, ReplayWhy why);
  // Pauses live or a replay (false: no replay here, nothing done).
  // `quiet`: no log line (the Rewind timeline holding the picture).
  bool pause(bool quiet = false);
  // Plays on from the paused minute (a few seconds early).
  bool resumePaused(bool quiet = false);
  // Back to live on the copy that played live, as a fresh start.
  void goLive(bool quiet = false);
  // A short notice for the player ("You're live", "Replay isn't available
  // right now on this channel"), once; "" none.
  std::string takeReplayNotice();
  // The Rewind timeline's programmes from `from` on: the guide's (it keeps
  // six hours back for these channels) and the copy's own past once asked
  // for (wantPast), sorted.
  std::vector<replay::Programme> timeline(time::Seconds from) const;
  // Asks the copy's panel for its own past (one request, once per channel;
  // dropped when the channel changes). Labels fill in as it lands.
  void wantPast();
  // Moves when the copy's own past has landed (or failed).
  int pastSerial() const { return pastSerial_; }

  // Full screen or a preview. Going full screen makes the tune "watched".
  void setShowingFull(bool full);
  bool full() const { return full_; }

  // Every loop: drives the recovery rules. True while something is pending
  // (opening, a note on screen).
  bool tick(uint32_t ms);

  const std::string& entry() const { return entry_; }
  const std::vector<Feed>& feeds() const { return feeds_; }
  const std::vector<std::string>& flipList() const { return flip_; }
  size_t feedIndex() const { return rec_.feedIndex(); }
  bool active() const { return !entry_.empty(); }
  // (Paused in the background: as it was when the app left. A replay: its
  // picture, held while paused, and kept while the next stretch opens.)
  bool ready() const {
    if (pausedAt_) return pausedAt_->ready;
    if (replay_) return replay_->phase != Replay::Phase::Opening || replay_->next;
    return rec_.sawFirstFrame();
  }
  bool failed() const { return rec_.phase() == playback::Recovery::Phase::Failed; }
  // A channel is opening: no picture yet, not given up (a spinner shows).
  // Not while paused in the background.
  bool opening() const { return active() && !paused_ && !ready() && !failed(); }
  // What the screens show of playback. The app draws again when it changes
  // (a picture coming up, a note, giving up), not every frame meanwhile.
  struct Look {
    std::string entry, note;
    size_t feed = 0;
    bool full = false, ready = false, failed = false, noPicture = false, hadPicture = false, backup = false;
    int replay = 0;  // a replay: 1 + its phase (0 live)
    size_t replayFeed = 0;
    bool replayReleased = false;
    bool operator==(const Look& o) const {
      return entry == o.entry && note == o.note && feed == o.feed && full == o.full && ready == o.ready &&
             failed == o.failed && noPicture == o.noPicture && hadPicture == o.hadPicture && backup == o.backup &&
             replay == o.replay && replayFeed == o.replayFeed && replayReleased == o.replayReleased;
    }
    bool operator!=(const Look& o) const { return !(*this == o); }
  };
  Look look() const;
  const std::string& failMessage() const { return failMessage_; }
  // No feed shows a picture but the sound plays on: the player asks what to
  // do (its card says `noPictureMessage()`).
  bool noPicture() const { return noPicture_; }
  const std::string& noPictureMessage() const { return noPictureMessage_; }
  // "Keep listening": the card goes, and the channel plays as sound only,
  // unchecked, for a week (Store::markAudioOnly).
  void keepListening();
  void dismissNoPicture() { noPicture_ = false; }
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
  // Settings › Help & info › Send a report to your phone › Playback log.
  const report::PlaybackLog& log() const { return log_; }
  void clearLog() { log_.clear(); }
  // Leaving the app (to the background): what the log has not saved yet,
  // memory readings too, written now (on a worker).
  void saveLogNow() { saveLog(ms(), true); }
  // A "memory" line in the log: the app's memory now, after `what` ("channels
  // sorted"; empty for the regular reading every few minutes).
  void logMemory(std::string_view what = {});
  // The run's first line ("launch": build, version, system; see
  // report::launchDetail), so exports from several builds can be told apart.
  void logLaunch(std::string_view detail);
  // A line about the stream playing ("captions", "in the stream"), for the
  // app's parts of playback.
  void logNote(std::string_view event, std::string_view detail = {}) { logEvent(event, detail); }

 private:
  bool begin(const std::string& entryId, std::vector<Feed> feeds, const std::vector<std::string>& flip);
  void apply(const playback::Actions& actions);
  void playFeed(size_t feed, uint64_t token);
  std::vector<playback::FeedInfo> feedInfos(const std::string& entryId) const;
  bool expectsVideo() const;
  void pictureCheck(uint32_t now, const PlayerState& s, bool picture);
  void watched();
  void rememberGame();
  // What is playing as Right would play it again; whether it counts (shown
  // full screen with a picture) is the caller's.
  Viewed viewedNow() const;
  // The source playing (Viewed::source): the pick, else the picture's feed.
  std::string sourceNow() const;
  // Leaving the entry playing: it becomes what Right goes back to, if it
  // counts.
  void leaving();
  // Starts `entryId` with `feeds`, `feed` first when it's there.
  bool replay(const std::string& entryId, std::vector<Feed> feeds, const std::string& feed,
              const std::vector<std::string>& flip);
  void logEvent(std::string_view event, std::string_view detail = {}, bool withFeed = true);
  // Replay: the copy with an archive for live copy `live`; the recovery
  // standing aside; a stretch handed to the player (`what`: its log line);
  // a tick; a failed stretch; leaving it (`live`: live plays again).
  std::optional<size_t> archiveCopy(size_t live) const;
  void standAside();
  void playReplay(size_t feed, time::Seconds from, const std::string& what, bool next = false);
  void tickReplay(uint32_t now);
  void replayFailed(const std::string& why);
  void endReplay(bool live);
  // Starts a replay at `at` in place of a live start (Right, the app's
  // return); false when the archive no longer holds it.
  bool startReplayInstead(time::Seconds at, ReplayWhy why, size_t live);
  void logReplay(std::string detail, size_t copy, bool clock = false);
  std::string localTime(time::Seconds t) const;
  void saveLog(uint32_t ms, bool all = false);
  uint32_t ms() const { return host_.ms(); }

  Store& store_;
  Host host_;
  std::unique_ptr<Player> player_;
  Hooks hooks_;
  playback::Recovery rec_;
  std::string entry_, lastWatched_;
  std::string picked_;  // pickedSource()
  bool bouncing_ = false;   // this tune is previous()
  // What was viewed (Right's targets), newest first, one per entry; Right
  // goes to the newest that isn't playing.
  std::vector<Viewed> viewed_;
  // The entry playing has shown full screen / drawn a picture (any feed,
  // any tune of it); the raw channel of the latest picture.
  bool shownFull_ = false, pictured_ = false;
  std::string pictureChannel_;
  std::string gameId_;  // the game playing's schedule game ("" none)
  bool game_ = false;   // the entry playing was a game when tuned
  std::string cameraSource_;  // the race's source before its cameras
  views::Tab pickedTab_ = views::Tab::Lineup;
  std::vector<std::string> pickedFlip_;
  // Paused in the background: what to start again on return.
  bool paused_ = false;
  struct Paused {
    bool restart = false;  // something played or opened (not a card)
    bool ready = false;    // it showed a picture
    bool game = false;
    size_t feed = 0;
    // Build 165: a replay was playing (it plays from there on return), or
    // was paused (it stays paused).
    std::optional<time::Seconds> replayAt;
    bool replayPaused = false;
  };
  std::optional<Paused> pausedAt_;
  std::vector<std::string> flip_;
  std::vector<Feed> feeds_;
  bool full_ = false;
  std::string failMessage_;
  bool noPicture_ = false;
  std::string noPictureMessage_;
  bool factsLogged_ = false;  // the picture's facts are in the log (this attempt)
  // ... and its picture, until it has played long enough to remember.
  std::optional<quality::Picture> measured_;
  std::set<std::string> quickTried_;  // Emby quick starts tried this tune
  std::string quickPending_;          // a quick start not yet shown a picture
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
  // The log on disk: appended at most every 10 s (log_journal.hpp).
  report::LogJournal journal_;
  std::shared_ptr<report::JournalFile> logFile_;
  uint64_t logSeen_ = 0;  // the log's revision when last saved (or found with nothing to save)
  uint32_t logSaveAt_ = 0;
  uint32_t memoryAt_ = 0;
  // The replay on now; the copy that played live before it and whether it
  // had a picture; the copies tried for this moment; the server clock the
  // stretch was asked in; stretches in a row that ended early; the last
  // tick; the notice for the player.
  std::optional<Replay> replay_;
  size_t replayLive_ = 0;
  bool replayLiveReady_ = false;
  std::set<size_t> replayTried_;
  std::optional<int> replayOffset_;
  int shortStretches_ = 0;
  uint32_t replayTick_ = 0;
  std::string replayNotice_;
  // Right or the app's return replays this moment instead of a live start.
  std::optional<std::pair<time::Seconds, ReplayWhy>> replayInstead_;
  // The copy's own past (wantPast), for the entry it was asked for.
  std::string pastFor_;
  std::optional<std::vector<replay::Programme>> past_;
  int pastSerial_ = 0;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
  // Back to live after a replay: that copy's picture coming up again is
  // not a new success for it (the first feed outcome after the return).
  bool quietReturn_ = false;
};

}  // namespace marigold::engine
