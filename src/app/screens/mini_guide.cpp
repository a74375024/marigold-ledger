// The guide in the player (Left while watching): a grid over the left two
// thirds of the picture, which keeps playing on the right with its sound.
// The channels of the list being watched, 2.5 hours at a time, starting on
// the channel playing (marked WATCHING) at now. Up/Down move between
// channels, Right/Left through the shows (the window slides past its edge,
// up to two days ahead), OK on something on now watches it and closes the
// guide, OK on a later show opens its details, holding OK sets a reminder.
// Up from the top row reaches the tabs: Now, Tonight, Tomorrow, and the
// list's categories (jumping to them). Number keys jump to a channel. Back
// from a later time returns to now; from now it closes. Nothing previews by
// itself. The LG pointer clicks shows, tabs and the edge arrows; a click
// beside the guide closes it.
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app/app.hpp"
#include "app/playback.hpp"
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

constexpr Rect kCard{24, 24, 1240, 1032};
constexpr float kLeft = 64;            // inside the card
constexpr float kChannelW = 186;       // number and logo
constexpr float kProgX = kLeft + 196;  // the shows start here
constexpr float kGridRight = kCard.x + kCard.w - 64;  // room for the "later" arrow
constexpr float kGridW = kGridRight - kProgX;
constexpr float kTabsY = 48;
constexpr float kRulerY = 120;
constexpr float kGridTop = 164;
constexpr float kRowH = 96, kRowGap = 8;
constexpr int kVisibleRows = 7;
constexpr float kDetailY = kGridTop + kVisibleRows * (kRowH + kRowGap) + 10;
constexpr int kWindowMinutes = 150;
constexpr time::Seconds kShift = 90 * 60;        // Right / Left past the edge
constexpr time::Seconds kMaxAhead = 48 * 3600;   // how far later it goes
constexpr float kMinCell = 40;                   // narrower shows are gaps
constexpr float kTextCell = 120;                 // text only from this width
constexpr size_t kGuideRows = 300;               // listings asked for on open
constexpr uint32_t kDigitsMs = 1500;
constexpr uint32_t kNoticeMs = 3500;

// Pointer targets.
constexpr int kHitTab = 10;          // + tab
constexpr int kHitEarlier = 90, kHitLater = 91, kHitOutside = 92;
constexpr int kHitChannel = 1000;    // + row
constexpr int kHitProgram = 100000;  // + row * 64 + show

class MiniGuide : public Screen {
 public:
  explicit MiniGuide(App& app) : Screen(app) {
    windowStart_ = nowWindow();
    focusTime_ = time::now();
    rebuild();
    // Start on the channel being watched.
    const std::string& playing = app_.playback().entry();
    for (size_t i = 0; i < rows_.size(); ++i)
      if (channel(static_cast<int>(i)).id == playing) focus_ = static_cast<int>(i);
    ensureVisible();
    std::vector<std::string> ids;
    for (size_t i = 0; i < rows_.size() && ids.size() < kGuideRows; ++i) ids.push_back(channel(static_cast<int>(i)).id);
    app_.store().wantListings(ids);
    watching_ = playing;
  }

  bool opaque() const override { return false; }
  bool holdsOk() const override { return area_ == Area::Grid && !outside_; }

  // Back from a show's details: closes too if a channel was picked there.
  void shown() override {
    if (app_.playback().entry() != watching_) app_.nav().pop();
  }

