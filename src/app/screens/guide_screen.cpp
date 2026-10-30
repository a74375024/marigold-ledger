#include "app/screens/guide_screen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/screens/top_bar.hpp"
#include "app/store/store.hpp"
#include "core/engine/views.hpp"
#include "core/input/hints.hpp"
#include "core/model/channel.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

// Geometry from LiveTVGuideView (points = layout units).
constexpr float kLeft = 80;
constexpr float kChannelW = 182;
constexpr float kGap = 8;
constexpr float kProgX = kLeft + 190;
constexpr float kGridW = gfx::kLayoutWidth - kLeft - 80 - 190;
constexpr int kWindowMinutes = 180;
constexpr float kRowH = 84;
constexpr float kHeaderH = 22;
constexpr float kRulerY = 394;
constexpr float kGridTop = 446;
constexpr float kGridBottom = 996;
constexpr float kMinCell = 40;   // narrower segments render as gaps
constexpr float kTextCell = 110;  // text only from this width
constexpr Rect kPreview = top_bar::kPreview;

constexpr time::Seconds kShift = 90 * 60;       // Right/Left past the edge
constexpr time::Seconds kMaxAhead = 48 * 3600;  // how far later the grid goes
constexpr uint32_t kIdleBackToNowMs = 120000;
constexpr uint32_t kPreviewDelayMs = top_bar::kPreviewDelayMs;
constexpr uint32_t kPreviewGapMs = top_bar::kPreviewGapMs;
constexpr uint32_t kDigitsMs = 1500;
constexpr uint32_t kBackExitMs = 2500;

// Pointer ids.
constexpr int kHitTab = 10;
constexpr int kHitButton = 40;
constexpr int kHitTime = 50;
constexpr int kHitRail = 60;
constexpr int kHitChannel = 1000;
constexpr int kHitProgram = 100000;  // + line * 64 + program

const char* tabTitle(int t) {
  static const char* kTitles[] = {"My Lineup", "Favorites", "Recent", "Live Events", "All Channels"};
  return kTitles[t];
}

std::string durationText(time::Seconds s) {
  long m = static_cast<long>((s + 59) / 60);
  char buf[48];
  if (m >= 60) std::snprintf(buf, sizeof buf, "Starts in %ld h %ld m", m / 60, m % 60);
  else std::snprintf(buf, sizeof buf, "Starts in %ld m", m);
  return buf;
}

std::string upper(const std::string& s) {
  std::string out = s;
  for (char& ch : out) ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
  return out;
}

}  // namespace

GuideScreen::GuideScreen(App& app) : Screen(app) {
  windowStart_ = nowWindow();
  focusTime_ = time::now();
  refresh(true);
}

time::Seconds GuideScreen::nowWindow() const { return time::now() / 1800 * 1800; }
time::Seconds GuideScreen::windowEnd() const { return windowStart_ + kWindowMinutes * 60; }

float GuideScreen::xAt(time::Seconds t) const {
  return kProgX + static_cast<float>(t - windowStart_) * kGridW / (kWindowMinutes * 60.0f);
}

void GuideScreen::shown() {
  refresh(true);
  // Back after a while (hours of a channel): the window starts now again.
  if (windowStart_ < nowWindow()) returnToNow();
  // Back from the player: the picture shrinks into the corner again.
  Playback& p = app_.playback();
  if (p.active() && p.full()) {
    if (app_.store().settings().previewChannels) p.setCorner(kPreview);
    else p.stop();
    if (!p.entry().empty()) focusChannel(p.entry());
  }
}

void GuideScreen::refresh(bool force) {
  Store& s = app_.store();
  auto ch = s.channels();
  auto li = s.listings();
  bool chChanged = force || !ch_ || ch->version != chVersion_;
  ch_ = ch;
  li_ = li;
  chVersion_ = ch->version;
  liVersion_ = li->version;
  if (!chChanged) return;
  tabs_ = {Tab::Lineup, Tab::Favorites, Tab::Recent, Tab::Events};
  if (s.settings().allChannelsTab) tabs_.push_back(Tab::All);
  if (std::find(tabs_.begin(), tabs_.end(), tab_) == tabs_.end()) tab_ = Tab::Lineup;
  rebuildLines(!force);
}

const std::vector<int>& GuideScreen::tabRows() const {
  static const std::vector<int> kNone;
  return ch_ ? engine::views::tabRows(*ch_, tab_, filter()) : kNone;
}

void GuideScreen::rebuildLines(bool samePlace) {
  namespace views = engine::views;
  lines_.clear();
  sections_.clear();
  flip_.clear();
  views::List list;
  if (ch_) {
    list = views::guide(*ch_, li_.get(), tab_, filter(), time::now());
    flip_ = views::flipList(*ch_, list);
  }
  float y = 0;
  for (const views::Line& v : list.lines) {
    Line l;
    l.channel = v.channel;
    if (v.kind == views::Line::Kind::Prompt) {
      l.kind = Line::Kind::Prompt;
      l.y = y;
      y += kRowH + kGap;
    } else if (v.kind == views::Line::Kind::Header) {
      l.kind = Line::Kind::Header;
      l.title = upper(v.title);
      if (!lines_.empty()) y += 4;
      l.y = y;
      y += kHeaderH + kGap;
    } else {
      l.y = y;
      y += kRowH + kGap;
    }
    lines_.push_back(std::move(l));
  }
  for (const views::Section& sec : list.sections) sections_.push_back({sec.title, sec.firstLine, sec.count});
  contentH_ = y;
  // Keep the focus on the same channel when it is still there, under its
  // new id when the list was sorted again (renamed or merged), else on the
  // row in the same place.
  auto lineOf = [&](const std::string& id) {
    for (size_t i = 0; i < lines_.size(); ++i) {
      if (lines_[i].kind == Line::Kind::Channel && ch_->all[static_cast<size_t>(lines_[i].channel)].id == id) {
        return static_cast<int>(i);
      }
    }
    return -1;
  };
  int was = focusLine_;
  int found = lineOf(focusId_);
  if (found < 0 && ch_) {
    if (auto now = ch_->renamed.find(focusId_); now != ch_->renamed.end()) found = lineOf(now->second);
  }
  if (found < 0 && samePlace && was >= 0 && !lines_.empty()) {
    found = std::min(was, static_cast<int>(lines_.size()) - 1);
    while (found > 0 && lines_[static_cast<size_t>(found)].kind == Line::Kind::Header) --found;
    if (lines_[static_cast<size_t>(found)].kind == Line::Kind::Header) found = -1;
  }
  if (found < 0) {
    for (size_t i = 0; i < lines_.size(); ++i) {
      if (lines_[i].kind != Line::Kind::Header) {
        found = static_cast<int>(i);
        break;
      }
    }
  }
  focusLine_ = found;
  if (const GuideChannel* g = focusedChannel()) focusId_ = g->id;
  ensureVisible();
}

const GuideChannel* GuideScreen::focusedChannel() const {
  if (!ch_ || focusLine_ < 0 || static_cast<size_t>(focusLine_) >= lines_.size()) return nullptr;
  const Line& l = lines_[static_cast<size_t>(focusLine_)];
  if (l.kind != Line::Kind::Channel) return nullptr;
  return &ch_->all[static_cast<size_t>(l.channel)];
}

int GuideScreen::programIndex(const std::vector<Listing>& v, time::Seconds t) const {
  return engine::views::programAt(v, t, windowStart_, windowEnd());
}

