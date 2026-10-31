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

// Geometry from LiveTVGuideView (points = layout units), right of the left
// rail: the hero stays at kHeroX; the timeline and grid start there at rest
// and are pushed right while the neighborhood column is open. With no tab
// row, the timeline sits right under the hero.
constexpr float kHeroX = left_rail::kRestLeft;
constexpr float kRight = gfx::kLayoutWidth - 80;  // the grid's right edge
constexpr float kChannelW = 182;
constexpr float kChannelCol = 190;  // the channel cell and its gap
constexpr float kGap = 8;
constexpr int kRestMinutes = 180;
constexpr int kColumnMinutes = 120;  // a minute stays about 10 px wide
constexpr float kRowH = 84;
constexpr float kHeaderH = 22;
constexpr float kRulerY = 340;
constexpr float kGridTop = 392;
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

// Pointer ids (the rail's are left_rail::kHit*, from 200).
constexpr int kHitTime = 50;
constexpr int kHitFilter = 51;
constexpr int kHitChannel = 1000;
constexpr int kHitProgram = 100000;  // + line * 64 + program

const char* tabTitle(int t) {
  static const char* kTitles[] = {"My Lineup", "Favorites", "Recent", "Live Events", "All Channels"};
  return kTitles[t];
}

// The guide's tabs are the rail's guide sections.
rail::Item itemOf(engine::views::Tab t) {
  using Tab = engine::views::Tab;
  switch (t) {
    case Tab::Favorites: return rail::Item::Favorites;
    case Tab::Recent: return rail::Item::Recent;
    case Tab::Events: return rail::Item::Events;
    case Tab::All: return rail::Item::All;
    case Tab::Lineup: break;
  }
  return rail::Item::Lineup;
}

engine::views::Tab tabOf(rail::Item i) {
  using Tab = engine::views::Tab;
  switch (i) {
    case rail::Item::Favorites: return Tab::Favorites;
    case rail::Item::Recent: return Tab::Recent;
    case rail::Item::All: return Tab::All;
    default: return Tab::Lineup;
  }
}

bool inRange(int id, int base) { return id >= base && id < base + 100; }

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
time::Seconds GuideScreen::windowEnd() const { return windowStart_ + windowMinutes() * 60; }
int GuideScreen::windowMinutes() const { return narrow_ ? kColumnMinutes : kRestMinutes; }
float GuideScreen::progX() const { return left_ + kChannelCol; }

float GuideScreen::pxPerSec() const {
  float left = narrow_ ? left_rail::kPushLeft : left_rail::kRestLeft;
  return (kRight - left - kChannelCol) / (static_cast<float>(windowMinutes()) * 60.0f);
}

time::Seconds GuideScreen::drawEnd() const {
  return windowStart_ + static_cast<time::Seconds>((kRight - progX()) / pxPerSec());
}

float GuideScreen::xAt(time::Seconds t) const { return progX() + static_cast<float>(t - windowStart_) * pxPerSec(); }

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
  bool all = s.settings().allChannelsTab;
  nav_.setItems(rail::items(all));
  if (tab_ == Tab::All && !all) tab_ = Tab::Lineup;
  nav_.setSection(itemOf(tab_));
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

int GuideScreen::lineOf(const std::string& id) const {
  if (!ch_) return -1;
  for (size_t i = 0; i < lines_.size(); ++i) {
    if (lines_[i].kind == Line::Kind::Channel && ch_->all[static_cast<size_t>(lines_[i].channel)].id == id)
      return static_cast<int>(i);
  }
  return -1;
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
  // Above the first row: the time button (the filter is beside it).
  if (step < 0) area_ = Area::Time;
}

