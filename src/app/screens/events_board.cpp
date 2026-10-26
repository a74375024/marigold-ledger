// The Live Events board (LiveEventsBoard in tvOS): the focused game in the
// hero with its two badges large and faded behind, the guide's tab bar, then
// one row per sport of 360x210 tiles under group labels. Left from a row's
// first tile opens the sports column. Scores refresh only while this board
// (or the event guide in the player) is open.
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr float kLeft = 80;
constexpr float kTabsY = 318;
constexpr float kBoardTop = 404;
constexpr float kBoardBottom = 1010;
constexpr float kRowHeader = 44;
constexpr float kGroupLabel = 30;
constexpr float kRowGap = 26;
constexpr float kTileGap = 24;
constexpr float kGroupGap = 32;
constexpr int kHitTile = 100000;  // + row * 1000 + tile
constexpr int kHitTab = 10;
constexpr int kHitColumn = 50;

const char* kTabs[] = {"My Lineup", "Favorites", "Recent", "Live Events"};

class EventsBoardScreen : public Screen {
 public:
  EventsBoardScreen(App& app, std::function<void(int)> pickTab) : Screen(app), pickTab_(std::move(pickTab)) {
    app_.store().eventsOpen(true);
    refresh();
    // Offer My Sports once, the first time Live Events opens.
    if (app_.store().shouldPromptMySports()) {
      app_.store().markMySportsPrompted();
      promptAt_ = app_.ms() + 600;
    }
  }
  ~EventsBoardScreen() override { app_.store().eventsOpen(false); }

  void shown() override { refresh(); }

  bool tick(uint32_t ms) override {
    if (promptAt_ && ms >= promptAt_) {
      promptAt_ = 0;
      app_.nav().push(makeMySports(app_));
      return true;
    }
    if (app_.store().events()->version != version_) {
      refresh();
      app_.invalidate();
    }
    return board_->loading || promptAt_ != 0;
  }

  bool key(const KeyEvent& e) override {
    if (column_) return columnKey(e);
    if (tabs_) return tabsKey(e);
    if (e.longPress) {
      if (const EventTile* t = focused()) openMenu(*t);
      return true;
    }
    switch (e.key) {
      case Key::Up:
        if (row_ == 0 || board_->rows.empty()) {
          tabs_ = true;
          tab_ = 3;
        } else {
          setRow(row_ - 1);
        }
        return true;
      case Key::Down:
        if (row_ + 1 < static_cast<int>(board_->rows.size())) setRow(row_ + 1);
        return true;
      case Key::Left:
        if (tile_ > 0) --tile_;
        else if (!board_->rows.empty()) {
          column_ = true;
          columnRow_ = row_;
          columnReturn_ = row_;
        }
        return true;
      case Key::Right:
        if (tile_ + 1 < static_cast<int>(flat(row_).size())) ++tile_;
        return true;
      case Key::Ok:
        if (const EventTile* t = focused()) select(*t);
        return true;
      case Key::Red:
        if (const EventTile* t = focused()) app_.nav().push(makeWhereToWatch(app_, t->id));
        return true;
      case Key::Yellow:
      case Key::Back:
        close(0);
        return true;
      case Key::Blue:
        app_.nav().push(makeOptions(app_));
        return true;
      default:
        return true;
    }
  }

  void hover(int id) override {
    if (id >= kHitTile) {
      int r = (id - kHitTile) / 1000, t = (id - kHitTile) % 1000;
      if (r < static_cast<int>(board_->rows.size())) {
        row_ = r;
        tile_ = t;
        tabs_ = column_ = false;
      }
    } else if (id >= kHitColumn && column_) {
      columnRow_ = std::clamp(id - kHitColumn, 0, static_cast<int>(board_->rows.size()) - 1);
    } else if (id >= kHitTab && id < kHitTab + 4) {
      tabs_ = true;
      tab_ = id - kHitTab;
    }
  }

