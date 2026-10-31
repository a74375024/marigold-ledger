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
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
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
#include "core/util/strings.hpp"

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
constexpr int kHitKeyboard = 1000, kHitCorner = 1600, kHitPill = 1601, kHitCells = 2000;

// The last query, for reopening within five minutes.
struct Last {
  std::string query;
  uint32_t at = 0;
  bool set = false;
};
Last gLast;

enum class CellKind { Channel, Team, Game, Show, Genre, Chip, Recent, ClearRecents, Instead, Follow };

struct Cell {
  CellKind kind = CellKind::Channel;
  std::string id;     // entry id, team key, event id, genre or chip key, recent id
  std::string title;  // chips, genres, recent searches
  std::string entryId;
  Listing listing;    // shows
  time::Seconds also = 0;
  bool top = false;   // the band's top result card
};

enum class RowKind { Band, Tiles, Chips, List };

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
  ~SearchScreen() override { app_.store().eventsOpen(false); }

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
    bool busy = false;
    if (showsAt_) {
      if (ms >= showsAt_) {
        showsAt_ = 0;
        search(true);
      }
      busy = true;
    }
    if (!digits_.empty() && digitsTick(ms)) busy = true;
    // New channels, guide or board: the lists again (never under the focus,
    // and at most every few seconds while the guide is still loading).
    if ((app_.store().events()->version != eventsVersion_ || app_.store().channels()->version != channelsVersion_ ||
         app_.store().listings()->version != listingsVersion_) &&
        !inResults() && ms - refreshedAt_ >= kRefreshMs) {
      refreshedAt_ = ms;
      search(!showsAt_);
    }
    if (previewTick(ms)) busy = true;
    return busy || menu_.moving(ms);
  }

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
    // Red clears the typed text (never recent searches).
    if (k == Key::Red) {
      setQuery("");
      focusKeyboard();
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
    if (kb::spotFor(id, kHitKeyboard, spot)) {
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

  // Runs the query (`shows`: the guide's shows too, after the pause) and
  // lays out the rows, unless focus is in them.
  void search(bool shows) {
    Store& s = app_.store();
    eventsVersion_ = s.events()->version;
    channelsVersion_ = s.channels()->version;
    listingsVersion_ = s.listings()->version;
    index_ = s.searchIndex();
    se::Query q = parsed();
    if (!shows && q.length() >= 2 && !showsAt_) showsAt_ = app_.ms() + kShowsDelayMs;
    else if (shows) showsAt_ = 0;
    se::Options o;
    o.now = time::now();
    o.zone = app_.zone();
    o.showScores = s.settings().showScores;
    o.shows = shows || q.length() < 2;
    found_ = se::run(*index_, q, o);
    showsIn_ = o.shows;
    if (!inResults()) layout();
  }

  // ---------------------------------------------------------------- layout

  void layout() {
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

  static Cell cellOf(const se::Item& it) {
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
      return;
    }
    std::vector<Cell> rest(recent.begin() + static_cast<std::ptrdiff_t>(recentFrom), recent.end());
    addTiles(rest, recentFrom ? "" : "RECENTLY WATCHED");
    addRecentSearches();
  }

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

  void layoutResults() {
    const se::Results& r = found_;
    bool topReady = showsIn_ || r.query.length() < 2;
    if (r.nothing && topReady) {
      // One plain line with what to try, then the chips and recent searches.
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
      return;
    }
    if (r.top && topReady) {
      Cell c = cellOf(*r.top);
      c.top = true;
      addBand(c, r.showing.empty() ? "TOP RESULT" : "");
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
    se::Digits d = se::digits(*index_, digits_);
    if (!d.channel.empty()) {
      Cell c;
      c.kind = CellKind::Channel;
      c.id = d.channel;
      addBand(c, "CHANNEL " + digits_);
    }
    std::vector<Cell> also;
    for (size_t i = 0; i < d.also.size() && i < 4; ++i) {
      Cell c;
      c.kind = CellKind::Channel;
      c.id = d.also[i];
      also.push_back(c);
    }
    if (d.waits && !d.team.empty()) {
      se::Options o;
      o.now = time::now();
      o.showScores = app_.store().settings().showScores;
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
          } else if (at.col == static_cast<int>(search_keys::Wide::Space)) {
            if (!query_.empty() && query_.back() != ' ') setQuery(query_ + " ");
          } else {
            setQuery("");
          }
        } else {
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

  // The number plays when it is complete: after a pause, or at once when
  // no longer number starts with it; never by itself when the digits also
  // start a team or channel name (OK plays it).
  // True while it waits out the pause.
  bool digitsTick(uint32_t ms) {
    se::Digits d = se::digits(*index_, digits_);
    if (d.waits) return false;
    if (ms - digitsAt_ < kDigitsMs && !views::digitsSettled(digits_, *app_.store().channels())) return true;
    std::string number = digits_;
    digits_.clear();
    if (d.channel.empty()) {
      app_.toast("No channel " + number);
      layout();
      return false;
    }
    watchChannel(d.channel, true);
    return false;
  }

  // Keys while LG's number box shows. OK watches the channel, unless the
  // focus was moved to a grid key (or the pointer clicks one): a letter
  // carries on the name ("49…"). ▼ goes to the name the digits start.
  bool digitsKey(const KeyEvent& e) {
    se::Digits d = se::digits(*index_, digits_);
    switch (e.key) {
      case Key::Ok: {
        Grid::Spot spot;
        bool pointed = pointer_ && kb::spotFor(pointer_, kHitKeyboard, spot);
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
  bool previewTick(uint32_t ms) {
    Playback& p = app_.playback();
    if (app_.nav().top() != this) return false;
    const Cell* c = inResults() ? focusedCell() : nullptr;
    std::string id = c ? previewId(*c) : std::string();
    if (p.active() && !p.full() && p.entry() != id) p.stop();
    switch (pace_.step(id, !id.empty() && p.entry() == id, ms)) {
      case preview::Pace::Step::Idle: return false;
      case preview::Pace::Step::Wait: return true;
      case preview::Pace::Step::Tune: break;
    }
    p.setCorner(top_bar::kPreview, true);
    bool event = app_.store().isEvent(id);
    if (!(event ? p.tune(id, app_.store().eventFlipList(id)) : p.tune(id))) p.stop();
    return false;
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
      std::string text = page_.title + "  \xc3\x97";
      float w = ui::chipWidth(c, text, 20);
      c.hit({kX, 34, w, 30}, kHitPill);
      ui::chip(c, text, kX, 34, 20, theme::hex(theme::kNowLine, 0.3f), theme::white());
    }
    if (!found_.showing.empty() && page_.empty() && digits_.empty() && !found_.query.text.empty()) {
      ui::label(c, "Showing " + found_.showing, kX, 40, Weight::Bold, 26, theme::white(), kBandW);
    }
    if (found_.nothing && showsIn_ && page_.empty() && digits_.empty()) {
      ui::label(c, "Nothing for \xe2\x80\x9c" + query_ + "\xe2\x80\x9d", kX, 96, Weight::Bold, 34, theme::white(), kBandW);
      ui::paragraph(c, "Not in your channels or the guide right now. Try a channel name or number, a show or a team.",
                    kX, 150, kBandW, Weight::Regular, 24, theme::white(0.7f), 3);
    }
    bool waiting = !showsIn_ && found_.query.length() >= 2 && page_.empty() && digits_.empty();
    if (waiting) {
      // The top result waits for the shows (it never swaps after it's read).
      ui::label(c, "TOP RESULT", kX, 52, Weight::Bold, 20, theme::white(0.75f));
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
      default: return {};
    }
  }

  void drawCell(ui::Ctx& c, const Row& row, const Cell& cell, int r, int k, float y) {
    bool f = focusedAt(r, k);
    float w = row.kind == RowKind::Tiles ? kTileW : row.kind == RowKind::Band ? kBandW : kW;
    float x = kX + (row.kind == RowKind::Tiles ? static_cast<float>(k) * (kTileW + 20) : 0);
    Rect box{x, y, w, row.h};
    c.hit(box, kHitCells + r * 100 + k);
    c.g.fill(box, f ? theme::white() : theme::white(0.07f), 18);
    Color ink = f ? theme::black() : theme::white();
    Color sub = f ? theme::black(0.65f) : theme::white(0.7f);
    std::string ok = f ? okText(cell) : std::string();
    float okW = ok.empty() ? 0 : ui::textWidth(c, ok, Weight::Bold, 20) + 24;
    if (cell.top) {
      drawTopCard(c, cell, box, f, ok);
      return;
    }
    float tx = box.x + 22, right = box.x + box.w - 22 - okW;
    if (!ok.empty()) ui::labelIn(c, ok, {right + 12, box.y, okW - 12, box.h}, Weight::Bold, 20, ink, Align::Right);
    std::string title, line;
    bool west = false;  // a West feed: its WEST chip
    bool live = false;
    std::string tag;
    switch (cell.kind) {
      case CellKind::Channel: {
        const GuideChannel* g = app_.store().channels()->find(cell.id);
        if (!g) return;
        ui::logo(c, g->logoUrl, g->name, {tx, box.y + (box.h - 54) * 0.5f, 96, 54}, f);
        tx += 116;
        title = g->name;
        line = channelLine(c, *g, live);
        west = g->west;
        break;
      }
      case CellKind::Show: {
        const GuideChannel* g = app_.store().channels()->find(cell.entryId);
        if (g) ui::logo(c, g->logoUrl, g->name, {tx, box.y + (box.h - 48) * 0.5f, 84, 48}, f);
        tx += 104;
        title = cell.listing.title;
        live = cell.listing.isOn(c.now);
        tag = se::showTag(se::fold(title));
        line = showLine(c, cell, g);
        break;
      }
      case CellKind::Game: {
        const EventTile* t = app_.store().eventTile(cell.id);
        if (!t) return;
        tx += badges(c, *t, tx, box.y + (box.h - 52) * 0.5f, 52, f);
        title = matchup(*t);
        live = liveTile(*t);
        line = gameLine(c, *t);
        break;
      }
      case CellKind::Team: {
        const se::TeamEntry* t = team(cell.id);
        if (!t) return;
        tx += teamBadge(c, *t, tx, box.y + (box.h - 52) * 0.5f, f) + 18;
        title = t->name + (app_.store().followsTeam(t->key) ? " \xe2\x98\x85" : "");
        line = teamLine(c, *t, live);
        break;
      }
      case CellKind::Genre:
        title = cell.title;
        line = "Live now and later today";
        break;
      case CellKind::Follow:
        title = "\xe2\x98\x85 Follow your teams";
        line = "Their games show up here first. OK opens My Sports.";
        break;
      default: break;
    }
    float ty = box.y + box.h * 0.5f - 36;
    float lx = tx;
    if (live) lx += ui::chip(c, "LIVE", lx, ty + 4, 17, theme::hex(theme::kLive), theme::white()) + 10;
    if (west) lx += ui::coastChip(c, true, lx, ty + 4, 17) + 10;
    if (!tag.empty()) lx += ui::chip(c, tag, lx, ty + 4, 17, f ? theme::black(0.15f) : theme::white(0.18f), ink) + 10;
    ui::label(c, title, lx, ty, Weight::Bold, 26, ink, right - lx);
    ui::label(c, line, tx, ty + 38, Weight::Medium, 22, sub, right - tx);
  }

  // The top result: a type tag, a big title and what's on, beside the corner.
  void drawTopCard(ui::Ctx& c, const Cell& cell, Rect box, bool f, const std::string& ok) {
    Store& s = app_.store();
    Color ink = f ? theme::black() : theme::white();
    Color sub = f ? theme::black(0.65f) : theme::white(0.75f);
    std::string tag, title, line;
    bool live = false;
    float tx = box.x + 22, ty = box.y + 64;
    switch (cell.kind) {
      case CellKind::Channel: {
        const GuideChannel* g = s.channels()->find(cell.id);
        if (!g) return;
        tag = "CHANNEL" + std::string(g->number ? " \xc2\xb7 " + std::to_string(*g->number) : "") +
              (g->inLineup ? " \xc2\xb7 MY LINEUP" : "");
        ui::logo(c, g->logoUrl, g->name, {tx, ty, 112, 64}, f);
        tx += 132;
        title = g->name;
        line = channelLine(c, *g, live);
        break;
      }
      case CellKind::Game: {
        const EventTile* t = s.eventTile(cell.id);
        if (!t) return;
        tag = "GAME" + (t->league.empty() ? std::string() : " \xc2\xb7 " + t->league) +
              (t->favorite ? " \xc2\xb7 YOUR TEAM \xe2\x98\x85" : "");
        tx += badges(c, *t, tx, ty, 64, f);
        title = matchup(*t);
        live = liveTile(*t);
        line = gameLine(c, *t);
        break;
      }
      case CellKind::Team: {
        const se::TeamEntry* t = team(cell.id);
        if (!t) return;
        tag = "TEAM \xc2\xb7 " + t->league;
        tx += teamBadge(c, *t, tx, ty, f) + 18;
        title = t->name;
        line = teamLine(c, *t, live);
        break;
      }
      case CellKind::Show: {
        const GuideChannel* g = s.channels()->find(cell.entryId);
        std::string rep = se::showTag(se::fold(cell.listing.title));
        tag = "SHOW" + (rep.empty() ? std::string() : " \xc2\xb7 " + rep);
        if (g) ui::logo(c, g->logoUrl, g->name, {tx, ty, 112, 64}, f);
        tx += 132;
        title = cell.listing.title;
        live = cell.listing.isOn(c.now);
        line = showLine(c, cell, g);
        break;
      }
      case CellKind::Genre:
        tag = cell.id.rfind("genre:", 0) == 0 ? "GENRE" : cell.id.rfind("sport:", 0) == 0 ? "SPORT" : "LEAGUE";
        title = cell.title;
        line = "Live now and later today";
        break;
      default: break;
    }
    ui::label(c, tag, box.x + 22, box.y + 18, Weight::Bold, 18, sub, box.w - 44);
    // A long title wraps to two lines here only.
    ui::paragraph(c, title, tx, ty, box.x + box.w - 22 - tx, Weight::Bold, 32, ink, 2, 1.15f);
    float lx = box.x + 22, ly = box.y + box.h - 48;
    if (live) lx += ui::chip(c, "LIVE", lx, ly + 2, 18, theme::hex(theme::kLive), theme::white()) + 10;
    if (!ok.empty()) line += (line.empty() ? "" : " \xc2\xb7 ") + ok;
    ui::label(c, line, lx, ly, Weight::Medium, 22, sub, box.x + box.w - 22 - lx);
  }

  // Two side badges (or the sport's mark). Returns the width used.
  float badges(ui::Ctx& c, const EventTile& t, float x, float y, float size, bool f) {
    if (t.sides.size() == 2) {
      events_ui::badge(c, t.sides[0], x, y, size, f);
      events_ui::badge(c, t.sides[1], x + size + 8, y, size, f);
      return 2 * size + 28;
    }
    ui::logo(c, t.channelLogo, t.channelName, {x, y, size * 1.6f, size}, f);
    return size * 1.6f + 18;
  }

  float teamBadge(ui::Ctx& c, const se::TeamEntry& t, float x, float y, bool f) {
    // The badge the board has for it, when it has a game; else initials.
    for (const std::string& gid : {t.liveGame, t.nextGame}) {
      const EventTile* g = app_.store().eventTile(gid);
      if (!g || g->sides.size() != 2) continue;
      for (const EventSide& s : g->sides) {
        if (se::fold(s.name).empty() || t.names.empty()) continue;
        if (t.names.front().find(se::fold(s.name)) != std::string::npos) {
          events_ui::badge(c, s, x, y, 52, f);
          return 52;
        }
      }
    }
    EventSide side;
    side.abbr = se::initials(se::words(se::fold(t.name)));
    for (char& ch : side.abbr) ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    events_ui::badge(c, side, x, y, 52, f);
    return 52;
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
      se::Digits d = se::digits(*index_, digits_);
      ui::labelIn(c, d.waits ? "OK: Watch channel " + digits_ : "Plays when the number is complete",
                  {r.x, r.y + r.h - 70, r.w, 40}, Weight::Medium, 20, theme::white(0.7f), Align::Center);
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
      // A caption naming what plays, and that it is muted.
      std::string caption = cornerCaption(cell);
      float x = r.x;
      x += ui::chip(c, "MUTED", x, r.y + r.h + 10, 15, theme::white(0.18f), theme::white()) + 10;
      ui::label(c, caption, x, r.y + r.h + 8, Weight::Medium, 20, theme::white(0.8f), r.x + r.w - x);
      return;
    }
    c.g.fill(r, theme::white(0.05f), 16);
    c.g.stroke(r, theme::white(0.15f), 16, 1);
    if (!cell) return;
    // The card: a logo or badges, the title and its time (no video).
    std::string logo, name, title, line;
    Store& s = app_.store();
    const EventTile* t = nullptr;
    switch (cell->kind) {
      case CellKind::Channel:
        if (const GuideChannel* g = s.channels()->find(cell->id)) {
          logo = g->logoUrl;
          name = title = g->name;
          views::NowNext nn = views::nowAndNext(*s.listings(), g->id, c.now);
          if (nn.now) line = nn.now->title;
        }
        break;
      case CellKind::Show:
        if (const GuideChannel* g = s.channels()->find(cell->entryId)) {
          logo = g->logoUrl;
          name = g->name;
        }
        title = cell->listing.title;
        line = cell->listing.isOn(c.now) ? "Now until " + ui::clock(c, cell->listing.end) : whenText(c, cell->listing.start);
        if (!cell->listing.isOn(c.now))
          line += s.hasShowReminder(cell->entryId, cell->listing.start) ? " \xc2\xb7 Reminder set \xe2\x9c\x93" : " \xc2\xb7 OK: Remind me";
        break;
      case CellKind::Team:
        if (const se::TeamEntry* tm = team(cell->id)) {
          title = tm->name;
          t = s.eventTile(teamGame(*tm));
        }
        break;
      case CellKind::Game: t = s.eventTile(cell->id); break;
      default: title = cell->title; break;
    }
    if (t) {
      title = matchup(*t);
      logo = t->channelLogo;
      name = t->channelName;
      if (!t->watchable) line = std::string(t->footer == "Streaming only" ? "Streaming only" : "Not in your channels") + " \xc2\xb7 OK: Where to Watch";
      else if (t->finished) line = "Ended";
      else if (liveTile(*t)) line = "LIVE " + t->status;
      else if (t->start) line = whenText(c, t->start) + (s.hasReminder(t->id) ? " \xc2\xb7 Reminder set \xe2\x9c\x93" : " \xc2\xb7 OK: Remind me");
    }
    if (!logo.empty() || !name.empty()) ui::logo(c, logo, name, {r.x + 24, r.y + 24, 120, 56});
    ui::paragraph(c, title, r.x + 24, r.y + 96, r.w - 48, Weight::Bold, 26, theme::white(), 2, 1.2f);
    ui::label(c, line, r.x + 24, r.y + r.h - 44, Weight::Medium, 20, theme::white(0.7f), r.w - 48);
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
      se::Digits d = se::digits(*index_, digits_);
      bar = hints::search(app_.remoteKeys(), hints::SearchPlace::Digits, false,
                          {digits_, d.waits ? d.team : std::string()});
    } else {
      bar = hints::search(app_.remoteKeys(), inResults() ? hints::SearchPlace::Results : hints::SearchPlace::Keyboard,
                          hasResults());
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
  std::shared_ptr<const se::Index> index_;
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
};

}  // namespace

std::unique_ptr<Screen> makeSearch(App& app, std::function<void(rail::Item)> go) {
  return std::make_unique<SearchScreen>(app, std::move(go));
}

}  // namespace marigold::app
