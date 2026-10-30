#include "app/playback.hpp"

#include <algorithm>
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

// What the relay's source answered, for the log ("; the source answered
// HTTP 403 with no video").
std::string sourceAnswer(const platform::TsProxy::Source& s) {
  if (!s.known) return {};
  if (s.http == 0 && s.bytes == 0) return "; the source didn't answer";
  std::string http = s.http ? "HTTP " + std::to_string(s.http) : std::string("no HTTP status");
  if (s.bytes == 0) return "; the source answered " + http + " with no video";
  return "; the source sent " + std::to_string(std::max(1LL, s.bytes / 1024)) + " KB (" + http + ")";
}

// The platform's video player, with the TS proxy in front of HLS feeds where
// the platform wants it (webOS). Continuous TS feeds go through it too
// (Options › Play through the app, or with subtitles on), so their captions
// can be read on the way to the player.
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
    reopenedSeen_ = 0;
    app_.video().setWindow(window_());
  }

  void play(const engine::Feed& f, bool asGiven) override {
    feed_ = f;
    std::string url = f.url, headers = f.headers;
    bool hls = looksHls(url);
    const Settings& set = app_.store().settings();
    unsigned relay = 0;
    if (!asGiven && (hls || anySubtitles(set) || set.playThroughApp) && !proxyFailed_.count(f.channelId)) {
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
    std::string answered = sourceAnswer(proxy_.source());
    proxyFailed_.insert(feed_.channelId);
    proxy_.stop();
    if (redirected) return std::nullopt;
    MLOG_I("play", "proxy didn't start this feed; playing it directly");
    app_.playback().subtitlesBegin(0, false);
    app_.video().play(feed_.url, feed_.headers, feed_.loadMode);
    return Retry{"proxy failed", "playing directly" + answered};
  }

  // The relay reopened a source that closed mid-stream (the player kept
  // playing): said in the log once each time.
  std::optional<Retry> note() override {
    if (!viaProxy_) return std::nullopt;
    uint32_t now = app_.ms();
    if (now - noteCheckedAt_ < 1000) return std::nullopt;
    noteCheckedAt_ = now;
    platform::TsProxy::Source s = proxy_.source();
    if (!s.known || s.reopened <= reopenedSeen_) return std::nullopt;
    reopenedSeen_ = s.reopened;
    return Retry{"relay", "the source closed; reopened it (" + std::to_string(s.reopened) +
                              (s.reopened == 1 ? " time)" : " times)")};
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
  int reopenedSeen_ = 0;
  uint32_t noteCheckedAt_ = 0;
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
  // Which way the stream goes (webOS), to compare the two in the log.
  if (relaySession) logNote("route", "through the app");
  else if (relayPossible) logNote("route", "direct");
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
  if (subtitleChoice().on && relayPossible_ && active() && !failed() && !subtitles_->fromStream())
    restart("subtitles on");
  app_.invalidate();
}

bool Playback::spanishChannel() const { return channelLanguage(app_) != "English"; }

SubtitleChoice Playback::subtitleChoice() const {
  return engine::subtitleChoice(app_.store().settings(), spanishChannel());
}

void Playback::setSubtitleChoice(const SubtitleChoice& c) {
  Settings s = app_.store().settings();
  engine::setSubtitleChoice(s, spanishChannel(), c);
  app_.store().setSettings(s);
  subtitlesChanged();
}

bool Playback::toggleSubtitles() {
  SubtitleChoice c = subtitleChoice();
  c.on = !c.on;
  setSubtitleChoice(c);
  return c.on;
}

Playback::SubtitleStatus Playback::subtitleStatus() const {
  SubtitleStatus st;
  SubtitleChoice c = subtitleChoice();
  st.on = c.on;
  if (!c.on) {
    st.label = "Off";
    return st;
  }
  std::string shown = subtitles_->selectedLabel();
  if (shown.empty()) {
    st.label = c.language.empty() ? std::string("On") : c.language;
    st.detail = "waiting\xe2\x80\xa6";
    return st;
  }
  st.label = shown;
  if (!c.language.empty() && c.language != shown) st.detail = "no " + c.language + " here";
  return st;
}

void Playback::tickSubtitles(uint32_t ms) {
  engine::Subtitles& s = *subtitles_;
  // The system's captions switch, every few seconds: turned on or off
  // there, subtitles follow here (both languages). The first reading only
  // turns them on, never off.
  if (ms >= systemCheckAt_) {
    systemCheckAt_ = ms + 5000;
    int sys = platform::Video::systemCaptions();
    const Settings& now = app_.store().settings();
    if (sys >= 0 && sys != now.systemCaptions) {
      Settings next = now;
      if (now.systemCaptions >= 0 || sys == 1) next.subtitles = next.subtitlesEs = sys == 1;
      next.systemCaptions = sys;
      bool changed = next.subtitles != now.subtitles || next.subtitlesEs != now.subtitlesEs;
      app_.store().setSettings(next);
      if (changed) {
        logNote("captions", sys == 1 ? "turned on in the system settings" : "turned off in the system settings");
        subtitlesChanged();
      }
    }
  }
  if (!active()) return;
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
  SubtitleChoice choice = subtitleChoice();
  std::string want = choice.on ? s.pick(choice.language) : std::string();
  if (want != s.selected()) {
    s.select(want);
    if (!s.fromStream()) video.selectText(want, choice.on);
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
  } else if (!s.present() && !noneLogged_ && pictureAt_ && ms - pictureAt_ > 60000 && (s.fromStream() || choice.on)) {
    noneLogged_ = true;
    logNote("captions", "none after a minute");
  }
}

}  // namespace marigold::app