  void draw(ui::Ctx& c) override {
    const EventTile* f = focused();
    if (f) events_ui::backdrop(c, *f);
    drawHero(c, f);
    drawTabs(c);
    drawRows(c);
    if (column_) drawColumn(c);
    ui::keyBar(c,
               {{theme::kKeyRed, "Where to Watch"}, {theme::kKeyYellow, "Guide"}, {theme::kKeyBlue, "Options"}},
               "Hold OK: more \xc2\xb7 \xe2\x97\x80 sports");
  }

 private:
  void refresh() {
    board_ = app_.store().events();
    version_ = board_->version;
    // Keep the focus on the same game when it is still there.
    if (!focusId_.empty()) {
      for (size_t r = 0; r < board_->rows.size(); ++r) {
        std::vector<const EventTile*> tiles = flat(static_cast<int>(r));
        for (size_t t = 0; t < tiles.size(); ++t) {
          if (tiles[t]->id == focusId_ && static_cast<int>(r) == row_) {
            tile_ = static_cast<int>(t);
            return;
          }
        }
      }
    }
    row_ = std::clamp(row_, 0, std::max(0, static_cast<int>(board_->rows.size()) - 1));
    tile_ = std::clamp(tile_, 0, std::max(0, static_cast<int>(flat(row_).size()) - 1));
  }

  std::vector<const EventTile*> flat(int row) const {
    std::vector<const EventTile*> out;
    if (row < 0 || row >= static_cast<int>(board_->rows.size())) return out;
    for (const EventGroup& g : board_->rows[static_cast<size_t>(row)].groups)
      for (const EventTile& t : g.tiles) out.push_back(&t);
    return out;
  }

  const EventTile* focused() {
    std::vector<const EventTile*> tiles = flat(row_);
    if (tiles.empty()) return nullptr;
    tile_ = std::clamp(tile_, 0, static_cast<int>(tiles.size()) - 1);
    focusId_ = tiles[static_cast<size_t>(tile_)]->id;
    return tiles[static_cast<size_t>(tile_)];
  }

  void setRow(int r) {
    // Keep roughly the same column when moving between rows.
    float x = tileX(row_, tile_);
    row_ = r;
    std::vector<const EventTile*> tiles = flat(row_);
    int best = 0;
    float bestDist = 1e9f;
    for (size_t i = 0; i < tiles.size(); ++i) {
      float d = std::fabs(tileX(row_, static_cast<int>(i)) - x);
      if (d < bestDist) {
        bestDist = d;
        best = static_cast<int>(i);
      }
    }
    tile_ = best;
  }

  // Tile x before scrolling: tiles 24 apart, an extra 32 between groups.
  float tileX(int row, int index) const {
    if (row < 0 || row >= static_cast<int>(board_->rows.size())) return 0;
    float x = 0;
    int i = 0;
    bool firstGroup = true;
    for (const EventGroup& g : board_->rows[static_cast<size_t>(row)].groups) {
      if (!firstGroup) x += kGroupGap;
      firstGroup = false;
      for (size_t k = 0; k < g.tiles.size(); ++k) {
        if (i == index) return x;
        x += events_ui::kTileW + kTileGap;
        ++i;
      }
    }
    return x;
  }

  bool tabsKey(const KeyEvent& e) {
    switch (e.key) {
      case Key::Left:
        tab_ = std::max(0, tab_ - 1);
        return true;
      case Key::Right:
        tab_ = std::min(3, tab_ + 1);
        return true;
      case Key::Down:
        tabs_ = false;
        return true;
      case Key::Ok:
        if (tab_ == 3) tabs_ = false;
        else close(tab_);
        return true;
      case Key::Back:
        close(0);
        return true;
      default:
        return true;
    }
  }

  bool columnKey(const KeyEvent& e) {
    int n = static_cast<int>(board_->rows.size());
    switch (e.key) {
      case Key::Up:
      case Key::Down:
        columnRow_ = std::clamp(columnRow_ + (e.key == Key::Up ? -1 : 1), 0, n - 1);
        row_ = columnRow_;  // the board scrolls behind the column
        tile_ = 0;
        return true;
      case Key::Right:
      case Key::Ok:
        column_ = false;
        row_ = columnRow_;
        tile_ = 0;
        return true;
      case Key::Back:
      case Key::Left:
        column_ = false;
        row_ = columnReturn_;
        tile_ = 0;
        return true;
      default:
        return true;
    }
  }

