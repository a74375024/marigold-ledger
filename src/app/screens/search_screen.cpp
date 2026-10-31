// Search (Build 113): a section of the left rail, the same on every TV. Our
// own alphabetical keyboard on the left (core/input/search_keys.hpp), live
// results on the right: the top result beside the guide's preview corner,
// then Channels, Teams, Games and Shows (core/engine/search.hpp decides
// what each finds; this screen only lays them out). Before typing: your
// teams' games while live or within the hour (else Recently watched),
// one-press chips, Recently watched and recent searches.
//
// The corner shows the top result's card while you type. With focus in the
// results, what's on now plays there, muted, after a 2 s rest (not on the
// first item focus lands on from the keyboard); OK goes full screen on the
// same stream with sound. Back from the player within 2 minutes returns
// here with the stream in the corner. Results don't move while focus is in
// them. Never a score, and finished games only with Show scores on.
//
// Build 115: the keyboard's letters and 123 pages (search_keys.hpp), and
// your lineup and Live Events first once a lineup is set up. The header
// says so; a last row names what else matches outside and shows it
// (EVERYTHING, those items marked "Outside your lineup"), and then goes
// back. That choice holds while Search is open. With nothing in your
// lineup, what's outside shows at once under its own header. Never a
// count.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/left_rail.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/screens/top_bar.hpp"
#include "app/store/store.hpp"
#include "app/ui/keyboard.hpp"
#include "core/engine/views.hpp"
#include "core/input/hints.hpp"
#include "core/input/preview_pace.hpp"
#include "core/input/search_keys.hpp"
#include "core/sports/my_sports.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"
#include "platform/icons.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;
namespace se = engine::search;
namespace kb = ui::keyboard;
using search_keys::Grid;

constexpr float kX = 760, kRight = 1840, kW = kRight - kX;
constexpr float kBandW = 656;
constexpr float kListTop = 330, kListBottom = 990;
constexpr float kTileW = 530, kTileH = 112, kRowH = 96, kChipH = 56;
constexpr uint32_t kShowsDelayMs = 450;   // shows are looked for once typing pauses
constexpr uint32_t kDigitsMs = 1500;      // number keys: the pause that ends a number
constexpr uint32_t kReturnMs = 120000;    // Back from the player within this: the corner
constexpr uint32_t kRestoreMs = 300000;   // reopened within this: the last query
constexpr uint32_t kRefreshMs = 3000;     // new data redraws the lists at most this often
constexpr uint32_t kLogMs = 10000;        // the timing log's line, at most this often
constexpr int kHitKeyboard = 1000, kHitCorner = 1600, kHitPill = 1601, kHitCells = 2000;

// The last query, for reopening within five minutes.
struct Last {
  std::string query;
  uint32_t at = 0;
  bool set = false;
};
Last gLast;

enum class CellKind { Channel, Team, Game, Show, Genre, Chip, Recent, ClearRecents, Instead, Follow, Scope };

struct Cell {
  CellKind kind = CellKind::Channel;
  std::string id;     // entry id, team key, event id, genre or chip key, recent id
  std::string title;  // chips, genres, recent searches
  std::string entryId;
  Listing listing;    // shows
  time::Seconds also = 0;
  bool top = false;   // the band's top result card
  bool outside = false;  // marked "Outside your lineup"
  // What it shows, made by label() after a layout, each minute and when
  // the data changes: drawing makes no text.
  struct Label {
    bool missing = false;  // its channel, game or team is gone: only the box
    std::string title, line, tag, ok;
    bool live = false, west = false, logo = false;
    std::string logoUrl, logoName;  // a channel's or show's logo; a game's channel
    std::vector<EventSide> sides;   // a game's two badges, or a team's one
    std::string cardTag, cardTitle, cardLineOk;  // the top result's card
    std::string cornerLogo, cornerName, cornerTitle, cornerLine;  // the corner's
  } label;
};

enum class RowKind { Band, Tiles, Chips, List, Note };  // Note: one quiet line (its header), no cells

struct Row {
  RowKind kind = RowKind::List;
  std::string header;
  std::vector<Cell> cells;
  float y = 0, h = 0;
};

bool liveTile(const EventTile& t) {
  if (t.upcoming || t.finished) return false;
  using P = EventTile::Pill;
  return t.pill == P::Live || t.pill == P::LiveOutline || t.pill == P::OnNow || t.pill == P::Delayed;
}

// "in 20 min", "Tonight 7:30 PM", "Tomorrow 7:30 PM", "Sat 7:30 PM".
std::string whenText(const ui::Ctx& c, time::Seconds t) {
  time::Seconds d = t - c.now;
  if (d > 0 && d <= 3600) return "in " + std::to_string((d + 59) / 60) + " min";
  std::string day = ui::dayWord(c, t);
  return (day.empty() ? std::string("Today") : day) + " " + ui::clock(c, t);
}

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

constexpr const char* kOutside = "Outside your lineup";

// The same query for what it finds (the shows kept for it).
bool sameQuery(const se::Query& a, const se::Query& b) {
  return a.text == b.text && a.tokens == b.tokens && a.when == b.when && a.exactOnly == b.exactOnly &&
         a.digits == b.digits && a.regional == b.regional;
}

std::string recentId(const Cell& cell) {
  switch (cell.kind) {
    case CellKind::Channel: return "c:" + cell.id;
    case CellKind::Team: return "t:" + cell.id;
    case CellKind::Game: return "g:" + cell.id;
    case CellKind::Show: return "s:" + se::fold(cell.listing.title);
    case CellKind::Genre: return "x:" + cell.id;
    default: return {};
  }
}

class SearchScreen : public Screen {
 public:
  bool holdsOk() const override { return true; }
  bool keepsPreview() const override { return true; }

  SearchScreen(App& app, std::function<void(rail::Item)> go)
      : Screen(app), go_(std::move(go)), nav_(rail::Item::Search) {
    // Search holds the Live Events data lifecycle while it shows (the
    // existing caps apply), so games and their states are fresh.
    app_.store().eventsOpen(true);
    nav_.setItems(rail::items(app_.store().settings().allChannelsTab));
    RemoteKeys platform = app_.window().remoteKeys();
    // 🎤 Speak where the system keyboard takes dictation: LG, Android TV and
    // Apple TV (not Fire TV, until Amazon's keyboard is known to take it).
    grid_.setSpeak(app_.window().keyboardAvailable() && !(platform == RemoteKeys::fireTv()));
    appleTv_ = platform.clickpad;
    // The guide's channel preview doesn't carry over.
    Playback& p = app_.playback();
    if (p.active() && !p.full()) p.stop();
    if (gLast.set && app_.ms() - gLast.at < kRestoreMs) query_ = gLast.query;
    search(true);
  }
  ~SearchScreen() override {
    logStats(app_.ms(), true);
    app_.store().eventsOpen(false);
  }

  void shown() override {
    Playback& p = app_.playback();
    if (p.active() && p.full()) {
      // Back from the player: within two minutes, the same row with the
      // stream still playing in the corner (muted); later, Search is gone.
      if (watchedAt_ && app_.ms() - watchedAt_ <= kReturnMs) {
        p.setCorner(top_bar::kPreview, true);
        pace_.reset();
      } else {
        close(false);
        return;
      }
    }
    watchedAt_ = 0;
    labelsDirty_ = true;  // a panel may have set a reminder or followed a team
    if (!inResults()) search(true);
  }

  void text(const std::string& value, bool closed, bool) override {
    // The system keyboard (🎤 Speak): its text comes back into our field.
    if (value != query_) setQuery(value);
    if (closed) {
      grid_.set({Grid::Spot::Area::Speak, 0, 0});
      focusKeyboard();
    }
  }

  bool tick(uint32_t ms) override {
    bool draw = false;
    wake_ = 0;
    // The shows once typing pauses, added to what each letter found.
    if (showsAt_) {
      if (ms >= showsAt_) {
        showsAt_ = 0;
        search(true, true);
        draw = true;
      } else {
        wakeFor(showsAt_);
      }
    }
    if (!digits_.empty() && digitsTick(ms)) draw = true;
    // New channels, guide, board or index: the lists again (never under the
    // focus, and at most every few seconds while the guide is still loading).
    Store& s = app_.store();
    bool fresh = s.events()->version != eventsVersion_ || s.channels()->version != channelsVersion_ ||
                 s.listings()->version != listingsVersion_ || s.searchIndex() != index_;
    if (fresh && !inResults()) {
      if (ms - refreshedAt_ >= kRefreshMs) {
        refreshedAt_ = ms;
        search(!showsAt_);
        draw = true;
      } else {
        wakeFor(refreshedAt_ + kRefreshMs);
      }
    }
    // The rows' text is made again for new data (the rows stay put).
    if (labeled_ != versions()) {
      labelsDirty_ = true;
      draw = true;
    }
    previewTick(ms);
    logStats(ms, false);
    return draw || menu_.moving(ms);
  }

  uint32_t wakeAt() const override { return wake_; }

  bool key(const KeyEvent& e) override {
    Key k = e.key;
    if (k != Key::Ok) pointer_ = 0;
    if (e.longPress) {
      if (inResults()) hold();
      return true;
    }
    if (k == Key::Ok && pointer_ && click()) return true;
    if (nav_.mode() != rail::Mode::Content) {
      apply(nav_.key(k));
      return true;
    }
    if (!digits_.empty() && digitsKey(e)) return true;
    // Red clears the typed text (never recent searches), back on the letters.
    if (k == Key::Red) {
      setQuery("");
      focusKeyboard();
      grid_.letters();
      return true;
    }
    // Play/Pause changes the keyboard's page on Apple TV, as on Apple's own.
    if (k == Key::PlayPause && appleTv_ && !inResults()) {
      grid_.flip();
      return true;
    }
    if (int d = digitOf(k); d >= 0) {
      digit(d);
      return true;
    }
    return inResults() ? resultsKey(e) : keyboardKey(e);
  }

  void hover(int id) override {
    pointer_ = id;
    if (nav_.mode() == rail::Mode::Menu) {
      if (id >= left_rail::kHitMenu && id < left_rail::kHitMenu + 100) nav_.hoverMenu(id - left_rail::kHitMenu);
      return;
    }
    Grid::Spot spot;
    if (kb::spotFor(id, kHitKeyboard, grid_.page(), spot)) {
      grid_.set(spot);
      focusKeyboard();
    } else if (id >= kHitCells) {
      int r = (id - kHitCells) / 100, col = (id - kHitCells) % 100;
      if (r < static_cast<int>(rows_.size()) && col < static_cast<int>(rows_[static_cast<size_t>(r)].cells.size())) {
        if (!inResults()) enterResults(false);
        row_ = r;
        col_ = col;
        // With the pointer, the rest starts once it moved.
        pace_.moved(app_.ms());
      }
    }
  }

  void draw(ui::Ctx& c) override {
    Clock::time_point start = Clock::now();
    if (labelsDirty_ || c.now / 60 != labelMinute_) labelAll(c);
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(theme::kBackground));
    c.hit({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, left_rail::kHitNothing);
    kb::Look look;
    look.query = query_;
    look.focused = !inResults();
    look.tab = !query_.empty() && hasResults() && !inResults();
    kb::draw(c, grid_, look, kHitKeyboard);
    if (appleTv_ && grid_.speak())
      ui::paragraph(c, "To speak: choose Speak, then hold the microphone button on the remote and say what you want.",
                    kb::kLeft, kb::kBottom + 36, 520, Weight::Regular, 22, theme::white(0.6f), 3);
    top_bar::clock(c);
    drawRows(c);
    drawCorner(c);
    drawKeyBar(c);
    int hover = -1;
    if (c.pointerActive && nav_.mode() != rail::Mode::Menu && pointer_ >= left_rail::kHitStrip &&
        pointer_ < left_rail::kHitStrip + 100)
      hover = pointer_ - left_rail::kHitStrip;
    left_rail::strip(c, nav_.items(), rail::Item::Search, hover);
    float m = menu_.at(c.ms);
    if (m > 0.01f) left_rail::menu(c, nav_.items(), rail::Item::Search, nav_.menuFocus(), m);
    double ms = msSince(start);
    stats_.frameMax = std::max(stats_.frameMax, ms);
    if (ms >= 50) ++stats_.over;
  }

