// The guide in the player (Left while watching, Build 116): a card over the
// left two thirds of the picture, which keeps playing on the right with its
// sound. The list the channel playing was picked from (views::playerOpen:
// My Lineup when that list doesn't have the channel playing), 2.5 hours at
// a time, starting on the channel playing (marked WATCHING) at now. Its top
// line names the list and the focused row's neighborhood. Up/Down move
// between channels, Right/Left through the shows (the window slides past
// its edge, up to two days ahead), OK on something on now watches it and
// closes the guide, OK on a later show opens its details, holding OK sets a
// reminder. Up from the top row reaches "Now ▼" and its times. Number keys
// jump to a channel.
//
// Left at the first show at Now opens the list's neighborhoods in a column
// inside the card (the grid pushes right and shows 1.5 hours); Left again
// opens the lists. The key path is core/input/player_rail's: the main
// guide's, with its return rule. Walking the column only scrolls the grid;
// nothing previews and the picture never changes until OK. The LG pointer
// clicks shows, the list name, the column, the lists, "Now ▼" and the edge
// arrows; a click beside the guide closes it.
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app/app.hpp"
#include "app/playback.hpp"
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
namespace views = engine::views;

constexpr Rect kCard{24, 24, 1240, 1032};
constexpr float kLeft = 64;          // inside the card
constexpr float kChannelW = 186;     // number and logo
constexpr float kChannelCol = 196;   // the shows start this far right of the grid's left
constexpr float kPush = left_rail::kColumnW + 32;     // the column open: 272
constexpr float kGridRight = kCard.x + kCard.w - 64;  // room for the "later" arrow
constexpr float kTopY = 52;
constexpr float kRulerY = 120;
constexpr float kGridTop = 164;
constexpr float kRowH = 96, kRowGap = 8;
constexpr int kVisibleRows = 7;
constexpr float kDetailY = kGridTop + kVisibleRows * (kRowH + kRowGap) + 10;
constexpr float kLegendY = kCard.y + kCard.h - 42;
// 2.5 hours at rest, 1.5 with the column open: each minute stays at least
// as wide.
constexpr int kRestMinutes = 150, kColumnMinutes = 90;
constexpr time::Seconds kShift = 90 * 60;        // Right / Left past the edge
constexpr time::Seconds kMaxAhead = 48 * 3600;   // how far later it goes
constexpr float kMinCell = 40;                   // narrower shows are gaps
constexpr float kTextCell = 120;                 // text only from this width
constexpr size_t kGuideRows = 300;               // listings asked for on open
constexpr uint32_t kDigitsMs = 1500;
constexpr uint32_t kNoticeMs = 3500;

// Pointer targets (the column's and the lists' are left_rail::kHit*: the
// card under everything, the dimmed card beside the lists, and from 300).
constexpr int kHitTopLine = 90, kHitTime = 91, kHitEarlier = 93, kHitLater = 94, kHitBeside = 95;
constexpr int kHitLists = 96, kHitTimesBox = 97;
constexpr int kHitTimes = 100;       // + time preset
constexpr int kHitChannel = 1000;    // + row
constexpr int kHitProgram = 100000;  // + row * 64 + show

bool inRange(int id, int base, int n) { return id >= base && id < base + n; }

std::string upper(std::string s) {
  for (char& ch : s) ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
  return s;
}

views::Tab tabOf(rail::Item i) {
  switch (i) {
    case rail::Item::Favorites: return views::Tab::Favorites;
    case rail::Item::Recent: return views::Tab::Recent;
    case rail::Item::All: return views::Tab::All;
    default: return views::Tab::Lineup;
  }
}

rail::Item itemOf(views::Tab t) {
  switch (t) {
    case views::Tab::Favorites: return rail::Item::Favorites;
    case views::Tab::Recent: return rail::Item::Recent;
    case views::Tab::All: return rail::Item::All;
    case views::Tab::Events: return rail::Item::Events;
    case views::Tab::Lineup: break;
  }
  return rail::Item::Lineup;
}

class MiniGuide : public Screen {
 public:
  // `list`: open on that list with its neighborhoods (the games rail's
  // lists picked it); else the list the channel playing came from.
  MiniGuide(App& app, std::optional<rail::Item> list) : Screen(app) {
    windowStart_ = nowWindow();
    focusTime_ = time::now();
    const auto& s = app_.store().settings();
    nav_.setLists(rail::playerLists(s.allChannelsTab, s.scheduleEnabled));
    watching_ = app_.playback().entry();
    if (list && *list != rail::Item::Events) {
      openList(*list);
      settleSlides(app_.ms());
      return;
    }
    rebuild();
    nav_.setList(itemOf(tab_));
    focusPlaying();
    requestListings();
  }

  bool opaque() const override { return false; }
  bool holdsOk() const override {
    return area_ == Area::Grid && nav_.mode() == rail::Mode::Content && pointer_ != kHitBeside;
  }

  // Back from a show's details: closes too if a channel was picked there.
  void shown() override {
    if (app_.playback().entry() != watching_) app_.nav().pop();
  }

