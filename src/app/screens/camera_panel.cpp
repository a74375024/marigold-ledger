// The cameras panel (LiveCameraPanel in tvOS): Down while watching a race.
// It covers the bottom of the picture: the race title with LIVE, the quick
// picks (main feed, mixed onboard, tracker, data, podium, multiview), then
// every driver in team colours, followed drivers first. OK switches, holding
// OK (or Play/Pause) follows a driver, Back closes.
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/sports/cameras.hpp"
#include "platform/logo_cache.hpp"

namespace marigold::app {
namespace {

namespace cam = sports::cameras;
using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr float kPanelH = 660;
constexpr float kLeft = 80;
constexpr float kQuickH = 76;
constexpr float kPillH = 92;
constexpr float kGap = 14;
constexpr int kHitQuick = 100, kHitDriver = 1000;
constexpr const char* kHint = "OK: switch   \xc2\xb7   Hold OK: follow a driver   \xc2\xb7   Back: close";

uint32_t parseHex(const std::optional<std::string>& hex, uint32_t fallback) {
  if (!hex || hex->empty()) return fallback;
  std::string h = (*hex)[0] == '#' ? hex->substr(1) : *hex;
  if (h.size() != 6) return fallback;
  char* end = nullptr;
  unsigned long v = std::strtoul(h.c_str(), &end, 16);
  return end && *end == '\0' ? static_cast<uint32_t>(v) : fallback;
}

class CameraPanelScreen : public Screen {
 public:
  bool holdsOk() const override { return true; }
  explicit CameraPanelScreen(App& app) : Screen(app) {
    event_ = app_.playback().entry();
    rebuild(true);
    if (panel_.focusIndex) focus_ = static_cast<int>(*panel_.focusIndex);
  }

  bool opaque() const override { return false; }

  bool tick(uint32_t) override {
    if (!app_.playback().active() || app_.playback().entry() != event_) {
      app_.nav().pop();
      return true;
    }
    int rev = app_.store().camerasRevision();
    std::string playing = app_.playback().playingChannel();
    if (rev != revision_ || playing != playing_) {
      rebuild(false);
      return true;
    }
    return false;
  }

  bool key(const KeyEvent& e) override {
    int q = static_cast<int>(panel_.quick.size());
    int d = static_cast<int>(panel_.drivers.size());
    int cols = std::max(1, panel_.columns);
    if ((e.key == Key::Ok && e.longPress) || e.key == Key::PlayPause || e.key == Key::Play || e.key == Key::Pause) {
      follow();
      return true;
    }
    switch (e.key) {
      case Key::Left:
        if (focus_ > 0 && (focus_ < q || (focus_ - q) % cols != 0)) --focus_;
        return true;
      case Key::Right:
        if (focus_ + 1 < q || (focus_ >= q && focus_ + 1 < q + d && (focus_ - q) % cols != cols - 1)) ++focus_;
        return true;
      case Key::Down:
        if (focus_ < q) {
          if (d > 0) focus_ = q;
        } else if (focus_ + cols < q + d) {
          focus_ += cols;
        } else if ((focus_ - q) / cols < (d - 1) / cols) {
          focus_ = q + d - 1;  // a shorter last row
        }
        return true;
      case Key::Up:
        if (focus_ >= q + cols) focus_ -= cols;
        else if (focus_ >= q && q > 0) focus_ = std::min(q - 1, (focus_ - q));
        else if (focus_ < q || q == 0) app_.nav().pop();  // Up from the top closes, like tvOS
        return true;
      case Key::Ok:
        select();
        return true;
      case Key::Back:
        app_.nav().pop();
        return true;
      default:
        return false;  // channel keys, colours: the player
    }
  }

  void hover(int id) override {
    if (id >= kHitDriver && id - kHitDriver < static_cast<int>(panel_.drivers.size()))
      focus_ = static_cast<int>(panel_.quick.size()) + id - kHitDriver;
    else if (id >= kHitQuick && id - kHitQuick < static_cast<int>(panel_.quick.size()))
      focus_ = id - kHitQuick;
  }

