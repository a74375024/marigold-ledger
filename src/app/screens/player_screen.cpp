// Full screen playback (LiveTVOverlay): the tuning card, the info banner,
// the backup note, channel flipping, previous channel and number keys.
#include <algorithm>
#include <cstdlib>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr uint32_t kBannerDelayMs = 1500;
constexpr uint32_t kBannerMs = 6000;
constexpr uint32_t kNoteMs = 5000;
constexpr uint32_t kDigitsMs = 1500;

class PlayerScreen : public Screen {
 public:
  explicit PlayerScreen(App& app) : Screen(app) { bannerFrom_ = app.ms() + kBannerDelayMs; }

  bool key(const KeyEvent& e) override {
    Playback& p = app_.playback();
    int d = digitOf(e.key);
    if (d >= 0) {
      if (digits_.size() >= 4) digits_.clear();
      digits_ += static_cast<char>('0' + d);
      digitsAt_ = app_.ms();
      return true;
    }
    switch (e.key) {
      case Key::Up:
      case Key::ChannelUp:
        flipped(p.flip(-1));
        return true;
      case Key::Down:
      case Key::ChannelDown:
        flipped(p.flip(1));
        return true;
      case Key::Right: {
        std::string prev = p.previousEntry();
        if (prev.empty()) {
          showBanner();
          return true;
        }
        std::string name = nameOf(prev);
        p.previous();
        app_.toast("Back to " + name, true);
        showBanner();
        return true;
      }
      case Key::Left:
        if (!p.failed()) {
          // Watching a game from Live Events: the event guide.
          app_.nav().push(app_.store().isEvent(p.entry()) ? makeEventGuide(app_) : makeMiniGuide(app_));
          return true;
        }
        [[fallthrough]];
      case Key::Info:
      case Key::Ok:
        if (p.failed()) {
          // Try Again.
          std::string id = p.entry();
          p.stop();
          p.tune(id);
          p.setFull();
          return true;
        }
        if (bannerVisible()) bannerUntil_ = 0;
        else showBanner();
        return true;
      case Key::Red:
        app_.nav().push(makeSources(app_, p.entry()));
        return true;
      case Key::Blue:
        app_.nav().push(makeOptions(app_));
        return true;
      case Key::Back:
        if (!digits_.empty()) {
          digits_.clear();
          return true;
        }
        app_.nav().pop();
        return true;
      default:
        return false;
    }
  }

  bool tick(uint32_t ms) override {
    Playback& p = app_.playback();
    bool opening = !p.ready() && !p.failed();
    if (!p.active()) {
      app_.nav().pop();
      return false;
    }
    if (p.entry() != shownEntry_) {
      shownEntry_ = p.entry();
      bannerFrom_ = ms + kBannerDelayMs;
      bannerUntil_ = bannerFrom_ + kBannerMs;
    }
    if (!digits_.empty() && ms - digitsAt_ >= kDigitsMs) {
      int n = std::atoi(digits_.c_str());
      digits_.clear();
      std::string target;
      auto ch = app_.store().channels();
      for (const GuideChannel& g : ch->all) {
        if (g.number && *g.number == n) {
          target = g.id;
          break;
        }
      }
      if (target.empty()) app_.toast("No channel " + std::to_string(n), true);
      else flipped(p.tune(target) ? target : std::string());
    }
    bool bannerAnim = ms < bannerUntil_ + 50;
    bool note = p.switchedAt() && ms - p.switchedAt() < kNoteMs + 50;
    return opening || bannerAnim || note || !digits_.empty();
  }

  void draw(ui::Ctx& c) override {
    Playback& p = app_.playback();
    auto ch = app_.store().channels();
    const GuideChannel* g = ch->find(p.entry());
    if (p.ready() && !p.failed()) {
      c.g.hole({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight});
    } else {
      drawTuning(c, g);
    }
    if (bannerVisible() && p.ready()) drawBanner(c, g);
    if (p.switchedAt() && c.ms - p.switchedAt() < kNoteMs) {
      std::string note = "Switched to a backup feed";
      float w = ui::textWidth(c, note, Weight::Medium, 20) + 48;
      Rect r{gfx::kLayoutWidth - 80 - w, 50, w, 48};
      c.g.fill(r, theme::black(0.7f), 24);
      ui::labelIn(c, note, r, Weight::Medium, 20, theme::white(0.9f), Align::Center);
    }
    if (!digits_.empty()) {
      Rect r{gfx::kLayoutWidth - 80 - 220, 60, 220, 90};
      c.g.fill(r, theme::black(0.75f), 20);
      ui::labelIn(c, digits_, r, Weight::Bold, 54, theme::white(), Align::Center);
    }
  }

 private:
  bool bannerVisible() const { return app_.ms() >= bannerFrom_ && app_.ms() < bannerUntil_; }
  void showBanner() {
    bannerFrom_ = app_.ms();
    bannerUntil_ = bannerFrom_ + kBannerMs;
  }
  void flipped(const std::string& id) {
    if (id.empty()) return;
    showBanner();
    bannerFrom_ = app_.ms() + kBannerDelayMs;
    bannerUntil_ = bannerFrom_ + kBannerMs;
  }
  std::string nameOf(const std::string& id) const {
    auto ch = app_.store().channels();
    const GuideChannel* g = ch->find(id);
    return g ? g->name : std::string("the previous channel");
  }
  const Listing* current(const std::string& id, time::Seconds now, const Listing** next) const {
    listings_ = app_.store().listings();
    const std::vector<Listing>* v = listings_->forChannel(id);
    if (next) *next = nullptr;
    if (!v) return nullptr;
    for (size_t i = 0; i < v->size(); ++i) {
      if ((*v)[i].isOn(now)) {
        if (next && i + 1 < v->size()) *next = &(*v)[i + 1];
        return &(*v)[i];
      }
      if ((*v)[i].start > now) {
        if (next) *next = &(*v)[i];
        return nullptr;
      }
    }
    return nullptr;
  }

