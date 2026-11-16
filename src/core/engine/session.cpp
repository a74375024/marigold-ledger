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

// Wall clock in ms, for the log's times.
int64_t wallMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// The catch-up test: a picture's size waited for this long after it shows
// before its log line goes without one.
constexpr uint32_t kTestSizeWaitMs = 3000;

// "1920x1080" from the player's facts ("1920x1080 59.94p · h264 ..."); ""
// when they don't start with one.
std::string pictureSize(const std::string& facts) {
  std::string first = facts.substr(0, facts.find(' '));
  size_t x = first.find('x');
  if (x == std::string::npos || x == 0 || x + 1 >= first.size() || first.size() > 11) return {};
  for (size_t i = 0; i < first.size(); ++i)
    if (i != x && (first[i] < '0' || first[i] > '9')) return {};
  return first;
}

// "2.3 s".
std::string tenthsOfSeconds(uint32_t ms) {
  uint32_t tenths = (ms + 50) / 100;
  return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " s";
}

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
  size_t i = rec_.feedIndex();
  if (i < feeds_.size()) store_.noteEventFeed(entry_, feeds_[i].channelId, ready());
}

bool Session::begin(const std::string& entryId, std::vector<Feed> feeds, const std::vector<std::string>& flip) {
  if (feeds.empty()) {
    MLOG_W("play", "no feeds for this channel");
    return false;
  }
  // A catch-up test ends with the tune (it goes to live, as usual).
  if (test_) closeCatchup("another channel");
  // What is left becomes Right's target (before its source, flip list and
  // feeds are replaced). Another source or a camera of the same entry
  // isn't leaving it.
  bool other = entryId != entry_;
  if (other) leaving();
  if (!flip.empty()) flip_ = flip;
  rememberGame();
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
  // Every caller has already waited (OK, a settled flip, a preview's dwell,
  // a typed number): no second settle.
  playback::TuneOptions options;
  options.settle = 0;
  options.game = store_.isEvent(entryId);
  apply(rec_.tune(ms(), std::move(infos), options));
  return true;
}

