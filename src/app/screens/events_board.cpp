// The Live Events board (LiveEventsBoard in tvOS): the focused game in the
// hero with its two badges large and faded behind, then one row per sport
// of 360x230 tiles under group labels, right of the left rail's strip
// (Build 112; Live Events is one of its sections, this screen over the
// guide). Left from a row's first tile opens the SPORTS column, which
// pushes the rows right; Back opens the menu. A small filter button ends the
// first row's header. A live game in your channels plays in the guide's
// preview corner once focus rests on it. Scores refresh only while this
// board (or the event guide in the player) is open.
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/left_rail.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/screens/top_bar.hpp"
#include "app/store/store.hpp"
#include "core/input/hints.hpp"
#include "core/input/preview_pace.hpp"
#include "core/sports/my_sports.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr float kHeroX = left_rail::kRestLeft;  // the hero never moves
constexpr float kRight = gfx::kLayoutWidth - 80;  // the rows' right edge
constexpr float kBoardTop = 330;
constexpr float kBoardBottom = 1010;
constexpr float kRowHeader = 44;
constexpr float kGroupLabel = 30;
constexpr float kRowGap = 26;
constexpr float kTileGap = 24;
constexpr float kGroupGap = 32;
constexpr int kHitTile = 100000;  // + row * 1000 + tile
constexpr int kHitFilter = 51;    // the rail's ids are left_rail::kHit*, 1-499

class EventsBoardScreen : public Screen {
 public:
  bool holdsOk() const override { return true; }
  EventsBoardScreen(App& app, std::function<void(rail::Item, bool)> go, bool sports)
      : Screen(app), go_(std::move(go)), nav_(rail::Item::Events) {
    app_.store().eventsOpen(true);
    nav_.setItems(rail::items(app_.store().settings().allChannelsTab));
    // The guide's channel preview doesn't carry over into the board.
    stopPreview();
    refresh();
    // From the menu (or the strip): on the SPORTS column first.
    if (sports) openColumn(true);
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
    bool sliding = push_.moving(ms) || menu_.moving(ms);
    return previewTick(ms) || pulse || sliding || promptAt_ != 0 || board_->updating;
  }