const Listing* GuideScreen::focusedListing() const {
  const GuideChannel* g = focusedChannel();
  if (!g || !li_) return nullptr;
  const std::vector<Listing>* v = li_->forChannel(g->id);
  if (!v || v->empty()) return nullptr;
  int i = programIndex(*v, focusTime_);
  return i < 0 ? nullptr : &(*v)[static_cast<size_t>(i)];
}

void GuideScreen::ensureVisible() {
  if (focusLine_ < 0 || static_cast<size_t>(focusLine_) >= lines_.size()) {
    scroll_ = 0;
    return;
  }
  float visible = kGridBottom - kGridTop;
  const Line& l = lines_[static_cast<size_t>(focusLine_)];
  float top = l.y;
  // Show the neighborhood header above the row when it is the first one.
  if (focusLine_ > 0 && lines_[static_cast<size_t>(focusLine_ - 1)].kind == Line::Kind::Header)
    top = lines_[static_cast<size_t>(focusLine_ - 1)].y;
  if (top < scroll_) scroll_ = top;
  // One more row in view below the focus, when there is one.
  float bottom = l.y + kRowH + (focusLine_ + 1 < static_cast<int>(lines_.size()) ? kRowH + kGap : 0);
  if (bottom > scroll_ + visible) scroll_ = bottom - visible;
  scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, contentH_ - visible));
}

void GuideScreen::moveRow(int step) {
  int i = focusLine_;
  while (true) {
    i += step;
    if (i < 0 || i >= static_cast<int>(lines_.size())) break;
    if (lines_[static_cast<size_t>(i)].kind != Line::Kind::Header) {
      focusLine_ = i;
      if (const GuideChannel* g = focusedChannel()) focusId_ = g->id;
      ensureVisible();
      return;
    }
  }
  if (step < 0) {
    // Above the first row: the time button sits over the channel column,
    // the tabs over the shows.
    const Listing* l = focusedListing();
    bool nearLeft = !l || xAt(std::max(l->start, windowStart_)) < kProgX + 200;
    area_ = nearLeft ? Area::Time : Area::Tabs;
    if (area_ == Area::Tabs) {
      auto it = std::find(tabs_.begin(), tabs_.end(), tab_);
      tabFocus_ = static_cast<int>(it - tabs_.begin());
    }
  }
}

void GuideScreen::moveProgram(int step) {
  const GuideChannel* g = focusedChannel();
  const std::vector<Listing>* v = g && li_ ? li_->forChannel(g->id) : nullptr;
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
    // Last show in view: the window moves 90 minutes later.
    if (windowStart_ + kShift <= nowWindow() + kMaxAhead) {
      windowStart_ += kShift;
      if (cur >= 0 && (*v)[static_cast<size_t>(cur)].end > windowStart_)
        focusTime_ = std::max(focusTime_, windowStart_);
      else focusTime_ = windowStart_;
      requestVisibleListings();
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
    // In a later window, Left first moves the window back.
    windowStart_ = std::max(nowWindow(), windowStart_ - kShift);
    focusTime_ = cur >= 0 ? std::max((*v)[static_cast<size_t>(cur)].start, windowStart_) : windowStart_;
    if (windowStart_ == nowWindow()) focusTime_ = std::max(focusTime_, time::now());
    return;
  }
  openRail();
}

void GuideScreen::openRail() {
  if (sections_.empty()) return;
  railReturnLine_ = focusLine_;
  railIndex_ = 0;
  for (size_t i = 0; i < sections_.size(); ++i) {
    if (sections_[i].firstLine < focusLine_) railIndex_ = static_cast<int>(i);
  }
  railOrigin_ = railIndex_;
  area_ = Area::Rail;
}

void GuideScreen::setWindow(time::Seconds start) {
  windowStart_ = std::max(nowWindow(), start / 1800 * 1800);
  focusTime_ = windowStart_ == nowWindow() ? time::now() : start;
  requestVisibleListings();
  app_.invalidate();
}

void GuideScreen::returnToNow() { setWindow(time::now()); }

bool GuideScreen::focusChannel(const std::string& entryId) {
  for (size_t i = 0; i < lines_.size(); ++i) {
    const Line& l = lines_[i];
    if (l.kind == Line::Kind::Channel && ch_->all[static_cast<size_t>(l.channel)].id == entryId) {
      focusLine_ = static_cast<int>(i);
      focusId_ = entryId;
      area_ = Area::Grid;
      ensureVisible();
      return true;
    }
  }
  return false;
}

void GuideScreen::watch(const std::string& entryId) {
  Playback& p = app_.playback();
  if (!p.tune(entryId, flipList())) {
    app_.toast("This channel has no working feed right now");
    return;
  }
  p.setFull();
  app_.nav().push(makePlayer(app_));
}

void GuideScreen::select() {
  if (focusLine_ < 0) return;
  const Line& line = lines_[static_cast<size_t>(focusLine_)];
  if (line.kind == Line::Kind::Prompt) {
    openLineupSetup();
    return;
  }
  const GuideChannel* g = focusedChannel();
  if (!g) return;
  const Listing* l = focusedListing();
  time::Seconds now = time::now();
  // A show on now (or starting within a minute) plays; a later one opens
  // its detail card.
  if (!l || l->start <= now + 60) {
    watch(g->id);
    return;
  }
  app_.nav().push(makeProgramDetail(app_, g->id, *l));
}

void GuideScreen::longPress() {
  const GuideChannel* g = focusedChannel();
  if (!g) return;
  std::string id = g->id;
  App& app = app_;
  GuideScreen* self = this;
  std::optional<Listing> later;
  if (const Listing* l = focusedListing(); l && l->start > time::now() + 60) later = *l;
  auto panel = std::make_unique<PanelScreen>(app_, PanelScreen::Side::Trailing, 640.0f,
                                             [&app, self, id, later](ui::ListPanel& p) {
    auto ch = app.store().channels();
    const GuideChannel* c = ch->find(id);
    if (!c) return;
    p.title = c->name;
    ui::PanelItem watchItem{ui::PanelItem::Kind::Action, "Watch"};
    watchItem.select = [&app, self, id] {
      app.nav().pop();
      self->watch(id);
    };
    p.items.push_back(watchItem);
    if (later) {
      bool set = app.store().hasShowReminder(id, later->start);
      ui::PanelItem remind{ui::PanelItem::Kind::Action, set ? "Remove Reminder" : "Remind Me"};
      remind.detail = later->title;
      remind.select = [&app, id, l = *later] { app.store().toggleShowReminder(id, l); };
      p.items.push_back(remind);
    }
    // East/West: the other version, to watch or to add next to this one.
    const GuideChannel* other = c->partner.empty() ? nullptr : ch->find(c->partner);
    if (other) {
      ui::PanelItem watchOther{ui::PanelItem::Kind::Action, "Watch " + other->name};
      watchOther.coast = other->west ? 1 : 0;
      auto li = app.store().listings();
      if (const std::vector<Listing>* v = li ? li->forChannel(other->id) : nullptr) {
        time::Seconds now = time::now();
        for (const Listing& l : *v)
          if (l.isOn(now)) watchOther.detail = "Now: " + l.title + (l.episode.empty() ? "" : " \xc2\xb7 " + l.episode);
      }
      std::string oid = other->id;
      watchOther.select = [&app, self, oid] {
        app.nav().pop();
        self->watch(oid);
      };
      p.items.push_back(watchOther);
      if (!other->inLineup && c->inLineup) {
        ui::PanelItem addOther{ui::PanelItem::Kind::Action, "+ Add " + other->name + " to My Lineup"};
        addOther.detail = std::string("Right ") + (other->west ? "below " : "above ") + c->name + ", with its own listings";
        std::string name = other->name;
        addOther.select = [&app, oid, name] {
          app.store().toggleLineup(oid);
          app.toast(name + " added to My Lineup");
        };
        p.items.push_back(addOther);
      }
    }
    ui::PanelItem lineup{ui::PanelItem::Kind::Action, c->inLineup ? "Remove from My Lineup" : "Add to My Lineup"};
    lineup.select = [&app, id] { app.store().toggleLineup(id); };
    p.items.push_back(lineup);
    if (c->feeds > 1) {
      ui::PanelItem feeds{ui::PanelItem::Kind::Chevron, "Choose Feed"};
      feeds.value = std::to_string(c->feeds);
      feeds.select = [&app, id] {
        app.nav().pop();
        app.nav().push(makeSources(app, id));
      };
      p.items.push_back(feeds);
    }
    ui::PanelItem fav{ui::PanelItem::Kind::Action, c->favorite ? "Remove from Favorites" : "Add to Favorites"};
    fav.select = [&app, id] { app.store().toggleFavorite(id); };
    p.items.push_back(fav);
    if (self->tab_ == Tab::All && !c->group.empty()) {
      ui::PanelItem hide{ui::PanelItem::Kind::Action, "Hide Group from All Channels"};
      hide.detail = c->group;
      std::string group = c->group;
      hide.select = [&app, group] {
        app.store().setGroupHidden(group, true);
        app.nav().pop();
        app.toast("Hidden. Options \xe2\x80\xba Provider groups shows it again");
      };
      p.items.push_back(hide);
    }
  });
  app_.nav().push(std::move(panel));
}

