// Full screen playback (LiveTVOverlay). Up/Down (and CH, and swipes)
// always change channel: the banner after a flip only shows information
// (what's on, later on this channel, where Up and Down go, the keys). OK
// brings up the controls on the banner, Subtitles first (OK again turns
// them on or off; Aa or holding OK opens languages and style), then
// Sources, the guide, the last channel, Favorite, Settings; Up/Down still flip
// from there. Changes that stick (subtitles, favorites) say so in a large
// notice with how to undo them. Back closes what's on top, then asks once
// before leaving. Under a panel the player draws only the picture and its
// captions. Also: the one-time "Getting around" card, number keys (OK tunes
// at once), LG's Green as a CC key, Exit clearing the screen, and the Magic
// Remote pointer (the controls with nothing highlighted, a click on the
// picture opening or closing them, two edge buttons for Guide and Last).
// A game (a Live Events tile or feed, or a guide channel showing one the
// board matched) gets a lower card with one line of who's playing instead
// of the show's title, times and "Later on this channel".
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/captions/sentence_case.hpp"
#include "core/engine/views.hpp"
#include "core/input/hints.hpp"
#include "core/sports/game_banner.hpp"
#include "platform/icons.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr uint32_t kBannerMs = 5000;       // after the picture comes up
constexpr uint32_t kRowIdleMs = 12000;  // the controls hide after this long untouched
constexpr uint32_t kDoubleOkMs = 600;   // an OK this soon after the controls opened is a double press
constexpr uint32_t kWheelAfterClickMs = 300;  // the wheel turned by pressing it isn't a flip
constexpr uint32_t kBackAgainMs = 2500;  // "Press Back again to leave"
constexpr uint32_t kNoticeMs = 3500;     // "Subtitles on", "Added to Favorites"
constexpr uint32_t kFlipSettleMs = 500;    // channel keys: the channel stopped on tunes
constexpr uint32_t kStillTryingMs = 7000;  // a first tune without a picture yet
constexpr uint32_t kBackOnMs = 2000;       // "Back on CNN" after a backup took over
constexpr uint32_t kDigitsMs = 1500;
constexpr float kLeft = 96;
constexpr float kBannerBottom = 870;  // the banner's lower edge
constexpr float kRowY = 952;          // the key guide or the action row
constexpr float kStripW = 500;        // "Later on this channel"
constexpr float kCardTop = 656;       // the banner's card
constexpr float kGameCardTop = 836;   // a game's: one line of who's playing over the keys
constexpr float kGameMark = 60;       // its logos, flags or sport icon
constexpr float kGameText = 236;      // where its title and league line start
constexpr time::Seconds kGameRecheck = 30;  // which game a guide channel shows, asked again
constexpr uint32_t kRed = 0xE5372B, kBlue = 0x3D7BFF, kGreen = 0x34C759;

// Pointer targets.
constexpr int kHitPill = 1;      // + pill index
constexpr int kHitGuide = 50, kHitLast = 51;
constexpr int kHitFail = 60;     // + button index
constexpr int kHitTips = 70;
constexpr int kHitPicture = 80;  // the picture itself (under everything)

class PlayerScreen : public Screen {
  bool cameras_ = false;  // the race on screen has cameras (a Cameras button)

 public:
  explicit PlayerScreen(App& app) : Screen(app) { bannerFrom_ = app.ms(); }
  // Freed after the screen below has been shown again: the guide has taken
  // the picture into its corner by then. Anywhere else (Live Events, a
  // game's page, search), leaving the player ends the stream.
  ~PlayerScreen() override {
    Playback& p = app_.playback();
    if (p.active() && p.full()) p.stop();
  }

  // Holding OK on the Subtitles button opens languages and style.
  bool holdsOk() const override {
    return zone_ == Zone::Row && pill_ >= 0 && static_cast<size_t>(pill_) < pills_.size() &&
           pills_[static_cast<size_t>(pill_)] == Pill::Subtitles;
  }

