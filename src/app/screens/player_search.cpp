#include "app/screens/player_search.hpp"

#include <algorithm>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/left_rail.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "app/ui/keyboard.hpp"
#include "core/engine/views.hpp"
#include "core/input/hints.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;
namespace se = engine::search;
namespace kb = ui::keyboard;
namespace views = engine::views;
using search_view::Cell;
using search_view::CellKind;
using search_keys::Grid;
using Do = rail::PlayerStep::Do;

// Inside the guide's card (its top line and legend are the guide's).
constexpr float kInset = 64;          // the content's left edge in the card
constexpr float kTopY = 52;           // the guide's top line
constexpr float kBodyY = 96;          // the keyboard's field and the results start this far down the card
constexpr float kResultsX = 650;      // the results column, from the content's left edge
constexpr float kResultsRight = 56;   // its right margin in the card
constexpr float kScale = 0.8f;        // the Search screen's cells, smaller
constexpr float kTopH = 160, kRowH = 76, kGap = 8, kHeaderBand = 30, kGroupGap = 12;
constexpr uint32_t kRefreshMs = 3000;  // new data redraws the results at most this often
constexpr float kListsRight = 64;      // "◀ Lists" ends this far from the card's right

rail::PlayerStep none() { return {Do::None}; }

}  // namespace

PlayerSearch::PlayerSearch(App& app, rail::PlayerNav& nav) : app_(app), nav_(nav) {
  RemoteKeys platform = app_.window().remoteKeys();
  // 🎤 Speak where the Search screen has it: the TV's keyboard takes
  // dictation (LG, Android TV, Apple TV), not Fire TV.
  grid_.setSpeak(app_.window().keyboardAvailable() && !(platform == RemoteKeys::fireTv()));
  appleTv_ = platform.clickpad;
}

PlayerSearch::~PlayerSearch() {
  if (open_) app_.store().eventsOpen(false);
}

void PlayerSearch::open() {
  if (!open_) {
    open_ = true;
    // Games and their states stay fresh while it shows (as on the Search
    // screen).
    app_.store().eventsOpen(true);
    // The query typed last while the player has been open.
    query_ = app_.playback().playerQuery();
    if (query_.empty()) grid_.letters();
    rows_.clear();
    row_ = 0;
    scroll_ = 0;
    search(true);
    return;
  }
  focusKeyboard();
}

void PlayerSearch::close() {
  if (!open_) return;
  open_ = false;
  app_.store().eventsOpen(false);
}

void PlayerSearch::focusResults() {
  row_ = 0;
  scroll_ = 0;
}

void PlayerSearch::focusKeyboard() {
  // Back to an empty field: the letters.
  if (query_.empty()) grid_.letters();
  // What changed while the focus was in the results shows now.
  search(!live_.showsDue());
}

// ---------------------------------------------------------------- query

void PlayerSearch::setQuery(const std::string& q) {
  query_ = q;
  app_.playback().setPlayerQuery(q);
  live_.typed();  // the shows wait for a pause in the typing
  search(false);
}

// Each letter looks through everything but the guide's shows; `shows` adds
// them (after the pause). The Search screen's passes, your lineup first.
void PlayerSearch::search(bool shows, bool keep) {
  Store& s = app_.store();
  eventsVersion_ = s.events()->version;
  channelsVersion_ = s.channels()->version;
  listingsVersion_ = s.listings()->version;
  index_ = s.searchIndex();
  se::Options o;
  o.now = time::now();
  o.zone = app_.zone();
  o.showScores = s.settings().showScores;
  o.scoped = true;
  live_.run(index_, se::parseFor(index_, query_, false), o, shows, keep, app_.ms());
  labelsDirty_ = true;
  // The results don't move while the focus is in them.
  if (!inResults()) layout();
}

void PlayerSearch::add(const std::vector<se::Item>& items, const char* header, bool mark) {
  bool first = true;
  for (const se::Item& it : items) {
    if (it.kind == se::Kind::Genre) continue;  // a page of its own: the Search screen's
    Row r;
    if (first) r.header = header;
    first = false;
    r.cell = search_view::cellOf(it, mark);
    rows_.push_back(std::move(r));
  }
}