void GuideScreen::moveProgram(int step) {
  const GuideChannel* g = focusedChannel();
  const std::vector<Listing>* v = g && li_ ? li_->forChannel(g->id) : nullptr;
  int cur = v && !v->empty() ? programIndex(*v, focusTime_) : -1;
  float perSec = pxPerSec();
  auto visibleWidth = [&](const Listing& l) {
    time::Seconds a = std::max(l.start, windowStart_), b = std::min(l.end, windowEnd());
    return static_cast<float>(b - a) * perSec;
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
  leftEdge();
}

// ---------------------------------------------------------------- left rail

int GuideScreen::originSection() const {
  int origin = 0;
  for (size_t i = 0; i < sections_.size(); ++i) {
    if (sections_[i].firstLine < focusLine_) origin = static_cast<int>(i);
  }
  return origin;
}

// Left from the first show at Now: the column on this neighborhood (the
// menu where there is none, as on Recent).
void GuideScreen::leftEdge() {
  place_ = {focusLine_, focusId_, windowStart_, focusTime_, scroll_};
  nav_.leftEdge(originSection(), static_cast<int>(sections_.size()));
  syncSlides();
}

void GuideScreen::openColumn(bool fromMenu) {
  place_ = {focusLine_, focusId_, windowStart_, focusTime_, scroll_};
  area_ = Area::Grid;
  nav_.openColumn(originSection(), static_cast<int>(sections_.size()), fromMenu);
  syncSlides();
}

// Exactly where the focus was when the column opened: row, show, time and
// scroll (the channel's row again if the list was rebuilt meanwhile).
void GuideScreen::restorePlace() {
  focusLine_ = place_.line;
  const GuideChannel* g = focusLine_ < static_cast<int>(lines_.size()) ? focusedChannel() : nullptr;
  if (!g || g->id != place_.id) {
    int found = lineOf(place_.id);
    if (found >= 0) focusLine_ = found;
    else focusLine_ = std::min(std::max(focusLine_, 0), static_cast<int>(lines_.size()) - 1);
  }
  if (const GuideChannel* now = focusedChannel()) focusId_ = now->id;
  windowStart_ = std::max(place_.window, nowWindow());
  focusTime_ = place_.time;
  scroll_ = place_.scroll;
  ensureVisible();
  requestVisibleListings();
}

void GuideScreen::apply(const rail::Step& s, bool fromMenu) {
  using Do = rail::Step::Do;
  int n = static_cast<int>(sections_.size());
  switch (s.what) {
    case Do::None: break;
    case Do::Scroll:
      // The grid scrolls behind the column, as it did behind JUMP TO.
      if (s.index < n) {
        focusLine_ = std::min(static_cast<int>(lines_.size()) - 1, sections_[static_cast<size_t>(s.index)].firstLine + 1);
        if (const GuideChannel* g = focusedChannel()) focusId_ = g->id;
        ensureVisible();
        requestVisibleListings();
      }
      break;
    case Do::Restore: restorePlace(); break;
    case Do::Top:
      // Another neighborhood: its first channel, at Now.
      if (s.index < n) {
        focusLine_ = std::min(static_cast<int>(lines_.size()) - 1, sections_[static_cast<size_t>(s.index)].firstLine + 1);
        if (const GuideChannel* g = focusedChannel()) focusId_ = g->id;
        windowStart_ = nowWindow();
        focusTime_ = time::now();
        ensureVisible();
        requestVisibleListings();
      }
      break;
    case Do::Open: open(s.item, fromMenu); break;
    case Do::Home: showSection(Tab::Lineup); break;
    case Do::Exit: backOut(); break;
  }
  area_ = Area::Grid;
  syncSlides();
}

// A strip item, from the menu or a click on the strip: a section lands on
// its column (Recent on its grid); the others open their screens as before.
void GuideScreen::open(rail::Item item, bool fromMenu) {
  switch (item) {
    case rail::Item::Search: openSearch(); break;
    case rail::Item::MySports: app_.nav().push(makeMySports(app_)); break;
    case rail::Item::Options: openOptions(); break;
    case rail::Item::Events: openEvents(true); break;
    default:
      showSection(tabOf(item));
      if (rail::hasColumn(item)) openColumn(fromMenu);
      break;
  }
}

void GuideScreen::showSection(Tab t) {
  nav_.setSection(itemOf(t));
  area_ = Area::Grid;
  if (t == tab_) return;
  tab_ = t;
  filter_ = Filter::None;
  // Recent opens newest first; the others keep the channel when it's there.
  if (t == Tab::Recent) focusId_.clear();
  rebuildLines();
  requestVisibleListings();
}

// What Live Events' strip or menu picked for the guide.
void GuideScreen::fromEvents(rail::Item item, bool column) {
  if (item == rail::Item::Options) {
    openOptions();
    return;
  }
  if (!rail::isSection(item) || item == rail::Item::Events) return;
  nav_.close();
  showSection(tabOf(item));
  if (column && rail::hasColumn(item)) openColumn(true);
  syncSlides();
}

// A pointer click (OK while the pointer is on something). True when the
// rail took it; the content's own clicks are OK as before (hovering moved
// the focus there).
bool GuideScreen::click() {
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
    // A section's icon: its column (Recent: its grid). An open column is
    // put back first.
    if (mode == rail::Mode::Column) restorePlace();
    apply(nav_.pick(item(left_rail::kHitStrip)), false);
    return true;
  }
  if (mode != rail::Mode::Column) return id == left_rail::kHitNothing;
  if (inRange(id, left_rail::kHitColumn)) {
    apply(nav_.clickColumn(id - left_rail::kHitColumn), false);
  } else if (id >= kHitChannel) {
    // The grid: the column closes and that show takes the focus.
    nav_.close();
    syncSlides();
    hover(id);
  }
  return true;
}