  bool key(const KeyEvent& e) override {
    Playback& p = app_.playback();
    if (e.key == Key::Ok) lastOkAt_ = app_.ms();
    // One long swipe is one move here: one channel, one button.
    if (e.glide) {
      if (e.glide == glide_) return true;
      glide_ = e.glide;
    }
    if (tips_) {
      // "Getting around": OK or Back closes it; other keys wait.
      if (e.key == Key::Ok || e.key == Key::Back) closeTips();
      return true;
    }
    if (e.key == Key::Exit) {
      // LG Exit: clears the screen, stays on the channel.
      clearScreen();
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
    // OK while a number is typed: tune it now (a cable box's Enter).
    if (e.key == Key::Ok && !digits_.empty()) {
      commitDigits();
      return true;
    }
    switch (e.key) {
      case Key::Up:
      case Key::Down:
        // Up/Down always change channel. The pointer's wheel waits a moment
        // after a click (pressing the wheel turns it a notch).
        if (e.wheel && app_.ms() - lastOkAt_ < kWheelAfterClickMs) return true;
        flipBy(arrowStep(e.key));
        return true;
      case Key::ChannelUp:
      case Key::ChannelDown:
        flipBy(e.key == Key::ChannelUp ? 1 : -1);
        return true;
      case Key::Right:
      case Key::Rewind:  // Fire TV: the key a cable remote's Last sits on
        goPrevious();
        return true;
      case Key::Left:
      case Key::Guide:
        openGuide();
        return true;
      case Key::Ok:
      case Key::Menu:
        // The controls, Subtitles first. With the pointer, nothing is
        // highlighted until it's over a button.
        openControls(!pointer_);
        return true;
      case Key::Info:
        if (bannerVisible()) bannerUntil_ = app_.ms();
        else showBanner();
        return true;
      case Key::Green:
      case Key::Captions:
        quickSubtitles();
        return true;
      case Key::Red:
        openSources();
        return true;
      case Key::Blue:
        app_.nav().push(makeQuickSettings(app_, QuickPlace::Player));
        return true;
      case Key::PlayPause:
      case Key::Play:
      case Key::Pause:
        notice("Live TV can\xe2\x80\x99t be paused", "", 2500);
        return true;
      case Key::Back:
        return backKey();
      default:
        return false;
    }
  }

  void hover(int id) override {
    if (id >= kHitPill && id < kHitPill + static_cast<int>(pills_.size())) {
      zone_ = Zone::Row;
      pill_ = id - kHitPill;
      touch();
    } else if (id == kHitPicture) {
      // Over the picture: nothing highlighted, a click opens or closes the
      // controls.
      if (zone_ == Zone::Row) pill_ = -1;
      else if (zone_ != Zone::None) zone_ = Zone::None;
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
      noSubtitlesSaid_ = false;
      if (zone_ != Zone::Row) zone_ = Zone::None;
      prefetch(shownEntry_);
      cameras_ = app_.store().isEvent(shownEntry_) && app_.store().hasCameras(shownEntry_);
      if (cameras_) say("Onboard cameras: OK, then Cameras", 5000);
    }
    // The first picture ever: the one-time "Getting around" card.
    if (p.ready() && !p.failed() && !tips_ && !app_.store().settings().playerTipsShown) {
      tips_ = true;
      Settings s = app_.store().settings();
      s.playerTipsShown = true;
      app_.store().setSettings(s);
    }
    if (zone_ == Zone::Row && ms - rowTouched_ > kRowIdleMs && !pointer_) {
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
      if (!pictureSeen_) pictureAt_ = ms;
      pictureSeen_ = true;
    }
    // Subtitles on and none here: said once, quietly, while the banner is
    // still up from tuning in.
    if (pictureSeen_ && !noSubtitlesSaid_ && ms - pictureAt_ > 4500 && p.subtitleChoice().on) {
      noSubtitlesSaid_ = true;
      if (!offersSubtitles() && bannerVisible()) say("No subtitles on this channel so far", 3000);
    }
    wasReady_ = p.ready();
    if (p.ready()) pictureFeed_ = p.feedIndex();
    // A note from the recovery (buffering, reconnecting): the banner shows it.
    if (p.ready() && !p.note().empty()) {
      if (!bannerVisible()) bannerFrom_ = ms;
      bannerUntil_ = std::max(bannerUntil_, ms + 1500);
    }
    if (const auto& b = p.backupNotice(); b && ms - b->at > kBackOnMs) p.dismissBackupNotice();
    if (!digits_.empty() &&
        (ms - digitsAt_ >= kDigitsMs || engine::views::digitsSettled(digits_, *app_.store().channels())))
      commitDigits();
    bool bannerAnim = ms < bannerUntil_ + 50;
    bool note = !p.note().empty();
    if (!missed_.empty() && ms >= missedUntil_) missed_.clear();
    bool noticeUp = !notice_.empty() && ms < noticeUntil_ + 50;
    return opening || bannerAnim || note || !digits_.empty() || !missed_.empty() || p.backupNotice().has_value() ||
           zone_ == Zone::Row || !pendingFlip_.empty() || noticeUp;
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
    // A game: the low card (not while it fails: that banner says why).
    const GameBanner* game = p.failed() && !pending ? nullptr : gameShown(c);
    cardTop_ = game ? kGameCardTop : kCardTop;
    // Under a panel (the guide, Sources, Quick settings): the picture and its
    // captions only, never the banner beside the panel.
    if (c.covered) {
      if (picture) drawSubtitles(c, false);
      return;
    }
    // The pointer appearing brings up the controls, nothing highlighted.
    if (c.pointerActive && !pointer_ && !p.failed()) openControls(false);
    pointer_ = c.pointerActive;
    c.hit(screen, kHitPicture);
    bool controls = zone_ == Zone::Row && picture;
    if (p.failed() && !pending) {
      drawScrim(c);
      drawBanner(c, g, true);
      drawFailButtons(c);
    } else if (bannerVisible() || controls) {
      drawScrim(c);
      if (game) drawGameBanner(c, g, *game);
      else drawBanner(c, g, false);
      if (controls) drawPills(c, g);
      else drawKeyGuide(c, g);
    }
    if (picture) drawSubtitles(c, bannerVisible() || controls);
    if (pointer_ && picture) drawEdges(c);
    if (!digits_.empty()) {
      Rect r{gfx::kLayoutWidth - 80 - 220, 60, 220, 90};
      c.g.fill(r, theme::black(0.75f), 20);
      ui::labelIn(c, digits_, r, Weight::Bold, 54, theme::white(), Align::Center);
    } else if (!missed_.empty() && app_.ms() < missedUntil_) {
      // The number typed, struck through, and that there's no such channel.
      std::string line = "No channel " + missed_;
      float w = std::max(220.0f, ui::textWidth(c, line, Weight::Medium, 24) + 60);
      Rect r{gfx::kLayoutWidth - 80 - w, 60, w, 128};
      c.g.fill(r, theme::black(0.8f), 20);
      ui::labelIn(c, missed_, {r.x, r.y + 8, r.w, 70}, Weight::Bold, 54, theme::white(0.45f), Align::Center);
      float sw = ui::textWidth(c, missed_, Weight::Bold, 54);
      c.g.fill({r.x + (r.w - sw) * 0.5f - 6, r.y + 44, sw + 12, 4}, theme::hex(kRed, 0.9f), 2);
      ui::labelIn(c, line, {r.x, r.y + 80, r.w, 34}, Weight::Medium, 24, theme::white(0.85f), Align::Center);
    }
    drawNotice(c);
    if (tips_) drawTips(c);
  }

 private:
  enum class Zone { None, Row, EdgeGuide, EdgeLast };
  enum class Pill { Cameras, Subtitles, Style, Sources, Coast, Guide, Last, Favorite, More };

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
  // A change that sticks (subtitles, favorites) or a key that does nothing
  // here: a large card at the top, saying how to undo it.
  void notice(std::string title, std::string detail, uint32_t forMs) {
    notice_ = std::move(title);
    noticeDetail_ = std::move(detail);
    noticeUntil_ = app_.ms() + forMs;
  }
  // While it loads (or a channel key is pending) the banner stays up.
  bool loading() const {
    const Playback& p = app_.playback();
    return !pendingFlip_.empty() || !p.ready() || p.failed();
  }
  bool bannerVisible() const { return loading() || (app_.ms() >= bannerFrom_ && app_.ms() < bannerUntil_); }
  // Up/Down: Up is the channel above in the list (lower numbers) unless
  // Settings › Watching says Up goes to higher numbers, as CH+ does.
  int arrowStep(Key k) const {
    int up = app_.store().settings().upGoesHigher ? 1 : -1;
    return k == Key::Up ? up : -up;
  }
  // Channel keys: the banner shows the channel at once; the one stopped on
  // tunes after a moment (zapping past a channel doesn't open it).
  void flipBy(int step) {
    Playback& p = app_.playback();
    std::string target = p.flipTarget(pendingFlip_, step);
    if (target.empty()) return;
    zone_ = Zone::None;
    failButton_ = 0;
    backAskUntil_ = 0;
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
  // The banner with its buttons. `focus`: Subtitles highlighted (keys);
  // without, nothing is until the pointer is over a button.
  void openControls(bool focus) {
    if (!app_.playback().ready()) {
      // Still tuning in: the banner shows what's coming; no buttons yet.
      showBanner();
      return;
    }
    buildPills();
    zone_ = Zone::Row;
    pill_ = -1;
    if (focus)
      for (size_t i = 0; i < pills_.size(); ++i)
        if (pills_[i] == Pill::Subtitles) pill_ = static_cast<int>(i);
    controlsAt_ = app_.ms();
    backAskUntil_ = 0;
    touch();
  }
  void closeControls() {
    zone_ = Zone::None;
    bannerUntil_ = app_.ms();
  }
  // Exit (LG): the controls, the banner and any message go; the channel stays.
  void clearScreen() {
    zone_ = Zone::None;
    bannerUntil_ = app_.ms();
    digits_.clear();
    missed_.clear();
    notice_.clear();
    message_.clear();
    backAskUntil_ = 0;
  }
  // Back on a clear picture asks once before leaving the player; with the
  // banner up it only hides it.
  bool backKey() {
    if (!digits_.empty()) {
      digits_.clear();
      return true;
    }
    uint32_t ms = app_.ms();
    if (bannerVisible() && !loading()) {
      bannerUntil_ = ms;
      return true;
    }
    if (ms < backAskUntil_) {
      app_.nav().pop();
      return true;
    }
    backAskUntil_ = ms + kBackAgainMs;
    const std::string& prev = app_.playback().previousEntry();
    std::string detail = app_.store().channels()->find(prev) ? "\xe2\x96\xb6 Back to " + nameOf(prev) : std::string();
    notice("Press Back again to leave", detail, kBackAgainMs);
    return true;
  }
  // Green / CC: subtitles on or off in one press, from anywhere.
  void quickSubtitles() {
    Playback& p = app_.playback();
    bool on = p.toggleSubtitles();
    subtitlesNotice(on, "Press again to turn them " + std::string(on ? "off" : "on"));
  }
  void subtitlesNotice(bool on, const std::string& how) {
    Playback& p = app_.playback();
    if (!on) {
      notice("Subtitles off", how, kNoticeMs);
      return;
    }
    Playback::SubtitleStatus st = p.subtitleStatus();
    std::string title = st.label == "On" ? std::string("Subtitles on") : "Subtitles on \xc2\xb7 " + st.label;
    std::string where = p.spanishChannel() ? "On for every Spanish-language channel that has them"
                                           : "On for every channel that has them";
    notice(title, where + " \xc2\xb7 " + how, kNoticeMs);
  }
  void buildPills() {
    Playback& p = app_.playback();
    pills_.clear();
    if (cameras_) pills_.push_back(Pill::Cameras);
    pills_.push_back(Pill::Subtitles);
    pills_.push_back(Pill::Style);
    pills_.push_back(Pill::Sources);
    if (!otherCoast().empty()) pills_.push_back(Pill::Coast);
    pills_.push_back(Pill::Guide);
    if (app_.store().channels()->find(p.previousEntry())) pills_.push_back(Pill::Last);
    if (app_.store().channels()->find(p.entry())) pills_.push_back(Pill::Favorite);
    pills_.push_back(Pill::More);
    pill_ = std::clamp(pill_, -1, static_cast<int>(pills_.size()) - 1);
  }
  bool rowKey(const KeyEvent& e) {
    touch();
    int d = digitOf(e.key);
    if (d >= 0) {
      // Typing a number: the controls go, the number shows.
      closeControls();
      digits_ = std::string(1, static_cast<char>('0' + d));
      digitsAt_ = app_.ms();
      return true;
    }
    switch (e.key) {
      case Key::Left:
        pill_ = pill_ < 0 ? 0 : std::max(0, pill_ - 1);
        return true;
      case Key::Right:
        pill_ = pill_ < 0 ? 0 : std::min(static_cast<int>(pills_.size()) - 1, pill_ + 1);
        return true;
      case Key::Up:
      case Key::Down:
        // A swipe along the buttons that slants isn't a flip; nor is the
        // wheel turning while the pointer is on a button.
        if (e.swipe) return true;
        if (e.wheel && (pill_ >= 0 || app_.ms() - lastOkAt_ < kWheelAfterClickMs)) return true;
        flipBy(arrowStep(e.key));
        return true;
      case Key::ChannelUp:
      case Key::ChannelDown:
        flipBy(e.key == Key::ChannelUp ? 1 : -1);
        return true;
      case Key::Ok:
        // A second OK right after the first is a double press: ignored.
        if (app_.ms() - controlsAt_ < kDoubleOkMs) return true;
        if (pill_ < 0) {
          closeControls();  // a click on the picture
          return true;
        }
        if (e.longPress && pills_[static_cast<size_t>(pill_)] == Pill::Subtitles) {
          activate(Pill::Style);
          return true;
        }
        activate(pills_[static_cast<size_t>(pill_)]);
        return true;
      case Key::Back:
      case Key::Menu:
      case Key::Info:
        closeControls();
        return true;
      case Key::Green:
      case Key::Captions:
        quickSubtitles();
        return true;
      case Key::Red:
        closeControls();
        openSources();
        return true;
      case Key::Guide:
        closeControls();
        openGuide();
        return true;
      case Key::Rewind:
        closeControls();
        goPrevious();
        return true;
      case Key::PlayPause:
      case Key::Play:
      case Key::Pause:
        notice("Live TV can\xe2\x80\x99t be paused", "", 2500);
        return true;
      default:
        return true;
    }
  }
  void activate(Pill pill) {
    Playback& p = app_.playback();
    // A panel opens over the plain picture: back from it, the controls
    // are gone too.
    auto panel = [this](std::unique_ptr<Screen> s) {
      closeControls();
      app_.nav().push(std::move(s));
    };
    switch (pill) {
      case Pill::Cameras: panel(makeCameraPanel(app_)); break;
      case Pill::Subtitles: {
        bool on = p.toggleSubtitles();
        subtitlesNotice(on, std::string("OK again turns them ") + (on ? "off" : "on"));
        break;
      }
      case Pill::Style: panel(makeSubtitles(app_)); break;
      case Pill::Sources:
        closeControls();
        openSources();
        break;
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
      case Pill::Guide:
        closeControls();
        openGuide();
        break;
      case Pill::Last:
        closeControls();
        goPrevious();
        break;
      case Pill::Favorite: {
        bool was = false;
        if (const GuideChannel* g = app_.store().channels()->find(p.entry())) was = g->favorite;
        app_.store().toggleFavorite(p.entry());
        notice(was ? "Removed from Favorites" : "Added to Favorites", "OK again to undo", kNoticeMs);
        break;
      }
      case Pill::More: panel(makeQuickSettings(app_, QuickPlace::Player)); break;
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
    zone_ = Zone::None;
    bannerUntil_ = app_.ms();
    // Watching a game from Live Events: the event guide.
    app_.nav().push(app_.store().isEvent(p.entry()) ? makeEventGuide(app_) : makeMiniGuide(app_));
  }
  void goPrevious() {
    Playback& p = app_.playback();
    backAskUntil_ = 0;
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
  // The channel number typed: tuned (or said that there's none).
  void commitDigits() {
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
      missedUntil_ = app_.ms() + 2500;
      return;
    }
    Playback& p = app_.playback();
    flipped(p.tune(target) ? target : std::string());
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
      case Key::Down:
        flipBy(arrowStep(e.key));
        return true;
      case Key::ChannelUp:
      case Key::ChannelDown:
        flipBy(e.key == Key::ChannelUp ? 1 : -1);
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
  // The game on screen (or the one a channel key is on its way to), or
  // null for a channel's usual banner. From the board as it is: asked
  // again when what plays changes, the board is rebuilt, or every 30 s for
  // which game a guide channel is showing.
  const GameBanner* gameShown(const ui::Ctx& c) {
    std::string id = pendingFlip_.empty() ? app_.playback().entry() : pendingFlip_;
    uint64_t board = app_.store().events() ? app_.store().events()->version : 0;
    if (id != gameFor_ || board != gameBoard_ || c.now - gameAt_ >= kGameRecheck || c.now < gameAt_) {
      gameFor_ = id;
      gameBoard_ = board;
      gameAt_ = c.now;
      game_ = id.empty() ? std::nullopt : app_.store().gameBanner(id, c.now);
    }
    return game_ ? &*game_ : nullptr;
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
    // A word ("Click") gets a wider cap.
    float w = std::max(40.0f, ui::textWidth(c, glyph, Weight::Bold, 17) + 20);
    Rect r{x, y, w, 40};
    c.g.fill(r, theme::hex(0x14161B, 0.75f), 10);
    c.g.stroke(r, theme::white(0.28f), 10, 1);
    ui::labelIn(c, glyph, r, Weight::Bold, 17, theme::white(), Align::Center);
    return w;
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

  // The Subtitles pill: the stream carries captions, or this channel did
  // before (with subtitles off, LG only reads them once they're on).
  bool offersSubtitles() const {
    const Playback& p = app_.playback();
    if (p.subtitles().present()) return true;
    size_t i = p.feedIndex();
    return i < p.feeds().size() && app_.store().captionsKnown(p.feeds()[i].channelId);
  }

  // ---------------------------------------------------------- drawing
  // The captions, centred low (or at the top), lifted over the banner while
  // it shows. Players that draw them themselves (tvOS) leave nothing here.
  void drawSubtitles(ui::Ctx& c, bool banner) {
    const engine::Subtitles& subs = app_.playback().subtitles();
    const std::vector<captions::Line>& lines = subs.lines();
    if (lines.empty()) return;
    const Settings& set = app_.store().settings();
    static const int kPx[] = {36, 44, 56, 72};
    int px = kPx[std::clamp(set.subtitleSize, 0, 3)];
    float lineH = std::round(static_cast<float>(px) * 1.36f);
    float pad = std::round(static_cast<float>(px) * 0.4f);
    float total = lineH * static_cast<float>(lines.size());
    float y = set.subtitleTop ? 70.0f : (banner ? cardTop_ - 20 : gfx::kLayoutHeight - 70.0f) - total;
    bool sentences = set.subtitleSentenceCase;  // mixed-case text is left as it is
    bool startsSentence = true;
    for (const captions::Line& l : lines) {
      std::string text = sentences ? captions::sentenceCase(l.text, startsSentence) : l.text;
      char last = l.text.empty() ? ' ' : l.text.back();
      startsSentence = last == '.' || last == '!' || last == '?';
      float w = ui::textWidth(c, text, Weight::Medium, px);
      float x = std::round((gfx::kLayoutWidth - w) * 0.5f);
      float ty = y + std::round((lineH - static_cast<float>(px) * 1.2f) * 0.5f);
      switch (std::clamp(set.subtitleLook, 0, 2)) {
        case 0:
          c.g.fill({x - pad, y, w + 2 * pad, lineH}, theme::black(0.78f));
          break;
        case 1: {
          // A dark edge round each letter.
          static const float kRing[][2] = {{-2, -2}, {0, -2}, {2, -2}, {-2, 0}, {2, 0}, {-2, 2}, {0, 2}, {2, 2}};
          for (const auto& o : kRing) ui::label(c, text, x + o[0], ty + o[1], Weight::Medium, px, theme::black(0.9f));
          break;
        }
        default:
          ui::label(c, text, x + 1, ty + 3, Weight::Medium, px, theme::black(0.85f));
          ui::label(c, text, x + 2, ty + 4, Weight::Medium, px, theme::black(0.5f));
          break;
      }
      ui::label(c, text, x, ty, Weight::Medium, px, theme::white());
      y += lineH;
    }
  }

  // The banner and its row on one floating card (no fade over the picture).
  void drawScrim(ui::Ctx& c) { ui::floatingCard(c, {48, cardTop_, gfx::kLayoutWidth - 96, gfx::kLayoutHeight - 28 - cardTop_}); }

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
    // CC: this stream carries captions.
    if (!pending && p.subtitles().present()) {
      float ccW = ui::chipWidth(c, "CC", 15);
      c.g.stroke({lx, top + 42, ccW, 24}, theme::white(0.75f), 6, 2);
      ui::labelIn(c, "CC", {lx, top + 42, ccW, 24}, Weight::Bold, 15, theme::white(), Align::Center);
      lx += ccW + 12;
    }
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

  // A game's banner ("ID strip"): its logos (flags for players, the sport's
  // icon for a title), who's playing, your team's star and where it stands
  // by the clock; under it the league, round and venue; at the right the
  // channel and, in small type, its times and what's next. Never a score,
  // clock or result: the broadcaster's bug in the picture has those.
  void drawGameBanner(ui::Ctx& c, const GuideChannel* g, const GameBanner& b) {
    float x = kLeft, right = gfx::kLayoutWidth - kLeft;
    float top = kGameCardTop + 22;  // the title's line
    sports::banner::Status st = sports::banner::status(b.start, b.end, b.postponed, c.now);

    // At the right: the channel's logo, number and name (East/West), then
    // the listing times and what's next (before the start, what's on).
    std::string name = g ? (g->number ? std::to_string(*g->number) + " \xc2\xb7 " : std::string()) + g->name : b.channelName;
    std::string logoUrl = g ? g->logoUrl : b.channelLogo;
    bool logo = c.logos && !logoUrl.empty() && c.logos->get(logoUrl).id;
    bool coast = g && !g->partner.empty();
    const float logoW = 104, logoH = 44, gap = 14;
    float nameW = std::min(360.0f, ui::textWidth(c, name, Weight::Bold, 22));
    float coastW = coast ? ui::chipWidth(c, g->west ? "WEST" : "EAST", 14) : 0;
    float rowW = (logo ? logoW + gap : 0) + nameW + (coast ? gap + coastW : 0);
    const Listing* next = nullptr;
    const Listing* l = g ? current(g->id, c.now, &next) : nullptr;
    std::vector<std::string> times;
    using sports::banner::Chip;
    if (st.chip == Chip::soon) {
      times.push_back("Starts " + ui::clock(c, *b.start));
      if (l) times.push_back("on now: " + l->title);
    } else {
      if (st.chip == Chip::likelyOver && b.end) times.push_back("was scheduled until " + ui::clock(c, *b.end));
      else if (l) times.push_back(ui::range(c, l->start, l->end, true));
      else if (b.start) times.push_back("started " + ui::clock(c, *b.start));
      // Past the game's own listing, the show the guide has on now is next.
      const Listing* after = st.chip == Chip::likelyOver && l && b.start && l->start > *b.start ? l : next;
      if (after) times.push_back("next " + ui::clock(c, after->start) + " " + after->title);
    }
    std::string sub;
    for (const std::string& t : times) sub += (sub.empty() ? "" : " \xc2\xb7 ") + t;
    float subW = std::min(724.0f, ui::textWidth(c, sub, Weight::Regular, 19));
    float rightW = std::min(724.0f, std::max(rowW, subW));
    float rx = right - rowW;
    if (logo) {
      Rect r{rx, top, logoW, logoH};
      c.g.fill(r, theme::hex(0x14161B, 0.85f), 12);
      ui::logo(c, logoUrl, "", {r.x + 8, r.y + 5, r.w - 16, r.h - 10});
      rx += logoW + gap;
    }
    float nameY = top + (logoH - c.t.lineHeight(Weight::Bold, 22)) * 0.5f;
    shadowed(c, name, rx, nameY, Weight::Bold, 22, theme::white(0.9f), nameW + 1);
    if (coast) ui::coastChip(c, g->west, rx + nameW + gap, top + (logoH - 21) * 0.5f, 14);
    if (!sub.empty()) shadowed(c, sub, right - subW, top + 50, Weight::Regular, 19, theme::white(0.72f), subW + 1);

    // At the left: the marks, 60 tall.
    float my = top + 2;
    if (b.sides.size() == 2) {
      const EventSide& a = b.sides[0];
      const EventSide& h = b.sides[1];
      auto person = [](const EventSide& s) { return !s.flags.empty() || !s.country.empty(); };
      auto hasLogo = [&](const EventSide& s) { return c.logos && !s.logoUrl.empty() && c.logos->get(s.logoUrl).id; };
      if (person(a) || person(h)) {
        float w = events_ui::mark(c, a, x, my, kGameMark);
        events_ui::mark(c, h, x + w + 8, my, kGameMark);
      } else {
        // Badges overlap a little, as a pair; logos sit side by side.
        float step = hasLogo(a) || hasLogo(h) ? kGameMark + 4 : kGameMark - 12;
        events_ui::mark(c, a, x, my, kGameMark);
        events_ui::mark(c, h, x + step, my, kGameMark);
      }
    } else {
      Rect box{x, my, 120, kGameMark};
      c.g.fill(box, theme::white(0.1f), 12);
      if (unsigned icon = c.icons && !b.icon.empty() ? c.icons->get(b.icon) : 0)
        c.g.textureAlpha(icon, {box.x + 34, box.y + 4, 52, 52}, theme::white(0.85f));
      else
        ui::labelIn(c, b.symbol.empty() ? "\xe2\x97\x8f" : b.symbol, box, Weight::Bold, 30, theme::white(0.85f), Align::Center);
    }

    // Who's playing, the star, the status chip; the line under them.
    float tx = kGameText;
    float room = right - rightW - 40 - tx;
    float starW = b.favorite ? ui::textWidth(c, "\xe2\x98\x85", Weight::Bold, 30) + 12 : 0;
    float chipW = st.chip == Chip::none ? 0 : ui::textWidth(c, st.text, Weight::Bold, 16) + 16 + 16;
    float lineH = c.t.lineHeight(Weight::Bold, 36);
    float titleW = shadowed(c, b.title, tx, top, Weight::Bold, 36, theme::white(), std::max(80.0f, room - starW - chipW));
    float cx = tx + titleW;
    if (b.favorite) {
      ui::label(c, "\xe2\x98\x85", cx + 12, top + (lineH - c.t.lineHeight(Weight::Bold, 30)) * 0.5f, Weight::Bold, 30,
                theme::hex(theme::kStar));
      cx += starW;
    }
    events_ui::clockPill(c, st.chip, st.text, cx + 16, top + std::round((lineH - 26) * 0.5f));
    shadowed(c, b.line, tx, top + 46, Weight::Bold, 19, theme::white(0.62f), std::max(80.0f, room));
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

  // The keys under the banner, in plain words: the channels Up and Down go
  // to, the guide, the last channel, the controls, and LG's Green for CC.
  void drawKeyGuide(ui::Ctx& c, const GuideChannel* g) {
    (void)g;
    Playback& p = app_.playback();
    const RemoteKeys& keys = app_.remoteKeys();
    float x = kLeft, y = kRowY + 8;
    c.g.fill({kLeft, kRowY - 8, gfx::kLayoutWidth - 2 * kLeft, 1}, theme::white(0.14f));
    auto item = [&](float glyphW, const std::string& title, const std::string& extra) {
      x += glyphW + 12;
      x += shadowed(c, title, x, y + 6, Weight::Bold, 26, theme::white());
      if (!extra.empty()) x += 8 + shadowed(c, extra, x + 8, y + 6, Weight::Medium, 26, theme::white(0.85f), 240);
      x += 32;
    };
    // Where Up and Down go: their numbers (or names), so the direction is
    // never a guess.
    std::string from = pendingFlip_.empty() ? p.entry() : pendingFlip_;
    auto near = [&](Key k) {
      std::string id = p.flipTarget(from, arrowStep(k));
      const GuideChannel* n = id.empty() || id == from ? nullptr : app_.store().channels()->find(id);
      if (!n) return std::string();
      return n->number ? std::to_string(*n->number) : n->name;
    };
    std::string up = near(Key::Up), down = near(Key::Down);
    if (!up.empty() || !down.empty()) {
      keyCap(c, x, y, "\xe2\x96\xb2");
      x += 40 + 10;
      x += shadowed(c, up, x, y + 6, Weight::Bold, 26, theme::white(), 200) + 18;
      keyCap(c, x, y, "\xe2\x96\xbc");
      x += 40 + 10;
      x += shadowed(c, down, x, y + 6, Weight::Bold, 26, theme::white(), 200) + 32;
    }
    keyCap(c, x, y, "\xe2\x97\x80");
    item(40, app_.store().isEvent(p.entry()) ? "Games" : "Guide", "");
    if (app_.store().channels()->find(p.previousEntry())) {
      keyCap(c, x, y, "\xe2\x96\xb6");
      item(40, "Back to", nameOf(p.previousEntry()));
    }
    float okW = keyCap(c, x, y, keys.clickpad ? "Click" : "OK");
    item(okW, "Subtitles & more", "");
    if (keys.colour) item(dot(c, x, y, kGreen), "CC", "");
    if (zone_ != Zone::None || x > gfx::kLayoutWidth - kLeft - 300) return;
    // The load states, quietly, where "Hides in" sits once it plays.
    Status st = status(g);
    float right = gfx::kLayoutWidth - kLeft;
    if (st.text.empty()) {
      int left = static_cast<int>((bannerUntil_ > app_.ms() ? bannerUntil_ - app_.ms() : 0) / 1000) + 1;
      ui::label(c, "Hides in " + std::to_string(std::min(left, static_cast<int>(kBannerMs / 1000))) + " s", right - 300,
                y + 10, Weight::Regular, 22, theme::white(0.75f), 300, Align::Right);
      return;
    }
    float tw = ui::textWidth(c, st.text, Weight::Medium, 22);
    float tx = right - tw;
    ui::label(c, st.text, tx, y + 9, Weight::Medium, 22, theme::white(0.88f));
    if (st.busy) ui::spinner(c, tx - 22, y + 22, 10);
    if (st.done) ui::labelIn(c, "\xe2\x9c\x93", {tx - 34, y + 8, 24, 28}, Weight::Bold, 22, theme::hex(kGreen), Align::Center);
  }

  // The controls: Subtitles first (with its language), Aa for languages
  // and style, then Sources, the guide, the last channel, Favorite, Settings.
  void drawPills(ui::Ctx& c, const GuideChannel* g) {
    buildPills();
    Playback& p = app_.playback();
    struct Item {
      std::string title, extra;
    };
    std::vector<Item> items;
    for (Pill pill : pills_) {
      Item it;
      switch (pill) {
        case Pill::Cameras: it.title = "Cameras"; break;
        case Pill::Subtitles: {
          Playback::SubtitleStatus st = p.subtitleStatus();
          if (!st.on) {
            it.title = "Subtitles";
            it.extra = "Off";
          } else if (st.label == "On") {
            it.title = "Subtitles";
            it.extra = "On \xc2\xb7 " + st.detail;
          } else {
            it.title = st.label + " \xe2\x9c\x93";
            it.extra = st.detail;
          }
          break;
        }
        case Pill::Style: it.title = "Aa"; break;
        case Pill::Sources: it.title = "Sources"; it.extra = backups() > 0 ? feedLine() : std::string(); break;
        case Pill::Coast: it.title = g && g->west ? "Watch East \xe2\x86\x94" : "Watch West \xe2\x86\x94"; break;
        case Pill::Guide: it.title = app_.store().isEvent(p.entry()) ? "Games" : "Guide"; break;
        case Pill::Last: it.title = "Back to " + nameOf(p.previousEntry()); break;
        case Pill::Favorite: it.title = g && g->favorite ? "Unfavorite" : "Favorite"; break;
        case Pill::More: it.title = "Settings"; break;
      }
      items.push_back(std::move(it));
    }
    const float gap = 14, height = 80, room = gfx::kLayoutWidth - 2 * kLeft;
    auto widthOf = [&](const Item& it) {
      float w = ui::textWidth(c, it.title, Weight::Bold, 28) + 56;
      if (!it.extra.empty()) w += ui::textWidth(c, it.extra, Weight::Medium, 24) + 14;
      return w;
    };
    // Too many to fit: the second words go, Sources' first, then the rest.
    float total = 0;
    for (const Item& it : items) total += widthOf(it) + gap;
    for (size_t i = 0; i < items.size() && total > room; ++i) {
      if (pills_[i] == Pill::Subtitles || items[i].extra.empty()) continue;
      total -= widthOf(items[i]);
      items[i].extra.clear();
      total += widthOf(items[i]);
    }
    float x = kLeft, y = kRowY - 6;
    for (size_t i = 0; i < items.size(); ++i) {
      const Item& it = items[i];
      float w = widthOf(it);
      if (x + w > kLeft + room) break;
      Rect r{x, y, w, height};
      c.hit(r, kHitPill + static_cast<int>(i));
      bool f = static_cast<int>(i) == pill_;
      if (f) {
        Rect big{r.x - r.w * 0.02f, r.y - 1.5f, r.w * 1.04f, r.h + 3};
        c.g.fill(big, theme::white(), height * 0.5f + 2);
        c.g.stroke({big.x - 4, big.y - 4, big.w + 8, big.h + 8}, theme::black(0.85f), height * 0.5f + 6, 3);
      } else {
        c.g.fill(r, theme::hex(0x14161B, 0.85f), height * 0.5f);
        c.g.stroke(r, theme::white(0.3f), height * 0.5f, 1.5f);
      }
      Color ink = f ? theme::black() : theme::white();
      float tx = r.x + 28;
      tx += ui::label(c, it.title, tx, r.y + 22, Weight::Bold, 28, ink);
      if (!it.extra.empty())
        ui::label(c, it.extra, tx + 14, r.y + 25, Weight::Medium, 24, f ? theme::black(0.7f) : theme::white(0.75f));
      x += w + gap;
    }
  }

  // A change that sticks, or "Press Back again to leave": large, at the top.
  void drawNotice(ui::Ctx& c) {
    if (notice_.empty() || app_.ms() >= noticeUntil_) return;
    float tw = ui::textWidth(c, notice_, Weight::Bold, 40);
    float dw = noticeDetail_.empty() ? 0 : ui::textWidth(c, noticeDetail_, Weight::Medium, 26);
    float w = std::min(1500.0f, std::max(tw, dw) + 88);
    float h = noticeDetail_.empty() ? 96 : 140;
    Rect r{(gfx::kLayoutWidth - w) * 0.5f, 64, w, h};
    c.g.fill(r, theme::hex(0x0C0E12, 0.94f), 26);
    c.g.stroke(r, theme::white(0.18f), 26, 2);
    ui::labelIn(c, notice_, {r.x + 44, r.y + 18, r.w - 88, 56}, Weight::Bold, 40, theme::white(), Align::Center);
    if (!noticeDetail_.empty())
      ui::labelIn(c, noticeDetail_, {r.x + 44, r.y + 78, r.w - 88, 40}, Weight::Medium, 26, theme::white(0.88f),
                  Align::Center);
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

  // 1C: once, the first time a channel plays (Settings › Help & info shows
  // it again).
  void drawTips(ui::Ctx& c) { drawGettingAround(c, app_.remoteKeys(), false, kHitTips); }

  uint32_t bannerFrom_ = 0, bannerUntil_ = 0, rowTouched_ = 0;
  std::string pendingFlip_;  // channel keys: the channel the banner shows, tuned once they stop
  uint32_t flipAt_ = 0;
  bool wasReady_ = false, pictureSeen_ = false;  // this tune's picture: now, and ever
  uint32_t pictureAt_ = 0;                        // when this tune's first picture came
  bool noSubtitlesSaid_ = false;
  size_t pictureFeed_ = 0;  // the feed the picture last came from
  Zone zone_ = Zone::None;
  std::vector<Pill> pills_;
  int pill_ = 0;
  int failButton_ = 0;
  bool tips_ = false;
  bool pointer_ = false;
  uint32_t controlsAt_ = 0;    // when the controls last opened
  uint32_t lastOkAt_ = 0;      // the last OK (or click)
  uint32_t glide_ = 0;         // the swipe that last moved (Apple TV)
  uint32_t backAskUntil_ = 0;  // "Press Back again to leave" is up
  std::string notice_, noticeDetail_;  // the large card at the top
  uint32_t noticeUntil_ = 0;
  std::string shownEntry_;
  std::string digits_;
  uint32_t digitsAt_ = 0;
  std::string missed_;  // a number typed that no channel has, shown for a moment
  uint32_t missedUntil_ = 0;
  std::string message_;  // a message in the banner's status line
  bool messageDone_ = false;
  uint32_t messageUntil_ = 0;
  mutable std::shared_ptr<const Listings> listings_;
  float cardTop_ = kCardTop;        // the card this frame: a game's is lower
  std::optional<GameBanner> game_;  // the game banner for gameFor_, if it's a game
  std::string gameFor_;
  time::Seconds gameAt_ = 0;
  uint64_t gameBoard_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makePlayer(App& app) { return std::make_unique<PlayerScreen>(app); }

}  // namespace marigold::app