  bool key(const KeyEvent& e) override {
    // A key that isn't a click: where the pointer last was no longer counts.
    if (e.key != Key::Ok) {
      outside_ = false;
      arrow_ = 0;
    }
    int d = digitOf(e.key);
    if (d >= 0) {
      if (digits_.size() >= 4) digits_.clear();
      digits_ += static_cast<char>('0' + d);
      digitsAt_ = app_.ms();
      return true;
    }
    if (area_ == Area::Tabs) return tabsKey(e);
    if (e.key == Key::Ok && outside_) {
      app_.nav().pop();  // a click beside the guide
      return true;
    }
    switch (e.key) {
      case Key::Up:
        if (focus_ <= 0) {
          area_ = Area::Tabs;
          return true;
        }
        moveRow(-1);
        return true;
      case Key::Down: moveRow(1); return true;
      case Key::ChannelUp: moveRow(-6); return true;
      case Key::ChannelDown: moveRow(6); return true;
      case Key::Right: moveProgram(1); return true;
      case Key::Left: moveProgram(-1); return true;
      case Key::FastForward: shift(kShift); return true;
      case Key::Rewind: shift(-kShift); return true;
      case Key::Ok:
        if (e.longPress) remind();
        else select();
        return true;
      case Key::Guide:
        app_.nav().pop();
        return true;
      case Key::Back:
        if (!digits_.empty()) {
          digits_.clear();
          return true;
        }
        if (later()) {
          returnToNow();
          return true;
        }
        app_.nav().pop();
        return true;
      default:
        return true;
    }
  }