void GuideScreen::openTimeMenu() {
  App& app = app_;
  GuideScreen* self = this;
  time::Seconds current = windowStart_;
  app_.nav().push(std::make_unique<PanelScreen>(app_, PanelScreen::Side::Leading, 420.0f,
                                                [&app, self, current](ui::ListPanel& p) {
    const time::Zone& z = app.zone();
    time::Seconds now = time::now();
    time::Civil l = z.toLocal(now);
    auto at = [&](int dayOffset, int hour) {
      time::Civil c = l;
      c.hour = hour;
      c.minute = 0;
      c.second = 0;
      time::Seconds base = z.fromLocal(c);
      return base + dayOffset * 86400;
    };
    struct Preset {
      const char* title;
      time::Seconds t;
    };
    std::vector<Preset> presets = {{"Now", now}};
    if (l.hour < 12) presets.push_back({"This Afternoon", at(0, 12)});
    if (l.hour < 19) presets.push_back({"Tonight", at(0, 19)});
    presets.push_back({"Tomorrow Morning", at(1, 8)});
    presets.push_back({"Tomorrow Afternoon", at(1, 12)});
    presets.push_back({"Tomorrow Night", at(1, 19)});
    p.subtitle = "GO TO";
    for (const Preset& pr : presets) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, pr.title};
      ui::Ctx tmp{app.renderer(), app.textRenderer(), nullptr, nullptr, now, 0, &z};
      it.value = std::string(pr.title) == "Now" ? ui::clock(tmp, now) : ui::clock(tmp, pr.t);
      it.on = pr.t / 1800 * 1800 == current || (std::string(pr.title) == "Now" && current == now / 1800 * 1800);
      time::Seconds t = pr.t;
      it.select = [&app, self, t] {
        self->setWindow(t);
        app.nav().pop();
      };
      p.items.push_back(it);
    }
  }));
}

void GuideScreen::openFilter() {
  App& app = app_;
  GuideScreen* self = this;
  app_.nav().push(std::make_unique<PanelScreen>(app_, PanelScreen::Side::Trailing, 760.0f,
                                                [&app, self](ui::ListPanel& p) {
    p.subtitle = std::string("FILTER \xc2\xb7 ") + tabTitle(static_cast<int>(self->tab_));
    p.title = "Show";
    auto add = [&](const std::string& title, Filter f, const std::string& section, size_t count) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, title};
      it.on = self->filter_ == f && ((f != Filter::Section && f != Filter::Group) || self->filterSection_ == section);
      it.value = std::to_string(count);
      it.dot = f != Filter::None && f != Filter::Section && f != Filter::Group;
      it.select = [&app, self, f, section] {
        self->filter_ = f;
        self->filterSection_ = section;
        self->rebuildLines();
        app.nav().pop();
      };
      p.items.push_back(it);
    };
    const std::vector<int>& rows = self->tabRows();
    add("All channels", Filter::None, "", rows.size());
    p.items.push_back(ui::PanelItem::header("ON NOW"));
    for (const engine::views::Count& n : engine::views::onNowCounts(*self->ch_, rows, self->li_.get(), time::now()))
      add(n.title, n.filter.kind, "", n.count);
    p.items.push_back(ui::PanelItem::header("NEIGHBORHOODS"));
    for (const engine::views::Count& n : engine::views::sectionCounts(*self->ch_, rows))
      add(n.title, Filter::Section, n.title, n.count);
    if (self->tab_ == Tab::All) {
      std::vector<GroupSummary> groups = app.store().groups();
      p.items.push_back(ui::PanelItem::header("PROVIDER GROUPS (" + std::to_string(groups.size()) + ")"));
      for (const GroupSummary& g : groups) {
        add(g.name, Filter::Group, g.name, static_cast<size_t>(g.count));
        ui::PanelItem& it = p.items.back();
        if (g.hidden) it.detail = "Hidden from All Channels";
        std::string name = g.name;
        bool hidden = g.hidden;
        it.hold = [&app, name, hidden] { app.store().setGroupHidden(name, !hidden); };
      }
      p.items.push_back(ui::PanelItem::note(
          "Hold OK on a group to hide it from All Channels. Hidden groups can still be opened here or found with Search."));
    }
  }));
}

void GuideScreen::openOptions() { app_.nav().push(makeOptions(app_)); }

void GuideScreen::openSources() {
  if (const GuideChannel* g = focusedChannel()) app_.nav().push(makeSources(app_, g->id));
}

void GuideScreen::openSearch() {
  GuideScreen* self = this;
  App& app = app_;
  app_.nav().push(makeSearch(app_, [self, &app](const std::string& id) {
    // Prefer the row in the current tab; else switch to all channels.
    if (!self->focusChannel(id)) {
      self->tab_ = app.store().settings().allChannelsTab ? Tab::All : Tab::Lineup;
      self->filter_ = Filter::None;
      self->rebuildLines();
      if (!self->focusChannel(id)) {
        self->watch(id);
        return;
      }
    }
    self->returnToNow();
  }));
}

void GuideScreen::openEvents() {
  // The board is its own screen over the guide; closing it picks a tab.
  GuideScreen* self = this;
  app_.nav().push(makeEventsBoard(app_, [self](int tab) {
    static const Tab kTabs[] = {Tab::Lineup, Tab::Favorites, Tab::Recent};
    self->tab_ = kTabs[std::clamp(tab, 0, 2)];
    self->filter_ = Filter::None;
    self->rebuildLines();
    auto it = std::find(self->tabs_.begin(), self->tabs_.end(), self->tab_);
    self->tabFocus_ = static_cast<int>(it - self->tabs_.begin());
    self->area_ = tab == 0 ? Area::Grid : Area::Tabs;
    self->requestVisibleListings();
  }));
}

