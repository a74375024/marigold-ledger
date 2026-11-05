// The Live Events board (LiveEventsBoard in tvOS): the focused game in the
// hero with its two badges large and faded behind, then one row per sport
// of 360x230 tiles under group labels, right of the left rail's strip
// (Build 112; Live Events is one of its sections, this screen over the
// guide). Left from a row's first tile opens the SPORTS column, which
// pushes the rows right; Back opens the menu. A small filter button ends the
// first row's header. A live game in your channels plays in the guide's
// preview corner once focus rests on it. Scores refresh only while this
// board (or the event guide in the player) is open. A new board (made on a
// worker) is shown as it is, never copied: the rows shown, and a filter's
// tiles, point into it (Build 119: More events, Time not listed and Event
// channels show only when switched on in Settings › Live Events; empty, the
// board says so); each row's layout and text are worked out when it comes
// in, and the focus stays on its game. While the
// games load, placeholders in the board's own layout stand where rows will
// be, pulsing slowly, for 20 s at most: after an open on an old board (or
// none) all of them until the whole board is in (Build 123: no tiles and
// no hero game before, so nothing moves as it fills in); on a board already
// shown whole, after its rows; under the title, a spinner and what the load
// waits for now (Build 124, the board's stage). Within half an hour of the start, OK plays a
// game whose best source is a channel (views::okPlays()).
//
// Build 127: a row's title has no counts, only how it is filtered after it
// in gray ("College football · Top 25 and your schools"); a row that can be
// changed ends with a "Choose competitions" / "Choose schools" tile (OK
// opens Soccer's or the college page). After the update a one-time note
// stands where the hero is, with OK and Change (My Sports). With "I don't
// watch sports" saved, More events shows whatever Show on the board says.
// Holding OK on a game offers Follow and Hide beside Where to Watch.
#include <algorithm>
#include <chrono>
#include <cmath>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/left_rail.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/screens/top_bar.hpp"
#include "app/store/store.hpp"
#include "core/engine/views.hpp"
#include "core/input/hints.hpp"
#include "core/input/wake.hpp"
#include "core/input/preview_pace.hpp"
#include "core/sports/my_sports.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
using engine::views::ShownRow;
using engine::ScoresLine;
namespace theme = ui::theme;

constexpr float kHeroX = left_rail::kRestLeft;  // the hero never moves
constexpr float kRight = gfx::kLayoutWidth - 80;  // the rows' right edge
// The guide's top (Build 126, Build 128 here): the 640 x 360 preview at the
// top right, the game's column at its left, the rows under the preview.
constexpr Rect kPreview = top_bar::kGuidePreview;
constexpr float kHeroRight = kPreview.x - 40;
constexpr float kHeroW = kHeroRight - kHeroX;
constexpr float kBoardTop = kPreview.y + kPreview.h + 24;
constexpr float kBoardBottom = 1010;
constexpr float kRowHeader = 44;
constexpr float kGroupLabel = 30;
constexpr float kRowGap = 26;
constexpr float kTileGap = 24;
constexpr float kGroupGap = 32;
constexpr int kHitTile = 100000;  // + row * 1000 + tile
constexpr int kHitFilter = 51;    // the rail's ids are left_rail::kHit*, 1-499
constexpr int kHitShowHidden = 52;  // the empty board's "Show them in Settings"
constexpr int kHitNote = 53;        // + 0 OK, + 1 Change: the one-time note's buttons
constexpr uint32_t kPulseStepMs = 250;       // placeholders drawn again this often (the pulse is 1.2 s)
// Then the board as it is, not endless placeholders.
constexpr uint32_t kLoadingAtMostMs = engine::kPlaceholdersAtMostMs;
constexpr uint32_t kLogMs = 60000;        // the timing log's line, at most this often
// The placeholders shown this long: the loading line's second line.
constexpr uint32_t kSlowLoadMs = 10000;

