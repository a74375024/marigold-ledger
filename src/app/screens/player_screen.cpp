// Full screen playback (LiveTVOverlay): the tuning and switching cards, the
// info banner with its key guide, the action row (Sources, Guide, last
// channel, Favorite, More), the backup card, the "nothing is working" card,
// the one-time "Getting around" card, channel flipping, previous channel and
// number keys. With the Magic Remote pointer the row and two edge buttons
// (Guide, last channel) are clickable.
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr uint32_t kBannerDelayMs = 1500;
constexpr uint32_t kBannerMs = 6000;
constexpr uint32_t kRowIdleMs = 10000;  // the action row hides after this long untouched
constexpr uint32_t kBackupCardMs = 8000;
constexpr uint32_t kDigitsMs = 1500;
constexpr float kLeft = 96;
constexpr float kBannerBottom = 870;  // the banner's lower edge
constexpr float kRowY = 952;          // the key guide or the action row
constexpr uint32_t kRed = 0xE5372B, kBlue = 0x3D7BFF, kAmber = 0xF5A623, kGreen = 0x34C759;

// Pointer targets.
constexpr int kHitPill = 1;      // + pill index
constexpr int kHitGuide = 50, kHitLast = 51;
constexpr int kHitFail = 60;     // + button index
constexpr int kHitTips = 70;

class PlayerScreen : public Screen {
  bool cameras_ = false;  // the race on screen has cameras (Down opens them)

 public:
  explicit PlayerScreen(App& app) : Screen(app) { bannerFrom_ = app.ms() + kBannerDelayMs; }

  bool key(const KeyEvent& e) override {
    Playback& p = app_.playback();
    if (tips_) {
      // "Getting around": OK or Back closes it; other keys wait.
      if (e.key == Key::Ok || e.key == Key::Back) closeTips();
      return true;
    }
    if (p.failed()) return failedKey(e);
    if (zone_ == Zone::Row) return rowKey(e);
    if (zone_ == Zone::EdgeGuide || zone_ == Zone::EdgeLast) {
      if (e.key == Key::Ok) {
        Zone z = zone_;
        zone_ = Zone::None;
        if (z == Zone::EdgeGuide) openGuide();
        else goPrevious();
        return true;
      }
      zone_ = Zone::None;
    }
    int d = digitOf(e.key);
    if (d >= 0) {
      if (digits_.size() >= 4) digits_.clear();
      digits_ += static_cast<char>('0' + d);
      digitsAt_ = app_.ms();
      return true;
    }
    switch (e.key) {
      case Key::Up:
      case Key::ChannelUp:
        flipped(p.flip(-1));
        return true;
      case Key::Down:
        // The banner is up: Down goes to its action row.
        if (bannerVisible() && p.ready()) {
          focusRow();
          return true;
        }
        // A race with cameras: Down opens them (channel keys still flip).
        if (cameras_) {
          app_.nav().push(makeCameraPanel(app_));
          return true;
        }
        flipped(p.flip(1));
        return true;
      case Key::ChannelDown:
        flipped(p.flip(1));
        return true;
      case Key::Right:
        goPrevious();
        return true;
      case Key::Left:
        openGuide();
        return true;
      case Key::Info:
      case Key::Ok:
        if (bannerVisible()) bannerUntil_ = 0;
        else showBanner();
        return true;
      case Key::Red:
        openSources();
        return true;
      case Key::Blue:
        app_.nav().push(makeOptions(app_));
        return true;
      case Key::Back:
        if (!digits_.empty()) {
          digits_.clear();
          return true;
        }
        app_.nav().pop();
        return true;
      default:
        return false;
    }
  }

  void hover(int id) override {
    if (id >= kHitPill && id < kHitPill + static_cast<int>(pills_.size())) {
      zone_ = Zone::Row;
      pill_ = id - kHitPill;
      touch();
    } else if (id == kHitGuide) {
      zone_ = Zone::EdgeGuide;
    } else if (id == kHitLast) {
      zone_ = Zone::EdgeLast;
    } else if (id >= kHitFail && id < kHitFail + 3) {
      failButton_ = id - kHitFail;
    }
  }