  bool key(const KeyEvent& e) override {
    settleSlides(app_.ms());
    // A key that isn't a click: where the pointer last was no longer counts.
    if (e.key != Key::Ok) pointer_ = 0;
    if (e.key == Key::Ok && !e.longPress && pointer_ && click()) return true;
    int d = digitOf(e.key);
    if (d >= 0) {
      // A number acts on the grid: an open column or the lists close first.
      closeRail();
      area_ = Area::Grid;
      if (digits_.size() >= 4) digits_.clear();
      digits_ += static_cast<char>('0' + d);
      digitsAt_ = app_.ms();
      return true;
    }
    if (e.key == Key::Guide) {
      app_.nav().pop();
      return true;
    }
    rail::Mode mode = nav_.mode();
    if (mode == rail::Mode::Content && area_ != Area::Grid) return timeKey(e);
    if (mode != rail::Mode::Content && (e.key == Key::ChannelUp || e.key == Key::ChannelDown)) {
      // A page of channels: the grid as it was, a page on.
      closeRail();
      moveRow(e.key == Key::ChannelUp ? -6 : 6);
      return true;
    }
    apply(nav_.key(e.key, gridState(), e.glide), &e);
    return true;
  }

  void hover(int id) override {
    pointer_ = id;
    // The column and the lists highlight what the pointer is on; the grid
    // behind them takes the focus only on a click.
    if (nav_.mode() != rail::Mode::Content) return;
    if (area_ == Area::Times) {
      if (inRange(id, kHitTimes, static_cast<int>(times_.size()))) time_ = id - kHitTimes;
      return;
    }
    if (id >= kHitProgram) {
      int row = (id - kHitProgram) / 64, show = (id - kHitProgram) % 64;
      if (row >= 0 && row < static_cast<int>(rows_.size())) {
        area_ = Area::Grid;
        focus_ = row;
        const std::vector<Listing>* v = listingsOf(row);
        if (v && show < static_cast<int>(v->size())) focusTime_ = std::max((*v)[static_cast<size_t>(show)].start, windowStart_);
      }
    } else if (id >= kHitChannel) {
      area_ = Area::Grid;
      focus_ = std::clamp(id - kHitChannel, 0, static_cast<int>(rows_.size()) - 1);
    } else if (id == kHitTime) {
      area_ = Area::Time;
    } else if (id == kHitEarlier || id == kHitLater || id == kHitTopLine) {
      area_ = Area::Grid;
    }
  }

  void pointerGone() override { pointer_ = 0; }

  bool tick(uint32_t ms) override {
    if (app_.store().channels()->version != ch_->version) {
      // The list itself changed: an open column or the lists close, the
      // grid back where it was.
      bool column = nav_.mode() == rail::Mode::Column;
      nav_.close();
      rebuild();
      if (column) restorePlace();
      nav_.setList(itemOf(tab_));
      syncSlides();
    }
    // A typed number's pause and a notice's few seconds: asleep until they
    // end (wakeAt()), then drawn once.
    bool changed = wake_.reached(ms);
    wake_.clear();
    if (!digits_.empty() &&
        (ms - digitsAt_ >= kDigitsMs || views::digitsSettled(digits_, *app_.store().channels()))) {
      jumpToNumber();
      changed = true;
    }
    if (!digits_.empty()) wake_.at(digitsAt_ + kDigitsMs);
    if (!notice_.empty() && ms < noticeUntil_) wake_.at(noticeUntil_);
    bool moving = push_.moving(ms) || lists_.moving(ms);
    settleSlides(ms);
    return moving || changed;
  }
  uint32_t wakeAt() const override { return wake_.get(); }
  const char* name() const override { return "mini guide"; }

  void draw(ui::Ctx& c) override {
    li_ = app_.store().listings();
    settleSlides(c.ms);
    rail::Mode mode = nav_.mode();
    ui::floatingCard(c, kCard);
    // Beside the guide: a click there closes it. The card's blank: nothing.
    c.hit({kCard.x + kCard.w, 0, gfx::kLayoutWidth - kCard.x - kCard.w, gfx::kLayoutHeight}, kHitBeside);
    c.hit(kCard, left_rail::kHitNothing);
    drawTop(c);
    drawRuler(c);
    drawGrid(c);
    drawDetail(c);
    float column = push_.at(c.ms);
    if (column > 0.01f) drawColumn(c, column);
    const RemoteKeys& keys = app_.remoteKeys();
    if (mode != rail::Mode::Menu) {
      hints::PlayerGuide place = mode == rail::Mode::Column ? hints::PlayerGuide::Column
                                 : area_ == Area::Times     ? hints::PlayerGuide::Times
                                 : later()                  ? hints::PlayerGuide::Later
                                                            : hints::PlayerGuide::Now;
      std::string legend = hints::playerGuide(keys, place, !sections_.empty(), c.pointerActive);
      ui::label(c, legend, left_, kLegendY, Weight::Regular, 22, theme::white(0.75f), kCard.x + kCard.w - 40 - left_);
    }
    if (area_ == Area::Times && mode == rail::Mode::Content) drawTimes(c);
    if (!digits_.empty()) {
      // In the free top line, over the clock.
      Rect r{kCard.x + kCard.w - 240, kCard.y + 10, 200, 76};
      c.g.fill(r, theme::black(0.8f), 18);
      ui::labelIn(c, digits_, r, Weight::Bold, 46, theme::white(), Align::Center);
    }
    if (!notice_.empty() && app_.ms() < noticeUntil_) {
      float w = std::min(1100.0f, ui::textWidth(c, notice_, Weight::Bold, 34) + 80);
      Rect r{kCard.x + (kCard.w - w) * 0.5f, kDetailY - 110, w, 84};
      c.g.fill(r, theme::hex(0x0C0E12, 0.95f), 24);
      c.g.stroke(r, theme::white(0.18f), 24, 2);
      ui::labelIn(c, notice_, r, Weight::Bold, 34, theme::white(), Align::Center);
    }
    float lists = lists_.at(c.ms);
    if (lists > 0.01f) {
      int hover = c.pointerActive && inRange(pointer_, left_rail::kHitMenu, 100) ? pointer_ - left_rail::kHitMenu : -1;
      bool back = nav_.fromColumn();
      left_rail::lists(c, kCard, nav_.lists(), nav_.list(), nav_.listFocus(), hover,
                       back ? "Back: neighborhoods" : "Back: the grid", lists);
      if (mode == rail::Mode::Menu)
        ui::label(c, hints::playerGuide(keys, hints::PlayerGuide::Lists, back), kCard.x + left_rail::kListsW + 40,
                  kLegendY, Weight::Regular, 22, theme::white(0.75f), kCard.w - left_rail::kListsW - 80);
    }
  }

