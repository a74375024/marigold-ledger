#include "core/engine/session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>

#include "core/engine/store.hpp"
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

constexpr const char* kLogFile = "playback-log.json";
constexpr uint32_t kLogSaveMs = 10000;
constexpr uint32_t kMemoryEveryMs = 5 * 60 * 1000;
// Watched this long, a channel becomes the one "Back to" goes to.
constexpr uint32_t kStayedMs = 10000;

// Wall clock in ms, for the log's times.
int64_t wallMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

Session::Session(Store& store, Host host, std::unique_ptr<Player> player, Hooks hooks)
    : store_(store), host_(std::move(host)), player_(std::move(player)), hooks_(std::move(hooks)) {
  if (auto text = host_.files.read(kLogFile)) log_.restore(*text);
  logSaved_ = log_.revision();
}

void Session::logEvent(std::string_view event, std::string_view detail) {
  std::string channel, feed, provider;
  if (const GuideChannel* g = store_.channels()->find(entry_)) channel = g->name;
  else if (const EventTile* t = store_.eventTile(entry_)) channel = t->heroTitle.empty() ? t->title : t->heroTitle;
  size_t i = rec_.feedIndex();
  if (i < feeds_.size()) {
    feed = feeds_[i].label;
    provider = store_.providerName(feeds_[i].channelId);
  }
  log_.add(wallMs(), event, session_, channel, feed, provider, detail);
}

void Session::logMemory(std::string_view what) {
  std::string reading = memory::describe(memory::read());
  if (reading.empty()) return;
  std::string detail = what.empty() ? reading : std::string(what) + ": " + reading;
  log_.add(wallMs(), "memory", 0, {}, {}, {}, detail);
}

void Session::saveLog(uint32_t now) {
  if (log_.revision() == logSaved_ || now < logSaveAt_) return;
  logSaved_ = log_.revision();
  logSaveAt_ = now + kLogSaveMs;
  std::string text = log_.toJson();
  std::string path = host_.files.path(kLogFile);
  host_.pool.post(
      [path, text] {
        std::string tmp = path + ".tmp";
        std::FILE* f = std::fopen(tmp.c_str(), "wb");
        if (!f) return;
        bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        ok = std::fclose(f) == 0 && ok;
        if (ok) std::rename(tmp.c_str(), path.c_str());
      },
      nullptr, http::Priority::Background);
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
  bool ok = begin(entry, std::move(feeds), {});
  camera_ = ok;
  if (ok) logEvent("camera");
  return ok;
}

void Session::rememberGame() {
  // Leaving or switching a game: its source starts it next time (6 h).
  if (entry_.empty() || feeds_.empty() || camera_ || !store_.isEvent(entry_)) return;
  size_t i = rec_.feedIndex();
  if (i < feeds_.size()) store_.noteEventFeed(entry_, feeds_[i].channelId, ready());
}

bool Session::begin(const std::string& entryId, std::vector<Feed> feeds, const std::vector<std::string>& flip) {
  if (!flip.empty()) flip_ = flip;
  if (feeds.empty()) {
    MLOG_W("play", "no feeds for this channel");
    return false;
  }
  rememberGame();
  logMemory("before tuning");
  camera_ = false;
  hadPicture_ = false;
  backup_.reset();
  entry_ = entryId;
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
  logEvent("tune", std::to_string(feeds_.size()) + (feeds_.size() == 1 ? " feed" : " feeds"));
  // Every caller has already waited (OK, a settled flip, a preview's dwell,
  // a typed number): no second settle.
  playback::TuneOptions options;
  options.settle = 0;
  options.game = store_.isEvent(entryId);
  apply(rec_.tune(ms(), std::move(infos), options));
  return true;
}

bool Session::tune(const std::string& entryId, const std::vector<std::string>& flip) {
  if (!flip.empty()) flip_ = flip;
  if (entryId == entry_ && !failed()) return true;
  return begin(entryId, store_.feeds(entryId), flip);
}

bool Session::tuneEventVia(const std::string& eventId, const std::string& via, const std::vector<std::string>& flip) {
  return begin(eventId, store_.eventFeeds(eventId, via), flip);
}

bool Session::restart(std::string_view why) {
  if (entry_.empty() || feeds_.empty() || camera_) return false;
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
  apply(rec_.pick(ms(), index));
  return true;
}

void Session::stop() {
  if (entry_.empty()) return;
  rememberGame();
  apply(rec_.stop(ms()));
  player_->stop();
  store_.closeStream();
  entry_.clear();
  feeds_.clear();
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
        logEvent("gave up", a.text);
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
          store_.recordFeed(id, a.worked);
          if (a.worked) {
            store_.rememberFeed(entry_, id);
            problems_.erase(id);
          } else {
            lastFailed_ = a.feed;
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
  if (entry_.empty() || entry_ == lastWatched_) return;
  // The last channel: the one stayed on for a while, so a run of quick
  // flips doesn't lose the game you started from. Jumping back and forth
  // with previous() always swaps the two.
  if (!lastWatched_.empty() && (bouncing_ || previous_.empty() || ms() - watchedAt_ >= kStayedMs))
    previous_ = lastWatched_;
  bouncing_ = false;
  lastWatched_ = entry_;
  watchedAt_ = ms();
  store_.noteWatched(entry_);
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
  if (previous_.empty()) return {};
  std::string target = previous_;
  bouncing_ = true;
  tune(target);
  return target;
}

void Session::setShowingFull(bool full) {
  full_ = full;
  if (full) watched();
}

bool Session::tick(uint32_t now) {
  if (now >= memoryAt_) {
    if (memoryAt_) logMemory();
    memoryAt_ = now + kMemoryEveryMs;
  }
  for (std::string& note : store_.takeEventsLog()) log_.add(wallMs(), "live events", 0, {}, {}, {}, note);
  saveLog(now);
  if (entry_.empty()) return false;
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
  }
  // Sound only (no feed showed a picture): its clock is what plays.
  if (sawPicture_ || rec_.listening()) apply(rec_.position(now, s.position, s.bufferEnd));
  else if (s.reportsLoading) rec_.loading(s.loaded);
  apply(rec_.tick(now));
  return (!ready() && !failed()) || !note().empty();
}

}  // namespace marigold::engine