  bool tick(uint32_t ms) override {
    Playback& p = app_.playback();
    bool opening = !p.ready() && !p.failed();
    if (!p.active()) {
      app_.nav().pop();
      return false;
    }
    if (p.entry() != shownEntry_) {
      shownEntry_ = p.entry();
      bannerFrom_ = ms + kBannerDelayMs;
      bannerUntil_ = bannerFrom_ + kBannerMs;
      zone_ = Zone::None;
      cameras_ = app_.store().isEvent(shownEntry_) && app_.store().hasCameras(shownEntry_);
      if (cameras_) app_.toast("Press Down for onboard cameras", true);
    }
    // The first picture ever: the one-time "Getting around" card.
    if (p.ready() && !p.failed() && !tips_ && !app_.store().settings().playerTipsShown) {
      tips_ = true;
      Settings s = app_.store().settings();
      s.playerTipsShown = true;
      app_.store().setSettings(s);
    }
    if (zone_ == Zone::Row && ms - rowTouched_ > kRowIdleMs) {
      zone_ = Zone::None;
      bannerUntil_ = ms;
    }
    if (const auto& b = p.backupNotice(); b && ms - b->at > kBackupCardMs) p.dismissBackupNotice();
    if (!digits_.empty() && ms - digitsAt_ >= kDigitsMs) {
      int n = std::atoi(digits_.c_str());
      digits_.clear();
      std::string target;
      auto ch = app_.store().channels();
      for (const GuideChannel& g : ch->all) {
        if (g.number && *g.number == n) {
          target = g.id;
          break;
        }
      }
      if (target.empty()) app_.toast("No channel " + std::to_string(n), true);
      else flipped(p.tune(target) ? target : std::string());
    }
    bool bannerAnim = ms < bannerUntil_ + 50;
    bool note = !p.note().empty();
    return opening || bannerAnim || note || !digits_.empty() || p.backupNotice().has_value() || zone_ == Zone::Row;
  }

  void draw(ui::Ctx& c) override {
    Playback& p = app_.playback();
    auto ch = app_.store().channels();
    const GuideChannel* g = ch->find(p.entry());
    if (!g) {
      // A game: show the channel it plays on.
      if (!p.feeds().empty()) g = ch->find(app_.store().entryOfChannel(p.feeds()[std::min(p.feedIndex(), p.feeds().size() - 1)].channelId));
    }
    pointer_ = c.pointerActive;
    if (p.failed()) {
      drawFailed(c, g);
    } else if (!p.ready()) {
      drawTuning(c, g);
    } else {
      c.g.hole({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight});
      bool banner = bannerVisible() || zone_ == Zone::Row || pointer_;
      if (banner) {
        drawScrim(c);
        drawBanner(c, g);
        if (zone_ == Zone::Row || pointer_) drawPills(c, g);
        else drawKeyGuide(c);
      }
      if (pointer_) drawEdges(c);
      if (p.backupNotice()) drawBackupCard(c);
    }
    if (!p.note().empty() && !p.failed()) {
      const std::string& note = p.note();
      float w = ui::textWidth(c, note, Weight::Medium, 20) + 48;
      Rect r{gfx::kLayoutWidth - 48 - w, 48, w, 48};
      c.g.fill(r, theme::black(0.7f), 24);
      ui::labelIn(c, note, r, Weight::Medium, 20, theme::white(0.9f), Align::Center);
    }
    if (!digits_.empty()) {
      Rect r{gfx::kLayoutWidth - 80 - 220, 60, 220, 90};
      c.g.fill(r, theme::black(0.75f), 20);
      ui::labelIn(c, digits_, r, Weight::Bold, 54, theme::white(), Align::Center);
    }
    if (tips_) drawTips(c);
  }

 private:
  enum class Zone { None, Row, EdgeGuide, EdgeLast };
  enum class Pill { Cameras, Sources, Guide, Last, Favorite, More };

