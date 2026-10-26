#include "app/playback.hpp"

#include <algorithm>

#include "app/app.hpp"
#include "app/store/store.hpp"
#include "core/util/log.hpp"

namespace marigold::app {
namespace {
constexpr uint32_t kFirstFrameMs = 20000;
}

bool Playback::tune(const std::string& entryId, const std::vector<std::string>& flip) {
  if (!flip.empty()) flip_ = flip;
  if (entryId == entry_ && !failed_) return true;
  std::vector<Feed> feeds = app_.store().feeds(entryId);
  if (feeds.empty()) {
    MLOG_W("play", "no feeds for this channel");
    return false;
  }
  entry_ = entryId;
  feeds_ = std::move(feeds);
  tried_ = 0;
  tunedAt_ = app_.ms();
  switchedAt_ = 0;
  if (full_) watched();
  return start(0);
}

bool Playback::start(size_t feed) {
  if (feed >= feeds_.size()) return false;
  feed_ = feed;
  ready_ = false;
  failed_ = false;
  feedAt_ = app_.ms();
  ++tried_;
  ++attempt_;
  const Feed& f = feeds_[feed];
  MLOG_I("play", "feed %zu of %zu (%s)", feed + 1, feeds_.size(), f.label.c_str());
  if (full_) app_.video().setWindow({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight});
  else app_.video().setWindow(corner_);
  if (f.needsOpen) {
    // Emby / Jellyfin open the stream on the server first; a newer tune
    // makes this one moot.
    uint64_t attempt = attempt_;
    app_.store().open(f, [this, attempt](std::optional<Feed> opened) {
      if (attempt != attempt_ || entry_.empty()) return;
      if (!opened) {
        openFailed_ = true;
        return;
      }
      feeds_[feed_] = *opened;
      app_.video().play(opened->url, opened->headers, opened->loadMode);
    });
    return true;
  }
  app_.store().closeStream();
  return app_.video().play(f.url, f.headers, f.loadMode);
}

bool Playback::useFeed(size_t index) {
  if (index >= feeds_.size()) return false;
  tried_ = 0;
  return start(index);
}

void Playback::watched() {
  if (entry_.empty() || entry_ == lastWatched_) return;
  if (!lastWatched_.empty()) previous_ = lastWatched_;
  lastWatched_ = entry_;
  app_.store().noteWatched(entry_);
}

void Playback::stop() {
  if (entry_.empty()) return;
  ++attempt_;
  app_.video().stop();
  app_.store().closeStream();
  entry_.clear();
  feeds_.clear();
  ready_ = false;
  failed_ = false;
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

void Playback::setFull() {
  full_ = true;
  watched();
  app_.video().setWindow({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight});
}

void Playback::setCorner(gfx::Rect r) {
  bool changed = full_ || r.x != corner_.x || r.y != corner_.y || r.w != corner_.w || r.h != corner_.h;
  full_ = false;
  corner_ = r;
  if (changed) app_.video().setWindow(r);
}

bool Playback::tick(uint32_t ms) {
  if (entry_.empty()) return false;
  platform::VideoState s = app_.video().state();
  if (!ready_ && (s.playing || s.decoderSeen)) {
    ready_ = true;
    app_.store().recordFeed(feeds_[feed_].channelId, true);
  }
  bool stuck = !ready_ && ms - feedAt_ > kFirstFrameMs;
  bool broken = s.failed || openFailed_;
  if ((broken || stuck) && !failed_) {
    openFailed_ = false;
    app_.store().recordFeed(feeds_[feed_].channelId, false);
    // Next feed of the same channel; after a full round, give up.
    if (tried_ < feeds_.size()) {
      MLOG_W("play", "feed %zu %s; trying the next one", feed_ + 1, broken ? "failed" : "showed nothing");
      switchedAt_ = ms;
      start((feed_ + 1) % feeds_.size());
    } else {
      failed_ = true;
      app_.video().stop();
    }
  }
  return !ready_ && !failed_;
}

}  // namespace marigold::app