 private:
  // The column and the lists are the rail's (nav_.mode()); these are the
  // grid's own places: the grid, "Now ▼", and its times open under it.
  enum class Area { Grid, Time, Times };
  struct Section {
    std::string title;
    int row = 0;  // its first channel
  };
  // Where the focus was when the column opened (the return rule).
  struct Place {
    std::string id;
    int row = 0, first = 0;
    time::Seconds window = 0, time = 0;
  };
  struct Preset {
    std::string title;
    time::Seconds at = 0;
  };

  // ---------------------------------------------------------- the list
  const GuideChannel& channel(int row) const { return ch_->all[static_cast<size_t>(rows_[static_cast<size_t>(row)])]; }
  const std::vector<Listing>* listingsOf(int row) const {
    return li_ && row >= 0 && row < static_cast<int>(rows_.size()) ? li_->forChannel(channel(row).id) : nullptr;
  }

  // The rows of the list on screen: the one the channel playing came from
  // (picked_ false), or one picked in the lists, whole.
  void rebuild() {
    std::string keep = focus_ >= 0 && focus_ < static_cast<int>(rows_.size()) ? channel(focus_).id : std::string();
    ch_ = app_.store().channels();
    li_ = app_.store().listings();
    std::vector<std::string> ids;
    if (picked_) {
      ids = views::playerTab(*ch_, tab_);
    } else {
      const Playback& p = app_.playback();
      views::PlayerList open = views::playerOpen(*ch_, p.flipList(), p.entry(), p.pickedTab(), p.pickedFlip());
      tab_ = open.tab;
      ids = std::move(open.ids);
    }
    views::List list = views::playerGrid(*ch_, tab_, ids);
    flip_ = views::flipList(*ch_, list);
    rows_.clear();
    sections_.clear();
    for (const views::Line& l : list.lines) {
      if (l.kind == views::Line::Kind::Header) sections_.push_back({l.title, static_cast<int>(rows_.size())});
      else rows_.push_back(l.channel);
    }
    focus_ = 0;
    for (size_t i = 0; i < rows_.size(); ++i)
      if (channel(static_cast<int>(i)).id == keep) focus_ = static_cast<int>(i);
    if (rows_.empty()) focus_ = -1;
    ensureVisible();
  }

  // The channel playing, at now, when the list has it.
  void focusPlaying() {
    const std::string& playing = app_.playback().entry();
    for (size_t i = 0; i < rows_.size(); ++i)
      if (channel(static_cast<int>(i)).id == playing) focus_ = static_cast<int>(i);
    first_ = 0;
    ensureVisible();
  }

  void requestListings() {
    std::vector<std::string> ids;
    // A long list: from a page above the focus.
    size_t from = static_cast<size_t>(std::max(0, focus_ - kVisibleRows));
    if (rows_.size() <= kGuideRows) from = 0;
    for (size_t i = from; i < rows_.size() && ids.size() < kGuideRows; ++i) ids.push_back(channel(static_cast<int>(i)).id);
    app_.store().wantListings(ids);
  }

  // The neighborhood the focused row is in.
  int originSection() const {
    int origin = 0;
    for (size_t i = 0; i < sections_.size(); ++i)
      if (sections_[i].row <= focus_) origin = static_cast<int>(i);
    return origin;
  }
  const std::string& sectionOf(int row) const {
    static const std::string kNone;
    if (row < 0 || row >= static_cast<int>(rows_.size())) return kNone;
    return channel(row).section;
  }

  // ---------------------------------------------------------- time
  time::Seconds nowWindow() const { return time::now() / 1800 * 1800; }
  int windowMinutes() const { return narrow_ ? kColumnMinutes : kRestMinutes; }
  time::Seconds windowEnd() const { return windowStart_ + windowMinutes() * 60; }
  bool later() const { return windowStart_ > nowWindow(); }
  float progX() const { return left_ + kChannelCol; }
  // While the grid slides it keeps the old scale and shows what fits up to
  // the right edge (drawEnd); the window re-times once the push lands.
  float pxPerSec() const {
    float left = narrow_ ? kLeft + kPush : kLeft;
    return (kGridRight - left - kChannelCol) / (static_cast<float>(windowMinutes()) * 60.0f);
  }
  time::Seconds drawEnd() const {
    return windowStart_ + static_cast<time::Seconds>((kGridRight - progX()) / pxPerSec());
  }
  float xAt(time::Seconds t) const { return progX() + static_cast<float>(t - windowStart_) * pxPerSec(); }
  int programIndex(const std::vector<Listing>& v, time::Seconds t) const {
    return views::programAt(v, t, windowStart_, windowEnd());
  }
  const Listing* focusedListing() const {
    const std::vector<Listing>* v = listingsOf(focus_);
    if (!v || v->empty()) return nullptr;
    int i = programIndex(*v, focusTime_);
    return i < 0 ? nullptr : &(*v)[static_cast<size_t>(i)];
  }
  float visibleWidth(const Listing& l) const {
    time::Seconds a = std::max(l.start, windowStart_), b = std::min(l.end, windowEnd());
    return static_cast<float>(b - a) * pxPerSec();
  }