  void drawTuning(ui::Ctx& c, const GuideChannel* g) {
    Playback& p = app_.playback();
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::black());
    float x = 110, y = gfx::kLayoutHeight - 110 - 420;
    if (g) ui::logo(c, g->logoUrl, "", {x, y, 300, 110});
    y += 130;
    if (g) ui::label(c, g->name, x, y, Weight::Bold, 34, theme::white());
    y += 54;
    ui::label(c, p.failed() ? "CAN'T PLAY THIS CHANNEL" : "TUNING IN", x, y, Weight::Bold, 22,
              p.failed() ? theme::hex(theme::kWatching) : theme::white(0.6f));
    y += 40;
    const Listing* l = g ? current(g->id, c.now, nullptr) : nullptr;
    if (l) ui::label(c, l->title, x, y, Weight::Bold, 60, theme::white(), 1500);
    y += 90;
    if (p.failed()) {
      ui::label(c, "None of its feeds answered. OK tries again; Back returns to the guide.", x, y,
                Weight::Regular, 28, theme::white(0.7f));
    } else {
      ui::spinner(c, x + 20, y + 20, 18);
    }
  }

  void drawBanner(ui::Ctx& c, const GuideChannel* g) {
    Rect r{80, gfx::kLayoutHeight - 60 - 190, gfx::kLayoutWidth - 160, 190};
    c.g.fill(r, theme::hex(0x15171C, 0.9f), theme::kPanelRadius);
    float x = r.x + 40;
    ui::labelIn(c, "LIVE", {x, r.y, 120, r.h}, Weight::Bold, 34, theme::white());
    x += 130;
    c.g.fill({x, r.y + 36, 2, r.h - 72}, theme::white(0.2f));
    x += 36;
    Playback& p = app_.playback();
    float right = r.x + r.w - 40;
    // Previous channel box and the CH hint, from the right.
    ui::labelIn(c, "\xe2\x96\xb2 CH \xe2\x96\xbc", {right - 110, r.y, 110, r.h}, Weight::Bold, 20, theme::white(0.6f),
                Align::Right);
    right -= 140;
    auto ch = app_.store().channels();
    if (const GuideChannel* prev = ch->find(p.previousEntry())) {
      Rect box{right - 320, r.y + 40, 320, r.h - 80};
      c.g.fill(box, theme::white(0.08f), 16);
      ui::label(c, "\xe2\x86\x92 PREVIOUS", box.x + 20, box.y + 14, Weight::Bold, 15, theme::white(0.55f));
      ui::logo(c, prev->logoUrl, "", {box.x + 20, box.y + 46, 76, 32});
      ui::label(c, prev->name, box.x + (prev->logoUrl.empty() ? 20 : 108), box.y + 48, Weight::Bold, 22, theme::white(),
                box.w - 128);
      right = box.x - 30;
    }
    if (!g) return;
    const Listing* next = nullptr;
    const Listing* l = current(g->id, c.now, &next);
    if (next) {
      std::string nextText = "NEXT \xc2\xb7 " + ui::clock(c, next->start, false);
      float nw = std::min(520.0f, std::max(ui::textWidth(c, next->title, Weight::Medium, 22), ui::textWidth(c, nextText, Weight::Bold, 15)));
      ui::label(c, nextText, right - nw, r.y + 54, Weight::Bold, 15, theme::white(0.55f));
      ui::label(c, next->title, right - nw, r.y + 82, Weight::Medium, 22, theme::white(0.85f), nw);
      right -= nw + 40;
    }
    float w = right - x;
    std::string name = g->name;
    for (char& chr : name) chr = static_cast<char>(chr >= 'a' && chr <= 'z' ? chr - 32 : chr);
    ui::label(c, name, x, r.y + 30, Weight::Bold, 17, theme::white(0.6f), w);
    ui::label(c, l ? l->title : g->name, x, r.y + 60, Weight::Bold, 36, theme::white(), w);
    if (l) {
      std::string line = ui::range(c, l->start, l->end, true);
      if (!l->episode.empty()) line += " \xc2\xb7 " + l->episode;
      ui::label(c, line, x, r.y + 112, Weight::Regular, 22, theme::white(0.7f), w);
      double frac = static_cast<double>(c.now - l->start) / static_cast<double>(std::max<time::Seconds>(1, l->end - l->start));
      ui::progress(c, {x, r.y + 150, std::min(420.0f, w), 5}, frac);
    }
  }

  uint32_t bannerFrom_ = 0, bannerUntil_ = 0;
  std::string shownEntry_;
  std::string digits_;
  uint32_t digitsAt_ = 0;
  mutable std::shared_ptr<const Listings> listings_;
};

}  // namespace

std::unique_ptr<Screen> makePlayer(App& app) { return std::make_unique<PlayerScreen>(app); }

}  // namespace marigold::app