void GuideScreen::openLineupSetup() { app_.nav().push(makeLineupSetup(app_)); }

void GuideScreen::digit(int d) {
  if (digits_.size() >= 4) digits_.clear();
  digits_ += static_cast<char>('0' + d);
  digitsAt_ = app_.ms();
}

void GuideScreen::requestVisibleListings() {
  // Rows on screen plus a screenful around them ("±20 rows").
  if (!ch_ || lines_.empty()) return;
  std::vector<std::string> ids;
  int first = std::max(0, focusLine_ - 20), last = std::min(static_cast<int>(lines_.size()) - 1, focusLine_ + 20);
  for (int i = first; i <= last; ++i) {
    const Line& l = lines_[static_cast<size_t>(i)];
    if (l.kind == Line::Kind::Channel) ids.push_back(ch_->all[static_cast<size_t>(l.channel)].id);
  }
  app_.store().wantListings(ids);
}

bool GuideScreen::key(const KeyEvent& e) {
  lastKeyMs_ = app_.ms();
  Key k = e.key;
  if (!ch_ || ch_->all.empty()) {
    // Status screen: one button.
    const StoreStatus& st = app_.store().status();
    if (k == Key::Ok && st.phase == StoreStatus::Phase::NoAccounts) app_.nav().push(makeAccounts(app_, false));
    else if (k == Key::Ok && st.phase != StoreStatus::Phase::Loading) app_.store().refreshAll();
    else if (k == Key::Blue) openOptions();
    else if (k == Key::Back) backOut();
    return true;
  }
  int d = digitOf(k);
  if (d >= 0) {
    digit(d);
    return true;
  }
  if (e.longPress) {
    if (area_ == Area::Grid) longPress();
    return true;
  }
  // Keys a remote without colour or CH keys has (Fire TV): play/pause
  // opens Live Events, rewind and fast forward page.
  if (k == Key::PlayPause || k == Key::Play) k = Key::Yellow;
  if (k == Key::Rewind) k = Key::ChannelUp;
  if (k == Key::FastForward) k = Key::ChannelDown;
  switch (k) {
    case Key::Red: openSources(); return true;
    case Key::Green:
      if (const GuideChannel* g = focusedChannel()) {
        bool adding = !g->inLineup;
        std::string name = g->name;
        app_.store().toggleLineup(g->id);
        app_.toast(adding ? name + " added to My Lineup" : name + " removed from My Lineup");
      }
      return true;
    case Key::Yellow:
      openEvents();
      return true;
    case Key::Blue: openOptions(); return true;
    case Key::ChannelUp:
    case Key::ChannelDown: {
      // A page of rows at a time.
      for (int i = 0; i < 5; ++i) moveRow(k == Key::ChannelUp ? -1 : 1);
      area_ = Area::Grid;
      requestVisibleListings();
      return true;
    }
    default: break;
  }

  if (area_ == Area::Rail) {
    switch (k) {
      case Key::Up:
      case Key::Down: {
        int n = static_cast<int>(sections_.size());
        railIndex_ = std::clamp(railIndex_ + (k == Key::Up ? -1 : 1), 0, n - 1);
        focusLine_ = sections_[static_cast<size_t>(railIndex_)].firstLine + 1;
        ensureVisible();
        return true;
      }
      case Key::Right:
      case Key::Ok:
        area_ = Area::Grid;
        focusLine_ = std::min(static_cast<int>(lines_.size()) - 1, sections_[static_cast<size_t>(railIndex_)].firstLine + 1);
        if (const GuideChannel* g = focusedChannel()) focusId_ = g->id;
        focusTime_ = std::max(time::now(), windowStart_);
        ensureVisible();
        requestVisibleListings();
        return true;
      case Key::Back:
      case Key::Left:
        area_ = Area::Grid;
        focusLine_ = railReturnLine_;
        ensureVisible();
        return true;
      default: return true;
    }
  }

  if (area_ == Area::Tabs || area_ == Area::Buttons) {
    int nTabs = static_cast<int>(tabs_.size());
    switch (k) {
      case Key::Left:
        if (area_ == Area::Buttons) {
          if (button_ > 0) --button_;
          else {
            area_ = Area::Tabs;
            tabFocus_ = nTabs - 1;
          }
        } else if (tabFocus_ > 0) {
          --tabFocus_;
        }
        return true;
      case Key::Right:
        if (area_ == Area::Tabs) {
          if (tabFocus_ < nTabs - 1) ++tabFocus_;
          else {
            area_ = Area::Buttons;
            button_ = 0;
          }
        } else if (button_ < 2) {
          ++button_;
        }
        return true;
      case Key::Down:
        if (focusLine_ >= 0) area_ = Area::Grid;
        return true;
      case Key::Ok:
        if (area_ == Area::Tabs) {
          // Moving across tabs only highlights; OK switches.
          if (tabs_[static_cast<size_t>(tabFocus_)] == Tab::Events) {
            openEvents();
            return true;
          }
          tab_ = tabs_[static_cast<size_t>(tabFocus_)];
          filter_ = Filter::None;
          rebuildLines();
          if (focusLine_ >= 0) area_ = Area::Grid;
          requestVisibleListings();
        } else if (button_ == 0) {
          openSearch();
        } else if (button_ == 1) {
          openFilter();
        } else {
          openOptions();
        }
        return true;
      case Key::Back:
        area_ = Area::Grid;
        return true;
      default: return true;
    }
  }

  if (area_ == Area::Time) {
    switch (k) {
      case Key::Ok: openTimeMenu(); return true;
      case Key::Right:
        area_ = Area::Tabs;
        tabFocus_ = 0;
        return true;
      case Key::Down:
        if (focusLine_ >= 0) area_ = Area::Grid;
        return true;
      case Key::Back:
        area_ = Area::Grid;
        return true;
      default: return true;
    }
  }

  // Grid.
  switch (k) {
    case Key::Up:
      moveRow(-1);
      requestVisibleListings();
      return true;
    case Key::Down:
      moveRow(1);
      requestVisibleListings();
      return true;
    case Key::Left:
      if (focusLine_ >= 0 && lines_[static_cast<size_t>(focusLine_)].kind == Line::Kind::Prompt) return true;
      moveProgram(-1);
      return true;
    case Key::Right:
      if (focusLine_ >= 0 && lines_[static_cast<size_t>(focusLine_)].kind == Line::Kind::Prompt) return true;
      moveProgram(1);
      return true;
    case Key::Ok:
      select();
      return true;
    case Key::Info:
    case Key::Menu:
      longPress();
      return true;
    case Key::Back:
      if (!digits_.empty()) {
        digits_.clear();
        return true;
      }
      if (filter_ != Filter::None) {
        filter_ = Filter::None;
        rebuildLines();
        return true;
      }
      if (later()) {
        returnToNow();
        return true;
      }
      backOut();
      return true;
    default:
      return false;
  }
}