  void setWindow(time::Seconds start) {
    windowStart_ = std::clamp(start / 1800 * 1800, nowWindow(), nowWindow() + kMaxAhead);
    focusTime_ = windowStart_ == nowWindow() ? time::now() : std::max(start, windowStart_);
    app_.invalidate();
  }
  void returnToNow() { setWindow(time::now()); }
  void shift(time::Seconds by) {
    time::Seconds to = windowStart_ + by;
    if (to < nowWindow()) to = nowWindow();
    if (to > nowWindow() + kMaxAhead) return;
    windowStart_ = to;
    focusTime_ = to == nowWindow() ? time::now() : to;
  }

  void moveRow(int step) {
    if (rows_.empty()) return;
    focus_ = std::clamp(focus_ + step, 0, static_cast<int>(rows_.size()) - 1);
    ensureVisible();
  }
  void ensureVisible() {
    if (focus_ < first_) first_ = focus_;
    if (focus_ >= first_ + kVisibleRows) first_ = focus_ - kVisibleRows + 1;
    first_ = std::clamp(first_, 0, std::max(0, static_cast<int>(rows_.size()) - kVisibleRows));
  }

  // The focus is on the first show in view (nothing earlier to go to at
  // Now: Left there opens the column).
  bool atFirstShow() const {
    const std::vector<Listing>* v = listingsOf(focus_);
    int cur = v && !v->empty() ? programIndex(*v, focusTime_) : -1;
    for (int i = cur - 1; i >= 0; --i) {
      const Listing& p = (*v)[static_cast<size_t>(i)];
      if (p.end <= windowStart_) break;
      if (visibleWidth(p) >= kMinCell) return false;
    }
    return true;
  }
  rail::Grid gridState() const {
    return {atFirstShow(), later(), !digits_.empty(), originSection(), static_cast<int>(sections_.size())};
  }

  // Right / Left: the next or previous show in view; past the edge the
  // window slides 90 minutes (never before now).
  void moveProgram(int step) {
    const std::vector<Listing>* v = listingsOf(focus_);
    int cur = v && !v->empty() ? programIndex(*v, focusTime_) : -1;
    if (step > 0) {
      if (cur >= 0) {
        for (size_t i = static_cast<size_t>(cur) + 1; i < v->size(); ++i) {
          const Listing& n = (*v)[i];
          if (n.start >= windowEnd()) break;
          if (visibleWidth(n) >= kMinCell) {
            focusTime_ = std::max(n.start, windowStart_);
            return;
          }
        }
      }
      if (windowStart_ + kShift <= nowWindow() + kMaxAhead) {
        windowStart_ += kShift;
        if (cur >= 0 && (*v)[static_cast<size_t>(cur)].end > windowStart_) focusTime_ = std::max(focusTime_, windowStart_);
        else focusTime_ = windowStart_;
      }
      return;
    }
    if (cur >= 0) {
      for (int i = cur - 1; i >= 0; --i) {
        const Listing& p = (*v)[static_cast<size_t>(i)];
        if (p.end <= windowStart_) break;
        if (visibleWidth(p) >= kMinCell) {
          focusTime_ = std::max(p.start, windowStart_);
          return;
        }
      }
    }
    if (later()) {
      windowStart_ = std::max(nowWindow(), windowStart_ - kShift);
      focusTime_ = cur >= 0 ? std::max((*v)[static_cast<size_t>(cur)].start, windowStart_) : windowStart_;
      if (windowStart_ == nowWindow()) focusTime_ = std::max(focusTime_, time::now());
    }
  }

  // ---------------------------------------------------------- the rail
  void apply(const rail::PlayerStep& s, const KeyEvent* e = nullptr) {
    using Do = rail::PlayerStep::Do;
    int n = static_cast<int>(sections_.size());
    switch (s.what) {
      case Do::Grid:
        if (e) gridKey(*e);
        return;
      case Do::None: break;
      case Do::Column: keepPlace(); break;
      case Do::Scroll:
        // The grid scrolls behind the column; on the neighborhood it opened
        // on, back where it was (what OK lands on).
        if (s.index == nav_.origin()) restorePlace();
        else if (s.index < n) toSection(s.index);
        break;
      case Do::Restore: restorePlace(); break;
      case Do::Top:
        // Another neighborhood: its first channel, at Now.
        if (s.index < n) {
          windowStart_ = nowWindow();
          focusTime_ = time::now();
          toSection(s.index);
        }
        break;
      case Do::Open: openList(s.item); return;
      case Do::Digits: digits_.clear(); break;
      case Do::Now: returnToNow(); break;
      case Do::Close: app_.nav().pop(); return;
    }
    area_ = Area::Grid;
    syncSlides();
  }

