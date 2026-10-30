// The event guide (LiveEventGuide): Left in the player while watching a
// game. The other games of that sport (live, final, starting soon) with
// clock, score and channel; OK switches games, Left opens the sports column
// (a game's other channels and feeds are under the banner's Sources). The
// rail is as narrow as the mini guide; the sports column widens it while
// it's open. Built from the board's
// rows: no requests of its own, but scores keep refreshing while it's open.
#include <algorithm>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/engine/views.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr float kRailW = 600, kSportsW = 270;
constexpr float kRowH = 96, kHeaderH = 40, kTop = 110;

class EventGuide : public Screen {
 public:
  explicit EventGuide(App& app) : Screen(app) {
    app_.store().eventsOpen(true);
    board_ = app_.store().events();
    // The row of the game on screen.
    const std::string& watching = app_.playback().entry();
    for (size_t r = 0; r < board_->rows.size() && rowIndex_ < 0; ++r) {
      if (board_->rows[r].families) continue;
      for (const EventGroup& g : board_->rows[r].groups)
        for (const EventTile& t : g.tiles)
          if (t.id == watching && board_->rows[r].id != "live" && board_->rows[r].id != "teams") rowIndex_ = static_cast<int>(r);
    }
    if (rowIndex_ < 0) rowIndex_ = 0;
    build();
    for (size_t i = 0; i < items_.size(); ++i)
      if (items_[i].tile && items_[i].tile->id == watching) focus_ = static_cast<int>(i);
    if (focus_ < 0) move(0, 1);
  }
  ~EventGuide() override { app_.store().eventsOpen(false); }

  bool opaque() const override { return false; }