// Back with nothing left to undo: Back twice leaves the app, or once where
// the platform's rule is that Back on the top screen goes home (tvOS).
void GuideScreen::backOut() {
  if (app_.window().leaveOnFirstBack()) {
    app_.window().leave();
    return;
  }
  if (backAt_ && app_.ms() - backAt_ < kBackExitMs) {
    app_.quit();
  } else {
    backAt_ = app_.ms();
    app_.toast("Press Back again to exit");
  }
}

void GuideScreen::hover(int id) {
  if (id >= kHitProgram) {
    int line = (id - kHitProgram) / 64;
    int prog = (id - kHitProgram) % 64;
    if (line < 0 || line >= static_cast<int>(lines_.size())) return;
    focusLine_ = line;
    area_ = Area::Grid;
    if (const GuideChannel* g = focusedChannel()) {
      focusId_ = g->id;
      const std::vector<Listing>* v = li_ ? li_->forChannel(g->id) : nullptr;
      if (v && prog < static_cast<int>(v->size())) focusTime_ = std::max((*v)[static_cast<size_t>(prog)].start, windowStart_);
    }
    return;
  }
  if (id >= kHitChannel) {
    int line = id - kHitChannel;
    if (line >= 0 && line < static_cast<int>(lines_.size())) {
      focusLine_ = line;
      area_ = Area::Grid;
      if (const GuideChannel* g = focusedChannel()) focusId_ = g->id;
    }
    return;
  }
  if (id >= kHitRail && area_ == Area::Rail) {
    railIndex_ = std::clamp(id - kHitRail, 0, static_cast<int>(sections_.size()) - 1);
    return;
  }
  if (id == kHitTime) area_ = Area::Time;
  else if (id >= kHitButton && id < kHitButton + 3) {
    area_ = Area::Buttons;
    button_ = id - kHitButton;
  } else if (id >= kHitTab && id < kHitTab + 5) {
    area_ = Area::Tabs;
    tabFocus_ = id - kHitTab;
  }
}

bool GuideScreen::tick(uint32_t ms) {
  bool animating = !launchOff_;  // the launch mark moves every frame
  Store& s = app_.store();
  if (s.channels()->version != chVersion_ || s.listings()->version != liVersion_) {
    refresh();
    app_.invalidate();
  }
  // Number keys tune after a pause, or at once when no longer number
  // starts with them.
  if (!digits_.empty() && (ms - digitsAt_ >= kDigitsMs || engine::views::digitsSettled(digits_, *ch_))) {
    int n = std::atoi(digits_.c_str());
    digits_.clear();
    std::string target;
    for (const GuideChannel& g : ch_->all) {
      if (g.number && *g.number == n) {
        target = g.id;
        break;
      }
    }
    if (target.empty()) app_.toast("No channel " + std::to_string(n));
    else {
      focusChannel(target);
      watch(target);
    }
    app_.invalidate();
  }
  if (!digits_.empty()) animating = true;
  // Placeholders pulse (a few redraws a second) while rows load.
  if (skeletonShown_ && ui::skeletonMoved(ms, pulseStep_)) animating = true;
  // The progress screen, the header's spinner and a result fading out move.
  const StoreStatus& status = s.status();
  if (status.phase == StoreStatus::Phase::Loading || status.mark == StoreStatus::Mark::Busy ||
      (!shownLine_.empty() && status.line.empty()))
    animating = true;
  // Two idle minutes in a later window: back to now. A window left behind
  // as time passed (it never starts before now) moves on with it.
  if (later() && ms - lastKeyMs_ > kIdleBackToNowMs) returnToNow();
  else if (windowStart_ < nowWindow()) returnToNow();

  // Preview: after focus rests on a channel, only while the grid has focus
  // and nothing else is open.
  Playback& p = app_.playback();
  const GuideChannel* g = focusedChannel();
  bool onTop = app_.nav().top() == this;
  if (onTop && g && area_ == Area::Grid && s.settings().previewChannels) {
    if (previewCandidate_ != g->id) {
      previewCandidate_ = g->id;
      focusSince_ = ms;
    }
    if (p.entry() != g->id && ms - focusSince_ >= kPreviewDelayMs && ms - lastPreviewAt_ >= kPreviewGapMs) {
      lastPreviewAt_ = ms;
      p.setCorner(kPreview);
      p.tune(g->id, flipList());
    }
    if (p.entry() != g->id) animating = animating || ms - focusSince_ < kPreviewDelayMs + 100;
  }
  return animating;
}

// ---------------------------------------------------------------- drawing

void GuideScreen::draw(ui::Ctx& c) {
  skeletonShown_ = false;
  if (!launchOff_) {
    // Opening the app: the flower covers loading until the guide can show,
    // then blooms out as the guide comes in behind it.
    Store& s = app_.store();
    bool ready = s.status().phase != StoreStatus::Phase::Loading;
    launch_.update(c.ms, s.loadProgress(), s.openingSaved(), ready);
    if (launch_.covering(c.ms)) {
      launch_.backdrop(c, c.ms);
      launch_.mark(c, c.ms);
      return;
    }
    if (launch_.done(c.ms)) launchOff_ = true;
  }
  drawGuide(c);
  if (!launchOff_) {
    launch_.veil(c, c.ms);
    launch_.mark(c, c.ms);
  }
}

void GuideScreen::drawGuide(ui::Ctx& c) {
  if (!ch_ || ch_->all.empty()) {
    drawStatus(c);
    drawPreview(c);
    return;
  }
  drawHero(c);
  drawPreview(c);
  drawTabs(c);
  drawRuler(c);
  drawGrid(c);
  if (area_ == Area::Rail) drawRail(c);
  const GuideChannel* g = focusedChannel();
  ui::keyBar(c, hints::guide(app_.remoteKeys(), g && g->inLineup));
  drawDigits(c);
}