  void keepPlace() {
    place_ = {focus_ >= 0 && focus_ < static_cast<int>(rows_.size()) ? channel(focus_).id : std::string(), focus_, first_,
              windowStart_, focusTime_};
  }
  // Exactly where the focus was when the column opened: row, show, time and
  // scroll (the channel's row again if the list was rebuilt meanwhile).
  void restorePlace() {
    focus_ = std::min(place_.row, static_cast<int>(rows_.size()) - 1);
    for (size_t i = 0; i < rows_.size(); ++i)
      if (channel(static_cast<int>(i)).id == place_.id) focus_ = static_cast<int>(i);
    first_ = place_.first;
    windowStart_ = std::max(place_.window, nowWindow());
    focusTime_ = place_.time;
    ensureVisible();
  }
  // A neighborhood walked to or picked: its first channel, at the top of
  // the grid (as high as the end of the list lets it go).
  void toSection(int index) {
    int row = sections_[static_cast<size_t>(index)].row;
    focus_ = std::min(row, static_cast<int>(rows_.size()) - 1);
    first_ = static_cast<int>(rail::topScroll(static_cast<float>(row), static_cast<float>(rows_.size()), kVisibleRows));
    ensureVisible();
    requestListings();
  }
  // An open column is cancelled, the lists close: the grid as it was.
  void closeRail() {
    if (nav_.mode() == rail::Mode::Column) restorePlace();
    nav_.close();
    syncSlides();
  }

  // A list picked in the lists: its grid at Now on the channel playing
  // (else its first), and its neighborhoods on that channel's (Recent has
  // none: its grid). Live Events: the games rail with its SPORTS column.
  void openList(rail::Item item) {
    if (item == rail::Item::Events) {
      app_.nav().pop();
      app_.nav().push(makeEventGuide(app_, true));
      return;
    }
    nav_.setList(item);
    picked_ = true;
    tab_ = tabOf(item);
    focus_ = -1;
    rebuild();
    windowStart_ = nowWindow();
    focusTime_ = time::now();
    area_ = Area::Grid;
    focusPlaying();
    requestListings();
    keepPlace();
    nav_.openColumn(originSection(), static_cast<int>(sections_.size()));
    syncSlides();
  }

  void syncSlides() {
    uint32_t ms = app_.ms();
    push_.go(nav_.mode() == rail::Mode::Column ? 1.0f : 0.0f, ms);
    lists_.go(nav_.mode() == rail::Mode::Menu ? 1.0f : 0.0f, ms);
    settleSlides(ms);
    app_.invalidate();
  }
  void settleSlides(uint32_t ms) {
    left_ = kLeft + kPush * push_.at(ms);
    if (push_.landed(ms)) narrow_ = push_.target() > 0.5f;
  }

