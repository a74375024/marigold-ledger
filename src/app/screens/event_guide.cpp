// The event guide (LiveEventGuide), the games rail: Left in the player
// while watching a game, or Live Events picked in the guide's lists. The
// other games of a sport you can watch (live, final, starting soon) with
// clock and channel, never a score; OK switches games. Left opens the
// SPORTS column and Left again the lists, as in the guide in the player
// (Build 116: core/input/player_rail, the column's look and its return
// rule); a game's other channels and feeds are under the banner's
// Sources. The rail is narrower than the guide's card, so more of the
// picture shows; the column widens it while it's open. Built from the
// board's rows as the board screen shows them (Build 119: Show on the
// board), with the row of the game on screen kept: indices into the board,
// never a copy of it, and no requests of its own. Search, the lists' first
// (Build 132), opens the guide in the player on search.
//
// Live Events picked in the lists (Build 141) opens as My Lineup and
// Favorites do: on SPORTS, the focus there and the games following it
// behind; OK or Right on a sport goes to its games. The board is often
// still loading then (opening Live Events shows placeholders until a fresh
// board is in): SPORTS waits for its first sports and opens on them, and a
// new board keeps an open SPORTS open on the same sports (it used to close
// it, which left the viewer in the first sport's games).
#include <algorithm>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/left_rail.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/engine/views.hpp"
#include "core/input/hints.hpp"
#include "core/input/player_rail.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr float kRailW = 600;
constexpr float kSportsW = left_rail::kColumnW + 32;  // the column open: 272
constexpr float kRowH = 96, kHeaderH = 40, kTop = 110;
constexpr float kTopY = 52, kColumnTop = 120, kColumnFirst = 164;
constexpr float kLegendY = gfx::kLayoutHeight - ui::kCardInset - 42;
constexpr float kVisible = kLegendY - 30 - kTop;  // the games' height on screen

// Pointer targets (the column's and the lists' are left_rail::kHit*).
constexpr int kHitBeside = 90, kHitLists = 91, kHitTopLine = 92;
constexpr int kHitGame = 1000;  // + item

bool inRange(int id, int base, int n) { return id >= base && id < base + n; }

std::string upper(std::string s) {
  for (char& ch : s) ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
  return s;
}

class EventGuide : public Screen {
 public:
  EventGuide(App& app, bool sports) : Screen(app), nav_(rail::Item::Events) {
    app_.store().eventsOpen(true);
    const auto& s = app_.store().settings();
    nav_.setLists(rail::playerLists(s.allChannelsTab, s.scheduleEnabled));
    visible(app_.store().events());
    placeOnWatching();
    // From the lists: SPORTS first, or as soon as the loading board has
    // sports.
    if (sports) openSports();
  }
  ~EventGuide() override { app_.store().eventsOpen(false); }

  bool opaque() const override { return false; }

  const char* name() const override { return "event guide"; }

  bool tick(uint32_t ms) override {
    if (app_.store().events()->version != board_->version) {
      // A new board: the same sport and game found again by their ids.
      std::string keep = focusedTile() ? focusedTile()->id : std::string();
      std::string rowId = rowIdAt(rowIndex_), originId = rowIdAt(nav_.origin()), indexId = rowIdAt(nav_.index());
      int origin = nav_.origin(), index = nav_.index();
      bool had = rowCount() > 0;
      visible(app_.store().events());
      if (!had) {
        // The first rows in (the board was loading): as on opening.
        placeOnWatching();
      } else {
        rowIndex_ = rowOf(rowId, rowIndex_);
        build();
        focus_ = -1;
        for (size_t i = 0; i < items_.size(); ++i)
          if (items_[i].tile && items_[i].tile->id == keep) focus_ = static_cast<int>(i);
        if (focus_ < 0) move(0, 1);
        follow();
      }
      // SPORTS: opens when it was waited for, stays open on the same
      // sports (the games following its focus), closes with no rows left.
      apply(nav_.entries(rowOf(originId, origin), rowOf(indexId, index), static_cast<int>(rowCount()), board_->loading));
    }
    return push_.moving(ms) || lists_.moving(ms);
  }