  bool key(const KeyEvent& e) override {
    Key k = e.key;
    // Other keys hide the pointer, and its target with it.
    if (k != Key::Ok) pointer_ = 0;
    rail::Mode mode = nav_.mode();
    if (e.longPress) {
      if (mode == rail::Mode::Content && !filterFocus_ && !inRange(pointer_, left_rail::kHitStrip)) {
        if (const EventTile* t = focused()) openMenu(*t);
      }
      return true;
    }
    // A click: the rail's targets first (the strip, the menu, the column).
    if (k == Key::Ok && pointer_ && click()) return true;
    // Yellow (play/pause on Fire TV) and Blue do what they did in every state.
    switch (k) {
      case Key::Yellow:
      case Key::PlayPause:
      case Key::Play:
        close();
        return true;
      case Key::Blue:
        app_.nav().push(makeQuickSettings(app_, QuickPlace::LiveEvents));
        return true;
      default: break;
    }
    // The column and the menu: the rail's key path.
    if (mode != rail::Mode::Content) {
      apply(nav_.key(k), true);
      return true;
    }
    if (filterFocus_) {
      if (k == Key::Ok) openFilter();
      else if (k == Key::Down || k == Key::Back) filterFocus_ = false;
      return true;
    }
    switch (k) {
      case Key::Up:
        if (row_ == 0 || rows_.empty()) filterFocus_ = true;
        else setRow(row_ - 1);
        return true;
      case Key::Down:
        if (row_ + 1 < static_cast<int>(rows_.size())) setRow(row_ + 1);
        return true;
      case Key::Left:
        if (tile_ > 0) {
          --tile_;
        } else {
          // A row's first tile: SPORTS (the menu when there are no rows).
          place_ = {row_, tile_, focusId_};
          nav_.leftEdge(row_, static_cast<int>(rows_.size()));
          syncSlides();
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
        // A filter first, as the × on its chip says; then the menu.
        if (filtered()) {
          setFilter(Show::All, "");
        } else {
          nav_.openMenu();
          syncSlides();
        }
        return true;
      default:
        return true;
    }
  }

  void hover(int id) override {
    pointer_ = id;
    if (nav_.mode() == rail::Mode::Menu) {
      if (inRange(id, left_rail::kHitMenu)) nav_.hoverMenu(id - left_rail::kHitMenu);
      return;
    }
    // The strip only shows the icon's name; with SPORTS open the tiles take
    // the focus on a click.
    if (id < kHitFilter || (id >= left_rail::kHitStrip && id < left_rail::kHitEnd)) return;
    if (nav_.mode() == rail::Mode::Column) return;
    if (id >= kHitTile) {
      int r = (id - kHitTile) / 1000, t = (id - kHitTile) % 1000;
      if (r < static_cast<int>(rows_.size())) {
        row_ = r;
        tile_ = t;
        filterFocus_ = false;
      }
    } else if (id == kHitFilter) {
      filterFocus_ = true;
    }
  }

  void draw(ui::Ctx& c) override {
    skeletonShown_ = false;
    // Under everything: the pointer off any target (the strip's pill goes).
    c.hit({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, left_rail::kHitNothing);
    settleSlides(c.ms);
    const EventTile* f = focused();
    bool corner = f && previewable(*f);
    // The corner takes the place of the big faded badges.
    if (f) events_ui::backdrop(c, *f, !corner);
    drawHero(c, f);
    top_bar::clock(c);
    if (corner) top_bar::preview(c, app_.playback(), f->channelLogo, f->channelName);
    drawTop(c);
    drawRows(c);
    float column = push_.at(c.ms);
    if (column > 0.01f) drawColumn(c, column);
    ui::keyBar(c, hints::eventsBoard(app_.remoteKeys(), nav_.mode() == rail::Mode::Column ? hints::Rail::Column : hints::Rail::Content),
               left_);
    int hover = -1;
    if (c.pointerActive && nav_.mode() != rail::Mode::Menu && inRange(pointer_, left_rail::kHitStrip))
      hover = pointer_ - left_rail::kHitStrip;
    left_rail::strip(c, nav_.items(), rail::Item::Events, hover);
    float menu = menu_.at(c.ms);
    if (menu > 0.01f) left_rail::menu(c, nav_.items(), rail::Item::Events, nav_.menuFocus(), menu);
  }

 private:
  static bool inRange(int id, int base) { return id >= base && id < base + 100; }

  // ---------------------------------------------------------------- left rail

  void openColumn(bool fromMenu) {
    place_ = {row_, tile_, focusId_};
    nav_.openColumn(row_, static_cast<int>(rows_.size()), fromMenu);
    syncSlides();
  }

  // The same game tile the column opened on (found again if the board was
  // rebuilt meanwhile).
  void restorePlace() {
    row_ = std::clamp(place_.row, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
    tile_ = place_.tile;
    std::vector<const EventTile*> tiles = flat(row_);
    for (size_t t = 0; t < tiles.size(); ++t) {
      if (tiles[t]->id == place_.id) tile_ = static_cast<int>(t);
    }
    focusId_ = place_.id;
  }

  void apply(const rail::Step& s, bool fromMenu) {
    using Do = rail::Step::Do;
    int n = static_cast<int>(rows_.size());
    switch (s.what) {
      case Do::None: break;
      case Do::Scroll:
      case Do::Top:
        // The rows scroll behind the column; confirming another sport lands
        // on its first game.
        if (s.index < n) {
          row_ = s.index;
          tile_ = 0;
        }
        break;
      case Do::Restore: restorePlace(); break;
      case Do::Open: open(s.item, fromMenu); break;
      case Do::Home: {
        // Back in the menu: My Lineup, at rest.
        auto go = go_;
        close();
        if (go) go(rail::Item::Lineup, false);
        break;
      }
      case Do::Exit: break;  // only on My Lineup, which is the guide's
    }
    filterFocus_ = false;
    syncSlides();
  }

  void open(rail::Item item, bool fromMenu) {
    switch (item) {
      case rail::Item::Search: openSearch(); break;
      case rail::Item::MySports: app_.nav().push(makeMySports(app_)); break;
      case rail::Item::Events: openColumn(fromMenu); break;
      case rail::Item::Options:
        if (go_) go_(item, false);
        break;
      default: {
        // A guide section: the board closes onto it (its neighborhoods).
        auto go = go_;
        close();
        if (go) go(item, rail::hasColumn(item));
        break;
      }
    }
  }

  // A pointer click (OK while the pointer is on something). True when the
  // rail took it; on the tiles OK acts as before (hovering moved the focus).
  bool click() {
    int id = pointer_;
    const std::vector<rail::Item>& items = nav_.items();
    rail::Mode mode = nav_.mode();
    auto item = [&](int base) {
      return items[static_cast<size_t>(std::clamp(id - base, 0, static_cast<int>(items.size()) - 1))];
    };
    if (mode == rail::Mode::Menu) {
      if (inRange(id, left_rail::kHitMenu) && !items.empty()) apply(nav_.pick(item(left_rail::kHitMenu)), true);
      else if (id == left_rail::kHitOutside) nav_.close();
      syncSlides();
      return true;
    }
    if (inRange(id, left_rail::kHitStrip) && !items.empty()) {
      if (mode == rail::Mode::Column) restorePlace();
      apply(nav_.pick(item(left_rail::kHitStrip)), false);
      return true;
    }
    if (mode != rail::Mode::Column) return id == left_rail::kHitNothing;
    if (inRange(id, left_rail::kHitColumn)) {
      apply(nav_.clickColumn(id - left_rail::kHitColumn), false);
    } else if (id >= kHitTile) {
      // A tile: the column closes and that game takes the focus.
      nav_.close();
      syncSlides();
      hover(id);
    }
    return true;
  }

  void syncSlides() {
    uint32_t ms = app_.ms();
    push_.go(nav_.mode() == rail::Mode::Column ? 1.0f : 0.0f, ms);
    menu_.go(nav_.mode() == rail::Mode::Menu ? 1.0f : 0.0f, ms);
    settleSlides(ms);
    app_.invalidate();
  }

  void settleSlides(uint32_t ms) {
    left_ = left_rail::kRestLeft + (left_rail::kPushLeft - left_rail::kRestLeft) * push_.at(ms);
  }

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

  void close() {
    stopPreview();
    app_.nav().pop();
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
    filterFocus_ = false;
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
        ui::PanelItem unwatchable{ui::PanelItem::Kind::Toggle, "Games not on my channels"};
        unwatchable.detail = "Dimmed at the end of each row";
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
    // Search plays what is picked itself; its strip and menu come back here.
    stopPreview();
    EventsBoardScreen* self = this;
    app_.nav().push(makeSearch(app_, [self](rail::Item item) {
      if (item != rail::Item::Search) self->open(item, true);
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
    if (app_.nav().top() != this || filterFocus_ || nav_.mode() != rail::Mode::Content) return false;
    const EventTile* t = focused();
    if (!t || !previewable(*t)) {
      if (p.active() && !p.full()) p.stop();
      pace_.step("", false, ms);
      return false;
    }
    switch (pace_.step(t->id, p.entry() == t->id, ms)) {
      case preview::Pace::Step::Idle: return false;
      case preview::Pace::Step::Wait: return true;  // waiting out the delay
      case preview::Pace::Step::Tune: break;
    }
    p.setCorner(top_bar::kPreview);
    if (!p.tune(t->id, app_.store().eventFlipList(t->id))) p.stop();
    return false;
  }

  void stopPreview() {
    Playback& p = app_.playback();
    if (p.active() && !p.full()) p.stop();
  }

  // ---------------------------------------------------------------- drawing

  void drawHero(ui::Ctx& c, const EventTile* t) {
    float x = kHeroX, y = 44;
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
    ui::label(c, t->heroTitle.empty() ? t->title : t->heroTitle, tx, y + 4, Weight::Bold, 40, theme::white(), x + 1240 - tx);
    y += 72;
    float sx = x;
    sx += events_ui::pill(c, *t, sx, y + 4, false) ;
    if (sx > x) sx += 12;
    ui::label(c, t->heroStatus.empty() ? t->status : t->heroStatus, sx, y, Weight::Medium, 24, theme::white(), x + 1240 - sx);
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
    ui::label(c, t->whereLine, wx, y + 2, Weight::Regular, 22, theme::white(0.8f), x + 1240 - wx);
  }

  // The filter button at the end of the first row's header line, its chip
  // and "Updating…" to its left (the tab row they sat in is gone).
  void drawTop(ui::Ctx& c) {
    Rect button{kRight - 40, kBoardTop - 6, 40, 40};
    bool focusedButton = filterFocus_ && nav_.mode() == rail::Mode::Content;
    top_bar::filterButton(c, button, focusedButton, filtered(), kHitFilter);
    float right = button.x - 16;
    if (filtered()) {
      std::string text = "\xe2\x97\x8f " + filterTitle() + "  \xc3\x97";
      right -= ui::chipWidth(c, text, 19);
      ui::chip(c, text, right, kBoardTop - 1, 19, theme::hex(theme::kNowLine, 0.3f), theme::white());
      right -= 20;
    }
    if (board_->updating) {
      // An old board while the fresh one is made (never a count).
      const char* text = "Updating\xe2\x80\xa6";
      float tw = ui::textWidth(c, text, Weight::Medium, 22);
      ui::label(c, text, right - tw, kBoardTop, Weight::Medium, 22, theme::white(0.6f));
      ui::spinner(c, right - tw - 22, kBoardTop + 14, 11);
    }
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
      ui::label(c, empty, left_, kBoardTop + 30, Weight::Medium, 26, theme::white(0.6f));
      return;
    }
    // The focused row sits at the top of the board area.
    float y = kBoardTop;
    float scroll = 0;
    for (int r = 0; r < row_; ++r) scroll += rowHeight(rows_[static_cast<size_t>(r)]);
    // At rest a scrolled row runs under the strip; with SPORTS open it stops
    // at the column's rule.
    float clipX = left_rail::kStripW + (left_ - left_rail::kRestLeft) *
                                           (left_rail::kColumnX + left_rail::kColumnW + 15 - left_rail::kStripW) /
                                           (left_rail::kPushLeft - left_rail::kRestLeft);
    c.g.pushClip({clipX, kBoardTop - 10, gfx::kLayoutWidth - clipX, kBoardBottom - kBoardTop + 10});
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
      c.g.fill({left_, y + 4, 220, 22}, block, 11);
      float tileY = y + kRowHeader;
      for (float x = left_; x < gfx::kLayoutWidth; x += events_ui::kTileW + kTileGap)
        c.g.fill({x, tileY, events_ui::kTileW, events_ui::kTileH}, block, 12);
      y += kRowHeader + events_ui::kTileH + kRowGap;
    }
  }

  void drawRow(ui::Ctx& c, const EventRow& row, int r, float y) {
    float hx = left_;
    hx += ui::label(c, row.title, hx, y, Weight::Bold, 25, theme::white()) + 10;
    std::string summary = "\xc2\xb7 " + std::to_string(row.count);
    if (row.families) {
      // Event channels: never how many (they are channels).
    } else if (row.live > 0) {
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
      float visible = kRight - left_;
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
      float gx = left_ + x - scrollX;
      if (!g.title.empty() && gx > -400 && gx < gfx::kLayoutWidth)
        ui::label(c, g.title, gx, y + kRowHeader, Weight::Bold, 20, theme::white(0.75f));
      for (const EventTile& t : g.tiles) {
        float tx = left_ + x - scrollX;
        if (tx + events_ui::kTileW > 0 && tx < gfx::kLayoutWidth) {
          bool f = !filterFocus_ && nav_.mode() == rail::Mode::Content && r == row_ && index == tile_;
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

  void drawColumn(ui::Ctx& c, float alpha) {
    // SPORTS, inside the board: each row's sport and how many are live.
    std::vector<left_rail::Entry> entries;
    entries.reserve(rows_.size());
    for (const EventRow& row : rows_) entries.push_back({row.title, row.live});
    int hover = c.pointerActive && inRange(pointer_, left_rail::kHitColumn) ? pointer_ - left_rail::kHitColumn : -1;
    left_rail::column(c, "Live Events", "Sports", entries, nav_.index(), nav_.origin(), hover, kBoardTop,
                      kBoardTop + 52, alpha);
  }

  // Where the focus was when SPORTS opened (the return rule).
  struct Place {
    int row = 0, tile = 0;
    std::string id;
  };

  uint32_t promptAt_ = 0;
  bool skeletonShown_ = false;  // placeholders on screen: the pulse redraws
  uint32_t pulseStep_ = 0;
  std::function<void(rail::Item, bool)> go_;
  rail::Nav nav_;
  Place place_;
  left_rail::Slide push_, menu_;  // SPORTS' push, the menu's slide
  float left_ = left_rail::kRestLeft;  // the rows' left edge
  int pointer_ = 0;  // what the pointer is on (a hit id) while it shows
  std::shared_ptr<const EventsBoard> board_;
  std::vector<EventRow> rows_;  // the board's rows, filtered
  Show show_ = Show::All;
  std::string sport_;  // a "sport|..." row id
  uint64_t version_ = ~0ull;
  int row_ = 0, tile_ = 0;
  std::string focusId_;
  bool filterFocus_ = false;  // on the filter button
  float rowScroll_ = 0;
  preview::Pace pace_{true};  // the corner: after a rest, each game once
};

}  // namespace

std::unique_ptr<Screen> makeEventsBoard(App& app, std::function<void(rail::Item, bool)> go, bool sports) {
  return std::make_unique<EventsBoardScreen>(app, std::move(go), sports);
}

}  // namespace marigold::app