void GuideScreen::drawStatus(ui::Ctx& c) {
  const StoreStatus& st = app_.store().status();
  float cx = gfx::kLayoutWidth * 0.5f;
  std::string title, message;
  switch (st.phase) {
    case StoreStatus::Phase::NoAccounts:
      title = "Add your channels";
      message = "Press OK to add an Xtream, Emby or M3U provider.";
      break;
    case StoreStatus::Phase::Loading:
      // No list on screen yet: how far the channel list has got.
      setup_ui::building(c, {0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, app_.store().loadProgress(), building_);
      return;
    case StoreStatus::Phase::Failed:
      title = "Couldn't load channels";
      message = st.problem.empty() ? "Check the provider in Options." : st.problem;
      break;
    case StoreStatus::Phase::Ready:
      title = "No channels";
      message = "Your providers sent an empty channel list.";
      break;
  }
  ui::label(c, title, 0, 460, Weight::Bold, 44, theme::white(), gfx::kLayoutWidth, Align::Center);
  ui::paragraph(c, message, cx - 600, 530, 1200, Weight::Regular, 26, theme::white(0.7f), 3);
  if (st.phase != StoreStatus::Phase::Loading) {
    const char* button = st.phase == StoreStatus::Phase::NoAccounts ? "Add a Provider" : "Try Again";
    float w = ui::chipWidth(c, button, 26) + 40;
    Rect r{cx - w * 0.5f, 640, w, 70};
    c.g.fill(r, theme::white(), 35);
    ui::labelIn(c, button, r, Weight::Bold, 26, theme::black(), Align::Center);
  }
}

void GuideScreen::drawHeaderLine(ui::Ctx& c, float x, float y, float right) {
  const StoreStatus& st = app_.store().status();
  StoreStatus::Mark mark = st.phase == StoreStatus::Phase::Loading ? StoreStatus::Mark::Busy : st.mark;
  std::string line = st.line;
  float alpha = 1;
  if (!line.empty()) {
    shownLine_ = line;
    shownMark_ = mark;
    fadeFrom_ = 0;
  } else if (!shownLine_.empty() &&
             (shownMark_ == StoreStatus::Mark::Done || shownMark_ == StoreStatus::Mark::Failed)) {
    // "Channel list updated": its few seconds are up, so it fades.
    if (!fadeFrom_) fadeFrom_ = c.ms ? c.ms : 1;
    float t = static_cast<float>(c.ms - fadeFrom_) / 400.0f;
    if (t >= 1) {
      shownLine_.clear();
    } else {
      line = shownLine_;
      mark = shownMark_;
      alpha = 1 - t;
    }
  } else {
    shownLine_.clear();
  }
  if (line.empty()) return;
  // The mark takes the spinner's place, so the words don't move.
  switch (mark) {
    case StoreStatus::Mark::Busy:
      ui::spinner(c, x + 10, y + 14, 10, theme::white(0.8f * alpha));
      x += 30;
      break;
    case StoreStatus::Mark::Done:
      ui::labelIn(c, "\xe2\x9c\x93", {x, y + 2, 20, 24}, Weight::Bold, 20, theme::hex(theme::kToggleOn, 0.9f * alpha),
                  Align::Center);
      x += 30;
      break;
    case StoreStatus::Mark::Failed:
      ui::labelIn(c, "!", {x, y + 2, 20, 24}, Weight::Bold, 20, theme::hex(theme::kWarning, 0.9f * alpha), Align::Center);
      x += 30;
      break;
    case StoreStatus::Mark::None:
      break;
  }
  ui::label(c, line, x, y + 3, Weight::Regular, 18, theme::white(0.45f * alpha), right - x);
}

void GuideScreen::drawHero(ui::Ctx& c) {
  const GuideChannel* g = focusedChannel();
  float x = kLeft, y = 48;
  float w = 1240;
  if (g) {
    ui::label(c, g->section, x, y, Weight::Bold, 22, theme::white(0.6f));
  } else if (focusLine_ >= 0 && lines_[static_cast<size_t>(focusLine_)].kind == Line::Kind::Prompt) {
    ui::label(c, "My Lineup", x, y, Weight::Bold, 22, theme::white(0.6f));
  }
  float sx = x + (g ? ui::textWidth(c, g->section, Weight::Bold, 22) + 24 : 0);
  drawHeaderLine(c, sx, y, x + w);
  y += 40;
  if (!g) {
    if (focusLine_ >= 0 && lines_[static_cast<size_t>(focusLine_)].kind == Line::Kind::Prompt) {
      ui::label(c, "Make Live TV yours", x, y + 10, Weight::Bold, 44, theme::white());
      ui::paragraph(c,
                    "Set up My Lineup: your local stations and the channels you watch, in one short "
                    "guide instead of " + std::to_string(ch_->rawChannels) + " channels.",
                    x, y + 80, 1200, Weight::Regular, 22, theme::white(0.7f), 2);
    }
    return;
  }
  // Logo, name, star, feeds chip.
  ui::logo(c, g->logoUrl, "", {x, y, 90, 34});
  float nx = x + (g->logoUrl.empty() ? 0 : 104);
  nx += ui::label(c, g->name, nx, y + 1, Weight::Medium, 26, theme::white(), 600) + 14;
  if (g->favorite) nx += ui::label(c, "\xe2\x98\x85", nx, y + 2, Weight::Bold, 24, theme::hex(theme::kStar)) + 12;
  if (g->feeds > 1)
    nx += ui::chip(c, std::to_string(g->feeds) + " feeds", nx, y + 4, 18, theme::white(0.5f), theme::white(0.85f), true) +
          12;
  const Listing* l = focusedListing();
  time::Seconds now = c.now;
  if (!g->partner.empty()) {
    // East/West: which version, and when this show is on the other one.
    nx += ui::coastChip(c, g->west, nx, y + 4, 16) + 14;
    const GuideChannel* other = ch_->find(g->partner);
    const std::vector<Listing>* ov = li_ && other ? li_->forChannel(other->id) : nullptr;
    if (l && ov) {
      for (const Listing& o : *ov) {
        if (o.title != l->title || o.end <= now || o.start == l->start) continue;
        std::string when = o.isOn(now) ? "now" : "at " + ui::clock(c, o.start);
        std::string day = o.isOn(now) ? "" : ui::dayWord(c, o.start);
        ui::label(c, "Also on " + other->name + " " + (day.empty() ? when : day + " " + when), nx, y + 2,
                  Weight::Medium, 22, theme::white(0.7f), x + w - nx);
        break;
      }
    }
  }
  y += 46;

  ui::label(c, l ? l->title : g->name, x, y, Weight::Bold, 44, theme::white(), w);
  y += 62;
  float bx = x;
  if (l) {
    bool on = l->isOn(now);
    if (on) bx += ui::chip(c, "LIVE", bx, y + 2, 18, theme::hex(theme::kLive), theme::white()) + 12;
    else bx += ui::chip(c, "LATER", bx, y + 2, 18, theme::white(0.6f), theme::white(0.85f), true) + 12;
    if (l->premiere) bx += ui::chip(c, "NEW", bx, y + 2, 18, theme::hex(theme::kNew), theme::white()) + 12;
    if (!on && app_.store().hasShowReminder(g->id, l->start))
      bx += ui::chip(c, "REMINDER SET", bx, y + 2, 18, theme::white(0.6f), theme::white(0.85f), true) + 12;
    std::string day = ui::dayWord(c, l->start);
    std::string when = (day.empty() ? "" : day + " \xc2\xb7 ") + ui::range(c, l->start, l->end, true);
    bx += ui::label(c, when, bx, y, Weight::Medium, 24, theme::white()) + 10;
    if (l->outside) bx += ui::chip(c, "OUTSIDE LISTING", bx, y + 4, 15, theme::white(0.45f), theme::white(0.7f), true) + 10;
    if (!l->episode.empty())
      ui::label(c, "\xc2\xb7 " + l->episode, bx, y + 2, Weight::Regular, 22, theme::white(0.65f), x + w - bx);
    y += 44;
    if (on) {
      double frac = static_cast<double>(now - l->start) / static_cast<double>(std::max<time::Seconds>(1, l->end - l->start));
      ui::progress(c, {x, y + 4, 420, 6}, frac);
    } else {
      ui::label(c, durationText(l->start - now), x, y - 4, Weight::Medium, 20, theme::white(0.7f));
    }
    y += 22;
    if (!l->overview.empty()) ui::paragraph(c, l->overview, x, y, 1200, Weight::Regular, 20, theme::white(0.7f), 2, 1.4f);
  } else {
    const std::vector<Listing>* v = li_ ? li_->forChannel(g->id) : nullptr;
    bool askedEmpty = li_ && li_->empty.count(g->id);
    ui::chip(c, "LIVE", bx, y + 2, 18, theme::hex(theme::kLive), theme::white());
    if (v || askedEmpty) {
      ui::label(c, "No guide info", bx + 80, y, Weight::Medium, 24, theme::white(0.7f));
    } else {
      // Still loading: placeholders where the show and its times go.
      skeletonShown_ = true;
      ui::Color block = ui::skeleton(c);
      c.g.fill({bx + 80, y + 4, 420, 24}, block, 12);
      c.g.fill({bx + 80, y + 44, 260, 16}, block, 8);
    }
  }
}

void GuideScreen::drawPreview(ui::Ctx& c) {
  top_bar::clock(c);
  if (!ch_ || ch_->all.empty()) return;
  const GuideChannel* g = focusedChannel();
  top_bar::preview(c, app_.playback(), g ? g->logoUrl : std::string(), g ? g->name : std::string());
}

void GuideScreen::drawTabs(ui::Ctx& c) {
  float x = kLeft, y = 318;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const char* title = tabTitle(static_cast<int>(tabs_[i]));
    float w = ui::textWidth(c, title, Weight::Bold, 27);
    bool selected = tabs_[i] == tab_;
    bool focused = area_ == Area::Tabs && static_cast<int>(i) == tabFocus_;
    Rect hit{x - 16, y, w + 32, 60};
    c.hit(hit, kHitTab + static_cast<int>(i));
    if (focused) c.g.fill(hit, theme::white(), 30);
    Color col = focused ? theme::black() : selected ? theme::white() : theme::white(0.55f);
    ui::labelIn(c, title, {x, y, w, 54}, Weight::Bold, 27, col);
    if (selected && !focused) c.g.fill({x, y + 52, w, 5}, theme::white(), 2.5f);
    x += w + 44;
  }
  if (filter_ != Filter::None) {
    std::string f = (filter_ == Filter::Section || filter_ == Filter::Group) ? filterSection_
                    : filter_ == Filter::SportsNow ? "Sports on now"
                    : filter_ == Filter::NewsNow ? "News on now"
                    : filter_ == Filter::MoviesNow ? "Movies on now"
                                                   : "Kids on now";
    ui::chip(c, "\xe2\x97\x8f " + f + "  \xc3\x97", x + 10, y + 14, 19, theme::hex(theme::kNowLine, 0.3f), theme::white());
  }
  if (later()) {
    float bw = ui::chipWidth(c, "BACK returns to Now", 18);
    ui::chip(c, "BACK returns to Now", 1600 - bw - 24, y + 16, 18, theme::hex(theme::kNowLine, 0.35f), theme::white());
  }
  // Round buttons: search, filter, options.
  top_bar::buttons(c, area_ == Area::Buttons ? button_ : -1, filter_ != Filter::None, kHitButton);
}