  // ---------------------------------------------------------- actions
  bool bannerVisible() const { return app_.ms() >= bannerFrom_ && app_.ms() < bannerUntil_; }
  void showBanner() {
    bannerFrom_ = app_.ms();
    bannerUntil_ = bannerFrom_ + kBannerMs;
  }
  void touch() {
    rowTouched_ = app_.ms();
    bannerFrom_ = app_.ms();
    bannerUntil_ = bannerFrom_ + kRowIdleMs;
  }
  void focusRow() {
    buildPills();
    zone_ = Zone::Row;
    pill_ = 0;
    for (size_t i = 0; i < pills_.size(); ++i)
      if (pills_[i] == Pill::Sources) pill_ = static_cast<int>(i);
    if (!pills_.empty() && pills_.front() == Pill::Cameras) pill_ = 0;
    touch();
  }
  void buildPills() {
    Playback& p = app_.playback();
    pills_.clear();
    if (cameras_) pills_.push_back(Pill::Cameras);
    pills_.push_back(Pill::Sources);
    pills_.push_back(Pill::Guide);
    if (app_.store().channels()->find(p.previousEntry())) pills_.push_back(Pill::Last);
    if (app_.store().channels()->find(p.entry())) pills_.push_back(Pill::Favorite);
    pills_.push_back(Pill::More);
    pill_ = std::clamp(pill_, 0, static_cast<int>(pills_.size()) - 1);
  }
  bool rowKey(const KeyEvent& e) {
    touch();
    switch (e.key) {
      case Key::Left:
        if (pill_ > 0) --pill_;
        return true;
      case Key::Right:
        if (pill_ + 1 < static_cast<int>(pills_.size())) ++pill_;
        return true;
      case Key::Up:
      case Key::Back:
        zone_ = Zone::None;
        bannerUntil_ = app_.ms() + 3000;
        return true;
      case Key::Ok:
        activate(pills_[static_cast<size_t>(pill_)]);
        return true;
      case Key::Red:
        openSources();
        return true;
      case Key::ChannelUp:
      case Key::ChannelDown:
        zone_ = Zone::None;
        flipped(app_.playback().flip(e.key == Key::ChannelUp ? -1 : 1));
        return true;
      default:
        return true;
    }
  }
  void activate(Pill pill) {
    Playback& p = app_.playback();
    switch (pill) {
      case Pill::Cameras: zone_ = Zone::None; app_.nav().push(makeCameraPanel(app_)); break;
      case Pill::Sources: openSources(); break;
      case Pill::Guide: zone_ = Zone::None; openGuide(); break;
      case Pill::Last: zone_ = Zone::None; goPrevious(); break;
      case Pill::Favorite: {
        bool was = false;
        if (const GuideChannel* g = app_.store().channels()->find(p.entry())) was = g->favorite;
        app_.store().toggleFavorite(p.entry());
        app_.toast(was ? "Removed from Favorites" : "Added to Favorites", true);
        break;
      }
      case Pill::More: app_.nav().push(makeOptions(app_)); break;
    }
  }
  void openSources() {
    Playback& p = app_.playback();
    if (app_.store().isEvent(p.entry())) app_.nav().push(makeWhereToWatch(app_, p.entry()));
    else app_.nav().push(makeSources(app_, p.entry()));
  }
  void openGuide() {
    Playback& p = app_.playback();
    if (p.failed()) return;
    // Watching a game from Live Events: the event guide.
    app_.nav().push(app_.store().isEvent(p.entry()) ? makeEventGuide(app_) : makeMiniGuide(app_));
  }
  void goPrevious() {
    Playback& p = app_.playback();
    std::string prev = p.previousEntry();
    if (prev.empty()) {
      showBanner();
      return;
    }
    std::string name = nameOf(prev);
    p.previous();
    app_.toast("Back to " + name, true);
    showBanner();
  }
  void closeTips() {
    tips_ = false;
    showBanner();
  }

  // ---------------------------------------------------------- failed
  std::vector<std::string> failButtons() const {
    std::vector<std::string> out = {"Try Again", "Choose a Source"};
    if (app_.store().channels()->find(app_.playback().previousEntry()))
      out.push_back("Back to " + nameOf(app_.playback().previousEntry()));
    return out;
  }
  bool failedKey(const KeyEvent& e) {
    int n = static_cast<int>(failButtons().size());
    switch (e.key) {
      case Key::Left:
        if (failButton_ > 0) --failButton_;
        return true;
      case Key::Right:
        if (failButton_ + 1 < n) ++failButton_;
        return true;
      case Key::Ok: {
        Playback& p = app_.playback();
        if (failButton_ == 0) {
          std::string id = p.entry();
          p.stop();
          p.tune(id);
          p.setFull();
        } else if (failButton_ == 1) {
          openSources();
        } else {
          failButton_ = 0;
          goPrevious();
        }
        return true;
      }
      case Key::Red: openSources(); return true;
      case Key::Up:
      case Key::ChannelUp:
        failButton_ = 0;
        flipped(app_.playback().flip(-1));
        return true;
      case Key::Down:
      case Key::ChannelDown:
        failButton_ = 0;
        flipped(app_.playback().flip(1));
        return true;
      case Key::Back: app_.nav().pop(); return true;
      default: return true;
    }
  }

