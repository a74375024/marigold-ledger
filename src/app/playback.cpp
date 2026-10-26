#include "app/playback.hpp"

#include <algorithm>

#include "app/app.hpp"
#include "app/store/store.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

namespace marigold::app {
namespace {

playback::FeedKind kindOf(const Feed& f) {
  if (f.needsOpen) return playback::FeedKind::Emby;
  if (str::startsWith(f.channelId, "xt:")) return playback::FeedKind::Xtream;
  return playback::FeedKind::Playlist;
}

// HLS feeds go through the local TS proxy (Legacy #158: the TV's HLS engine
// stays mute on some live channels that play at once as continuous TS).
bool looksHls(const std::string& url) {
  std::string lower = str::lowerAscii(url);
  std::string path = lower.substr(0, lower.find('?'));
  return str::endsWith(path, ".m3u8") || str::endsWith(path, ".m3u");
}

}  // namespace

Playback::Playback(App& app) : app_(app) {
  if (!proxy_.start()) MLOG_W("play", "TS proxy unavailable; HLS feeds play directly");
}

void Playback::rememberGame() {
  // Leaving or switching a game: its source starts it next time (6 h).
  if (entry_.empty() || feeds_.empty() || !app_.store().isEvent(entry_)) return;
  size_t i = rec_.feedIndex();
  if (i < feeds_.size()) app_.store().noteEventFeed(entry_, feeds_[i].channelId, ready());
}

bool Playback::begin(const std::string& entryId, std::vector<Feed> feeds, const std::vector<std::string>& flip) {
  if (!flip.empty()) flip_ = flip;
  if (feeds.empty()) {
    MLOG_W("play", "no feeds for this channel");
    return false;
  }
  rememberGame();
  entry_ = entryId;
  feeds_ = std::move(feeds);
  proxyFailed_.clear();
  failMessage_.clear();
  tunedAt_ = app_.ms();
  if (full_) watched();
  std::vector<playback::FeedInfo> infos;
  for (const Feed& f : feeds_) infos.push_back({f.channelId, kindOf(f)});
  apply(rec_.tune(app_.ms(), std::move(infos)));
  return true;
}

bool Playback::tune(const std::string& entryId, const std::vector<std::string>& flip) {
  if (!flip.empty()) flip_ = flip;
  if (entryId == entry_ && !failed()) return true;
  return begin(entryId, app_.store().feeds(entryId), flip);
}

bool Playback::tuneEventVia(const std::string& eventId, const std::string& via, const std::vector<std::string>& flip) {
  return begin(eventId, app_.store().eventFeeds(eventId, via), flip);
}

bool Playback::useFeed(size_t index) {
  if (index >= feeds_.size()) return false;
  apply(rec_.pick(app_.ms(), index));
  return true;
}

void Playback::stop() {
  if (entry_.empty()) return;
  rememberGame();
  apply(rec_.stop(app_.ms()));
  app_.video().stop();
  app_.store().closeStream();
  proxy_.stop();
  entry_.clear();
  feeds_.clear();
}

void Playback::apply(const playback::Actions& actions) {
  for (const playback::Action& a : actions) {
    switch (a.kind) {
      case playback::Action::Kind::Play:
      case playback::Action::Kind::Reconnect:
        playFeed(a.feed, a.token);
        break;
      case playback::Action::Kind::CloseStream:
        app_.store().closeStream();
        proxy_.stop();
        break;
      case playback::Action::Kind::GiveUp:
        app_.video().stop();
        failMessage_ = a.text;
        MLOG_W("play", "gave up: %s", playback::safeMessage(a.text).c_str());
        break;
      case playback::Action::Kind::Note:
        app_.invalidate();
        break;
      case playback::Action::Kind::Toast:
        if (full_) app_.toast(a.text, true);
        break;
      case playback::Action::Kind::RecordFeed:
        if (a.feed < feeds_.size()) app_.store().recordFeed(feeds_[a.feed].channelId, a.worked);
        break;
      case playback::Action::Kind::Log:
        MLOG_I("play", "%s %s", a.text.c_str(), playback::safeMessage(a.detail).c_str());
        break;
    }
  }
}

void Playback::playFeed(size_t index, uint64_t token) {
  if (index >= feeds_.size()) return;
  sawPicture_ = sawBuffering_ = sawFailed_ = sawEnded_ = false;
  viaProxy_ = false;
  playingFeed_ = index;
  const Feed& f = feeds_[index];
  MLOG_I("play", "feed %zu of %zu (%s)", index + 1, feeds_.size(), f.label.c_str());
  app_.video().setWindow(window());
  if (f.needsOpen) {
    // Emby / Jellyfin open the stream on the server first. Every open
    // reports back exactly once; a superseded one is closed again.
    app_.store().open(f, [this, token, index](std::optional<Feed> opened) {
      uint32_t now = app_.ms();
      if (!opened) {
        apply(rec_.openFailed(now, token, "The server couldn't open this channel."));
        return;
      }
      if (rec_.isCurrent(token) && index < feeds_.size()) {
        feeds_[index] = *opened;
        app_.video().play(opened->url, opened->headers, opened->loadMode);
      }
      apply(rec_.streamHanded(now, token));
    });
    return;
  }
  std::string url = f.url, headers = f.headers;
  if (looksHls(url) && !proxyFailed_.count(f.channelId)) {
    std::string local = proxy_.localUrl(url, headers);
    if (!local.empty()) {
      url = local;
      headers.clear();  // the proxy sends them to the source
      viaProxy_ = true;
    }
  }
  app_.video().play(url, headers, f.loadMode);
  apply(rec_.streamHanded(app_.ms(), token));
}

// The proxy's attempt failed before any picture: play the same feed once
// directly, unless the proxy only redirected to it (that was the direct try).
bool Playback::retryDirect() {
  if (!viaProxy_ || playingFeed_ >= feeds_.size()) return false;
  viaProxy_ = false;
  bool redirected = proxy_.redirected();
  const Feed& f = feeds_[playingFeed_];
  proxyFailed_.insert(f.channelId);
  proxy_.stop();
  if (redirected) return false;
  MLOG_I("play", "proxy didn't start this feed; playing it directly");
  sawFailed_ = sawEnded_ = false;
  app_.video().play(f.url, f.headers, f.loadMode);
  return true;
}

void Playback::watched() {
  if (entry_.empty() || entry_ == lastWatched_) return;
  if (!lastWatched_.empty()) previous_ = lastWatched_;
  lastWatched_ = entry_;
  app_.store().noteWatched(entry_);
}

std::string Playback::flip(int step) {
  if (flip_.empty()) return {};
  auto it = std::find(flip_.begin(), flip_.end(), entry_);
  long n = static_cast<long>(flip_.size());
  long i = it == flip_.end() ? 0 : (it - flip_.begin());
  i = ((i + step) % n + n) % n;
  std::string next = flip_[static_cast<size_t>(i)];
  tune(next);
  return next;
}

std::string Playback::previous() {
  if (previous_.empty()) return {};
  std::string target = previous_;
  tune(target);
  return target;
}

gfx::Rect Playback::window() const {
  return full_ ? gfx::Rect{0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight} : corner_;
}

void Playback::scheduleReapply() {
  // A change made while the TV was still attaching the picture can be lost:
  // apply it again a little later, and once more when the picture is up.
  uint32_t now = app_.ms();
  reapplyAt_[0] = now + 300;
  reapplyAt_[1] = now + 1200;
  reapplyAt_[2] = now + 3000;
  reapplyOnReady_ = true;
}

void Playback::setFull() {
  bool changed = !full_;
  full_ = true;
  watched();
  app_.video().setWindow(window());
  if (changed) scheduleReapply();
}

void Playback::setCorner(gfx::Rect r) {
  bool changed = full_ || r.x != corner_.x || r.y != corner_.y || r.w != corner_.w || r.h != corner_.h;
  full_ = false;
  corner_ = r;
  if (changed) {
    app_.video().setWindow(r);
    if (active()) scheduleReapply();
  }
}

bool Playback::tick(uint32_t ms) {
  if (entry_.empty()) return false;
  platform::VideoState s = app_.video().state();
  // Report changes of the player's flags only.
  bool picture = s.playing || s.decoderSeen;
  if (picture && !sawPicture_) {
    sawPicture_ = true;
    apply(rec_.firstFrame(ms));
    if (reapplyOnReady_) {
      reapplyOnReady_ = false;
      app_.video().reapplyWindow(window());
    }
  }
  bool buffering = s.bufferingMs > 0;
  if (buffering != sawBuffering_) {
    sawBuffering_ = buffering;
    apply(buffering ? rec_.bufferingStarted(ms) : rec_.bufferingEnded(ms));
  }
  if ((s.failed || s.ended) && !sawPicture_ && retryDirect()) return true;
  if (s.failed && !sawFailed_) {
    sawFailed_ = true;
    apply(rec_.playerError(ms, s.error));
  }
  if (s.ended && !sawEnded_) {
    sawEnded_ = true;
    apply(rec_.ended(ms));
  }
  if (sawPicture_) apply(rec_.position(ms, s.position, s.bufferEnd));
  apply(rec_.tick(ms));
  for (uint32_t& at : reapplyAt_) {
    if (at && ms >= at) {
      at = 0;
      app_.video().reapplyWindow(window());
    }
  }
  bool reapplying = reapplyAt_[0] || reapplyAt_[1] || reapplyAt_[2];
  return (!ready() && !failed()) || reapplying || !note().empty();
}

}  // namespace marigold::app