// The top result (once the shows are in, so it never swaps), then
// Channels, Games and Shows. Nothing in your lineup: what's outside, under
// one header.
void PlayerSearch::layout() {
  rows_.clear();
  labelsDirty_ = true;
  const se::Results& r = live_.results();
  bool topReady = live_.showsIn() || r.query.length() < 2;
  float y = 0;
  if (!r.query.text.empty() && !(r.nothing && topReady) && (!r.outsideOnly || topReady)) {
    // Outside only: unmarked (the header says where they are).
    bool mark = !r.outsideOnly;
    std::vector<se::Item> top;
    if (r.top && topReady && r.top->kind != se::Kind::Genre) top.push_back(*r.top);
    add(top, "TOP RESULT", mark);
    if (!rows_.empty()) rows_.back().cell.top = true;
    // While the top result waits for the shows, its place is kept.
    if (!topReady) y = kHeaderBand + kTopH + kGroupGap;
    add(r.channels, "CHANNELS", mark);
    add(r.games, "GAMES", mark);
    add(r.shows, "SHOWS", mark);
    if (r.outsideOnly) {
      for (Row& row : rows_) {
        row.header.clear();
        row.cell.top = false;
      }
      if (!rows_.empty()) rows_.front().header = "OUTSIDE YOUR LINEUP";
    }
  }
  for (size_t i = 0; i < rows_.size(); ++i) {
    Row& row = rows_[i];
    if (!row.header.empty()) y += (i ? kGroupGap : 0) + kHeaderBand;
    row.y = y;
    row.h = row.cell.top ? kTopH : kRowH;
    y += row.h + kGap;
  }
  row_ = std::clamp(row_, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
}

void PlayerSearch::wakeFor(uint32_t at) {
  if (!wake_ || at < wake_) wake_ = at;
}

std::array<uint64_t, 3> PlayerSearch::versions() const {
  Store& s = app_.store();
  return {s.events()->version, s.channels()->version, s.listings()->version};
}

bool PlayerSearch::tick(uint32_t ms) {
  wake_ = 0;
  if (!open_) return false;
  bool draw = false;
  // The shows once typing pauses (asleep until then).
  if (uint32_t due = live_.showsDue()) {
    if (ms >= due) {
      live_.typed();
      search(true, true);
      draw = true;
    } else {
      wakeFor(due);
    }
  }
  // New channels, guide, board or index: the results again (never under
  // the focus, and at most every few seconds while data keeps coming).
  Store& s = app_.store();
  bool fresh = s.events()->version != eventsVersion_ || s.channels()->version != channelsVersion_ ||
               s.listings()->version != listingsVersion_ || s.searchIndex() != index_;
  if (fresh && !inResults()) {
    if (ms - refreshedAt_ >= kRefreshMs) {
      refreshedAt_ = ms;
      search(!live_.showsDue());
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
  return draw;
}

void PlayerSearch::labelAll(const ui::Ctx& c) {
  labelsDirty_ = false;
  labelMinute_ = c.now / 60;
  labeled_ = versions();
  if (index_)
    for (Row& r : rows_) search_view::label(c, app_.store(), *index_, r.cell, "");
  nothingText_ = "Nothing for \xe2\x80\x9c" + query_ + "\xe2\x80\x9d";
}

// ---------------------------------------------------------------- keys

rail::PlayerStep PlayerSearch::key(const KeyEvent& e) {
  Key k = e.key;
  if (k != Key::Ok) pointer_ = 0;
  // Red clears the typed text, back on the letters.
  if (k == Key::Red) {
    setQuery("");
    grid_.letters();
    return inResults() ? nav_.toKeyboard() : none();
  }
  // Play/Pause changes the keyboard's page on Apple TV, as on Apple's own.
  if (k == Key::PlayPause && appleTv_ && !inResults()) {
    grid_.flip();
    return none();
  }
  // Number keys type into the field ("206", "sn2").
  if (int d = digitOf(k); d >= 0) {
    setQuery(query_ + static_cast<char>('0' + d));
    return inResults() ? nav_.toKeyboard() : none();
  }
  return inResults() ? resultsKey(e) : keyboardKey(e);
}

rail::PlayerStep PlayerSearch::keyboardKey(const KeyEvent& e) {
  switch (e.key) {
    case Key::Up:
    case Key::Down:
    case Key::Left:
    case Key::Right: {
      Grid::Move m = grid_.move(e.key, e.glide);
      if (m.edge == Grid::Edge::Right) return nav_.toResults(any());
      // Full left: the lists, as from any list.
      if (m.edge == Grid::Edge::Left) return nav_.openLists(e.glide);
      return none();
    }
    case Key::Ok: {
      search_keys::Press p = search_keys::press(grid_, query_);
      if (p.what == search_keys::Press::Do::Speak) app_.openKeyboard(query_);
      else if (p.what == search_keys::Press::Do::Text) setQuery(p.query);
      return none();
    }
    default: return none();
  }
}

// Up/Down through the results; Left back to the keyboard. A swipe stops at
// their edges (search_keys::GlideStop), as on the Search screen.
rail::PlayerStep PlayerSearch::resultsKey(const KeyEvent& e) {
  int n = static_cast<int>(rows_.size());
  switch (e.key) {
    case Key::Up:
    case Key::Down: {
      if (glide_.blocked(e.glide)) return none();
      int r = row_ + (e.key == Key::Up ? -1 : 1);
      if (r < 0 || r >= n) {
        glide_.stop(e.glide);
        return none();
      }
      row_ = r;
      glide_.moved(e.glide);
      return none();
    }
    case Key::Left:
      if (glide_.blocked(e.glide) || glide_.stop(e.glide)) return none();
      return nav_.toKeyboard();
    case Key::Right:
      if (!glide_.blocked(e.glide)) glide_.stop(e.glide);
      return none();
    case Key::Ok:
      if (row_ >= 0 && row_ < n) select(rows_[static_cast<size_t>(row_)].cell);
      return none();
    default: return none();
  }
}

void PlayerSearch::hover(int id) {
  pointer_ = id;
  Grid::Spot spot;
  if (kb::spotFor(id, kHitKeys, grid_.page(), spot)) {
    grid_.set(spot);
    if (nav_.toKeyboard().what == Do::Keyboard) focusKeyboard();
    return;
  }
  if (id >= kHitCells && id < kHitCells + static_cast<int>(rows_.size())) {
    if (!inResults() && nav_.toResults(any()).what == Do::Results) focusResults();
    if (inResults()) row_ = id - kHitCells;
  }
}

bool PlayerSearch::click(int id, rail::PlayerStep& step) {
  if (id == kHitLists) {
    step = nav_.openLists();
    return true;
  }
  if (id == kHitKeys + kb::kHitTab) {
    step = nav_.toResults(any());
    return true;
  }
  if (id == kHitKeys + kb::kHitField || id == left_rail::kHitNothing) {
    step = none();
    return true;
  }
  return false;  // a key or a result: the hover focused it, OK acts on it
}

void PlayerSearch::text(const std::string& value, bool closed) {
  // The TV's keyboard (🎤 Speak): its text comes back into our field.
  if (value != query_) setQuery(value);
  if (closed) grid_.set({Grid::Spot::Area::Speak, 0, 0});
}

// ---------------------------------------------------------------- picking

bool PlayerSearch::watchesHere(const Cell& cell) const {
  Store& s = app_.store();
  time::Seconds now = time::now();
  switch (cell.kind) {
    case CellKind::Channel: return true;
    case CellKind::Show: return cell.listing.start <= now + 60;
    case CellKind::Game: {
      const EventTile* t = s.eventTile(cell.id);
      return t && search_view::playsGame(*t, now);
    }
    case CellKind::Team: {
      const se::TeamEntry* t = index_ ? search_view::team(*index_, cell.id) : nullptr;
      const EventTile* g = t ? s.eventTile(search_view::teamGame(*t)) : nullptr;
      return g && search_view::playsGame(*g, now);
    }
    default: return false;
  }
}

void PlayerSearch::remember(const Cell& cell, const std::string& name) {
  // What was picked, by name: never the typed text (as on the Search screen).
  if (query_.empty()) return;
  app_.store().rememberSearch({name, search_view::recentId(cell)});
}

void PlayerSearch::select(const Cell& cell) {
  Store& s = app_.store();
  labelsDirty_ = true;
  switch (cell.kind) {
    case CellKind::Channel:
      if (cell.family) {
        watchFamily(cell.id);
        return;
      }
      if (const GuideChannel* g = s.channels()->find(cell.id)) remember(cell, g->name);
      watchChannel(cell.id);
      return;
    case CellKind::Show:
      remember(cell, cell.listing.title);
      // On now (or starting within a minute): it plays; later: its details.
      if (cell.listing.start <= time::now() + 60) watchChannel(cell.entryId);
      else app_.nav().push(makeProgramDetail(app_, cell.entryId, cell.listing));
      return;
    case CellKind::Game:
      if (const EventTile* t = s.eventTile(cell.id)) {
        remember(cell, matchup(*t));
        selectGame(cell.id);
      }
      return;
    case CellKind::Team:
      if (const se::TeamEntry* t = index_ ? search_view::team(*index_, cell.id) : nullptr) {
        remember(cell, t->name);
        selectGame(search_view::teamGame(*t));  // none this week: its line says so
      }
      return;
    default: return;
  }
}

// A game on now plays here (as from the board, but in place); a later one,
// or one not in your channels, opens its details (Remind Me, Where to Watch).
void PlayerSearch::selectGame(const std::string& eventId) {
  const EventTile* t = app_.store().eventTile(eventId);
  if (!t) return;
  if (search_view::playsGame(*t, time::now())) watchGame(eventId);
  else app_.nav().push(makeEventDetail(app_, eventId));
}

// The picture switches in place, as for a channel picked in the guide: CH+
// / CH- then go through My Lineup (every channel when it doesn't have this
// one), and the guide opens on it next time. The card closes.
void PlayerSearch::watchChannel(const std::string& entryId) {
  Playback& p = app_.playback();
  if (!p.tune(entryId, views::lineupList(*app_.store().channels(), entryId))) {
    app_.toast("This channel has no working feed right now");
    return;
  }
  p.pickedFrom(views::Tab::Lineup);
  app_.nav().pop();
}

// An event channel group: its first channel, Up/Down through the rest.
void PlayerSearch::watchFamily(const std::string& familyId) {
  std::vector<std::string> feeds = app_.store().familyFeeds(familyId);
  Playback& p = app_.playback();
  if (feeds.empty() || !p.tune(feeds.front(), feeds)) {
    app_.toast("No feeds in this family right now");
    return;
  }
  app_.nav().pop();
}

void PlayerSearch::watchGame(const std::string& eventId) {
  Playback& p = app_.playback();
  if (!(p.active() && p.entry() == eventId && !p.failed()) && !p.tune(eventId, app_.store().eventFlipList(eventId))) {
    app_.toast("This game has no working feed right now");
    return;
  }
  app_.nav().pop();
}

// ---------------------------------------------------------------- drawing

void PlayerSearch::draw(ui::Ctx& c, Rect card, float legendY, bool legend) {
  if (labelsDirty_ || c.now / 60 != labelMinute_) labelAll(c);
  float x0 = card.x + kInset;
  // The header as any list's: its name, and "◀ Lists" (the pointer's way
  // back to the lists) at the right.
  ui::labelIn(c, "SEARCH", {x0, kTopY, 400, 34}, Weight::Bold, 22, theme::white(0.7f));
  float listsX = card.x + card.w - kListsRight - left_rail::kColumnW - 6;
  left_rail::listsButton(c, listsX, kTopY - 3, c.pointerActive && pointer_ == kHitLists, 1.0f, kHitLists);
  kb::Look look;
  look.query = query_;
  look.focused = !inResults();
  look.tab = any() && !inResults();
  look.dx = x0 - kb::kLeft;
  look.dy = card.y + kBodyY - kb::kField.y;
  kb::draw(c, grid_, look, kHitKeys);
  if (appleTv_ && grid_.speak())
    ui::paragraph(c, "To speak: choose Speak, then hold the microphone button on the remote and say what you want.",
                  x0, kb::kBottom + look.dy + 36, 520, Weight::Regular, 22, theme::white(0.6f), 3);
  drawResults(c, card);
  if (legend) {
    bool watch = inResults() && row_ >= 0 && row_ < static_cast<int>(rows_.size()) &&
                 watchesHere(rows_[static_cast<size_t>(row_)].cell);
    ui::label(c, hints::playerSearch(app_.remoteKeys(), inResults(), any(), watch), x0, legendY, Weight::Regular, 22,
              theme::white(0.75f), card.w - 2 * kInset);
  }
}

void PlayerSearch::drawResults(ui::Ctx& c, Rect card) {
  float x = card.x + kInset + kResultsX;
  float w = card.x + card.w - kResultsRight - x;
  float top = card.y + kBodyY;
  float bottom = card.y + card.h - 72;
  const se::Results& r = live_.results();
  if (query_.empty()) {
    ui::paragraph(c, "Type a channel, show or team. What you\xe2\x80\x99re watching keeps playing until you pick something.",
                  x, top + 6, w, Weight::Regular, 22, theme::white(0.6f), 4);
    return;
  }
  bool topReady = live_.showsIn() || r.query.length() < 2;
  if (r.nothing && topReady) {
    ui::paragraph(c, nothingText_, x, top, w, Weight::Bold, 28, theme::white(), 2);
    ui::paragraph(c, "Not in your channels or the guide right now. Try a channel name or number, a show or a team.", x,
                  top + 80, w, Weight::Regular, 22, theme::white(0.7f), 4);
    return;
  }
  if (!topReady) {
    // The top result waits for the shows (it never swaps after it's read).
    ui::label(c, "TOP RESULT", x, top, Weight::Bold, 16, theme::white(0.5f));
    c.g.fill({x, top + kHeaderBand, w, kTopH}, ui::skeleton(c), 18 * kScale);
  }
  // Scroll so the focused row stays in view.
  float viewH = bottom - top;
  if (inResults() && row_ >= 0 && row_ < static_cast<int>(rows_.size())) {
    const Row& f = rows_[static_cast<size_t>(row_)];
    float fTop = f.y - (f.header.empty() ? 0 : kHeaderBand);
    if (fTop - scroll_ < 0) scroll_ = std::max(0.0f, fTop);
    if (f.y + f.h - scroll_ > viewH) scroll_ = f.y + f.h - viewH;
  } else {
    scroll_ = 0;
  }
  bool focusable = inResults() && nav_.mode() == rail::Mode::Content;
  c.g.pushClip({x - 12, top - 4, w + 24, viewH + 8});
  for (size_t i = 0; i < rows_.size(); ++i) {
    const Row& row = rows_[i];
    float y = top + row.y - scroll_;
    if (y + row.h < top - 4 || y - kHeaderBand > bottom) continue;
    if (!row.header.empty()) ui::label(c, row.header, x, y - kHeaderBand, Weight::Bold, 16, theme::white(0.5f));
    Rect box{x, y, w, row.h};
    // The pointer finds it only where it shows.
    float hitTop = std::max(y, top), hitBottom = std::min(y + row.h, bottom);
    if (hitBottom > hitTop) c.hit({x, hitTop, w, hitBottom - hitTop}, kHitCells + static_cast<int>(i));
    search_view::drawCell(c, row.cell, box, focusable && static_cast<int>(i) == row_, kScale);
  }
  c.g.popClip();
}

}  // namespace marigold::app
