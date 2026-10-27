// The mini guide (LiveTVMiniGuide): Left in the player. A leading panel with
// the channels of the list being watched, under their neighborhoods, each
// with what's on and what's next. Up/Down browse while the picture keeps
// playing, OK switches, Left opens the neighborhoods, Back steps out.
#include <algorithm>

#include "core/engine/views.hpp"
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

constexpr float kListW = 980;
constexpr float kHoodW = 320;
constexpr float kRowH = 96;
constexpr float kHeaderH = 40;
constexpr float kTop = 120;
constexpr size_t kGuideRows = 300;  // listings asked for on open
constexpr int kHitRow = 100;
constexpr int kHitHood = 10;

class MiniGuide : public Screen {
 public:
  explicit MiniGuide(App& app) : Screen(app) {
    rebuild();
    // Start on the channel being watched.
    const std::string& playing = app_.playback().entry();
    for (size_t i = 0; i < lines_.size(); ++i) {
      if (lines_[i].channel >= 0 && ch_->all[static_cast<size_t>(lines_[i].channel)].id == playing) focus_ = static_cast<int>(i);
    }
    if (focus_ < 0) moveTo(0, 1);
    std::vector<std::string> ids;
    for (const Line& l : lines_) {
      if (l.channel >= 0 && ids.size() < kGuideRows) ids.push_back(ch_->all[static_cast<size_t>(l.channel)].id);
    }
    app_.store().wantListings(ids);
  }

  bool opaque() const override { return false; }

  bool key(const KeyEvent& e) override {
    if (hoods_) {
      switch (e.key) {
        case Key::Up:
        case Key::Down:
          hood_ = std::clamp(hood_ + (e.key == Key::Up ? -1 : 1), 0, static_cast<int>(sections_.size()) - 1);
          return true;
        case Key::Right:
        case Key::Ok:
          hoods_ = false;
          if (hood_ < static_cast<int>(sections_.size())) moveTo(sections_[static_cast<size_t>(hood_)].second + 1, 1);
          return true;
        case Key::Back:
        case Key::Left:
          hoods_ = false;
          return true;
        default:
          return true;
      }
    }
    switch (e.key) {
      case Key::Up:
        moveTo(focus_ - 1, -1);
        return true;
      case Key::Down:
        moveTo(focus_ + 1, 1);
        return true;
      case Key::ChannelUp:
        moveTo(focus_ - 6, -1);
        return true;
      case Key::ChannelDown:
        moveTo(focus_ + 6, 1);
        return true;
      case Key::Left:
        if (!sections_.empty()) {
          hoods_ = true;
          hood_ = 0;
          for (size_t i = 0; i < sections_.size(); ++i)
            if (sections_[i].second < focus_) hood_ = static_cast<int>(i);
        }
        return true;
      case Key::Ok: {
        const GuideChannel* g = focused();
        if (!g) return true;
        Playback& p = app_.playback();
        if (g->id == p.entry()) {
          app_.nav().pop();
          return true;
        }
        if (!p.tune(g->id)) app_.toast("This channel has no working feed right now", true);
        return true;
      }
      case Key::Right:
        app_.nav().pop();
        return true;
      default:
        return e.key != Key::Back;
    }
  }

  void hover(int id) override {
    if (id >= kHitRow) focus_ = std::clamp(id - kHitRow, 0, static_cast<int>(lines_.size()) - 1);
    else if (id >= kHitHood && hoods_) hood_ = id - kHitHood;
  }

  bool tick(uint32_t) override {
    if (app_.store().channels()->version != ch_->version) rebuild();
    return false;
  }