void GuideScreen::drawRuler(ui::Ctx& c) {
  // Time button: "Tonight ▼".
  std::string word;
  if (!later()) word = "Now";
  else {
    word = ui::dayWord(c, windowStart_);
    if (word.empty()) word = "Today";
  }
  word += " \xe2\x96\xbc";
  bool focused = area_ == Area::Time;
  float w = ui::chipWidth(c, word, 19) + 10;
  Rect r{kLeft, kRulerY, w, 40};
  c.hit(r, kHitTime);
  c.g.fill(r, focused ? theme::white() : theme::white(0.1f), 20);
  ui::labelIn(c, word, r, Weight::Bold, 19, focused ? theme::black() : theme::white(), Align::Center);

  c.g.fill({kProgX, kRulerY + 39, kGridW, 1}, theme::white(0.15f));
  for (int i = 0; i < kWindowMinutes / 30; ++i) {
    time::Seconds t = windowStart_ + i * 1800;
    ui::label(c, ui::clock(c, t, false), xAt(t) + 8, kRulerY + 8, Weight::Medium, 18, theme::white(0.6f));
  }
}

void GuideScreen::drawGrid(ui::Ctx& c) {
  if (lines_.empty()) {
    std::string message = tab_ == Tab::Events      ? "The Live Events board arrives in a later build."
                          : tab_ == Tab::Favorites ? "No favorites yet. Hold OK on a channel to add one."
                          : tab_ == Tab::Recent    ? "Channels you watch show up here."
                          : filter_ != Filter::None ? "Nothing matches this filter. Back clears it."
                                                    : "No channels here.";
    ui::label(c, message, kLeft, kGridTop + 40, Weight::Medium, 26, theme::white(0.6f));
    return;
  }
  Rect clip{0, kGridTop - 6, gfx::kLayoutWidth, kGridBottom - kGridTop + 12};
  c.g.pushClip(clip);
  for (size_t i = 0; i < lines_.size(); ++i) {
    float y = kGridTop + lines_[i].y - scroll_;
    if (y > kGridBottom || y + kRowH < kGridTop) continue;
    drawRow(c, static_cast<int>(i), y);
  }
  c.g.popClip();
  // Now line.
  time::Seconds now = c.now;
  if (area_ != Area::Rail && now >= windowStart_ && now < windowEnd()) {
    float x = std::round(xAt(now));
    c.g.fill({x - 1.5f, kRulerY + 40, 3, kGridBottom - kRulerY - 40}, theme::hex(theme::kNowLine));
  }
}