// Back to the content as it is, for keys that act there whatever is open
// (CH paging, Live Events, a channel number): an open column is cancelled.
void GuideScreen::closeRail() {
  if (nav_.mode() == rail::Mode::Column) restorePlace();
  nav_.close();
  syncSlides();
}

void GuideScreen::syncSlides() {
  uint32_t ms = app_.ms();
  push_.go(nav_.mode() == rail::Mode::Column ? 1.0f : 0.0f, ms);
  menu_.go(nav_.mode() == rail::Mode::Menu ? 1.0f : 0.0f, ms);
  settleSlides(ms);
  app_.invalidate();
}

// Where the timeline and grid are in the push; the window re-times to two
// hours (or back to three) once it lands.
void GuideScreen::settleSlides(uint32_t ms) {
  left_ = left_rail::kRestLeft + (left_rail::kPushLeft - left_rail::kRestLeft) * push_.at(ms);
  if (push_.landed(ms)) narrow_ = push_.target() > 0.5f;
}

hints::Rail GuideScreen::railHint() const {
  if (nav_.mode() == rail::Mode::Column) return hints::Rail::Column;
  return rail::hasColumn(nav_.section()) ? hints::Rail::Content : hints::Rail::NoColumn;
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
      // Whatever the rail had open closes (an open column without going
      // back: the focus moves on).
      nav_.close();
      syncSlides();
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
        app.toast("Hidden. Settings \xe2\x80\xba Guide \xe2\x80\xba Groups in All Channels shows it again");
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
    // Never how many channels each holds.
    auto add = [&](const std::string& title, Filter f, const std::string& section) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, title};
      it.on = self->filter_ == f && ((f != Filter::Section && f != Filter::Group) || self->filterSection_ == section);
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
    add("All channels", Filter::None, "");
    p.items.push_back(ui::PanelItem::header("ON NOW"));
    for (const engine::views::Count& n : engine::views::onNowCounts(*self->ch_, rows, self->li_.get(), time::now()))
      add(n.title, n.filter.kind, "");
    p.items.push_back(ui::PanelItem::header("NEIGHBORHOODS"));
    for (const engine::views::Count& n : engine::views::sectionCounts(*self->ch_, rows))
      add(n.title, Filter::Section, n.title);
    if (self->tab_ == Tab::All) {
      std::vector<GroupSummary> groups = app.store().groups();
      p.items.push_back(ui::PanelItem::header("PROVIDER GROUPS (" + std::to_string(groups.size()) + ")"));
      for (const GroupSummary& g : groups) {
        add(g.name, Filter::Group, g.name);
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

void GuideScreen::openOptions() { openAllSettings(app_); }

void GuideScreen::openQuickSettings() { app_.nav().push(makeQuickSettings(app_, QuickPlace::Guide)); }

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
      self->nav_.setSection(itemOf(self->tab_));
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

void GuideScreen::openEvents(bool sports) {
  // The board is its own screen over the guide, with the same strip; what
  // its strip picks for the guide comes back here. Yellow there closes it
  // onto the guide as it was.
  closeRail();
  area_ = Area::Grid;
  GuideScreen* self = this;
  app_.nav().push(makeEventsBoard(app_, [self](rail::Item item, bool column) { self->fromEvents(item, column); }, sports));
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
  settleSlides(app_.ms());
  Key k = e.key;
  // Other keys hide the pointer, and its target with it.
  if (k != Key::Ok) pointer_ = 0;
  if (!ch_ || ch_->all.empty()) {
    // Status screen: one button.
    const StoreStatus& st = app_.store().status();
    if (k == Key::Ok && st.phase == StoreStatus::Phase::NoAccounts) app_.nav().push(makeAccounts(app_, false));
    else if (k == Key::Ok && st.phase != StoreStatus::Phase::Loading) app_.store().refreshAll();
    else if (k == Key::Blue) openQuickSettings();
    else if (k == Key::Back) backOut();
    return true;
  }
  int d = digitOf(k);
  if (d >= 0) {
    digit(d);
    return true;
  }
  rail::Mode mode = nav_.mode();
  if (e.longPress) {
    bool onRail = inRange(pointer_, left_rail::kHitStrip) || pointer_ == left_rail::kHitNothing;
    if (area_ == Area::Grid && mode == rail::Mode::Content && !onRail) longPress();
    return true;
  }
  // A click: the rail's targets first (the strip, the menu, the column).
  if (k == Key::Ok && pointer_ && click()) return true;
  // Keys a remote without colour or CH keys has (Fire TV): play/pause
  // opens Live Events, rewind and fast forward page.
  if (k == Key::PlayPause || k == Key::Play) k = Key::Yellow;
  if (k == Key::Rewind) k = Key::ChannelUp;
  if (k == Key::FastForward) k = Key::ChannelDown;
  // The colour keys do what they did before the rail in every state; with
  // the column open they act on the channel behind it, as in JUMP TO.
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
    case Key::Blue: openQuickSettings(); return true;
    case Key::ChannelUp:
    case Key::ChannelDown: {
      // A page of rows at a time.
      closeRail();
      for (int i = 0; i < 5; ++i) moveRow(k == Key::ChannelUp ? -1 : 1);
      area_ = Area::Grid;
      requestVisibleListings();
      return true;
    }
    default: break;
  }

  // The column and the menu: the key path is the rail's (Up/Down from a
  // swipe or the wheel walk the list).
  if (mode != rail::Mode::Content) {
    apply(nav_.key(k), true);
    return true;
  }

  if (area_ == Area::Time || area_ == Area::Filter) {
    switch (k) {
      case Key::Ok:
        if (area_ == Area::Time) openTimeMenu();
        else openFilter();
        return true;
      case Key::Right:
        area_ = Area::Filter;
        return true;
      case Key::Left:
        // Full left from the time button: the menu.
        if (area_ == Area::Filter) {
          area_ = Area::Time;
        } else {
          nav_.openMenu();
          syncSlides();
        }
        return true;
      case Key::Down:
      case Key::Back:
        if (focusLine_ >= 0) area_ = Area::Grid;
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
      if (focusLine_ >= 0 && lines_[static_cast<size_t>(focusLine_)].kind == Line::Kind::Prompt) {
        leftEdge();
        return true;
      }
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
      // Nothing left to undo: the menu (Back there goes to My Lineup, then
      // out).
      nav_.openMenu();
      syncSlides();
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
  pointer_ = id;
  rail::Mode mode = nav_.mode();
  if (mode == rail::Mode::Menu) {
    // Hovering highlights; a click picks, or closes it outside.
    if (inRange(id, left_rail::kHitMenu)) nav_.hoverMenu(id - left_rail::kHitMenu);
    return;
  }
  // The strip shows the icon's name and moves nothing; with the column open
  // the grid only takes the focus on a click.
  if (id < kHitTime || (id >= left_rail::kHitStrip && id < left_rail::kHitEnd)) return;
  if (mode == rail::Mode::Column) return;
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
  if (id == kHitTime) area_ = Area::Time;
  else if (id == kHitFilter) area_ = Area::Filter;
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
  // The column's push and the menu's slide.
  settleSlides(ms);
  if (push_.moving(ms) || menu_.moving(ms)) animating = true;
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
  if (onTop && g && area_ == Area::Grid && nav_.mode() == rail::Mode::Content && s.settings().previewChannels) {
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
  // Under everything: the pointer off any target (the strip's pill goes).
  c.hit({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, left_rail::kHitNothing);
  settleSlides(c.ms);
  drawHero(c);
  drawPreview(c);
  drawRuler(c);
  drawGrid(c);
  float column = push_.at(c.ms);
  if (column > 0.01f) drawColumn(c, column);
  const GuideChannel* g = focusedChannel();
  ui::keyBar(c, hints::guide(app_.remoteKeys(), g && g->inLineup, railHint()), left_);
  // The strip; the pointer on an icon shows its name.
  int hover = -1;
  if (c.pointerActive && nav_.mode() != rail::Mode::Menu && inRange(pointer_, left_rail::kHitStrip))
    hover = pointer_ - left_rail::kHitStrip;
  left_rail::strip(c, nav_.items(), nav_.section(), hover);
  float menu = menu_.at(c.ms);
  if (menu > 0.01f) left_rail::menu(c, nav_.items(), nav_.section(), nav_.menuFocus(), menu);
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
      message = st.problem.empty() ? "Check the provider in Settings." : st.problem;
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
  float x = kHeroX, y = 48;
  float w = 1240;
  float right = x + w;
  float chipsLeft = drawChips(c, right);
  if (g) {
    ui::label(c, g->section, x, y, Weight::Bold, 22, theme::white(0.6f));
  } else if (focusLine_ >= 0 && lines_[static_cast<size_t>(focusLine_)].kind == Line::Kind::Prompt) {
    ui::label(c, "My Lineup", x, y, Weight::Bold, 22, theme::white(0.6f));
  }
  float sx = x + (g ? ui::textWidth(c, g->section, Weight::Bold, 22) + 24 : 0);
  drawHeaderLine(c, sx, y, chipsLeft - 16);
  y += 40;
  if (!g) {
    if (focusLine_ >= 0 && lines_[static_cast<size_t>(focusLine_)].kind == Line::Kind::Prompt) {
      ui::label(c, "Make Live TV yours", x, y + 10, Weight::Bold, 44, theme::white());
      ui::paragraph(c,
                    "Set up My Lineup: your local stations and the channels you watch, in one short "
                    "guide instead of every channel your provider has.",
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

float GuideScreen::drawChips(ui::Ctx& c, float right) {
  float y = 46;
  if (later()) {
    float bw = ui::chipWidth(c, "BACK returns to Now", 18);
    right -= bw;
    ui::chip(c, "BACK returns to Now", right, y + 1, 18, theme::hex(theme::kNowLine, 0.35f), theme::white());
    right -= 12;
  }
  if (filter_ != Filter::None) {
    std::string f = (filter_ == Filter::Section || filter_ == Filter::Group) ? filterSection_
                    : filter_ == Filter::SportsNow ? "Sports on now"
                    : filter_ == Filter::NewsNow ? "News on now"
                    : filter_ == Filter::MoviesNow ? "Movies on now"
                                                   : "Kids on now";
    std::string text = "\xe2\x97\x8f " + f + "  \xc3\x97";
    right -= ui::chipWidth(c, text, 19);
    ui::chip(c, text, right, y, 19, theme::hex(theme::kNowLine, 0.3f), theme::white());
  }
  return right;
}

void GuideScreen::drawRuler(ui::Ctx& c) {
  // Time button: "Tonight ▼", and the filter beside it.
  std::string word;
  if (!later()) word = "Now";
  else {
    word = ui::dayWord(c, windowStart_);
    if (word.empty()) word = "Today";
  }
  word += " \xe2\x96\xbc";
  bool focused = area_ == Area::Time && nav_.mode() == rail::Mode::Content;
  float w = std::max(104.0f, ui::chipWidth(c, word, 19) + 10);
  Rect r{left_, kRulerY, w, 40};
  c.hit(r, kHitTime);
  c.g.fill(r, focused ? theme::white() : theme::white(0.1f), 20);
  ui::labelIn(c, word, r, Weight::Bold, 19, focused ? theme::black() : theme::white(), Align::Center);
  top_bar::filterButton(c, {r.x + r.w + 10, kRulerY, 40, 40}, area_ == Area::Filter && nav_.mode() == rail::Mode::Content,
                        filter_ != Filter::None, kHitFilter);

  c.g.fill({progX(), kRulerY + 39, kRight - progX(), 1}, theme::white(0.15f));
  for (time::Seconds t = windowStart_; xAt(t) + 60 < kRight; t += 1800)
    ui::label(c, ui::clock(c, t, false), xAt(t) + 8, kRulerY + 8, Weight::Medium, 18, theme::white(0.6f));
}

void GuideScreen::drawGrid(ui::Ctx& c) {
  if (lines_.empty()) {
    std::string message = tab_ == Tab::Events      ? "The Live Events board arrives in a later build."
                          : tab_ == Tab::Favorites ? "No favorites yet. Hold OK on a channel to add one."
                          : tab_ == Tab::Recent    ? "Channels you watch show up here."
                          : filter_ != Filter::None ? "Nothing matches this filter. Back clears it."
                                                    : "No channels here.";
    ui::label(c, message, left_, kGridTop + 40, Weight::Medium, 26, theme::white(0.6f));
    return;
  }
  // Clipped at the grid's left edge, so it never runs under the column.
  float clipX = left_ - 10;
  Rect clip{clipX, kGridTop - 6, kRight + 20 - clipX, kGridBottom - kGridTop + 12};
  c.g.pushClip(clip);
  for (size_t i = 0; i < lines_.size(); ++i) {
    float y = kGridTop + lines_[i].y - scroll_;
    if (y > kGridBottom || y + kRowH < kGridTop) continue;
    drawRow(c, static_cast<int>(i), y);
  }
  c.g.popClip();
  // Now line.
  time::Seconds now = c.now;
  if (now >= windowStart_ && now < drawEnd()) {
    float x = std::round(xAt(now));
    c.g.fill({x - 1.5f, kRulerY + 40, 3, kGridBottom - kRulerY - 40}, theme::hex(theme::kNowLine));
  }
}

void GuideScreen::drawRow(ui::Ctx& c, int lineIndex, float y) {
  const Line& line = lines_[static_cast<size_t>(lineIndex)];
  bool rowFocused = area_ == Area::Grid && nav_.mode() == rail::Mode::Content && lineIndex == focusLine_;
  if (line.kind == Line::Kind::Header) {
    ui::label(c, line.title, left_, y, Weight::Bold, 17, theme::white(0.5f));
    return;
  }
  if (line.kind == Line::Kind::Prompt) {
    Rect r{left_, y, kRight - left_, kRowH};
    c.hit(r, kHitChannel + lineIndex);
    c.g.fill(r, rowFocused ? theme::white() : theme::white(0.09f), 14);
    Color fg = rowFocused ? theme::black() : theme::white();
    ui::label(c, "Make Live TV yours", r.x + 28, r.y + 14, Weight::Bold, 24, fg);
    ui::label(c, "Set up My Lineup with your local stations and favourite channels.", r.x + 28, r.y + 48,
              Weight::Regular, 18, rowFocused ? theme::black(0.65f) : theme::white(0.65f));
    float cw = ui::chipWidth(c, "Set up My Lineup", 20) + 20;
    Rect chipR{r.x + r.w - cw - 24, r.y + 21, cw, 42};
    c.g.fill(chipR, rowFocused ? theme::black() : theme::white(), 21);
    ui::labelIn(c, "Set up My Lineup", chipR, Weight::Bold, 20, rowFocused ? theme::white() : theme::black(),
                Align::Center);
    return;
  }
  const GuideChannel& g = ch_->all[static_cast<size_t>(line.channel)];
  Rect cell{left_, y, kChannelW, kRowH};
  c.hit(cell, kHitChannel + lineIndex);
  drawChannelCell(c, g, cell, rowFocused);

  const std::vector<Listing>* v = li_ ? li_->forChannel(g.id) : nullptr;
  time::Seconds ws = windowStart_, we = drawEnd();
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
    Rect r{progX(), y, kRight - progX() - kGap, kRowH};
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

void GuideScreen::drawColumn(ui::Ctx& c, float alpha) {
  // The neighborhoods, inside the guide; no counts.
  std::vector<left_rail::Entry> entries;
  entries.reserve(sections_.size());
  for (const Section& s : sections_) entries.push_back({s.title, 0});
  int hover = c.pointerActive && inRange(pointer_, left_rail::kHitColumn) ? pointer_ - left_rail::kHitColumn : -1;
  left_rail::column(c, rail::title(nav_.section()), "Jump to", entries, nav_.index(), nav_.origin(), hover, kRulerY,
                    kGridTop, alpha);
}

void GuideScreen::drawDigits(ui::Ctx& c) {
  if (digits_.empty()) return;
  Rect r{gfx::kLayoutWidth - 80 - 220, 330, 220, 90};
  c.g.fill(r, theme::hex(0x1C1F25, 0.96f), 20);
  ui::labelIn(c, digits_, r, Weight::Bold, 54, theme::white(), Align::Center);
}

}  // namespace marigold::app