 private:
  // A chip's page in the results column ("PBL ×").
  struct Page {
    std::string id, title;
    bool empty() const { return id.empty(); }
  };

  bool inResults() const { return results_; }

  bool hasResults() const {
    for (const Row& r : rows_)
      if (!r.cells.empty()) return true;
    return false;
  }

  // ---------------------------------------------------------------- query

  void setQuery(const std::string& q) {
    query_ = q;
    page_ = {};
    if (q.empty()) exactOnly_ = false;
    leaveDigits();
    showsAt_ = 0;  // shows wait for a pause in the typing
    search(false);
  }

  se::Query parsed() const {
    std::shared_ptr<const se::Index> index = index_;
    se::Query q = se::parse(query_, [index](const std::string& f) { return index && index->isTitle(f); });
    q.exactOnly = exactOnly_;
    return q;
  }

  // Runs the query and lays out the rows, unless focus is in them. Each
  // letter looks through everything but the guide's shows; `shows` adds
  // them (after the pause; at once for one character). `keep`: what this
  // query found on this index stays, so the pause's pass looks only
  // through the shows. The shows found stay while the query and the
  // guide's listings do (a data refresh doesn't look again).
  void search(bool shows, bool keep = false) {
    Clock::time_point start = Clock::now();
    Store& s = app_.store();
    eventsVersion_ = s.events()->version;
    channelsVersion_ = s.channels()->version;
    listingsVersion_ = s.listings()->version;
    std::shared_ptr<const se::Index> index = s.searchIndex();
    bool newIndex = index != index_;
    index_ = std::move(index);
    if (showsIndex_ && showsIndex_->showTitles.get() != index_->showTitles.get()) {
      showsIndex_.reset();
      shows_.reset();
    }
    se::Query q = parsed();
    if (!shows && q.length() >= 2 && !showsAt_) showsAt_ = app_.ms() + kShowsDelayMs;
    else if (shows) showsAt_ = 0;
    se::Options o;
    o.now = time::now();
    o.zone = app_.zone();
    o.showScores = s.settings().showScores;
    o.shows = shows || q.length() < 2;
    o.scoped = !wide_;  // no effect before a lineup is set up
    if (!keep || newIndex || !find_ || !sameQuery(find_->query, q) || find_->options.scoped != o.scoped) {
      Clock::time_point t = Clock::now();
      find_ = se::find(*index_, q, o);
      double ms = msSince(t);
      ++stats_.keys;
      stats_.runSum += ms;
      stats_.runMax = std::max(stats_.runMax, ms);
    }
    se::FoundShows none;
    const se::FoundShows* found = &none;
    if (o.shows && q.length() >= 2) {
      if (!shows_ || !showsIndex_ || !sameQuery(showsQuery_, q) || showsScoped_ != o.scoped) {
        Clock::time_point t = Clock::now();
        shows_ = se::findShows(*index_, q, o);
        showsIndex_ = index_;
        showsQuery_ = q;
        showsScoped_ = o.scoped;
        ++stats_.shows;
        stats_.showsMax = std::max(stats_.showsMax, msSince(t));
      }
      found = &*shows_;
    }
    found_ = se::merge(*index_, *find_, *found);
    showsIn_ = o.shows;
    labelsDirty_ = true;
    if (!inResults()) {
      Clock::time_point t = Clock::now();
      layout();
      stats_.layoutMax = std::max(stats_.layoutMax, msSince(t));
    }
    if (msSince(start) >= 50) ++stats_.over;
  }

  // The timing log: a line every few seconds while keys are typed. Counts
  // and milliseconds only, never what was typed.
  void logStats(uint32_t ms, bool last) {
    if (!stats_.since) stats_.since = ms;
    if (!last && ms - stats_.since < kLogMs) return;
    if (stats_.keys || stats_.shows) {
      MLOG_I("search", "keys n=%d runMax=%.1fms runAvg=%.1fms showsMax=%.1fms layoutMax=%.1fms frameMax=%.1fms over50=%d",
             stats_.keys, stats_.runMax, stats_.keys ? stats_.runSum / stats_.keys : 0.0, stats_.showsMax,
             stats_.layoutMax, stats_.frameMax, stats_.over);
    }
    stats_ = Stats{};
    stats_.since = ms;
  }

  void wakeFor(uint32_t at) {
    if (!wake_ || at < wake_) wake_ = at;
  }

  // ---------------------------------------------------------------- layout