void GuideScreen::drawRow(ui::Ctx& c, int lineIndex, float y) {
  const Line& line = lines_[static_cast<size_t>(lineIndex)];
  bool rowFocused = area_ == Area::Grid && lineIndex == focusLine_;
  if (line.kind == Line::Kind::Header) {
    ui::label(c, line.title, kLeft, y, Weight::Bold, 17, theme::white(0.5f));
    return;
  }
  if (line.kind == Line::Kind::Prompt) {
    Rect r{kLeft, y, gfx::kLayoutWidth - 2 * kLeft, kRowH};
    c.hit(r, kHitChannel + lineIndex);
    c.g.fill(r, rowFocused ? theme::white() : theme::white(0.09f), 14);
    Color fg = rowFocused ? theme::black() : theme::white();
    ui::label(c, "Make Live TV yours", r.x + 28, r.y + 14, Weight::Bold, 24, fg);
    ui::label(c, "Set up My Lineup with your local stations and favourite channels.", r.x + 28, r.y + 48,
              Weight::Regular, 18, rowFocused ? theme::black(0.65f) : theme::white(0.65f));
    float cw = ui::chipWidth(c, "Set Up My Lineup", 20) + 20;
    Rect chipR{r.x + r.w - cw - 24, r.y + 21, cw, 42};
    c.g.fill(chipR, rowFocused ? theme::black() : theme::white(), 21);
    ui::labelIn(c, "Set Up My Lineup", chipR, Weight::Bold, 20, rowFocused ? theme::white() : theme::black(),
                Align::Center);
    return;
  }
  const GuideChannel& g = ch_->all[static_cast<size_t>(line.channel)];
  Rect cell{kLeft, y, kChannelW, kRowH};
  c.hit(cell, kHitChannel + lineIndex);
  drawChannelCell(c, g, cell, rowFocused);

  const std::vector<Listing>* v = li_ ? li_->forChannel(g.id) : nullptr;
  time::Seconds ws = windowStart_, we = windowEnd();
  time::Seconds now = c.now;
  bool drewAny = false;
  int focusedProgram = rowFocused && v ? programIndex(*v, focusTime_) : -1;
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
      Rect r{x0, y, x1 - x0 - kGap, kRowH};
      c.hit(r, kHitProgram + lineIndex * 64 + std::min(idx, 63));
      if (focused) {
        Rect big{r.x - r.w * 0.015f, r.y - 1, r.w * 1.03f, r.h + 2};
        c.g.fill(big, theme::white(), 12);
        c.g.stroke({big.x - 6, big.y - 6, big.w + 12, big.h + 12}, theme::white(), 16, 3);
      } else {
        c.g.fill(r, l.isOn(now) ? theme::white(0.15f) : theme::white(0.07f), 12);
      }
      if (r.w >= kTextCell) {
        Color fg = focused ? theme::black() : theme::white();
        Color sub = focused ? theme::black(0.65f) : theme::white(0.7f);
        std::string title = (l.start < ws ? "\xe2\x97\x80 " : "") + l.title;
        ui::label(c, title, r.x + 16, r.y + 14, Weight::Bold, 21, fg, r.w - 32);
        std::string second = !l.episode.empty() ? l.episode : ui::range(c, l.start, l.end);
        ui::label(c, second, r.x + 16, r.y + 47, Weight::Regular, 17, sub, r.w - 32);
      }
    }
  }
  if (!drewAny) {
    // One cell for the whole row.
    Rect r{kProgX, y, kGridW - kGap, kRowH};
    bool focused = rowFocused;
    c.hit(r, kHitProgram + lineIndex * 64);
    c.g.fill(r, focused ? theme::white() : theme::white(0.07f), 12);
    bool askedEmpty = li_ && li_->empty.count(g.id);
    if (!later() && !v && !askedEmpty) {
      // Still loading: cell-shaped placeholders (a title and a time bar
      // each) in a slow pulse; "No guide info" only when there's none.
      skeletonShown_ = true;
      ui::Color block = ui::skeleton(c);
      if (focused) block = theme::black(block.a * 1.6f);
      static const float kShare[] = {0.42f, 0.33f, 0.25f};
      float cx = r.x;
      for (float share : kShare) {
        float cw = r.w * share;
        c.g.fill({cx + 18, r.y + kRowH * 0.5f - 18, std::min(cw - 36, 260.0f), 16}, block, 8);
        c.g.fill({cx + 18, r.y + kRowH * 0.5f + 8, std::min(cw * 0.4f, 110.0f), 12}, block, 6);
        cx += cw;
        if (cx < r.x + r.w - 4) c.g.fill({cx - 1, r.y + 10, 2, kRowH - 20}, theme::black(0.25f));
      }
    } else {
      std::string text = later() ? "No listings for this time" : "Live \xc2\xb7 No guide info";
      c.g.fill({r.x + 18, r.y + kRowH * 0.5f - 6, 12, 12}, theme::hex(theme::kNowLine), 6);
      ui::labelIn(c, text, {r.x + 40, r.y, r.w - 60, r.h}, Weight::Bold, 21, focused ? theme::black() : theme::white(0.8f));
    }
  }
}

void GuideScreen::drawChannelCell(ui::Ctx& c, const GuideChannel& g, Rect r, bool focused) {
  c.g.fill(r, focused ? theme::white() : theme::white(0.07f), 14);
  Color fg = focused ? theme::black() : theme::white();
  Color sub = focused ? theme::black(0.65f) : theme::white(0.65f);
  switch (g.style) {
    case GuideChannel::Style::LogoOnly:
      ui::logo(c, g.logoUrl, g.name, {r.x + 14, r.y + 12, r.w - 28, r.h - 24}, focused);
      break;
    case GuideChannel::Style::Local:
      ui::logo(c, g.logoUrl, "", {r.x + 12, r.y + 24, 58, 36}, focused);
      ui::label(c, g.name, r.x + 80, r.y + 18, Weight::Bold, 20, fg, r.w - (g.favorite ? 108 : 88));
      ui::label(c, g.subtitle, r.x + 80, r.y + 46, Weight::Regular, 15, sub, r.w - 88);
      break;
    case GuideChannel::Style::Named: {
      bool hasLogo = !g.logoUrl.empty();
      float tx = r.x + (hasLogo ? 80 : 14);
      if (hasLogo) ui::logo(c, g.logoUrl, "", {r.x + 12, r.y + 24, 58, 36}, focused);
      // Leave room for the favourite star in the corner.
      float right = r.x + r.w - (g.favorite ? 28 : 10);
      std::vector<std::string> lines = c.t.wrap(g.name, Weight::Bold, 18, right - tx, 2);
      float ty = r.y + (lines.size() > 1 ? 10 : 18);
      for (const std::string& s : lines) {
        ui::label(c, s, tx, ty, Weight::Bold, 18, fg);
        ty += 23;
      }
      if (lines.size() < 2) ui::label(c, g.subtitle, tx, ty + 2, Weight::Regular, 15, sub, r.x + r.w - 10 - tx);
      break;
    }
  }
  if (g.favorite) ui::label(c, "\xe2\x98\x85", r.x + r.w - 24, r.y + 6, Weight::Bold, 16, theme::hex(theme::kStar));
  if (!g.partner.empty()) {
    // A channel with both versions: blue EAST or amber WEST.
    float w = ui::chipWidth(c, g.west ? "WEST" : "EAST", 12);
    ui::coastChip(c, g.west, r.x + r.w - w - 8, r.y + r.h - 24, 12);
  } else if (g.west) {
    float w = ui::chipWidth(c, "WEST", 12);
    ui::chip(c, "WEST", r.x + r.w - w - 8, r.y + r.h - 24, 12, focused ? theme::black(0.15f) : theme::white(0.2f), fg);
  }
}

void GuideScreen::drawRail(ui::Ctx& c) {
  Rect r{kLeft - 10, kRulerY + 40, 380, std::min(640.0f, kGridBottom - kRulerY - 40)};
  c.g.fill(r, theme::hex(0x1C1F25, 0.97f), theme::kPanelRadius);
  ui::label(c, "JUMP TO", r.x + 24, r.y + 20, Weight::Bold, 16, theme::white(0.55f));
  float y = r.y + 56;
  float rowH = 54;
  int visible = static_cast<int>((r.h - 70) / rowH);
  int first = std::clamp(railIndex_ - visible / 2, 0, std::max(0, static_cast<int>(sections_.size()) - visible));
  for (int i = first; i < static_cast<int>(sections_.size()) && i < first + visible; ++i) {
    const Section& s = sections_[static_cast<size_t>(i)];
    Rect pill{r.x + 14, y, r.w - 28, rowH - 6};
    bool focused = i == railIndex_;
    c.hit(pill, kHitRail + i);
    if (focused) c.g.fill(pill, theme::white(), (rowH - 6) * 0.5f);
    Color fg = focused ? theme::black() : theme::white();
    float x = pill.x + 20;
    if (i == railOrigin_) {
      c.g.fill({x, pill.y + pill.h * 0.5f - 5, 10, 10}, theme::hex(theme::kNowLine), 5);
      x += 20;
    }
    ui::labelIn(c, s.title, {x, pill.y, pill.w - 90, pill.h}, Weight::Bold, 22, fg);
    ui::labelIn(c, std::to_string(s.count), {pill.x, pill.y, pill.w - 20, pill.h}, Weight::Medium, 19,
                focused ? theme::black(0.6f) : theme::white(0.6f), Align::Right);
    y += rowH;
  }
}

void GuideScreen::drawDigits(ui::Ctx& c) {
  if (digits_.empty()) return;
  Rect r{gfx::kLayoutWidth - 80 - 220, 330, 220, 90};
  c.g.fill(r, theme::hex(0x1C1F25, 0.96f), 20);
  ui::labelIn(c, digits_, r, Weight::Bold, 54, theme::white(), Align::Center);
}

}  // namespace marigold::app
