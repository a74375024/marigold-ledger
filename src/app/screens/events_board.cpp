// The Live Events board (LiveEventsBoard in tvOS): the focused game in the
// hero with its two badges large and faded behind, the guide's tab bar and
// round buttons (search, filter, options), then one row per sport of 360x230
// tiles under group labels. Left from a row's first tile opens the sports
// column. A live game in your channels plays in the guide's preview corner
// once focus rests on it. Scores refresh only while this board (or the
// event guide in the player) is open.
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/screens/top_bar.hpp"
#include "app/store/store.hpp"
#include "core/input/hints.hpp"
#include "core/sports/my_sports.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr float kLeft = 80;
constexpr float kTabsY = top_bar::kTabsY;
constexpr float kBoardTop = 404;
constexpr float kBoardBottom = 1010;
constexpr float kRowHeader = 44;
constexpr float kGroupLabel = 30;
constexpr float kRowGap = 26;
constexpr float kTileGap = 24;
constexpr float kGroupGap = 32;
constexpr int kHitTile = 100000;  // + row * 1000 + tile
constexpr int kHitTab = 10;
constexpr int kHitButton = 40;
constexpr int kHitColumn = 50;

const char* kTabs[] = {"My Lineup", "Favorites", "Recent", "Live Events"};

class EventsBoardScreen : public Screen {
 public:
  bool holdsOk() const override { return true; }
  EventsBoardScreen(App& app, std::function<void(int)> pickTab) : Screen(app), pickTab_(std::move(pickTab)) {
    app_.store().eventsOpen(true);
    // The guide's channel preview doesn't carry over into the board.
    stopPreview();
    refresh();
    // Offer My Sports once, the first time Live Events opens.
    if (app_.store().shouldPromptMySports()) {
      app_.store().markMySportsPrompted();
      promptAt_ = app_.ms() + 600;
    }
  }
  ~EventsBoardScreen() override { app_.store().eventsOpen(false); }