  void draw(ui::Ctx& c) override {
    float top = gfx::kLayoutHeight - kPanelH;
    // A floating card over the picture, like the banner's.
    ui::floatingCard(c, {48, top, gfx::kLayoutWidth - 96, kPanelH - 28});
    float x = kLeft, y = top + 34, w = gfx::kLayoutWidth - 2 * kLeft;
    float lx = x + ui::chip(c, panel_.header.live, x, y + 4, 17, theme::hex(theme::kLive), theme::white()) + 14;
    ui::label(c, panel_.header.title, lx, y, Weight::Bold, 32, theme::white(), w * 0.6f);
    ui::label(c, kHint, x + w, y + 8, Weight::Regular, 19, theme::white(0.6f), w * 0.4f, Align::Right);
    y += 66;
    if (panel_.empty) {
      ui::label(c, "No cameras for this race right now.", x, y, Weight::Regular, 24, theme::white(0.7f));
      return;
    }
    int q = static_cast<int>(panel_.quick.size());

    // Quick picks: one row, scrolled to keep the focused one in view.
    if (q > 0) {
      std::vector<float> widths;
      float total = 0;
      for (const cam::QuickPick& p : panel_.quick) {
        float pw = std::max(ui::textWidth(c, p.title, Weight::Bold, 22), ui::textWidth(c, p.detail, Weight::Regular, 17)) + 48;
        pw = std::min(pw, 420.0f);
        widths.push_back(pw);
        total += pw + kGap;
      }
      float offset = 0;
      if (focus_ < q) {
        float fx = 0;
        for (int i = 0; i < focus_; ++i) fx += widths[static_cast<size_t>(i)] + kGap;
        if (fx + widths[static_cast<size_t>(focus_)] - quickScroll_ > w) quickScroll_ = fx + widths[static_cast<size_t>(focus_)] - w;
        if (fx < quickScroll_) quickScroll_ = fx;
      }
      offset = std::clamp(quickScroll_, 0.0f, std::max(0.0f, total - kGap - w));
      c.g.pushClip({x - 10, y - 10, w + 20, kQuickH + 20});
      float px = x - offset;
      for (int i = 0; i < q; ++i) {
        const cam::QuickPick& p = panel_.quick[static_cast<size_t>(i)];
        Rect r{px, y, widths[static_cast<size_t>(i)], kQuickH};
        px += r.w + kGap;
        c.hit(r, kHitQuick + i);
        bool f = focus_ == i;
        c.g.fill(r, f ? theme::white() : theme::white(0.1f), 18);
        if (p.onScreen) c.g.stroke(r, f ? theme::black(0.4f) : theme::white(0.85f), 18, 3);
        Color ink = f ? theme::black() : theme::white();
        ui::label(c, p.title, r.x + 24, r.y + 12, Weight::Bold, 22, ink, r.w - 48);
        ui::label(c, p.detail, r.x + 24, r.y + 44, Weight::Regular, 17, ink.withAlpha(0.65f), r.w - 48);
      }
      c.g.popClip();
      y += kQuickH + 30;
    }

    // Drivers.
    if (panel_.onboardLabel) {
      ui::label(c, *panel_.onboardLabel, x, y, Weight::Bold, 16, theme::white(0.55f));
      y += 32;
    }
    int cols = std::max(1, panel_.columns);
    float pw = (w - kGap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    float gridTop = y, gridH = gfx::kLayoutHeight - 40 - y;
    int rows = (static_cast<int>(panel_.drivers.size()) + cols - 1) / cols;
    if (focus_ >= q) {
      int row = (focus_ - q) / cols;
      float rowY = static_cast<float>(row) * (kPillH + kGap);
      if (rowY - gridScroll_ + kPillH > gridH) gridScroll_ = rowY + kPillH - gridH;
      if (rowY < gridScroll_) gridScroll_ = rowY;
    }
    gridScroll_ = std::clamp(gridScroll_, 0.0f, std::max(0.0f, static_cast<float>(rows) * (kPillH + kGap) - kGap - gridH));
    c.g.pushClip({x - 10, gridTop - 6, w + 20, gridH + 12});
    for (size_t i = 0; i < panel_.drivers.size(); ++i) {
      const cam::DriverPill& p = panel_.drivers[i];
      int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
      Rect r{x + static_cast<float>(col) * (pw + kGap), gridTop + static_cast<float>(row) * (kPillH + kGap) - gridScroll_, pw, kPillH};
      if (r.y + r.h < gridTop - 6 || r.y > gridTop + gridH + 6) continue;
      c.hit(r, kHitDriver + static_cast<int>(i));
      drawDriver(c, p, r, focus_ == q + static_cast<int>(i));
    }
    c.g.popClip();
  }

 private:
  void drawDriver(ui::Ctx& c, const cam::DriverPill& p, Rect r, bool f) {
    c.g.fill(r, f ? theme::white() : theme::white(0.08f), 16);
    if (p.onScreen) c.g.stroke(r, f ? theme::black(0.4f) : theme::white(0.85f), 16, 3);
    Color ink = f ? theme::black() : theme::white();
    uint32_t color = parseHex(p.color, 0x9A9DA3);
    float tx = r.x + 20;
    if (p.badge) {
      float size = 62;
      Rect b{r.x + 14, r.y + (r.h - size) * 0.5f, size, size};
      c.g.fill(b, theme::hex(color), size * 0.5f);
      logos::Texture tex;
      if (c.logos && p.headshot) tex = c.logos->get(*p.headshot);
      if (tex.id) {
        c.g.image(tex.id, {b.x + 3, b.y + 3, b.w - 6, b.h - 6}, 1.0f, (size - 6) * 0.5f);
      } else {
        float lum = 0.299f * static_cast<float>((color >> 16) & 0xff) + 0.587f * static_cast<float>((color >> 8) & 0xff) +
                    0.114f * static_cast<float>(color & 0xff);
        ui::labelIn(c, p.code, b, Weight::Bold, 21, lum > 170 ? theme::black() : theme::white(), Align::Center);
      }
      tx = b.x + size + 16;
    } else {
      c.g.fill({r.x + 14, r.y + 18, 6, r.h - 36}, p.color ? theme::hex(color) : theme::white(0.3f), 3);
      tx = r.x + 34;
    }
    float right = r.x + r.w - 16;
    if (p.followed) {
      ui::label(c, "\xe2\x98\x85", right - 22, r.y + 16, Weight::Bold, 22, theme::hex(theme::kStar));
      right -= 28;
    }
    ui::label(c, p.name, tx, r.y + 18, Weight::Bold, 23, ink, right - tx);
    ui::label(c, p.detail, tx, r.y + 52, Weight::Regular, 17, ink.withAlpha(0.65f), r.x + r.w - 16 - tx);
  }

  void rebuild(bool opening) {
    std::string keep = focusId();
    playing_ = app_.playback().playingChannel();
    panel_ = app_.store().cameraPanel(event_, playing_, opening);
    revision_ = app_.store().camerasRevision();
    if (!keep.empty()) {
      if (auto i = cam::indexOf(panel_, keep)) focus_ = static_cast<int>(*i);
    }
    int n = static_cast<int>(panel_.quick.size() + panel_.drivers.size());
    focus_ = std::clamp(focus_, 0, std::max(0, n - 1));
    app_.invalidate();
  }

  std::string focusId() const {
    int q = static_cast<int>(panel_.quick.size());
    if (focus_ < q) return focus_ >= 0 ? panel_.quick[static_cast<size_t>(focus_)].id : std::string();
    size_t d = static_cast<size_t>(focus_ - q);
    return d < panel_.drivers.size() ? panel_.drivers[d].id : std::string();
  }

  void select() {
    int q = static_cast<int>(panel_.quick.size());
    std::string channel;
    std::vector<std::string> alternates;
    if (focus_ < q && focus_ >= 0) {
      channel = panel_.quick[static_cast<size_t>(focus_)].channelID;
      alternates = panel_.quick[static_cast<size_t>(focus_)].alternates;
    } else if (static_cast<size_t>(focus_ - q) < panel_.drivers.size()) {
      channel = panel_.drivers[static_cast<size_t>(focus_ - q)].channelID;
      alternates = panel_.drivers[static_cast<size_t>(focus_ - q)].alternates;
    }
    if (channel.empty()) return;
    App& app = app_;
    auto feeds = app.store().cameraFeeds(channel, alternates, app.playback().playingChannel());
    app.nav().pop();  // `this` is gone after this line
    if (!feeds) return;  // already on screen
    if (feeds->empty() || !app.playback().tuneCamera(std::move(*feeds))) app.toast("This camera isn't working right now", true);
  }

  void follow() {
    int q = static_cast<int>(panel_.quick.size());
    if (focus_ < q) return;
    size_t d = static_cast<size_t>(focus_ - q);
    if (d >= panel_.drivers.size()) return;
    std::string name = panel_.drivers[d].name;
    bool on = app_.store().toggleFollowDriver(name);
    app_.toast(on ? "Following " + name : "Stopped following " + name, true);
    rebuild(false);
  }

  std::string event_;
  cam::CameraPanel panel_;
  std::string playing_;
  int revision_ = -1;
  int focus_ = 0;
  float quickScroll_ = 0, gridScroll_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeCameraPanel(App& app) { return std::make_unique<CameraPanelScreen>(app); }

}  // namespace marigold::app