  void close(int tab) {
    auto pick = pickTab_;
    app_.nav().pop();
    if (pick) pick(tab);
  }

  void select(const EventTile& t) {
    // An event channel family: its first feed, Up/Down surfs the others.
    if (row_ < static_cast<int>(board_->rows.size()) && board_->rows[static_cast<size_t>(row_)].families) {
      std::vector<std::string> feeds = app_.store().familyFeeds(t.id);
      Playback& p = app_.playback();
      if (feeds.empty() || !p.tune(feeds.front(), feeds)) {
        app_.toast("No feeds in this family right now");
        return;
      }
      p.setFull();
      app_.nav().push(makePlayer(app_));
      return;
    }
    // Live, likely over or untimed with somewhere to watch: play. Otherwise
    // the detail card.
    if (t.watchable && !t.upcoming && !t.finished) {
      watch(t.id);
      return;
    }
    app_.nav().push(makeEventDetail(app_, t.id));
  }

  void watch(const std::string& eventId) {
    Playback& p = app_.playback();
    if (!p.tune(eventId, app_.store().eventFlipList(eventId))) {
      app_.toast("This game has no working feed right now");
      return;
    }
    p.setFull();
    app_.nav().push(makePlayer(app_));
  }

  void openMenu(const EventTile& t) {
    std::string id = t.id;
    bool upcoming = t.upcoming;
    App& app = app_;
    app_.nav().push(std::make_unique<PanelScreen>(app_, PanelScreen::Side::Trailing, 640.0f,
                                                  [&app, id, upcoming](ui::ListPanel& p) {
      p.title = "Game";
      ui::PanelItem where{ui::PanelItem::Kind::Chevron, "Where to Watch"};
      where.select = [&app, id] {
        app.nav().pop();
        app.nav().push(makeWhereToWatch(app, id));
      };
      p.items.push_back(where);
      if (upcoming) {
        bool set = app.store().hasReminder(id);
        ui::PanelItem remind{ui::PanelItem::Kind::Action, set ? "Remove Reminder" : "Remind Me"};
        remind.select = [&app, id] {
          bool on = app.store().toggleReminder(id);
          app.toast(on ? "We'll show a card when it starts" : "Reminder removed");
        };
        p.items.push_back(remind);
      }
    }));
  }

  // ---------------------------------------------------------------- drawing

  void drawHero(ui::Ctx& c, const EventTile* t) {
    float x = kLeft, y = 44;
    if (!t) {
      ui::label(c, "Live Events", x, y + 40, Weight::Bold, 44, theme::white());
      ui::label(c, board_->loading ? "Loading this week's games\xe2\x80\xa6" : "This week's games, and where you can watch them.",
                x, y + 110, Weight::Regular, 26, theme::white(0.7f));
      if (board_->loading) ui::spinner(c, x + 20, y + 190, 16);
      ui::label(c, ui::clock(c, c.now), 1440, 40, Weight::Medium, 26, theme::white(0.7f), 400, Align::Right);
      return;
    }
    float cx = x;
    if (!t->leagueChip.empty()) cx += ui::chip(c, t->leagueChip, cx, y, 20, theme::white(0.14f), theme::white()) + 16;
    ui::label(c, ui::clock(c, c.now), cx, y + 2, Weight::Medium, 24, theme::white(0.7f));
    y += 52;
    float tx = x;
    if (t->sides.size() == 2) {
      events_ui::badge(c, t->sides[0], tx, y, 52, false);
      events_ui::badge(c, t->sides[1], tx + 64, y, 52, false);
      tx += 140;
    }
    ui::label(c, t->heroTitle.empty() ? t->title : t->heroTitle, tx, y + 4, Weight::Bold, 40, theme::white(), 1240 - tx);
    y += 72;
    float sx = x;
    sx += events_ui::pill(c, *t, sx, y + 4, false) ;
    if (sx > x) sx += 12;
    ui::label(c, t->heroStatus.empty() ? t->status : t->heroStatus, sx, y, Weight::Medium, 24, theme::white(), 1240 - sx);
    y += 44;
    if (t->progress >= 0) {
      ui::progress(c, {x, y, 420, 6}, t->progress);
      y += 22;
    }
    float wx = x;
    if (!t->channelLogo.empty() || !t->channelName.empty()) {
      ui::logo(c, t->channelLogo, t->channelName, {wx, y, 80, 32});
      wx += 96;
    }
    ui::label(c, t->whereLine, wx, y + 2, Weight::Regular, 22, theme::white(0.8f), 1240 - wx);
  }