  void hover(int id) override {
    outside_ = id == kHitOutside;
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
    } else if (id >= kHitTab && id < kHitTab + static_cast<int>(tabs_.size())) {
      area_ = Area::Tabs;
      tab_ = id - kHitTab;
    } else if (id == kHitEarlier || id == kHitLater) {
      area_ = Area::Grid;
      arrow_ = id;
    }
    if (id != kHitEarlier && id != kHitLater) arrow_ = 0;
  }

  bool tick(uint32_t ms) override {
    if (app_.store().channels()->version != ch_->version) rebuild();
    if (!digits_.empty() &&
        (ms - digitsAt_ >= kDigitsMs || engine::views::digitsSettled(digits_, *app_.store().channels())))
      jumpToNumber();
    return !digits_.empty() || (!notice_.empty() && ms < noticeUntil_ + 50);
  }

  void draw(ui::Ctx& c) override {
    li_ = app_.store().listings();
    ui::floatingCard(c, kCard);
    // Beside the guide: a click there closes it.
    c.hit({kCard.x + kCard.w, 0, gfx::kLayoutWidth - kCard.x - kCard.w, gfx::kLayoutHeight}, kHitOutside);
    drawTabs(c);
    drawRuler(c);
    drawGrid(c);
    drawDetail(c);
    std::string legend = later() ? "\xe2\x96\xb2\xe2\x96\xbc channels \xc2\xb7 \xe2\x97\x80\xe2\x96\xb6 earlier and later "
                                   "\xc2\xb7 OK details \xc2\xb7 Back to now"
                                 : "\xe2\x96\xb2\xe2\x96\xbc channels \xc2\xb7 \xe2\x96\xb6 later \xc2\xb7 OK watch "
                                   "\xc2\xb7 Back closes";
    const RemoteKeys& keys = app_.remoteKeys();
    if (keys.digits) legend += " \xc2\xb7 123 jumps to a channel";
    ui::label(c, legend, kLeft, kCard.y + kCard.h - 50, Weight::Regular, 22, theme::white(0.75f), kCard.w - 80);
    if (!digits_.empty()) {
      Rect r{kCard.x + kCard.w - 260, kCard.y + 30, 200, 84};
      c.g.fill(r, theme::black(0.8f), 18);
      ui::labelIn(c, digits_, r, Weight::Bold, 50, theme::white(), Align::Center);
    }
    if (!notice_.empty() && app_.ms() < noticeUntil_) {
      float w = std::min(1100.0f, ui::textWidth(c, notice_, Weight::Bold, 34) + 80);
      Rect r{kCard.x + (kCard.w - w) * 0.5f, kDetailY - 110, w, 84};
      c.g.fill(r, theme::hex(0x0C0E12, 0.95f), 24);
      c.g.stroke(r, theme::white(0.18f), 24, 2);
      ui::labelIn(c, notice_, r, Weight::Bold, 34, theme::white(), Align::Center);
    }
  }

 private:
  enum class Area { Grid, Tabs };
  struct Tab {
    std::string title;
    time::Seconds at = 0;  // a time preset (0: a category)
    int row = -1;          // a category's first channel
  };

  // ---------------------------------------------------------- the list
  const GuideChannel& channel(int row) const { return ch_->all[static_cast<size_t>(rows_[static_cast<size_t>(row)])]; }
  const std::vector<Listing>* listingsOf(int row) const {
    return li_ && row >= 0 && row < static_cast<int>(rows_.size()) ? li_->forChannel(channel(row).id) : nullptr;
  }

  void rebuild() {
    std::string keep = focus_ >= 0 && focus_ < static_cast<int>(rows_.size()) ? channel(focus_).id : std::string();
    ch_ = app_.store().channels();
    li_ = app_.store().listings();
    rows_.clear();
    // The list the channel was picked from; the whole lineup when there is none.
    std::vector<std::string> ids = app_.playback().flipList();
    if (ids.empty()) {
      const std::vector<int>& list = ch_->lineupConfigured ? ch_->lineup : ch_->browse;
      for (int i : list) ids.push_back(ch_->all[static_cast<size_t>(i)].id);
    }
    engine::views::List list = engine::views::grouped(*ch_, ids);
    std::vector<std::pair<std::string, int>> sections;
    for (const engine::views::Line& l : list.lines) {
      if (l.kind == engine::views::Line::Kind::Header) sections.push_back({l.title, static_cast<int>(rows_.size())});
      else rows_.push_back(l.channel);
    }
    buildTabs(sections);
    focus_ = 0;
    for (size_t i = 0; i < rows_.size(); ++i)
      if (channel(static_cast<int>(i)).id == keep) focus_ = static_cast<int>(i);
    if (rows_.empty()) focus_ = -1;
    ensureVisible();
  }

  void buildTabs(const std::vector<std::pair<std::string, int>>& sections) {
    tabs_.clear();
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
    tabs_.push_back({"Now", now, -1});
    if (l.hour < 19) tabs_.push_back({"Tonight", at(0, 19), -1});
    tabs_.push_back({"Tomorrow", at(1, 8), -1});
    for (const auto& [title, row] : sections)
      if (!title.empty() && row < static_cast<int>(rows_.size())) tabs_.push_back({title, 0, row});
    tab_ = std::clamp(tab_, 0, static_cast<int>(tabs_.size()) - 1);
  }

  // ---------------------------------------------------------- time
  time::Seconds nowWindow() const { return time::now() / 1800 * 1800; }
  time::Seconds windowEnd() const { return windowStart_ + kWindowMinutes * 60; }
  bool later() const { return windowStart_ > nowWindow(); }
  float xAt(time::Seconds t) const {
    return kProgX + static_cast<float>(t - windowStart_) * kGridW / (kWindowMinutes * 60.0f);
  }
  int programIndex(const std::vector<Listing>& v, time::Seconds t) const {
    return engine::views::programAt(v, t, windowStart_, windowEnd());
  }
  const Listing* focusedListing() const {
    const std::vector<Listing>* v = listingsOf(focus_);
    if (!v || v->empty()) return nullptr;
    int i = programIndex(*v, focusTime_);
    return i < 0 ? nullptr : &(*v)[static_cast<size_t>(i)];
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

  // Right / Left: the next or previous show in view; past the edge the
  // window slides 90 minutes (never before now).
  void moveProgram(int step) {
    const std::vector<Listing>* v = listingsOf(focus_);
    int cur = v && !v->empty() ? programIndex(*v, focusTime_) : -1;
    float pxPerSec = kGridW / (kWindowMinutes * 60.0f);
    auto visibleWidth = [&](const Listing& l) {
      time::Seconds a = std::max(l.start, windowStart_), b = std::min(l.end, windowEnd());
      return static_cast<float>(b - a) * pxPerSec;
    };
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
    // At now, Left stays: the guide closes with Back.
  }

  // ---------------------------------------------------------- actions
  void select() {
    if (arrow_) {
      shift(arrow_ == kHitLater ? kShift : -kShift);
      return;
    }
    if (focus_ < 0 || rows_.empty()) return;
    const GuideChannel& g = channel(focus_);
    const Listing* l = focusedListing();
    Playback& p = app_.playback();
    // A show on now (or starting within a minute) plays; a later one opens
    // its details (Remind Me, Watch, other airings).
    if (!l || l->start <= time::now() + 60) {
      if (g.id == p.entry()) {
        app_.nav().pop();
        return;
      }
      if (!p.tune(g.id)) {
        notice("This channel has no working feed right now");
        return;
      }
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

  bool tabsKey(const KeyEvent& e) {
    switch (e.key) {
      case Key::Left: tab_ = std::max(0, tab_ - 1); return true;
      case Key::Right: tab_ = std::min(static_cast<int>(tabs_.size()) - 1, tab_ + 1); return true;
      case Key::Down:
      case Key::Back:
        area_ = Area::Grid;
        return true;
      case Key::Ok: {
        const Tab& t = tabs_[static_cast<size_t>(tab_)];
        if (t.row >= 0) {
          focus_ = t.row;
          first_ = t.row;
          ensureVisible();
        } else {
          setWindow(t.at);
        }
        area_ = Area::Grid;
        return true;
      }
      case Key::Guide:
        app_.nav().pop();
        return true;
      default:
        return true;
    }
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
  void drawTabs(ui::Ctx& c) {
    ui::label(c, "LIVE TV \xc2\xb7 " + ui::clock(c, c.now), kLeft, kTabsY + 14, Weight::Bold, 21, theme::white(0.7f));
    float x0 = kLeft + 230, right = kCard.x + kCard.w - 40;
    // Keep the focused tab in view: start late enough that it fits.
    std::vector<float> widths;
    for (const Tab& t : tabs_) widths.push_back(ui::chipWidth(c, t.title, 23) + 28);
    int firstTab = 0;
    auto fits = [&](int from) {
      float x = x0;
      for (int i = from; i <= tab_; ++i) x += widths[static_cast<size_t>(i)] + 10;
      return x <= right;
    };
    while (firstTab < tab_ && !fits(firstTab)) ++firstTab;
    float x = x0;
    bool currentTime = !later();
    for (size_t i = static_cast<size_t>(firstTab); i < tabs_.size(); ++i) {
      float w = widths[i];
      if (x + w > right) break;
      Rect r{x, kTabsY, w, 52};
      c.hit(r, kHitTab + static_cast<int>(i));
      bool f = area_ == Area::Tabs && static_cast<int>(i) == tab_;
      bool on = i == 0 && currentTime;
      c.g.fill(r, f ? theme::white() : on ? theme::white(0.24f) : theme::white(0.1f), 26);
      ui::labelIn(c, tabs_[i].title, r, Weight::Bold, 23, f ? theme::black() : theme::white(), Align::Center);
      x += w + 10;
    }
  }

  void drawRuler(ui::Ctx& c) {
    if (later()) {
      std::string day = ui::dayWord(c, windowStart_);
      ui::label(c, day.empty() ? std::string("Today") : day, kLeft, kRulerY + 4, Weight::Bold, 22, theme::white(0.8f));
    }
    for (int i = 0; i < kWindowMinutes / 30; ++i) {
      time::Seconds t = windowStart_ + i * 1800;
      ui::label(c, ui::clock(c, t, false), xAt(t) + 8, kRulerY + 4, Weight::Medium, 22, theme::white(0.8f));
    }
    c.g.fill({kProgX, kRulerY + 36, kGridW, 1}, theme::white(0.15f));
  }

  void drawGrid(ui::Ctx& c) {
    if (rows_.empty()) {
      ui::label(c, "No channels in this list.", kLeft, kGridTop + 30, Weight::Medium, 26, theme::white(0.7f));
      return;
    }
    const std::string& playing = app_.playback().entry();
    time::Seconds ws = windowStart_, we = windowEnd(), now = c.now;
    for (int k = 0; k < kVisibleRows; ++k) {
      int row = first_ + k;
      if (row >= static_cast<int>(rows_.size())) break;
      float y = kGridTop + static_cast<float>(k) * (kRowH + kRowGap);
      const GuideChannel& g = channel(row);
      bool rowFocused = area_ == Area::Grid && row == focus_;
      // Number and logo.
      Rect cell{kLeft, y, kChannelW, kRowH};
      c.hit(cell, kHitChannel + row);
      c.g.fill(cell, rowFocused ? theme::white(0.2f) : theme::hex(0x262A32), 14);
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
        Rect r{kProgX, y, kGridW - 8, kRowH};
        c.hit(r, kHitProgram + row * 64);
        bool focused = rowFocused;
        c.g.fill(r, focused ? theme::white() : theme::hex(0x262A32), 12);
        std::string text = g.id == playing ? "WATCHING \xc2\xb7 " + g.name : g.name;
        ui::labelIn(c, text, {r.x + 18, r.y, r.w - 36, r.h}, Weight::Bold, 26, focused ? theme::black() : theme::white());
      }
    }
    // The now line, and the arrows for earlier and later.
    if (now >= ws && now < we) {
      float x = std::round(xAt(now));
      c.g.fill({x - 1.5f, kRulerY + 36, 3, kVisibleRows * (kRowH + kRowGap)}, theme::hex(theme::kNowLine));
    }
    Rect laterR{kGridRight + 6, kGridTop + kRowH + kRowGap, 44, kRowH};
    c.hit(laterR, kHitLater);
    c.g.fill(laterR, arrow_ == kHitLater ? theme::white() : theme::white(0.12f), 12);
    ui::labelIn(c, "\xe2\x96\xb6", laterR, Weight::Bold, 24, arrow_ == kHitLater ? theme::black() : theme::white(), Align::Center);
    if (later()) {
      Rect earlierR{kProgX - 52, kRulerY - 4, 44, 44};
      c.hit(earlierR, kHitEarlier);
      c.g.fill(earlierR, arrow_ == kHitEarlier ? theme::white() : theme::white(0.12f), 12);
      ui::labelIn(c, "\xe2\x97\x80", earlierR, Weight::Bold, 22, arrow_ == kHitEarlier ? theme::black() : theme::white(),
                  Align::Center);
    }
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

  // The focused show: title, channel and time, and a line about it.
  void drawDetail(ui::Ctx& c) {
    c.g.fill({kLeft, kDetailY - 6, kCard.w - 80, 1}, theme::white(0.12f));
    if (focus_ < 0 || rows_.empty()) return;
    const GuideChannel& g = channel(focus_);
    const Listing* l = focusedListing();
    float w = kCard.w - 80;
    if (!l) {
      ui::label(c, g.name, kLeft, kDetailY + 8, Weight::Bold, 30, theme::white(), w);
      ui::label(c, "No guide info for this time", kLeft, kDetailY + 50, Weight::Regular, 22, theme::white(0.75f), w);
      return;
    }
    ui::label(c, l->title, kLeft, kDetailY + 8, Weight::Bold, 30, theme::white(), w);
    std::string day = ui::dayWord(c, l->start);
    std::string line = g.name + " \xc2\xb7 " + (day.empty() ? std::string() : day + " ") + ui::range(c, l->start, l->end, true);
    if (!l->episode.empty()) line += " \xc2\xb7 " + l->episode;
    if (app_.store().hasShowReminder(g.id, l->start)) line += " \xc2\xb7 Reminder set";
    ui::label(c, line, kLeft, kDetailY + 50, Weight::Regular, 22, theme::white(0.8f), w);
    if (!l->overview.empty())
      ui::label(c, l->overview, kLeft, kDetailY + 82, Weight::Regular, 21, theme::white(0.65f), w);
  }

  std::shared_ptr<const Channels> ch_;
  std::shared_ptr<const Listings> li_;
  std::vector<int> rows_;  // indexes into Channels::all
  std::vector<Tab> tabs_;
  Area area_ = Area::Grid;
  int tab_ = 0;
  int focus_ = -1, first_ = 0;
  int arrow_ = 0;         // the pointer is on an edge arrow
  bool outside_ = false;  // the pointer is beside the guide
  time::Seconds windowStart_ = 0, focusTime_ = 0;
  std::string watching_;  // the channel playing when the guide opened
  std::string digits_;
  uint32_t digitsAt_ = 0;
  std::string notice_;
  uint32_t noticeUntil_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeMiniGuide(App& app) { return std::make_unique<MiniGuide>(app); }

}  // namespace marigold::app