  // ---------------------------------------------------------- helpers
  void flipped(const std::string& id) {
    if (id.empty()) return;
    // Games: "2 / 5 · Rams at Seahawks".
    if (app_.store().isEvent(id)) {
      const std::vector<std::string>& list = app_.playback().flipList();
      auto it = std::find(list.begin(), list.end(), id);
      const EventTile* t = app_.store().eventTile(id);
      if (it != list.end() && t)
        app_.toast(std::to_string(it - list.begin() + 1) + " / " + std::to_string(list.size()) + " \xc2\xb7 " +
                       (t->heroTitle.empty() ? t->title : t->heroTitle),
                   true);
    }
    bannerFrom_ = app_.ms() + kBannerDelayMs;
    bannerUntil_ = bannerFrom_ + kBannerMs;
  }
  std::string nameOf(const std::string& id) const {
    auto ch = app_.store().channels();
    const GuideChannel* g = ch->find(id);
    return g ? g->name : std::string("the previous channel");
  }
  const Listing* current(const std::string& id, time::Seconds now, const Listing** next) const {
    listings_ = app_.store().listings();
    const std::vector<Listing>* v = listings_->forChannel(id);
    if (next) *next = nullptr;
    if (!v) return nullptr;
    for (size_t i = 0; i < v->size(); ++i) {
      if ((*v)[i].isOn(now)) {
        if (next && i + 1 < v->size()) *next = &(*v)[i + 1];
        return &(*v)[i];
      }
      if ((*v)[i].start > now) {
        if (next) *next = &(*v)[i];
        return nullptr;
      }
    }
    return nullptr;
  }
  std::string feedLine() const {
    Playback& p = app_.playback();
    if (p.feeds().empty()) return {};
    size_t i = std::min(p.feedIndex(), p.feeds().size() - 1);
    std::string q = p.feeds()[i].quality;
    std::string line = "Feed " + std::to_string(i + 1) + " of " + std::to_string(p.feeds().size());
    return q.empty() ? line : q + " \xc2\xb7 " + line;
  }
  int backups() const {
    return std::max(0, static_cast<int>(app_.playback().feeds().size()) - 1);
  }
  float keyCap(ui::Ctx& c, float x, float y, const char* glyph) {
    Rect r{x, y, 40, 40};
    c.g.fill(r, theme::white(0.16f), 10);
    ui::labelIn(c, glyph, r, Weight::Bold, 17, theme::white(), Align::Center);
    return 40;
  }
  float dot(ui::Ctx& c, float x, float y, uint32_t color) {
    c.g.fill({x, y + 9, 22, 22}, theme::hex(color), 11);
    return 22;
  }

  // ---------------------------------------------------------- drawing
  void drawScrim(ui::Ctx& c) {
    // Darkens the bottom of the picture under the banner.
    float top = gfx::kLayoutHeight - 520;
    for (int i = 0; i < 26; ++i) {
      float t = static_cast<float>(i) / 25.0f;
      c.g.fill({0, top + 20.0f * static_cast<float>(i), gfx::kLayoutWidth, 20}, theme::black(0.88f * t * t));
    }
  }

  void drawBanner(ui::Ctx& c, const GuideChannel* g) {
    Playback& p = app_.playback();
    float x = kLeft, w = gfx::kLayoutWidth - 2 * kLeft;
    float top = kBannerBottom - 150;
    // Logo, number and name, LIVE and the show.
    Rect logo{x, top + 8, 150, 64};
    if (g && !g->logoUrl.empty()) {
      c.g.fill(logo, theme::white(0.12f), 12);
      ui::logo(c, g->logoUrl, g->name, {logo.x + 10, logo.y + 6, logo.w - 20, logo.h - 12});
    } else {
      c.g.fill(logo, theme::white(0.12f), 12);
      ui::labelIn(c, g ? g->name : std::string("LIVE"), {logo.x + 8, logo.y, logo.w - 16, logo.h}, Weight::Bold, 22,
                  theme::white(), Align::Center);
    }
    float tx = logo.x + logo.w + 22;
    const Listing* next = nullptr;
    const Listing* l = g ? current(g->id, c.now, &next) : nullptr;
    std::string where = g ? (g->number ? std::to_string(*g->number) + " \xc2\xb7 " : std::string()) + g->name : std::string();
    if (const EventTile* t = app_.store().eventTile(p.entry()); t && !g) where = t->leagueChip;
    ui::label(c, where, tx, top + 2, Weight::Bold, 22, theme::white(0.7f), w - (tx - x));
    float lx = tx + ui::chip(c, "LIVE", tx, top + 42, 16, theme::hex(kRed), theme::white()) + 12;
    std::string title = l ? l->title : (g ? g->name : std::string());
    if (const EventTile* t = app_.store().eventTile(p.entry()))
      title = t->heroTitle.empty() ? t->title : t->heroTitle;
    ui::label(c, title, lx, top + 30, Weight::Bold, 44, theme::white(), x + w - lx);
    // Times, progress, next and the feed.
    float y = top + 104;
    float rx = x;
    if (l) {
      rx += ui::label(c, ui::range(c, l->start, l->end, true), rx, y, Weight::Medium, 22, theme::white(0.75f)) + 18;
      double frac = static_cast<double>(c.now - l->start) / static_cast<double>(std::max<time::Seconds>(1, l->end - l->start));
      ui::progress(c, {rx, y + 11, 420, 6}, frac, theme::white(0.18f), theme::white());
      rx += 420 + 22;
    }
    if (next) rx += ui::label(c, "Next: " + ui::clock(c, next->start) + " " + next->title, rx, y, Weight::Regular, 22,
                              theme::white(0.6f), 520) + 22;
    std::string feed = feedLine();
    if (!feed.empty()) {
      float fw = ui::chipWidth(c, feed, 17) + 6;
      if (rx + fw < x + w) ui::chip(c, feed, rx, y - 1, 17, theme::white(0.45f), theme::white(0.75f), true);
    }
  }