  bool tick(uint32_t) override {
    if (app_.store().events()->version != board_->version) {
      std::string keep = focusedTile() ? focusedTile()->id : std::string();
      board_ = app_.store().events();
      build();
      focus_ = -1;
      for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i].tile && items_[i].tile->id == keep) focus_ = static_cast<int>(i);
      if (focus_ < 0) move(0, 1);
    }
    return false;
  }

  bool key(const KeyEvent& e) override {
    if (sports_) {
      int n = static_cast<int>(board_->rows.size());
      switch (e.key) {
        case Key::Up:
        case Key::Down:
          sport_ = std::clamp(sport_ + (e.key == Key::Up ? -1 : 1), 0, n - 1);
          rowIndex_ = sport_;  // the list follows the column
          build();
          focus_ = -1;
          move(0, 1);
          return true;
        case Key::Right:
        case Key::Ok:
          sports_ = false;
          return true;
        case Key::Back:
        case Key::Left:
          sports_ = false;
          return true;
        default:
          return true;
      }
    }
    switch (e.key) {
      case Key::Up:
        move(focus_ - 1, -1);
        return true;
      case Key::Down:
        move(focus_ + 1, 1);
        return true;
      case Key::Left:
        sports_ = true;
        sport_ = rowIndex_;
        return true;
      case Key::Ok:
        if (const EventTile* t = focusedTile()) {
          Playback& p = app_.playback();
          if (t->id != p.entry()) p.tune(t->id, app_.store().eventFlipList(t->id));
          app_.nav().pop();
        }
        return true;
      case Key::Guide:
        app_.nav().pop();
        return true;
      default:
        return e.key != Key::Back;
    }
  }

  void draw(ui::Ctx& c) override {
    float cardW = kRailW + (sports_ ? kSportsW : 0);
    ui::sidePanel(c, {0, 0, cardW + ui::kCardInset, gfx::kLayoutHeight}, true);
    float x = ui::kCardInset + 38;
    std::string title = rowIndex_ < static_cast<int>(board_->rows.size()) ? board_->rows[static_cast<size_t>(rowIndex_)].title : "";
    ui::label(c, "LIVE EVENTS \xc2\xb7 " + title + " \xc2\xb7 " + ui::clock(c, c.now), x, 52, Weight::Bold, 18,
              theme::white(0.55f), cardW - 76);
    if (sports_) {
      drawSports(c, ui::kCardInset + 30);
      x += kSportsW;
    }
    drawList(c, x, sports_ ? theme::kInactiveOpacity : 1.0f);
    ui::label(c,
              sports_ ? "\xe2\x96\xb2\xe2\x96\xbc sports \xc2\xb7 OK: its games \xc2\xb7 \xe2\x97\x80 or Back: close"
                      : "\xe2\x96\xb2\xe2\x96\xbc games \xc2\xb7 OK: switch \xc2\xb7 \xe2\x97\x80 sports",
              ui::kCardInset + 38, gfx::kLayoutHeight - 62, Weight::Medium, 17, theme::white(0.55f), cardW - 76);
  }

 private:
  struct Item {
    std::string header;
    const EventTile* tile = nullptr;
    float y = 0;
  };

  const EventTile* focusedTile() const {
    return focus_ >= 0 && focus_ < static_cast<int>(items_.size()) ? items_[static_cast<size_t>(focus_)].tile : nullptr;
  }

  void move(int target, int step) {
    int n = static_cast<int>(items_.size());
    target = std::clamp(target, 0, std::max(0, n - 1));
    while (target >= 0 && target < n && !items_[static_cast<size_t>(target)].tile) target += step;
    if (target >= 0 && target < n) focus_ = target;
  }

  void build() {
    items_.clear();
    float y = 0;
    for (const engine::views::EventLine& l : engine::views::eventGuide(*board_, static_cast<size_t>(rowIndex_))) {
      items_.push_back({l.header, l.tile, y});
      y += l.tile ? kRowH : kHeaderH;
    }
  }

  void drawList(ui::Ctx& c, float x, float alpha) {
    float visible = gfx::kLayoutHeight - kTop - 90;
    float fy = focus_ >= 0 ? items_[static_cast<size_t>(focus_)].y : 0;
    if (fy - kRowH < scroll_) scroll_ = std::max(0.0f, fy - kRowH);
    if (fy + 2 * kRowH > scroll_ + visible) scroll_ = fy + 2 * kRowH - visible;
    if (items_.empty()) {
      ui::label(c, "No other games you can watch in this sport.", x, kTop + 20, Weight::Medium, 22, theme::white(0.6f));
      return;
    }
    const std::string& watching = app_.playback().entry();
    c.g.pushClip({x - 20, kTop, kRailW - 40, visible});
    for (size_t i = 0; i < items_.size(); ++i) {
      const Item& it = items_[i];
      float y = kTop + it.y - scroll_;
      if (y > kTop + visible || y + kRowH < kTop) continue;
      if (!it.tile) {
        ui::label(c, it.header, x, y + 12, Weight::Bold, 16, theme::white(0.5f * alpha));
        continue;
      }
      const EventTile& t = *it.tile;
      bool f = static_cast<int>(i) == focus_ && alpha >= 1;
      Rect r{x - 14, y, kRailW - 72, kRowH - 6};
      if (f) c.g.fill(r, theme::white(), theme::kRowRadius);
      Color ink = f ? theme::black() : theme::white(alpha * (t.finished ? 0.6f : 1));
      float sx = r.x + 14;
      if (t.sides.size() == 2) {
        for (int s = 0; s < 2; ++s) {
          const EventSide& side = t.sides[static_cast<size_t>(s)];
          float ly = r.y + 8 + static_cast<float>(s) * 38;
          events_ui::badge(c, side, sx, ly + 2, 30, f);
          ui::label(c, (side.rank.empty() ? "" : side.rank + " ") + side.name, sx + 40, ly + 4, Weight::Medium, 20, ink, 196);
          if (!side.score.empty())
            ui::label(c, side.score, sx + 240, ly + 2, Weight::Bold, 21, ink.withAlpha(ink.a * (side.trails ? 0.55f : 1)), 52,
                      Align::Right);
        }
      } else {
        ui::label(c, t.title, sx, r.y + 14, Weight::Bold, 21, ink, 290);
        ui::label(c, t.subtitle, sx, r.y + 50, Weight::Regular, 17, ink.withAlpha(ink.a * 0.7f), 290);
      }
      float ix = r.x + 322;
      float px = ix + events_ui::pill(c, t, ix, r.y + 14, f);
      if (px > ix) px += 10;
      ui::label(c, t.status, px, r.y + 14, Weight::Medium, 18, ink, r.x + r.w - 14 - px);
      std::string where = t.channelName.empty() ? t.footer : t.channelName;
      float ww = ui::label(c, where, ix, r.y + 50, Weight::Regular, 17, ink.withAlpha(ink.a * 0.7f), r.w - 336);
      // The game on screen: WATCHING after its channel.
      if (t.id == watching && ww < r.w - 336 - 110)
        ui::label(c, "WATCHING", ix + ww + 10, r.y + 51, Weight::Bold, 15, f ? theme::black(0.7f) : theme::hex(theme::kWatching));
    }
    c.g.popClip();
  }

  void drawSports(ui::Ctx& c, float x) {
    float y = kTop;
    for (size_t i = 0; i < board_->rows.size(); ++i) {
      const EventRow& row = board_->rows[i];
      Rect r{x - 14, y, kSportsW - 30, 52};
      bool f = static_cast<int>(i) == sport_;
      if (f) c.g.fill(r, theme::white(), 26);
      ui::labelIn(c, row.title, {r.x + 18, r.y, r.w - 52, r.h}, Weight::Bold, 21, f ? theme::black() : theme::white());
      // Something live in it now: a red dot (not how many).
      if (row.live > 0) c.g.fill({r.x + r.w - 26, r.y + r.h * 0.5f - 5, 10, 10}, theme::hex(theme::kWatching), 5);
      y += 58;
      if (y > gfx::kLayoutHeight - 120) break;
    }
  }

  std::shared_ptr<const EventsBoard> board_;
  int rowIndex_ = -1;
  std::vector<Item> items_;
  int focus_ = -1;
  float scroll_ = 0;
  bool sports_ = false;
  int sport_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeEventGuide(App& app) { return std::make_unique<EventGuide>(app); }

}  // namespace marigold::app