  // A click (OK while the pointer is on something). True when it was taken
  // here; on the grid it is OK on the show hovering focused.
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
    if (mode == rail::Mode::Column) {
      if (inRange(id, left_rail::kHitColumn, static_cast<int>(sections_.size()))) {
        apply(nav_.clickColumn(id - left_rail::kHitColumn));
      } else if (id == kHitLists) {
        apply(nav_.openLists());
      } else if (id >= kHitChannel) {
        // The grid: the column closes on that show.
        nav_.close();
        syncSlides();
        hover(id);
      }
      return true;
    }
    switch (id) {
      case kHitTopLine: apply(nav_.edge(originSection(), static_cast<int>(sections_.size()))); return true;
      case kHitTime:
        openTimes();
        return true;
      case kHitEarlier:
      case kHitLater: shift(id == kHitLater ? kShift : -kShift); return true;
      case kHitTimesBox:
      case left_rail::kHitNothing: return true;  // the card's blank
      default: break;
    }
    if (area_ == Area::Times) {
      if (inRange(id, kHitTimes, static_cast<int>(times_.size()))) pickTime(id - kHitTimes);
      else area_ = Area::Grid;
      return true;
    }
    return false;
  }

  // ---------------------------------------------------------- keys
  void gridKey(const KeyEvent& e) {
    switch (e.key) {
      case Key::Up:
        if (focus_ <= 0) {
          area_ = Area::Time;
          return;
        }
        moveRow(-1);
        return;
      case Key::Down: moveRow(1); return;
      case Key::ChannelUp: moveRow(-6); return;
      case Key::ChannelDown: moveRow(6); return;
      case Key::Right: moveProgram(1); return;
      case Key::Left: moveProgram(-1); return;
      case Key::FastForward: shift(kShift); return;
      case Key::Rewind: shift(-kShift); return;
      case Key::Ok:
        if (e.longPress) remind();
        else select();
        return;
      default: return;
    }
  }

  // "Now ▼" (Up from the top row), and its times under it: Now, Tonight
  // (before 7 PM), Tomorrow morning, as the pills before them.
  bool timeKey(const KeyEvent& e) {
    int n = static_cast<int>(times_.size());
    if (area_ == Area::Time) {
      switch (e.key) {
        case Key::Ok: openTimes(); return true;
        case Key::Down:
        case Key::Back: area_ = Area::Grid; return true;
        default: return true;
      }
    }
    switch (e.key) {
      case Key::Up:
      case Key::Down: time_ = std::clamp(time_ + (e.key == Key::Up ? -1 : 1), 0, n - 1); return true;
      case Key::Ok:
      case Key::Right: pickTime(time_); return true;
      case Key::Back: area_ = Area::Grid; return true;
      default: return true;
    }
  }
  void openTimes() {
    buildTimes();
    area_ = Area::Times;
    time_ = std::max(0, currentTime());
  }
  void pickTime(int i) {
    if (i >= 0 && i < static_cast<int>(times_.size())) setWindow(times_[static_cast<size_t>(i)].at);
    area_ = Area::Grid;
  }
  void buildTimes() {
    times_.clear();
    const time::Zone& z = app_.zone();
    time::Seconds now = time::now();
    time::Civil l = z.toLocal(now);
    auto at = [&](int dayOffset, int hour) {
      time::Civil c = l;
      c.hour = hour;
      c.minute = 0;
      c.second = 0;
      return z.fromLocal(c) + dayOffset * 86400;
    };
    times_.push_back({"Now", now});
    if (l.hour < 19) times_.push_back({"Tonight", at(0, 19)});
    times_.push_back({"Tomorrow", at(1, 8)});
  }
  int currentTime() const {
    if (!later()) return 0;
    for (size_t i = 1; i < times_.size(); ++i)
      if (times_[i].at / 1800 * 1800 == windowStart_) return static_cast<int>(i);
    return -1;
  }

  // ---------------------------------------------------------- actions
  void select() {
    if (focus_ < 0 || rows_.empty()) return;
    const GuideChannel& g = channel(focus_);
    const Listing* l = focusedListing();
    Playback& p = app_.playback();
    // A show on now (or starting within a minute) plays; a later one opens
    // its details (Remind Me, Watch, other airings).
    if (!l || l->start <= time::now() + 60) {
      // CH+ / CH- then go through this list (the one playing too: a list
      // picked here takes over), and the guide opens on it again.
      if (!p.tune(g.id, flip_)) {
        notice("This channel has no working feed right now");
        return;
      }
      p.pickedFrom(tab_);
      app_.nav().pop();
      return;
    }
    app_.nav().push(makeProgramDetail(app_, g.id, *l));
  }
  // Holding OK on a later show: its reminder, on or off.
  void remind() {
    const Listing* l = focusedListing();
    if (focus_ < 0 || !l || l->start <= time::now() + 60) {
      select();
      return;
    }
    bool on = app_.store().toggleShowReminder(channel(focus_).id, *l);
    notice(on ? "Reminder set \xc2\xb7 hold OK again to remove it" : "Reminder removed");
  }
  void notice(std::string text) {
    notice_ = std::move(text);
    noticeUntil_ = app_.ms() + kNoticeMs;
  }

  void jumpToNumber() {
    int n = std::atoi(digits_.c_str());
    digits_.clear();
    for (size_t i = 0; i < rows_.size(); ++i) {
      const GuideChannel& g = channel(static_cast<int>(i));
      if (g.number && *g.number == n) {
        focus_ = static_cast<int>(i);
        area_ = Area::Grid;
        ensureVisible();
        return;
      }
    }
    notice("No channel " + std::to_string(n) + " in this list");
  }

  // ---------------------------------------------------------- drawing
  // The list and the focused row's neighborhood, the clock at the right.
  // With the pointer about, the list name is a button (the column, as Left
  // at the first show).
  void drawTop(ui::Ctx& c) {
    std::string text = upper(rail::title(itemOf(tab_)));
    const std::string& section = sectionOf(focus_);
    if (tab_ != views::Tab::Recent && !section.empty()) text += " \xc2\xb7 " + upper(section);
    float right = kCard.x + kCard.w - 40;
    float maxW = right - 240 - left_;
    if (c.pointerActive && nav_.mode() == rail::Mode::Content) {
      const char* arrow = "\xe2\x97\x80";
      float aw = ui::textWidth(c, arrow, Weight::Bold, 16);
      float w = std::min(maxW, 16 + aw + 10 + ui::textWidth(c, text, Weight::Bold, 22) + 20);
      Rect r{left_ - 16, kTopY - 8, w, 50};
      bool on = pointer_ == kHitTopLine;
      c.hit(r, kHitTopLine);
      if (on) c.g.fill(r, theme::white(), 25);
      else c.g.stroke(r, theme::white(0.35f), 25, 2);
      Color col = on ? theme::black() : theme::white(0.85f);
      ui::labelIn(c, arrow, {left_, r.y, aw, r.h}, Weight::Bold, 16, col);
      ui::labelIn(c, text, {left_ + aw + 10, r.y, w - aw - 36, r.h}, Weight::Bold, 22, col);
    } else {
      ui::labelIn(c, text, {left_, kTopY, maxW, 34}, Weight::Bold, 22, theme::white(0.7f));
    }
    if (digits_.empty())
      ui::labelIn(c, ui::clock(c, c.now), {right - 200, kTopY - 4, 200, 40}, Weight::Medium, 26, theme::white(0.7f),
                  Align::Right);
  }

  void drawRuler(ui::Ctx& c) {
    // "Now ▼" (the day when later), as in the main guide.
    std::string word = "Now";
    if (later()) {
      word = ui::dayWord(c, windowStart_);
      if (word.empty()) word = "Today";
    }
    word += " \xe2\x96\xbc";
    bool focused = area_ != Area::Grid && nav_.mode() == rail::Mode::Content;
    Rect r{left_, kRulerY - 2, std::max(122.0f, ui::chipWidth(c, word, 21) + 16), 42};
    c.hit(r, kHitTime);
    c.g.fill(r, focused ? theme::white() : theme::white(0.1f), 21);
    ui::labelIn(c, word, r, Weight::Bold, 21, focused ? theme::black() : theme::white(), Align::Center);
    for (time::Seconds t = windowStart_; xAt(t) + 60 < kGridRight; t += 1800)
      ui::label(c, ui::clock(c, t, false), xAt(t) + 8, kRulerY + 4, Weight::Medium, 22, theme::white(0.8f));
    c.g.fill({progX(), kRulerY + 36, kGridRight - progX(), 1}, theme::white(0.15f));
    if (later()) {
      Rect earlierR{std::max(progX() - 52, r.x + r.w + 8), kRulerY - 4, 44, 44};
      bool on = c.pointerActive && pointer_ == kHitEarlier;
      c.hit(earlierR, kHitEarlier);
      c.g.fill(earlierR, on ? theme::white() : theme::white(0.12f), 12);
      ui::labelIn(c, "\xe2\x97\x80", earlierR, Weight::Bold, 22, on ? theme::black() : theme::white(), Align::Center);
    }
  }

  // The times under "Now ▼".
  void drawTimes(ui::Ctx& c) {
    int n = static_cast<int>(times_.size());
    Rect box{left_ - 6, kRulerY + 50, 300, 26 + 64.0f * static_cast<float>(n)};
    c.hit(box, kHitTimesBox);
    c.g.fill(box, theme::hex(0x0C0E12), 22);
    c.g.stroke(box, theme::white(0.16f), 22, 2);
    int current = currentTime();
    for (int i = 0; i < n; ++i) {
      Rect r{box.x + 10, box.y + 13 + 64.0f * static_cast<float>(i), box.w - 20, 56};
      bool f = i == time_;
      c.hit(r, kHitTimes + i);
      if (f) c.g.fill(r, theme::white(), 28);
      else if (c.pointerActive && pointer_ == kHitTimes + i) c.g.fill(r, theme::white(0.12f), 28);
      const Preset& p = times_[static_cast<size_t>(i)];
      std::string text = i == 0 ? p.title : p.title + " " + ui::clock(c, p.at);
      Color col = f ? theme::black() : theme::white();
      ui::labelIn(c, text, {r.x + 20, r.y, r.w - 70, r.h}, Weight::Bold, 24, col);
      if (i == current) ui::labelIn(c, "\xe2\x9c\x93", {r.x + r.w - 46, r.y, 30, r.h}, Weight::Bold, 24, col, Align::Center);
    }
  }

  void drawGrid(ui::Ctx& c) {
    if (rows_.empty()) {
      ui::label(c, "No channels in this list.", left_, kGridTop + 30, Weight::Medium, 26, theme::white(0.7f));
      return;
    }
    bool active = area_ == Area::Grid && nav_.mode() == rail::Mode::Content;
    const std::string& playing = app_.playback().entry();
    time::Seconds ws = windowStart_, we = drawEnd(), now = c.now;
    for (int k = 0; k < kVisibleRows; ++k) {
      int row = first_ + k;
      if (row >= static_cast<int>(rows_.size())) break;
      float y = kGridTop + static_cast<float>(k) * (kRowH + kRowGap);
      const GuideChannel& g = channel(row);
      bool rowFocused = active && row == focus_;
      // Number and logo.
      Rect cell{left_, y, kChannelW, kRowH};
      c.hit(cell, kHitChannel + row);
      // Focused: the same fill and a 3 px white outline, so the logo on it
      // looks the same focused or not (the dense column does not lift).
      c.g.fill(cell, theme::hex(0x262A32), 14);
      if (rowFocused) c.g.stroke(cell, theme::white(), 14, 3);
      if (g.number) ui::label(c, std::to_string(*g.number), cell.x + 14, cell.y + 32, Weight::Bold, 24, theme::white(), 60);
      ui::logo(c, g.logoUrl, g.name, {cell.x + 74, cell.y + 18, 100, 60});
      const std::vector<Listing>* v = listingsOf(row);
      int focusedProgram = rowFocused && v ? programIndex(*v, focusTime_) : -1;
      bool drewAny = false;
      if (v) {
        auto it = std::upper_bound(v->begin(), v->end(), ws, [](time::Seconds t, const Listing& l) { return t < l.end; });
        for (; it != v->end() && it->start < we; ++it) {
          const Listing& l = *it;
          float x0 = std::round(xAt(std::max(l.start, ws)));
          float x1 = std::round(xAt(std::min(l.end, we)));
          if (x1 - x0 < kMinCell) continue;
          drewAny = true;
          int idx = static_cast<int>(it - v->begin());
          bool focused = idx == focusedProgram;
          Rect r{x0, y, x1 - x0 - 8, kRowH};
          c.hit(r, kHitProgram + row * 64 + std::min(idx, 63));
          drawShow(c, r, l, focused, g.id == playing && l.isOn(now), l.isOn(now), l.start < ws,
                   app_.store().hasShowReminder(g.id, l.start));
        }
      }
      if (!drewAny) {
        Rect r{progX(), y, kGridRight - progX() - 8, kRowH};
        c.hit(r, kHitProgram + row * 64);
        c.g.fill(r, rowFocused ? theme::white() : theme::hex(0x262A32), 12);
        std::string text = g.id == playing ? "WATCHING \xc2\xb7 " + g.name : g.name;
        ui::labelIn(c, text, {r.x + 18, r.y, r.w - 36, r.h}, Weight::Bold, 26, rowFocused ? theme::black() : theme::white());
      }
    }
    // The now line, and the arrow for later.
    if (now >= ws && now < we) {
      float x = std::round(xAt(now));
      c.g.fill({x - 1.5f, kRulerY + 36, 3, kVisibleRows * (kRowH + kRowGap)}, theme::hex(theme::kNowLine));
    }
    Rect laterR{kGridRight + 6, kGridTop + kRowH + kRowGap, 44, kRowH};
    bool on = c.pointerActive && pointer_ == kHitLater;
    c.hit(laterR, kHitLater);
    c.g.fill(laterR, on ? theme::white() : theme::white(0.12f), 12);
    ui::labelIn(c, "\xe2\x96\xb6", laterR, Weight::Bold, 24, on ? theme::black() : theme::white(), Align::Center);
  }

  void drawShow(ui::Ctx& c, Rect r, const Listing& l, bool focused, bool watching, bool onNow, bool startedBefore,
                bool reminder) {
    if (focused) {
      c.g.fill(r, theme::white(), 12);
      c.g.stroke({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, theme::white(), 16, 3);
    } else {
      c.g.fill(r, onNow ? theme::hex(0x30353F) : theme::hex(0x262A32), 12);
    }
    if (r.w < kTextCell) return;
    Color fg = focused ? theme::black() : theme::white();
    Color sub = focused ? theme::black(0.75f) : theme::white(0.8f);
    std::string title = (startedBefore ? "\xe2\x97\x80 " : "") + l.title;
    ui::label(c, title, r.x + 16, r.y + 14, Weight::Bold, 27, fg, r.w - 32);
    float sx = r.x + 16;
    if (watching) {
      sx += ui::label(c, "WATCHING", sx, r.y + 56, Weight::Bold, 18, focused ? theme::hex(0xC62828) : theme::hex(theme::kWatching)) + 10;
    }
    if (reminder) {
      sx += ui::chip(c, "REMINDER", sx, r.y + 56, 15, theme::hex(theme::kStar), theme::black()) + 10;
    }
    std::string second = !l.episode.empty() ? l.episode : ui::range(c, l.start, l.end);
    ui::label(c, second, sx, r.y + 54, Weight::Regular, 21, sub, r.x + r.w - 16 - sx);
  }

  // The focused show: title, channel and time, and a line about it. With
  // the column open, dimmed, with what OK there lands on.
  void drawDetail(ui::Ctx& c) {
    float w = kCard.x + kCard.w - 40 - left_;
    c.g.fill({left_, kDetailY - 6, w, 1}, theme::white(0.12f));
    if (focus_ < 0 || rows_.empty()) return;
    bool column = nav_.mode() == rail::Mode::Column;
    float a = column ? 0.75f : 1.0f;
    const GuideChannel& g = channel(focus_);
    const Listing* l = focusedListing();
    std::string note;
    if (column) {
      int i = nav_.index();
      note = i == nav_.origin() || i >= static_cast<int>(sections_.size())
                 ? std::string(" \xc2\xb7 OK: back where you were")
                 : " \xc2\xb7 OK: " + sections_[static_cast<size_t>(i)].title + ", from the top";
    }
    std::string title = l ? l->title : g.name;
    float tw = ui::label(c, title, left_, kDetailY + 8, Weight::Bold, 30, theme::white(a), w);
    if (!note.empty() && tw + 40 < w)
      ui::label(c, note, left_ + tw, kDetailY + 16, Weight::Medium, 22, theme::white(0.7f * a), w - tw);
    if (!l) {
      ui::label(c, "No guide info for this time", left_, kDetailY + 50, Weight::Regular, 22, theme::white(0.75f * a), w);
      return;
    }
    std::string day = ui::dayWord(c, l->start);
    std::string line = g.name + " \xc2\xb7 " + (day.empty() ? std::string() : day + " ") + ui::range(c, l->start, l->end, true);
    if (!l->episode.empty()) line += " \xc2\xb7 " + l->episode;
    if (app_.store().hasShowReminder(g.id, l->start)) line += " \xc2\xb7 Reminder set";
    ui::label(c, line, left_, kDetailY + 50, Weight::Regular, 22, theme::white(0.8f * a), w);
    if (!l->overview.empty())
      ui::label(c, l->overview.string(), left_, kDetailY + 82, Weight::Regular, 21, theme::white(0.65f * a), w);
  }

  // The list's neighborhoods inside the card, the main guide's column in
  // the card's own colour, with "◀ Lists" in its header.
  void drawColumn(ui::Ctx& c, float alpha) {
    std::vector<left_rail::Entry> entries;
    entries.reserve(sections_.size());
    for (const Section& s : sections_) entries.push_back({s.title, 0});
    int hover = c.pointerActive && inRange(pointer_, left_rail::kHitColumn, 100) ? pointer_ - left_rail::kHitColumn : -1;
    left_rail::column(c, rail::title(nav_.list()), "", entries, nav_.index(), nav_.origin(), hover, kRulerY, kGridTop,
                      alpha, kLeft);
    left_rail::listsButton(c, kLeft, kRulerY, c.pointerActive && pointer_ == kHitLists, alpha, kHitLists);
  }

  std::shared_ptr<const Channels> ch_;
  std::shared_ptr<const Listings> li_;
  std::vector<int> rows_;  // indexes into Channels::all
  std::vector<Section> sections_;
  std::vector<std::string> flip_;  // their entries, the flip list a pick plays with
  views::Tab tab_ = views::Tab::Lineup;
  bool picked_ = false;  // a list picked in the lists (not the one playing came from)
  rail::PlayerNav nav_;
  Place place_;
  left_rail::Slide push_, lists_;  // the column's push, the lists' slide
  float left_ = kLeft;             // the grid's left edge (pushed by the column)
  bool narrow_ = false;            // the 1.5 hour window (column open)
  Area area_ = Area::Grid;
  std::vector<Preset> times_;
  int time_ = 0;
  int focus_ = -1, first_ = 0;
  int pointer_ = 0;  // what the pointer is on (a hit id) while it shows
  time::Seconds windowStart_ = 0, focusTime_ = 0;
  std::string watching_;  // the channel playing when the guide opened
  std::string digits_;
  uint32_t digitsAt_ = 0;
  std::string notice_;
  uint32_t noticeUntil_ = 0;
  wake::Due wake_;  // when tick() next has something to show
};

}  // namespace

std::unique_ptr<Screen> makeMiniGuide(App& app, std::optional<rail::Item> list) {
  return std::make_unique<MiniGuide>(app, list);
}

}  // namespace marigold::app