  // 1A: the keys, under the banner (fades with it).
  void drawKeyGuide(ui::Ctx& c) {
    float x = kLeft, y = kRowY + 10;
    c.g.fill({kLeft, kRowY - 8, gfx::kLayoutWidth - 2 * kLeft, 1}, theme::white(0.14f));
    auto item = [&](float glyphW, const std::string& title, const std::string& extra) {
      x += glyphW + 12;
      x += ui::label(c, title, x, y + 8, Weight::Bold, 22, theme::white());
      if (!extra.empty()) x += 8 + ui::label(c, extra, x + 8, y + 8, Weight::Medium, 22, theme::white(0.6f));
      x += 34;
    };
    Playback& p = app_.playback();
    keyCap(c, x, y, "\xe2\x97\x80");
    item(40, app_.store().isEvent(p.entry()) ? "Games" : "Guide", "");
    if (app_.store().channels()->find(p.previousEntry())) {
      keyCap(c, x, y, "\xe2\x96\xb6");
      item(40, "Last channel", nameOf(p.previousEntry()));
    }
    keyCap(c, x, y, "\xe2\x96\xbc");
    item(40, cameras_ ? "Cameras & more" : "More", "");
    keyCap(c, x, y, "OK");
    item(40, "Info", "");
    dot(c, x, y, kRed);
    int b = backups();
    item(22, "Sources", b > 0 ? std::to_string(b) + (b == 1 ? " backup" : " backups") : std::string());
    dot(c, x, y, kBlue);
    item(22, "Options", "");
    int left = static_cast<int>((bannerUntil_ > app_.ms() ? bannerUntil_ - app_.ms() : 0) / 1000) + 1;
    if (zone_ == Zone::None && x < gfx::kLayoutWidth - kLeft - 360)
      ui::label(c, "Hides in " + std::to_string(std::min(left, 6)) + " s", gfx::kLayoutWidth - kLeft, y + 10,
                Weight::Regular, 18, theme::white(0.45f), 300, Align::Right);
  }

  // 2A / 3D: the action row.
  void drawPills(ui::Ctx& c, const GuideChannel* g) {
    buildPills();
    Playback& p = app_.playback();
    float x = kLeft, y = kRowY;
    for (size_t i = 0; i < pills_.size(); ++i) {
      std::string title, extra;
      switch (pills_[i]) {
        case Pill::Cameras: title = "Cameras"; break;
        case Pill::Sources: title = "Sources"; extra = feedLine(); break;
        case Pill::Guide: title = app_.store().isEvent(p.entry()) ? "Games" : "Guide"; break;
        case Pill::Last: title = "Back to " + nameOf(p.previousEntry()); break;
        case Pill::Favorite: title = g && g->favorite ? "Unfavorite" : "Favorite"; break;
        case Pill::More: title = "More"; break;
      }
      float w = ui::textWidth(c, title, Weight::Bold, 24) + 60;
      if (!extra.empty()) w += ui::textWidth(c, extra, Weight::Medium, 22) + 14;
      Rect r{x, y, w, 72};
      c.hit(r, kHitPill + static_cast<int>(i));
      bool f = zone_ == Zone::Row && static_cast<int>(i) == pill_;
      if (f) {
        Rect big{r.x - r.w * 0.02f, r.y - 1.5f, r.w * 1.04f, r.h + 3};
        c.g.fill(big, theme::white(), 37);
      } else {
        c.g.fill(r, theme::white(0.14f), 36);
      }
      Color ink = f ? theme::black() : theme::white();
      float tx = r.x + 30;
      tx += ui::label(c, title, tx, r.y + 22, Weight::Bold, 24, ink);
      if (!extra.empty()) ui::label(c, extra, tx + 14, r.y + 24, Weight::Medium, 22, f ? theme::black(0.6f) : theme::white(0.6f));
      x += w + 16;
    }
    if (zone_ == Zone::Row)
      ui::label(c, "Up hides it", gfx::kLayoutWidth - kLeft, y + 24, Weight::Regular, 19, theme::white(0.5f), 300,
                Align::Right);
  }