  bool key(const KeyEvent& e) override {
    if (e.key != Key::Ok) pointer_ = 0;
    if (e.key == Key::Ok && pointer_ && click()) return true;
    if (e.key == Key::Guide) {
      app_.nav().pop();
      return true;
    }
    // Every game is at the rail's left edge: Left opens SPORTS.
    rail::Grid g{true, false, false, rowIndex_, static_cast<int>(rowCount())};
    rail::PlayerStep s = nav_.key(e.key, g, e.glide);
    if (s.what != rail::PlayerStep::Do::Grid) {
      apply(s);
      return true;
    }
    switch (e.key) {
      case Key::Up:
        move(focus_ - 1, -1);
        return true;
      case Key::Down:
        move(focus_ + 1, 1);
        return true;
      case Key::Ok: watch(); return true;
      default: return true;
    }
  }

  void hover(int id) override {
    pointer_ = id;
    // The column and the lists highlight what the pointer is on.
    if (nav_.mode() != rail::Mode::Content) return;
    if (inRange(id, kHitGame, static_cast<int>(items_.size())) && items_[static_cast<size_t>(id - kHitGame)].tile)
      focus_ = id - kHitGame;
  }

  void pointerGone() override { pointer_ = 0; }

  void draw(ui::Ctx& c) override {
    float push = push_.at(c.ms);
    float cardW = kRailW + kSportsW * push;
    Rect card{ui::kCardInset, ui::kCardInset, cardW, gfx::kLayoutHeight - 2 * ui::kCardInset};
    ui::sidePanel(c, {0, 0, cardW + ui::kCardInset, gfx::kLayoutHeight}, true);
    c.hit({card.x + card.w, 0, gfx::kLayoutWidth - card.x - card.w, gfx::kLayoutHeight}, kHitBeside);
    c.hit(card, left_rail::kHitNothing);
    rail::Mode mode = nav_.mode();
    float x = ui::kCardInset + 38 + kSportsW * push;
    std::string title = rowIndex_ < static_cast<int>(rowCount()) ? rowAt(static_cast<size_t>(rowIndex_)).title : "";
    float right = card.x + card.w - 40;
    drawTop(c, "LIVE EVENTS \xc2\xb7 " + upper(title), x, right - 220 - x);
    ui::labelIn(c, ui::clock(c, c.now), {right - 200, kTopY - 4, 200, 40}, Weight::Medium, 26, theme::white(0.7f),
                Align::Right);
    if (push > 0.01f) drawSports(c, push);
    drawList(c, x, mode == rail::Mode::Content ? 1.0f : theme::kInactiveOpacity);
    const RemoteKeys& keys = app_.remoteKeys();
    if (mode != rail::Mode::Menu) {
      hints::PlayerGuide place = mode == rail::Mode::Column ? hints::PlayerGuide::Sports : hints::PlayerGuide::Games;
      ui::label(c, hints::playerGuide(keys, place, true, c.pointerActive), ui::kCardInset + 38, kLegendY,
                Weight::Regular, 22, theme::white(0.75f), cardW - 76);
    }
    float lists = lists_.at(c.ms);
    if (lists > 0.01f) {
      int hover = c.pointerActive && inRange(pointer_, left_rail::kHitMenu, 100) ? pointer_ - left_rail::kHitMenu : -1;
      bool back = nav_.fromColumn();
      left_rail::lists(c, card, nav_.lists(), nav_.list(), nav_.listFocus(), hover,
                       back ? "Back: sports" : "Back: the games", lists);
      if (mode == rail::Mode::Menu)
        ui::label(c, hints::playerGuide(keys, hints::PlayerGuide::GameLists, back), card.x + left_rail::kListsW + 40,
                  kLegendY, Weight::Regular, 22, theme::white(0.75f), cardW - left_rail::kListsW - 80);
    }
  }

 private:
  struct Item {
    std::string header;
    const EventTile* tile = nullptr;
    float y = 0;
  };
  // Where the focus was when SPORTS opened (the return rule): its sport by
  // id too, so a new board meanwhile finds it again.
  struct Place {
    int row = 0;
    std::string rowId, id;
    float scroll = 0;
  };

  const EventTile* focusedTile() const {
    return focus_ >= 0 && focus_ < static_cast<int>(items_.size()) ? items_[static_cast<size_t>(focus_)].tile : nullptr;
  }