  void drawTabs(ui::Ctx& c) {
    float x = kLeft;
    for (int i = 0; i < 4; ++i) {
      float w = ui::textWidth(c, kTabs[i], Weight::Bold, 27);
      bool focusedTab = tabs_ && tab_ == i;
      Rect hit{x - 16, kTabsY, w + 32, 60};
      c.hit(hit, kHitTab + i);
      if (focusedTab) c.g.fill(hit, theme::white(), 30);
      Color col = focusedTab ? theme::black() : i == 3 ? theme::white() : theme::white(0.55f);
      ui::labelIn(c, kTabs[i], {x, kTabsY, w, 54}, Weight::Bold, 27, col);
      if (i == 3 && !focusedTab) c.g.fill({x, kTabsY + 52, w, 5}, theme::white(), 2.5f);
      x += w + 44;
    }
  }

  float rowHeight(const EventRow& r) const {
    bool labels = std::any_of(r.groups.begin(), r.groups.end(), [](const EventGroup& g) { return !g.title.empty(); });
    return kRowHeader + (labels ? kGroupLabel : 0) + events_ui::kTileH + kRowGap;
  }

  void drawRows(ui::Ctx& c) {
    if (board_->rows.empty()) {
      ui::label(c, board_->loading ? "Loading this week's games\xe2\x80\xa6" : "No games this week in your sports.", kLeft,
                kBoardTop + 30, Weight::Medium, 26, theme::white(0.6f));
      return;
    }
    // The focused row sits at the top of the board area.
    float y = kBoardTop;
    float scroll = 0;
    for (int r = 0; r < row_; ++r) scroll += rowHeight(board_->rows[static_cast<size_t>(r)]);
    c.g.pushClip({0, kBoardTop - 10, gfx::kLayoutWidth, kBoardBottom - kBoardTop + 10});
    y -= scroll;
    for (size_t r = 0; r < board_->rows.size(); ++r) {
      const EventRow& row = board_->rows[r];
      float h = rowHeight(row);
      if (y > kBoardBottom) break;
      if (y + h >= kBoardTop) drawRow(c, row, static_cast<int>(r), y);
      y += h;
    }
    c.g.popClip();
  }