bool Session::tune(const std::string& entryId, const std::vector<std::string>& flip) {
  // The channel under a catch-up test: back to it live.
  if (test_ && entryId == entry_) {
    if (!flip.empty()) flip_ = flip;
    endCatchupTest();
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
  if (entry_.empty() || feeds_.empty() || camera_ || test_) return false;
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
  if (test_) {
    // Out of the test first; another copy picked is a live pick like any.
    endCatchupTest();
    if (index != testLive_) quietReturn_ = false;
  }
  apply(rec_.pick(ms(), index));
  return true;
}

void Session::stop() {
  pausedAt_.reset();
  if (entry_.empty()) return;
  if (test_) closeCatchup("stopped");
  leaving();
  rememberGame();
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
        playFeed(a.feed, a.token);
        break;
      case playback::Action::Kind::CloseStream:
        store_.closeStream();
        // A late, superseded server open: what plays now keeps playing.
        if (a.token == 0) player_->streamClosed();
        break;
      case playback::Action::Kind::GiveUp:
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
          // Back to live after a catch-up test: the copy that played before
          // it showing its picture again is no new success (nothing charged,
          // nothing remembered). A failure there is live's own.
          bool quiet = quietReturn_ && a.worked;
          quietReturn_ = false;
          if (quiet) {
            problems_.erase(id);
            break;
          }
          store_.recordFeed(id, a.worked);
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
  // A catch-up test simply ends: live plays again on return.
  if (test_) closeCatchup("app in background");
  paused_ = true;
  if (entry_.empty()) return;
  Paused p;
  p.restart = !failed();
  p.ready = ready();
  p.game = game_;
  p.feed = feeds_.empty() ? 0 : std::min(rec_.feedIndex(), feeds_.size() - 1);
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
  if (test_) {
    l.test = 1 + static_cast<int>(test_->phase);
    l.testFeed = test_->feed;
  }
  return l;
}

bool Session::tick(uint32_t now) {
  if (now >= memoryAt_) {
    if (memoryAt_) logMemory();
    memoryAt_ = now + kMemoryEveryMs;
  }
  for (std::string& note : store_.takeEventsLog()) log_.add(wallMs(), "live events", 0, {}, {}, {}, note);
  saveLog(now);
  // Paused in the background: the recovery waits for the app's return.
  if (entry_.empty() || paused_) return false;
  if (test_) {
    // The catch-up test's own short path: the recovery stands aside.
    tickCatchup(now);
    return test_ && test_->phase == CatchupTest::Phase::Opening;
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

// ---------------------------------------------------------------- catch-up test

std::optional<size_t> Session::catchupCopy() const {
  if (entry_.empty() || paused_ || camera_ || feeds_.empty()) return std::nullopt;
  if (!test_ && (!rec_.sawFirstFrame() || failed())) return std::nullopt;
  size_t live = test_ ? testLive_ : rec_.feedIndex();
  if (live < feeds_.size() && store_.catchupDays(feeds_[live].channelId) > 0) return live;
  for (size_t i = 0; i < feeds_.size(); ++i) {
    const std::string& id = feeds_[i].channelId;
    if (store_.catchupDays(id) == 0) continue;
    // A guide channel's own versions (not a game's backup on another channel).
    if (!game_ && store_.entryOfChannel(id) != entry_) continue;
    return i;
  }
  return std::nullopt;
}

uint16_t Session::catchupDays() const {
  std::optional<size_t> i = catchupCopy();
  return i ? store_.catchupDays(feeds_[*i].channelId) : 0;
}

bool Session::startCatchupTest(CatchupMoment moment, time::Seconds from) {
  // Which copy replays: worked out before a test on now closes.
  std::optional<size_t> copy = catchupCopy();
  uint16_t days = copy ? store_.catchupDays(feeds_[*copy].channelId) : 0;
  if (!days) return false;
  time::Seconds now = time::now();
  from -= ((from % 60) + 60) % 60;  // the provider takes minutes
  if (from >= now || now - from > (static_cast<time::Seconds>(days) + 1) * 86400) return false;
  if (test_) {
    // Tested again: the test on now ends (its line is written); the
    // recovery is still aside.
    closeCatchup("tested again");
  } else {
    testLive_ = rec_.feedIndex();
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
  }
  quietReturn_ = false;
  testTried_.clear();
  test_ = CatchupTest{};
  test_->moment = moment;
  test_->from = from;
  playCatchup(*copy);
  return true;
}

void Session::playCatchup(size_t index) {
  CatchupTest& t = *test_;
  index = std::min(index, feeds_.size() - 1);
  time::Seconds now = time::now();
  t.phase = CatchupTest::Phase::Opening;
  t.feed = index;
  t.days = store_.catchupDays(feeds_[index].channelId);
  t.behind = now - t.from;
  t.startedAt = ms();
  t.pictureMs = 0;
  t.timedOut = false;
  t.error.clear();
  t.size.clear();
  testLogged_ = false;
  testTried_.insert(index);
  // The link carries the login: to the player only, never the log.
  std::optional<Store::CatchupFeed> f = store_.catchupFeed(feeds_[index], t.from, now);
  testOffset_ = f ? f->serverOffset : std::nullopt;
  if (hooks_.changed) hooks_.changed();
  if (!f) {
    failCatchup(false, "no catch-up link could be made for this copy");
    return;
  }
  MLOG_I("play", "catch-up test: feed %zu of %zu, %lld min back, %d min asked", index + 1, feeds_.size(),
         static_cast<long long>(t.behind / 60), f->minutes);
  player_->attempt();
  player_->play(f->feed, false);
}

void Session::tickCatchup(uint32_t now) {
  CatchupTest& t = *test_;
  PlayerState s = player_->state();
  // What the relay notes of its own belongs to live playback: read and left.
  (void)player_->note();
  (void)player_->sourceClosed();
  if (t.phase == CatchupTest::Phase::Opening) {
    bool picture = s.picture || (s.playing && s.streamChecked && s.stream == media::Verdict::Picture);
    if (picture) {
      t.phase = CatchupTest::Phase::Worked;
      t.pictureMs = now - t.startedAt;
      if (hooks_.picture) hooks_.picture();
      if (hooks_.changed) hooks_.changed();
    } else if (s.failed || s.ended) {
      std::string error = s.failed ? playback::safeMessage(s.error) : std::string("the stream ended at once");
      failCatchup(false, error.empty() ? std::string("the player reported an error") : error);
      return;
    } else if (now - t.startedAt >= kCatchupTimeoutMs) {
      failCatchup(true, {});
      return;
    }
  }
  if (t.phase == CatchupTest::Phase::Worked && !testLogged_) {
    if (t.size.empty()) t.size = pictureSize(s.facts);
    if (!t.size.empty() || now - t.startedAt - t.pictureMs >= kTestSizeWaitMs) logCatchup();
  }
}

void Session::failCatchup(bool timedOut, std::string error) {
  CatchupTest& t = *test_;
  t.phase = CatchupTest::Phase::Failed;
  t.timedOut = timedOut;
  t.error = std::move(error);
  // Only said: no reconnect, no other copy unless the viewer asks.
  player_->stop();
  logCatchup();
  if (hooks_.changed) hooks_.changed();
}

void Session::logCatchup(std::string_view ended) {
  if (!test_ || testLogged_) return;
  testLogged_ = true;
  const CatchupTest& t = *test_;
  std::string detail;
  switch (t.moment) {
    case CatchupMoment::TenMinutesAgo: detail = "10 min ago"; break;
    case CatchupMoment::ProgrammeStart: {
      time::Zone zone = host_.zone ? host_.zone() : time::Zone::device();
      time::Civil at = zone.toLocal(t.from);
      char hm[8];
      std::snprintf(hm, sizeof hm, "%02d:%02d", at.hour, at.minute);
      detail = std::string("start of programme ") + hm;
      break;
    }
    case CatchupMoment::Yesterday: detail = "yesterday, same time"; break;
  }
  detail += " (archive says " + std::to_string(t.days) + (t.days == 1 ? " day): " : " days): ");
  if (!ended.empty()) {
    detail += "ended before a result (" + std::string(ended) + ")";
  } else if (t.phase == CatchupTest::Phase::Worked) {
    detail += "worked, picture in " + tenthsOfSeconds(t.pictureMs) + (t.size.empty() ? "" : ", " + t.size);
  } else {
    detail += "failed, " + (t.timedOut ? "no picture after " + std::to_string(kCatchupTimeoutMs / 1000) + " s" : t.error);
  }
  detail += "; server clock " + (testOffset_ ? sources::xtream::offsetText(*testOffset_) : "not known, asked as UTC");
  std::string channel, feed, provider;
  if (const GuideChannel* g = store_.channels()->find(entry_)) channel = g->name;
  else if (const EventTile* e = store_.eventTile(entry_)) channel = e->heroTitle.empty() ? e->title : e->heroTitle;
  if (t.feed < feeds_.size()) {
    feed = feeds_[t.feed].label;
    provider = store_.providerName(feeds_[t.feed].channelId);
  }
  detail = playback::safeMessage(detail);
  MLOG_I("play", "catch-up test: %s", detail.c_str());
  log_.add(wallMs(), "catch-up test", session_, channel, feed, provider, detail);
}

void Session::closeCatchup(std::string_view why) {
  if (!test_) return;
  // Every test has its line: one still opening ended before a result.
  logCatchup(test_->phase == CatchupTest::Phase::Opening ? why : std::string_view());
  test_.reset();
  testTried_.clear();
  testOffset_.reset();
  if (hooks_.changed) hooks_.changed();
}

std::optional<size_t> Session::catchupOtherCopy() const {
  if (!test_ || feeds_.empty()) return std::nullopt;
  for (size_t k = 1; k < feeds_.size(); ++k) {
    size_t i = (test_->feed + k) % feeds_.size();
    if (testTried_.count(i)) continue;
    const std::string& id = feeds_[i].channelId;
    if (store_.catchupDays(id) == 0) continue;
    // A guide channel's own versions (not a game's backup on another channel).
    if (!game_ && store_.entryOfChannel(id) != entry_) continue;
    return i;
  }
  return std::nullopt;
}

bool Session::catchupTryOtherCopy() {
  std::optional<size_t> next = catchupOtherCopy();
  if (!next) return false;
  logCatchup("another copy");
  playCatchup(*next);
  return true;
}

void Session::endCatchupTest() {
  if (!test_) return;
  closeCatchup("back to live");
  if (entry_.empty() || feeds_.empty()) return;
  // The copy before the test again, as a fresh start (its backups after
  // it), like a return from the background.
  hadPicture_ = false;
  failMessage_.clear();
  noPicture_ = false;
  player_->beginTune();
  tunedAt_ = ms();
  playback::TuneOptions options;
  options.start = std::min(testLive_, feeds_.size() - 1);
  options.settle = 0;
  options.game = game_;
  apply(rec_.tune(ms(), feedInfos(entry_), options));
  quietReturn_ = true;
  if (hooks_.changed) hooks_.changed();
}

}  // namespace marigold::engine
