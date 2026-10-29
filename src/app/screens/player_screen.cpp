// Full screen playback (LiveTVOverlay): the info banner, which also carries
// every load state (tuning, reconnecting, another feed, still trying, not
// playing) in one quiet line, with what's on and later on the channel; its
// key guide; the action row (Sources, Guide, last channel, Favorite, More);
// the one-time "Getting around" card; channel flipping (the banner shows the
// channel at once, the one stopped on tunes), previous channel and number
// keys. With the Magic Remote pointer the row and two edge buttons (Guide,
// last channel) are clickable.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/engine/views.hpp"
#include "core/input/hints.hpp"
#include "platform/icons.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr uint32_t kBannerMs = 6000;       // after the picture comes up
constexpr uint32_t kRowIdleMs = 10000;  // the action row hides after this long untouched
constexpr uint32_t kFlipSettleMs = 500;    // channel keys: the channel stopped on tunes
constexpr uint32_t kStillTryingMs = 7000;  // a first tune without a picture yet
constexpr uint32_t kBackOnMs = 2000;       // "Back on CNN" after a backup took over
constexpr uint32_t kDigitsMs = 1500;
constexpr float kLeft = 96;
constexpr float kBannerBottom = 870;  // the banner's lower edge
constexpr float kRowY = 952;          // the key guide or the action row
constexpr float kStripW = 500;        // "Later on this channel"
constexpr uint32_t kRed = 0xE5372B, kBlue = 0x3D7BFF, kGreen = 0x34C759;

// Pointer targets.
constexpr int kHitPill = 1;      // + pill index
constexpr int kHitGuide = 50, kHitLast = 51;
constexpr int kHitFail = 60;     // + button index
constexpr int kHitTips = 70;

class PlayerScreen : public Screen {
  bool cameras_ = false;  // the race on screen has cameras (Down opens them)