  void move(int target, int step) {
    int n = static_cast<int>(items_.size());
    target = std::clamp(target, 0, std::max(0, n - 1));
    while (target >= 0 && target < n && !items_[static_cast<size_t>(target)].tile) target += step;
    if (target >= 0 && target < n) focus_ = target;
    follow();
  }
  // The list scrolls to keep a row around the focus in view (keys only:
  // the pointer's hover leaves it where it is).
  void follow() {
    float fy = focus_ >= 0 && focus_ < static_cast<int>(items_.size()) ? items_[static_cast<size_t>(focus_)].y : 0;
    if (fy - kRowH < scroll_) scroll_ = std::max(0.0f, fy - kRowH);
    if (fy + 2 * kRowH > scroll_ + kVisible) scroll_ = fy + 2 * kRowH - kVisible;
  }

  void build() {
    items_.clear();
    float y = 0;
    size_t at = static_cast<size_t>(rowIndex_);
    size_t row = at < rowCount() ? shown_[at] : board_->rows.size();
    for (const engine::views::EventLine& l : engine::views::eventGuide(*board_, row)) {
      items_.push_back({l.header, l.tile, y});
      y += l.tile ? kRowH : kHeaderH;
    }
  }

  // The row of the game on screen (else the first) and that game focused,
  // as the rail opens.
  void placeOnWatching() {
    const std::string& watching = app_.playback().entry();
    rowIndex_ = -1;
    for (size_t r = 0; r < rowCount() && rowIndex_ < 0; ++r) {
      const EventRow& row = rowAt(r);
      if (row.families) continue;
      for (const EventGroup& g : row.groups)
        for (const EventTile& t : g.tiles)
          if (t.id == watching && row.id != "live" && row.id != "teams") rowIndex_ = static_cast<int>(r);
    }
    if (rowIndex_ < 0) rowIndex_ = 0;
    build();
    focus_ = -1;
    scroll_ = 0;
    for (size_t i = 0; i < items_.size(); ++i)
      if (items_[i].tile && items_[i].tile->id == watching) focus_ = static_cast<int>(i);
    if (focus_ < 0) move(0, 1);
    follow();
  }

  // SPORTS, as the lists open My Lineup on its neighborhoods: on the
  // sport of the game on screen (else the first), the focus there. A board
  // still loading has no sports yet: it opens on the first ones in.
  void openSports() { apply(nav_.openColumn(rowIndex_, static_cast<int>(rowCount()), board_->loading)); }

  // A shown row's id ("" for none), and the shown row with `id` (else
  // `fallback`, kept in range).
  std::string rowIdAt(int r) const {
    return r >= 0 && r < static_cast<int>(rowCount()) ? rowAt(static_cast<size_t>(r)).id : std::string();
  }
  int rowOf(const std::string& id, int fallback) const {
    if (!id.empty())
      for (size_t r = 0; r < rowCount(); ++r)
        if (rowAt(r).id == id) return static_cast<int>(r);
    return std::clamp(fallback, 0, std::max(0, static_cast<int>(rowCount()) - 1));
  }

  // A sport's games, from its first.
  void showRow(int row) {
    rowIndex_ = std::clamp(row, 0, std::max(0, static_cast<int>(rowCount()) - 1));
    build();
    focus_ = -1;
    scroll_ = 0;
    move(0, 1);
  }

  void watch() {
    if (const EventTile* t = focusedTile()) {
      Playback& p = app_.playback();
      if (t->id != p.entry()) p.tune(t->id, app_.store().eventFlipList(t->id));
      app_.nav().pop();
    }
  }

  // ---------------------------------------------------------- the rail
  void apply(const rail::PlayerStep& s) {
    using Do = rail::PlayerStep::Do;
    switch (s.what) {
      case Do::Grid:
      case Do::None:
      case Do::Digits:
      case Do::Now:
      case Do::Results:
      case Do::Keyboard:
      case Do::Leave: break;
      case Do::Search:
        // Search, the lists' first (Build 132): the guide in the player as
        // search, as the other lists open it.
        app_.nav().pop();
        app_.nav().push(makeMiniGuide(app_, rail::Item::Search));
        return;
      case Do::Column:
        place_ = {rowIndex_, rowIdAt(rowIndex_), focusedTile() ? focusedTile()->id : std::string(), scroll_};
        break;
      // The games follow the column; on the sport it opened on, as they were.
      case Do::Scroll:
        if (s.index == nav_.origin()) restorePlace();
        else showRow(s.index);
        break;
      case Do::Restore: restorePlace(); break;
      case Do::Top: showRow(s.index); break;
      case Do::Open:
        if (s.item == rail::Item::Events) {
          // This list: its SPORTS again.
          openSports();
          return;
        }
        // A channel list: the guide in the player, on its neighborhoods.
        app_.nav().pop();
        app_.nav().push(makeMiniGuide(app_, s.item));
        return;
      case Do::Close: app_.nav().pop(); return;
    }
    syncSlides();
  }