  void shown() override {
    refresh();
    // Back from the player: the game shrinks into the corner, as in the guide.
    Playback& p = app_.playback();
    if (p.active() && p.full()) {
      if (app_.store().settings().previewChannels) p.setCorner(top_bar::kPreview);
      else p.stop();
    }
  }
  bool keepsPreview() const override { return true; }

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
    bool pulse = skeletonShown_ && ui::skeletonMoved(ms, pulseStep_);
    return previewTick(ms) || pulse || promptAt_ != 0;
  }

  bool key(const KeyEvent& e) override {
    if (column_) return columnKey(e);
    if (buttons_) return buttonsKey(e);
    if (tabs_) return tabsKey(e);
    if (e.longPress) {
      if (const EventTile* t = focused()) openMenu(*t);
      return true;
    }
    switch (e.key) {
      case Key::Up:
        if (row_ == 0 || rows_.empty()) {
          tabs_ = true;
          tab_ = 3;
        } else {
          setRow(row_ - 1);
        }
        return true;
      case Key::Down:
        if (row_ + 1 < static_cast<int>(rows_.size())) setRow(row_ + 1);
        return true;
      case Key::Left:
        if (tile_ > 0) --tile_;
        else if (!rows_.empty()) {
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
      case Key::Menu:  // a remote without colour keys (Fire TV)
        if (const EventTile* t = focused()) app_.nav().push(makeWhereToWatch(app_, t->id));
        return true;
      case Key::Back:
        // A filter first, as the × on its chip says.
        if (filtered()) setFilter(Show::All, "");
        else close(0);
        return true;
      case Key::Yellow:
      case Key::PlayPause:
      case Key::Play:
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
      if (r < static_cast<int>(rows_.size())) {
        row_ = r;
        tile_ = t;
        tabs_ = buttons_ = column_ = false;
      }
    } else if (id >= kHitColumn && column_) {
      columnRow_ = std::clamp(id - kHitColumn, 0, static_cast<int>(rows_.size()) - 1);
    } else if (id >= kHitButton && id < kHitButton + 3) {
      tabs_ = buttons_ = true;
      button_ = id - kHitButton;
    } else if (id >= kHitTab && id < kHitTab + 4) {
      tabs_ = true;
      buttons_ = false;
      tab_ = id - kHitTab;
    }
  }

  void draw(ui::Ctx& c) override {
    skeletonShown_ = false;
    const EventTile* f = focused();
    bool corner = f && previewable(*f);
    // The corner takes the place of the big faded badges.
    if (f) events_ui::backdrop(c, *f, !corner);
    drawHero(c, f);
    top_bar::clock(c);
    if (corner) top_bar::preview(c, app_.playback(), f->channelLogo, f->channelName);
    drawTabs(c);
    drawRows(c);
    if (column_) drawColumn(c);
    ui::keyBar(c, hints::eventsBoard(app_.remoteKeys()));
  }

 private:
  void refresh() {
    board_ = app_.store().events();
    version_ = board_->version;
    rows_ = filteredRows();
    // Keep the focus on the same game when it is still there.
    if (!focusId_.empty()) {
      for (size_t r = 0; r < rows_.size(); ++r) {
        std::vector<const EventTile*> tiles = flat(static_cast<int>(r));
        for (size_t t = 0; t < tiles.size(); ++t) {
          if (tiles[t]->id == focusId_ && static_cast<int>(r) == row_) {
            tile_ = static_cast<int>(t);
            return;
          }
        }
      }
    }
    row_ = std::clamp(row_, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
    tile_ = std::clamp(tile_, 0, std::max(0, static_cast<int>(flat(row_).size()) - 1));
  }

  std::vector<const EventTile*> flat(int row) const {
    std::vector<const EventTile*> out;
    if (row < 0 || row >= static_cast<int>(rows_.size())) return out;
    for (const EventGroup& g : rows_[static_cast<size_t>(row)].groups)
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
    if (row < 0 || row >= static_cast<int>(rows_.size())) return 0;
    float x = 0;
    int i = 0;
    bool firstGroup = true;
    for (const EventGroup& g : rows_[static_cast<size_t>(row)].groups) {
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
        if (tab_ < 3) {
          ++tab_;
        } else {
          buttons_ = true;
          button_ = 0;
        }
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

  // The round buttons, right of the tabs (focus counts as in the tab row).
  bool buttonsKey(const KeyEvent& e) {
    switch (e.key) {
      case Key::Left:
        if (button_ > 0) {
          --button_;
        } else {
          buttons_ = false;
          tab_ = 3;
        }
        return true;
      case Key::Right:
        button_ = std::min(2, button_ + 1);
        return true;
      case Key::Down:
      case Key::Back:
        tabs_ = buttons_ = false;
        return true;
      case Key::Ok:
        if (button_ == 0) openSearch();
        else if (button_ == 1) openFilter();
        else app_.nav().push(makeOptions(app_));
        return true;
      default:
        return true;
    }
  }

  bool columnKey(const KeyEvent& e) {
    int n = static_cast<int>(rows_.size());
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
    stopPreview();
    auto pick = pickTab_;
    app_.nav().pop();
    if (pick) pick(tab);
  }

  void select(const EventTile& t) {
    // An event channel family: its first feed, Up/Down surfs the others.
    if (row_ < static_cast<int>(rows_.size()) && rows_[static_cast<size_t>(row_)].families) {
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
    // Already in the corner: the same stream goes full screen.
    if (p.active() && p.entry() == eventId && !p.failed()) {
      p.setFull();
      app_.nav().push(makePlayer(app_));
      return;
    }
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

  // ---------------------------------------------------------------- filter

  // SHOW (all, live now, my teams) or ONE SPORT; one at a time, like the
  // guide's filter.
  enum class Show { All, Live, Mine, Sport };

  static bool isLive(const EventTile& t) {
    if (t.upcoming || t.finished) return false;
    using P = EventTile::Pill;
    return t.pill == P::Live || t.pill == P::LiveOutline || t.pill == P::OnNow || t.pill == P::Delayed;
  }

  // The board's rows with a filter applied. Live now and My teams keep each
  // sport's own row (the board's "Live now" and "Your teams" rows would
  // only repeat them).
  static std::vector<EventRow> apply(const std::vector<EventRow>& rows, Show show, const std::string& sport) {
    if (show == Show::All) return rows;
    std::vector<EventRow> out;
    for (const EventRow& row : rows) {
      if (show == Show::Sport) {
        if (row.id == sport) out.push_back(row);
        continue;
      }
      if (row.families || row.id == "live" || row.id == "teams" || row.id == "untimed") continue;
      EventRow kept = row;
      kept.groups.clear();
      kept.count = kept.live = 0;
      for (const EventGroup& g : row.groups) {
        EventGroup group{g.title, {}};
        for (const EventTile& t : g.tiles) {
          if (show == Show::Live ? isLive(t) : t.favorite) group.tiles.push_back(t);
        }
        if (group.tiles.empty()) continue;
        kept.count += static_cast<int>(group.tiles.size());
        kept.live += static_cast<int>(std::count_if(group.tiles.begin(), group.tiles.end(),
                                                    [](const EventTile& t) { return isLive(t) && t.watchable; }));
        kept.groups.push_back(std::move(group));
      }
      if (!kept.groups.empty()) out.push_back(std::move(kept));
    }
    return out;
  }

  static int total(const std::vector<EventRow>& rows) {
    int n = 0;
    for (const EventRow& r : rows) n += r.count;
    return n;
  }

  std::vector<EventRow> filteredRows() const { return apply(board_->rows, show_, sport_); }
  bool filtered() const { return show_ != Show::All; }

  std::string filterTitle() const {
    if (show_ == Show::Live) return "Live now";
    if (show_ == Show::Mine) return "My teams";
    for (const EventRow& r : board_->rows) {
      if (r.id == sport_) return r.title;
    }
    return "One sport";
  }

  void setFilter(Show show, const std::string& sport) {
    show_ = show;
    sport_ = show == Show::Sport ? sport : std::string();
    rows_ = filteredRows();
    row_ = tile_ = 0;
    focusId_.clear();
    tabs_ = buttons_ = false;
  }

  void openFilter() {
    App& app = app_;
    EventsBoardScreen* self = this;
    app_.nav().push(std::make_unique<PanelScreen>(app_, PanelScreen::Side::Trailing, 760.0f,
                                                  [&app, self](ui::ListPanel& p) {
      p.subtitle = "LIVE EVENTS";
      p.title = "Filter";
      const std::vector<EventRow>& rows = self->board_->rows;
      auto add = [&](const std::string& title, Show show, const std::string& sport, bool dot) {
        ui::PanelItem it{ui::PanelItem::Kind::Check, title};
        it.on = self->show_ == show && (show != Show::Sport || self->sport_ == sport);
        it.value = std::to_string(total(apply(rows, show, sport)));
        it.dot = dot;
        it.select = [&app, self, show, sport] {
          self->setFilter(show, sport);
          app.nav().pop();
        };
        p.items.push_back(it);
      };
      p.items.push_back(ui::PanelItem::header("SHOW"));
      add("All events", Show::All, "", false);
      add("Live now", Show::Live, "", true);
      if (app.store().mySports().configured) add("My teams", Show::Mine, "", false);
      bool any = false;
      for (const EventRow& r : rows) {
        if (r.id.rfind("sport|", 0) != 0) continue;
        if (!any) p.items.push_back(ui::PanelItem::header("ONE SPORT"));
        any = true;
        add(r.title, Show::Sport, r.id, false);
      }
      if (app.store().settings().scheduleEnabled) {
        p.items.push_back(ui::PanelItem::header("ALSO"));
        ui::PanelItem unwatchable{ui::PanelItem::Kind::Toggle, "Games not in my channels"};
        unwatchable.detail = "Show them dimmed at the end of each row";
        unwatchable.on = app.store().settings().showUnwatchable;
        unwatchable.select = [&app] {
          Settings n = app.store().settings();
          n.showUnwatchable = !n.showUnwatchable;
          app.store().setSettings(n);
        };
        p.items.push_back(unwatchable);
      }
    }));
  }

  void openSearch() {
    App& app = app_;
    app_.nav().push(makeSearch(app_, [&app](const std::string& id) {
      // A channel: straight to it, full screen (the board has no channel rows).
      Playback& p = app.playback();
      if (!p.tune(id)) {
        app.toast("This channel has no working feed right now");
        return;
      }
      p.setFull();
      app.nav().push(makePlayer(app));
    }));
  }

  // ---------------------------------------------------------------- preview

  // The guide's corner preview, for a game that is on now in your channels.
  bool previewable(const EventTile& t) const {
    if (!app_.store().settings().previewChannels || !t.watchable || !isLive(t)) return false;
    return !(row_ < static_cast<int>(rows_.size()) && rows_[static_cast<size_t>(row_)].families);
  }

  // After focus rests on a live game (the guide's delay), it plays in the
  // corner; focus on a game that isn't on ends the preview. Moving up to
  // the tabs or the sports column leaves it playing.
  bool previewTick(uint32_t ms) {
    Playback& p = app_.playback();
    if (app_.nav().top() != this || tabs_ || column_) return false;
    const EventTile* t = focused();
    if (!t || !previewable(*t)) {
      if (p.active() && !p.full()) p.stop();
      previewCandidate_.clear();
      return false;
    }
    if (previewCandidate_ != t->id) {
      previewCandidate_ = t->id;
      focusSince_ = ms;
    }
    if (p.entry() == t->id || previewTried_ == t->id) return false;
    if (ms - focusSince_ >= top_bar::kPreviewDelayMs && ms - lastPreviewAt_ >= top_bar::kPreviewGapMs) {
      lastPreviewAt_ = ms;
      previewTried_ = t->id;
      p.setCorner(top_bar::kPreview);
      if (!p.tune(t->id, app_.store().eventFlipList(t->id))) p.stop();
      return false;
    }
    return true;  // waiting out the delay
  }

  void stopPreview() {
    Playback& p = app_.playback();
    if (p.active() && !p.full()) p.stop();
  }

  // ---------------------------------------------------------------- drawing

  void drawHero(ui::Ctx& c, const EventTile* t) {
    float x = kLeft, y = 44;
    if (!t) {
      ui::label(c, "Live Events", x, y + 40, Weight::Bold, 44, theme::white());
      if (board_->loading) {
        // Still loading: placeholders where the game's title and status go.
        skeletonShown_ = true;
        ui::Color block = ui::skeleton(c);
        c.g.fill({x, y + 116, 560, 26}, block, 13);
        c.g.fill({x, y + 160, 320, 18}, block, 9);
        return;
      }
      ui::label(c, "This week's games, and where you can watch them.", x, y + 110, Weight::Regular, 26, theme::white(0.7f));
      return;
    }
    if (!t->leagueChip.empty()) ui::chip(c, t->leagueChip, x, y, 20, theme::white(0.14f), theme::white());
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
      bool focusedTab = tabs_ && !buttons_ && tab_ == i;
      Rect hit{x - 16, kTabsY, w + 32, 60};
      c.hit(hit, kHitTab + i);
      if (focusedTab) c.g.fill(hit, theme::white(), 30);
      Color col = focusedTab ? theme::black() : i == 3 ? theme::white() : theme::white(0.55f);
      ui::labelIn(c, kTabs[i], {x, kTabsY, w, 54}, Weight::Bold, 27, col);
      if (i == 3 && !focusedTab) c.g.fill({x, kTabsY + 52, w, 5}, theme::white(), 2.5f);
      x += w + 44;
    }
    if (filtered()) {
      ui::chip(c, "\xe2\x97\x8f " + filterTitle() + "  \xc3\x97", x + 10, kTabsY + 14, 19,
               theme::hex(theme::kNowLine, 0.3f), theme::white());
    }
    top_bar::buttons(c, buttons_ ? button_ : -1, filtered(), kHitButton);
  }

  float rowHeight(const EventRow& r) const {
    bool labels = std::any_of(r.groups.begin(), r.groups.end(), [](const EventGroup& g) { return !g.title.empty(); });
    return kRowHeader + (labels ? kGroupLabel : 0) + events_ui::kTileH + kRowGap;
  }

  void drawRows(ui::Ctx& c) {
    if (rows_.empty() && board_->loading) {
      drawLoadingRows(c);
      return;
    }
    if (rows_.empty()) {
      const char* empty = !filtered()           ? "No games this week in your sports."
                          : show_ == Show::Live ? "No games on right now. Back shows them all."
                                                : "None this week. Back shows every game.";
      ui::label(c, empty, kLeft, kBoardTop + 30, Weight::Medium, 26, theme::white(0.6f));
      return;
    }
    // The focused row sits at the top of the board area.
    float y = kBoardTop;
    float scroll = 0;
    for (int r = 0; r < row_; ++r) scroll += rowHeight(rows_[static_cast<size_t>(r)]);
    c.g.pushClip({0, kBoardTop - 10, gfx::kLayoutWidth, kBoardBottom - kBoardTop + 10});
    y -= scroll;
    for (size_t r = 0; r < rows_.size(); ++r) {
      const EventRow& row = rows_[r];
      float h = rowHeight(row);
      if (y > kBoardBottom) break;
      if (y + h >= kBoardTop) drawRow(c, row, static_cast<int>(r), y);
      y += h;
    }
    c.g.popClip();
  }

  // Rows of tile-shaped placeholders down the board while the week's games load.
  void drawLoadingRows(ui::Ctx& c) {
    skeletonShown_ = true;
    ui::Color block = ui::skeleton(c);
    float y = kBoardTop;
    while (y + kRowHeader < kBoardBottom) {
      c.g.fill({kLeft, y + 4, 220, 22}, block, 11);
      float tileY = y + kRowHeader;
      for (float x = kLeft; x < gfx::kLayoutWidth; x += events_ui::kTileW + kTileGap)
        c.g.fill({x, tileY, events_ui::kTileW, events_ui::kTileH}, block, 12);
      y += kRowHeader + events_ui::kTileH + kRowGap;
    }
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
  // A row of more than one sport: its tiles name theirs.
  static bool mixed(const EventRow& row) {
    const EventTile* first = nullptr;
    for (const EventGroup& g : row.groups)
      for (const EventTile& t : g.tiles) {
        if (!first) first = &t;
        else if (t.sport != first->sport || t.sport.empty()) return true;
      }
    return first && first->sport.empty();
  }

  void drawColumn(ui::Ctx& c) {
    float h = std::min(640.0f, 70.0f + 60.0f * static_cast<float>(rows_.size()));
    Rect r{kLeft - 10, kBoardTop - 20, 400, h};
    c.g.fill(r, theme::hex(0x1C1F25, 0.97f), theme::kPanelRadius);
    ui::label(c, "SPORTS", r.x + 24, r.y + 20, Weight::Bold, 16, theme::white(0.55f));
    float y = r.y + 56;
    int visible = static_cast<int>((r.h - 70) / 60);
    int first = std::clamp(columnRow_ - visible / 2, 0, std::max(0, static_cast<int>(rows_.size()) - visible));
    for (int i = first; i < static_cast<int>(rows_.size()) && i < first + visible; ++i) {
      const EventRow& row = rows_[static_cast<size_t>(i)];
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
  bool skeletonShown_ = false;  // placeholders on screen: the pulse redraws
  uint32_t pulseStep_ = 0;
  std::function<void(int)> pickTab_;
  std::shared_ptr<const EventsBoard> board_;
  std::vector<EventRow> rows_;  // the board's rows, filtered
  Show show_ = Show::All;
  std::string sport_;  // a "sport|..." row id
  uint64_t version_ = ~0ull;
  int row_ = 0, tile_ = 0;
  std::string focusId_;
  bool tabs_ = false;
  int tab_ = 3;
  bool buttons_ = false;  // on the round buttons (part of the tab row)
  int button_ = 0;        // 0 search, 1 filter, 2 options
  bool column_ = false;
  int columnRow_ = 0, columnReturn_ = 0;
  float rowScroll_ = 0;
  std::string previewCandidate_, previewTried_;
  uint32_t focusSince_ = 0, lastPreviewAt_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeEventsBoard(App& app, std::function<void(int)> pickTab) {
  return std::make_unique<EventsBoardScreen>(app, std::move(pickTab));
}

}  // namespace marigold::app