  // 3D: Guide on the left edge, the last channel on the right.
  void drawEdges(ui::Ctx& c) {
    Playback& p = app_.playback();
    auto edge = [&](Rect r, int hit, bool focused, const char* glyph, const std::string& text) {
      c.hit(r, hit);
      c.g.fill(r, focused ? theme::white() : theme::hex(0x14161B, 0.75f), r.w * 0.5f);
      Color ink = focused ? theme::black() : theme::white();
      ui::labelIn(c, glyph, {r.x, r.y + 30, r.w, 40}, Weight::Bold, 26, ink, Align::Center);
      ui::labelIn(c, text, {r.x + 6, r.y + 84, r.w - 12, 30}, Weight::Bold, 16, ink, Align::Center);
    };
    edge({32, 460, 96, 160}, kHitGuide, zone_ == Zone::EdgeGuide, "\xe2\x97\x80",
         app_.store().isEvent(p.entry()) ? "Games" : "Guide");
    if (app_.store().channels()->find(p.previousEntry()))
      edge({gfx::kLayoutWidth - 128, 460, 96, 160}, kHitLast, zone_ == Zone::EdgeLast, "\xe2\x96\xb6",
           nameOf(p.previousEntry()));
  }

  // 3B: a backup took over by itself.
  void drawBackupCard(ui::Ctx& c) {
    Playback& p = app_.playback();
    const auto& b = p.backupNotice();
    if (!b || p.feeds().empty()) return;
    Rect r{gfx::kLayoutWidth - 48 - 560, p.note().empty() ? 48.0f : 110.0f, 560, 190};
    c.g.fill(r, theme::hex(0x14161B, 0.9f), 22);
    Rect icon{r.x + 28, r.y + 26, 34, 34};
    c.g.fill(icon, theme::hex(kGreen, 0.2f), 17);
    ui::labelIn(c, "\xe2\x9c\x93", icon, Weight::Bold, 20, theme::hex(kGreen), Align::Center);
    ui::label(c, "Switched to a backup", r.x + 76, r.y + 28, Weight::Bold, 24, theme::white());
    size_t to = std::min(b->to, p.feeds().size() - 1);
    std::string q = p.feeds()[to].quality;
    std::string text = "Feed " + std::to_string(b->from + 1) + " stopped, so Feed " + std::to_string(to + 1) +
                       (q.empty() ? "" : " (" + q + ")") + " took over.";
    ui::paragraph(c, text, r.x + 28, r.y + 74, r.w - 56, Weight::Regular, 20, theme::white(0.72f), 2);
    float dx = r.x + 28;
    c.g.fill({dx, r.y + 146, 18, 18}, theme::hex(kRed), 9);
    ui::label(c, "Sources to pick another", dx + 28, r.y + 142, Weight::Bold, 19, theme::white(0.8f));
  }