 public:
  explicit PlayerScreen(App& app) : Screen(app) { bannerFrom_ = app.ms(); }
  // Freed after the screen below has been shown again: the guide has taken
  // the picture into its corner by then. Anywhere else (Live Events, a
  // game's page, search), leaving the player ends the stream.
  ~PlayerScreen() override {
    Playback& p = app_.playback();
    if (p.active() && p.full()) p.stop();
  }

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
        flipBy(-1);
        return true;
      case Key::Down:
        // The banner is up: Down goes to its action row.
        if (bannerVisible() && p.ready() && pendingFlip_.empty()) {
          focusRow();
          return true;
        }
        // A race with cameras: Down opens them (channel keys still flip).
        if (cameras_ && pendingFlip_.empty()) {
          app_.nav().push(makeCameraPanel(app_));
          return true;
        }
        flipBy(1);
        return true;
      case Key::ChannelDown:
        flipBy(1);
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
      case Key::Menu:  // a remote without colour keys (Fire TV)
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
    // Channel keys: the channel stopped on tunes.
    if (!pendingFlip_.empty() && ms >= flipAt_) {
      std::string id = std::move(pendingFlip_);
      pendingFlip_.clear();
      if (id != p.entry()) flipped(p.tune(id) ? id : std::string());
    }
    if (p.entry() != shownEntry_) {
      shownEntry_ = p.entry();
      // The banner is up while it loads; its few seconds start with the picture.
      bannerFrom_ = bannerUntil_ = ms;
      wasReady_ = pictureSeen_ = false;
      zone_ = Zone::None;
      prefetch(shownEntry_);
      cameras_ = app_.store().isEvent(shownEntry_) && app_.store().hasCameras(shownEntry_);
      if (cameras_) say("Press Down for onboard cameras", 5000);
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
    // The picture (first, or back after a reconnect or another feed): the
    // banner's usual few seconds from now.
    if (p.ready() && !wasReady_) {
      bannerFrom_ = ms;
      bannerUntil_ = std::max(bannerUntil_, ms + kBannerMs);
      // A backup on the first start isn't "back on" anything: the banner
      // said it was still trying.
      if (!pictureSeen_) p.dismissBackupNotice();
      pictureSeen_ = true;
    }
    wasReady_ = p.ready();
    if (p.ready()) pictureFeed_ = p.feedIndex();
    // A note from the recovery (buffering, reconnecting): the banner shows it.
    if (p.ready() && !p.note().empty()) {
      if (!bannerVisible()) bannerFrom_ = ms;
      bannerUntil_ = std::max(bannerUntil_, ms + 1500);
    }
    if (const auto& b = p.backupNotice(); b && ms - b->at > kBackOnMs) p.dismissBackupNotice();
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
      // A number with no channel: said where the number was typed.
      if (target.empty()) {
        missed_ = std::to_string(n);
        missedUntil_ = ms + 2500;
      }
      else flipped(p.tune(target) ? target : std::string());
    }
    bool bannerAnim = ms < bannerUntil_ + 50;
    bool note = !p.note().empty();
    if (!missed_.empty() && ms >= missedUntil_) missed_.clear();
    return opening || bannerAnim || note || !digits_.empty() || !missed_.empty() || p.backupNotice().has_value() ||
           zone_ == Zone::Row || !pendingFlip_.empty();
  }

  void draw(ui::Ctx& c) override {
    Playback& p = app_.playback();
    auto ch = app_.store().channels();
    bool pending = !pendingFlip_.empty();
    const GuideChannel* g = ch->find(pending ? pendingFlip_ : p.entry());
    if (!g && !pending) {
      // A game: show the channel it plays on.
      if (!p.feeds().empty()) g = ch->find(app_.store().entryOfChannel(p.feeds()[std::min(p.feedIndex(), p.feeds().size() - 1)].channelId));
    }
    pointer_ = c.pointerActive;
    Rect screen{0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight};
    bool picture = p.ready() && !pending && !p.failed();
    // No loading screen: the banner carries it. Behind it, the picture (or
    // what the player still holds of it after a stall), else black with a
    // small spinner where the picture will be.
    if (picture || (p.hadPicture() && !pending && !p.failed())) {
      c.g.hole(screen);
    } else {
      c.g.fill(screen, theme::black());
      if (!p.failed()) ui::spinner(c, gfx::kLayoutWidth * 0.5f, 400, 26);
    }
    if (p.failed() && !pending) {
      drawScrim(c);
      drawBanner(c, g, true);
      drawFailButtons(c);
    } else if (bannerVisible() || zone_ == Zone::Row || pointer_) {
      drawScrim(c);
      drawBanner(c, g, false);
      if ((zone_ == Zone::Row || pointer_) && picture) drawPills(c, g);
      else drawKeyGuide(c, g);
    }
    if (pointer_ && picture) drawEdges(c);
    if (!digits_.empty()) {
      Rect r{gfx::kLayoutWidth - 80 - 220, 60, 220, 90};
      c.g.fill(r, theme::black(0.75f), 20);
      ui::labelIn(c, digits_, r, Weight::Bold, 54, theme::white(), Align::Center);
    } else if (!missed_.empty() && app_.ms() < missedUntil_) {
      // The number typed, struck through, and that there's no such channel.
      std::string line = "No channel " + missed_;
      float w = std::max(220.0f, ui::textWidth(c, line, Weight::Medium, 22) + 60);
      Rect r{gfx::kLayoutWidth - 80 - w, 60, w, 128};
      c.g.fill(r, theme::black(0.8f), 20);
      ui::labelIn(c, missed_, {r.x, r.y + 8, r.w, 70}, Weight::Bold, 54, theme::white(0.45f), Align::Center);
      float sw = ui::textWidth(c, missed_, Weight::Bold, 54);
      c.g.fill({r.x + (r.w - sw) * 0.5f - 6, r.y + 44, sw + 12, 4}, theme::hex(kRed, 0.9f), 2);
      ui::labelIn(c, line, {r.x, r.y + 80, r.w, 34}, Weight::Medium, 22, theme::white(0.85f), Align::Center);
    }
    if (tips_) drawTips(c);
  }

 private:
  enum class Zone { None, Row, EdgeGuide, EdgeLast };
  enum class Pill { Cameras, Sources, Coast, Guide, Last, Favorite, More };

  // ---------------------------------------------------------- actions
  // A message while watching: in the banner's status line (brought up for
  // it), not a toast over the picture. `done` puts a tick before it.
  void say(std::string text, uint32_t forMs, bool done = false) {
    message_ = std::move(text);
    messageDone_ = done;
    messageUntil_ = app_.ms() + forMs;
    if (!bannerVisible()) bannerFrom_ = app_.ms();
    bannerUntil_ = std::max(bannerUntil_, messageUntil_);
  }
  // While it loads (or a channel key is pending) the banner stays up.
  bool loading() const {
    const Playback& p = app_.playback();
    return !pendingFlip_.empty() || !p.ready() || p.failed();
  }
  bool bannerVisible() const { return loading() || (app_.ms() >= bannerFrom_ && app_.ms() < bannerUntil_); }
  // Channel keys: the banner shows the channel at once; the one stopped on
  // tunes after a moment (zapping past a channel doesn't open it).
  void flipBy(int step) {
    Playback& p = app_.playback();
    std::string target = p.flipTarget(pendingFlip_, step);
    if (target.empty()) return;
    zone_ = Zone::None;
    failButton_ = 0;
    pendingFlip_ = target;
    flipAt_ = app_.ms() + kFlipSettleMs;
    prefetch(target);
  }
  // The listings the banner shows for a channel and its neighbours, asked
  // for before they're needed.
  void prefetch(const std::string& id) {
    const std::vector<std::string>& list = app_.playback().flipList();
    std::vector<std::string> ids = {id};
    auto it = std::find(list.begin(), list.end(), id);
    if (it != list.end()) {
      long n = static_cast<long>(list.size()), i = it - list.begin();
      for (long d : {-2L, -1L, 1L, 2L}) ids.push_back(list[static_cast<size_t>(((i + d) % n + n) % n)]);
    }
    app_.store().wantListings(ids);
  }
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
    if (!otherCoast().empty()) pills_.push_back(Pill::Coast);
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
      case Key::Menu:
        openSources();
        return true;
      case Key::ChannelUp:
      case Key::ChannelDown:
        zone_ = Zone::None;
        flipBy(e.key == Key::ChannelUp ? -1 : 1);
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
      case Pill::Coast: {
        // The other version, in place (Last goes back).
        std::string other = otherCoast();
        if (other.empty()) break;
        if (!p.tune(other)) {
          say(nameOf(other) + " has no working feed right now", 3500);
          break;
        }
        flipped(other);
        break;
      }
      case Pill::Guide: zone_ = Zone::None; openGuide(); break;
      case Pill::Last: zone_ = Zone::None; goPrevious(); break;
      case Pill::Favorite: {
        bool was = false;
        if (const GuideChannel* g = app_.store().channels()->find(p.entry())) was = g->favorite;
        app_.store().toggleFavorite(p.entry());
        say(was ? "Removed from Favorites" : "Added to Favorites", 2500, !was);
        break;
      }
      case Pill::More: app_.nav().push(makeOptions(app_)); break;
    }
  }
  // The other version of the channel playing, when it has both.
  std::string otherCoast() const {
    auto ch = app_.store().channels();
    const GuideChannel* g = ch->find(app_.playback().entry());
    return g && !g->partner.empty() && ch->find(g->partner) ? g->partner : std::string();
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
    p.previous();  // the banner shows where to
    showBanner();
  }
  void closeTips() {
    tips_ = false;
    showBanner();
  }

  // ---------------------------------------------------------- failed
  std::vector<std::string> failButtons() const {
    std::vector<std::string> out = {"Try again", "Pick a feed"};
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
      case Key::Red:
      case Key::Menu: openSources(); return true;
      case Key::Up:
      case Key::ChannelUp:
        flipBy(-1);
        return true;
      case Key::Down:
      case Key::ChannelDown:
        flipBy(1);
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
        say(std::to_string(it - list.begin() + 1) + " / " + std::to_string(list.size()) + " \xc2\xb7 " +
                (t->heroTitle.empty() ? t->title : t->heroTitle),
            3500);
    }
    bannerFrom_ = bannerUntil_ = app_.ms();
  }
  std::string nameOf(const std::string& id) const {
    auto ch = app_.store().channels();
    const GuideChannel* g = ch->find(id);
    return g ? g->name : std::string("the previous channel");
  }
  const Listing* current(const std::string& id, time::Seconds now, const Listing** next) const {
    listings_ = app_.store().listings();
    engine::views::NowNext on = engine::views::nowAndNext(*listings_, id, now);
    if (next) *next = on.next;
    return on.now;
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
    c.g.fill(r, theme::hex(0x14161B, 0.75f), 10);
    c.g.stroke(r, theme::white(0.28f), 10, 1);
    ui::labelIn(c, glyph, r, Weight::Bold, 17, theme::white(), Align::Center);
    return 40;
  }
  // A key from the hints: the player's colour dot, or a cap with symbols or
  // text. Returns the width it took.
  float hintKey(ui::Ctx& c, float x, float y, const hints::Hint& h) {
    if (h.colour == hints::Colour::Red) return dot(c, x, y, kRed);
    if (h.colour == hints::Colour::Blue) return dot(c, x, y, kBlue);
    if (h.icons.empty()) return keyCap(c, x, y, h.cap.c_str());
    Rect r{x, y, 40, 40};
    c.g.fill(r, theme::hex(0x14161B, 0.75f), 10);
    c.g.stroke(r, theme::white(0.28f), 10, 1);
    if (unsigned tex = c.icons ? c.icons->get(h.icons.front()) : 0)
      c.g.textureAlpha(tex, {r.x + 9, r.y + 9, 22, 22}, theme::white());
    return 40;
  }
  // Text over the picture: a dark copy 2 px lower keeps it readable on a
  // bright frame.
  float shadowed(ui::Ctx& c, const std::string& s, float x, float y, Weight w, int px, Color col,
                 float maxWidth = 0) {
    ui::label(c, s, x, y + 2, w, px, theme::black(0.55f * col.a), maxWidth);
    return ui::label(c, s, x, y, w, px, col, maxWidth);
  }
  float dot(ui::Ctx& c, float x, float y, uint32_t color) {
    c.g.fill({x, y + 9, 22, 22}, theme::hex(color), 11);
    return 22;
  }

  // ---------------------------------------------------------- drawing
  void drawScrim(ui::Ctx& c) {
    // Darkens the bottom of the picture under the banner: already about two
    // thirds dark at the title, so white text reads on a bright frame (snow,
    // a pitch, a studio); the top half of the picture is left alone.
    static const float kStops[][2] = {{0.0f, 0.0f}, {0.2f, 0.30f}, {0.4f, 0.66f}, {0.6f, 0.84f}, {1.0f, 0.88f}};
    float top = gfx::kLayoutHeight - 540;
    for (int i = 0; i < 27; ++i) {
      float t = static_cast<float>(i) / 26.0f;
      size_t k = 1;
      while (k + 1 < std::size(kStops) && t > kStops[k][0]) ++k;
      float f = (t - kStops[k - 1][0]) / (kStops[k][0] - kStops[k - 1][0]);
      float a = kStops[k - 1][1] + (kStops[k][1] - kStops[k - 1][1]) * std::clamp(f, 0.0f, 1.0f);
      c.g.fill({0, top + 20.0f * static_cast<float>(i), gfx::kLayoutWidth, 20}, theme::black(a));
    }
  }

  void drawBanner(ui::Ctx& c, const GuideChannel* g, bool failed) {
    Playback& p = app_.playback();
    bool pending = !pendingFlip_.empty();
    float x = kLeft, w = gfx::kLayoutWidth - 2 * kLeft;
    float top = kBannerBottom - 150;
    // Logo, number and name, LIVE and the show.
    Rect logo{x, top + 8, 150, 64};
    if (g && !g->logoUrl.empty()) {
      c.g.fill(logo, theme::hex(0x14161B, 0.85f), 12);
      ui::logo(c, g->logoUrl, g->name, {logo.x + 10, logo.y + 6, logo.w - 20, logo.h - 12});
    } else {
      c.g.fill(logo, theme::hex(0x14161B, 0.85f), 12);
      ui::labelIn(c, g ? g->name : std::string("LIVE"), {logo.x + 8, logo.y, logo.w - 16, logo.h}, Weight::Bold, 22,
                  theme::white(), Align::Center);
    }
    float tx = logo.x + logo.w + 22;
    const Listing* next = nullptr;
    const Listing* l = g ? current(g->id, c.now, &next) : nullptr;
    std::string where = g ? (g->number ? std::to_string(*g->number) + " \xc2\xb7 " : std::string()) + g->name : std::string();
    if (const EventTile* t = pending ? nullptr : app_.store().eventTile(p.entry()); t && !g) where = t->leagueChip;
    // Later on this channel, on the right; the title stops short of it.
    float stripX = x + w - kStripW;
    bool strip = g && drawLater(c, g->id, stripX, top - 36);
    float titleRight = strip ? stripX - 36 : x + w;
    float whereW = shadowed(c, where, tx, top + 2, Weight::Bold, 22, theme::white(0.9f), w - (tx - x));
    // Which version plays, for channels with both.
    if (g && !g->partner.empty()) ui::coastChip(c, g->west, tx + whereW + 14, top + 2, 14);
    float y = top + 104;
    if (failed) {
      // Not playing: said in the banner, with why in one line.
      std::string name = g ? g->name : std::string("This channel");
      shadowed(c, name + " isn't playing right now", tx, top + 30, Weight::Bold, 44, theme::white(), titleRight - tx);
      std::string why = p.feeds().size() > 1
                            ? "Every feed was tried. The provider may be down for it, or busy on another screen."
                            : (p.failMessage().empty() ? std::string("Its feed didn't answer.") : p.failMessage());
      shadowed(c, why, x, y, Weight::Regular, 22, theme::white(0.82f), titleRight - x);
      return;
    }
    float lx = tx + ui::chip(c, "LIVE", tx, top + 42, 16, theme::hex(kRed), theme::white()) + 12;
    std::string title = l ? l->title : (g ? g->name : std::string());
    if (const EventTile* t = pending ? nullptr : app_.store().eventTile(p.entry()))
      title = t->heroTitle.empty() ? t->title : t->heroTitle;
    shadowed(c, title, lx, top + 30, Weight::Bold, 44, theme::white(), titleRight - lx);
    // Times, progress and time left (the feed is under Sources, not here).
    float rx = x;
    if (l) {
      rx += shadowed(c, ui::range(c, l->start, l->end, true), rx, y, Weight::Medium, 22, theme::white(0.92f)) + 18;
      double frac = static_cast<double>(c.now - l->start) / static_cast<double>(std::max<time::Seconds>(1, l->end - l->start));
      ui::progress(c, {rx, y + 11, 420, 6}, frac, theme::white(0.28f), theme::white());
      rx += 420 + 22;
      long left = static_cast<long>((l->end - c.now + 59) / 60);
      if (left > 0) shadowed(c, std::to_string(left) + " min left", rx, y, Weight::Regular, 22, theme::white(0.82f));
    } else if (next && !strip) {
      shadowed(c, "Next: " + ui::clock(c, next->start) + " " + next->title, rx, y, Weight::Regular, 22,
               theme::white(0.82f), titleRight - rx);
    }
  }

  // "Later on this channel": the next show (when it's close, how soon) and
  // the two after it. False when the guide has nothing later.
  bool drawLater(ui::Ctx& c, const std::string& id, float x, float y) {
    listings_ = app_.store().listings();
    const std::vector<Listing>* list = listings_ ? listings_->forChannel(id) : nullptr;
    if (!list) return false;
    std::vector<const Listing*> later;
    for (const Listing& l : *list) {
      if (l.start > c.now) later.push_back(&l);
      if (later.size() == 3) break;
    }
    if (later.empty()) return false;
    shadowed(c, "LATER ON THIS CHANNEL", x, y, Weight::Bold, 15, theme::white(0.6f));
    y += 28;
    for (size_t i = 0; i < later.size(); ++i) {
      const Listing& l = *later[i];
      bool first = i == 0;
      Rect r{x, y, kStripW, first ? 52.0f : 44.0f};
      c.g.fill(r, theme::hex(0x14161B, first ? 0.82f : 0.62f), 10);
      float tx = r.x + 16;
      ui::label(c, ui::clock(c, l.start), tx, r.y + (first ? 14 : 10), first ? Weight::Bold : Weight::Medium, 20,
                theme::white(first ? 0.95f : 0.7f));
      tx += 118;
      std::string soon;
      if (first) {
        long mins = static_cast<long>((l.start - c.now + 59) / 60);
        if (mins <= 5) soon = "Starts in " + std::to_string(std::max(1L, mins)) + " min";
        else if (mins <= 120) soon = "in " + std::to_string(mins) + " min";
      }
      float soonW = 0;
      if (!soon.empty()) {
        bool close = soon.rfind("Starts", 0) == 0;
        if (close) {
          soonW = ui::chipWidth(c, soon, 16) + 8;
          ui::chip(c, soon, r.x + r.w - 14 - soonW, r.y + 13, 16, theme::hex(theme::kStar, 0.9f), theme::black());
        } else {
          soonW = ui::textWidth(c, soon, Weight::Regular, 18);
          ui::label(c, soon, r.x + r.w - 14, r.y + 16, Weight::Regular, 18, theme::white(0.6f), 200, Align::Right);
        }
      }
      ui::label(c, l.title, tx, r.y + (first ? 13 : 9), first ? Weight::Bold : Weight::Regular, first ? 22 : 20,
                theme::white(first ? 1.0f : 0.78f), r.x + r.w - tx - soonW - 28);
      y += r.h + 6;
    }
    return true;
  }

  // The banner's status line: what's loading, or the picture back after a
  // backup took over. Empty once it plays.
  struct Status {
    std::string text;
    bool busy = false, done = false;
  };
  Status status(const GuideChannel* g) const {
    const Playback& p = app_.playback();
    if (!pendingFlip_.empty()) return {"Tuning in", true, false};
    if (p.failed()) return {};
    std::string q;
    if (!p.feeds().empty()) q = p.feeds()[std::min(p.feedIndex(), p.feeds().size() - 1)].quality;
    std::string withQ = q.empty() ? std::string() : " \xc2\xb7 " + q;
    if (!p.ready()) {
      if (!p.hadPicture()) {
        if (app_.ms() - p.tunedAt() < kStillTryingMs) return {"Tuning in", true, false};
        return {q.empty() ? std::string("Still trying\xe2\x80\xa6") : "Still trying" + withQ, true, false};
      }
      if (p.feedIndex() != pictureFeed_) return {"Switching to another feed" + withQ, true, false};
      return {"Reconnecting\xe2\x80\xa6", true, false};
    }
    if (p.backupNotice()) return {"Back on " + (g ? g->name : std::string("the channel")), false, true};
    if (!p.note().empty()) return {p.note(), true, false};
    if (!message_.empty() && app_.ms() < messageUntil_) return {message_, false, messageDone_};
    return {};
  }

  // While it doesn't play: Try again, Pick a feed, the last channel; where
  // the key guide goes.
  void drawFailButtons(ui::Ctx& c) {
    std::vector<std::string> buttons = failButtons();
    failButton_ = std::clamp(failButton_, 0, static_cast<int>(buttons.size()) - 1);
    float x = kLeft, y = kRowY;
    c.g.fill({kLeft, kRowY - 8, gfx::kLayoutWidth - 2 * kLeft, 1}, theme::white(0.14f));
    for (size_t i = 0; i < buttons.size(); ++i) {
      float w = ui::textWidth(c, buttons[i], Weight::Bold, 24) + 60;
      Rect r{x, y + 8, w, 64};
      c.hit(r, kHitFail + static_cast<int>(i));
      bool f = static_cast<int>(i) == failButton_;
      if (f) {
        c.g.fill(r, theme::white(), 32);
      } else {
        c.g.fill(r, theme::hex(0x14161B, 0.75f), 32);
        c.g.stroke(r, theme::white(0.22f), 32, 1);
      }
      ui::labelIn(c, buttons[i], r, Weight::Bold, 24, f ? theme::black() : theme::white(), Align::Center);
      x += w + 16;
    }
  }

  // 1A: the keys, under the banner (fades with it).
  void drawKeyGuide(ui::Ctx& c, const GuideChannel* g) {
    float x = kLeft, y = kRowY + 10;
    c.g.fill({kLeft, kRowY - 8, gfx::kLayoutWidth - 2 * kLeft, 1}, theme::white(0.14f));
    auto item = [&](float glyphW, const std::string& title, const std::string& extra) {
      x += glyphW + 12;
      x += shadowed(c, title, x, y + 8, Weight::Bold, 22, theme::white());
      if (!extra.empty()) x += 8 + shadowed(c, extra, x + 8, y + 8, Weight::Medium, 22, theme::white(0.78f));
      x += 34;
    };
    Playback& p = app_.playback();
    keyCap(c, x, y, "\xe2\x97\x80");
    item(40, app_.store().isEvent(p.entry()) ? "Games" : "Guide", "");
    if (app_.store().channels()->find(p.previousEntry())) {
      keyCap(c, x, y, "\xe2\x96\xb6");
      item(40, "Last channel", nameOf(p.previousEntry()));
    }
    const RemoteKeys& keys = app_.remoteKeys();
    hints::Hint sources = hints::playerSources(keys);
    int b = backups();
    std::string backupsLine = b > 0 ? std::to_string(b) + (b == 1 ? " backup" : " backups") : std::string();
    // Without a colour key or Menu, Sources is the row's first pill: one ▼.
    bool sourcesInRow = sources.colour == hints::Colour::None && sources.icons.empty();
    keyCap(c, x, y, "\xe2\x96\xbc");
    if (sourcesInRow) item(40, cameras_ ? "Cameras, Sources & more" : "Sources & more", backupsLine);
    else item(40, cameras_ ? "Cameras & more" : "More", "");
    keyCap(c, x, y, "OK");
    item(40, "Info", "");
    if (!sourcesInRow) item(hintKey(c, x, y, sources), sources.label, backupsLine);
    if (auto options = hints::playerOptions(keys)) item(hintKey(c, x, y, *options), options->label, "");
    if (zone_ != Zone::None || x > gfx::kLayoutWidth - kLeft - 360) return;
    // The load states, quietly, where "Hides in" sits once it plays.
    Status st = status(g);
    float right = gfx::kLayoutWidth - kLeft;
    if (st.text.empty()) {
      int left = static_cast<int>((bannerUntil_ > app_.ms() ? bannerUntil_ - app_.ms() : 0) / 1000) + 1;
      ui::label(c, "Hides in " + std::to_string(std::min(left, 6)) + " s", right, y + 10, Weight::Regular, 18,
                theme::white(0.62f), 300, Align::Right);
      return;
    }
    float tw = ui::textWidth(c, st.text, Weight::Medium, 19);
    float tx = right - tw;
    ui::label(c, st.text, tx, y + 9, Weight::Medium, 19, theme::white(0.8f));
    if (st.busy) ui::spinner(c, tx - 20, y + 21, 9);
    if (st.done) ui::labelIn(c, "\xe2\x9c\x93", {tx - 32, y + 8, 22, 26}, Weight::Bold, 19, theme::hex(kGreen), Align::Center);
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
        case Pill::Coast: title = g && g->west ? "Watch East \xe2\x86\x94" : "Watch West \xe2\x86\x94"; break;
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
        c.g.fill(r, theme::hex(0x14161B, 0.75f), 36);
        c.g.stroke(r, theme::white(0.22f), 36, 1);
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

  // 1C: once, the first time a channel plays.
  void drawTips(ui::Ctx& c) {
    ui::scrim(c, 0.55f);
    Rect card{460, 150, 1000, 780};
    c.g.fill(card, theme::hex(0x1C1F25, 0.97f), 34);
    float x = card.x + 64, y = card.y + 56;
    ui::label(c, "WATCHING LIVE TV", x, y, Weight::Bold, 18, theme::white(0.5f));
    ui::label(c, "Getting around", x, y + 30, Weight::Bold, 44, theme::white());
    y += 120;
    std::vector<hints::Tip> tips = hints::gettingAround(app_.remoteKeys());
    float colW = (card.w - 128 - 40) / 2;
    for (size_t i = 0; i < tips.size() && i < 6; ++i) {
      float tx = x + static_cast<float>(i % 2) * (colW + 40), ty = y + static_cast<float>(i / 2) * 96;
      Rect cap{tx, ty, 56, 56};
      c.g.fill(cap, theme::white(0.12f), 14);
      const hints::Hint& key = tips[i].key;
      unsigned icon = !key.icons.empty() && c.icons ? c.icons->get(key.icons.front()) : 0;
      if (key.colour != hints::Colour::None) c.g.fill({cap.x + 16, cap.y + 16, 24, 24}, theme::hex(kRed), 12);
      else if (icon) c.g.textureAlpha(icon, {cap.x + 14, cap.y + 14, 28, 28}, theme::white());
      else ui::labelIn(c, key.cap, cap, Weight::Bold, 18, theme::white(), Align::Center);
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
  std::string pendingFlip_;  // channel keys: the channel the banner shows, tuned once they stop
  uint32_t flipAt_ = 0;
  bool wasReady_ = false, pictureSeen_ = false;  // this tune's picture: now, and ever
  size_t pictureFeed_ = 0;  // the feed the picture last came from
  Zone zone_ = Zone::None;
  std::vector<Pill> pills_;
  int pill_ = 0;
  int failButton_ = 0;
  bool tips_ = false;
  bool pointer_ = false;
  std::string shownEntry_;
  std::string digits_;
  uint32_t digitsAt_ = 0;
  std::string missed_;  // a number typed that no channel has, shown for a moment
  uint32_t missedUntil_ = 0;
  std::string message_;  // a message in the banner's status line
  bool messageDone_ = false;
  uint32_t messageUntil_ = 0;
  mutable std::shared_ptr<const Listings> listings_;
};

}  // namespace

std::unique_ptr<Screen> makePlayer(App& app) { return std::make_unique<PlayerScreen>(app); }

}  // namespace marigold::app