  void layout() {
    labelsDirty_ = true;
    rows_.clear();
    if (!digits_.empty()) layoutDigits();
    else if (!page_.empty()) layoutPage();
    else if (found_.query.text.empty()) layoutZero();
    else layoutResults();
    place();
    row_ = std::clamp(row_, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
  }

  // Positions: the band beside the corner, everything else from y = 330.
  void place() {
    float bandY = 88, y = kListTop;
    for (Row& r : rows_) {
      if (r.kind == RowKind::Band) {
        bool card = !r.cells.empty() && r.cells.front().top;
        r.h = card ? 200 : kRowH;
        r.y = bandY;
        bandY += r.h + 8;
        continue;
      }
      if (r.kind == RowKind::Note) {
        r.y = y + 6;
        r.h = 30;
        y += 50;
        continue;
      }
      if (!r.header.empty()) y += 44;
      r.y = y;
      r.h = r.kind == RowKind::Tiles ? kTileH : r.kind == RowKind::Chips ? kChipH : kRowH;
      y += r.h + (r.kind == RowKind::Chips ? 26 : 14);
    }
  }

  void addBand(Cell cell, const std::string& header) {
    Row r;
    r.kind = RowKind::Band;
    r.header = header;
    r.cells.push_back(std::move(cell));
    rows_.push_back(std::move(r));
  }

  // Cells two across.
  void addTiles(const std::vector<Cell>& cells, std::string header) {
    for (size_t i = 0; i < cells.size(); i += 2) {
      Row r;
      r.kind = RowKind::Tiles;
      if (i == 0) r.header = header;
      r.cells.push_back(cells[i]);
      if (i + 1 < cells.size()) r.cells.push_back(cells[i + 1]);
      rows_.push_back(std::move(r));
    }
  }

  void addList(const std::vector<Cell>& cells, std::string header) {
    for (size_t i = 0; i < cells.size(); ++i) {
      Row r;
      r.kind = RowKind::List;
      if (i == 0) r.header = header;
      r.cells.push_back(cells[i]);
      rows_.push_back(std::move(r));
    }
  }

  // A row of chips, wrapped onto more rows when they wouldn't fit across
  // (by an estimate of each chip's width at 22 px).
  void addChips(std::vector<Cell> cells, std::string header) {
    Row r;
    r.kind = RowKind::Chips;
    r.header = std::move(header);
    float used = 0;
    for (Cell& c : cells) {
      float w = 13.0f * static_cast<float>(str::utf8Length(c.title)) + 70;
      if (!r.cells.empty() && used + w > kW) {
        rows_.push_back(std::move(r));
        r = Row{};
        r.kind = RowKind::Chips;
        used = 0;
      }
      used += w;
      r.cells.push_back(std::move(c));
    }
    if (!r.cells.empty()) rows_.push_back(std::move(r));
  }

  static Cell chip(const std::string& id, const std::string& title, CellKind kind = CellKind::Chip) {
    Cell c;
    c.kind = kind;
    c.id = id;
    c.title = title;
    return c;
  }

  // `mark`: an item outside your lineup says so.
  static Cell cellOf(const se::Item& it, bool mark = true) {
    Cell c;
    switch (it.kind) {
      case se::Kind::Channel: c.kind = CellKind::Channel; break;
      case se::Kind::Team: c.kind = CellKind::Team; break;
      case se::Kind::Game: c.kind = CellKind::Game; break;
      case se::Kind::Show: c.kind = CellKind::Show; break;
      case se::Kind::Genre: c.kind = CellKind::Genre; break;
    }
    c.id = it.id;
    c.title = it.name;
    c.entryId = it.entryId;
    if (it.listing) c.listing = *it.listing;
    c.also = it.also;
    c.outside = mark && it.outside;
    return c;
  }

  // What's live now among the games: watchable first, your teams first.
  std::vector<const se::GameEntry*> liveGames() const {
    std::vector<const se::GameEntry*> out;
    for (const se::GameEntry& g : index_->games)
      if (g.state == se::GameEntry::State::Live && g.watchable) out.push_back(&g);
    std::stable_sort(out.begin(), out.end(), [](const auto* a, const auto* b) { return a->favorite && !b->favorite; });
    return out;
  }

  std::vector<const se::GameEntry*> soonGames(time::Seconds within) const {
    std::vector<const se::GameEntry*> out;
    time::Seconds now = time::now();
    for (const se::GameEntry& g : index_->games)
      if (g.state == se::GameEntry::State::Upcoming && g.watchable && g.start > now && g.start - now <= within)
        out.push_back(&g);
    std::sort(out.begin(), out.end(), [](const auto* a, const auto* b) { return a->start < b->start; });
    return out;
  }

  // Channels of My Lineup (or every channel before it's set up) on now
  // under one of the guide's filters.
  std::vector<std::string> onNow(views::Filter::Kind kind, size_t max) const {
    std::vector<std::string> out;
    auto ch = app_.store().channels();
    auto li = app_.store().listings();
    views::Filter f{kind, {}};
    time::Seconds now = time::now();
    for (int i : views::tabRows(*ch, views::Tab::Lineup, f)) {
      const GuideChannel& g = ch->all[static_cast<size_t>(i)];
      if (views::passes(g, f, li.get(), now)) out.push_back(g.id);
      if (out.size() >= max) break;
    }
    return out;
  }

  void layoutZero() {
    const sports::MySportsPrefs& prefs = app_.store().mySports();
    time::Seconds now = time::now();
    // The band: followed teams' and leagues' games, live or within the
    // hour, in your channels (at most two); else Recently watched.
    std::vector<Cell> band;
    std::set<std::string> teamLeagues;
    for (const std::string& k : prefs.teams) teamLeagues.insert(se::fold(k.substr(0, k.find('|'))));
    std::vector<const se::GameEntry*> games;
    for (const se::GameEntry& g : index_->games) {
      if (!g.watchable) continue;
      bool soon = g.state == se::GameEntry::State::Live ||
                  (g.state == se::GameEntry::State::Upcoming && g.start > now && g.start - now <= 3600);
      if (!soon) continue;
      bool league = false;
      for (const std::string& l : prefs.leagues)
        if (se::fold(l) == g.league && !teamLeagues.count(g.league)) league = true;  // a league with no teams picked
      if (g.favorite || league) games.push_back(&g);
    }
    std::stable_sort(games.begin(), games.end(), [](const auto* a, const auto* b) {
      bool la = a->state == se::GameEntry::State::Live, lb = b->state == se::GameEntry::State::Live;
      return la != lb ? la : a->start < b->start;
    });
    for (const se::GameEntry* g : games) {
      if (band.size() >= 2) break;
      Cell c;
      c.kind = CellKind::Game;
      c.id = g->id;
      band.push_back(c);
    }
    auto ch = app_.store().channels();
    std::vector<Cell> recent;
    for (int i : ch->recent) {
      if (recent.size() >= 4) break;
      Cell c;
      c.kind = CellKind::Channel;
      c.id = ch->all[static_cast<size_t>(i)].id;
      recent.push_back(c);
    }
    const std::vector<se::Recent>& searches = app_.store().recentSearches();
    bool newUser = band.empty() && recent.empty() && searches.empty();
    size_t recentFrom = 0;
    if (!band.empty()) {
      for (size_t i = 0; i < band.size(); ++i) addBand(band[i], i == 0 ? "YOUR TEAMS AND LEAGUES" : "");
    } else if (newUser) {
      if (prefs.teams.empty()) addBand(chip("follow", "Follow your teams", CellKind::Follow), "GET STARTED");
    } else {
      for (; recentFrom < 2 && recentFrom < recent.size(); ++recentFrom)
        addBand(recent[recentFrom], recentFrom == 0 ? "RECENTLY WATCHED" : "");
    }
    // Chips: one-press pages, each only when it has something.
    std::vector<Cell> chips;
    std::vector<Cell> genres;
    if (!onNow(views::Filter::Kind::NewsNow, 1).empty()) genres.push_back(chip("genre:news", "News"));
    if (!onNow(views::Filter::Kind::MoviesNow, 1).empty()) genres.push_back(chip("genre:movies", "Movies"));
    if (!onNow(views::Filter::Kind::KidsNow, 1).empty()) genres.push_back(chip("genre:kids", "Kids"));
    std::vector<Cell> sportChips;
    if (!liveGames().empty()) sportChips.push_back(chip("live", "Games on now"));
    for (const std::string& l : prefs.leagues) {
      if (sportChips.size() >= 4) break;  // the rest are under More
      bool any = std::any_of(index_->games.begin(), index_->games.end(), [&](const se::GameEntry& g) {
        return g.league == se::fold(l) && g.state != se::GameEntry::State::Ended;
      });
      if (any) sportChips.push_back(chip(l, sports::nameOf(l).value_or(l)));
    }
    // A sports fan's leagues come first; otherwise the guide's genres.
    bool fan = !prefs.leagues.empty();
    for (const Cell& c : fan ? sportChips : genres) chips.push_back(c);
    for (const Cell& c : fan ? genres : sportChips) chips.push_back(c);
    if (!soonGames(3600).empty()) chips.push_back(chip("soon", "Starting soon"));
    chips.push_back(chip("more", "More"));
    addChips(chips, "");
    if (newUser) {
      std::vector<Cell> live, soon;
      for (const se::GameEntry* g : liveGames()) {
        if (live.size() >= 3) break;
        Cell c;
        c.kind = CellKind::Game;
        c.id = g->id;
        live.push_back(c);
      }
      for (const se::GameEntry* g : soonGames(3600)) {
        if (soon.size() >= 3) break;
        Cell c;
        c.kind = CellKind::Game;
        c.id = g->id;
        soon.push_back(c);
      }
      addList(live, "GAMES ON NOW");
      addList(soon, "STARTING SOON");
      addScopeNote();
      return;
    }
    std::vector<Cell> rest(recent.begin() + static_cast<std::ptrdiff_t>(recentFrom), recent.end());
    addTiles(rest, recentFrom ? "" : "RECENTLY WATCHED");
    addRecentSearches();
    addScopeNote();
  }

  // Once a lineup is set up: where Search looks first.
  void addScopeNote() {
    if (!app_.store().channels()->lineupConfigured) return;
    Row r;
    r.kind = RowKind::Note;
    r.header = "Search looks in your lineup and Live Events first.";
    rows_.push_back(std::move(r));
  }

  // The scope's words show once a lineup is set up (before that,
  // everything is yours).
  bool scopeShown() const { return index_ && index_->lineupSet; }

  void addRecentSearches() {
    if (!app_.store().settings().recentSearches) return;
    const std::vector<se::Recent>& searches = app_.store().recentSearches();
    std::vector<Cell> cells;
    for (size_t i = 0; i < searches.size() && i < se::kRecentShown; ++i)
      cells.push_back(chip(searches[i].id, searches[i].name, CellKind::Recent));
    if (cells.empty()) return;
    cells.push_back(chip("clear", "Clear", CellKind::ClearRecents));
    addChips(cells, "RECENT SEARCHES");
  }

  // What to try: the chips and recent searches.
  void addTry() {
    std::vector<Cell> chips;
    if (!liveGames().empty()) chips.push_back(chip("live", "Games on now"));
    for (const std::string& l : app_.store().mySports().leagues)
      if (chips.size() < 4) chips.push_back(chip(l, sports::nameOf(l).value_or(l)));
    chips.push_back(chip("genre:news", "News"));
    chips.push_back(chip("genre:movies", "Movies"));
    chips.push_back(chip("genre:kids", "Kids"));
    chips.push_back(chip("more", "More"));
    addChips(chips, "TRY");
    addRecentSearches();
  }

  void layoutResults() {
    const se::Results& r = found_;
    bool topReady = showsIn_ || r.query.length() < 2;
    if (r.nothing && topReady) {
      // One plain line with what to try, then the chips and recent searches.
      addTry();
      return;
    }
    if (r.outsideOnly) {
      // Nothing in your lineup: what's outside at once, under one header
      // (it says where they are), then what to try. The shows may still be
      // yours until they're in.
      if (!topReady) return;
      std::vector<Cell> groups[4];  // channels, teams, games, shows
      if (r.top) {
        se::Kind k = r.top->kind;
        int g = k == se::Kind::Channel ? 0 : k == se::Kind::Team ? 1 : k == se::Kind::Game ? 2 : 3;
        groups[g].push_back(cellOf(*r.top, false));
      }
      for (const se::Item& it : r.channels) groups[0].push_back(cellOf(it, false));
      for (const se::Item& it : r.teams) groups[1].push_back(cellOf(it, false));
      for (const se::Item& it : r.games) groups[2].push_back(cellOf(it, false));
      for (const se::Item& it : r.shows) groups[3].push_back(cellOf(it, false));
      std::string header = "OUTSIDE YOUR LINEUP";
      for (int g = 0; g < 4; ++g) {
        if (groups[g].empty()) continue;
        if (g == 0) addTiles(groups[g], header);
        else addList(groups[g], header);
        header.clear();
      }
      addTry();
      return;
    }
    if (r.top && topReady) {
      Cell c = cellOf(*r.top);
      c.top = true;
      // With a lineup, the scope's header is over the band (drawRows()).
      addBand(c, r.showing.empty() && !scopeShown() ? "TOP RESULT" : "");
    }
    if (!r.showing.empty() && topReady)
      addChips({chip("instead", "Search for \xe2\x80\x9c" + query_ + "\xe2\x80\x9d instead", CellKind::Instead)}, "");
    std::vector<Cell> cells;
    for (const se::Item& it : r.channels) cells.push_back(cellOf(it));
    addTiles(cells, "CHANNELS");
    cells.clear();
    for (const se::Item& it : r.teams) cells.push_back(cellOf(it));
    addList(cells, "TEAMS");
    cells.clear();
    for (const se::Item& it : r.games) cells.push_back(cellOf(it));
    addList(cells, "GAMES");
    cells.clear();
    for (const se::Item& it : r.shows) cells.push_back(cellOf(it));
    addList(cells, "SHOWS");
    cells.clear();
    for (const se::Item& it : r.genres) cells.push_back(cellOf(it));
    addList(cells, "BROWSE");
    if (!scopeShown() || !hasResults()) return;
    // The last row: what else matches outside your lineup, by name (never a
    // count); widened, the way back.
    if (wide_) {
      addList({chip("narrow", "", CellKind::Scope)}, "");
    } else if (!r.outside.empty()) {
      std::vector<std::string> names;
      auto ch = app_.store().channels();
      for (const se::Item& it : r.outside) {
        std::string name = it.name;
        if (it.kind == se::Kind::Channel)
          if (const GuideChannel* g = ch->find(it.id)) name = g->name;
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
      }
      addList({chip("wide", str::join(names, " \xc2\xb7 "), CellKind::Scope)}, "");
    }
  }

  // Shows what's outside your lineup (`on`), or hides it again. Widened,
  // focus lands on the first new item outside; narrowed, on the row that
  // widens again.
  void widen(bool on) {
    auto key = [](const Cell& c) { return std::to_string(static_cast<int>(c.kind)) + ":" + c.id; };
    std::set<std::string> before;
    for (const Row& r : rows_)
      for (const Cell& c : r.cells) before.insert(key(c));
    wide_ = on;
    results_ = false;
    stopPreview();
    search(true);
    enterResults(false);
    auto land = [&] {
      for (size_t r = 0; r < rows_.size(); ++r)
        for (size_t k = 0; k < rows_[r].cells.size(); ++k) {
          const Cell& c = rows_[r].cells[k];
          if (on ? c.outside && !before.count(key(c)) : c.kind == CellKind::Scope) {
            row_ = static_cast<int>(r);
            col_ = static_cast<int>(k);
            return;
          }
        }
    };
    land();
    if (const Cell* f = focusedCell()) pace_.hold(previewId(*f));
  }

  // A chip's page: Games on now, Starting soon, a league or a sport, the
  // guide's News / Movies / Kids on now, More.
  void layoutPage() {
    const std::string& id = page_.id;
    auto gameCell = [](const se::GameEntry& g) {
      Cell c;
      c.kind = CellKind::Game;
      c.id = g.id;
      return c;
    };
    auto channelCells = [&](views::Filter::Kind kind) {
      std::vector<Cell> cells;
      for (const std::string& e : onNow(kind, 20)) {
        Cell c;
        c.kind = CellKind::Channel;
        c.id = e;
        cells.push_back(c);
      }
      return cells;
    };
    if (id == "genre:news" || id == "genre:movies" || id == "genre:kids") {
      views::Filter::Kind kind = id == "genre:news"     ? views::Filter::Kind::NewsNow
                                 : id == "genre:movies" ? views::Filter::Kind::MoviesNow
                                                        : views::Filter::Kind::KidsNow;
      addTiles(channelCells(kind), "ON NOW");
      return;
    }
    if (id == "more") {
      std::vector<Cell> chips;
      for (const se::LeagueEntry& l : index_->leagues) chips.push_back(chip(l.id, l.title));
      addChips(chips, "SPORTS AND LEAGUES");
      addChips({chip("events", "Live Events")}, "");
      return;
    }
    std::vector<const se::GameEntry*> games;
    if (id == "live") {
      games = liveGames();
    } else if (id == "soon") {
      games = soonGames(3600);
    } else {
      std::string word = id.rfind("sport:", 0) == 0 ? id.substr(6) : se::fold(id);
      for (const se::GameEntry& g : index_->games) {
        if (g.state == se::GameEntry::State::Ended) continue;
        if (g.league == word || g.sport == word || (id == "genre:sports" && g.state == se::GameEntry::State::Live))
          games.push_back(&g);
      }
    }
    // The top live item in the band, then Live now, Later today, the week.
    time::Seconds now = time::now();
    time::Civil l = app_.zone().toLocal(now);
    time::Seconds midnight = app_.zone().fromLocal(
        time::civilFromSeconds(time::secondsFromCivil({l.year, l.month, l.day, 0, 0, 0}) + 86400));
    std::vector<Cell> live, today, later;
    for (const se::GameEntry* g : games) {
      if (g->state == se::GameEntry::State::Live) live.push_back(gameCell(*g));
      else if (g->start < midnight) today.push_back(gameCell(*g));
      else later.push_back(gameCell(*g));
    }
    if (!live.empty() && id != "soon") {
      Cell top = live.front();
      top.top = true;
      addBand(top, "");
      live.erase(live.begin());
    }
    addList(live, id == "live" ? "GAMES" : "LIVE NOW");
    addList(today, id == "soon" ? "STARTING SOON" : "LATER TODAY");
    addList(later, "THIS WEEK");
    if (id == "live" || id == "genre:sports") addTiles(channelCells(views::Filter::Kind::SportsNow), "ON NOW IN MY LINEUP");
    if (id == "soon") {
      // Shows starting within the hour on My Lineup and Favorites.
      std::vector<Cell> shows;
      auto ch = app_.store().channels();
      auto li = app_.store().listings();
      std::vector<int> rows = ch->lineupConfigured ? ch->lineup : ch->browse;
      rows.insert(rows.end(), ch->favorites.begin(), ch->favorites.end());
      std::set<std::string> seen;
      for (int i : rows) {
        const GuideChannel& g = ch->all[static_cast<size_t>(i)];
        const std::vector<Listing>* v = li->forChannel(g.id);
        if (!v || !seen.insert(g.id).second) continue;
        for (const Listing& x : *v) {
          if (x.start <= now || x.start - now > 3600) continue;
          Cell c;
          c.kind = CellKind::Show;
          c.entryId = g.id;
          c.listing = x;
          c.id = g.id + "|" + std::to_string(x.start);
          shows.push_back(c);
          break;
        }
        if (shows.size() >= 6) break;
      }
      addList(shows, "SHOWS");
    }
  }

  // LG's number keys from an empty field: the channel, the numbers that
  // start with the digits, and a team those digits also start ("49").
  void layoutDigits() {
    const se::Digits d = digitsNow();
    auto ch = app_.store().channels();
    // A number outside your lineup still finds its channel, marked.
    auto outside = [&](const std::string& id) {
      const GuideChannel* g = ch->find(id);
      return ch->lineupConfigured && g && !g->inLineup && !g->favorite;
    };
    if (!d.channel.empty()) {
      Cell c;
      c.kind = CellKind::Channel;
      c.id = d.channel;
      c.outside = outside(c.id);
      addBand(c, "CHANNEL " + digits_);
    }
    std::vector<Cell> also;
    for (size_t i = 0; i < d.also.size() && i < 4; ++i) {
      Cell c;
      c.kind = CellKind::Channel;
      c.id = d.also[i];
      c.outside = outside(c.id);
      also.push_back(c);
    }
    if (d.waits && !d.team.empty()) {
      // Teams and games only: the shows aren't looked through here.
      se::Options o;
      o.now = time::now();
      o.showScores = app_.store().settings().showScores;
      o.shows = false;
      se::Results r = se::run(*index_, se::parse(digits_), o);
      std::vector<Cell> teams;
      for (const se::Item& it : r.teams) teams.push_back(cellOf(it));
      if (r.top && r.top->kind != se::Kind::Channel) teams.insert(teams.begin(), cellOf(*r.top));
      addList(teams, "OR DID YOU MEAN");
    }
    addTiles(also, "ALSO STARTING WITH " + digits_);
  }

  // ---------------------------------------------------------------- keys

  bool keyboardKey(const KeyEvent& e) {
    Key k = e.key;
    switch (k) {
      case Key::Up:
      case Key::Down:
      case Key::Left:
      case Key::Right: {
        Grid::Move m = grid_.move(k, e.glide);
        if (m.edge == Grid::Edge::Right && hasResults()) enterResults(true);
        else if (m.edge == Grid::Edge::Left) {
          nav_.leftEdge(0, 0);
          syncMenu();
        }
        return true;
      }
      case Key::Ok: {
        using A = Grid::Spot::Area;
        const Grid::Spot& at = grid_.at();
        if (at.area == A::Speak) {
          app_.openKeyboard(query_);
        } else if (at.area == A::Bottom) {
          if (at.col == static_cast<int>(search_keys::Wide::Delete)) {
            if (!query_.empty()) setQuery(dropLast(query_));
          } else if (at.col == static_cast<int>(search_keys::Wide::Mode)) {
            grid_.flip();  // the focus stays on the key: a second OK flips back
          } else {
            setQuery("");
            grid_.letters();
          }
        } else if (grid_.typed() == " ") {
          if (!query_.empty() && query_.back() != ' ') setQuery(query_ + " ");
        } else {
          // The page stays on 123 after a digit (numbers are several).
          exactOnly_ = false;
          setQuery(query_ + grid_.typed());
        }
        return true;
      }
      case Key::Back:
        if (!page_.empty()) {
          page_ = {};
          layout();
          return true;
        }
        close(true);
        return true;
      default:
        return true;
    }
  }

  static std::string dropLast(const std::string& s) {
    // One character, whole (UTF-8 from the system keyboard).
    size_t n = s.size();
    if (n == 0) return s;
    --n;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return s.substr(0, n);
  }

  bool resultsKey(const KeyEvent& e) {
    Key k = e.key;
    int nrows = static_cast<int>(rows_.size());
    auto cellsIn = [&](int r) { return static_cast<int>(rows_[static_cast<size_t>(r)].cells.size()); };
    switch (k) {
      case Key::Up:
      case Key::Down: {
        if (glide_.blocked(e.glide)) return true;
        int step = k == Key::Up ? -1 : 1;
        int r = row_ + step;
        while (r >= 0 && r < nrows && cellsIn(r) == 0) r += step;
        if (r < 0 || r >= nrows) {
          glide_.stop(e.glide);
          return true;
        }
        float x = cellX(row_, col_);
        row_ = r;
        col_ = nearest(r, x);
        glide_.moved(e.glide);
        return true;
      }
      case Key::Left:
        if (glide_.blocked(e.glide)) return true;
        if (col_ > 0) {
          --col_;
          glide_.moved(e.glide);
        } else if (!glide_.stop(e.glide)) {
          focusKeyboard();  // back to the key it left
        }
        return true;
      case Key::Right:
        if (glide_.blocked(e.glide)) return true;
        if (col_ + 1 < cellsIn(row_)) {
          ++col_;
          glide_.moved(e.glide);
        } else {
          glide_.stop(e.glide);
        }
        return true;
      case Key::Ok:
        if (const Cell* c = focusedCell()) select(*c);
        return true;
      case Key::Menu:  // ☰ is More on Fire TV
        hold();
        return true;
      case Key::Back:
        if (!page_.empty()) {
          // A chip's page: back to all of Search.
          page_ = {};
          results_ = false;
          stopPreview();
          layout();
          return true;
        }
        focusKeyboard();
        return true;
      default:
        return true;
    }
  }

  void enterResults(bool fromKeyboard) {
    if (!hasResults()) return;
    results_ = true;
    row_ = 0;
    col_ = 0;
    while (row_ < static_cast<int>(rows_.size()) && rows_[static_cast<size_t>(row_)].cells.empty()) ++row_;
    // No preview on the first item focus lands on until it moves.
    if (fromKeyboard)
      if (const Cell* c = focusedCell()) pace_.hold(previewId(*c));
    scroll_ = 0;
  }

  void focusKeyboard() {
    if (!results_) return;
    results_ = false;
    // Back to an empty field: the letters.
    if (query_.empty()) grid_.letters();
    stopPreview();
    // What changed while focus was in the results shows now.
    search(!showsAt_);
  }

  // ---------------------------------------------------------------- digits

  void digit(int d) {
    char ch = static_cast<char>('0' + d);
    // With text in the field, digits type into it ("sn2", "49ers").
    if (!query_.empty() || !page_.empty()) {
      focusKeyboard();
      setQuery(query_ + ch);
      return;
    }
    if (digits_.size() >= 4) digits_.clear();
    if (digits_.empty()) digitsMoved_ = false;
    digits_ += ch;
    digitsAt_ = app_.ms();
    focusKeyboard();
    layout();
  }

  void leaveDigits() {
    if (digits_.empty()) return;
    digits_.clear();
    layout();
  }

  // The number plays when it is complete: after a pause (asleep until
  // then), or at once when no longer number starts with it; never by itself
  // when the digits also start a team or channel name (OK plays it). True
  // when it played or gave up.
  bool digitsTick(uint32_t ms) {
    const se::Digits& d = digitsNow();
    if (d.waits) return false;
    if (ms - digitsAt_ < kDigitsMs && !digitsSettled()) {
      wakeFor(digitsAt_ + kDigitsMs);
      return false;
    }
    std::string number = digits_, channel = d.channel;
    digits_.clear();
    if (channel.empty()) {
      app_.toast("No channel " + number);
      layout();
      return true;
    }
    watchChannel(channel, true);
    return true;
  }

  // The digits' channel, numbers and team: once per digit typed.
  const se::Digits& digitsNow() { return digitsCache_.of(index_, digits_); }

  // No longer number starts with the digits (once per digit and channel list).
  bool digitsSettled() {
    uint64_t version = app_.store().channels()->version;
    if (settledFor_ != digits_ || settledVersion_ != version) {
      settledFor_ = digits_;
      settledVersion_ = version;
      settled_ = views::digitsSettled(digits_, *app_.store().channels());
    }
    return settled_;
  }

  // Keys while LG's number box shows. OK watches the channel, unless the
  // focus was moved to a grid key (or the pointer clicks one): a letter
  // carries on the name ("49…"). ▼ goes to the name the digits start.
  bool digitsKey(const KeyEvent& e) {
    const se::Digits d = digitsNow();
    switch (e.key) {
      case Key::Ok: {
        Grid::Spot spot;
        bool pointed = pointer_ && kb::spotFor(pointer_, kHitKeyboard, grid_.page(), spot);
        if (pointed) grid_.set(spot);
        if (pointed || digitsMoved_) {
          std::string typed = grid_.typed();
          if (!typed.empty()) {
            std::string q = digits_ + typed;
            digits_.clear();
            setQuery(q);
          }
          return true;
        }
        if (!d.channel.empty()) {
          digits_.clear();
          watchChannel(d.channel, true);
        }
        return true;
      }
      case Key::Back:
        leaveDigits();
        return true;
      case Key::Down:
        if (d.waits) {
          std::string typed = digits_;
          digits_.clear();
          setQuery(typed);
          enterResults(true);
          return true;
        }
        digitsMoved_ = true;
        return false;
      case Key::Up:
      case Key::Left:
      case Key::Right:
        digitsMoved_ = true;
        return false;
      default:
        return false;
    }
  }

  // ---------------------------------------------------------------- select

  const Cell* focusedCell() const {
    if (row_ < 0 || row_ >= static_cast<int>(rows_.size())) return nullptr;
    const Row& r = rows_[static_cast<size_t>(row_)];
    if (col_ < 0 || col_ >= static_cast<int>(r.cells.size())) return nullptr;
    return &r.cells[static_cast<size_t>(col_)];
  }

  const se::TeamEntry* team(const std::string& key) const {
    auto it = index_->teamByKey.find(key);
    return it == index_->teamByKey.end() ? nullptr : &index_->teams[it->second];
  }

  // The game a team row stands for: live, else next.
  std::string teamGame(const se::TeamEntry& t) const { return !t.liveGame.empty() ? t.liveGame : t.nextGame; }

  void remember(const Cell& c, const std::string& name) {
    // What was picked, by name: never the typed text, and only from a search.
    if (query_.empty()) return;
    app_.store().rememberSearch({name, recentId(c)});
  }

  void select(const Cell& c) {
    Store& s = app_.store();
    labelsDirty_ = true;  // a reminder set shows on the row
    switch (c.kind) {
      case CellKind::Channel:
        if (const GuideChannel* g = s.channels()->find(c.id)) remember(c, g->name);
        watchChannel(c.id, false);
        return;
      case CellKind::Show: {
        remember(c, c.listing.title);
        time::Seconds now = time::now();
        if (c.listing.start <= now + 60) {
          watchChannel(c.entryId, false);
          return;
        }
        // Later: OK sets the reminder; a second OK does nothing (hold OK cancels).
        if (!s.hasShowReminder(c.entryId, c.listing.start)) {
          s.toggleShowReminder(c.entryId, c.listing);
          app_.toast("We'll show a card when it starts");
        }
        return;
      }
      case CellKind::Game: {
        const EventTile* t = s.eventTile(c.id);
        if (!t) return;
        remember(c, matchup(*t));
        selectGame(*t);
        return;
      }
      case CellKind::Team: {
        const se::TeamEntry* t = team(c.id);
        if (!t) return;
        remember(c, t->name);
        const EventTile* g = s.eventTile(teamGame(*t));
        if (!g) {
          hold();  // no game: Follow and the rest
          return;
        }
        selectGame(*g);
        return;
      }
      case CellKind::Genre:
        remember(c, c.title);
        openPage(c.id, c.title);
        return;
      case CellKind::Chip:
        if (c.id == "events") {
          close(false);
          if (go_) go_(rail::Item::Events);
          return;
        }
        openPage(c.id, c.title);
        return;
      case CellKind::Recent: runRecent(c); return;
      case CellKind::ClearRecents: askClear(); return;
      case CellKind::Instead:
        exactOnly_ = true;
        search(true);
        layout();
        enterResults(false);
        return;
      case CellKind::Follow: app_.nav().push(makeMySports(app_)); return;
      case CellKind::Scope: widen(c.id == "wide"); return;
    }
  }

  void selectGame(const EventTile& t) {
    Store& s = app_.store();
    if (t.watchable && liveTile(t)) {
      watchGame(t.id);
    } else if (!t.watchable || t.finished) {
      // Not in your channels (or streaming only): where it is, no reminder.
      app_.nav().push(makeWhereToWatch(app_, t.id));
    } else if (!s.hasReminder(t.id)) {
      s.toggleReminder(t.id);
      app_.toast("We'll show a card when it starts");
    }
  }

  void watchChannel(const std::string& entryId, bool fromDigits) {
    if (fromDigits) {
      // A number picked by digits is saved as the channel's name.
      if (const GuideChannel* g = app_.store().channels()->find(entryId))
        app_.store().rememberSearch({g->name, "c:" + entryId});
    }
    Playback& p = app_.playback();
    // Already in the corner: the same stream goes full screen, with sound.
    if (!(p.active() && p.entry() == entryId && !p.failed()) && !p.tune(entryId)) {
      app_.toast("This channel has no working feed right now");
      layout();
      return;
    }
    full();
  }

  void watchGame(const std::string& eventId) {
    Playback& p = app_.playback();
    if (!(p.active() && p.entry() == eventId && !p.failed()) &&
        !p.tune(eventId, app_.store().eventFlipList(eventId))) {
      app_.toast("This game has no working feed right now");
      return;
    }
    full();
  }

  void full() {
    app_.playback().setFull();
    watchedAt_ = app_.ms();
    app_.nav().push(makePlayer(app_));
  }

  void openPage(const std::string& id, const std::string& title) {
    stopPreview();
    page_ = {id, title};
    results_ = false;
    layout();
    enterResults(false);
  }

  void runRecent(const Cell& c) {
    // It runs again and lands on what was picked.
    std::string id = c.id;
    query_ = c.title;
    exactOnly_ = false;
    page_ = {};
    results_ = false;
    search(true);
    enterResults(false);
    for (size_t r = 0; r < rows_.size(); ++r)
      for (size_t k = 0; k < rows_[r].cells.size(); ++k)
        if (recentId(rows_[r].cells[k]) == id) {
          row_ = static_cast<int>(r);
          col_ = static_cast<int>(k);
        }
    if (const Cell* f = focusedCell()) pace_.hold(previewId(*f));
  }

  void askClear() {
    App& app = app_;
    SearchScreen* self = this;
    app_.nav().push(std::make_unique<PanelScreen>(app_, PanelScreen::Side::Center, 640.0f, [&app, self](ui::ListPanel& p) {
      p.title = "Clear recent searches?";
      ui::PanelItem clear{ui::PanelItem::Kind::Action, "Clear"};
      clear.select = [&app, self] {
        app.store().clearSearches();
        app.nav().pop();
        self->afterPanel();
      };
      ui::PanelItem cancel{ui::PanelItem::Kind::Action, "Cancel"};
      cancel.select = [&app] { app.nav().pop(); };
      p.items = {clear, cancel};
    }));
  }

  // A panel changed what the results show (a search removed, cleared).
  void afterPanel() {
    results_ = false;
    search(true);
    if (hasResults()) enterResults(false);
  }

  // ---------------------------------------------------------------- hold OK

  void hold() {
    const Cell* c = focusedCell();
    if (!c) return;
    App& app = app_;
    SearchScreen* self = this;
    Cell cell = *c;
    Store& s = app_.store();
    auto panel = [&](const std::string& title, std::function<void(ui::ListPanel&)> rows) {
      app_.nav().push(std::make_unique<PanelScreen>(app_, PanelScreen::Side::Trailing, 640.0f,
                                                    [title, rows](ui::ListPanel& p) {
                                                      p.title = title;
                                                      rows(p);
                                                    }));
    };
    auto followRow = [&app](const std::string& key, const std::string& name) {
      bool on = app.store().followsTeam(key);
      ui::PanelItem it{ui::PanelItem::Kind::Action, (on ? "Unfollow " : "Follow ") + name};
      it.detail = on ? "Takes them out of My Sports" : "Adds them to My Sports";
      it.select = [&app, key] { app.store().toggleFollowTeam(key); };
      return it;
    };
    auto whereRow = [&app](const std::string& eventId) {
      ui::PanelItem it{ui::PanelItem::Kind::Chevron, "Where to Watch"};
      it.select = [&app, eventId] {
        app.nav().pop();
        app.nav().push(makeWhereToWatch(app, eventId));
      };
      return it;
    };
    auto remindRow = [&app](const std::string& eventId) {
      bool set = app.store().hasReminder(eventId);
      ui::PanelItem it{ui::PanelItem::Kind::Action, set ? "Cancel reminder" : "Remind me"};
      it.select = [&app, eventId] {
        bool on = app.store().toggleReminder(eventId);
        app.toast(on ? "We'll show a card when it starts" : "Reminder removed");
      };
      return it;
    };
    switch (cell.kind) {
      case CellKind::Channel: {
        const GuideChannel* g = s.channels()->find(cell.id);
        if (!g) return;
        channelPanel(*g);
        return;
      }
      case CellKind::Team: {
        const se::TeamEntry* t = team(cell.id);
        if (!t) return;
        std::string key = t->key, name = t->name, game = teamGame(*t);
        bool upcoming = !game.empty() && t->liveGame.empty();
        const EventTile* tile = s.eventTile(game);
        bool remind = upcoming && tile && tile->watchable;
        panel(name, [followRow, whereRow, remindRow, key, name, game, remind](ui::ListPanel& p) {
          p.items.push_back(followRow(key, name));
          if (!game.empty()) p.items.push_back(whereRow(game));
          if (remind) p.items.push_back(remindRow(game));
        });
        return;
      }
      case CellKind::Game: {
        const EventTile* t = s.eventTile(cell.id);
        if (!t) return;
        std::string id = t->id, title = matchup(*t);
        bool remind = t->upcoming && t->watchable && !t->finished;
        std::vector<std::pair<std::string, std::string>> sides;  // key, name
        auto x = index_->gameById.find(id);
        if (x != index_->gameById.end())
          for (const std::string& k : index_->games[x->second].teams)
            if (const se::TeamEntry* tm = team(k)) sides.push_back({k, tm->name});
        panel(title, [followRow, whereRow, remindRow, id, remind, sides](ui::ListPanel& p) {
          // Follow either team (★ on the one you follow).
          for (const auto& [k, n] : sides) p.items.push_back(followRow(k, n));
          p.items.push_back(whereRow(id));
          if (remind) p.items.push_back(remindRow(id));
        });
        return;
      }
      case CellKind::Show: {
        std::string entry = cell.entryId;
        Listing l = cell.listing;
        bool later = l.start > time::now() + 60;
        panel(l.title, [&app, self, entry, l, later](ui::ListPanel& p) {
          ui::PanelItem more{ui::PanelItem::Kind::Chevron, "More about this show"};
          more.select = [&app, entry, l] {
            app.nav().pop();
            app.nav().push(makeProgramDetail(app, entry, l));
          };
          p.items.push_back(more);
          if (later) {
            bool set = app.store().hasShowReminder(entry, l.start);
            ui::PanelItem remind{ui::PanelItem::Kind::Action, set ? "Cancel reminder" : "Remind me"};
            remind.select = [&app, entry, l] {
              bool on = app.store().toggleShowReminder(entry, l);
              app.toast(on ? "We'll show a card when it starts" : "Reminder removed");
            };
            p.items.push_back(remind);
          }
          ui::PanelItem channel{ui::PanelItem::Kind::Chevron, "Channel"};
          channel.select = [&app, self, entry] {
            app.nav().pop();
            if (const GuideChannel* g = app.store().channels()->find(entry)) self->channelPanel(*g);
          };
          p.items.push_back(channel);
        });
        return;
      }
      case CellKind::Recent: {
        std::string id = cell.id;
        panel(cell.title, [&app, self, id](ui::ListPanel& p) {
          ui::PanelItem remove{ui::PanelItem::Kind::Action, "Remove this search"};
          remove.select = [&app, self, id] {
            app.store().forgetSearch(id);
            app.nav().pop();
            self->afterPanel();
          };
          p.items.push_back(remove);
        });
        return;
      }
      default:
        return;  // chips and genres have no hold menu
    }
  }

  // The guide's channel panel: Watch, the other coast, My Lineup, Choose
  // feed, Favorites.
  void channelPanel(const GuideChannel& g) {
    App& app = app_;
    SearchScreen* self = this;
    std::string id = g.id, partner = g.partner;
    bool west = g.west;
    app_.nav().push(std::make_unique<PanelScreen>(app_, PanelScreen::Side::Trailing, 640.0f,
                                                  [&app, self, id, partner, west](ui::ListPanel& p) {
      const GuideChannel* c = app.store().channels()->find(id);
      if (!c) return;
      p.title = c->name;
      ui::PanelItem watch{ui::PanelItem::Kind::Action, "Watch"};
      watch.select = [&app, self, id] {
        app.nav().pop();
        self->watchChannel(id, false);
      };
      p.items.push_back(watch);
      if (!partner.empty()) {
        ui::PanelItem other{ui::PanelItem::Kind::Action, west ? "Watch the East coast" : "Watch the West coast"};
        other.select = [&app, self, partner] {
          app.nav().pop();
          self->watchChannel(partner, false);
        };
        p.items.push_back(other);
      }
      ui::PanelItem lineup{ui::PanelItem::Kind::Toggle, "My Lineup"};
      lineup.on = c->inLineup;
      lineup.select = [&app, id] { app.store().toggleLineup(id); };
      p.items.push_back(lineup);
      ui::PanelItem fav{ui::PanelItem::Kind::Toggle, "Favorites"};
      fav.on = c->favorite;
      fav.select = [&app, id] { app.store().toggleFavorite(id); };
      p.items.push_back(fav);
      ui::PanelItem feed{ui::PanelItem::Kind::Chevron, "Choose Feed"};
      feed.select = [&app, id] {
        app.nav().pop();
        app.nav().push(makeSources(app, id));
      };
      p.items.push_back(feed);
    }));
  }

  // ---------------------------------------------------------------- rail

  void syncMenu() {
    menu_.go(nav_.mode() == rail::Mode::Menu ? 1.0f : 0.0f, app_.ms());
    app_.invalidate();
  }

  void apply(const rail::Step& s) {
    using Do = rail::Step::Do;
    switch (s.what) {
      case Do::Open:
        if (s.item != rail::Item::Search) {
          close(false);
          if (go_) go_(s.item);
        }
        break;
      case Do::Home:
        close(false);
        if (go_) go_(rail::Item::Lineup);
        break;
      default: break;
    }
    syncMenu();
  }

  // A pointer click: the rail's targets, the corner, the tab. True when
  // used; elsewhere the hover moved the focus and OK acts on it.
  bool click() {
    int id = pointer_;
    const std::vector<rail::Item>& items = nav_.items();
    auto item = [&](int base) {
      return items[static_cast<size_t>(std::clamp(id - base, 0, static_cast<int>(items.size()) - 1))];
    };
    if (nav_.mode() == rail::Mode::Menu) {
      if (id >= left_rail::kHitMenu && id < left_rail::kHitMenu + 100 && !items.empty()) apply(nav_.pick(item(left_rail::kHitMenu)));
      else if (id == left_rail::kHitOutside) nav_.close();
      syncMenu();
      return true;
    }
    if (id >= left_rail::kHitStrip && id < left_rail::kHitStrip + 100 && !items.empty()) {
      apply(nav_.pick(item(left_rail::kHitStrip)));
      return true;
    }
    if (id == kHitCorner) {
      // A click on a playing corner goes full screen.
      Playback& p = app_.playback();
      if (p.active() && !p.full() && p.ready()) full();
      return true;
    }
    if (id == kHitPill) {
      page_ = {};
      results_ = false;
      layout();
      return true;
    }
    if (id == kHitKeyboard + kb::kHitTab) {
      enterResults(true);
      return true;
    }
    if (id == kHitKeyboard + kb::kHitField) return true;
    return id == left_rail::kHitNothing;
  }

  // `keep`: Back from the keyboard; reopened within five minutes, the
  // query comes back.
  void close(bool keep) {
    stopPreview();
    gLast = {query_, app_.ms(), keep && !query_.empty()};
    app_.nav().pop();
  }

  // ---------------------------------------------------------------- preview

  // What the corner may play for a cell: a channel, a show on now, a live
  // game in your channels (a team's too); "" for anything else. Live
  // sports don't auto-preview with Show scores off (the picture has a score).
  std::string previewId(const Cell& c) const {
    Store& s = app_.store();
    if (!s.settings().previewChannels) return {};
    bool scores = s.settings().showScores;
    switch (c.kind) {
      case CellKind::Channel: return c.id;
      case CellKind::Show: return c.listing.isOn(time::now()) ? c.entryId : std::string();
      case CellKind::Game: {
        const EventTile* t = s.eventTile(c.id);
        return t && t->watchable && liveTile(*t) && scores ? c.id : std::string();
      }
      case CellKind::Team: {
        const se::TeamEntry* t = team(c.id);
        if (!t || t->liveGame.empty() || !scores) return {};
        const EventTile* g = s.eventTile(t->liveGame);
        return g && g->watchable ? t->liveGame : std::string();
      }
      default: return {};
    }
  }

  // Only in the results: after a 2 s rest on something on now, muted.
  // Moving on drops the corner to the new item's card at once.
  void previewTick(uint32_t ms) {
    Playback& p = app_.playback();
    if (app_.nav().top() != this) return;
    const Cell* c = inResults() ? focusedCell() : nullptr;
    std::string id = c ? previewId(*c) : std::string();
    if (p.active() && !p.full() && p.entry() != id) p.stop();
    switch (pace_.step(id, !id.empty() && p.entry() == id, ms)) {
      case preview::Pace::Step::Idle: return;
      case preview::Pace::Step::Wait:  // asleep until the rest is over
        wakeFor(pace_.due());
        return;
      case preview::Pace::Step::Tune: break;
    }
    p.setCorner(top_bar::kPreview, true);
    bool event = app_.store().isEvent(id);
    if (!(event ? p.tune(id, app_.store().eventFlipList(id)) : p.tune(id))) p.stop();
  }

  void stopPreview() {
    Playback& p = app_.playback();
    if (p.active() && !p.full()) p.stop();
  }

  // ---------------------------------------------------------------- drawing

  float cellX(int row, int col) const {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return kX;
    const Row& r = rows_[static_cast<size_t>(row)];
    if (r.kind == RowKind::Tiles) return kX + static_cast<float>(col) * (kTileW + 20);
    if (r.kind == RowKind::Chips) return kX + static_cast<float>(col) * 160;  // roughly
    return kX;
  }

  int nearest(int row, float x) const {
    const Row& r = rows_[static_cast<size_t>(row)];
    int best = 0;
    float d = 1e9f;
    for (size_t i = 0; i < r.cells.size(); ++i) {
      float dx = std::fabs(cellX(row, static_cast<int>(i)) - x);
      if (dx < d) {
        d = dx;
        best = static_cast<int>(i);
      }
    }
    return best;
  }

  bool focusedAt(int r, int col) const { return inResults() && nav_.mode() == rail::Mode::Content && r == row_ && col == col_; }

  void drawRows(ui::Ctx& c) {
    // The pill of a chip's page, over the band.
    if (!page_.empty()) {
      float w = ui::chipWidth(c, pillText_, 20);
      c.hit({kX, 34, w, 30}, kHitPill);
      ui::chip(c, pillText_, kX, 34, 20, theme::hex(theme::kNowLine, 0.3f), theme::white());
    }
    if (!found_.showing.empty() && page_.empty() && digits_.empty() && !found_.query.text.empty()) {
      ui::label(c, showingText_, kX, 40, Weight::Bold, 26, theme::white(), kBandW);
    }
    bool typed = page_.empty() && digits_.empty() && !found_.query.text.empty();
    bool topReady = showsIn_ || found_.query.length() < 2;
    if (found_.nothing && showsIn_ && page_.empty() && digits_.empty()) {
      ui::label(c, nothingText_, kX, 96, Weight::Bold, 34, theme::white(), kBandW);
      ui::paragraph(c, "Not in your channels or the guide right now. Try a channel name or number, a show or a team.",
                    kX, 150, kBandW, Weight::Regular, 24, theme::white(0.7f), 3);
    }
    if (found_.outsideOnly && topReady && typed) {
      ui::label(c, nothingInText_, kX, 96, Weight::Bold, 34, theme::white(), kBandW);
      ui::paragraph(c, "Here\xe2\x80\x99s what\xe2\x80\x99s outside it.", kX, 150, kBandW, Weight::Regular, 24,
                    theme::white(0.7f), 3);
    }
    bool waiting = !showsIn_ && found_.query.length() >= 2 && page_.empty() && digits_.empty();
    // Once a lineup is set up, the header says where the results are from.
    if (typed && scopeShown() && found_.showing.empty() && !(found_.nothing && topReady) && (waiting || hasResults()))
      ui::label(c, wide_ ? "EVERYTHING" : "YOUR LINEUP AND LIVE EVENTS", kX, 52, Weight::Bold, 20, theme::white(0.75f));
    if (waiting) {
      // The top result waits for the shows (it never swaps after it's read).
      if (!scopeShown()) ui::label(c, "TOP RESULT", kX, 52, Weight::Bold, 20, theme::white(0.75f));
      c.g.fill({kX, 88, kBandW, 200}, ui::skeleton(c), 18);
    }
    // Scroll so the focused row stays in view (rows below the band).
    float focusTop = 0, focusBottom = 0;
    if (inResults() && row_ < static_cast<int>(rows_.size())) {
      const Row& f = rows_[static_cast<size_t>(row_)];
      if (f.kind != RowKind::Band) {
        focusTop = f.y - (f.header.empty() ? 0 : 44);
        focusBottom = f.y + f.h;
        if (focusTop - scroll_ < kListTop) scroll_ = std::max(0.0f, focusTop - kListTop);
        if (focusBottom - scroll_ > kListBottom) scroll_ = focusBottom - kListBottom;
      } else {
        scroll_ = 0;
      }
    } else {
      scroll_ = 0;
    }
    for (size_t i = 0; i < rows_.size(); ++i) {
      const Row& r = rows_[i];
      if (r.kind == RowKind::Band) {
        if (!r.header.empty()) ui::label(c, r.header, kX, r.y - 36, Weight::Bold, 20, theme::white(0.75f));
        for (size_t k = 0; k < r.cells.size(); ++k) drawCell(c, r, r.cells[k], static_cast<int>(i), static_cast<int>(k), r.y);
      }
    }
    c.g.pushClip({left_rail::kStripW + 1, kListTop - 50, gfx::kLayoutWidth, kListBottom - kListTop + 60});
    for (size_t i = 0; i < rows_.size(); ++i) {
      const Row& r = rows_[i];
      if (r.kind == RowKind::Band) continue;
      float y = r.y - scroll_;
      if (y + r.h < kListTop - 50 || y - 44 > kListBottom) continue;
      if (r.kind == RowKind::Note) {
        ui::label(c, r.header, kX, y, Weight::Regular, 22, theme::white(0.6f), kW);
        continue;
      }
      if (!r.header.empty()) ui::label(c, r.header, kX, y - 38, Weight::Bold, 20, theme::white(0.75f));
      if (r.kind == RowKind::Chips) {
        float x = kX;
        for (size_t k = 0; k < r.cells.size(); ++k) {
          const Cell& cell = r.cells[k];
          bool f = focusedAt(static_cast<int>(i), static_cast<int>(k));
          float w = ui::chipWidth(c, cell.title, 22) + 20;
          Rect box{x, y, w, kChipH};
          c.hit(box, kHitCells + static_cast<int>(i) * 100 + static_cast<int>(k));
          if (cell.kind == CellKind::ClearRecents) {
            // Clear: outlined at 60% (3:1 against the background).
            if (f) c.g.fill(box, theme::white(), kChipH * 0.5f);
            else c.g.stroke(box, theme::white(0.6f), kChipH * 0.5f, 2);
          } else {
            c.g.fill(box, f ? theme::white() : theme::white(0.12f), kChipH * 0.5f);
          }
          ui::labelIn(c, cell.title, box, Weight::Bold, 22, f ? theme::black() : theme::white(), Align::Center);
          x += w + 14;
        }
        continue;
      }
      for (size_t k = 0; k < r.cells.size(); ++k) drawCell(c, r, r.cells[k], static_cast<int>(i), static_cast<int>(k), y);
    }
    c.g.popClip();
  }

  // What OK does on a cell, written on it while focused.
  std::string okText(const Cell& cell) const {
    Store& s = app_.store();
    switch (cell.kind) {
      case CellKind::Channel: return "OK: Watch";
      case CellKind::Show:
        if (cell.listing.isOn(time::now())) return "OK: Watch";
        return s.hasShowReminder(cell.entryId, cell.listing.start) ? "Hold OK: Cancel reminder" : "OK: Remind me";
      case CellKind::Game:
      case CellKind::Team: {
        std::string id = cell.id;
        if (cell.kind == CellKind::Team) {
          const se::TeamEntry* t = team(cell.id);
          id = t ? teamGame(*t) : std::string();
          if (id.empty()) return "OK: More";
        }
        const EventTile* t = s.eventTile(id);
        if (!t) return {};
        if (t->watchable && liveTile(*t)) return "OK: Watch";
        if (!t->watchable || t->finished) return "OK: Where to Watch";
        return s.hasReminder(id) ? "Hold OK: Cancel reminder" : "OK: Remind me";
      }
      case CellKind::Genre: return "OK: Open";
      case CellKind::Scope: return cell.id == "wide" ? "OK: Show" : "";
      default: return {};
    }
  }

  void drawCell(ui::Ctx& c, const Row& row, const Cell& cell, int r, int k, float y) {
    bool f = focusedAt(r, k);
    float w = row.kind == RowKind::Tiles ? kTileW : row.kind == RowKind::Band ? kBandW : kW;
    float x = kX + (row.kind == RowKind::Tiles ? static_cast<float>(k) * (kTileW + 20) : 0);
    Rect box{x, y, w, row.h};
    c.hit(box, kHitCells + r * 100 + k);
    if (cell.kind == CellKind::Scope) {
      scopeRow(c, cell, box, f);
      return;
    }
    c.g.fill(box, f ? theme::white() : theme::white(0.07f), 18);
    Color ink = f ? theme::black() : theme::white();
    Color sub = f ? theme::black(0.65f) : theme::white(0.7f);
    const Cell::Label& l = cell.label;
    bool ok = f && !l.ok.empty();
    float okW = ok ? ui::textWidth(c, l.ok, Weight::Bold, 20) + 24 : 0;
    if (cell.top) {
      drawTopCard(c, cell, box, f, ok);
      return;
    }
    float tx = box.x + 22, right = box.x + box.w - 22 - okW;
    if (ok) ui::labelIn(c, l.ok, {right + 12, box.y, okW - 12, box.h}, Weight::Bold, 20, ink, Align::Right);
    if (l.missing) return;
    switch (cell.kind) {
      case CellKind::Channel:
        ui::logo(c, l.logoUrl, l.logoName, {tx, box.y + (box.h - 54) * 0.5f, 96, 54}, f);
        tx += 116;
        break;
      case CellKind::Show:
        if (l.logo) ui::logo(c, l.logoUrl, l.logoName, {tx, box.y + (box.h - 48) * 0.5f, 84, 48}, f);
        tx += 104;
        break;
      case CellKind::Game: tx += badges(c, l, tx, box.y + (box.h - 52) * 0.5f, 52, f); break;
      case CellKind::Team: tx += teamBadge(c, l, tx, box.y + (box.h - 52) * 0.5f, f) + 18; break;
      default: break;
    }
    float ty = box.y + box.h * 0.5f - 36;
    float lx = tx;
    if (l.live) lx += ui::chip(c, "LIVE", lx, ty + 4, 17, theme::hex(theme::kLive), theme::white()) + 10;
    if (l.west) lx += ui::coastChip(c, true, lx, ty + 4, 17) + 10;
    if (!l.tag.empty()) lx += ui::chip(c, l.tag, lx, ty + 4, 17, f ? theme::black(0.15f) : theme::white(0.18f), ink) + 10;
    ui::label(c, l.title, lx, ty, Weight::Bold, 26, ink, right - lx);
    ui::label(c, l.line, tx, ty + 38, Weight::Medium, 22, sub, right - tx);
  }

  // The scope's row: outlined, full width, a round icon, the words and a ›
  // ("OK: Show" while focused).
  void scopeRow(ui::Ctx& c, const Cell& cell, Rect box, bool f) {
    if (f) c.g.fill(box, theme::white(), 18);
    else c.g.stroke(box, theme::white(0.4f), 18, 2);
    const Cell::Label& l = cell.label;
    bool wide = cell.id == "wide";
    Color ink = f ? theme::black() : theme::white();
    Color sub = f ? theme::black(0.66f) : theme::white(0.75f);
    Rect lead{box.x + 22, box.y + (box.h - 52) * 0.5f, 52, 52};
    c.g.fill(lead, f ? theme::black(0.08f) : theme::white(0.12f), 26);
    if (unsigned t = c.icons ? c.icons->get(wide ? "ui-all" : "ui-lineup") : 0)
      c.g.textureAlpha(t, {lead.x + 11, lead.y + 11, 30, 30}, ink);
    std::string right = f ? l.ok : wide ? "\xe2\x80\xba" : "";
    int px = f ? 20 : 34;
    float rw = right.empty() ? 0 : ui::textWidth(c, right, Weight::Bold, px) + 24;
    float rx = box.x + box.w - 22 - rw;
    if (!right.empty())
      ui::labelIn(c, right, {rx, box.y, rw, box.h}, Weight::Bold, px, f ? ink : theme::white(0.85f), Align::Right);
    float tx = lead.x + lead.w + 18, ty = box.y + box.h * 0.5f - 36;
    ui::label(c, l.title, tx, ty, Weight::Bold, 26, ink, rx - tx);
    ui::label(c, l.line, tx, ty + 38, Weight::Medium, 22, sub, rx - tx);
  }

  // The top result: a type tag, a big title and what's on, beside the corner.
  void drawTopCard(ui::Ctx& c, const Cell& cell, Rect box, bool f, bool ok) {
    const Cell::Label& l = cell.label;
    if (l.missing) return;
    Color ink = f ? theme::black() : theme::white();
    Color sub = f ? theme::black(0.65f) : theme::white(0.75f);
    float tx = box.x + 22, ty = box.y + 64;
    switch (cell.kind) {
      case CellKind::Channel:
        ui::logo(c, l.logoUrl, l.logoName, {tx, ty, 112, 64}, f);
        tx += 132;
        break;
      case CellKind::Game: tx += badges(c, l, tx, ty, 64, f); break;
      case CellKind::Team: tx += teamBadge(c, l, tx, ty, f) + 18; break;
      case CellKind::Show:
        if (l.logo) ui::logo(c, l.logoUrl, l.logoName, {tx, ty, 112, 64}, f);
        tx += 132;
        break;
      default: break;
    }
    ui::label(c, l.cardTag, box.x + 22, box.y + 18, Weight::Bold, 18, sub, box.w - 44);
    // A long title wraps to two lines here only.
    ui::paragraph(c, l.cardTitle, tx, ty, box.x + box.w - 22 - tx, Weight::Bold, 32, ink, 2, 1.15f);
    float lx = box.x + 22, ly = box.y + box.h - 48;
    if (l.live) lx += ui::chip(c, "LIVE", lx, ly + 2, 18, theme::hex(theme::kLive), theme::white()) + 10;
    ui::label(c, ok ? l.cardLineOk : l.line, lx, ly, Weight::Medium, 22, sub, box.x + box.w - 22 - lx);
  }

  // Two side badges (or the sport's mark). Returns the width used.
  float badges(ui::Ctx& c, const Cell::Label& l, float x, float y, float size, bool f) {
    if (l.sides.size() == 2) {
      events_ui::badge(c, l.sides[0], x, y, size, f);
      events_ui::badge(c, l.sides[1], x + size + 8, y, size, f);
      return 2 * size + 28;
    }
    ui::logo(c, l.logoUrl, l.logoName, {x, y, size * 1.6f, size}, f);
    return size * 1.6f + 18;
  }

  float teamBadge(ui::Ctx& c, const Cell::Label& l, float x, float y, bool f) {
    if (!l.sides.empty()) events_ui::badge(c, l.sides.front(), x, y, 52, f);
    return 52;
  }

  // The badge the board has for a team, when it has a game; else initials.
  EventSide teamSide(const se::TeamEntry& t) const {
    for (const std::string& gid : {t.liveGame, t.nextGame}) {
      const EventTile* g = app_.store().eventTile(gid);
      if (!g || g->sides.size() != 2) continue;
      for (const EventSide& s : g->sides) {
        if (se::fold(s.name).empty() || t.names.empty()) continue;
        if (t.names.front().find(se::fold(s.name)) != std::string::npos) return s;
      }
    }
    EventSide side;
    side.abbr = se::initials(se::words(se::fold(t.name)));
    for (char& ch : side.abbr) ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    return side;
  }

  // The data a cell's text is made from: made again when it moves on.
  std::array<uint64_t, 3> versions() const {
    Store& s = app_.store();
    return {s.events()->version, s.channels()->version, s.listings()->version};
  }

  // Every row's text, and the lines over the band (drawn as they are).
  void labelAll(const ui::Ctx& c) {
    labelsDirty_ = false;
    labelMinute_ = c.now / 60;
    labeled_ = versions();
    ++labelGeneration_;
    for (Row& r : rows_)
      for (Cell& cell : r.cells) label(c, cell);
    pillText_ = page_.empty() ? std::string() : page_.title + "  \xc3\x97";
    showingText_ = found_.showing.empty() ? std::string() : "Showing " + found_.showing;
    nothingText_ = "Nothing for \xe2\x80\x9c" + query_ + "\xe2\x80\x9d";
    nothingInText_ = nothingText_ + " in your lineup";
  }

  void label(const ui::Ctx& c, Cell& cell) {
    Store& s = app_.store();
    Cell::Label& l = cell.label;
    l = Cell::Label{};
    l.ok = okText(cell);
    // The board's tile for the corner's card (a game, a team's game).
    const EventTile* corner = nullptr;
    switch (cell.kind) {
      case CellKind::Channel: {
        const GuideChannel* g = s.channels()->find(cell.id);
        if (!g) {
          l.missing = true;
          break;
        }
        l.logo = true;
        l.logoUrl = l.cornerLogo = g->logoUrl;
        l.logoName = l.cornerName = l.cornerTitle = g->name;
        l.title = l.cardTitle = g->name;
        l.line = channelLine(c, *g, l.live);
        if (cell.outside) {
          // "2401 · Outside your lineup".
          l.line = (g->number ? std::to_string(*g->number) + " \xc2\xb7 " : std::string()) + kOutside;
          l.live = false;
        }
        l.west = g->west;
        l.cardTag = "CHANNEL" + std::string(g->number ? " \xc2\xb7 " + std::to_string(*g->number) : "") +
                    (g->inLineup ? " \xc2\xb7 MY LINEUP" : "");
        views::NowNext nn = views::nowAndNext(*s.listings(), g->id, c.now);
        if (nn.now) l.cornerLine = nn.now->title;
        if (cell.outside) l.cornerLine = l.line;
        break;
      }
      case CellKind::Show: {
        const GuideChannel* g = s.channels()->find(cell.entryId);
        if (g) {
          l.logo = true;
          l.logoUrl = l.cornerLogo = g->logoUrl;
          l.logoName = l.cornerName = g->name;
        }
        const Listing& x = cell.listing;
        l.title = l.cardTitle = l.cornerTitle = x.title;
        l.live = x.isOn(c.now);
        l.tag = se::showTag(se::fold(x.title));
        l.line = showLine(c, cell, g);
        if (cell.outside) l.line += std::string(" \xc2\xb7 ") + kOutside;
        l.cardTag = "SHOW" + (l.tag.empty() ? std::string() : " \xc2\xb7 " + l.tag);
        l.cornerLine = x.isOn(c.now) ? "Now until " + ui::clock(c, x.end) : whenText(c, x.start);
        if (!x.isOn(c.now))
          l.cornerLine += s.hasShowReminder(cell.entryId, x.start) ? " \xc2\xb7 Reminder set \xe2\x9c\x93" : " \xc2\xb7 OK: Remind me";
        break;
      }
      case CellKind::Game: {
        const EventTile* t = s.eventTile(cell.id);
        if (!t) {
          l.missing = true;
          break;
        }
        corner = t;
        if (t->sides.size() == 2) l.sides = t->sides;
        l.logoUrl = t->channelLogo;
        l.logoName = t->channelName;
        l.title = l.cardTitle = matchup(*t);
        l.live = liveTile(*t);
        l.line = gameLine(c, *t);
        l.cardTag = "GAME" + (t->league.empty() ? std::string() : " \xc2\xb7 " + t->league) +
                    (t->favorite ? " \xc2\xb7 YOUR TEAM \xe2\x98\x85" : "");
        break;
      }
      case CellKind::Team: {
        const se::TeamEntry* t = team(cell.id);
        if (!t) {
          l.missing = true;
          break;
        }
        l.sides = {teamSide(*t)};
        l.title = t->name + (s.followsTeam(t->key) ? " \xe2\x98\x85" : "");
        l.cardTitle = l.cornerTitle = t->name;
        l.line = teamLine(c, *t, l.live);
        // Its games say why already when they're not in your channels.
        if (cell.outside && l.line.find("Not in your channels") == std::string::npos &&
            l.line.find("Streaming only") == std::string::npos)
          l.line += std::string(" \xc2\xb7 ") + kOutside;
        l.cardTag = "TEAM \xc2\xb7 " + t->league;
        corner = s.eventTile(teamGame(*t));
        break;
      }
      case CellKind::Genre:
        l.title = l.cardTitle = l.cornerTitle = cell.title;
        l.line = "Live now and later today";
        l.cardTag = cell.id.rfind("genre:", 0) == 0 ? "GENRE" : cell.id.rfind("sport:", 0) == 0 ? "SPORT" : "LEAGUE";
        break;
      case CellKind::Scope:
        if (cell.id == "wide") {
          l.title = "More outside your lineup";
          l.line = cell.title;  // the first few names
        } else {
          l.title = "Only your lineup and Live Events";
          l.line = "Hide what\xe2\x80\x99s outside your lineup";
        }
        l.cornerTitle = l.title;
        break;
      case CellKind::Follow:
        l.title = "\xe2\x98\x85 Follow your teams";
        l.line = "Their games show up here first. OK opens My Sports.";
        l.cornerTitle = cell.title;
        break;
      default: l.cornerTitle = cell.title; break;
    }
    l.cardLineOk = l.line;
    if (!l.ok.empty()) l.cardLineOk += (l.line.empty() ? "" : " \xc2\xb7 ") + l.ok;
    if (const EventTile* t = corner) {
      l.cornerTitle = matchup(*t);
      l.cornerLogo = t->channelLogo;
      l.cornerName = t->channelName;
      if (!t->watchable)
        l.cornerLine = std::string(t->footer == "Streaming only" ? "Streaming only" : "Not in your channels") +
                       " \xc2\xb7 OK: Where to Watch";
      else if (t->finished) l.cornerLine = "Ended";
      else if (liveTile(*t)) l.cornerLine = "LIVE " + t->status;
      else if (t->start)
        l.cornerLine = whenText(c, t->start) + (s.hasReminder(t->id) ? " \xc2\xb7 Reminder set \xe2\x9c\x93" : " \xc2\xb7 OK: Remind me");
    }
  }

  // "230 · LIVE Bulls at Harbor Kings · Q3" or "24 · The Evening Report".
  std::string channelLine(const ui::Ctx& c, const GuideChannel& g, bool& live) {
    std::string line = g.number ? std::to_string(*g.number) : std::string();
    if (g.style == GuideChannel::Style::Local && !g.subtitle.empty())
      line += (line.empty() ? "" : " \xc2\xb7 ") + g.subtitle;
    // A live game on it, by the board (its state, never a score).
    for (const se::GameEntry& x : index_->games) {
      if (x.state != se::GameEntry::State::Live || !x.watchable) continue;
      const EventTile* t = app_.store().eventTile(x.id);
      if (t && t->channelName == g.name) {
        live = true;
        return line + (line.empty() ? "" : " \xc2\xb7 ") + matchup(*t) + (t->status.empty() ? "" : " \xc2\xb7 " + t->status);
      }
    }
    views::NowNext nn = views::nowAndNext(*app_.store().listings(), g.id, c.now);
    if (nn.now) line += (line.empty() ? "" : " \xc2\xb7 ") + nn.now->title;
    return line;
  }

  // "Tonight 10:00 PM · NBX 230 · also tomorrow 7:00 AM".
  std::string showLine(const ui::Ctx& c, const Cell& cell, const GuideChannel* g) {
    const Listing& l = cell.listing;
    std::string line;
    if (app_.store().hasShowReminder(cell.entryId, l.start)) line = "Reminder set \xe2\x9c\x93 \xc2\xb7 ";
    line += l.isOn(c.now) ? "Now until " + ui::clock(c, l.end) : whenText(c, l.start);
    if (g) line += " \xc2\xb7 " + g->name + (g->number ? " " + std::to_string(*g->number) : "");
    if (cell.also) {
      std::string day = ui::dayWord(c, cell.also);
      for (char& ch : day) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
      line += " \xc2\xb7 also " + (day.empty() ? std::string("today") : day) + " " + ui::clock(c, cell.also);
    }
    if (!l.episode.empty() && l.isOn(c.now)) line += " \xc2\xb7 " + l.episode;
    return line;
  }

  // "Q3 · PBL · NBX Sports", "in 20 min · …", "Ended", "Not in your channels".
  std::string gameLine(const ui::Ctx& c, const EventTile& t) {
    std::string line;
    const char* dot = " \xc2\xb7 ";
    if (app_.store().hasReminder(t.id) && t.upcoming) line = "Reminder set \xe2\x9c\x93 \xc2\xb7 ";
    if (t.finished) line += "Ended";
    else if (liveTile(t)) line += t.status;
    else if (t.start > 0) line += whenText(c, t.start);
    if (!t.league.empty()) line += (line.empty() ? "" : dot) + t.league;
    if (!t.watchable) line += std::string(dot) + (t.footer == "Streaming only" ? "Streaming only" : "Not in your channels");
    else if (!t.channelName.empty()) line += dot + t.channelName;
    return line;
  }

  // "PBL · LIVE Q3 · Bulls at Harbor Kings · NBX Sports", "PBL · Next: Sat
  // 7:30 PM · …", "No game in the next week".
  std::string teamLine(const ui::Ctx& c, const se::TeamEntry& t, bool& live) {
    std::string line = t.league;
    const char* dot = " \xc2\xb7 ";
    if (!t.leaguePicked) return line + dot + "Add " + t.league + " in My Sports to see their games";
    if (const EventTile* g = app_.store().eventTile(t.liveGame)) {
      live = true;
      line += dot + g->status + dot + matchup(*g);
      line += dot + (g->watchable ? g->channelName : std::string("Not in your channels"));
      return line;
    }
    if (const EventTile* g = app_.store().eventTile(t.nextGame)) {
      line += std::string(dot) + "Next: " + whenText(c, g->start) + dot + matchup(*g);
      if (!g->watchable) line += std::string(dot) + (g->footer == "Streaming only" ? "Streaming only" : "Not in your channels");
      else if (!g->channelName.empty()) line += dot + g->channelName;
      return line;
    }
    return line + dot + "No game in the next week";
  }

  // The corner: LG's number box; the video while the preview plays; else
  // the card of the focused item (the top result's while typing).
  void drawCorner(ui::Ctx& c) {
    Rect r = top_bar::kPreview;
    c.hit(r, kHitCorner);
    if (!digits_.empty()) {
      c.g.fill(r, theme::hex(0x1C1F25, 0.96f), 16);
      ui::labelIn(c, digits_, {r.x, r.y, r.w, r.h - 50}, Weight::Bold, 64, theme::white(), Align::Center);
      if (digitsLineFor_ != digits_ || digitsLineIndex_ != index_) {
        digitsLineFor_ = digits_;
        digitsLineIndex_ = index_;
        digitsLine_ = digitsNow().waits ? "OK: Watch channel " + digits_ : "Plays when the number is complete";
      }
      ui::labelIn(c, digitsLine_, {r.x, r.y + r.h - 70, r.w, 40}, Weight::Medium, 20, theme::white(0.7f), Align::Center);
      return;
    }
    Playback& p = app_.playback();
    const Cell* cell = inResults() ? focusedCell() : nullptr;
    if (!cell) {
      for (const Row& row : rows_)
        if (row.kind == RowKind::Band && !row.cells.empty()) {
          cell = &row.cells.front();
          break;
        }
    }
    if (p.active() && !p.full() && p.ready()) {
      top_bar::preview(c, p, "", "");
      // A caption naming what plays, and that it is muted (made again when
      // what plays, the cell or the rows' text change).
      if (captionEntry_ != p.entry() || captionCell_ != cell || captionGeneration_ != labelGeneration_) {
        captionEntry_ = p.entry();
        captionCell_ = cell;
        captionGeneration_ = labelGeneration_;
        caption_ = cornerCaption(cell);
      }
      float x = r.x;
      x += ui::chip(c, "MUTED", x, r.y + r.h + 10, 15, theme::white(0.18f), theme::white()) + 10;
      ui::label(c, caption_, x, r.y + r.h + 8, Weight::Medium, 20, theme::white(0.8f), r.x + r.w - x);
      return;
    }
    c.g.fill(r, theme::white(0.05f), 16);
    c.g.stroke(r, theme::white(0.15f), 16, 1);
    if (!cell) return;
    // The card: a logo or badges, the title and its time (no video).
    const Cell::Label& l = cell->label;
    if (!l.cornerLogo.empty() || !l.cornerName.empty()) ui::logo(c, l.cornerLogo, l.cornerName, {r.x + 24, r.y + 24, 120, 56});
    ui::paragraph(c, l.cornerTitle, r.x + 24, r.y + 96, r.w - 48, Weight::Bold, 26, theme::white(), 2, 1.2f);
    ui::label(c, l.cornerLine, r.x + 24, r.y + r.h - 44, Weight::Medium, 20, theme::white(0.7f), r.w - 48);
  }

  std::string cornerCaption(const Cell* cell) {
    Store& s = app_.store();
    Playback& p = app_.playback();
    const EventTile* t = s.eventTile(p.entry());
    if (t) return (t->channelName.empty() ? std::string() : t->channelName + " \xc2\xb7 ") + matchup(*t);
    const GuideChannel* g = s.channels()->find(p.entry());
    if (!g) return {};
    std::string what;
    if (cell && cell->kind == CellKind::Show) what = cell->listing.title;
    else if (const Listing* l = views::nowAndNext(*s.listings(), g->id, time::now()).now) what = l->title;
    return g->name + (what.empty() ? "" : " \xc2\xb7 " + what);
  }

  void drawKeyBar(ui::Ctx& c) {
    hints::Bar bar;
    if (!digits_.empty()) {
      const se::Digits& d = digitsNow();
      bar = hints::search(app_.remoteKeys(), hints::SearchPlace::Digits, false,
                          {digits_, d.waits ? d.team : std::string(), d.waits});
    } else {
      hints::SearchPlace place = inResults()                                ? hints::SearchPlace::Results
                                 : grid_.page() == search_keys::Page::Digits ? hints::SearchPlace::Keypad
                                                                            : hints::SearchPlace::Keyboard;
      bar = hints::search(app_.remoteKeys(), place, hasResults());
    }
    ui::keyBar(c, bar, kb::kLeft);
  }

  std::function<void(rail::Item)> go_;
  rail::Nav nav_;
  left_rail::Slide menu_;
  Grid grid_;
  bool appleTv_ = false;
  std::string query_;
  bool exactOnly_ = false;
  bool wide_ = false;  // everything, not only your lineup and Live Events (while Search is open)
  std::shared_ptr<const se::Index> index_;
  // What the query found (each letter), the shows (after the pause, on
  // the index they were found on) and the two merged.
  std::optional<se::Found> find_;
  std::optional<se::FoundShows> shows_;
  std::shared_ptr<const se::Index> showsIndex_;
  se::Query showsQuery_;
  bool showsScoped_ = false;
  se::Results found_;
  bool showsIn_ = false;
  uint32_t showsAt_ = 0;
  uint64_t eventsVersion_ = 0, channelsVersion_ = 0, listingsVersion_ = 0;
  Page page_;
  std::vector<Row> rows_;
  bool results_ = false;  // focus is in the results
  int row_ = 0, col_ = 0;
  float scroll_ = 0;
  search_keys::GlideStop glide_;
  int pointer_ = 0;
  std::string digits_;
  uint32_t digitsAt_ = 0;
  bool digitsMoved_ = false;  // an arrow moved the grid's focus since the first digit
  preview::Pace pace_{true};
  uint32_t watchedAt_ = 0;  // when OK went full screen from here
  uint32_t refreshedAt_ = 0;
  uint32_t wake_ = 0;  // tick() has something due then (wakeAt())
  // The number box: its digits worked out once per digit.
  se::DigitsCache digitsCache_;
  std::string settledFor_;
  uint64_t settledVersion_ = 0;
  bool settled_ = false;
  std::string digitsLine_, digitsLineFor_;
  std::shared_ptr<const se::Index> digitsLineIndex_;
  // The rows' text (label()), and what it was made from.
  bool labelsDirty_ = true;
  time::Seconds labelMinute_ = 0;
  std::array<uint64_t, 3> labeled_{};
  uint64_t labelGeneration_ = 0;
  std::string pillText_, showingText_, nothingText_, nothingInText_;
  std::string caption_, captionEntry_;
  const Cell* captionCell_ = nullptr;
  uint64_t captionGeneration_ = 0;
  // The timing log (logStats()).
  struct Stats {
    int keys = 0, shows = 0, over = 0;  // over: passes and frames of 50 ms or more
    double runMax = 0, runSum = 0, showsMax = 0, layoutMax = 0, frameMax = 0;
    uint32_t since = 0;
  };
  Stats stats_;
};

}  // namespace

std::unique_ptr<Screen> makeSearch(App& app, std::function<void(rail::Item)> go) {
  return std::make_unique<SearchScreen>(app, std::move(go));
}

}  // namespace marigold::app