// Under the title while the board loads: what it waits for now (the
// store's stage; never where from).
const char* stageLine(EventsBoard::Stage stage) {
  switch (stage) {
    case EventsBoard::Stage::Channels: return "Getting your channels\xe2\x80\xa6";
    case EventsBoard::Stage::Schedule: return "Getting this week's games\xe2\x80\xa6";
    case EventsBoard::Stage::Matching: return "Matching games to your channels\xe2\x80\xa6";
    case EventsBoard::Stage::Scores: return "Getting live scores\xe2\x80\xa6";
    case EventsBoard::Stage::None: break;
  }
  return nullptr;
}

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

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
      if (app_.store().settings().previewChannels) p.setCorner(kPreview);
      else p.stop();
    } else {
      // Back from Search with its corner still playing: the picture moves
      // into the larger preview.
      p.keepCorner(kPreview);
    }
  }
  bool keepsPreview() const override { return true; }

  bool tick(uint32_t ms) override {
    if (promptAt_ && ms >= promptAt_) {
      promptAt_ = 0;
      app_.nav().push(makeMySports(app_));
      return true;
    }
    if (app_.store().events()->version != version_ || (rowsHidden_ && !placeholders())) {
      // A new board, or the placeholders' time is up: the rows under them show.
      refresh();
      app_.invalidate();
    }
    bool sliding = push_.moving(ms) || menu_.moving(ms);
    wake_ = 0;
    // Placeholders: drawn again a few times a second for their pulse,
    // asleep in between; gone after kLoadingAtMostMs.
    bool pulse = false;
    if (placeholders()) {
      // The loading line's spinner: at each of its steps (eight a second).
      uint32_t every = spinnerShown_ ? wake::kSpinnerStepMs : kPulseStepMs;
      uint32_t step = ms / every;
      pulse = skeletonShown_ && step != pulseStep_;
      pulseStep_ = step;
      wakeFor((step + 1) * every);
      wakeFor(loadingSince_ + kLoadingAtMostMs);
    } else if (skeletonShown_) {
      pulse = true;  // their time is up: the board without them
    }
    // Build 142: "Updating scores…" under the title while the board's
    // scores catch up: its spinner at its steps, and drawn again as it
    // comes and goes; the paused note's few seconds end by themselves.
    if (!placeholders()) {
      ScoresLine line = app_.store().eventsScoresLine(true);
      if (line.text != scoresShown_) {
        scoresShown_ = line.text;
        pulse = true;
      }
      if (line.mark == ScoresLine::Mark::Busy) {
        uint32_t step = ms / wake::kSpinnerStepMs;
        if (step != scoresStep_) pulse = true;
        scoresStep_ = step;
        wakeFor((step + 1) * wake::kSpinnerStepMs);
      } else if (line.mark == ScoresLine::Mark::Paused) {
        wakeFor(ms + 500);
      }
    } else {
      scoresShown_ = nullptr;
    }
    if (promptAt_) wakeFor(promptAt_);  // My Sports is offered in a moment: asleep until then
    previewTick(ms);
    logStats(ms);
    return pulse || sliding;
  }
  uint32_t wakeAt() const override { return wake_; }
  const char* name() const override { return "live events"; }

  bool key(const KeyEvent& e) override {
    Key k = e.key;
    // Other keys hide the pointer, and its target with it.
    if (k != Key::Ok) pointer_ = 0;
    rail::Mode mode = nav_.mode();
    if (e.longPress) {
      if (mode == rail::Mode::Content && !filterFocus_ && !noteFocus_ && !inRange(pointer_, left_rail::kHitStrip)) {
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
    if (noteFocus_ && noteShown()) {
      switch (k) {
        case Key::Left: noteButton_ = 0; return true;
        case Key::Right: noteButton_ = 1; return true;
        case Key::Down: noteFocus_ = false; return true;
        case Key::Ok: noteDone(noteButton_ == 1); return true;
        case Key::Back: noteDone(false); return true;
        default: return true;
      }
    }
    noteFocus_ = false;
    if (filterFocus_) {
      if (k == Key::Ok) {
        openFilter();
      } else if (k == Key::Down || k == Key::Back) {
        filterFocus_ = false;
      } else if (k == Key::Up && noteShown()) {
        filterFocus_ = false;
        noteFocus_ = true;
      }
      return true;
    }
    switch (k) {
      case Key::Up:
        if (row_ == 0 || rows().empty()) filterFocus_ = true;
        else setRow(row_ - 1);
        return true;
      case Key::Down:
        if (row_ + 1 < static_cast<int>(rows().size())) setRow(row_ + 1);
        return true;
      case Key::Left:
        if (tile_ > 0) {
          --tile_;
        } else {
          // A row's first tile: SPORTS (the menu when there are no rows).
          place_ = {row_, tile_, focusId_, focusRow_};
          nav_.leftEdge(row_, static_cast<int>(rows().size()));
          syncSlides();
        }
        return true;
      case Key::Right:
        if (tile_ + 1 < count(row_)) ++tile_;
        return true;
      case Key::Ok:
        if (chooseFocused()) openChoose(*rows()[static_cast<size_t>(row_)].row);
        else if (const EventTile* t = focused()) select(*t);
        else if (showHiddenButton()) showHidden();
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

  void pointerGone() override { pointer_ = 0; }

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
      if (r < static_cast<int>(rows().size())) {
        row_ = r;
        tile_ = t;
        filterFocus_ = false;
        noteFocus_ = false;
      }
    } else if (id == kHitFilter) {
      filterFocus_ = true;
      noteFocus_ = false;
    } else if (id == kHitShowHidden) {
      filterFocus_ = false;
      noteFocus_ = false;
    } else if (id == kHitNote || id == kHitNote + 1) {
      filterFocus_ = false;
      noteFocus_ = true;
      noteButton_ = id - kHitNote;
    }
  }

  void draw(ui::Ctx& c) override {
    Clock::time_point start = Clock::now();
    // Show on the board switched (in Quick settings over the board): the
    // rows again, so the row appears behind the panel.
    // A new board while something is over this screen (the undo note
    // after Hide): its rows as they are now.
    if (showsChanged() || app_.store().events()->version != version_) refresh();
    drawBoard(c);
    stats_.frameMax = std::max(stats_.frameMax, msSince(start));
    ++stats_.frames;
  }

 private:
  void drawBoard(ui::Ctx& c) {
    skeletonShown_ = spinnerShown_ = false;
    // Under everything: the pointer off any target (the strip's pill goes).
    c.hit({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, left_rail::kHitNothing);
    settleSlides(c.ms);
    const EventTile* f = focused();
    bool corner = f && previewable(*f);
    // The corner takes the place of the big faded badges.
    if (f && !noteShown()) events_ui::backdrop(c, *f, !corner);
    if (noteShown()) drawNote(c);
    else drawHero(c, f);
    drawClock(c);
    if (!noteShown()) drawScoresLine(c, f);
    // No live mark on the picture, as in the guide: live is the status's red.
    if (corner) top_bar::preview(c, app_.playback(), f->channelLogo, f->channelName, kPreview, 18, false);
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

  // The timing log: a line at most every minute while the board is drawn.
  // Counts and milliseconds only.
  void logStats(uint32_t ms) {
    if (!stats_.since) stats_.since = ms;
    if (ms - stats_.since < kLogMs) return;
    if (stats_.frames || stats_.refreshes) {
      // With the logos decoded and prepared since the last line (worker ms).
      logos::LogoCache::Stats l = app_.logos().takeStats();
      MLOG_I("events",
             "board frames=%d frameMax=%.1fms refreshes=%d refreshMax=%.1fms tiles=%zu logos=%d logoPrepMax=%.1fms "
             "logoPrepAtOnce=%d logoLoadsAtOnce=%d",
             stats_.frames, stats_.frameMax, stats_.refreshes, stats_.refreshMax, tiles_, l.prepared, l.prepareMaxMs,
             l.maxPreparing, l.maxInFlight);
    }
    stats_ = Stats{};
    stats_.since = ms;
  }

  static bool inRange(int id, int base) { return id >= base && id < base + 100; }

  void wakeFor(uint32_t at) {
    if (!wake_ || at < wake_) wake_ = at;
  }

  // Still loading (for kLoadingAtMostMs at most): placeholders show.
  bool placeholders() const {
    return (board_->loading || board_->more) && loadingSince_ && app_.ms() - loadingSince_ < kLoadingAtMostMs;
  }

  // ---------------------------------------------------------------- left rail

  void openColumn(bool fromMenu) {
    place_ = {row_, tile_, focusId_, focusRow_};
    nav_.openColumn(row_, static_cast<int>(rows().size()), fromMenu);
    syncSlides();
  }

  // The same game tile the column opened on (found again if the board was
  // rebuilt meanwhile).
  void restorePlace() {
    engine::views::BoardFocus f =
        engine::views::boardFocusShown(rows(), place_.rowId, place_.id, place_.row, place_.tile);
    row_ = f.row;
    tile_ = f.tile;
    focusId_ = place_.id;
    focusRow_ = place_.rowId;
  }

  void apply(const rail::Step& s, bool fromMenu) {
    using Do = rail::Step::Do;
    int n = static_cast<int>(rows().size());
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

  // A new board: shown as it is (the rows shown point into it), its rows
  // laid out once, the focus kept on its game.
  void refresh() {
    Clock::time_point start = Clock::now();
    bool wasLoading = board_ && board_->loading;
    bool onChoose = board_ && chooseFocused();
    board_ = app_.store().events();
    version_ = board_->version;
    // A load's placeholders count from when they first showed, or showed
    // again in place of the rows (the TV woke on old games).
    if (!board_->loading && !board_->more) loadingSince_ = 0;
    else if (!loadingSince_ || (board_->loading && !wasLoading)) loadingSince_ = std::max<uint32_t>(1, app_.ms());
    showRows();
    makeRows();
    layoutRows();
    engine::views::BoardFocus f = engine::views::boardFocusShown(rows(), focusRow_, focusId_, row_, tile_);
    row_ = f.row;
    tile_ = f.tile;
    // On a row's "Choose" tile: still on it.
    if (onChoose) {
      for (size_t r = 0; r < rows().size(); ++r)
        if (rows()[r].row->id == focusRow_ && layout_[r].choose) {
          row_ = static_cast<int>(r);
          tile_ = static_cast<int>(rows()[r].tiles.size());
        }
    }
    // The one-time note: it has the focus when it first shows.
    if (noteShown() && !noteOffered_) {
      noteOffered_ = true;
      noteFocus_ = true;
      noteButton_ = 0;
    }
    stats_.refreshMax = std::max(stats_.refreshMax, msSince(start));
    ++stats_.refreshes;
  }

  // The rows shown: the board's own, or a filter's (pointing into the
  // board either way).
  const std::vector<ShownRow>& rows() const { return rows_; }
  // None while the board is still loading (Build 123: rows already in wait
  // under the placeholders until the whole board is, so nothing moves).
  void makeRows() {
    rowsHidden_ = board_->loading && placeholders();
    if (rowsHidden_) rows_.clear();
    else rows_ = shownRows(show_, sport_);
  }

  // Show on the board (Build 119): the board's rows without More events,
  // Time not listed and Event channels unless switched on (their indices).
  // The board itself keeps them.
  void showRows() {
    shows_ = shows();
    shown_ = engine::views::boardShownRows(board_->rows, shows_);
    hides_ = shown_.size() != board_->rows.size();
  }
  // Show on the board's switches; with "I don't watch sports" saved, More
  // events is where every game is, so it shows whatever the switch says.
  BoardShows shows() const {
    BoardShows s = boardShows(app_.store().settings());
    const sports::MySportsPrefs& my = app_.store().mySports();
    if (my.configured && my.leagues.empty()) s.more = true;
    return s;
  }
  bool showsChanged() const {
    BoardShows now = shows();
    return now.more != shows_.more || now.untimed != shows_.untimed || now.families != shows_.families;
  }
  // The empty board's button: rows left out have something in them.
  bool showHiddenButton() const { return rows().empty() && !filtered() && hides_ && !placeholders(); }
  void showHidden() { openAllSettings(app_, SettingsCategory::LiveEvents, kShowOnBoard); }

  // Each row's tiles in order, where they sit, its height and header text:
  // worked out when the rows change, not as they are drawn.
  void layoutRows() {
    const std::vector<ShownRow>& all = rows();
    layout_.assign(all.size(), RowLayout{});
    tiles_ = 0;
    for (size_t r = 0; r < all.size(); ++r) {
      const ShownRow& row = all[r];
      RowLayout& l = layout_[r];
      l.x.reserve(row.tiles.size());
      l.groupX.reserve(row.groups.size());
      float x = 0;
      bool firstGroup = true;
      for (const ShownRow::Group& g : row.groups) {
        if (!firstGroup) x += kGroupGap;
        firstGroup = false;
        l.groupX.push_back(x);
        if (!g.group->title.empty()) l.labels = true;
        for (size_t i = g.begin; i < g.end; ++i) {
          l.x.push_back(x);
          x += events_ui::kTileW + kTileGap;
        }
      }
      l.end = x;  // and the "Choose" tile's place
      l.height = kRowHeader + (l.labels ? kGroupLabel : 0) + events_ui::kTileH + kRowGap;
      l.mixed = mixed(row);
      // Never a count: how the row is filtered, after its title.
      if (!row.row->subtitle.empty()) l.subtitle = "\xc2\xb7 " + row.row->subtitle;
      // A row that can be changed ends with a way to change it (not under
      // a Live now or My teams filter).
      l.choose = !row.row->choose.empty() && !row.tiles.empty() && (show_ == Show::All || show_ == Show::Sport);
      if (l.choose) l.chooseWhat = row.row->choose;
      tiles_ += row.tiles.size();
    }
  }

  const std::vector<const EventTile*>& flat(int row) const {
    static const std::vector<const EventTile*> kNone;
    if (row < 0 || row >= static_cast<int>(rows_.size())) return kNone;
    return rows_[static_cast<size_t>(row)].tiles;
  }

  // Tiles in a row, its "Choose" tile included.
  int count(int row) const {
    int n = static_cast<int>(flat(row).size());
    if (n > 0 && row >= 0 && row < static_cast<int>(layout_.size()) && layout_[static_cast<size_t>(row)].choose) ++n;
    return n;
  }
  bool chooseFocused() const {
    if (row_ < 0 || row_ >= static_cast<int>(layout_.size()) || !layout_[static_cast<size_t>(row_)].choose) return false;
    return tile_ == static_cast<int>(flat(row_).size());
  }

  // The focused game (none on a row's "Choose" tile).
  const EventTile* focused() {
    const std::vector<const EventTile*>& tiles = flat(row_);
    if (tiles.empty()) return nullptr;
    tile_ = std::clamp(tile_, 0, count(row_) - 1);
    if (chooseFocused()) {
      focusId_.clear();
      focusRow_ = rows()[static_cast<size_t>(row_)].row->id;
      return nullptr;
    }
    focusId_ = tiles[static_cast<size_t>(tile_)]->id;
    focusRow_ = rows()[static_cast<size_t>(row_)].row->id;
    return tiles[static_cast<size_t>(tile_)];
  }

  void setRow(int r) {
    // Keep roughly the same column when moving between rows.
    float x = tileX(row_, tile_);
    row_ = r;
    size_t n = static_cast<size_t>(count(row_));
    int best = 0;
    float bestDist = 1e9f;
    for (size_t i = 0; i < n; ++i) {
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
    if (row < 0 || row >= static_cast<int>(layout_.size())) return 0;
    const RowLayout& l = layout_[static_cast<size_t>(row)];
    if (index >= 0 && static_cast<size_t>(index) < l.x.size()) return l.x[static_cast<size_t>(index)];
    return l.end;
  }

  void close() {
    stopPreview();
    app_.nav().pop();
  }

  void select(const EventTile& t) {
    // An event channel family: its first feed, Up/Down surfs the others.
    if (row_ < static_cast<int>(rows().size()) && rows()[static_cast<size_t>(row_)].row->families) {
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
    // Live, likely over or untimed with somewhere to watch, or starting
    // within half an hour on a channel: play. Otherwise the detail card.
    if (engine::views::okPlays(t, time::now())) {
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

  // OK held on a game: Where to Watch, Remind Me, and (Build 127) Follow
  // each team and Hide its competition.
  void openMenu(const EventTile& t) {
    std::string id = t.id;
    bool upcoming = t.upcoming;
    std::string kicker = str::upper(engine::matchup(t));
    std::string title = t.competitionName.empty() ? std::string("Game") : t.competitionName;
    std::vector<EventTile::FollowSide> follow = t.follow;
    std::string hideId = t.competitionId, hideName = t.competitionName;
    std::string hideLine = hideId.empty() ? std::string() : events_ui::hideLine(app_, t);
    App& app = app_;
    app_.nav().push(std::make_unique<PanelScreen>(app_, PanelScreen::Side::Trailing, 640.0f,
                                                  [&app, id, upcoming, kicker, title, follow, hideId, hideName,
                                                   hideLine](ui::ListPanel& p) {
      p.subtitle = kicker;
      p.title = title;
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
      for (const EventTile::FollowSide& side : follow) {
        if (side.key.empty()) continue;
        bool on = app.store().followsTeam(side.key);
        ui::PanelItem it{ui::PanelItem::Kind::Action, (on ? "Unfollow " : "Follow ") + side.name};
        std::string key = side.key, name = side.name;
        it.select = [&app, key, name] {
          bool now = !app.store().followsTeam(key);
          app.store().followTeam(key, now);
          app.toast(now ? "Following " + name : "Stopped following " + name);
        };
        p.items.push_back(it);
      }
      if (!hideId.empty()) {
        ui::PanelItem hide{ui::PanelItem::Kind::Action, "Hide " + hideName};
        hide.detail = hideLine;
        hide.select = [&app, hideId, hideName] {
          app.nav().pop();
          events_ui::hide(app, hideId, hideName);
        };
        p.items.push_back(hide);
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

  // The rows shown (Show on the board) with a filter applied, pointing
  // into the board. Live now and My teams keep each sport's own row (the
  // board's "Live now" and "Your teams" rows would only repeat them).
  std::vector<ShownRow> shownRows(Show show, const std::string& sport) const {
    std::vector<ShownRow> out;
    out.reserve(shown_.size());
    for (size_t index : shown_) {
      const EventRow& row = board_->rows[index];
      if (show == Show::All || show == Show::Sport) {
        if (show == Show::All || row.id == sport) out.push_back(engine::views::shownRow(row));
        continue;
      }
      if (row.families || row.id == "live" || row.id == "teams" || row.id == "untimed") continue;
      ShownRow kept = engine::views::shownRow(
          row, [show](const EventTile& t) { return show == Show::Live ? isLive(t) : t.favorite; },
          [](const EventTile& t) { return isLive(t) && t.watchable; });
      if (!kept.groups.empty()) out.push_back(std::move(kept));
    }
    return out;
  }

  static int total(const std::vector<ShownRow>& rows) {
    int n = 0;
    for (const ShownRow& r : rows) n += r.count;
    return n;
  }

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
    makeRows();
    layoutRows();
    row_ = tile_ = 0;
    focusId_.clear();
    focusRow_.clear();
    filterFocus_ = false;
  }

  void openFilter() {
    App& app = app_;
    EventsBoardScreen* self = this;
    app_.nav().push(std::make_unique<PanelScreen>(app_, PanelScreen::Side::Trailing, 760.0f,
                                                  [&app, self](ui::ListPanel& p) {
      p.subtitle = "LIVE EVENTS";
      p.title = "Filter";
      auto add = [&](const std::string& title, Show show, const std::string& sport, bool dot) {
        ui::PanelItem it{ui::PanelItem::Kind::Check, title};
        it.on = self->show_ == show && (show != Show::Sport || self->sport_ == sport);
        it.value = std::to_string(total(self->shownRows(show, sport)));
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
      for (size_t index : self->shown_) {
        const EventRow& r = self->board_->rows[index];
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
    return !(row_ < static_cast<int>(rows().size()) && rows()[static_cast<size_t>(row_)].row->families);
  }

  // After focus rests on a live game (the guide's delay), it plays in the
  // corner; focus on a game that isn't on ends the preview. Moving up to
  // the tabs or the sports column leaves it playing.
  void previewTick(uint32_t ms) {
    Playback& p = app_.playback();
    if (app_.nav().top() != this || filterFocus_ || noteFocus_ || nav_.mode() != rail::Mode::Content) return;
    const EventTile* t = focused();
    if (!t || !previewable(*t)) {
      if (p.active() && !p.full()) p.stop();
      pace_.step("", false, ms);
      return;
    }
    switch (pace_.step(t->id, p.entry() == t->id, ms)) {
      case preview::Pace::Step::Idle: return;
      case preview::Pace::Step::Wait:  // waiting out the delay, asleep
        wakeFor(pace_.due());
        return;
      case preview::Pace::Step::Tune: break;
    }
    p.setCorner(kPreview);
    if (!p.tune(t->id, app_.store().eventFlipList(t->id))) p.stop();
  }

  void stopPreview() {
    Playback& p = app_.playback();
    if (p.active() && !p.full()) p.stop();
  }

  // ---------------------------------------------------------------- drawing

  // Not while the board loads (its loading line stays where the hero is).
  bool noteShown() const { return !noteGone_ && board_ && !board_->noteTitle.empty() && !placeholders(); }

  // OK: the note goes. Change: and My Sports opens.
  void noteDone(bool change) {
    noteGone_ = true;
    noteFocus_ = false;
    app_.store().dismissBoardNote();
    if (change) app_.nav().push(makeMySports(app_));
  }

  // The note in the hero's place: what changed, and its two buttons.
  void drawNote(ui::Ctx& c) {
    Rect card{kHeroX, 92, kHeroW, 260};
    c.g.fill(card, theme::hex(0x20232A), 22);
    ui::label(c, board_->noteTitle, card.x + 36, card.y + 30, Weight::Bold, 32, theme::white(), card.w - 72);
    ui::paragraph(c, board_->noteLine, card.x + 36, card.y + 78, card.w - 72, Weight::Regular, 22, theme::white(0.72f),
                  2);
    float bx = card.x + 36, by = card.y + card.h - 76;
    const char* titles[] = {"OK", "Change"};
    bool focusedRow = noteFocus_ && nav_.mode() == rail::Mode::Content;
    for (int i = 0; i < 2; ++i) {
      float w = std::max(150.0f, ui::chipWidth(c, titles[i], 24) + 56);
      Rect b{bx, by, w, 56};
      c.hit(b, kHitNote + i);
      bool f = focusedRow && noteButton_ == i;
      c.g.fill(b, f ? theme::white() : theme::white(0.12f), 28);
      ui::labelIn(c, titles[i], b, Weight::Bold, 24, f ? theme::black() : theme::white(), Align::Center);
      bx += w + 16;
    }
  }

  // "Choose competitions" / "Choose schools": the page that changes the row.
  void openChoose(const EventRow& row) {
    if (row.choose == "schools") openCollegePicks(app_, row.chooseLeague);
    else openSoccerPicks(app_);
  }

  // Build 142: the scores line at the top of the hero, after the
  // competition (or the row's name), left of the clock: as the guide's
  // "Filling in missing listings…" after the channel's section.
  void drawScoresLine(ui::Ctx& c, const EventTile* t) {
    if (placeholders()) return;
    ScoresLine line = app_.store().eventsScoresLine(true);
    if (!line.text) return;
    float x = kHeroX;
    if (t && !t->leagueChip.empty()) {
      x += ui::textWidth(c, t->leagueChip, Weight::Bold, 22) + 24;
    } else if (!t && chooseFocused()) {
      x += ui::textWidth(c, rows()[static_cast<size_t>(row_)].row->title, Weight::Bold, 22) + 24;
    }
    std::string clock = ui::clock(c, c.now);
    float right = kHeroRight - ui::textWidth(c, clock, Weight::Medium, 26) - 16;
    if (right - x < 160) return;  // no room beside a long competition
    const float y = 48;
    if (line.mark == ScoresLine::Mark::Busy) {
      ui::spinner(c, x + 10, y + 14, 10, theme::white(0.8f));
    } else {
      ui::labelIn(c, "!", {x, y + 2, 20, 24}, Weight::Bold, 20, theme::hex(theme::kWarning, 0.9f), Align::Center);
    }
    x += 30;
    ui::label(c, line.text, x, y + 3, Weight::Regular, 18, theme::white(0.45f), right - x);
  }

  // The clock at the column's top right, beside the preview (as in the guide).
  void drawClock(ui::Ctx& c) {
    std::string clock = ui::clock(c, c.now);
    float w = ui::textWidth(c, clock, Weight::Medium, 26);
    ui::label(c, clock, kHeroRight - w, 44, Weight::Medium, 26, theme::white(0.7f));
  }

  // The column at the preview's left, laid out as the guide's: the
  // competition, the channel, the matchup, its status and progress, where to
  // watch it and where it's played.
  void drawHero(ui::Ctx& c, const EventTile* t) {
    const float x = kHeroX;
    if (!t && chooseFocused()) {
      // On a row's "Choose" tile: what it changes.
      const EventRow& row = *rows()[static_cast<size_t>(row_)].row;
      bool schools = row.choose == "schools";
      ui::label(c, row.title, x, 48, Weight::Bold, 22, theme::white(0.6f), kHeroW - 200);
      ui::label(c, schools ? "Choose schools" : "Choose competitions", x, 140, Weight::Bold, 50, theme::white(), kHeroW);
      ui::paragraph(c,
                    schools ? "Pick the schools and conferences in your " + row.title + " row."
                            : "Pick the competitions in your " + row.title + " row.",
                    x, 214, kHeroW, Weight::Regular, 22, theme::white(0.72f), 3);
      return;
    }
    if (!t) {
      ui::label(c, "Live Events", x, 140, Weight::Bold, 50, theme::white(), kHeroW);
      if (placeholders()) {
        // Still loading (Build 124): a spinner and what the load waits for
        // now, by the store; after a while, that the first load of the day
        // is the slow one.
        skeletonShown_ = spinnerShown_ = true;
        ui::spinner(c, x + 16, 231, 14, theme::white(0.85f));
        if (const char* line = stageLine(board_->stage))
          ui::label(c, line, x + 46, 214, Weight::Medium, 25, theme::white(0.85f), kHeroW - 46);
        if (c.ms - loadingSince_ >= kSlowLoadMs)
          ui::label(c, "Almost there. The first load of the day takes longest.", x + 46, 258, Weight::Regular, 22,
                    theme::white(0.55f), kHeroW - 46);
        return;
      }
      ui::label(c, "This week's games, and where you can watch them.", x, 214, Weight::Regular, 22, theme::white(0.72f),
                kHeroW);
      return;
    }
    // The competition, where the guide has the channel's section.
    if (!t->leagueChip.empty()) ui::label(c, t->leagueChip, x, 48, Weight::Bold, 22, theme::white(0.6f), kHeroW - 200);
    // The channel line: logo and name.
    float y = 92;
    if (!t->channelLogo.empty() || !t->channelName.empty()) {
      float nx = x;
      if (!t->channelLogo.empty()) {
        ui::logo(c, t->channelLogo, "", {x, y, 96, 36});
        nx += 112;
      }
      ui::label(c, t->channelName, nx, y + 2, Weight::Medium, 26, theme::white(), kHeroRight - nx);
    } else if (!t->pill2.empty()) {
      ui::label(c, t->pill2, x, y + 2, Weight::Medium, 26, theme::white(0.7f), kHeroW);
    }
    // The matchup, with its two badges.
    float tx = x;
    if (t->sides.size() == 2) {
      events_ui::badge(c, t->sides[0], tx, 138, 56, false);
      events_ui::badge(c, t->sides[1], tx + 68, 138, 56, false);
      tx += 148;
    }
    ui::label(c, t->heroTitle.empty() ? t->title : t->heroTitle, tx, 140, Weight::Bold, 50, theme::white(),
              kHeroRight - tx);
    // The state and status, as on the tile: live is red text alone (its
    // clock, or "LIVE" without one), never a pill.
    events_ui::status(c, *t, t->heroStatus.empty() ? t->status : t->heroStatus, x, 210, Weight::Medium, 25,
                      theme::white(), kHeroW, 4, 12);
    if (t->progress >= 0) ui::progress(c, {x, 260, 420, 6}, t->progress);
    // Where to watch it, and where it's played.
    y = t->progress >= 0 ? 286 : 256;
    if (!t->whereLine.empty()) {
      y += ui::paragraph(c, t->whereLine, x, y, kHeroW, Weight::Regular, 22, theme::white(0.72f), 2) + 8;
    }
    std::string place = t->venue;
    if (!t->detail.empty() && t->detail != place) place = place.empty() ? t->detail : place + " \xc2\xb7 " + t->detail;
    if (!place.empty() && y < 380) ui::label(c, place, x, y, Weight::Regular, 20, theme::white(0.55f), kHeroW);
  }

  // The filter button at the end of the first row's header line, and its
  // chip to its left (the tab row they sat in is gone).
  void drawTop(ui::Ctx& c) {
    Rect button{kRight - 40, kBoardTop - 6, 40, 40};
    bool focusedButton = filterFocus_ && nav_.mode() == rail::Mode::Content;
    top_bar::filterButton(c, button, focusedButton, filtered(), kHitFilter);
    float right = button.x - 16;
    if (filtered()) {
      std::string text = "\xe2\x97\x8f " + filterTitle() + "  \xc3\x97";
      right -= ui::chipWidth(c, text, 19);
      ui::chip(c, text, right, kBoardTop - 1, 19, theme::hex(theme::kNowLine, 0.3f), theme::white());
    }
  }


  void drawRows(ui::Ctx& c) {
    if (rows().empty() && placeholders()) {
      drawLoadingRows(c, kBoardTop);
      return;
    }
    if (rows().empty()) {
      const char* empty = !filtered()           ? "No games this week in your sports."
                          : show_ == Show::Live ? "No games on right now. Back shows them all."
                                                : "None this week. Back shows every game.";
      ui::label(c, empty, left_, kBoardTop + 30, Weight::Medium, 26, theme::white(0.6f));
      if (showHiddenButton()) drawShowHidden(c);
      return;
    }
    // The focused row sits at the top of the board area.
    float y = kBoardTop;
    float scroll = 0;
    for (int r = 0; r < row_; ++r) scroll += layout_[static_cast<size_t>(r)].height;
    // At rest a scrolled row runs under the strip; with SPORTS open it stops
    // at the column's rule.
    float clipX = left_rail::kStripW + (left_ - left_rail::kRestLeft) *
                                           (left_rail::kColumnX + left_rail::kColumnW + 15 - left_rail::kStripW) /
                                           (left_rail::kPushLeft - left_rail::kRestLeft);
    c.g.pushClip({clipX, kBoardTop - 10, gfx::kLayoutWidth - clipX, kBoardBottom - kBoardTop + 10});
    y -= scroll;
    for (size_t r = 0; r < rows().size(); ++r) {
      const ShownRow& row = rows()[r];
      float h = layout_[r].height;
      if (y > kBoardBottom) break;
      if (y + h >= kBoardTop) drawRow(c, row, static_cast<int>(r), y);
      y += h;
    }
    // More to come: placeholder rows after the ones in.
    if (board_->more && placeholders()) drawLoadingRows(c, y);
    c.g.popClip();
  }

  // Under the empty board's line: what is hidden, and the way to show it
  // (focused unless the filter button is).
  void drawShowHidden(ui::Ctx& c) {
    float y = kBoardTop + 30;
    ui::label(c, engine::views::hiddenLine(engine::views::boardHidden(board_->rows, shows_)), left_, y + 46,
              Weight::Regular, 22, theme::white(0.5f));
    const char* text = "Show them in Settings";
    Rect b{left_, y + 104, ui::textWidth(c, text, Weight::Bold, 24) + 60, 64};
    c.hit(b, kHitShowHidden);
    bool f = !filterFocus_ && nav_.mode() == rail::Mode::Content;
    if (f) {
      c.g.fill(b, theme::white(), 32);
    } else {
      c.g.fill(b, theme::white(0.1f), 32);
      c.g.stroke(b, theme::white(0.22f), 32, 1);
    }
    ui::labelIn(c, text, b, Weight::Bold, 24, f ? theme::black() : theme::white(), Align::Center);
  }

  // Rows of tile-shaped placeholders from `y` down the board while the
  // week's games load: a row's title, then its tiles where tiles go. Flat
  // shapes only (no text); nothing on them takes the focus.
  void drawLoadingRows(ui::Ctx& c, float y) {
    skeletonShown_ = true;
    ui::Color block = ui::skeleton(c);
    while (y + kRowHeader < kBoardBottom) {
      c.g.fill({left_, y + 4, 220, 22}, block, 11);
      float tileY = y + kRowHeader;
      for (float x = left_; x < gfx::kLayoutWidth; x += events_ui::kTileW + kTileGap)
        c.g.fill({x, tileY, events_ui::kTileW, events_ui::kTileH}, block, 12);
      y += kRowHeader + events_ui::kTileH + kRowGap;
    }
  }

  void drawRow(ui::Ctx& c, const ShownRow& row, int r, float y) {
    const RowLayout& l = layout_[static_cast<size_t>(r)];
    float hx = left_;
    hx += ui::label(c, row.row->title, hx, y, Weight::Bold, 25, theme::white()) + 10;
    // No counts: only how the row is filtered, in gray.
    if (!l.subtitle.empty()) ui::label(c, l.subtitle, hx, y + 4, Weight::Medium, 21, theme::white(0.5f), kRight - hx);
    float tileY = y + kRowHeader + (l.labels ? kGroupLabel : 0);
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
    for (size_t k = 0; k < row.groups.size(); ++k) {
      const ShownRow::Group& g = row.groups[k];
      float gx = left_ + l.groupX[k] - scrollX;
      const std::string& title = g.group->title;
      if (!title.empty() && gx > -400 && gx < gfx::kLayoutWidth)
        ui::label(c, title, gx, y + kRowHeader, Weight::Bold, 20, theme::white(0.75f));
      for (size_t i = g.begin; i < g.end; ++i) {
        float tx = left_ + l.x[i] - scrollX;
        if (tx + events_ui::kTileW > 0 && tx < gfx::kLayoutWidth) {
          int index = static_cast<int>(i);
          bool f = !filterFocus_ && !noteFocus_ && nav_.mode() == rail::Mode::Content && r == row_ && index == tile_;
          Rect rect{tx, tileY, events_ui::kTileW, events_ui::kTileH};
          c.hit(rect, kHitTile + r * 1000 + std::min(index, 999));
          events_ui::tile(c, *row.tiles[i], rect, f, l.mixed);
        }
      }
    }
    if (l.choose) {
      float tx = left_ + l.end - scrollX;
      int index = static_cast<int>(row.tiles.size());
      if (tx + events_ui::kTileW > 0 && tx < gfx::kLayoutWidth) {
        bool f = !filterFocus_ && !noteFocus_ && nav_.mode() == rail::Mode::Content && r == row_ && index == tile_;
        Rect rect{tx, tileY, events_ui::kTileW, events_ui::kTileH};
        c.hit(rect, kHitTile + r * 1000 + std::min(index, 999));
        events_ui::chooseTile(c, l.chooseWhat, rect, f);
      }
    }
  }

  // A row with more than one league names the league on each tile.
  // A row of more than one sport: its tiles name theirs.
  static bool mixed(const ShownRow& row) {
    const EventTile* first = nullptr;
    for (const EventTile* t : row.tiles) {
      if (!first) first = t;
      else if (t->sport != first->sport || t->sport.empty()) return true;
    }
    return first && first->sport.empty();
  }

  void drawColumn(ui::Ctx& c, float alpha) {
    // SPORTS, inside the board: each row's sport and how many are live.
    std::vector<left_rail::Entry> entries;
    entries.reserve(rows().size());
    for (const ShownRow& row : rows()) entries.push_back({row.row->title, row.live});
    int hover = c.pointerActive && inRange(pointer_, left_rail::kHitColumn) ? pointer_ - left_rail::kHitColumn : -1;
    left_rail::column(c, "Live Events", "Sports", entries, nav_.index(), nav_.origin(), hover, kBoardTop,
                      kBoardTop + 52, alpha);
  }

  // Where the focus was when SPORTS opened (the return rule).
  struct Place {
    int row = 0, tile = 0;
    std::string id, rowId;
  };
  // A row as laid out (layoutRows()); its tiles are its ShownRow's.
  struct RowLayout {
    std::vector<float> x;                 // each tile's x before scrolling
    std::vector<float> groupX;            // each group's (its label's)
    float end = 0;                        // past the last tile
    float height = 0;
    bool labels = false;  // group titles above the tiles
    bool mixed = false;   // tiles name their sport (mixed())
    std::string subtitle;  // "· Top 25 and your schools" (never a count)
    bool choose = false;   // ends with a "Choose" tile, at `end`
    std::string chooseWhat;  // "competitions", "schools"
  };
  struct Stats {
    int frames = 0, refreshes = 0;
    double frameMax = 0, refreshMax = 0;
    uint32_t since = 0;
  };

  uint32_t promptAt_ = 0;
  uint32_t wake_ = 0;  // the preview's rest ends then (wakeAt())
  bool skeletonShown_ = false;  // placeholders on screen: the pulse redraws
  bool spinnerShown_ = false;   // the loading line's spinner too: at its steps
  const char* scoresShown_ = nullptr;  // the scores line drawn (Build 142)
  uint32_t scoresStep_ = 0;
  uint32_t pulseStep_ = 0;
  std::function<void(rail::Item, bool)> go_;
  rail::Nav nav_;
  Place place_;
  left_rail::Slide push_, menu_;  // SPORTS' push, the menu's slide
  float left_ = left_rail::kRestLeft;  // the rows' left edge
  int pointer_ = 0;  // what the pointer is on (a hit id) while it shows
  std::shared_ptr<const EventsBoard> board_;
  std::vector<ShownRow> rows_;   // rows(): into board_, with any filter
  std::vector<size_t> shown_;    // the board's rows shown (Show on the board)
  bool hides_ = false;           // a row is left out
  BoardShows shows_;
  std::vector<RowLayout> layout_;   // rows()'s
  size_t tiles_ = 0;
  uint32_t loadingSince_ = 0;  // placeholders first shown (app ms; 0: none)
  bool rowsHidden_ = false;    // the board's rows wait under the placeholders (makeRows())
  Stats stats_;
  Show show_ = Show::All;
  std::string sport_;  // a "sport|..." row id
  uint64_t version_ = ~0ull;
  int row_ = 0, tile_ = 0;
  std::string focusId_, focusRow_;  // the focused game and its row (ids)
  bool filterFocus_ = false;  // on the filter button
  // The one-time note after the update (the board's noteTitle): shown in
  // place of the hero until OK or Change; it has the focus when it first
  // shows.
  bool noteFocus_ = false;
  bool noteOffered_ = false;
  bool noteGone_ = false;  // dismissed here (the next board has none)
  int noteButton_ = 0;     // 0 OK, 1 Change
  float rowScroll_ = 0;
  preview::Pace pace_{true};  // the corner: after a rest, each game once
};

}  // namespace

std::unique_ptr<Screen> makeEventsBoard(App& app, std::function<void(rail::Item, bool)> go, bool sports) {
  return std::make_unique<EventsBoardScreen>(app, std::move(go), sports);
}

}  // namespace marigold::app
