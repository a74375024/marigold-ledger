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

// The channel's own language, naming its caption services.
std::string channelLanguage(App& app) {
  const GuideChannel* g = app.store().channels()->find(app.playback().entry());
  return g && g->section == "Spanish" ? "Espa\xc3\xb1ol" : "English";
}

// The platform's video player, with the TS proxy in front of HLS feeds where
// the platform wants it (webOS). With subtitles on, continuous TS feeds go
// through it too, so their captions can be read on the way to the player.
class TvPlayer final : public engine::Player, private platform::TsProxy::Tap {
 public:
  TvPlayer(App& app, std::function<gfx::Rect()> window, std::shared_ptr<engine::Subtitles> subtitles)
      : app_(app), window_(std::move(window)), subtitles_(std::move(subtitles)) {
    if (platform::TsProxy::wanted() && !proxy_.start())
      MLOG_W("play", "TS proxy unavailable; HLS feeds play directly");
    proxy_.setTap(this);
  }
  ~TvPlayer() override { proxy_.setTap(nullptr); }

  void beginTune() override { proxyFailed_.clear(); }

  void attempt() override {
    viaProxy_ = false;
    app_.video().setWindow(window_());
  }

  void play(const engine::Feed& f, bool asGiven) override {
    feed_ = f;
    std::string url = f.url, headers = f.headers;
    bool hls = looksHls(url);
    bool subtitles = app_.store().settings().subtitles;
    unsigned relay = 0;
    if (!asGiven && (hls || subtitles) && !proxyFailed_.count(f.channelId)) {
      std::string local = proxy_.localUrl(url, headers, !hls);
      if (!local.empty()) {
        url = local;
        headers.clear();  // the proxy sends them to the source
        viaProxy_ = true;
        relay = proxy_.session();
      }
    }
    app_.playback().subtitlesBegin(relay, !asGiven && proxy_.running() && !proxyFailed_.count(f.channelId));
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
    app_.playback().subtitlesBegin(0, false);
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
  // The proxy's threads: the stream as it reaches the player.
  void bytes(const unsigned char* data, size_t n, unsigned session) override { subtitles_->bytes(data, n, session); }

  App& app_;
  std::function<gfx::Rect()> window_;
  std::shared_ptr<engine::Subtitles> subtitles_;
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

Playback::Playback(App& app) : Playback(app, std::make_shared<engine::Subtitles>()) {}

Playback::Playback(App& app, std::shared_ptr<engine::Subtitles> subtitles)
    : Session(app.store(), sessionHost(app),
              std::make_unique<TvPlayer>(app, [this] { return window(); }, subtitles),
              Hooks{[&app](const std::string& text) { app.toast(text, true); }, [&app] { app.invalidate(); },
                    [this] {
                      if (reapplyOnReady_) {
                        reapplyOnReady_ = false;
                        app_.video().reapplyWindow(window());
                      }
                    }}),
      app_(app),
      subtitles_(std::move(subtitles)) {}

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
  tickSubtitles(ms);
  for (uint32_t& at : reapplyAt_) {
    if (at && ms >= at) {
      at = 0;
      app_.video().reapplyWindow(window());
    }
  }
  bool reapplying = reapplyAt_[0] || reapplyAt_[1] || reapplyAt_[2];
  return pending || reapplying;
}

// ---------------------------------------------------------------- subtitles

bool Playback::playerDrawsSubtitles() { return platform::Video::drawsText(); }

void Playback::subtitlesBegin(unsigned relaySession, bool relayPossible) {
  subtitles_->begin(relaySession, channelLanguage(app_));
  relayPossible_ = relayPossible;
  captionsLogged_ = noneLogged_ = false;
  textVersion_ = 0;
  textTracksAt_ = 0;
  pictureAt_ = 0;
  const Settings& set = app_.store().settings();
  app_.video().setTextStyle(set.subtitleSize, set.subtitleLook);
}

void Playback::subtitlesChanged() {
  const Settings& set = app_.store().settings();
  app_.video().setTextStyle(set.subtitleSize, set.subtitleLook);
  // webOS: captions are read on the way through the relay, which a
  // continuous TS feed only takes with subtitles on; play it again that way.
  if (set.subtitles && relayPossible_ && active() && !failed() && !subtitles_->fromStream())
    restart("subtitles on");
  app_.invalidate();
}

void Playback::tickSubtitles(uint32_t ms) {
  engine::Subtitles& s = *subtitles_;
  if (!active()) return;
  const Settings& set = app_.store().settings();
  platform::Video& video = app_.video();
  std::string language = channelLanguage(app_);
  if (!s.fromStream()) {
    // A player that decodes them: its tracks twice a second, its lines
    // when they change.
    if (ms >= textTracksAt_) {
      textTracksAt_ = ms + 500;
      std::vector<engine::CaptionTrack> tracks;
      for (const platform::Video::TextTrack& t : video.textTracks()) {
        std::string label = engine::languageName(t.language);
        if (label.empty()) label = engine::captionServiceLabel(t.channel > 0 ? t.channel : 1, language);
        tracks.push_back({t.id, label});
      }
      s.playerTracks(std::move(tracks), playerDrawsSubtitles());
    }
    uint64_t version = 0;
    std::vector<std::string> lines = video.textLines(&version);
    if (version != textVersion_) {
      textVersion_ = version;
      s.playerLines(lines);
    }
  }
  // On: the language picked last (or the channel's own); off: none.
  std::string want = set.subtitles ? s.pick(set.subtitleLanguage) : std::string();
  if (want != s.selected()) {
    s.select(want);
    if (!s.fromStream()) video.selectText(want, set.subtitles);
  }
  int64_t lead = -1;
  if (s.fromStream()) {
    platform::VideoState v = video.state();
    if (v.bufferEnd > v.position && v.position > 0) lead = static_cast<int64_t>((v.bufferEnd - v.position) * 1000);
  }
  if (ready() && !pictureAt_) pictureAt_ = ms;
  if (s.tick(ms, lead, ready())) app_.invalidate();
  // The playback log: whether the stream carries captions.
  size_t i = feedIndex();
  if (s.present() && !captionsLogged_) {
    captionsLogged_ = true;
    if (i < feeds().size()) app_.store().captionsSeen(feeds()[i].channelId);
    std::string names;
    for (const engine::CaptionTrack& t : s.tracks()) names += (names.empty() ? "" : ", ") + t.label;
    logNote("captions", "in the stream" + (names.empty() ? std::string() : " (" + names + ")"));
  } else if (!s.present() && !noneLogged_ && pictureAt_ && ms - pictureAt_ > 60000 && (s.fromStream() || set.subtitles)) {
    noneLogged_ = true;
    logNote("captions", "none after a minute");
  }
}

}  // namespace marigold::app
