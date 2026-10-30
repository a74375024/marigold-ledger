#include "app/playback.hpp"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "app/app.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"
#include "platform/storage.hpp"
#include "platform/ts_proxy.hpp"

namespace marigold::app {
namespace {

// HLS feeds go through the local TS proxy (Legacy #158: the TV's HLS engine
// stays mute on some live channels that play at once as continuous TS).
bool looksHls(const std::string& url) {
  std::string lower = str::lowerAscii(url);
  std::string path = lower.substr(0, lower.find('?'));
  return str::endsWith(path, ".m3u8") || str::endsWith(path, ".m3u");
}

// The platform's video player, with the TS proxy in front of HLS feeds where
// the platform wants it (webOS).
class TvPlayer final : public engine::Player {
 public:
  TvPlayer(App& app, std::function<gfx::Rect()> window) : app_(app), window_(std::move(window)) {
    if (platform::TsProxy::wanted() && !proxy_.start())
      MLOG_W("play", "TS proxy unavailable; HLS feeds play directly");
  }

  void beginTune() override { proxyFailed_.clear(); }

  void attempt() override {
    viaProxy_ = false;
    app_.video().setWindow(window_());
  }

  void play(const engine::Feed& f, bool asGiven) override {
    feed_ = f;
    std::string url = f.url, headers = f.headers;
    if (!asGiven && looksHls(url) && !proxyFailed_.count(f.channelId)) {
      std::string local = proxy_.localUrl(url, headers);
      if (!local.empty()) {
        url = local;
        headers.clear();  // the proxy sends them to the source
        viaProxy_ = true;
      }
    }
    app_.video().play(url, headers, f.loadMode);
  }

  // The proxy's attempt failed before any picture: play the same feed once
  // directly, unless the proxy only redirected to it (that was the direct try).
  std::optional<Retry> retryAnotherWay() override {
    if (!viaProxy_) return std::nullopt;
    viaProxy_ = false;
    bool redirected = proxy_.redirected();
    proxyFailed_.insert(feed_.channelId);
    proxy_.stop();
    if (redirected) return std::nullopt;
    MLOG_I("play", "proxy didn't start this feed; playing it directly");
    app_.video().play(feed_.url, feed_.headers, feed_.loadMode);
    return Retry{"proxy failed", "playing directly"};
  }

  void streamClosed() override { proxy_.stop(); }

  void stop() override {
    app_.video().stop();
    proxy_.stop();
  }

  engine::PlayerState state() const override {
    platform::VideoState v = app_.video().state();
    engine::PlayerState s;
    s.picture = v.playing || v.decoderSeen;
    s.buffering = v.bufferingMs > 0;
    s.failed = v.failed;
    s.ended = v.ended;
    s.error = v.error;
    s.position = v.position;
    s.bufferEnd = v.bufferEnd;
    s.reportsLoading = v.reportsLoading;
    s.loaded = v.ready || v.bufferEnd > 0;
    return s;
  }

 private:
  App& app_;
  std::function<gfx::Rect()> window_;
  platform::TsProxy proxy_;
  engine::Feed feed_;  // the one playing (its link stays in memory only)
  // The current attempt went through the TS proxy; feeds (raw channel ids)
  // the proxy failed on play directly for the rest of this tune.
  bool viaProxy_ = false;
  std::set<std::string> proxyFailed_;
};

engine::Host sessionHost(App& app) {
  engine::Host h{app.pool(), app.storage()};
  h.ms = [&app] { return app.ms(); };
  return h;
}

}  // namespace

Playback::Playback(App& app)
    : Session(app.store(), sessionHost(app), std::make_unique<TvPlayer>(app, [this] { return window(); }),
              Hooks{[&app](const std::string& text) { app.toast(text, true); }, [&app] { app.invalidate(); },
                    [this] {
                      if (reapplyOnReady_) {
                        reapplyOnReady_ = false;
                        app_.video().reapplyWindow(window());
                      }
                    }}),
      app_(app) {}

gfx::Rect Playback::window() const {
  return full() ? gfx::Rect{0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight} : corner_;
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
  bool changed = !full();
  setShowingFull(true);
  app_.video().setWindow(window());
  if (changed) scheduleReapply();
}

void Playback::setCorner(gfx::Rect r) {
  bool changed = full() || r.x != corner_.x || r.y != corner_.y || r.w != corner_.w || r.h != corner_.h;
  setShowingFull(false);
  corner_ = r;
  if (changed) {
    app_.video().setWindow(r);
    if (active()) scheduleReapply();
  }
}

bool Playback::tick(uint32_t ms) {
  bool pending = Session::tick(ms);
  for (uint32_t& at : reapplyAt_) {
    if (at && ms >= at) {
      at = 0;
      app_.video().reapplyWindow(window());
    }
  }
  bool reapplying = reapplyAt_[0] || reapplyAt_[1] || reapplyAt_[2];
  return pending || reapplying;
}

}  // namespace marigold::app