  void draw(ui::Ctx& c) override {
    auto li = app_.store().listings();
    float panelW = kListW + (hoods_ ? kHoodW : 0);
    ui::sidePanel(c, {0, 0, panelW + 120, gfx::kLayoutHeight}, true);
    float x = 70 + (hoods_ ? kHoodW : 0);
    ui::label(c, "LIVE TV \xc2\xb7 " + ui::clock(c, c.now), x, 60, Weight::Bold, 20, theme::white(0.6f));
    if (hoods_) drawHoods(c);

    // Keep the focus in view.
    float visible = gfx::kLayoutHeight - kTop - 80;
    float focusY = focus_ >= 0 ? lines_[static_cast<size_t>(focus_)].y : 0;
    if (focusY - kRowH < scroll_) scroll_ = std::max(0.0f, focusY - kRowH);
    if (focusY + 2 * kRowH > scroll_ + visible) scroll_ = focusY + 2 * kRowH - visible;
    c.g.pushClip({0, kTop, panelW + 40, visible});
    const std::string& playing = app_.playback().entry();
    for (size_t i = 0; i < lines_.size(); ++i) {
      const Line& l = lines_[i];
      float y = kTop + l.y - scroll_;
      if (y > kTop + visible || y + kRowH < kTop) continue;
      if (l.channel < 0) {
        ui::label(c, l.header, x, y + 12, Weight::Bold, 16, theme::white(0.5f));
        continue;
      }
      const GuideChannel& g = ch_->all[static_cast<size_t>(l.channel)];
      bool f = static_cast<int>(i) == focus_ && !hoods_;
      Rect r{x - 14, y, kListW - 60, kRowH - 6};
      c.hit(r, kHitRow + static_cast<int>(i));
      if (f) c.g.fill(r, theme::white(), theme::kRowRadius);
      ui::Color fg = f ? theme::black() : theme::white();
      ui::Color sub = f ? theme::black(0.6f) : theme::white(0.6f);
      ui::logo(c, g.logoUrl, g.name, {r.x + 14, r.y + 13, 136, 64}, f);
      float tx = r.x + 170, tw = r.w - 190;
      engine::views::NowNext on = engine::views::nowAndNext(*li, g.id, c.now);
      const Listing* now = on.now;
      const Listing* next = on.now ? on.next : nullptr;
      if (g.id == playing) {
        c.g.fill({tx, r.y + 18, 10, 10}, theme::hex(theme::kWatching), 5);
        ui::label(c, "WATCHING", tx + 18, r.y + 13, Weight::Bold, 15, f ? theme::black(0.7f) : theme::hex(theme::kWatching));
        tx += 0;
      }
      float titleY = g.id == playing ? r.y + 34 : r.y + 16;
      ui::label(c, now ? now->title : g.name, tx, titleY, Weight::Bold, 25, fg, tw);
      std::string line;
      if (now) {
        line = "until " + ui::clock(c, now->end, false);
        if (next) line += " \xc2\xb7 Next: " + next->title;
      } else {
        line = g.name;
      }
      if (g.id != playing) ui::label(c, line, tx, r.y + 52, Weight::Medium, 19, sub, tw);
      if (now) {
        double frac = static_cast<double>(c.now - now->start) / static_cast<double>(std::max<time::Seconds>(1, now->end - now->start));
        ui::progress(c, {tx, r.y + kRowH - 18, 280, 5}, frac, f ? theme::black(0.15f) : theme::white(0.18f),
                     f ? theme::black() : theme::white());
      }
    }
    c.g.popClip();
    ui::label(c, "\xe2\x96\xb2\xe2\x96\xbc browse \xc2\xb7 OK: watch \xc2\xb7 \xe2\x97\x80 neighborhoods \xc2\xb7 Back: close", x,
              gfx::kLayoutHeight - 60, Weight::Regular, 18, theme::white(0.55f));
  }

 private:
  struct Line {
    int channel = -1;  // index into Channels::all, -1 for a header
    std::string header;
    float y = 0;
  };

  const GuideChannel* focused() const {
    if (focus_ < 0 || static_cast<size_t>(focus_) >= lines_.size()) return nullptr;
    int i = lines_[static_cast<size_t>(focus_)].channel;
    return i < 0 ? nullptr : &ch_->all[static_cast<size_t>(i)];
  }

  void moveTo(int target, int step) {
    int n = static_cast<int>(lines_.size());
    if (n == 0) return;
    target = std::clamp(target, 0, n - 1);
    while (target >= 0 && target < n && lines_[static_cast<size_t>(target)].channel < 0) target += step;
    if (target < 0 || target >= n) return;
    focus_ = target;
  }

  void rebuild() {
    std::string keep;
    if (const GuideChannel* g = focused()) keep = g->id;
    ch_ = app_.store().channels();
    lines_.clear();
    sections_.clear();
    // The list the channel was picked from; the whole lineup when there is none.
    std::vector<std::string> ids = app_.playback().flipList();
    if (ids.empty()) {
      const std::vector<int>& rows = ch_->lineupConfigured ? ch_->lineup : ch_->browse;
      for (int i : rows) ids.push_back(ch_->all[static_cast<size_t>(i)].id);
    }
    engine::views::List list = engine::views::grouped(*ch_, ids);
    float y = 0;
    for (const engine::views::Line& l : list.lines) {
      if (l.kind == engine::views::Line::Kind::Header) {
        std::string up = l.title;
        for (char& chr : up) chr = static_cast<char>(chr >= 'a' && chr <= 'z' ? chr - 32 : chr);
        lines_.push_back({-1, up, y});
        y += kHeaderH;
      } else {
        lines_.push_back({l.channel, "", y});
        y += kRowH;
      }
    }
    for (const engine::views::Section& sec : list.sections) sections_.push_back({sec.title, sec.firstLine});
    focus_ = -1;
    for (size_t i = 0; i < lines_.size(); ++i) {
      if (lines_[i].channel >= 0 && ch_->all[static_cast<size_t>(lines_[i].channel)].id == keep) focus_ = static_cast<int>(i);
    }
    if (focus_ < 0) moveTo(0, 1);
  }

  void drawHoods(ui::Ctx& c) {
    float y = kTop;
    for (size_t i = 0; i < sections_.size(); ++i) {
      Rect r{40, y, kHoodW - 30, 56};
      bool f = static_cast<int>(i) == hood_;
      c.hit(r, kHitHood + static_cast<int>(i));
      if (f) c.g.fill(r, theme::white(), 28);
      ui::labelIn(c, sections_[i].first, {r.x + 20, r.y, r.w - 40, r.h}, Weight::Bold, 22, f ? theme::black() : theme::white());
      y += 62;
      if (y > gfx::kLayoutHeight - 120) break;
    }
    c.g.fill({kHoodW + 30, kTop, 1, gfx::kLayoutHeight - kTop - 100}, theme::white(0.15f));
  }

  std::shared_ptr<const Channels> ch_;
  std::vector<Line> lines_;
  std::vector<std::pair<std::string, int>> sections_;  // title, header line
  int focus_ = -1;
  float scroll_ = 0;
  bool hoods_ = false;
  int hood_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeMiniGuide(App& app) { return std::make_unique<MiniGuide>(app); }

}  // namespace marigold::app
