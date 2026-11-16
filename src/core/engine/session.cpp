#include "core/engine/session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>

#include "core/engine/store.hpp"
#include "core/sources/xtream.hpp"
#include "core/util/log.hpp"
#include "core/util/memory.hpp"
#include "core/util/strings.hpp"

namespace marigold::engine {
namespace {

playback::FeedKind kindOf(const Feed& f) {
  if (f.needsOpen) return playback::FeedKind::Emby;
  if (str::startsWith(f.channelId, "xt:")) return playback::FeedKind::Xtream;
  return playback::FeedKind::Playlist;
}

// The playback log's journal; before Build 120 the whole log was saved at
// once (read once more, then written as a journal and removed).
constexpr const char* kLogFile = "playback-log.jsonl";
constexpr const char* kOldLogFile = "playback-log.json";
constexpr uint32_t kLogSaveMs = 10000;
constexpr uint32_t kMemoryEveryMs = 5 * 60 * 1000;
// A picture that played this long is remembered for the feed's rows.
constexpr uint32_t kMeasuredAfterMs = 20000;
// What was viewed, kept for Right (one per entry, newest first).
constexpr size_t kViewedKept = 4;
// Build 166: a long play's time goes into its copy's record this often.
constexpr uint32_t kPlayedFlushMs = 5 * 60 * 1000;

// Wall clock in ms, for the log's times.
int64_t wallMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Replay: a stretch that ends this soon after its picture came is short;
// this many short ones in a row and the archive is taken as not working.
constexpr uint32_t kShortStretchMs = 20000;
constexpr int kShortStretchesTried = 3;
// The player's notice when no copy plays the moment asked.
const char* const kReplayUnavailable = "Replay isn\xe2\x80\x99t available right now on this channel";

}  // namespace

Session::Session(Store& store, Host host, std::unique_ptr<Player> player, Hooks hooks)
    : store_(store), host_(std::move(host)), player_(std::move(player)), hooks_(std::move(hooks)),
      logFile_(std::make_shared<report::JournalFile>(host_.files.path(kLogFile))) {
  if (auto text = host_.files.read(kLogFile)) {
    journal_.restored(log_, log_.restoreJournal(*text));
  } else if (auto old = host_.files.read(kOldLogFile)) {
    log_.restore(*old);
    journal_.restored(log_, 0, true);
    logFile_->replaces(host_.files.path(kOldLogFile));
  }
  logSeen_ = log_.revision();
}

Session::~Session() {
  // Closing: the rest of the log, written here (the workers are stopping).
  if (std::optional<report::LogJournal::Write> w = journal_.take(log_, true)) {
    logFile_->queue(std::move(*w));
    logFile_->flush();
  }
}

void Session::logEvent(std::string_view event, std::string_view detail, bool withFeed) {
  std::string channel, feed, provider;
  if (const GuideChannel* g = store_.channels()->find(entry_)) channel = g->name;
  else if (const EventTile* t = store_.eventTile(entry_)) channel = t->heroTitle.empty() ? t->title : t->heroTitle;
  size_t i = rec_.feedIndex();
  if (withFeed && i < feeds_.size()) {
    feed = feeds_[i].label;
    provider = store_.providerName(feeds_[i].channelId);
  }
  log_.add(wallMs(), event, session_, channel, feed, provider, detail);
}

void Session::logMemory(std::string_view what) {
  std::string reading = memory::describe(memory::read());
  if (reading.empty()) return;
  std::string detail = what.empty() ? reading : std::string(what) + ": " + reading;
  log_.add(wallMs(), report::PlaybackLog::kMemoryEvent, 0, {}, {}, {}, detail);
}

void Session::logLaunch(std::string_view detail) { log_.addLaunch(wallMs(), detail); }

void Session::saveLogNow() {
  saveLog(ms(), true);
  store_.saveScoresNow();
}

bool Session::logProviders(bool force) {
  scores::Rollup r = store_.providerRollup();
  if (r.providers.empty()) return false;
  // The daily lines wait for the lineup (its coverage is half the point).
  bool lineup = std::any_of(r.providers.begin(), r.providers.end(),
                            [](const scores::ProviderFigures& f) { return f.lineup > 0; });
  if (!force && !lineup) return false;
  for (const auto& [provider, detail] : store_.providerLines(r))
    log_.add(wallMs(), "providers", 0, {}, {}, provider, detail);
  store_.markProvidersLogged(time::now());
  return true;
}

void Session::saveLog(uint32_t now, bool all) {
  if (logFile_->broken()) journal_.rewrite();  // a write was lost: the whole log next
  bool changed = log_.revision() != logSeen_ || journal_.wantsWhole();
  if (!all && (!changed || now < logSaveAt_)) return;
  logSeen_ = log_.revision();
  // Only the events added since the last save (memory readings alone wait).
  std::optional<report::LogJournal::Write> w = journal_.take(log_, all);
  if (!w) return;
  logSaveAt_ = now + kLogSaveMs;
  logFile_->queue(std::move(*w));
  std::shared_ptr<report::JournalFile> file = logFile_;
  host_.pool.post([file] { file->flush(); }, nullptr, http::Priority::Background);
}

std::optional<Session::FeedProblem> Session::feedProblem(const std::string& channelId) const {
  auto it = problems_.find(channelId);
  if (it == problems_.end()) return std::nullopt;
  return it->second;
}

std::string Session::playingChannel() const {
  size_t i = rec_.feedIndex();
  return entry_.empty() || i >= feeds_.size() ? std::string() : feeds_[i].channelId;
}

bool Session::tuneCamera(std::vector<Feed> feeds) {
  if (entry_.empty() || feeds.empty()) return false;
  std::string entry = entry_;
  // The race's own source stays what Right plays it on.
  std::string source = sourceNow();
  bool ok = begin(entry, std::move(feeds), {});
  camera_ = ok;
  if (ok) {
    cameraSource_ = source;
    logEvent("camera");
  }
  return ok;
}

void Session::rememberGame() {
  // Leaving or switching a game: its source starts it next time (6 h).
  if (entry_.empty() || feeds_.empty() || camera_ || !store_.isEvent(entry_)) return;
  // During a replay: the copy live played on, as it was (a replay is never
  // a source's success or failure).
  if (replay_ && !replayLiveReady_) return;
  size_t i = replay_ ? replayLive_ : rec_.feedIndex();
  if (i < feeds_.size()) store_.noteEventFeed(entry_, feeds_[i].channelId, replay_ ? true : ready());
}

bool Session::begin(const std::string& entryId, std::vector<Feed> feeds, const std::vector<std::string>& flip) {
  if (feeds.empty()) {
    MLOG_W("play", "no feeds for this channel");
    return false;
  }
  // What is left becomes Right's target (before its source, flip list and
  // feeds are replaced), with the moment a replay showed. Another source or
  // a camera of the same entry isn't leaving it.
  bool other = entryId != entry_;
  if (other) leaving();
  if (!flip.empty()) flip_ = flip;
  rememberGame();
  // A replay ends with the tune: the channel tuned plays live.
  if (replay_) endReplay(false);
  // Build 167: a game replay is this tune's (tuneGameReplay), else none.
  gameReplay_ = std::move(pendingGame_);
  pendingGame_.reset();
  gameBack_ = endOfGame_ = endOfGameSaid_ = false;
  logMemory("before tuning");
  camera_ = false;
  quietReturn_ = false;
  hadPicture_ = false;
  bouncing_ = false;
  paused_ = false;
  pausedAt_.reset();
  if (other) {
    shownFull_ = pictured_ = false;
    gameId_ = store_.eventGameId(entryId);
    game_ = store_.isEvent(entryId);
    // The copy's own past was for the channel left.
    pastFor_.clear();
    past_.reset();
  }
  pictureChannel_.clear();
  backup_.reset();
  entry_ = entryId;
  picked_.clear();
  feeds_ = std::move(feeds);
  session_ = log_.newSession();
  player_->beginTune();
  failMessage_.clear();
  tunedAt_ = ms();
  if (full_) watched();
  std::vector<playback::FeedInfo> infos;
  quickTried_.clear();
  quickPending_.clear();
  noPicture_ = false;
  infos = feedInfos(entryId);
  // Before the recovery picks a version: the one it played last time isn't
  // this channel's.
  logEvent("tune", std::to_string(feeds_.size()) + (feeds_.size() == 1 ? " feed" : " feeds"), false);
  // Right to a replay left (Build 165): the archive from that moment, in
  // place of a live start, while it still holds it.
  if (replayInstead_) {
    std::pair<time::Seconds, ReplayWhy> at = *replayInstead_;
    replayInstead_.reset();
    if (startReplayInstead(at.first, at.second, 0)) return true;
    if (gameReplay_) {
      // Build 167: no copy of the channel holds the game's start (any more).
      // A Final has nothing else to play (the caller stops); a live game
      // plays live, saying so.
      logReplay("game from " + localTime(at.first) + ": no copy of " + gameReplay_->channelName +
                    " holds it" + (gameReplay_->finished ? "" : "; live"),
                0);
      if (gameReplay_->finished) return false;
      replayNotice_ = kReplayUnavailable;
    }
  }
  // Every caller has already waited (OK, a settled flip, a preview's dwell,
  // a typed number): no second settle.
  playback::TuneOptions options;
  options.settle = 0;
  options.game = store_.isEvent(entryId);
  apply(rec_.tune(ms(), std::move(infos), options));
  return true;
}

bool Session::tune(const std::string& entryId, const std::vector<std::string>& flip) {
  // The channel replaying: back to it live.
  if (replay_ && entryId == entry_) {
    if (!flip.empty()) flip_ = flip;
    goLive();
    return true;
  }
  if (entryId == entry_ && !failed()) {
    if (!flip.empty()) flip_ = flip;  // a list picked here takes over
    return true;
  }
  return begin(entryId, store_.feeds(entryId), flip);
}

bool Session::tuneEventVia(const std::string& eventId, const std::string& via, const std::vector<std::string>& flip) {
  if (!begin(eventId, store_.eventFeeds(eventId, via), flip)) return false;
  picked_ = via;
  return true;
}

bool Session::restart(std::string_view why) {
  if (entry_.empty() || feeds_.empty() || camera_ || replay_) return false;
  std::vector<playback::FeedInfo> infos = feedInfos(entry_);
  logEvent("restart", why);
  playback::TuneOptions options;
  options.start = std::min(rec_.feedIndex(), feeds_.size() - 1);
  options.settle = 0;
  options.game = store_.isEvent(entry_);
  hadPicture_ = false;
  player_->beginTune();
  apply(rec_.tune(ms(), std::move(infos), options));
  return true;
}

bool Session::useFeed(size_t index) {
  if (index >= feeds_.size()) return false;
  if (replay_) {
    // Out of the replay first; another copy picked is a live pick like any.
    size_t live = replayLive_;
    goLive();
    if (index != live) quietReturn_ = false;
  }
  apply(rec_.pick(ms(), index));
  return true;
}

void Session::flushPlayed() {
  if (!playingId_.empty() && playingSince_ != 0) {
    uint32_t now = ms();
    if (now > playingSince_) store_.notePlayed(playingId_, now - playingSince_);
  }
  playingId_.clear();
  playingSince_ = 0;
}

void Session::stop() {
  pausedAt_.reset();
  flushPlayed();
  if (entry_.empty()) return;
  leaving();
  rememberGame();
  if (replay_) endReplay(false);
  apply(rec_.stop(ms()));
  player_->stop();
  store_.closeStream();
  entry_.clear();
  picked_.clear();
  feeds_.clear();
  gameId_.clear();
  game_ = false;
}

void Session::apply(const playback::Actions& actions) {
  for (const playback::Action& a : actions) {
    switch (a.kind) {
      case playback::Action::Kind::Play:
      case playback::Action::Kind::Reconnect:
        attemptReconnect_ = a.kind == playback::Action::Kind::Reconnect;
        playFeed(a.feed, a.token);
        break;
      case playback::Action::Kind::CloseStream:
        store_.closeStream();
        // A late, superseded server open: what plays now keeps playing.
        if (a.token == 0) player_->streamClosed();
        break;
      case playback::Action::Kind::GiveUp:
        flushPlayed();
        player_->stop();
        failMessage_ = a.text;
        // The recovery's own "gave up" line is already in the log.
        MLOG_W("play", "gave up: %s", playback::safeMessage(a.text).c_str());
        break;
      case playback::Action::Kind::Note:
        if (hooks_.changed) hooks_.changed();
        break;
      case playback::Action::Kind::NoPicture:
        noPicture_ = true;
        noPictureMessage_ = a.text;
        if (hooks_.changed) hooks_.changed();
        break;
      case playback::Action::Kind::Toast:
        // A backup's picture came up: the banner says "Back on" the channel
        // (no toast; on a first tune the banner already said it was trying).
        if (full_ && hadPicture_) backup_ = BackupNotice{lastFailed_, rec_.feedIndex(), ms()};
        break;
      case playback::Action::Kind::RecordFeed:
        if (a.feed < feeds_.size()) {
          const std::string& id = feeds_[a.feed].channelId;
          // Back to live after a replay: the copy that played before it
          // showing its picture again is no new success (nothing charged,
          // nothing remembered). A failure there is live's own.
          bool quiet = quietReturn_ && a.worked;
          quietReturn_ = false;
          if (quiet) {
            problems_.erase(id);
            feedPictured_ = true;
            playingId_ = id;
            playingSince_ = std::max<uint32_t>(1, ms());
            break;
          }
          store_.recordFeed(id, a.worked);
          // Build 166: the copy's own record. A start: how long its picture
          // took (a reconnect of a picture already shown is no new start);
          // a failure: a start that showed none, or a picture that broke.
          if (a.worked) {
            if (!(attemptReconnect_ && feedPictured_)) store_.noteStart(id, ms() - attemptAt_);
            feedPictured_ = true;
            playingId_ = id;
            playingSince_ = std::max<uint32_t>(1, ms());
          } else {
            bool broke = playingId_ == id && playingSince_ != 0;
            flushPlayed();
            if (!broke) store_.noteStartFailed(id);
            else if (!stallCounted_) store_.noteStall(id);
          }
          if (a.worked) {
            store_.rememberFeed(entry_, id);
            problems_.erase(id);
          } else {
            lastFailed_ = a.feed;
            measured_.reset();
            if (a.unplayable) store_.markUnplayable(id);
            std::string why = "no picture after " +
                              std::to_string(playback::startupTimeout(rec_.feeds()[a.feed].kind) / 1000) + " s";
            if (a.unplayable) {
              why = "this TV can't play it";
            } else if (!a.detail.empty()) {
              why = a.detail;  // "black picture", "the source keeps closing"
            } else if (sawPicture_ && pictureAt_) {
              uint32_t ran = (ms() - pictureAt_) / 1000;
              why = ran >= 120 ? "stopped after " + std::to_string(ran / 60) + " min"
                               : "stopped after " + std::to_string(std::max<uint32_t>(1, ran)) + " s";
            } else if (sawFailed_) {
              why = "the server answered with an error";
            }
            problems_[id] = FeedProblem{time::now(), why};
          }
        }
        break;
      case playback::Action::Kind::Log: {
        MLOG_I("play", "%s %s", a.text.c_str(), playback::safeMessage(a.detail).c_str());
        // Build 166: a stall the viewer saw ("Buffering…") on the picture
        // playing counts against its copy.
        if (a.text == "stall" && !playingId_.empty() && rec_.feedIndex() < feeds_.size() &&
            feeds_[rec_.feedIndex()].channelId == playingId_) {
          store_.noteStall(playingId_);
          stallCounted_ = true;
        }
        std::string channel, feed, provider;
        if (const GuideChannel* g = store_.channels()->find(entry_)) channel = g->name;
        if (a.feed < feeds_.size()) {
          feed = feeds_[a.feed].label;
          provider = store_.providerName(feeds_[a.feed].channelId);
        }
        log_.add(wallMs(), a, session_, channel, feed, provider);
        break;
      }
    }
  }
}

void Session::playFeed(size_t index, uint64_t token) {
  if (index >= feeds_.size()) return;
  flushPlayed();
  attemptAt_ = ms();
  stallCounted_ = false;
  if (!attemptReconnect_) feedPictured_ = false;
  sawPicture_ = sawBuffering_ = sawFailed_ = sawEnded_ = false;
  factsLogged_ = noPicture_ = false;
  measured_.reset();
  player_->attempt();
  const Feed& f = feeds_[index];
  MLOG_I("play", "feed %zu of %zu (%s)", index + 1, feeds_.size(), f.label.c_str());
  // Experimental Emby quick start: the direct link first, no open step; a
  // failed start falls back to the normal open (FeedInfo::retryOnce).
  // A quick start that didn't show a picture before this attempt: that
  // server refuses them; it isn't tried there again.
  if (!quickPending_.empty()) {
    store_.quickStartRefused(quickPending_);
    logEvent("quick start", "refused by the server; normal open from now on");
    quickPending_.clear();
  }
  if (f.needsOpen && store_.settings().embyQuickStart && !store_.quickStartRefusedBy(f.channelId) &&
      quickTried_.insert(f.channelId).second) {
    if (std::optional<Feed> q = store_.quickOpen(f)) {
      quickPending_ = f.channelId;
      logEvent("quick start", "the server's direct link");
      feeds_[index] = *q;
      feeds_[index].needsOpen = true;  // a retry goes through the normal open
      Feed play = *q;
      player_->play(play, true);
      apply(rec_.streamHanded(ms(), token));
      return;
    }
  }
  if (f.needsOpen) {
    // Emby / Jellyfin open the stream on the server first. Every open
    // reports back exactly once; a superseded one is closed again.
    uint32_t asked = ms();
    store_.open(f, [this, token, index, asked](std::optional<Feed> opened) {
      uint32_t now = ms();
      // How long the server took to open it (the playback log, for tuning).
      if (rec_.isCurrent(token)) logEvent("server opened", std::to_string(now - asked) + " ms");
      if (!opened) {
        apply(rec_.openFailed(now, token, "The server couldn't open this channel."));
        return;
      }
      if (rec_.isCurrent(token) && index < feeds_.size()) {
        feeds_[index] = *opened;
        player_->play(*opened, true);
      }
      apply(rec_.streamHanded(now, token));
    });
    return;
  }
  player_->play(f, false);
  apply(rec_.streamHanded(ms(), token));
}

// The feeds' recovery facts: their kind, and whether each should show a
// picture (not radio, not kept as sound only).
std::vector<playback::FeedInfo> Session::feedInfos(const std::string& entryId) const {
  std::vector<playback::FeedInfo> infos;
  bool quick = store_.settings().embyQuickStart;
  auto channels = store_.channels();
  const GuideChannel* entry = channels->find(entryId);
  bool entryAudio = (entry && entry->radio) || store_.audioOnly(entryId);
  for (const Feed& f : feeds_) {
    playback::FeedInfo info{f.channelId, kindOf(f), quick && f.needsOpen};
    // A game's source is a guide channel's feed: that channel may be radio.
    const GuideChannel* g = entry ? entry : channels->find(store_.entryOfChannel(f.channelId));
    info.expectVideo = !entryAudio && !(g && g->radio) && !store_.audioOnly(f.channelId);
    infos.push_back(std::move(info));
  }
  return infos;
}

bool Session::expectsVideo() const {
  size_t i = rec_.feedIndex();
  return i < rec_.feeds().size() && rec_.feeds()[i].expectVideo && !rec_.listening();
}

// What the picture check makes of the stream now, for recovery. The stream
// itself (the relay) is the better witness: when it says the frames are
// fine, a picture the TV doesn't show is never the source's fault.
void Session::pictureCheck(uint32_t now, const PlayerState& s, bool picture) {
  using media::Verdict;
  Verdict v = Verdict::Unknown;
  std::string detail = s.streamDetail;
  bool streamSays = s.streamChecked && s.stream != Verdict::Unknown;
  if (streamSays && s.stream != Verdict::Picture && s.stream != Verdict::Black && s.stream != Verdict::Static) {
    v = s.stream;  // unsupported, scrambled, no video, tables only
  } else if (s.videoUnsupported) {
    v = Verdict::Unsupported;
    detail = "the player can't decode its video";
  } else if (s.videoTracks == 0 && !picture && !(streamSays && s.stream == Verdict::Picture)) {
    // (The player's track list is only trusted when the relay hasn't seen
    // real pictures in the same stream.)
    v = Verdict::NoVideo;
    detail = "the player lists none";
  } else if (streamSays) {
    v = s.stream;
  } else if (s.framesStalled) {
    v = Verdict::Frozen;
    detail = "frames stopped while the sound plays";
  } else if (picture) {
    // Frames are drawn; with a relay reading the stream, wait for its word.
    v = s.streamChecked ? Verdict::Unknown : Verdict::Picture;
    detail.clear();
  } else if (s.playing) {
    v = Verdict::NoFrame;
    detail.clear();
  }
  apply(rec_.videoCheck(now, v, detail));
}

void Session::keepListening() {
  if (!noPicture_) return;
  noPicture_ = false;
  store_.markAudioOnly(entry_);
  std::string channel = playingChannel();
  if (!channel.empty()) store_.markAudioOnly(channel);
  logEvent("keep listening", "sound only, no picture check, for a week");
  if (hooks_.changed) hooks_.changed();
}

void Session::watched() {
  if (entry_.empty()) return;
  shownFull_ = true;
  if (entry_ == lastWatched_) return;
  lastWatched_ = entry_;
  store_.noteWatched(entry_);
}

std::string Session::sourceNow() const {
  if (camera_) return cameraSource_;
  if (pictureChannel_.empty()) return picked_;
  std::string entry = store_.entryOfChannel(pictureChannel_);
  // The pick while it plays (one of its versions for a channel picked)...
  if (!picked_.empty() && (picked_ == pictureChannel_ || picked_ == entry)) return picked_;
  // ... else the source whose picture showed: a guide channel's version
  // stands for the channel, as Where to Watch lists it.
  return entry.empty() ? pictureChannel_ : entry;
}

Session::Viewed Session::viewedNow() const {
  Viewed v;
  v.entry = entry_;
  v.game = game_;
  v.feed = camera_ ? std::string() : pictureChannel_;
  if (v.game) {
    v.source = sourceNow();
    v.gameId = gameId_;
    auto channels = store_.channels();
    if (channels->find(v.source)) {
      v.channel = v.source;
    } else {
      std::string raw = v.feed.empty() ? v.source : v.feed;
      if (!raw.empty()) v.channel = store_.entryOfChannel(raw);
    }
  } else {
    v.channel = entry_;
  }
  v.flip = flip_;
  if (!flip_.empty() && flip_ == pickedFlip_) v.tab = pickedTab_;
  // (The timeline holding live's picture is no moment left.)
  if (replay_ && !(replay_->fromLive && replay_->quiet)) v.replayAt = replay_->at();
  return v;
}

void Session::leaving() {
  // Full screen with a picture, however briefly (a run of quick flips is
  // already one tune: the player waits for the channel stopped on).
  if (entry_.empty() || !shownFull_ || !pictured_) return;
  Viewed v = viewedNow();
  viewed_.erase(std::remove_if(viewed_.begin(), viewed_.end(),
                               [&v](const Viewed& o) {
                                 return o.entry == v.entry || (!v.gameId.empty() && o.gameId == v.gameId);
                               }),
                viewed_.end());
  viewed_.insert(viewed_.begin(), std::move(v));
  if (viewed_.size() > kViewedKept) viewed_.resize(kViewedKept);
}

const Session::Viewed* Session::previousTarget() const {
  for (const Viewed& v : viewed_) {
    // Not what plays (nor the same game under a new id).
    if (v.entry == entry_ || (!v.gameId.empty() && v.gameId == gameId_)) continue;
    return &v;
  }
  return nullptr;
}

std::string Session::previousEntry() const {
  const Viewed* v = previousTarget();
  return v ? v->entry : std::string();
}

std::string Session::gameNow(const Viewed& v) const {
  if (!v.game) return {};
  if (store_.isEvent(v.entry)) return v.entry;
  return v.gameId.empty() ? std::string() : store_.eventOfGame(v.gameId);
}

bool Session::replay(const std::string& entryId, std::vector<Feed> feeds, const std::string& feed,
                     const std::vector<std::string>& flip) {
  // The feed whose picture showed first, the rest after it as they were.
  auto it = std::find_if(feeds.begin(), feeds.end(), [&feed](const Feed& f) { return f.channelId == feed; });
  if (!feed.empty() && it != feeds.end()) std::rotate(feeds.begin(), it, it + 1);
  return begin(entryId, std::move(feeds), flip);
}

std::string Session::flipTarget(const std::string& from, int step) const {
  if (flip_.empty()) return {};
  auto it = std::find(flip_.begin(), flip_.end(), from.empty() ? entry_ : from);
  long n = static_cast<long>(flip_.size());
  long i = it == flip_.end() ? 0 : (it - flip_.begin());
  i = ((i + step) % n + n) % n;
  return flip_[static_cast<size_t>(i)];
}

std::string Session::flip(int step) {
  std::string next = flipTarget(entry_, step);
  if (!next.empty()) tune(next);
  return next;
}

std::string Session::previous() {
  const Viewed* target = previousTarget();
  if (!target) return {};
  Viewed v = *target;  // (begin() changes the list)
  auto toast = [this](const std::string& text) {
    if (hooks_.toast) hooks_.toast(text);
  };
  auto channels = store_.channels();
  std::string tuned, how;
  // Left during a replay: the archive from that moment (begin() checks it
  // still holds it; else live).
  if (v.replayAt) replayInstead_ = std::make_pair(*v.replayAt, ReplayWhy::Back);
  struct Clear {
    std::optional<std::pair<time::Seconds, ReplayWhy>>& what;
    ~Clear() { what.reset(); }
  } clear{replayInstead_};
  if (v.game) {
    std::string id = gameNow(v);
    if (!id.empty()) {
      // The game on its last source, with its own flip list.
      std::vector<std::string> flip = v.flip.empty() || id != v.entry ? store_.eventFlipList(id) : v.flip;
      if (!replay(id, store_.eventFeeds(id, v.source), v.feed, flip)) {
        toast("This game has no working feed right now");
        return {};
      }
      picked_ = v.source;
      tuned = id;
      how = id == v.entry ? "game" : "game, found again";
    } else {
      // Off the board: the channel it played on (unless that's on already).
      const GuideChannel* g = v.channel == entry_ ? nullptr : channels->find(v.channel);
      std::string name = g ? g->name : std::string();
      if (!g || !replay(v.channel, store_.feeds(v.channel), v.feed, views::lineupList(*channels, v.channel))) {
        toast("That game has ended");
        return {};
      }
      toast("That game has ended \xc2\xb7 back on " + name);
      tuned = v.channel;
      how = "its channel; the game has ended";
      v.tab.reset();
    }
  } else {
    const GuideChannel* g = channels->find(v.entry);
    std::vector<std::string> flip = v.flip.empty() ? views::lineupList(*channels, v.entry) : v.flip;
    if (!g || !replay(v.entry, store_.feeds(v.entry), v.feed, flip)) {
      toast((g ? g->name : std::string("That channel")) + " has no working feed right now");
      return {};
    }
    tuned = v.entry;
    how = "channel";
  }
  // The guide list it was picked from, again.
  if (v.tab) {
    pickedTab_ = *v.tab;
    pickedFlip_ = flip_;
  }
  bouncing_ = true;
  logEvent("back to", how);
  return tuned;
}

void Session::suspend() {
  if (paused_) return;
  paused_ = true;
  flushPlayed();
  if (entry_.empty()) return;
  Paused p;
  if (replay_ && replay_->phase == Replay::Phase::Paused) {
    // A replay's pause stays a pause: its stream closes now.
    if (!replay_->released) {
      player_->stop();
      replay_->released = true;
    }
    p.ready = true;
    p.game = game_;
    p.feed = replayLive_;
    p.replayPaused = true;
    logEvent("paused", "app in background");
    pausedAt_ = p;
    return;
  }
  size_t live = rec_.feedIndex();
  if (replay_) {
    // A replay playing: from the same moment on return. (The recovery
    // already stands aside.)
    p.replayAt = replay_->at();
    live = replayLive_;
    endReplay(false);
  }
  p.restart = !failed();
  p.ready = p.replayAt ? true : ready();
  p.game = game_;
  p.feed = feeds_.empty() ? 0 : std::min(live, feeds_.size() - 1);
  logEvent("paused", "app in background");
  if (p.restart) {
    // The recovery stops where it is: no timer falls due, an open still on
    // the server is closed as it comes back, and nothing is charged to a
    // feed. Its "stop" line isn't one: it plays again on return.
    playback::Actions stopped = rec_.stop(ms());
    stopped.erase(std::remove_if(stopped.begin(), stopped.end(),
                                 [](const playback::Action& a) { return a.kind == playback::Action::Kind::Log; }),
                  stopped.end());
    apply(stopped);
    quickPending_.clear();
    measured_.reset();
    backup_.reset();
  }
  player_->stop();
  pausedAt_ = p;
}

void Session::resume() {
  if (!paused_) return;
  paused_ = false;
  std::optional<Paused> p = pausedAt_;
  pausedAt_.reset();
  if (!p || entry_.empty()) return;
  if (p->replayPaused) {
    logEvent("resumed", "the replay stays paused");
    if (hooks_.changed) hooks_.changed();
    return;
  }
  if (!p->restart) {
    logEvent("resumed", "the message stays");
    return;
  }
  // Gone meanwhile (a game off the board, a channel no longer listed):
  // nothing to come back to, quietly.
  bool gone = p->game ? !store_.isEvent(entry_) : !store_.channels()->find(entry_);
  if (gone || feeds_.empty()) {
    logEvent("resumed", p->game ? "the game is over; stopped" : "the channel is gone; stopped");
    stop();
    if (hooks_.changed) hooks_.changed();
    return;
  }
  // The same source again, as a fresh start (its backups after it).
  logEvent("resumed", "the same source again");
  hadPicture_ = false;
  failMessage_.clear();
  noPicture_ = false;
  player_->beginTune();
  tunedAt_ = ms();
  // A replay that was playing: on from the same moment.
  if (p->replayAt && startReplayInstead(*p->replayAt, ReplayWhy::Resume, p->feed)) {
    if (hooks_.changed) hooks_.changed();
    return;
  }
  playback::TuneOptions options;
  options.start = p->feed;
  options.settle = 0;
  options.game = p->game;
  apply(rec_.tune(ms(), feedInfos(entry_), options));
  if (hooks_.changed) hooks_.changed();
}

void Session::setShowingFull(bool full) {
  full_ = full;
  if (full) watched();
}

Session::Look Session::look() const {
  Look l;
  l.entry = entry_;
  l.note = note();
  l.feed = feedIndex();
  l.full = full_;
  l.ready = ready();
  l.failed = failed();
  l.noPicture = noPicture_;
  l.hadPicture = hadPicture_;
  l.backup = backup_.has_value();
  if (replay_) {
    l.replay = 1 + static_cast<int>(replay_->phase);
    l.replayFeed = replay_->feed;
    l.replayReleased = replay_->released;
  }
  return l;
}

bool Session::tick(uint32_t now) {
  if (now >= memoryAt_) {
    if (memoryAt_) logMemory();
    memoryAt_ = now + kMemoryEveryMs;
  }
  for (std::string& note : store_.takeEventsLog()) log_.add(wallMs(), "live events", 0, {}, {}, {}, note);
  for (std::string& note : store_.takeGuideLog()) log_.add(wallMs(), "guide", 0, {}, {}, {}, note);
  saveLog(now);
  // Build 166: the providers compared, once a day at most, when the app
  // has started and its lineup is known (looked at every minute until then).
  if (!providersLooked_ && now >= providersLookAt_) {
    providersLookAt_ = now + 60000;
    time::Seconds last = store_.providersLoggedAt();
    providersLooked_ = (last > 0 && time::now() - last < 86400) || logProviders(false);
  }
  // A long play goes into its copy's record every few minutes.
  if (playingSince_ != 0 && now - playingSince_ >= kPlayedFlushMs) {
    store_.notePlayed(playingId_, now - playingSince_);
    playingSince_ = now;
  }
  // Paused in the background: the recovery waits for the app's return.
  if (entry_.empty() || paused_) return false;
  if (replay_) {
    // A replay's own path: the recovery stands aside.
    tickReplay(now);
    return replay_ && replay_->phase == Replay::Phase::Opening;
  }
  PlayerState s = player_->state();
  // A picture is a frame drawn. Radio (or a channel kept as sound only)
  // plays when its clock runs; so does a stream whose frames the relay sees
  // are fine while the TV shows none (the display is ours: never a flip).
  bool expect = expectsVideo();
  bool picture = s.picture || (s.playing && (!expect || (s.streamChecked && s.stream == media::Verdict::Picture)));
  // Report changes of the player's flags only.
  if (picture && !sawPicture_) {
    quickPending_.clear();  // a quick start that worked
    sawPicture_ = true;
    hadPicture_ = true;
    // Right's target: a picture of a feed handed to the player (not one
    // the player still held from before).
    if (rec_.phase() == playback::Recovery::Phase::Playing && rec_.feedIndex() < feeds_.size()) {
      pictured_ = true;
      pictureChannel_ = feeds_[rec_.feedIndex()].channelId;
    }
    pictureAt_ = now;
    apply(rec_.firstFrame(now));
    if (hooks_.picture) hooks_.picture();
  }
  if (s.buffering != sawBuffering_) {
    sawBuffering_ = s.buffering;
    apply(s.buffering ? rec_.bufferingStarted(now) : rec_.bufferingEnded(now));
  }
  if ((s.failed || s.ended) && !sawPicture_) {
    if (auto retry = player_->retryAnotherWay()) {
      logEvent(retry->event, retry->detail);
      sawFailed_ = sawEnded_ = false;
      // The direct try gets its own start-up time (it was cut at 6 s from
      // the relay's start).
      rec_.restartedAnotherWay(now);
      return true;
    }
  }
  if (s.failed && !sawFailed_) {
    sawFailed_ = true;
    apply(rec_.playerError(now, s.error));
  }
  if (s.ended && !sawEnded_) {
    sawEnded_ = true;
    apply(rec_.ended(now));
  }
  if (auto n = player_->note()) logEvent(n->event, n->detail);
  for (const Player::SourceClosed& c : player_->sourceClosed())
    apply(rec_.sourceClosed(now, c.lastedMs, c.bytes, c.reopened));
  if (expect) pictureCheck(now, s, picture);
  if (sawPicture_ && !factsLogged_ && !s.facts.empty()) {
    factsLogged_ = true;
    logEvent("video", s.facts);
    measured_ = quality::fromFacts(s.facts);
  }
  // Build 136: the picture is remembered for the feed's rows once it has
  // played a while (a placeholder clip is caught before that).
  if (measured_ && sawPicture_ && rec_.phase() == playback::Recovery::Phase::Playing && ready() &&
      now - pictureAt_ >= kMeasuredAfterMs && rec_.feedIndex() < feeds_.size()) {
    store_.notePicture(feeds_[rec_.feedIndex()].channelId, *measured_);
    measured_.reset();
  }
  // Sound only (no feed showed a picture): its clock is what plays.
  if (sawPicture_ || rec_.listening()) apply(rec_.position(now, s.position, s.bufferEnd));
  else if (s.reportsLoading) rec_.loading(s.loaded);
  apply(rec_.tick(now));
  return (!ready() && !failed()) || !note().empty();
}

// ---------------------------------------------------------------- replay

bool Session::keepsArchive(size_t i, size_t live) const {
  if (i >= feeds_.size()) return false;
  const std::string& id = feeds_[i].channelId;
  if (store_.catchupDays(id) == 0) return false;
  // Build 167: a game's replay from Live Events plays on its channel's own
  // copies only.
  if (gameReplay_) return store_.entryOfChannel(id) == gameReplay_->channelId;
  // A guide channel's own versions (not a game's backup on another channel).
  return i == live || game_ || store_.entryOfChannel(id) == entry_;
}

uint16_t Session::longestArchive(size_t live) const {
  uint16_t best = 0;
  for (size_t i = 0; i < feeds_.size(); ++i)
    if (keepsArchive(i, live)) best = std::max(best, store_.catchupDays(feeds_[i].channelId));
  return best;
}

std::optional<size_t> Session::archiveCopyFor(time::Seconds at, size_t live, size_t playing,
                                              const std::set<size_t>* skip) const {
  time::Seconds now = time::now();
  std::optional<size_t> best;
  uint16_t bestDays = 0;
  std::optional<double> bestScore;
  for (size_t i = 0; i < feeds_.size(); ++i) {
    if (!keepsArchive(i, live) || (skip && skip->count(i))) continue;
    uint16_t days = store_.catchupDays(feeds_[i].channelId);
    if (!replay::covers(days, at, now)) continue;
    std::optional<double> score = store_.feedScore(feeds_[i].channelId);
    bool better = !best;
    if (best) {
      // The longest archive; then the copy playing; then a clearly better
      // measured one; else live order (the first found).
      if (days != bestDays) better = days > bestDays;
      else if ((i == playing) != (*best == playing)) better = i == playing;
      else if (score && bestScore) better = *score > *bestScore + scores::kClearlyBetter;
    }
    if (better) {
      best = i;
      bestDays = days;
      bestScore = score;
    }
  }
  return best;
}

std::optional<size_t> Session::archiveCopy(size_t live) const {
  if (feeds_.empty()) return std::nullopt;
  return archiveCopyFor(time::now(), live, live);
}

std::optional<size_t> Session::catchupCopy() const {
  if (entry_.empty() || paused_ || camera_ || feeds_.empty()) return std::nullopt;
  if (replay_) return replay_->feed;
  if (!rec_.sawFirstFrame() || failed()) return std::nullopt;
  return archiveCopy(rec_.feedIndex());
}

uint16_t Session::catchupDays() const {
  if (!catchupCopy()) return 0;
  return longestArchive(replay_ ? replayLive_ : rec_.feedIndex());
}

time::Seconds Session::replayAt() const { return replay_ ? replay_->at() : time::now(); }

time::Seconds Session::behindLive() const {
  return replay_ ? std::max<time::Seconds>(0, time::now() - replay_->at()) : 0;
}

std::string Session::localTime(time::Seconds t) const {
  time::Zone zone = host_.zone ? host_.zone() : time::Zone::device();
  time::Civil at = zone.toLocal(t);
  char hm[8];
  std::snprintf(hm, sizeof hm, "%02d:%02d", at.hour, at.minute);
  return hm;
}

void Session::logReplay(std::string detail, size_t copy, bool clock) {
  if (clock)
    detail += "; server clock " + (replayOffset_ ? sources::xtream::offsetText(*replayOffset_)
                                                 : std::string("not known, asked as UTC"));
  std::string channel, feed, provider;
  if (const GuideChannel* g = store_.channels()->find(entry_)) channel = g->name;
  else if (const EventTile* e = store_.eventTile(entry_)) channel = e->heroTitle.empty() ? e->title : e->heroTitle;
  if (copy < feeds_.size()) {
    feed = feeds_[copy].label;
    provider = store_.providerName(feeds_[copy].channelId);
  }
  // Never a link: the detail is ours, and goes through the log's filter too.
  detail = playback::safeMessage(detail);
  MLOG_I("play", "replay: %s", detail.c_str());
  log_.add(wallMs(), "replay", session_, channel, feed, provider, detail);
}

void Session::standAside() {
  flushPlayed();
  // The recovery stops where it is, as in the background: no timer falls
  // due and nothing is charged to a feed; its "stop" line isn't one.
  playback::Actions stopped = rec_.stop(ms());
  stopped.erase(std::remove_if(stopped.begin(), stopped.end(),
                               [](const playback::Action& a) { return a.kind == playback::Action::Kind::Log; }),
                stopped.end());
  apply(stopped);
  quickPending_.clear();
  measured_.reset();
  backup_.reset();
  noPicture_ = false;
  quietReturn_ = false;
}

void Session::playReplay(size_t index, time::Seconds from, const std::string& what, bool next) {
  Replay& r = *replay_;
  index = std::min(index, feeds_.size() - 1);
  time::Seconds now = time::now();
  r.phase = Replay::Phase::Opening;
  r.feed = index;
  r.days = store_.catchupDays(feeds_[index].channelId);
  r.from = replay::minuteOf(from);
  r.atMs = static_cast<int64_t>(r.from) * 1000;
  r.startedAt = ms();
  r.pictureAt = 0;
  r.held = r.released = r.fromLive = false;
  r.next = next;
  replayTick_ = ms();
  replayTried_.insert(index);
  {
    std::vector<playback::FeedInfo> infos = feedInfos(entry_);
    r.video = index < infos.size() ? infos[index].expectVideo : true;
  }
  // The link carries the login: to the player only, never the log.
  std::optional<Store::CatchupFeed> f = store_.catchupFeed(feeds_[index], r.from, now);
  replayOffset_ = f ? f->serverOffset : std::nullopt;
  r.minutes = f ? f->minutes : 0;
  if (hooks_.changed) hooks_.changed();
  if (!f) {
    logReplay(what, index);
    replayFailed("no replay link could be made for this copy");
    return;
  }
  logReplay(what, index, true);
  player_->attempt();
  player_->play(f->feed, false);
}

bool Session::replayFrom(time::Seconds from, ReplayWhy why) {
  if (!catchupCopy()) return false;
  // Inside the longest archive; the copy whose archive covers the moment
  // (the longest, then the one replaying or playing live, ...).
  size_t live = replay_ ? replayLive_ : rec_.feedIndex();
  uint16_t days = longestArchive(live);
  if (!days) return false;
  time::Seconds now = time::now();
  from = std::max(replay::minuteOf(from), replay::archiveStart(now, days));
  if (now - from < kLiveWithin) {
    // A minute from live is live.
    if (replay_) goLive();
    return true;
  }
  std::optional<size_t> copy = archiveCopyFor(from, live, replay_ ? replay_->feed : live);
  if (!copy) return false;
  if (!replay_) {
    replayLive_ = rec_.feedIndex();
    replayLiveReady_ = rec_.sawFirstFrame();
    standAside();
  }
  replay_ = Replay{};
  replayTried_.clear();
  shortStretches_ = 0;
  std::string at = localTime(from);
  std::string what = why == ReplayWhy::StartOver ? "start over " + at
                     : why == ReplayWhy::Rewind  ? "rewind to " + at
                     : why == ReplayWhy::Back    ? "back to " + at
                                                 : "resumed " + at;
  playReplay(*copy, from, what);
  return true;
}

bool Session::startReplayInstead(time::Seconds at, ReplayWhy why, size_t live) {
  if (camera_ || feeds_.empty() || entry_.empty()) return false;
  live = std::min(live, feeds_.size() - 1);
  time::Seconds now = time::now();
  at = replay::minuteOf(at);
  if (now - at < kLiveWithin) return false;
  // A copy whose archive still holds the moment (the same edge as the
  // timeline's: an hour in hand).
  std::optional<size_t> copy = archiveCopyFor(at, live, live);
  if (!copy) return false;
  standAside();
  replayLive_ = live;
  replayLiveReady_ = false;
  replay_ = Replay{};
  replayTried_.clear();
  shortStretches_ = 0;
  std::string when = localTime(at);
  if (why == ReplayWhy::Game) {
    playReplay(*copy, at, "game from " + when + " (Live Events" +
                              (gameReplay_ && gameReplay_->finished ? ", final)" : ", live)"));
    return true;
  }
  playReplay(*copy, at, (why == ReplayWhy::Back ? "back to " : "resumed ") + when +
                            (why == ReplayWhy::Resume ? " after the background" : ""));
  return true;
}

bool Session::tuneGameReplay(const GameReplay& g, const std::vector<std::string>& flip) {
  if (g.eventId.empty() || g.channelId.empty() || !store_.isEvent(g.eventId)) return false;
  // A Final: the channel's own copies first (Go live is that channel live);
  // live: as OK plays it.
  std::vector<Feed> feeds = store_.eventFeeds(g.eventId, g.finished ? g.channelId : std::string());
  if (feeds.empty()) return false;
  pendingGame_ = g;
  replayInstead_ = std::make_pair(g.from, ReplayWhy::Game);
  bool ok = begin(g.eventId, std::move(feeds), flip);
  pendingGame_.reset();
  replayInstead_.reset();
  if (!ok) {
    stop();
    return false;
  }
  if (g.finished) picked_ = g.channelId;
  return true;
}

bool Session::takeGameBack() {
  bool out = gameBack_;
  gameBack_ = false;
  return out;
}

bool Session::takeEndOfGame() {
  bool out = endOfGame_;
  endOfGame_ = false;
  return out;
}

void Session::gameReplayGone(const std::string& why) {
  size_t copy = replay_ ? replay_->feed : 0;
  if (replay_ && !replay_->released) player_->stop();
  logReplay("failed: " + why + "; back to the game's card", copy);
  replayNotice_ = kReplayUnavailable;
  gameBack_ = true;
  endReplay(false);
}

bool Session::pause(bool quiet) {
  if (entry_.empty() || paused_) return false;
  if (replay_) {
    Replay& r = *replay_;
    if (r.phase == Replay::Phase::Paused) return true;
    bool showing = r.phase == Replay::Phase::Playing;
    r.phase = Replay::Phase::Paused;
    r.pausedAt = ms();
    r.fromLive = false;
    r.next = false;
    r.held = showing && player_->pause();
    if (!r.held) {
      player_->stop();
      r.released = true;
    }
    if (!quiet) logReplay("paused at " + localTime(r.at()), r.feed);
    if (hooks_.changed) hooks_.changed();
    return true;
  }
  std::optional<size_t> copy = catchupCopy();
  if (!copy) return false;
  replayLive_ = rec_.feedIndex();
  replayLiveReady_ = rec_.sawFirstFrame();
  // The picture held first: the recovery then stands aside (closing what a
  // server opened), and the stream itself closes a few seconds later.
  bool held = player_->pause();
  standAside();
  replay_ = Replay{};
  Replay& r = *replay_;
  r.phase = Replay::Phase::Paused;
  r.feed = *copy;
  r.days = store_.catchupDays(feeds_[*copy].channelId);
  r.atMs = static_cast<int64_t>(time::now()) * 1000;
  r.from = replay::minuteOf(r.at());
  r.pausedAt = ms();
  r.fromLive = true;
  r.quiet = quiet;
  r.held = held;
  replayTried_.clear();
  shortStretches_ = 0;
  if (!r.held) {
    player_->stop();
    r.released = true;
  }
  replayTick_ = ms();
  if (!quiet) logReplay("paused at " + localTime(r.at()) + " (live)", replayLive_);
  if (hooks_.changed) hooks_.changed();
  return true;
}

bool Session::resumePaused(bool quiet) {
  if (!replay_ || replay_->phase != Replay::Phase::Paused || paused_) return false;
  Replay& r = *replay_;
  time::Seconds now = time::now();
  // From the paused minute (a few seconds early), inside the longest
  // archive, on the copy that covers it.
  time::Seconds from = std::max(replay::minuteOf(r.at()), replay::archiveStart(now, longestArchive(replayLive_)));
  if (now - from < kLiveWithin) {
    goLive(quiet);
    return true;
  }
  std::optional<size_t> copy = archiveCopyFor(from, replayLive_, r.feed);
  if (!copy) {
    // Build 167: a Final has no live of its own to go to: back to its card.
    if (gameReplay_ && gameReplay_->finished) {
      gameReplayGone("the archive no longer holds " + localTime(from));
      return true;
    }
    goLive(quiet);
    return true;
  }
  if (!r.released) player_->stop();
  replayTried_.clear();
  shortStretches_ = 0;
  playReplay(*copy, from, "resumed " + localTime(from));
  return true;
}

void Session::goLive(bool quiet) {
  if (!replay_) return;
  if (!quiet) logReplay("back to live", replayLive_);
  endReplay(true);
}

void Session::endReplay(bool live) {
  if (!replay_) return;
  replay_.reset();
  replayTried_.clear();
  replayOffset_.reset();
  shortStretches_ = 0;
  if (hooks_.changed) hooks_.changed();
  if (!live || entry_.empty() || feeds_.empty()) return;
  // The copy live played on, as a fresh start (its backups after it), like
  // a return from the background.
  hadPicture_ = false;
  failMessage_.clear();
  noPicture_ = false;
  player_->beginTune();
  tunedAt_ = ms();
  playback::TuneOptions options;
  options.start = std::min(replayLive_, feeds_.size() - 1);
  options.settle = 0;
  options.game = game_;
  apply(rec_.tune(ms(), feedInfos(entry_), options));
  quietReturn_ = true;
}

std::string Session::takeReplayNotice() {
  std::string out;
  out.swap(replayNotice_);
  return out;
}

void Session::replayFailed(const std::string& why) {
  if (!replay_) return;
  Replay& r = *replay_;
  player_->stop();
  // Another copy whose archive holds the same moment (in the same order).
  time::Seconds at = replay::minuteOf(r.at());
  std::set<size_t> tried = replayTried_;
  tried.insert(r.feed);
  std::optional<size_t> other = archiveCopyFor(at, replayLive_, r.feed, &tried);
  if (other) {
    logReplay("failed: " + why + "; another copy", r.feed);
    shortStretches_ = 0;
    playReplay(*other, at, "another copy from " + localTime(at));
    return;
  }
  if (gameReplay_ && gameReplay_->finished) {
    // Build 167: a Final's replay goes back to its card.
    gameReplayGone(why);
    return;
  }
  logReplay("failed: " + why + "; back to live", r.feed);
  replayNotice_ = kReplayUnavailable;
  endReplay(true);
}

void Session::tickReplay(uint32_t now) {
  Replay& r = *replay_;
  uint32_t dt = now - replayTick_;
  replayTick_ = now;
  if (r.phase == Replay::Phase::Paused) {
    // The picture held a moment; then the stream closes (the provider's
    // connection is free).
    if (!r.released && now - r.pausedAt >= kReleaseMs) {
      player_->stop();
      r.released = true;
      if (hooks_.changed) hooks_.changed();
    }
    return;
  }
  PlayerState s = player_->state();
  // What the relay notes of its own belongs to live playback: read and left.
  (void)player_->note();
  (void)player_->sourceClosed();
  bool picture = s.picture || (s.playing && (!r.video || (s.streamChecked && s.stream == media::Verdict::Picture)));
  if (r.phase == Replay::Phase::Opening) {
    if (picture) {
      r.phase = Replay::Phase::Playing;
      r.pictureAt = std::max<uint32_t>(1, now);
      r.next = false;
      // Watched, for Right: the channel counts as viewed (on its live copy).
      pictured_ = true;
      hadPicture_ = true;
      if (pictureChannel_.empty() && replayLive_ < feeds_.size()) pictureChannel_ = feeds_[replayLive_].channelId;
      if (hooks_.picture) hooks_.picture();
      if (hooks_.changed) hooks_.changed();
      return;
    }
    if (s.failed || s.ended) {
      std::string error = s.failed ? playback::safeMessage(s.error) : std::string("the stream ended at once");
      replayFailed(error.empty() ? std::string("the player reported an error") : error);
      return;
    }
    if (now - r.startedAt >= kReplayTimeoutMs) replayFailed("no picture after " + std::to_string(kReplayTimeoutMs / 1000) + " s");
    return;
  }
  // Playing: the moment showing moves on while the picture does.
  if (!s.buffering && (s.playing || picture)) r.atMs += dt;
  // Build 167: a Final's replay past the moment the app saw its final (never
  // when it didn't see it): the player's "End of the game", once.
  if (gameReplay_ && gameReplay_->finished && gameReplay_->finalAt > 0 && !endOfGameSaid_ &&
      r.at() >= gameReplay_->finalAt) {
    endOfGameSaid_ = endOfGame_ = true;
    logReplay("past the final (" + localTime(gameReplay_->finalAt) + ")", r.feed);
    if (hooks_.changed) hooks_.changed();
  }
  time::Seconds live = time::now();
  if (live - r.at() <= kLiveWithin) {
    logReplay("caught up", r.feed);
    replayNotice_ = "You\xe2\x80\x99re live";
    endReplay(true);
    return;
  }
  if (s.failed || s.ended) {
    // The stretch ended (or broke) behind live: the next one from where it
    // ended, seamlessly. Several short ones in a row: it isn't working.
    shortStretches_ = now - r.pictureAt < kShortStretchMs ? shortStretches_ + 1 : 0;
    if (shortStretches_ >= kShortStretchesTried) {
      replayFailed("the archive keeps stopping");
      return;
    }
    time::Seconds at = replay::minuteOf(r.at());
    replayTried_.clear();
    playReplay(r.feed, at, "next stretch from " + localTime(at), true);
  }
}

void Session::wantPast() {
  std::optional<size_t> copy = catchupCopy();
  if (!copy || pastFor_ == entry_) return;
  pastFor_ = entry_;
  past_.reset();
  std::string entry = entry_;
  std::weak_ptr<int> alive = alive_;
  store_.archiveGuide(feeds_[*copy].channelId,
                      [this, alive, entry](std::optional<std::vector<Store::PastProgramme>> list) {
                        if (alive.expired() || entry != entry_ || pastFor_ != entry) return;
                        ++pastSerial_;
                        if (!list) {
                          // Fine: the timeline works by minutes all the same.
                          log_.add(wallMs(), "replay", session_, {}, {}, {}, "past listings: the request failed");
                          return;
                        }
                        std::vector<replay::Programme> own;
                        time::Seconds now = time::now(), earliest = now;
                        int archived = 0;
                        for (Store::PastProgramme& p : *list) {
                          if (p.start >= now) continue;
                          earliest = std::min(earliest, p.start);
                          if (p.archived) ++archived;
                          replay::Programme q;
                          q.start = p.start;
                          q.end = p.end;
                          q.title = std::move(p.title);
                          q.archived = p.archived;
                          own.push_back(std::move(q));
                        }
                        log_.add(wallMs(), "replay", session_, {}, {}, {},
                                 "past listings: " + std::to_string((now - earliest + 1800) / 3600) + " h, " +
                                     std::to_string(archived) + " in archive");
                        past_ = std::move(own);
                        if (hooks_.changed) hooks_.changed();
                      });
}

std::vector<replay::Programme> Session::timeline(time::Seconds from) const {
  std::optional<size_t> copy = catchupCopy();
  // A game's timeline is its channel's.
  std::string entry = entry_;
  if (game_ && copy) entry = store_.entryOfChannel(feeds_[*copy].channelId);
  std::vector<replay::Programme> guide;
  for (Listing& l : store_.pastListing(entry, from)) {
    replay::Programme p;
    p.start = l.start;
    p.end = l.end;
    p.title = std::move(l.title);
    guide.push_back(std::move(p));
  }
  if (!past_ || pastFor_ != entry_) return guide;
  return replay::merged(std::move(guide), *past_);
}

}  // namespace marigold::engine