  void restorePlace() {
    showRow(rowOf(place_.rowId, place_.row));
    for (size_t i = 0; i < items_.size(); ++i)
      if (items_[i].tile && items_[i].tile->id == place_.id) {
        focus_ = static_cast<int>(i);
        scroll_ = place_.scroll;
        follow();
      }
  }

  void syncSlides() {
    uint32_t ms = app_.ms();
    // SPORTS waited for: the card widens already, its header showing.
    push_.go(nav_.mode() == rail::Mode::Column || nav_.waiting() ? 1.0f : 0.0f, ms);
    lists_.go(nav_.mode() == rail::Mode::Menu ? 1.0f : 0.0f, ms);
    app_.invalidate();
  }

  // A click: true when it was taken here; on a game it is OK on the game
  // hovering focused.
  bool click() {
    int id = pointer_;
    rail::Mode mode = nav_.mode();
    if (id == kHitBeside) {
      app_.nav().pop();
      return true;
    }
    if (mode == rail::Mode::Menu) {
      if (inRange(id, left_rail::kHitMenu, static_cast<int>(nav_.lists().size())))
        apply(nav_.pickList(id - left_rail::kHitMenu));
      else if (id == left_rail::kHitOutside) apply(nav_.closeLists());
      return true;
    }
    if (id == kHitTopLine && mode == rail::Mode::Content) {
      // The list name: as Left at a game (SPORTS, or the lists without).
      apply(nav_.edge(rowIndex_, static_cast<int>(rowCount())));
      return true;
    }
    if (mode == rail::Mode::Column) {
      if (inRange(id, left_rail::kHitColumn, static_cast<int>(rowCount()))) {
        apply(nav_.clickColumn(id - left_rail::kHitColumn));
      } else if (id == kHitLists) {
        apply(nav_.openLists());
      } else if (inRange(id, kHitGame, static_cast<int>(items_.size()))) {
        // The games: the column closes on that game.
        nav_.close();
        syncSlides();
        hover(id);
      }
      return true;
    }
    return id == left_rail::kHitNothing;  // the card's blank: nothing
  }

  // ---------------------------------------------------------- drawing
  void drawList(ui::Ctx& c, float x, float alpha) {
    const float visible = kVisible;
    if (items_.empty()) {
      const char* none = board_->loading && rowCount() == 0 ? "Loading games\xe2\x80\xa6"
                                                            : "No other games you can watch in this sport.";
      ui::label(c, none, x, kTop + 20, Weight::Medium, 22, theme::white(0.6f));
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
      c.hit(r, kHitGame + static_cast<int>(i));
      // Focused: the calm focus, so the badges look the same focused or not.
      if (f) ui::calmFocus(c, r, theme::kRowRadius);
      Color ink = theme::white(f ? 1.0f : alpha * (t.finished ? 0.6f : 1));
      // A dimmed row (the rail inactive, a finished game): its badges lose
      // their colour with the text.
      bool faded = !f && ink.a < 1;
      float sx = r.x + 14;
      // The teams, never a score.
      if (t.sides.size() == 2) {
        for (int s = 0; s < 2; ++s) {
          const EventSide& side = t.sides[static_cast<size_t>(s)];
          float ly = r.y + 8 + static_cast<float>(s) * 38;
          events_ui::badge(c, side, sx, ly + 2, 30, faded);
          ui::label(c, (side.rank.empty() ? "" : side.rank + " ") + side.name, sx + 40, ly + 4, Weight::Medium, 20, ink, 250);
        }
      } else {
        ui::label(c, t.title, sx, r.y + 14, Weight::Bold, 21, ink, 290);
        ui::label(c, t.subtitle, sx, r.y + 50, Weight::Regular, 17, ink.withAlpha(ink.a * 0.7f), 290);
      }
      float ix = r.x + 322;
      // The state and status, as on the board's tiles (live: red text alone).
      events_ui::status(c, t, t.status, ix, r.y + 14, Weight::Medium, 18, ink, r.x + r.w - 14 - ix, 0, 10);
      std::string where = t.channelName.empty() ? t.footer : t.channelName;
      float ww = ui::label(c, where, ix, r.y + 50, Weight::Regular, 17, ink.withAlpha(ink.a * 0.7f), r.w - 336);
      // The game on screen: WATCHING after its channel.
      if (t.id == watching && ww < r.w - 336 - 110)
        ui::label(c, "WATCHING", ix + ww + 10, r.y + 51, Weight::Bold, 15, theme::hex(theme::kWatching));
    }
    c.g.popClip();
  }

