#include "core/playback/recovery.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "core/util/redact.hpp"
#include "core/util/rx.hpp"

namespace marigold::playback {
namespace {

// Xtream links are cheap to reopen and usually come straight back. M3U
// links are direct links too (often an Xtream panel behind a playlist), so
// they get the Xtream treatment; the Swift only knows Xtream and Emby.
bool isDirect(FeedKind kind) { return kind != FeedKind::Emby; }

std::string seconds(Ms ms) { return std::to_string(ms / 1000) + "s"; }

std::string oneDecimal(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f", v);
  return buf;
}

std::string capitalized(std::string text) {
  if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') text[0] = static_cast<char>(text[0] - 'a' + 'A');
  return text;
}

std::string versionOf(size_t index, size_t count) {
  return std::to_string(index + 1) + " of " + std::to_string(count);
}

// "2 s", or "0.4 s" under a second.
std::string lastedText(Ms ms) {
  return ms < 1000 ? oneDecimal(static_cast<double>(ms) / 1000.0) + " s" : std::to_string(ms / 1000) + " s";
}

std::string kilobytes(int64_t bytes) {
  return std::to_string(std::max<int64_t>(bytes > 0 ? 1 : 0, bytes / 1024)) + " KB";
}

// What the picture check found, for the log: "black 9 s; I 432 B, 16 kbps",
// "no video track (PMT audio 1)".
std::string checkText(media::Verdict v, Ms held, std::string_view detail) {
  if (v == media::Verdict::NoVideo)
    return std::string("no video track") + (detail.empty() ? std::string() : " (" + std::string(detail) + ")");
  std::string text = std::string(media::verdictName(v)) + " " + std::to_string(held / 1000) + " s";
  if (!detail.empty()) text += "; " + std::string(detail);
  return text;
}

const char kDefaultOpenError[] = "This channel couldn't be opened right now.";

}  // namespace

Ms startupTimeout(FeedKind kind) { return kind == FeedKind::Xtream ? kXtreamStartupTimeoutMs : kStartupTimeoutMs; }

bool cannotPlayHere(std::string_view message) {
  return rx::test("resource allocation error", std::string(message), true);
}

std::string safeMessage(std::string_view text) {
  // Never show or keep a stream link (Xtream links carry the password).
  std::string out = rx::replace(text, "[a-z][a-z0-9+.-]*://\\S+", "the stream", true);
  return redact(out);
}

// --- inputs ------------------------------------------------------------------

Actions Recovery::tune(Ms now, std::vector<FeedInfo> feeds, TuneOptions options) {
  Actions out;
  feeds_ = std::move(feeds);
  surf_ = options.surf;
  game_ = options.game;
  playerOnScreen_ = false;
  if (feeds_.empty()) {
    // Swift: the open loop never runs and gives up with its default error.
    clearTimers();
    ++token_;
    setNote({}, out);
    phase_ = Phase::Waiting;
    giveUp(kDefaultOpenError, out);
    return out;
  }
  size_t start = options.start < feeds_.size() ? options.start : 0;
  startTune(now, start, 0, false, false, true, {}, out, options.settle);
  return out;
}

Actions Recovery::pick(Ms now, size_t feed) {
  Actions out;
  if (phase_ == Phase::Idle || feed >= feeds_.size()) return out;
  if (feed == index_ && sawFirstFrame_ && phase_ == Phase::Playing) return out;
  const FeedInfo& f = feeds_[feed];
  manualFeedId_ = f.id;
  reconnectsOnFeed_ = 0;
  startupRetries_[f.id] = 0;
  pictureReconnects_.erase(f.id);
  announceBackup_ = false;
  setNote("Switching…", out);
  log(out, "you picked", "version " + versionOf(feed, feeds_.size()), feed);
  // A pick starts a new round through the feeds from the one picked.
  startTune(now, feed, 0, true, false, true, "picked in Sources", out);
  return out;
}

Actions Recovery::stop(Ms now) {
  Actions out;
  if (phase_ == Phase::Idle) return out;
  if (active()) {
    log(out, "stop", firstFrameAt_ ? "watched " + seconds(now - *firstFrameAt_) : "before a picture");
  }
  ++token_;
  clearTimers();
  pending_.reset();
  setNote({}, out);
  phase_ = Phase::Idle;
  playerOnScreen_ = false;
  sawFirstFrame_ = false;
  listening_ = false;
  feeds_.clear();
  out.push_back({Action::Kind::CloseStream, Action::npos, 0, false, {}, {}});
  return out;
}

Actions Recovery::streamHanded(Ms now, uint64_t token) {
  Actions out;
  if (token != 0 && token == inFlight_) inFlight_ = 0;
  if (!isCurrent(token)) {
    // Superseded while the server was opening it: close it, never play it.
    if (token != 0) out.push_back({Action::Kind::CloseStream, Action::npos, token, false, {}, {}});
    return out;
  }
  bool replacing = playerOnScreen_;
  phase_ = Phase::Playing;
  playerOnScreen_ = true;
  handedAt_ = now;
  reportsLoading_ = loaded_ = false;
  havePosition_ = false;
  moved_ = false;
  position_ = 0;
  movedAt_ = now;
  bufferEnd_ = 0;
  bufferEndAt_ = now;
  buffering_ = false;
  verdict_ = media::Verdict::Unknown;
  verdictSince_ = now;
  realPicture_ = staticNoted_ = listening_ = keptClosing_ = false;
  closed_.clear();
  log(out, replacing ? "swapped in player" : "play", "version " + versionOf(index_, feeds_.size()), index_);
  return out;
}

Actions Recovery::openFailed(Ms now, uint64_t token, std::string_view reason) {
  Actions out;
  if (token != 0 && token == inFlight_) inFlight_ = 0;
  if (!isCurrent(token)) return out;  // nothing was opened
  std::string text = safeMessage(reason);
  lastOpenError_ = text.empty() ? kDefaultOpenError : text;
  out.push_back({Action::Kind::RecordFeed, index_, 0, false, {}, {}});
  log(out, "open failed", lastOpenError_, index_);
  // Swift: the next version straight away, without counting a recovery.
  if (triedCount_ < feeds_.size()) {
    ++token_;  // a late duplicate report of the failed open must not count
    startNow(now, (index_ + 1) % feeds_.size(), false, out);
    return out;
  }
  std::string message = feeds_.size() > 1
      ? (surf_ ? std::string("None of the next feeds are live right now.")
               : "None of this channel's " + std::to_string(feeds_.size()) +
                     " versions could be opened right now. " + lastOpenError_)
      : lastOpenError_;
  giveUp(message, out);
  return out;
}

Actions Recovery::firstFrame(Ms now) {
  Actions out;
  if (playing() && !sawFirstFrame_) playerStarted(now, out);
  return out;
}

Actions Recovery::position(Ms now, double positionS, double bufferEndS) {
  Actions out;
  if (!playing()) return out;
  if (paused_) {
    // Pause by the viewer: nothing counts, the clocks start again.
    position_ = positionS;
    havePosition_ = true;
    movedAt_ = now;
    bufferEndAt_ = now;
    return out;
  }
  // livestall.h: the buffer end grew (or the buffer started over).
  if (bufferEndS > 0.5 &&
      (bufferEndS > bufferEnd_ + kPositionStepS || bufferEndS < bufferEnd_ - 1.0 || bufferEnd_ == 0)) {
    bufferEnd_ = bufferEndS;
    bufferEndAt_ = now;
  }
  if (!havePosition_) {
    // The first sample is the reference, not motion.
    havePosition_ = true;
    position_ = positionS;
    return out;
  }
  // A sliding live window moves the origin back: rebase, but only forward
  // motion counts as new video.
  if (positionS < position_ - kPositionStepS) {
    position_ = positionS;
    return out;
  }
  if (positionS <= position_ + kPositionStepS) return out;
  position_ = positionS;
  movedAt_ = now;
  moved_ = true;
  // Playing is a picture even if the first-frame signal was missed.
  if (!sawFirstFrame_) playerStarted(now, out);
  if (trouble_) recoveredByItself(now, out);
  return out;
}

void Recovery::loading(bool loaded) {
  if (!playing() || sawFirstFrame_) return;
  reportsLoading_ = true;
  loaded_ = loaded_ || loaded;
}

Actions Recovery::bufferingStarted(Ms now) {
  Actions out;
  buffering_ = true;
  // Before the first picture this is just the start; startup has its own timer.
  if (!playing() || !sawFirstFrame_) return out;
  bool settling = firstFrameAt_ && now - *firstFrameAt_ < kStartupBufferingMs;
  beginTrouble(now, "stall", kStallTimeoutMs, out, settling);
  return out;
}

Actions Recovery::bufferingEnded(Ms now) {
  Actions out;
  buffering_ = false;
  // The Swift player reports "playing" again. Where the position is known,
  // only new video proves it (uMS can leave buffering on a frozen decoder).
  if (playing() && trouble_ && trouble_->cause == "stall" && !moved_) recoveredByItself(now, out);
  return out;
}

Actions Recovery::playerError(Ms now, std::string_view message) {
  Actions out;
  if (!playing()) return out;
  std::string text = safeMessage(message.empty() ? std::string_view("playback error") : message);
  log(out, "player error", text);
  if (sawFirstFrame_) {
    beginTrouble(now, "error: " + text, kErrorGraceMs, out);
  } else if (cannotPlayHere(text)) {
    // No grace and no retries: this version won't play on this TV.
    startupFailed(now, "this TV can't play this version (" + text + ")", out, true);
  } else if (isDirect(feeds_[index_].kind) && rx::test("network error", text, true)) {
    // Marigold: webOS "300 Network Error" before a picture doesn't clear by
    // itself (the playback log: every one played on the retry), so the
    // retry comes now rather than after the error grace.
    startupFailed(now, "error: " + text, out);
  } else if (feeds_[index_].retryOnce && startupRetries_[feeds_[index_].id] == 0) {
    // A quick start the server refused: straight to the normal open.
    startupFailed(now, "error: " + text, out);
  } else {
    scheduleStartupFailure(now, "error: " + text);
  }
  return out;
}

Actions Recovery::ended(Ms now) {
  Actions out;
  if (!playing()) return out;
  log(out, "stream ended");
  // The relay gave up on a source that kept closing: never a run that
  // played (and earns reconnects) unless one connection lasted a while.
  if (sawFirstFrame_ && !listening_ && closed_.size() >= static_cast<size_t>(kFlapConnections) &&
      std::none_of(closed_.begin(), closed_.end(), [](const Closed& c) { return c.lasted >= kPlayedRunMs; })) {
    log(out, "source keeps closing",
        std::to_string(closed_.size()) + " connections in " + lastedText(now - handedAt_) + "; the relay gave up");
    failedNoRetry(now, "the source keeps closing", "the source keeps closing", false, out);
    return out;
  }
  if (sawFirstFrame_) {
    // Marigold: the playback log (HLN) shows a live stream that "ends" never
    // plays on by itself; waiting the grace only froze the picture longer.
    recover(now, "the stream ended", out);
  } else {
    scheduleStartupFailure(now, "ended before a picture");
  }
  return out;
}

void Recovery::restartedAnotherWay(Ms now) {
  if (!playing() || sawFirstFrame_) return;
  handedAt_ = now;
  reportsLoading_ = loaded_ = false;
  startupErrorAt_.reset();
  startupErrorReason_.clear();
}

Actions Recovery::setPaused(Ms now, bool paused) {
  Actions out;
  if (paused_ == paused) return out;
  paused_ = paused;
  if (!paused) {
    movedAt_ = now;
    bufferEndAt_ = now;
  }
  return out;
}

Actions Recovery::videoCheck(Ms now, media::Verdict v, std::string_view detail) {
  using media::Verdict;
  Actions out;
  if (!playing() || listening_ || !feeds_[index_].expectVideo) return out;
  // What is on screen can't be judged while it waits for data or is paused:
  // the count starts again after.
  bool shown = v == Verdict::Black || v == Verdict::Frozen || v == Verdict::Static || v == Verdict::NoFrame;
  bool suspended = shown && (paused_ || buffering_ || trouble_);
  if (v != verdict_ || suspended) {
    verdict_ = v;
    verdictSince_ = now;
  }
  // A picture that held a while: what goes wrong after it is mid-stream.
  if (v == Verdict::Picture && now - verdictSince_ >= kRealPictureMs) realPicture_ = true;
  if (v == Verdict::Picture || v == Verdict::Unknown || suspended) return out;
  Ms held = now - verdictSince_;
  std::string what = checkText(v, held, detail);
  switch (v) {
    case Verdict::Unsupported:
      if (held < kUnsupportedMs) break;
      log(out, "picture check", what);
      startupFailed(now, "this TV can't decode this version (" + std::string(detail) + ")", out, true);
      break;
    case Verdict::Scrambled:
      if (held < kScrambledMs) break;
      log(out, "picture check", what);
      failedNoRetry(now, "the stream is scrambled", "scrambled", false, out);
      break;
    case Verdict::NoVideo:
    case Verdict::TablesOnly:
      if (held < kNoVideoMs) break;
      log(out, "picture check", what);
      if (v == Verdict::NoVideo) failedNoRetry(now, "the stream has no video", "no video", true, out);
      else failedNoRetry(now, "the stream carries no sound or video", "no sound or video", false, out);
      break;
    case Verdict::NoFrame:
      // Once a frame was drawn, a stuck picture is the stall watchdog's.
      if (sawFirstFrame_ || held < kNoFrameMs) break;
      log(out, "picture check", what);
      startupFailed(now, "the sound plays but no frame was drawn", out, false, kNoFrameRetries, true, "no picture");
      break;
    case Verdict::Black:
    case Verdict::Frozen:
      if (held < (realPicture_ ? kBlackMidStreamMs : kBlackAtStartMs)) break;
      log(out, "picture check", what);
      pictureFailed(now, v, out);
      break;
    case Verdict::Static:
      if (held < kStaticMs || staticNoted_) break;
      staticNoted_ = true;
      log(out, "picture check", what);
      // A game's slate or frozen source: another source may have the game.
      // A channel's picture that doesn't change is only noted.
      if (game_ && !surf_ && triedCount_ < feeds_.size())
        moveToNext(now, "the picture hasn’t changed in " + lastedText(held), out, true);
      break;
    case Verdict::Picture:
    case Verdict::Unknown:
      break;
  }
  return out;
}

Actions Recovery::sourceClosed(Ms now, Ms lasted, int64_t bytes, bool reopened) {
  Actions out;
  if (!playing()) return out;
  closed_.push_back({now, lasted, bytes});
  std::string detail = "closed after " + lastedText(lasted) + ", " + kilobytes(bytes);
  if (reopened) {
    size_t n = closed_.size();
    detail += "; reopened it (" + std::to_string(n) + (n == 1 ? " time)" : " times)");
  } else {
    detail += "; the relay gave up on it";
  }
  log(out, "relay", detail);
  if (listening_ || keptClosing_ || !reopened || !keepsClosing(now)) return out;
  keptClosing_ = true;
  log(out, "source keeps closing",
      std::to_string(closed_.size() + 1) + " connections in " + lastedText(now - handedAt_));
  failedNoRetry(now, "the source keeps closing", "the source keeps closing", false, out);
  return out;
}

// Short connections with data, closed by 15 s after the picture (or before
// it), and not one that lasted.
bool Recovery::keepsClosing(Ms now) const {
  Ms horizon = (firstFrameAt_ ? *firstFrameAt_ : now) + kFlapWindowMs;
  int shortOnes = 0;
  for (const Closed& c : closed_) {
    if (c.lasted >= kLongConnectionMs) return false;
    if (c.bytes > 0 && c.lasted < kShortConnectionMs && c.at <= horizon) ++shortOnes;
  }
  return shortOnes >= kFlapConnections;
}

Actions Recovery::tick(Ms now) {
  Actions out;
  // Server opens run one after another; a direct feed never waits for one.
  if (phase_ == Phase::Waiting && pending_ && now >= pending_->due &&
      (inFlight_ == 0 || (pending_->feed < feeds_.size() && feeds_[pending_->feed].kind != FeedKind::Emby))) {
    Pending p = *pending_;
    pending_.reset();
    startNow(now, p.feed, p.reconnect, out);
    return out;
  }
  if (!playing()) return out;
  if (!sawFirstFrame_) {
    // armFirstFrameWatchdog and scheduleStartupFailure: whichever is due first.
    Ms timeout = startupTimeout(feeds_[index_].kind);
    // Nothing at all came yet on a direct feed: no point waiting it out.
    if (isDirect(feeds_[index_].kind) && reportsLoading_ && !loaded_ && !startupErrorAt_ &&
        now >= handedAt_ + kNoDataMs) {
      startupFailed(now, "no data after " + seconds(kNoDataMs), out);
      return out;
    }
    Ms watchdog = handedAt_ + timeout;
    if (startupErrorAt_ && now >= *startupErrorAt_ && *startupErrorAt_ <= watchdog) {
      std::string reason = startupErrorReason_;
      startupFailed(now, reason, out);
    } else if (now >= watchdog) {
      startupFailed(now, "no picture after " + seconds(timeout), out);
    }
    return out;
  }
  if (!trouble_ && moved_ && !paused_) {
    // livestall.h, dry source: the picture still runs, but the buffer end
    // has not grown for a while and little is left. Reconnect now instead of
    // waiting for a frozen picture.
    if (bufferEnd_ > 0.5 && position_ <= bufferEnd_ && bufferEnd_ - position_ <= kDryReserveS &&
        now - bufferEndAt_ >= kDrySourceMs) {
      recover(now, "the source stopped sending", out);
      return out;
    }
    // livestall.h, frozen picture: no new video. Counted from the last new
    // frame, so a stall recovers kStallTimeoutMs after the picture stopped.
    if (now - movedAt_ >= kStallNoteDelayMs) beginTrouble(movedAt_, "stall", kStallTimeoutMs, out);
  }
  if (trouble_) {
    if (!trouble_->noted && now >= trouble_->noteAt) {
      trouble_->noted = true;
      // Start-up buffering that lasts this long is a stall after all.
      if (trouble_->quiet) log(out, "stall", "at the start");
      setNote("Buffering…", out);
    }
    if (now >= trouble_->deadline) {
      std::string reason = trouble_->cause + ", not recovered after " + seconds(trouble_->grace);
      recover(now, reason, out);
    }
  }
  return out;
}

// --- steps -------------------------------------------------------------------

void Recovery::startTune(Ms now, size_t feed, Ms delay, bool inPlace, bool reconnect, bool newRound,
                         std::string_view reason, Actions& out, Ms settle) {
  bool replacing = inPlace && playerOnScreen_;
  clearTimers();
  ++token_;
  if (!replacing) {
    // A new channel: a fresh recovery budget.
    manualFeedId_.reset();
    recoveries_.clear();
    reconnectsOnFeed_ = 0;
    startupRetries_.clear();
    pictureReconnects_.clear();
    setNote({}, out);
    std::string detail = std::to_string(feeds_.size()) + " version(s)";
    if (!reason.empty()) detail += "; " + std::string(reason);
    log(out, "tune", detail, feed);
  }
  if (newRound) {
    tried_.assign(feeds_.size(), false);
    triedCount_ = 0;
  }
  index_ = feed;
  sawFirstFrame_ = false;
  firstFrameAt_.reset();
  phase_ = Phase::Waiting;
  // Retries, reconnects and switches wait only their own pause.
  pending_ = Pending{now + settle + delay, feed, reconnect};
}

void Recovery::startNow(Ms /*now*/, size_t feed, bool reconnect, Actions& out) {
  index_ = feed;
  markTried(feed);
  phase_ = Phase::Opening;
  if (feeds_[feed].kind == FeedKind::Emby) inFlight_ = token_;
  // One stream at a time: close what is open before every open.
  out.push_back({Action::Kind::CloseStream, Action::npos, 0, false, {}, {}});
  out.push_back({reconnect ? Action::Kind::Reconnect : Action::Kind::Play, feed, token_, false, {}, {}});
}

void Recovery::playerStarted(Ms now, Actions& out) {
  sawFirstFrame_ = true;
  startupErrorAt_.reset();
  firstFrameAt_ = now;
  log(out, "picture", std::to_string(now - handedAt_) + " ms after the link");
  setNote({}, out);
  if (announceBackup_) {
    announceBackup_ = false;
    out.push_back({Action::Kind::Toast, Action::npos, 0, false,
                   "Playing a backup feed (" + versionOf(index_, feeds_.size()) + ")", {}});
  }
  out.push_back({Action::Kind::RecordFeed, index_, 0, true, {}, {}});
}

// Something is wrong with a playing stream (a stall, an error the engine
// reported, the stream "ending"). Engines often get over these by
// themselves, so wait `grace`; only if it has not started playing again by
// then does recovery step in.
void Recovery::beginTrouble(Ms since, std::string cause, Ms grace, Actions& out, bool quiet) {
  if (trouble_) {
    // Already waiting: keep the earlier deadline, note the latest cause.
    trouble_->cause = std::move(cause);
    return;
  }
  if (cause == "stall" && !quiet) log(out, "stall");
  Trouble t;
  t.quiet = quiet;
  t.since = since;
  t.grace = grace;
  t.noteAt = since + std::min(kStallNoteDelayMs, grace);
  t.deadline = since + grace;
  t.cause = std::move(cause);
  trouble_ = std::move(t);
}

void Recovery::recoveredByItself(Ms now, Actions& out) {
  if (!trouble_) return;
  std::string detail = oneDecimal(static_cast<double>(now - trouble_->since) / 1000.0) + "s after " + trouble_->cause;
  bool quiet = trouble_->quiet && !trouble_->noted;
  trouble_.reset();
  setNote({}, out);
  if (!quiet) log(out, "recovered by itself", detail);
}

// An error before the first picture: give the engine a moment too. Unlike
// the Swift (which restarts the 3 s each time), a repeated report keeps the
// first deadline, so a caller that reports a sticky error flag more than
// once cannot hold the failure off.
void Recovery::scheduleStartupFailure(Ms now, std::string reason) {
  if (!startupErrorAt_) startupErrorAt_ = now + kStartupErrorGraceMs;
  startupErrorReason_ = std::move(reason);
}

// A feed that never showed a picture.
void Recovery::startupFailed(Ms now, const std::string& reason, Actions& out, bool unplayable, int allowed,
                             bool picture, const std::string& why) {
  if (!playing()) return;
  startupErrorAt_.reset();
  const FeedInfo feed = feeds_[index_];
  log(out, "start failed", reason, index_);
  if (unplayable) {
    out.push_back({Action::Kind::RecordFeed, index_, 0, false, reason, {}, true});
    moveToNext(now, reason, out);
    return;
  }
  // Xtream: retry the same feed (twice) before any backup. A failed start is
  // often the provider's connection limit while the previous stream lets go,
  // or a slow first segment.
  int retries = startupRetries_[feed.id];
  bool more = (isDirect(feed.kind) && retries < kXtreamStartRetries) || (feed.retryOnce && retries < 1);
  if (allowed >= 0 && retries >= allowed) more = false;
  if (more) {
    startupRetries_[feed.id] = retries + 1;
    if (!allowRecovery(now)) {
      giveUp("This channel keeps failing to start. Pick another feed, or try again later.", out);
      return;
    }
    setNote("Retrying…", out);
    log(out, "retry",
        "same feed in " + seconds(isDirect(feed.kind) ? kXtreamRetryDelayMs : 0) + " (attempt " +
            std::to_string(retries + 1) + " of " +
            std::to_string(isDirect(feed.kind) ? kXtreamStartRetries : 1) + ")",
        index_);
    // A direct feed waits for the provider to let go of the last stream; a
    // quick start falls back to the normal open at once.
    startTune(now, index_, isDirect(feed.kind) ? kXtreamRetryDelayMs : 0, true, true, false, reason, out);
    return;
  }
  out.push_back({Action::Kind::RecordFeed, index_, 0, false, reason, why});
  moveToNext(now, reason, out, picture);
}

// A black or frozen picture: one reconnect (before a real picture only on a
// direct feed, which is cheap to reopen), then the next feed.
void Recovery::pictureFailed(Ms now, media::Verdict v, Actions& out) {
  const FeedInfo feed = feeds_[index_];
  bool direct = isDirect(feed.kind);
  std::string reason = v == media::Verdict::Black ? "the picture is black" : "the picture froze";
  int& done = pictureReconnects_[feed.id];
  if (done < 1 && (realPicture_ || direct)) {
    if (!allowRecovery(now)) {
      giveUp("This channel keeps failing. Pick another feed, or try again later.", out);
      return;
    }
    ++done;
    setNote("Reconnecting…", out);
    log(out, "reconnect", reason + "; once", index_);
    startTune(now, index_, direct ? kXtreamReconnectDelayMs : 0, true, true, false, reason, out);
    return;
  }
  out.push_back({Action::Kind::RecordFeed, index_, 0, false, reason,
                 v == media::Verdict::Black ? "black picture" : "frozen picture"});
  moveToNext(now, reason, out, true);
}

// A feed that can't work now however often it is tried (scrambled, no
// video, a source that keeps closing): the next one, no retries.
void Recovery::failedNoRetry(Ms now, const std::string& reason, const std::string& why, bool picture, Actions& out) {
  startupErrorAt_.reset();
  out.push_back({Action::Kind::RecordFeed, index_, 0, false, reason, why});
  moveToNext(now, reason, out, picture);
}

// Every feed was tried and none shows a picture, but this one's sound
// plays: it plays on, unchecked, and the viewer decides.
void Recovery::noPictureAnywhere(Ms now, Actions& out) {
  listening_ = true;
  trouble_.reset();
  startupErrorAt_.reset();
  if (!sawFirstFrame_) {
    // The sound is what plays now: the stall watchdog watches it.
    sawFirstFrame_ = true;
    firstFrameAt_ = now;
  }
  setNote({}, out);
  log(out, "no picture", "none of the feeds shows one; the sound plays on");
  out.push_back({Action::Kind::NoPicture, index_, 0, false,
                 surf_ || game_ ? "None of the sources shows a picture right now. The sound plays on."
                                : "None of this channel’s versions shows a picture right now. "
                                  "The sound plays on.",
                 {}});
}

// A playing stream that did not get over a stall / error by itself:
// reconnect the same feed (a few times), then move to the next one.
void Recovery::recover(Ms now, const std::string& reason, Actions& out) {
  if (!playing()) return;
  trouble_.reset();
  const FeedInfo feed = feeds_[index_];
  Ms played = firstFrameAt_ ? now - *firstFrameAt_ : 0;
  // A healthy run earns a fresh set of reconnects.
  if (played > kFreshRunMs) reconnectsOnFeed_ = 0;
  bool direct = isDirect(feed.kind);
  // A feed picked in the Sources panel gets more reconnects before anything
  // else is tried.
  int allowance = (direct ? 2 : 1) + (played > kLongRunMs ? 1 : 0) +
                  (manualFeedId_ && *manualFeedId_ == feed.id ? 2 : 0);
  if (reconnectsOnFeed_ < allowance) {
    if (!allowRecovery(now)) {
      giveUp("This channel keeps dropping. Pick another feed, or try again later.", out);
      return;
    }
    ++reconnectsOnFeed_;
    setNote("Reconnecting…", out);
    log(out, "reconnect",
        reason + "; attempt " + std::to_string(reconnectsOnFeed_) + " of " + std::to_string(allowance) + "; played " +
            seconds(played),
        index_);
    startTune(now, index_, direct ? kXtreamReconnectDelayMs : 0, true, true, false, reason, out);
  } else {
    out.push_back({Action::Kind::RecordFeed, index_, 0, false, {}, {}});
    moveToNext(now, reason, out);
  }
}

// The next feed, once round the list. The Swift never wraps (it starts at
// the first feed, so one round is the list); here a feed picked in the
// middle of the list is followed by the ones after it and then the ones
// before it, and the round ends when every feed has been tried once.
void Recovery::moveToNext(Ms now, const std::string& reason, Actions& out, bool picture) {
  size_t count = feeds_.size();
  if (triedCount_ >= count && picture && playing()) {
    noPictureAnywhere(now, out);
    return;
  }
  if (triedCount_ >= count) {
    giveUp(count > 1 ? "None of this channel's " + std::to_string(count) + " versions are working right now."
                     : "This channel stopped working. " + capitalized(reason) + ".",
           out);
    return;
  }
  if (!allowRecovery(now)) {
    giveUp("This channel keeps failing. Pick another feed, or try again later.", out);
    return;
  }
  size_t next = (index_ + 1) % count;
  reconnectsOnFeed_ = 0;
  announceBackup_ = !surf_;
  std::string note = surf_ ? "Skipping to the next feed…" : "Switching to a backup feed…";
  if (picture && !surf_) {
    note = game_ ? "No picture on this source — trying " + versionOf(next, count)
                 : "No picture on this version — trying another…";
  }
  if (manualFeedId_ && *manualFeedId_ == feeds_[index_].id) {
    manualFeedId_.reset();
    note = "The feed you picked stopped working. Trying another…";
  }
  setNote(std::move(note), out);
  log(out, "switch", "to version " + versionOf(next, count) + "; " + reason, next);
  startTune(now, next, 0, true, false, false, reason, out);
}

// Counts one automatic recovery; false once there have been too many.
bool Recovery::allowRecovery(Ms now) {
  recoveries_.erase(std::remove_if(recoveries_.begin(), recoveries_.end(),
                                   [now](Ms t) { return now - t >= kRecoveryWindowMs; }),
                    recoveries_.end());
  if (recoveries_.size() >= static_cast<size_t>(kMaxRecoveries)) return false;
  recoveries_.push_back(now);
  return true;
}

// Stops trying: the app closes the player and shows the "can't play" card.
void Recovery::giveUp(const std::string& message, Actions& out) {
  log(out, "gave up", message);
  ++token_;
  clearTimers();
  pending_.reset();
  setNote({}, out);
  phase_ = Phase::Failed;
  playerOnScreen_ = false;
  out.push_back({Action::Kind::GiveUp, Action::npos, 0, false, message, {}});
  out.push_back({Action::Kind::CloseStream, Action::npos, 0, false, {}, {}});
}

void Recovery::markTried(size_t feed) {
  if (feed < tried_.size() && !tried_[feed]) {
    tried_[feed] = true;
    ++triedCount_;
  }
}

void Recovery::clearTimers() {
  startupErrorAt_.reset();
  startupErrorReason_.clear();
  trouble_.reset();
  havePosition_ = false;
  moved_ = false;
}

void Recovery::setNote(std::string text, Actions& out) {
  if (text == note_) return;
  note_ = std::move(text);
  out.push_back({Action::Kind::Note, Action::npos, 0, false, note_, {}});
}

void Recovery::log(Actions& out, std::string event, std::string detail, size_t feed) const {
  if (feed == Action::npos && index_ < feeds_.size()) feed = index_;
  out.push_back({Action::Kind::Log, feed, 0, false, std::move(event), std::move(detail)});
}

}  // namespace marigold::playback