  void drawRow(ui::Ctx& c, const EventRow& row, int r, float y) {
    float hx = kLeft;
    hx += ui::label(c, row.title, hx, y, Weight::Bold, 25, theme::white()) + 10;
    std::string summary = "\xc2\xb7 " + std::to_string(row.count);
    if (row.live > 0) {
      hx += ui::label(c, summary, hx, y + 3, Weight::Medium, 22, theme::white(0.6f)) + 10;
      ui::label(c, "\xc2\xb7 " + std::to_string(row.live) + " live", hx, y + 3, Weight::Medium, 22, theme::hex(0xF4B4AE));
    } else {
      ui::label(c, summary, hx, y + 3, Weight::Medium, 22, theme::white(0.6f));
    }
    bool labels = std::any_of(row.groups.begin(), row.groups.end(), [](const EventGroup& g) { return !g.title.empty(); });
    float tileY = y + kRowHeader + (labels ? kGroupLabel : 0);
    // Horizontal scroll: the focused tile stays in view.
    float scrollX = 0;
    if (r == row_) {
      float fx = tileX(r, tile_);
      float visible = gfx::kLayoutWidth - 2 * kLeft;
      if (fx + events_ui::kTileW > visible) scrollX = fx + events_ui::kTileW - visible + 40;
      rowScroll_ = scrollX;
    } else if (r == row_ - 1 || r == row_ + 1) {
      scrollX = 0;
    }
    int index = 0;
    float x = 0;
    bool firstGroup = true;
    for (const EventGroup& g : row.groups) {
      if (!firstGroup) x += kGroupGap;
      firstGroup = false;
      float gx = kLeft + x - scrollX;
      if (!g.title.empty() && gx > -400 && gx < gfx::kLayoutWidth)
        ui::label(c, g.title, gx, y + kRowHeader, Weight::Bold, 20, theme::white(0.75f));
      for (const EventTile& t : g.tiles) {
        float tx = kLeft + x - scrollX;
        if (tx + events_ui::kTileW > 0 && tx < gfx::kLayoutWidth) {
          bool f = !tabs_ && !column_ && r == row_ && index == tile_;
          Rect rect{tx, tileY, events_ui::kTileW, events_ui::kTileH};
          c.hit(rect, kHitTile + r * 1000 + std::min(index, 999));
          events_ui::tile(c, t, rect, f, mixed(row));
        }
        x += events_ui::kTileW + kTileGap;
        ++index;
      }
    }
  }

  // A row with more than one league names the league on each tile.
  static bool mixed(const EventRow& row) {
    std::string first;
    for (const EventGroup& g : row.groups)
      for (const EventTile& t : g.tiles) {
        if (first.empty()) first = t.league;
        else if (t.league != first) return true;
      }
    return false;
  }

  void drawColumn(ui::Ctx& c) {
    float h = std::min(640.0f, 70.0f + 60.0f * static_cast<float>(board_->rows.size()));
    Rect r{kLeft - 10, kBoardTop - 20, 400, h};
    c.g.fill(r, theme::hex(0x1C1F25, 0.97f), theme::kPanelRadius);
    ui::label(c, "SPORTS", r.x + 24, r.y + 20, Weight::Bold, 16, theme::white(0.55f));
    float y = r.y + 56;
    int visible = static_cast<int>((r.h - 70) / 60);
    int first = std::clamp(columnRow_ - visible / 2, 0, std::max(0, static_cast<int>(board_->rows.size()) - visible));
    for (int i = first; i < static_cast<int>(board_->rows.size()) && i < first + visible; ++i) {
      const EventRow& row = board_->rows[static_cast<size_t>(i)];
      Rect pill{r.x + 14, y, r.w - 28, 54};
      bool f = i == columnRow_;
      c.hit(pill, kHitColumn + i);
      if (f) c.g.fill(pill, theme::white(), 27);
      Color fg = f ? theme::black() : theme::white();
      ui::labelIn(c, row.title, {pill.x + 20, pill.y, pill.w - 150, pill.h}, Weight::Bold, 22, fg);
      std::string right = std::to_string(row.count);
      ui::labelIn(c, right, {pill.x, pill.y, pill.w - 20, pill.h}, Weight::Medium, 19, f ? theme::black(0.6f) : theme::white(0.6f),
                  Align::Right);
      if (row.live > 0)
        ui::labelIn(c, std::to_string(row.live) + " live", {pill.x, pill.y, pill.w - 70, pill.h}, Weight::Bold, 18,
                    theme::hex(theme::kWatching), Align::Right);
      y += 60;
    }
  }

  uint32_t promptAt_ = 0;
  std::function<void(int)> pickTab_;
  std::shared_ptr<const EventsBoard> board_;
  uint64_t version_ = ~0ull;
  int row_ = 0, tile_ = 0;
  std::string focusId_;
  bool tabs_ = false;
  int tab_ = 3;
  bool column_ = false;
  int columnRow_ = 0, columnReturn_ = 0;
  float rowScroll_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeEventsBoard(App& app, std::function<void(int)> pickTab) {
  return std::make_unique<EventsBoardScreen>(app, std::move(pickTab));
}

}  // namespace marigold::app