  // The top line: the list and the sport. With the pointer, a button
  // ("◀ LIVE EVENTS · NFL") that opens SPORTS, as the guide's list name.
  void drawTop(ui::Ctx& c, const std::string& text, float x, float maxW) {
    if (c.pointerActive && nav_.mode() == rail::Mode::Content && !nav_.waiting()) {
      const char* arrow = "\xe2\x97\x80";
      float aw = ui::textWidth(c, arrow, Weight::Bold, 16);
      float w = std::min(maxW, 16 + aw + 10 + ui::textWidth(c, text, Weight::Bold, 22) + 20);
      Rect r{x - 16, kTopY - 8, w, 50};
      bool on = pointer_ == kHitTopLine;
      c.hit(r, kHitTopLine);
      if (on) c.g.fill(r, theme::white(), 25);
      else c.g.stroke(r, theme::white(0.35f), 25, 2);
      Color col = on ? theme::black() : theme::white(0.85f);
      ui::labelIn(c, arrow, {x, r.y, aw, r.h}, Weight::Bold, 16, col);
      ui::labelIn(c, text, {x + aw + 10, r.y, w - aw - 36, r.h}, Weight::Bold, 22, col);
    } else {
      ui::labelIn(c, text, {x, kTopY, maxW, 34}, Weight::Bold, 22, theme::white(0.7f));
    }
  }

  // SPORTS inside the card: the guide's column, each sport and how many of
  // its games are live, with "◀ Lists" in its header.
  void drawSports(ui::Ctx& c, float alpha) {
    std::vector<left_rail::Entry> entries;
    entries.reserve(rowCount());
    for (size_t r : shown_) entries.push_back({board_->rows[r].title, board_->rows[r].live});
    int hover = c.pointerActive && inRange(pointer_, left_rail::kHitColumn, 100) ? pointer_ - left_rail::kHitColumn : -1;
    float x = ui::kCardInset + 40;
    // Waited for: no sports yet, they are on their way.
    bool waiting = nav_.mode() != rail::Mode::Column && nav_.waiting();
    left_rail::column(c, "Live Events", "", entries, waiting ? -1 : nav_.index(), waiting ? -1 : nav_.origin(), hover,
                      kColumnTop, kColumnFirst, alpha, x);
    if (waiting)
      ui::label(c, "Loading sports\xe2\x80\xa6", x, kColumnFirst + 14, Weight::Medium, 20, theme::white(0.5f * alpha));
    else
      left_rail::listsButton(c, x, kColumnTop, c.pointerActive && pointer_ == kHitLists, alpha, kHitLists);
  }

  // The board as the board screen shows it (Show on the board), keeping
  // the row of the game on screen: the board itself (the store frees it on
  // a worker), and the indices of the rows shown.
  void visible(std::shared_ptr<const EventsBoard> board) {
    board_ = std::move(board);
    shown_ = engine::views::boardShownRows(board_->rows, boardShows(app_.store().settings()), app_.playback().entry());
  }
  size_t rowCount() const { return shown_.size(); }
  const EventRow& rowAt(size_t r) const { return board_->rows[shown_[r]]; }

  std::shared_ptr<const EventsBoard> board_;
  std::vector<size_t> shown_;  // rows of board_ shown, in order
  int rowIndex_ = -1;
  std::vector<Item> items_;
  int focus_ = -1;
  float scroll_ = 0;
  rail::PlayerNav nav_;
  Place place_;
  left_rail::Slide push_, lists_;  // SPORTS widening the card, the lists' slide
  int pointer_ = 0;  // what the pointer is on (a hit id) while it shows
};

}  // namespace

std::unique_ptr<Screen> makeEventGuide(App& app, bool sports) { return std::make_unique<EventGuide>(app, sports); }

}  // namespace marigold::app