  // 3A, or the first tune: the dark card while a feed opens.
  void drawTuning(ui::Ctx& c, const GuideChannel* g) {
    Playback& p = app_.playback();
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(0x0D1117));
    if (p.hadPicture() && !p.feeds().empty()) {
      // Switching feeds (picked or a backup): centred.
      size_t i = std::min(p.feedIndex(), p.feeds().size() - 1);
      float cy = 380;
      ui::spinner(c, gfx::kLayoutWidth * 0.5f, cy + 32, 28);
      ui::labelIn(c, "Switching to Feed " + std::to_string(i + 1), {0, cy + 90, gfx::kLayoutWidth, 50}, Weight::Bold,
                  36, theme::white(), Align::Center);
      ui::labelIn(c, p.feeds()[i].label, {0, cy + 150, gfx::kLayoutWidth, 34}, Weight::Regular, 22, theme::white(0.65f),
                  Align::Center);
      ui::labelIn(c, "If it doesn't start in 20 seconds, the next feed is tried", {0, cy + 196, gfx::kLayoutWidth, 30},
                  Weight::Regular, 19, theme::white(0.45f), Align::Center);
      std::string pill = "Feed " + std::to_string(i + 1) + " of " + std::to_string(p.feeds().size());
      float pw = ui::textWidth(c, pill, Weight::Bold, 20) + 44;
      Rect r{gfx::kLayoutWidth - 48 - pw, 48, pw, 48};
      c.g.fill(r, theme::hex(0x14161B, 0.82f), 24);
      ui::labelIn(c, pill, r, Weight::Bold, 20, theme::white(), Align::Center);
      float x = kLeft;
      c.g.fill({x, gfx::kLayoutHeight - 92, 20, 20}, theme::hex(kRed), 10);
      x += ui::label(c, "Pick another source", x + 30, gfx::kLayoutHeight - 96, Weight::Bold, 21, theme::white(0.75f)) + 70;
      Rect bk{x, gfx::kLayoutHeight - 102, 56, 36};
      c.g.fill(bk, theme::white(0.16f), 9);
      ui::labelIn(c, "Back", bk, Weight::Bold, 14, theme::white(), Align::Center);
      ui::label(c, "Back to the guide", bk.x + bk.w + 12, gfx::kLayoutHeight - 96, Weight::Bold, 21, theme::white(0.75f));
      return;
    }
    float x = 110, y = gfx::kLayoutHeight - 110 - 420;
    if (g) ui::logo(c, g->logoUrl, "", {x, y, 300, 110});
    y += 130;
    if (g) ui::label(c, g->name, x, y, Weight::Bold, 34, theme::white());
    y += 54;
    ui::label(c, "TUNING IN", x, y, Weight::Bold, 22, theme::white(0.6f));
    y += 40;
    const Listing* l = g ? current(g->id, c.now, nullptr) : nullptr;
    if (l) ui::label(c, l->title, x, y, Weight::Bold, 60, theme::white(), 1500);
    y += 90;
    ui::spinner(c, x + 20, y + 20, 18);
  }

  // 3C: every feed was tried.
  void drawFailed(ui::Ctx& c, const GuideChannel* g) {
    Playback& p = app_.playback();
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(0x0D1117));
    std::vector<std::string> lines;
    for (size_t i = 0; i < p.feeds().size() && i < 5; ++i) {
      const Feed& f = p.feeds()[i];
      auto problem = p.feedProblem(f.channelId);
      std::string q = f.quality.empty() ? "" : " \xc2\xb7 " + f.quality;
      lines.push_back("Feed " + std::to_string(i + 1) + q + " \xe2\x80\x94 " +
                      (problem ? problem->why : std::string("not tried")));
    }
    float h = 470 + 34 * static_cast<float>(lines.size());
    Rect card{460, std::max(60.0f, (gfx::kLayoutHeight - h) * 0.5f), 1000, h};
    c.g.fill(card, theme::hex(0x1C1F25, 0.97f), 34);
    float x = card.x + 64, y = card.y + 56, w = card.w - 128;
    Rect logo{x, y, 150, 64};
    c.g.fill(logo, theme::white(0.12f), 12);
    if (g && !g->logoUrl.empty()) ui::logo(c, g->logoUrl, g->name, {logo.x + 10, logo.y + 6, logo.w - 20, logo.h - 12});
    else ui::labelIn(c, g ? g->name : std::string(), {logo.x + 8, logo.y, logo.w - 16, logo.h}, Weight::Bold, 22, theme::white(), Align::Center);
    if (g)
      ui::label(c, (g->number ? std::to_string(*g->number) + " \xc2\xb7 " : std::string()) + g->name, logo.x + logo.w + 20,
                y + 18, Weight::Regular, 22, theme::white(0.6f), w - 170);
    y += 92;
    ui::label(c, "This channel isn't playing right now", x, y, Weight::Bold, 40, theme::white(), w);
    y += 62;
    std::string why = p.feeds().size() > 1
                          ? "All " + std::to_string(p.feeds().size()) +
                                " sources were tried. The provider may be down for this channel, or your connection "
                                "limit may be in use on another screen."
                          : (p.failMessage().empty() ? std::string("Its feed didn't answer.") : p.failMessage());
    y += ui::paragraph(c, why, x, y, w, Weight::Regular, 22, theme::white(0.68f), 3) + 28;
    std::vector<std::string> buttons = failButtons();
    failButton_ = std::clamp(failButton_, 0, static_cast<int>(buttons.size()) - 1);
    float bx = x;
    for (size_t i = 0; i < buttons.size(); ++i) {
      float bw = ui::chipWidth(c, buttons[i], 24) + 44;
      Rect r{bx, y, bw, 68};
      c.hit(r, kHitFail + static_cast<int>(i));
      bool f = static_cast<int>(i) == failButton_;
      c.g.fill(r, f ? theme::white() : theme::white(0.14f), 34);
      ui::labelIn(c, buttons[i], r, Weight::Bold, 24, f ? theme::black() : theme::white(), Align::Center);
      bx += bw + 16;
    }
    y += 100;
    for (const std::string& line : lines) {
      ui::label(c, line, x, y, Weight::Regular, 19, theme::white(0.55f), w);
      y += 34;
    }
  }

  // 1C: once, the first time a channel plays.
  void drawTips(ui::Ctx& c) {
    ui::scrim(c, 0.55f);
    Rect card{460, 150, 1000, 780};
    c.g.fill(card, theme::hex(0x1C1F25, 0.97f), 34);
    float x = card.x + 64, y = card.y + 56;
    ui::label(c, "WATCHING LIVE TV", x, y, Weight::Bold, 18, theme::white(0.5f));
    ui::label(c, "Getting around", x, y + 30, Weight::Bold, 44, theme::white());
    y += 120;
    struct Tip {
      const char* glyph;
      uint32_t color;  // 0: a key cap
      const char* title;
      const char* detail;
    };
    const Tip tips[] = {
        {"\xe2\x97\x80", 0, "Guide", "What's on, without leaving"},
        {"\xe2\x96\xb6", 0, "Last channel", "Jump back and forth"},
        {"\xe2\x96\xb2\xe2\x96\xbc", 0, "Channels", "Up, or CH + / CH \xe2\x88\x92"},
        {"OK", 0, "Info", "What's on now and next; \xe2\x96\xbc for more"},
        {"", kRed, "Sources", "Pick another feed of this channel"},
        {"123", 0, "Number keys", "Go straight to a channel"},
    };
    float colW = (card.w - 128 - 40) / 2;
    for (size_t i = 0; i < 6; ++i) {
      float tx = x + static_cast<float>(i % 2) * (colW + 40), ty = y + static_cast<float>(i / 2) * 96;
      Rect cap{tx, ty, 56, 56};
      c.g.fill(cap, theme::white(0.12f), 14);
      if (tips[i].color) c.g.fill({cap.x + 16, cap.y + 16, 24, 24}, theme::hex(tips[i].color), 12);
      else ui::labelIn(c, tips[i].glyph, cap, Weight::Bold, 18, theme::white(), Align::Center);
      ui::label(c, tips[i].title, tx + 74, ty + 4, Weight::Bold, 25, theme::white(), colW - 74);
      ui::label(c, tips[i].detail, tx + 74, ty + 36, Weight::Regular, 19, theme::white(0.65f), colW - 74);
    }
    y += 3 * 96 + 10;
    ui::paragraph(c,
                  "If a feed stops, a backup takes over by itself. This shows once; the keys are listed under the "
                  "banner whenever it's up.",
                  x, y, card.w - 128, Weight::Regular, 20, theme::white(0.6f), 2);
    Rect ok{x, card.y + card.h - 130, ui::chipWidth(c, "Got it", 24) + 60, 68};
    c.hit(ok, kHitTips);
    c.g.fill(ok, theme::white(), 34);
    ui::labelIn(c, "Got it", ok, Weight::Bold, 24, theme::black(), Align::Center);
  }

  uint32_t bannerFrom_ = 0, bannerUntil_ = 0, rowTouched_ = 0;
  Zone zone_ = Zone::None;
  std::vector<Pill> pills_;
  int pill_ = 0;
  int failButton_ = 0;
  bool tips_ = false;
  bool pointer_ = false;
  std::string shownEntry_;
  std::string digits_;
  uint32_t digitsAt_ = 0;
  mutable std::shared_ptr<const Listings> listings_;
};

}  // namespace

std::unique_ptr<Screen> makePlayer(App& app) { return std::make_unique<PlayerScreen>(app); }

}  // namespace marigold::app
