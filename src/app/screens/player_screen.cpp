// Full screen playback (LiveTVOverlay). Up/Down (and CH, and swipes)
// always change channel: the banner after a flip only shows information
// (what's on, later on this channel, where Up and Down go, the keys). OK
// brings up the controls on the banner: round icon buttons in three groups
// (Build 169, engine::controls): watching (Live, Pause, Rewind where the
// channel has replay), this channel (Subtitles, Sources, East/West,
// Cameras), elsewhere (Back, Guide, Favorite, Settings); only the focused
// one (or the one under the pointer) opens into a pill with its label and
// state. Up/Down still flip from there. Changes that stick (subtitles,
// favorites) say so in a large notice with how to undo them. Back closes
// what's on top, then asks once before leaving. Under a panel the player
// draws only the picture and its captions. Also: the one-time "Getting
// around" card, number keys (OK tunes at once), LG's Green as a CC key,
// Exit clearing the screen, and the Magic Remote pointer (the controls with
// nothing highlighted, a click on the picture opening or closing them, two
// edge buttons for Guide and Last).
// A game (a Live Events tile or feed, or a guide channel showing one the
// board matched) gets a lower card with one line of who's playing instead
// of the show's title, times and "Later on this channel". A show's banner
// has the guide's episode after its times and its description in the band
// under them, at today's height (Build 119).
//
// Build 165, replay (channels with a copy that keeps catch-up): Pause and
// Rewind at the front of the controls, with the programme's bar above them;
// the Rewind timeline; the paused panel; behind live, Live first and a
// tag at the top right (Build 169: for a few seconds after each jump, never
// over the banner or the controls). Build 169: no Start over in the row or
// on the banner; Rewind and the guide's and Live Events' hold-OK menus
// start a programme or a game over. Build 170: Start over back in the row,
// after Rewind, where it can work (engine::controls::startOver); still not
// on the banner. Behind live the bar runs from the
// programme watched to live, always its right end. Channels without
// catch-up have no replay buttons and no bar.
//
// Build 177, Skip break in a sports game's replay (engine::breaks): no new
// key. During a break the top-right tag says "BREAK · about 2 min · OK to
// skip" (its few seconds from when the break begins); OK opens the controls
// on Skip break (after Start over); OK on it plays from just before the
// next play and the notice says "Skipped a 2 min break · OK again to go
// back" (OK while it shows goes back). A break that runs to live: "live is
// in it too", no button. The Rewind timeline and the replay bar draw the
// breaks hatched; the timeline's Up / Down stop at each break's end.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/events_ui.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/captions/caption_style.hpp"
#include "core/captions/sentence_case.hpp"
#include "core/engine/breaks.hpp"
#include "core/engine/controls.hpp"
#include "core/engine/replay.hpp"
#include "core/engine/views.hpp"
#include "core/input/countdown.hpp"
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

constexpr uint32_t kBannerMs = 5000;       // after the picture comes up, with or without a description
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
// Between the two sides' marks, and after them before the title: a player
// a side, two flags 60 wide end 12 short of kGameText. A doubles pair's
// two flags each side are wider: the title starts that gap after them
// (Build 141; it used to stay at kGameText, under the second pair).
constexpr float kGameMarkGap = 8;
constexpr float kGameTitleGap = kGameText - kLeft - 2 * kGameMark - kGameMarkGap;  // 12
constexpr time::Seconds kGameRecheck = 30;  // which game a guide channel shows, asked again
constexpr uint32_t kRed = 0xE5372B, kBlue = 0x3D7BFF, kGreen = 0x34C759;

// Pointer targets.
constexpr int kHitPill = 1;      // + pill index
constexpr int kHitGuide = 50, kHitLast = 51;
constexpr int kHitFail = 60;     // + button index
constexpr int kHitTips = 70;
constexpr int kHitPicture = 80;  // the picture itself (under everything)
constexpr int kHitPaused = 90;   // + a paused panel button's index
constexpr int kHitPlayHere = 99;  // the timeline's "Play from" button
constexpr int kHitEndCard = 95;    // + "End of the game"'s button (Build 167)
constexpr int kHitBar = 100;      // + a slice of the timeline's bar
constexpr int kBarSlices = 160;
// Build 165, replay.
using ReplayPhase = engine::Session::Replay::Phase;

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
    // What was typed in the player's search goes with it (Build 132).
    p.setPlayerQuery({});
  }

  bool key(const KeyEvent& e) override {
    Playback& p = app_.playback();
    if (e.key == Key::Ok) lastOkAt_ = app_.ms();
    // The Rewind timeline: every swipe moves the playhead (a long one, as
    // a held key does).
    if (timeline_ && pendingFlip_.empty() && !tips_) return timelineKey(e);
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
    if (pausedPanel() && pendingFlip_.empty()) return pausedKey(e);
    if (endCard() && endCardKey(e)) return true;
    if (p.failed() || p.noPicture()) return failedKey(e);
    if (zone_ == Zone::Row) return rowKey(e);
    // Build 177: OK while "Skipped a 2 min break · OK again to go back"
    // shows (a second OK right after the skip is a double press: ignored).
    if (e.key == Key::Ok && backToBreakOffered()) {
      if (app_.ms() - skippedMs_ >= kDoubleOkMs) backToBreak();
      return true;
    }
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
        playPause(e.key);
        return true;
      case Key::FastForward:
        // Fire TV: the Rewind timeline (its Rewind key goes back to the last).
        openTimeline();
        return true;
      case Key::Back:
        return backKey();
      default:
        return false;
    }
  }

  void hover(int id) override {
    if (timeline_) {
      // The bar picks a time; the "Play from" button plays.
      barHover_ = id >= kHitBar && id < kHitBar + kBarSlices ? id - kHitBar : -1;
      playHover_ = id == kHitPlayHere;
      return;
    }
    if (pausedPanel()) {
      if (id >= kHitPaused && id < kHitPaused + 3) pausedButton_ = id - kHitPaused;
      else if (id == kHitPicture) pausedButton_ = -1;
      return;
    }
    if (endCard() && (id == kHitEndCard || id == kHitEndCard + 1)) {
      endCardButton_ = id - kHitEndCard;
      return;
    }
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
    if (!p.active()) {
      app_.nav().pop();
      return false;
    }
    // Drawn again only when something on it changes: a moment due since the
    // last tick (a flip settling, a typed number's pause, a message or
    // notice ending, the controls hiding, a spinner's step) or a change
    // made here; asleep in between (wakeAt()). The app draws it as the
    // picture comes, a note shows or the channel gives up.
    bool changed = wake_.reached(ms);
    wake_.clear();
    // Channel keys: the channel stopped on tunes.
    if (!pendingFlip_.empty() && ms >= flipAt_) {
      std::string id = std::move(pendingFlip_);
      pendingFlip_.clear();
      if (id != p.entry()) flipped(p.tune(id) ? id : std::string());
      changed = true;
    }
    if (replayTick(ms)) changed = true;
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
      changed = true;
    }
    // The first picture ever: the one-time "Getting around" card.
    if (p.ready() && !p.failed() && !tips_ && !app_.store().settings().playerTipsShown) {
      tips_ = true;
      Settings s = app_.store().settings();
      s.playerTipsShown = true;
      app_.store().setSettings(s);
      changed = true;
    }
    if (zone_ == Zone::Row && ms - rowTouched_ > kRowIdleMs && !pointer_) {
      zone_ = Zone::None;
      bannerUntil_ = ms;
      changed = true;
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
      changed = true;
    }
    // Subtitles on and none here: said once, quietly, while the banner is
    // still up from tuning in.
    if (pictureSeen_ && !noSubtitlesSaid_ && p.subtitleChoice().on) {
      if (ms - pictureAt_ > 4500) {
        noSubtitlesSaid_ = true;
        if (!offersSubtitles() && bannerVisible()) {
          say("No subtitles on this channel so far", 3000);
          changed = true;
        }
      } else {
        wake_.at(pictureAt_ + 4501);
      }
    }
    wasReady_ = p.ready();
    if (p.ready()) pictureFeed_ = p.feedIndex();
    // A note from the recovery (buffering, reconnecting): the banner shows it.
    if (p.ready() && !p.note().empty()) {
      if (!bannerVisible()) bannerFrom_ = ms;
      bannerUntil_ = std::max(bannerUntil_, ms + 1500);
    }
    if (const auto& b = p.backupNotice(); b && ms - b->at > kBackOnMs) {
      p.dismissBackupNotice();
      changed = true;
    } else if (b) {
      wake_.at(b->at + kBackOnMs + 1);
    }
    if (!digits_.empty() &&
        (ms - digitsAt_ >= kDigitsMs || engine::views::digitsSettled(digits_, *app_.store().channels()))) {
      commitDigits();
      changed = true;
    }
    // The banner at rest: drawn again only when its "Hides in" second moves
    // on, a message on it ends, and it hides. (The controls show no
    // countdown: only their hiding.)
    bool message = !message_.empty() && ms < messageUntil_;
    int second = bannerVisible() ? hidesIn(ms) * 2 + (message ? 1 : 0) : 0;
    if (zone_ == Zone::Row) second = bannerVisible() ? 1 : 0;
    if (second != bannerSecond_) changed = true;
    bannerSecond_ = second;
    if (second > 0 && !loading() && zone_ != Zone::Row) wake_.at(countdown::nextChange(bannerUntil_, ms));
    if (message) wake_.at(messageUntil_);
    if (zone_ == Zone::Row && !pointer_) wake_.at(rowTouched_ + kRowIdleMs + 1);
    // Spinners ("Tuning in", "Reconnecting…", the one where the picture
    // will be): a step every 125 ms.
    if (!p.failed() && (!pendingFlip_.empty() || !p.ready() || !p.note().empty())) {
      if (spinner_.moved(ms)) changed = true;
      wake_.at(spinner_.next(ms));
    }
    if (!pendingFlip_.empty()) wake_.at(flipAt_);
    if (!digits_.empty()) wake_.at(digitsAt_ + kDigitsMs);
    if (!missed_.empty()) {
      if (ms >= missedUntil_) {
        missed_.clear();
        changed = true;
      } else {
        wake_.at(missedUntil_);
      }
    }
    if (!notice_.empty() && ms < noticeUntil_) wake_.at(noticeUntil_);
    return changed;
  }

  uint32_t wakeAt() const override { return wake_.get(); }
  const char* name() const override { return "player"; }

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
    if (!pending && (timeline_ || pausedPanel())) {
      // The picture held (or what the player leaves when the stream has
      // closed), dimmed, under the timeline or the paused panel.
      c.g.hole(screen);
      if (c.covered) return;
      pointer_ = c.pointerActive;
      c.hit(screen, kHitPicture);
      if (timeline_) drawTimeline(c, g);
      else drawPaused(c, g);
      drawNotice(c);
      return;
    }
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
    const GameBanner* game = (p.failed() || p.noPicture()) && !pending ? nullptr : gameShown(c);
    cardTop_ = game ? kGameCardTop : kCardTop;
    // Under a panel (the guide, Sources, Quick settings): the picture and its
    // captions only, never the banner beside the panel.
    if (c.covered) {
      if (picture) drawSubtitles(c, false);
      return;
    }
    // The pointer appearing brings up the controls, nothing highlighted.
    if (c.pointerActive && !pointer_ && !p.failed() && !p.noPicture()) openControls(false);
    pointer_ = c.pointerActive;
    c.hit(screen, kHitPicture);
    bool controls = zone_ == Zone::Row && picture;
    if ((p.failed() || p.noPicture()) && !pending) {
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
    if (!pending) drawBehindTag(c);
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
  using Ctl = engine::controls::Button;

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
    return !pendingFlip_.empty() || !p.ready() || p.failed() || p.noPicture();
  }
  bool bannerVisible() const { return loading() || (app_.ms() >= bannerFrom_ && app_.ms() < bannerUntil_); }
  // The banner's "Hides in" seconds (1 in its last second).
  int hidesIn(uint32_t ms) const { return countdown::shown(bannerUntil_, ms); }
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
    // Build 170: where Start over goes, worked out once as the row opens.
    startOverFor_.clear();
    buildPills();
    zone_ = Zone::Row;
    // Subtitles first; behind live, Live; Build 177: during a break to
    // skip, Skip break.
    pill_ = focus ? engine::controls::opening(pills_, app_.playback().replay().has_value()) : -1;
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
    std::string back = backName();
    std::string detail = back.empty() ? std::string() : "\xe2\x96\xb6 Back to " + back;
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
  // Build 169: the row's buttons (engine::controls::row): replay's at the
  // front where a copy keeps an archive (engine::replay::buttons: behind
  // live, Live first and, on LG, Pause after it), then this channel's,
  // then elsewhere. Build 170: Start over after Rewind where it can work.
  void buildPills() {
    Playback& p = app_.playback();
    // Start over's start: again when the row opens, or what it's for
    // changes (another channel, live or behind) while it's open.
    std::string key = p.entry() + (p.replay() ? " behind" : " live");
    if (key != startOverFor_) {
      startOverFor_ = key;
      startOverAt_ = startOverAt();
    }
    engine::controls::RowIn in;
    in.replayDays = p.catchupDays();
    in.behind = p.replay().has_value();
    in.lgPause = app_.window().remoteKeys() == RemoteKeys::lg();
    in.startOver = startOverAt_.has_value();
    in.skipBreak = breakMoment().kind == engine::breaks::Moment::Kind::Skip;
    in.bothVersions = !otherCoast().empty();
    in.cameras = cameras_;
    in.back = !backName().empty();
    in.favorite = app_.store().channels()->find(p.entry()) != nullptr;
    pills_ = engine::controls::row(in);
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
        playPause(e.key);
        return true;
      case Key::FastForward:
        closeControls();
        openTimeline();
        return true;
      default:
        return true;
    }
  }
  void activate(Ctl pill) {
    Playback& p = app_.playback();
    // A panel opens over the plain picture: back from it, the controls
    // are gone too.
    auto panel = [this](std::unique_ptr<Screen> s) {
      closeControls();
      app_.nav().push(std::move(s));
    };
    switch (pill) {
      case Ctl::Live:
        closeControls();
        p.goLive();
        showBanner();
        break;
      case Ctl::Pause:
        playPause(Key::Pause);
        break;
      case Ctl::Rewind:
        closeControls();
        openTimeline();
        break;
      case Ctl::StartOver:
        // Build 170: the replay from the programme's (or the game's) start,
        // as the guide's hold-OK Start over plays it.
        closeControls();
        if (startOverAt_) startOver(*startOverAt_);
        break;
      case Ctl::SkipBreak:
        closeControls();
        skipBreak();
        break;
      case Ctl::Cameras: panel(makeCameraPanel(app_)); break;
      // Build 169: one Subtitles button: its panel has the languages and
      // their look (Green still turns them on or off from anywhere).
      case Ctl::Subtitles: panel(makeSubtitles(app_)); break;
      case Ctl::Sources:
        closeControls();
        openSources();
        break;
      case Ctl::Swap: {
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
      case Ctl::Guide:
        closeControls();
        openGuide();
        break;
      case Ctl::Back:
        closeControls();
        goPrevious();
        break;
      case Ctl::Favorite: {
        bool was = app_.store().isFavorite(p.entry());
        app_.store().toggleFavorite(p.entry());
        notice(was ? "Removed from Favorites" : "Added to Favorites", "OK again to undo", kNoticeMs);
        break;
      }
      case Ctl::Settings: panel(makeQuickSettings(app_, QuickPlace::Player)); break;
    }
  }
  void startOver(time::Seconds at) {
    Playback& p = app_.playback();
    if (!p.replayFrom(at, engine::Session::ReplayWhy::StartOver)) return;
    zone_ = Zone::None;
    showBanner();
  }
  // Build 177: Skip break: the replay plays from just before the next play
  // (engine::breaks::playFrom, where this channel's archive can start), as
  // the timeline's "Play from" does; the notice says so and how to go back,
  // as Favorites' "OK again to undo". No banner: the tag says where it is.
  void skipBreak() {
    Playback& p = app_.playback();
    engine::breaks::Moment m = breakMoment();
    if (m.kind != engine::breaks::Moment::Kind::Skip || !m.in) return;
    time::Seconds now = time::now(), at = p.replayAt();
    time::Seconds back =
        engine::breaks::backTo(at, p.reach(), engine::replay::archiveStart(now, p.catchupDays()), now);
    time::Seconds from = m.in->from, saved = m.saves;
    if (!p.replayFrom(m.target, engine::Session::ReplayWhy::Rewind)) return;
    p.logNote("replay", "skipped a break (" + engine::breaks::lengthText(saved) + ")");
    skipped_ = from;
    undoTo_ = back;
    notice(engine::breaks::skippedTitle(saved), engine::breaks::kBackLine, kNoticeMs);
    undoUntil_ = noticeUntil_;
    skippedMs_ = app_.ms();
    zone_ = Zone::None;
  }
  // "OK again to go back": while that notice shows, for the replay it skipped.
  bool backToBreakOffered() const {
    uint32_t ms = app_.ms();
    return undoUntil_ && undoUntil_ == noticeUntil_ && ms < undoUntil_ && !notice_.empty() &&
           app_.playback().replay().has_value();
  }
  void backToBreak() {
    Playback& p = app_.playback();
    time::Seconds to = undoTo_;
    undoUntil_ = 0;
    notice_.clear();
    skipped_ = 0;
    if (!p.replayFrom(to, engine::Session::ReplayWhy::Back)) return;
    p.logNote("replay", "went back to the break");
    zone_ = Zone::None;
  }
  // Build 170: where the row's Start over goes (engine::controls::
  // startOver), or none: the start of the game on screen, else of the
  // programme on at the moment shown (live: the guide's listing on now, a
  // game's on its channel; behind live: the replay banner's listing at the
  // replay's moment).
  std::optional<time::Seconds> startOverAt() {
    Playback& p = app_.playback();
    engine::controls::StartOverIn in;
    in.replayDays = p.catchupDays();
    if (!in.replayDays) return std::nullopt;
    in.now = time::now();
    if (p.replay()) in.replayAt = p.replayAt();
    if (app_.store().isEvent(p.entry())) {
      if (std::optional<GameBanner> b = app_.store().gameBanner(p.entry(), in.now)) in.gameStart = b->start;
    }
    auto ch = app_.store().channels();
    const GuideChannel* g = ch->find(p.entry());
    if (!g && !p.feeds().empty())
      g = ch->find(app_.store().entryOfChannel(p.feeds()[std::min(p.feedIndex(), p.feeds().size() - 1)].channelId));
    if (g) {
      const Listing* on = in.replayAt ? replayBanner(*g, *in.replayAt, in.now).show : current(g->id, in.now, nullptr);
      if (on) in.showStart = on->start;
    }
    std::optional<time::Seconds> at = engine::controls::startOver(in);
    // Build 172: where this channel's replay can really start, at or just
    // before it (an archive in chunks it can't skip into).
    if (at) {
      time::Seconds snapped = engine::replay::startOverTarget(
          p.reach(), *at, engine::replay::archiveStart(in.now, in.replayDays), in.now);
      if (in.now - snapped < engine::Session::kLiveWithin) return std::nullopt;
      at = snapped;
    }
    return at;
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
  // Right: back to what was viewed last, a channel or a game (the session
  // says so when it can't, or when a game has ended and its channel plays).
  void goPrevious() {
    Playback& p = app_.playback();
    backAskUntil_ = 0;
    if (!p.previousTarget()) {
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
    // No picture anywhere, the sound playing: another source, or the sound.
    if (app_.playback().noPicture()) return {"Try another source", "Keep listening"};
    std::vector<std::string> out = {"Try again", "Pick a feed"};
    if (std::string back = backName(); !back.empty()) out.push_back("Back to " + back);
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
        if (p.noPicture()) {
          // "Keep listening" remembers the channel as sound only for a week.
          if (failButton_ == 0) openSources();
          else p.keepListening();
          failButton_ = 0;
          return true;
        }
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
      case Key::Back:
        // The no-picture card goes (the sound plays on); a failed channel leaves.
        if (app_.playback().noPicture()) app_.playback().dismissNoPicture();
        else app_.nav().pop();
        return true;
      default: return true;
    }
  }

  // ---------------------------------------------------------- replay
  // Build 165. The session plays the archive; this is what shows of it.

  // The paused panel: paused, with the Rewind timeline closed.
  bool pausedPanel() const {
    const auto& r = app_.playback().replay();
    return r && r->phase == ReplayPhase::Paused && !timeline_;
  }

  // Play/Pause, Play and Pause: on a channel with replay they pause and
  // play on; elsewhere live TV can't be paused, as always.
  void playPause(Key k) {
    Playback& p = app_.playback();
    const auto& r = p.replay();
    if (r && r->phase == ReplayPhase::Paused) {
      if (k != Key::Pause) resumeReplay();
      return;
    }
    if (p.catchupDays() == 0) {
      notice("Live TV can\xe2\x80\x99t be paused", "", 2500);
      return;
    }
    if (k == Key::Play) return;  // playing already
    closeControls();
    if (!p.pause()) {
      notice("Live TV can\xe2\x80\x99t be paused", "", 2500);
      return;
    }
    pausedButton_ = 0;
  }
  void resumeReplay() {
    app_.playback().resumePaused();
    pausedButton_ = 0;
    zone_ = Zone::None;
    showBanner();
  }
  // Every tick: the session's notices, the tag's text, the timeline's live
  // edge and the paused panel's clock. True when something shown changed.
  bool replayTick(uint32_t ms) {
    Playback& p = app_.playback();
    bool changed = false;
    // Build 167: a Final's replay that can't go on: back to its card, with
    // the notice (this screen goes, and its stream with it).
    if (p.takeGameBack()) {
      std::string said = p.takeReplayNotice();
      const engine::Session::GameReplay* g = p.gameReplay();
      std::string id = g ? g->eventId : p.entry();
      App& app = app_;
      app.toast(said.empty() ? std::string("Replay isn\xe2\x80\x99t available right now on this channel") : said);
      app.nav().pop();
      app.nav().push(makeEventDetail(app, id));
      return true;
    }
    // ... and past the moment the app saw the final: "End of the game", for
    // 15 s while it plays on.
    if (p.takeEndOfGame()) {
      endCardUntil_ = std::max<uint32_t>(1, ms + engine::Session::kEndCardMs);
      endCardButton_ = 0;
      closeControls();
      changed = true;
    }
    if (endCardUntil_ && (ms >= endCardUntil_ || !finalReplay())) {
      endCardUntil_ = 0;
      changed = true;
    }
    if (endCardUntil_) wake_.at(endCardUntil_);
    std::string said = p.takeReplayNotice();
    if (!said.empty()) {
      notice(said, "", 3000);
      changed = true;
    }
    const auto& r = p.replay();
    if (timeline_ && p.entry() != timelineEntry_) {
      timeline_ = false;  // the channel changed under it
      changed = true;
    }
    if (timeline_ && !headMoved_) {
      // Build 175: it plays on under the timeline; until a time is picked,
      // the timeline's mark follows what plays (a replay that caught up
      // with live is live now).
      From from = !r ? From::Live : r->phase == ReplayPhase::Paused ? From::Paused : From::Replay;
      time::Seconds at = from == From::Live ? time::now() : p.replayAt();
      if (from != timelineFrom_ || at != head_) {
        timelineFrom_ = from;
        head_ = at;
        span_ = engine::replay::follow(span_, head_, earliest_, time::now());
        changed = true;
      }
    }
    if (!pausedPanel()) pausedButton_ = 0;
    // What the screen shows of the replay's clocks, once a second.
    std::string look;
    if (r && r->phase != ReplayPhase::Paused) look = engine::replay::behindText(p.behindLive());
    if (pausedPanel()) look = "p" + std::to_string((ms - r->pausedAt) / 1000);
    if (timeline_) look = "t" + std::to_string(time::now());
    if (look != replayLook_) {
      replayLook_ = look;
      changed = true;
    }
    if (timeline_) {
      // Live moves on: the bar's right end with it, while it shows live.
      time::Seconds now = time::now();
      if (span_.to >= now - 2 && span_.to < now) span_.to = now;
      if (p.pastSerial() != pastSerial_) {
        pastSerial_ = p.pastSerial();
        programmes_ = p.timeline(earliest_);
        changed = true;
      }
    }
    if (!look.empty()) wake_.at(ms + 1000 - ms % 1000);
    // Build 169: the tag's few seconds. A replay that starts, or plays from
    // another moment (a jump: the timeline, a resume, Right's return, a
    // menu's Start over, a game's replay), starts them again; the next
    // stretch of the same replay doesn't. While it opens they wait for its
    // picture.
    std::string mark = tagMark_;
    if (!r || r->phase == ReplayPhase::Paused) mark.clear();
    else if (!r->next) mark = p.entry() + " " + std::to_string(r->from);
    if (mark != tagMark_) {
      tagMark_ = mark;
      if (!mark.empty()) behindTag_.jumped();
    }
    if (r && r->phase == ReplayPhase::Opening && !r->next) behindTag_.jumped();
    // Build 177: a sports replay's breaks; one beginning (or landed in)
    // brings the tag's few seconds again, saying so.
    if (breaksTick(ms)) changed = true;
    engine::breaks::Moment bm = breakMoment();
    time::Seconds inBreak = bm.in ? bm.in->from : 0;
    if (inBreak != breakTagFor_) {
      breakTagFor_ = inBreak;
      if (inBreak) behindTag_.jumped();
      changed = true;
    }
    if (skipped_ && !inBreakFrom(skipped_)) skipped_ = 0;
    bool covered = tagCovered();
    behindTag_.tick(ms, covered);
    float shown = behindTag_.opacity(ms, covered);
    if (shown != tagShown_) {
      tagShown_ = shown;
      changed = true;
    }
    if (uint32_t next = behindTag_.nextChange(ms)) wake_.at(next);
    return changed;
  }
  // Build 177: the game the replay shows (the board's id; "" none): the one
  // its replay was started from (Live Events), else the game the board has
  // on the channel at the replay's minute (live: now's), as the game
  // banner finds it. Asked again when the minute, the channel or the board
  // changes.
  const std::string& breaksGame() {
    Playback& p = app_.playback();
    if (const engine::Session::GameReplay* g = p.gameReplay()) {
      breakGame_ = g->eventId;
      breakGameKey_.clear();
      return breakGame_;
    }
    time::Seconds at = engine::replay::minuteOf(p.replay() ? p.replayAt() : time::now());
    uint64_t board = app_.store().events() ? app_.store().events()->version : 0;
    std::string key = p.entry() + " " + std::to_string(at) + " " + std::to_string(board);
    if (key != breakGameKey_) {
      breakGameKey_ = key;
      std::optional<GameBanner> b = p.entry().empty() ? std::nullopt : app_.store().gameBanner(p.entry(), at);
      breakGame_ = b ? b->id : std::string();
    }
    return breakGame_;
  }
  // Build 177, once a second (or `now`): while a sports game's replay
  // plays, its play-by-play is asked for (Store::wantBreaks); the breaks
  // read so far. True when they changed.
  bool breaksTick(uint32_t ms, bool now = false) {
    if (!now && breaksAsked_ && ms - breaksAskedMs_ < 1000) return false;
    breaksAsked_ = true;
    breaksAskedMs_ = ms;
    Playback& p = app_.playback();
    const std::string& game = breaksGame();
    if (!game.empty() && p.replay()) app_.store().wantBreaks(game);
    std::shared_ptr<const sports::plays::Breaks> b = game.empty() ? nullptr : app_.store().breaks(game);
    if (b == breaks_) return false;
    breaks_ = std::move(b);
    return true;
  }
  // Where the replay's moment is among the breaks (engine::breaks::moment):
  // nothing live, paused, or without breaks.
  engine::breaks::Moment breakMoment() const {
    const Playback& p = app_.playback();
    const auto& r = p.replay();
    if (!breaks_ || !r || r->phase == ReplayPhase::Paused) return {};
    time::Seconds now = time::now();
    return engine::breaks::moment(breaks_->list, p.replayAt(), now, p.reach(),
                                  engine::replay::archiveStart(now, p.catchupDays()), skipped_);
  }
  // The replay's moment is in the break that starts at `from`.
  bool inBreakFrom(time::Seconds from) const {
    if (!breaks_) return false;
    time::Seconds at = app_.playback().replayAt();
    for (const engine::breaks::Break& b : breaks_->list)
      if (b.from == from) return at >= b.from && (b.open || at < b.to);
    return false;
  }

  // What hides the tag: the banner (it says how far behind itself), the
  // controls, a channel key pending.
  bool tagCovered() const { return bannerVisible() || zone_ == Zone::Row || !pendingFlip_.empty(); }

  // ------------------------------------------- the Rewind timeline
  enum class From { Live, Replay, Paused };
  void openTimeline() {
    Playback& p = app_.playback();
    uint16_t days = p.catchupDays();
    if (!days || !pendingFlip_.empty()) return;
    const auto& r = p.replay();
    timelineFrom_ = !r ? From::Live : r->phase == ReplayPhase::Paused ? From::Paused : From::Replay;
    // Build 175: what's playing keeps playing while the timeline is open
    // (it used to hold the picture before a time was even picked); only a
    // time picked changes what plays.
    p.wantPast();
    time::Seconds now = time::now();
    earliest_ = engine::replay::archiveStart(now, days);
    head_ = timelineFrom_ == From::Live ? now : p.replayAt();
    // Build 172: where this channel's replay can start (any minute, or the
    // chunk boundaries of an archive it can't skip into).
    reach_ = p.reach();
    pastSerial_ = p.pastSerial();
    programmes_ = p.timeline(earliest_);
    // Build 177: a sports game's breaks: their ends are stops for Up / Down.
    breaksTick(app_.ms(), true);
    timelineBreaks_ = breaks_;
    breakStops_ = timelineBreaks_ ? engine::breaks::stops(timelineBreaks_->list, reach_, earliest_, now)
                                  : std::vector<time::Seconds>{};
    span_ = engine::replay::opening(programmes_, now, earliest_);
    span_ = engine::replay::follow(span_, head_, earliest_, now);
    timelineEntry_ = p.entry();
    timeline_ = true;
    headMoved_ = false;
    repeats_ = 0;
    lastMove_ = Key::Unknown;
    barHover_ = -1;
    playHover_ = false;
    zone_ = Zone::None;
    bannerUntil_ = app_.ms();
  }
  bool timelineKey(const KeyEvent& e) {
    Playback& p = app_.playback();
    time::Seconds now = time::now();
    // Held (repeat events, or one long swipe): faster.
    bool again = e.key == lastMove_ && (e.repeat || (e.glide && e.glide == glide_));
    if (e.glide) glide_ = e.glide;
    auto moved = [&](time::Seconds to) {
      headMoved_ = true;
      head_ = to;
      span_ = engine::replay::follow(span_, head_, earliest_, now);
    };
    switch (e.key) {
      case Key::Left:
      case Key::Right:
        repeats_ = again ? repeats_ + 1 : 0;
        lastMove_ = e.key;
        moved(engine::replay::stepReach(reach_, head_, e.key == Key::Left ? -1 : 1, repeats_, earliest_, now));
        return true;
      case Key::Up:
      case Key::Down:
        if (e.wheel && app_.ms() - lastOkAt_ < kWheelAfterClickMs) return true;
        lastMove_ = e.key;
        moved(engine::replay::jumpReach(reach_, programmes_, head_, e.key == Key::Up ? -1 : 1, earliest_, now,
                                        breakStops_));
        return true;
      case Key::Ok:
        // With the pointer on the bar, a click picks that time.
        if (pointer_ && barHover_ >= 0) {
          double at = (static_cast<double>(barHover_) + 0.5) / kBarSlices;
          moved(engine::replay::snap(reach_, engine::replay::at(span_, at, earliest_, now), earliest_, now));
          return true;
        }
        playFromHead();
        return true;
      case Key::Play:
      case Key::PlayPause:
        playFromHead();
        return true;
      case Key::Back:
        leaveTimeline();
        return true;
      case Key::ChannelUp:
      case Key::ChannelDown:
        // Another channel: it plays live (the replay ends with the tune).
        timeline_ = false;
        flipBy(e.key == Key::ChannelUp ? 1 : -1);
        return true;
      case Key::Exit:
        leaveTimeline();
        clearScreen();
        return true;
      default:
        (void)p;
        return true;
    }
  }
  // OK: from the playhead (live, at live).
  void playFromHead() {
    Playback& p = app_.playback();
    timeline_ = false;
    // Build 175: no time picked: what plays goes on as it is (a paused
    // replay plays on from where it paused, as before).
    if (!headMoved_ && timelineFrom_ != From::Paused) {
      showBanner();
      return;
    }
    if (time::now() - head_ < engine::Session::kLiveWithin) p.goLive(timelineFrom_ == From::Live);
    else p.replayFrom(head_, engine::Session::ReplayWhy::Rewind);
    showBanner();
  }
  // Back: as it was. Build 175: nothing was held, so nothing to undo.
  void leaveTimeline() {
    timeline_ = false;
    showBanner();
  }

  // A time on the bar, as x.
  float barX(time::Seconds t, float x0, float w) const {
    double f = static_cast<double>(t - span_.from) / static_cast<double>(std::max<time::Seconds>(1, span_.length()));
    return x0 + static_cast<float>(std::clamp(f, 0.0, 1.0)) * w;
  }
  // "8:21 PM", on a clock face: what the guide shows.
  std::string timeText(ui::Ctx& c, time::Seconds t) const { return ui::clock(c, t); }

  void drawTimeline(ui::Ctx& c, const GuideChannel* g) {
    Playback& p = app_.playback();
    time::Seconds now = time::now();
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::black(0.35f));
    const float top = 616;
    ui::floatingCard(c, {48, top, gfx::kLayoutWidth - 96, gfx::kLayoutHeight - 28 - top});
    const float x0 = kLeft + 8, w = gfx::kLayoutWidth - 2 * (kLeft + 8);
    // Title: Rewind, the channel; at the right, live's time and Play.
    float tx = kLeft;
    if (unsigned icon = c.icons ? c.icons->get("ui-replay") : 0) {
      c.g.textureAlpha(icon, {tx, top + 30, 36, 36}, theme::white());
      tx += 48;
    }
    tx += shadowed(c, "Rewind", tx, top + 28, Weight::Bold, 34, theme::white());
    std::string name = g ? g->name : std::string();
    if (!name.empty()) shadowed(c, "\xc2\xb7 " + name, tx + 14, top + 34, Weight::Medium, 26, theme::white(0.8f), 520);
    std::string play = "\xe2\x96\xb6  Play from " + timeText(c, head_);
    if (now - head_ < engine::Session::kLiveWithin) play = "\xe2\x97\x8f  Go live";
    float pw = ui::textWidth(c, play, Weight::Bold, 24) + 56;
    Rect pr{gfx::kLayoutWidth - kLeft - pw, top + 22, pw, 56};
    c.hit(pr, kHitPlayHere);
    c.g.fill(pr, theme::white(), 28);
    if (playHover_) c.g.stroke({pr.x - 4, pr.y - 4, pr.w + 8, pr.h + 8}, theme::black(0.85f), 32, 3);
    ui::labelIn(c, play, pr, Weight::Bold, 24, theme::black(), Align::Center);
    shadowed(c, "Live " + timeText(c, now), pr.x - 24 - ui::textWidth(c, "Live " + timeText(c, now), Weight::Medium, 22), top + 38,
             Weight::Medium, 22, theme::white(0.7f));

    // The programmes on the bar, by title.
    const float blocksY = top + 190, blocksH = 50, barY = top + 256;
    const engine::replay::Programme* under = engine::replay::on(programmes_, head_);
    for (const engine::replay::Programme& pr2 : programmes_) {
      if (pr2.end <= span_.from || pr2.start >= span_.to) continue;
      float a = barX(pr2.start, x0, w), b = barX(pr2.end, x0, w);
      if (b - a < 4) continue;
      Rect r{a + 1, blocksY, b - a - 2, blocksH};
      bool here = &pr2 == under;
      c.g.fill(r, here ? theme::white(0.22f) : theme::hex(0x14161B, 0.85f), 10);
      c.g.stroke(r, theme::white(here ? 0.5f : 0.2f), 10, 1);
      if (r.w > 70) ui::label(c, pr2.title, r.x + 12, r.y + 13, Weight::Medium, 20, theme::white(here ? 1.0f : 0.8f),
                              r.w - 24);
    }
    // The bar: what the archive can't reach is dim; live is red.
    c.g.fill({x0, barY, w, 10}, theme::white(0.28f), 5);
    if (earliest_ > span_.from) c.g.fill({x0, barY, barX(earliest_, x0, w) - x0, 10}, theme::black(0.6f), 5);
    float hx = barX(head_, x0, w);
    c.g.fill({x0, barY, hx - x0, 10}, theme::hex(kBlue), 5);
    // Build 177: a sports game's breaks, hatched on the bar; halftime named
    // under its block (the hour marks give way to it).
    std::vector<std::pair<float, float>> named;
    bool breaksShown = false;
    if (timelineBreaks_) {
      for (const engine::breaks::Block& k : engine::breaks::blocks(timelineBreaks_->list, span_.from, span_.to, now)) {
        float a = x0 + static_cast<float>(k.from) * w, b = x0 + static_cast<float>(k.to) * w;
        hatch(c, {a, barY - 4, std::max(4.0f, b - a), 18});
        breaksShown = true;
        if (!k.halftime) continue;
        float lw = ui::textWidth(c, "Halftime", Weight::Regular, 17);
        float lx = std::min(a, x0 + w - lw);
        shadowed(c, "Halftime", lx, barY + 24, Weight::Regular, 17, theme::white(0.65f));
        named.push_back({lx - 10, lx + lw + 10});
      }
    }
    if (span_.to >= now - 1) {
      float lx = barX(now, x0, w);
      c.g.fill({lx - 9, barY - 4, 18, 18}, theme::hex(kRed), 9);
      shadowed(c, "LIVE", lx - 60, barY + 22, Weight::Bold, 16, theme::hex(kRed), 0);
    }
    // Build 172: the starts this channel's replay has, as faint ticks (a
    // chunked archive it can't skip into).
    for (time::Seconds t : engine::replay::boundaries(reach_, span_)) {
      if (t >= now) break;
      float bx = barX(t, x0, w);
      c.g.fill({bx - 1, barY - 6, 2, 22}, theme::white(0.35f));
    }
    // Hour and half-hour marks under the bar.
    time::Seconds step = span_.length() > 6 * 3600 ? 3 * 3600 : span_.length() > 3 * 3600 ? 3600 : 1800;
    for (time::Seconds t = (span_.from / step + 1) * step; t < span_.to - step / 3; t += step) {
      float mx = barX(t, x0, w);
      c.g.fill({mx - 1, barY + 12, 2, 8}, theme::white(0.4f));
      std::string mark = timeText(c, t);
      float half = ui::textWidth(c, mark, Weight::Regular, 17) * 0.5f;
      bool taken = std::any_of(named.begin(), named.end(), [&](const std::pair<float, float>& n) {
        return mx + half > n.first && mx - half < n.second;
      });
      if (!taken) ui::label(c, mark, mx - 60, barY + 24, Weight::Regular, 17, theme::white(0.6f), 120, Align::Center);
    }
    // Pointer: the bar in slices, each a time.
    for (int i = 0; i < kBarSlices; ++i) {
      float sw = w / kBarSlices;
      c.hit({x0 + sw * static_cast<float>(i), blocksY, sw, barY + 30 - blocksY}, kHitBar + i);
    }
    // The playhead and its bubble: the time, how far back, the programme.
    c.g.fill({hx - 1.5f, top + 160, 3, barY + 10 - (top + 160)}, theme::white());
    c.g.fill({hx - 12, barY - 7, 24, 24}, theme::white(), 12);
    std::string when = timeText(c, head_) + " \xc2\xb7 " +
                       (now - head_ < engine::Session::kLiveWithin ? std::string("live")
                                                                   : engine::replay::behindMinutes(now - head_));
    std::string what = under ? under->title : std::string();
    float bw = std::max(ui::textWidth(c, when, Weight::Bold, 24), std::min(520.0f, ui::textWidth(c, what, Weight::Regular, 20))) + 40;
    float bx = std::clamp(hx - bw * 0.5f, 56.0f, gfx::kLayoutWidth - 56.0f - bw);
    Rect bubble{bx, top + 92, bw, what.empty() ? 52.0f : 78.0f};
    c.g.fill(bubble, theme::white(), 16);
    ui::labelIn(c, when, {bubble.x, bubble.y + 8, bubble.w, 34}, Weight::Bold, 24, theme::black(), Align::Center);
    if (!what.empty())
      ui::labelIn(c, what, {bubble.x + 20, bubble.y + 42, bubble.w - 40, 28}, Weight::Regular, 20, theme::black(0.7f),
                  Align::Center);
    // How it's used.
    std::string steps = "\xe2\x97\x80 \xe2\x96\xb6 1 minute (hold to speed up: 5, then 15)";
    if (!reach_.anyMinute())
      steps = "\xe2\x97\x80 \xe2\x96\xb6 " + std::to_string(reach_.chunk / 60) +
              " minutes, where this channel\xe2\x80\x99s replay starts (hold to speed up)";
    // Build 177: with breaks on the bar, their key first, and Up / Down
    // stop at them too.
    float hx0 = kLeft;
    if (breaksShown) {
      hatch(c, {hx0, top + 336, 33, 15});
      hx0 += 33 + 10;
      hx0 += shadowed(c, "Breaks", hx0, top + 330, Weight::Regular, 21, theme::white(0.78f)) + 24;
    }
    std::string upDown =
        breaksShown ? "end of a break or start of a programme, before / after" : "start of the programme before / after";
    shadowed(c,
             steps + " \xc2\xb7 \xe2\x96\xb2 \xe2\x96\xbc " + upDown + " \xc2\xb7 OK play from here "
                     "\xc2\xb7 Back stay where you were",
             hx0, top + 330, Weight::Regular, 21, theme::white(0.78f), gfx::kLayoutWidth - kLeft - hx0);
    (void)p;
  }

  // ------------------------------------------- the paused panel
  std::vector<std::string> pausedButtons(ui::Ctx& c) const {
    const auto& r = app_.playback().replay();
    time::Seconds at = r ? r->at() : time::now();
    return {"\xe2\x96\xb6  Play from " + ui::clock(c, at - at % 60, false), "Go live", "Rewind"};
  }
  bool pausedKey(const KeyEvent& e) {
    Playback& p = app_.playback();
    switch (e.key) {
      case Key::Left:
        pausedButton_ = std::max(0, pausedButton_ - 1);
        return true;
      case Key::Right:
        pausedButton_ = pausedButton_ < 0 ? 0 : std::min(2, pausedButton_ + 1);
        return true;
      case Key::Ok:
        if (pausedButton_ == 0) resumeReplay();
        else if (pausedButton_ == 1) {
          p.goLive();
          showBanner();
        } else if (pausedButton_ == 2) {
          openTimeline();
        }
        return true;
      case Key::Play:
      case Key::PlayPause:
        resumeReplay();
        return true;
      case Key::FastForward:
        openTimeline();
        return true;
      case Key::Up:
      case Key::Down:
        if (e.swipe || (e.wheel && app_.ms() - lastOkAt_ < kWheelAfterClickMs)) return true;
        flipBy(arrowStep(e.key));
        return true;
      case Key::ChannelUp:
      case Key::ChannelDown:
        flipBy(e.key == Key::ChannelUp ? 1 : -1);
        return true;
      case Key::Rewind:
        goPrevious();
        return true;
      case Key::Guide:
        openGuide();
        return true;
      case Key::Back:
        return backKey();
      default:
        return true;
    }
  }
  void drawPaused(ui::Ctx& c, const GuideChannel* g) {
    Playback& p = app_.playback();
    const engine::Session::Replay& r = *p.replay();
    time::Seconds now = time::now();
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::black(0.5f));
    // The pause glyph.
    float cx = gfx::kLayoutWidth * 0.5f, cy = 360;
    c.g.fill({cx - 46, cy - 60, 30, 120}, theme::white(0.92f), 8);
    c.g.fill({cx + 16, cy - 60, 30, 120}, theme::white(0.92f), 8);
    const float top = 700;
    ui::floatingCard(c, {48, top, gfx::kLayoutWidth - 96, gfx::kLayoutHeight - 28 - top});
    time::Seconds at = r.at();
    shadowed(c, "Paused at " + ui::clock(c, at), kLeft, top + 30, Weight::Bold, 40, theme::white());
    // For how long, the channel, the programme.
    uint32_t forS = (app_.ms() - r.pausedAt) / 1000;
    char held[16];
    if (forS >= 3600)
      std::snprintf(held, sizeof held, "%u:%02u:%02u", forS / 3600, forS / 60 % 60, forS % 60);
    else
      std::snprintf(held, sizeof held, "%u:%02u", forS / 60, forS % 60);
    std::string line = std::string("for ") + held;
    if (g) line += " \xc2\xb7 " + g->name;
    if (pausedTitleFor_ != at) {
      pausedTitleFor_ = at;
      std::vector<engine::replay::Programme> list = p.timeline(at - 6 * 3600);
      const engine::replay::Programme* on = engine::replay::on(list, at);
      pausedTitle_ = on ? on->title : std::string();
      pausedStart_ = on ? on->start : at - 1800;
    }
    if (!pausedTitle_.empty()) line += " \xc2\xb7 " + pausedTitle_;
    shadowed(c, line, kLeft, top + 88, Weight::Regular, 26, theme::white(0.85f), gfx::kLayoutWidth - 2 * kLeft);
    // The programme's bar: from its start to live (Build 169: live always
    // the right end, as on the controls' bar), where it paused.
    float bx = kLeft + 110, bw = gfx::kLayoutWidth - 2 * kLeft - 220, by = top + 148;
    time::Seconds a = std::min(pausedStart_, at), b = std::max(now, at + 60);
    auto x = [&](time::Seconds t) {
      double f = static_cast<double>(t - a) / static_cast<double>(std::max<time::Seconds>(1, b - a));
      return bx + static_cast<float>(std::clamp(f, 0.0, 1.0)) * bw;
    };
    shadowed(c, ui::clock(c, a), kLeft, by - 12, Weight::Medium, 20, theme::white(0.7f));
    ui::label(c, "LIVE", gfx::kLayoutWidth - kLeft - 100, by - 12, Weight::Bold, 20, theme::hex(0xFF8A80), 100,
              Align::Right);
    c.g.fill({bx, by, bw, 8}, theme::white(0.25f), 4);
    c.g.fill({bx, by, x(at) - bx, 8}, theme::hex(kBlue), 4);
    c.g.fill({bx + bw - 7, by - 3, 14, 14}, theme::hex(kRed), 7);
    c.g.fill({x(at) - 9, by - 5, 18, 18}, theme::white(), 9);
    // The buttons.
    std::vector<std::string> buttons = pausedButtons(c);
    float bxx = kLeft, byy = top + 190;
    for (size_t i = 0; i < buttons.size(); ++i) {
      float dotW = i == 1 ? 30.0f : 0.0f;  // Go live's red dot
      float w = ui::textWidth(c, buttons[i], Weight::Bold, 26) + 64 + dotW;
      Rect rr{bxx, byy, w, 70};
      c.hit(rr, kHitPaused + static_cast<int>(i));
      bool f = static_cast<int>(i) == pausedButton_;
      if (f) {
        c.g.fill(rr, theme::white(), 35);
      } else {
        c.g.fill(rr, theme::hex(0x14161B, 0.85f), 35);
        c.g.stroke(rr, theme::white(0.3f), 35, 1.5f);
      }
      if (dotW > 0) c.g.fill({rr.x + 30, rr.y + 26, 18, 18}, theme::hex(kRed), 9);
      ui::labelIn(c, buttons[i], {rr.x + dotW, rr.y, rr.w - dotW, rr.h}, Weight::Bold, 26,
                  f ? theme::black() : theme::white(), Align::Center);
      bxx += w + 16;
    }
  }

  // ------------------------------------------- a game's replay (Build 167)
  // The Final whose replay plays now (from Live Events), else null.
  const engine::Session::GameReplay* finalReplay() const {
    const Playback& p = app_.playback();
    const engine::Session::GameReplay* g = p.gameReplay();
    return g && g->finished && g->eventId == p.entry() && p.replay() ? g : nullptr;
  }
  bool endCard() const { return endCardUntil_ != 0 && finalReplay() && !timeline_ && !pausedPanel(); }
  // "End of the game": Left / Right between its two buttons, OK takes one,
  // Back keeps watching; anything else lets it go and does what it does.
  bool endCardKey(const KeyEvent& e) {
    switch (e.key) {
      case Key::Left:
      case Key::Right:
        endCardButton_ = e.key == Key::Left ? 0 : 1;
        return true;
      case Key::Ok:
        if (endCardButton_ == 0) {
          // Back to Live Events: this screen goes, the stream with it.
          endCardUntil_ = 0;
          app_.nav().pop();
          return true;
        }
        endCardUntil_ = 0;
        return true;
      case Key::Back:
        endCardUntil_ = 0;
        return true;
      default:
        endCardUntil_ = 0;
        return false;
    }
  }
  void drawEndCard(ui::Ctx& c, float y) {
    if (!endCard()) return;
    const engine::Session::GameReplay* g = finalReplay();
    const EventTile* t = app_.store().eventTile(g->eventId);
    std::string what = t ? matchup(*t) : std::string();
    if (!g->channelName.empty()) what += (what.empty() ? "" : " \xc2\xb7 ") + g->channelName;
    const float w = 560;
    Rect r{gfx::kLayoutWidth - 64 - w, y, w, what.empty() ? 150.0f : 186.0f};
    c.g.fill(r, theme::hex(0x161B24, 0.96f), 26);
    float x = r.x + 28, ty = r.y + 22;
    ui::label(c, "End of the game", x, ty, Weight::Bold, 28, theme::white());
    ty += 44;
    if (!what.empty()) {
      ui::label(c, what, x, ty, Weight::Regular, 21, theme::white(0.65f), w - 56);
      ty += 38;
    }
    const char* titles[2] = {"Back to Live Events", "Keep watching"};
    float bx = x;
    for (int i = 0; i < 2; ++i) {
      float bw = ui::textWidth(c, titles[i], Weight::Bold, 22) + 44;
      Rect b{bx, ty, bw, 56};
      c.hit(b, kHitEndCard + i);
      bool f = endCardButton_ == i;
      c.g.fill(b, f ? theme::white() : theme::white(0.12f), 28);
      ui::labelIn(c, titles[i], b, Weight::Bold, 22, f ? theme::black() : theme::white(), Align::Center);
      bx += bw + 14;
    }
  }

  // Behind live: the rewind icon and how far, at the top right. Build 169:
  // for a few seconds after a jump, then it fades (engine::replay::
  // BehindTag); never over the banner or the controls.
  void drawBehindTag(ui::Ctx& c) {
    Playback& p = app_.playback();
    const auto& r = p.replay();
    if (!r || r->phase == ReplayPhase::Paused || c.covered) return;
    float a = behindTag_.opacity(app_.ms(), tagCovered());
    constexpr float kTop = 52, kH = 52;
    // Build 177: during a sports replay's break, the same tag says so.
    engine::breaks::Moment bm = breakMoment();
    if (bm.kind != engine::breaks::Moment::Kind::None) {
      if (a > 0) drawBreakTag(c, a, engine::breaks::tagText(bm), kTop, kH);
      if (finalReplay()) drawEndCard(c, kTop + kH + 16);
      return;
    }
    if (const engine::Session::GameReplay* g = finalReplay()) {
      // Build 167: a Final's replay counts from the game's start ("REPLAY ·
      // 48:10 in"), not behind live.
      if (a > 0) {
        std::string in = engine::views::replayIn(g->start, p.replayAt());
        float chipW = ui::chipWidth(c, "REPLAY", 17) + 26;
        float tw = ui::textWidth(c, in, Weight::Bold, 24);
        float w = 14 + chipW + 14 + tw + 24;
        Rect box{gfx::kLayoutWidth - 64 - w, kTop, w, kH};
        c.g.fill(box, theme::black(0.6f * a), 26);
        Rect chip{box.x + 12, box.y + 11, chipW, 30};
        c.g.fill(chip, theme::hex(kBlue, a), 15);
        if (unsigned icon = c.icons ? c.icons->get("ui-replay") : 0)
          c.g.textureAlpha(icon, {chip.x + 10, chip.y + 7, 16, 16}, theme::white(a));
        ui::labelIn(c, "REPLAY", {chip.x + 30, chip.y, chip.w - 36, chip.h}, Weight::Bold, 17, theme::white(a));
        ui::label(c, in, chip.x + chip.w + 14, box.y + 11, Weight::Bold, 24, theme::white(a));
      }
      // "End of the game" keeps its place under it, shown or not.
      drawEndCard(c, kTop + kH + 16);
      return;
    }
    if (a <= 0) return;
    std::string text = engine::replay::behindText(p.behindLive()) + " behind live";
    float tw = ui::textWidth(c, text, Weight::Bold, 24);
    float w = tw + 32 + 40;
    Rect box{gfx::kLayoutWidth - 64 - w, kTop, w, kH};
    c.g.fill(box, theme::black(0.6f * a), 26);
    float ix = box.x + 18;
    if (unsigned icon = c.icons ? c.icons->get("ui-replay") : 0)
      c.g.textureAlpha(icon, {ix, box.y + 13, 26, 26}, theme::white(a));
    ui::label(c, text, ix + 36, box.y + 11, Weight::Bold, 24, theme::white(a));
  }

  // Build 177: the tag during a break: a white-ish BREAK chip with the
  // double chevron, then what it says ("about 2 min · OK to skip").
  void drawBreakTag(ui::Ctx& c, float a, const std::string& text, float top, float h) {
    float chipW = ui::chipWidth(c, "BREAK", 17) + 26;
    float tw = ui::textWidth(c, text, Weight::Bold, 24);
    float w = 14 + chipW + 14 + tw + 24;
    Rect box{gfx::kLayoutWidth - 64 - w, top, w, h};
    c.g.fill(box, theme::black(0.6f * a), 26);
    Rect chip{box.x + 12, box.y + 11, chipW, 30};
    c.g.fill(chip, theme::white(0.22f * a), 15);
    if (unsigned icon = c.icons ? c.icons->get("ctl-skip") : 0)
      c.g.textureAlpha(icon, {chip.x + 10, chip.y + 7, 16, 16}, theme::white(a));
    ui::labelIn(c, "BREAK", {chip.x + 30, chip.y, chip.w - 36, chip.h}, Weight::Bold, 17, theme::white(a));
    ui::label(c, text, chip.x + chip.w + 14, box.y + 11, Weight::Bold, 24, theme::white(a));
  }
  // Build 177: a break's block: dark, with light diagonal stripes (the
  // ui-hatch tile, repeated; a plain dark block without it).
  void hatch(ui::Ctx& c, Rect r) {
    c.g.fill(r, theme::hex(0x0E1117, 0.95f), 3);
    unsigned tile = c.icons ? c.icons->get("ui-hatch") : 0;
    if (!tile) {
      c.g.stroke(r, theme::white(0.35f), 3, 1);
      return;
    }
    c.g.pushClip(r);
    for (float x = r.x; x < r.x + r.w; x += r.h) c.g.textureAlpha(tile, {x, r.y, r.h, r.h}, theme::white(0.4f));
    c.g.popClip();
  }

  // The programme's bar above the controls (engine::controls::bar). Live:
  // the show on now from its start to its end, white to now with a dot,
  // and "AC360 · 20 min in · 40 min left" under it. Build 169, behind
  // live: from the start of the programme watched (the replay banner's
  // listings) to live, always the right end (a red dot, LIVE); blue to the
  // moment, a white dot with a blue halo and its clock under it, grey on to
  // live; a thin tick at every programme start between, only the first one
  // named, cut to its room or left out.
  void drawReplayBar(ui::Ctx& c, const GuideChannel* g) {
    namespace ctl = engine::controls;
    Playback& p = app_.playback();
    time::Seconds now = c.now;
    const Listing* live = g ? current(g->id, now, nullptr) : nullptr;
    ctl::Bar bar;
    if (p.replay() && g) {
      time::Seconds at = p.replayAt();
      replayBanner(*g, at, now);  // the banner's listings, worked out once a minute
      bar = ctl::bar(replayShown_.shows, live, at, now);
    } else if (p.replay()) {
      bar = ctl::bar({}, nullptr, p.replayAt(), now);
    } else {
      bar = ctl::bar({}, live, std::nullopt, now);
    }
    constexpr int kPx = 18;
    const float y = kRowY - 54, lw = 110;
    const float x0 = kLeft + lw, w = gfx::kLayoutWidth - 2 * kLeft - 2 * lw;
    auto xAt = [&](double f) { return x0 + static_cast<float>(f) * w; };
    shadowed(c, ui::clock(c, bar.from), kLeft, y - 11, Weight::Medium, kPx, theme::white(0.75f));
    if (!bar.replay) {
      ui::label(c, ui::clock(c, bar.to), gfx::kLayoutWidth - kLeft - lw, y - 11, Weight::Medium, kPx,
                theme::white(0.75f), lw, Align::Right);
      c.g.fill({x0, y, w, 4}, theme::white(0.28f), 2);
      c.g.fill({x0, y, xAt(bar.at) - x0, 4}, theme::white(0.7f), 2);
      c.g.fill({xAt(bar.at) - 7, y - 5, 14, 14}, theme::white(), 7);
      if (!bar.line.empty()) shadowed(c, bar.line, x0, y + 14, Weight::Regular, kPx, theme::white(0.6f), w);
      return;
    }
    // Live at the right end.
    ui::label(c, "LIVE", gfx::kLayoutWidth - kLeft - lw, y - 11, Weight::Bold, kPx, theme::hex(0xFF8A80), lw,
              Align::Right);
    c.g.fill({x0, y, w, 4}, theme::white(0.28f), 2);
    float ax = xAt(bar.at);
    c.g.fill({x0, y, ax - x0, 4}, theme::hex(kBlue), 2);
    // Build 177: a sports game's breaks, as on the Rewind timeline (only
    // shown here).
    if (breaks_)
      for (const engine::breaks::Block& k : engine::breaks::blocks(breaks_->list, bar.from, bar.to, now))
        hatch(c, {xAt(k.from), y - 3, std::max(3.0f, xAt(k.to) - xAt(k.from)), 10});
    for (size_t i = 0; i < bar.ticks.size(); ++i)
      c.g.fill({xAt(bar.ticks[i].at) - 1, y - 5, 2, 14}, theme::white(i == 0 ? 0.55f : 0.4f), 1);
    c.g.fill({x0 + w - 11, y - 9, 22, 22}, theme::hex(kRed, 0.25f), 11);
    c.g.fill({x0 + w - 7, y - 5, 14, 14}, theme::hex(kRed), 7);
    // The moment: a white dot with a blue halo, its clock under it.
    c.g.fill({ax - 12, y - 10, 24, 24}, theme::hex(kBlue, 0.5f), 12);
    c.g.fill({ax - 8, y - 6, 16, 16}, theme::white(), 8);
    // Build 177: in a break, that in place of the clock.
    std::string clock = breakMoment().kind != engine::breaks::Moment::Kind::None ? std::string("in a break")
                                                                                   : ui::clock(c, p.replayAt());
    float cw = ui::textWidth(c, clock, Weight::Bold, kPx);
    float cx = std::clamp(ax - cw * 0.5f, kLeft, x0 + w - cw);
    shadowed(c, clock, cx, y + 14, Weight::Bold, kPx, theme::white());
    // The next programme's name by its tick.
    ctl::TickName name = ctl::tickName(bar, w, cx + cw - x0,
                                       [&c](const std::string& s) { return ui::textWidth(c, s, Weight::Regular, kPx); });
    if (!name.text.empty())
      shadowed(c, name.text, x0 + name.x, y + 14, Weight::Regular, kPx, theme::white(0.6f));
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
  // A channel's name, or a game's matchup ("Rams at Seahawks", never a
  // score) or title.
  std::string nameOf(const std::string& id) const {
    auto ch = app_.store().channels();
    if (const GuideChannel* g = ch->find(id)) return g->name;
    if (const EventTile* t = app_.store().eventTile(id)) return matchup(*t);
    return app_.store().isEvent(id) ? std::string("the last game") : std::string("the previous channel");
  }
  // Where Right goes (Build 159), by name: a channel, a game (found again
  // after the board was rebuilt), or the channel of a game that has ended;
  // "" when nothing.
  std::string backName() const {
    Playback& p = app_.playback();
    const engine::Session::Viewed* v = p.previousTarget();
    if (!v) return {};
    if (v->game) {
      std::string id = p.gameNow(*v);
      if (!id.empty()) return nameOf(id);
      if (v->channel == p.entry()) return {};  // on it already: Right only says the game has ended
      const GuideChannel* g = app_.store().channels()->find(v->channel);
      return g ? g->name : std::string();
    }
    const GuideChannel* g = app_.store().channels()->find(v->entry);
    return g ? g->name : std::string();
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
    // Behind live, the game on at the moment playing (a channel showing a
    // game is asked at the replay's minute, not now's).
    Playback& p = app_.playback();
    time::Seconds at = pendingFlip_.empty() && p.replay() ? engine::replay::minuteOf(p.replayAt()) : 0;
    if (id != gameFor_ || board != gameBoard_ || at != gameReplayAt_ || c.now - gameAt_ >= kGameRecheck ||
        c.now < gameAt_) {
      gameFor_ = id;
      gameBoard_ = board;
      gameAt_ = c.now;
      gameReplayAt_ = at;
      game_ = id.empty() ? std::nullopt : app_.store().gameBanner(id, at ? at : c.now);
    }
    return game_ ? &*game_ : nullptr;
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

  // Captions to offer: the stream carries them, or this channel did
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
    int px = captions::letterPx(set.subtitleSize);
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

  // Build 167: the channel banner during a replay names the programme at
  // the moment shown (the guide's listings, the past it keeps for replay
  // channels with their episodes and descriptions, and the copy's own past
  // once Rewind asked for it), worked out when the minute shown, live's
  // minute, the listings or the copy's past change; never per frame.
  struct ReplayShown {
    std::string entry;
    time::Seconds at = -1, now = -1;
    uint64_t listings = 0;
    int past = -1;
    std::vector<Listing> shows;
    engine::views::ReplayBanner banner;
  };
  ReplayShown replayShown_;
  const engine::views::ReplayBanner& replayBanner(const GuideChannel& g, time::Seconds at, time::Seconds now) {
    Playback& p = app_.playback();
    ReplayShown& r = replayShown_;
    listings_ = app_.store().listings();
    uint64_t version = listings_ ? listings_->version : 0;
    if (r.entry == g.id && r.at == at / 60 && r.now == now / 60 && r.listings == version && r.past == p.pastSerial())
      return r.banner;
    r.entry = g.id;
    r.at = at / 60;
    r.now = now / 60;
    r.listings = version;
    r.past = p.pastSerial();
    time::Seconds from = at - 6 * 3600;
    std::vector<Listing> guide;
    if (const std::vector<Listing>* v = listings_ ? listings_->forChannel(g.id) : nullptr) guide = *v;
    r.shows = engine::views::mergedListings(std::move(guide), app_.store().pastListing(g.id, from));
    std::vector<Listing> own;
    for (const engine::replay::Programme& pr : p.timeline(from)) {
      Listing l;
      l.title = pr.title;
      l.start = pr.start;
      l.end = pr.end;
      own.push_back(std::move(l));
    }
    r.shows = engine::views::mergedListings(std::move(r.shows), own);
    r.banner = engine::views::replayBanner(r.shows, current(g.id, now, nullptr), at, now);
    return r.banner;
  }

  // Build 167: the banner's times during a replay: the programme at the
  // moment, its bar in replay blue where it plays and "21 min in · 39 min
  // left" (or "Watching from 7:21 PM" when none is known), its description
  // on one line, and live in one muted line under it.
  void drawReplayTimes(ui::Ctx& c, const engine::views::ReplayBanner& rb, float rx, float y, float x, float top,
                       float titleRight) {
    Playback& p = app_.playback();
    if (const Listing* l = rb.show) {
      rx += shadowed(c, ui::range(c, l->start, l->end, true), rx, y, Weight::Medium, 22, theme::white(0.92f));
      if (!l->episode.empty())
        rx += 10 + shadowed(c, "\xc2\xb7 " + l->episode, rx + 10, y, Weight::Regular, 22, theme::white(0.65f), 360);
      rx += 18;
      ui::progress(c, {rx, y + 11, 420, 6}, rb.progress, theme::white(0.28f), theme::hex(kBlue));
      rx += 420 + 22;
      shadowed(c, rb.inLeft, rx, y, Weight::Regular, 22, theme::white(0.82f), titleRight - rx);
    } else {
      shadowed(c, "Watching from " + ui::clock(c, p.replayAt()), rx, y, Weight::Medium, 22, theme::white(0.92f),
               titleRight - rx);
    }
    float dw = std::min(1200.0f, titleRight - x);
    float ly = top + 144;
    if (rb.show && !rb.show->overview.empty()) {
      std::string about = rb.show->overview.string();
      ui::paragraph(c, about, x, ly + 2, dw, Weight::Regular, 20, theme::black(0.55f * 0.7f), 1, 1.4f);
      ui::paragraph(c, about, x, ly, dw, Weight::Regular, 20, theme::white(0.7f), 1, 1.4f);
      ly += 34;
    }
    c.g.fill({x, ly + 8, 9, 9}, theme::hex(kRed), 4.5f);
    shadowed(c, rb.liveLine, x + 18, ly, Weight::Regular, 20, theme::white(0.55f), dw - 18);
  }

  void drawBanner(ui::Ctx& c, const GuideChannel* g, bool failed) {
    Playback& p = app_.playback();
    bool pending = !pendingFlip_.empty();
    float x = kLeft, w = gfx::kLayoutWidth - 2 * kLeft;
    float top = kBannerBottom - 150;
    // Logo, number and name, LIVE and the show.
    Rect logo{x, top + 8, 150, 64};
    if (g && !g->logoUrl.empty()) {
      // Straight on the banner's card, like every logo: no plate.
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
    // Build 167: behind live on a channel, what the replay shows (the
    // programme at that moment) in place of the show on now.
    bool behind = !pending && p.replay().has_value();
    const engine::views::ReplayBanner* rb =
        behind && g && !failed && !app_.store().eventTile(p.entry()) ? &replayBanner(*g, p.replayAt(), c.now) : nullptr;
    // Later on this channel, on the right (behind live: next in the
    // replay); the title stops short of it.
    float stripX = x + w - kStripW;
    bool strip = rb ? drawLaterList(c, "NEXT IN THE REPLAY", rb->next, false, stripX, top - 36)
                    : g && drawLater(c, g->id, stripX, top - 36);
    float titleRight = strip ? stripX - 36 : x + w;
    float whereW = shadowed(c, where, tx, top + 2, Weight::Bold, 22, theme::white(0.9f), w - (tx - x));
    // Which version plays, for channels with both.
    if (g && !g->partner.empty()) ui::coastChip(c, g->west, tx + whereW + 14, top + 2, 14);
    float y = top + 104;
    if (failed && p.noPicture()) {
      // Sound with no picture anywhere: said in the banner, the sound plays on.
      std::string name = g ? g->name : std::string("This channel");
      shadowed(c, name + " has no picture right now", tx, top + 30, Weight::Bold, 44, theme::white(), titleRight - tx);
      shadowed(c, p.noPictureMessage(), x, y, Weight::Regular, 22, theme::white(0.82f), titleRight - x);
      return;
    }
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
    // Build 165: behind live, REPLAY in place of LIVE.
    float lx = tx + (behind ? ui::chip(c, "REPLAY", tx, top + 42, 16, theme::hex(kBlue), theme::white())
                            : ui::chip(c, "LIVE", tx, top + 42, 16, theme::hex(kRed), theme::white())) +
               12;
    // CC: this stream carries captions.
    if (!pending && p.subtitles().present()) {
      float ccW = ui::chipWidth(c, "CC", 15);
      c.g.stroke({lx, top + 42, ccW, 24}, theme::white(0.75f), 6, 2);
      ui::labelIn(c, "CC", {lx, top + 42, ccW, 24}, Weight::Bold, 15, theme::white(), Align::Center);
      lx += ccW + 12;
    }
    std::string title = l ? l->title : (g ? g->name : std::string());
    const Listing* show = l;  // the listing the title is (its episode and description)
    if (rb) {
      // No listing known for the moment: the channel's name.
      show = rb->show;
      title = show ? show->title : g->name;
    }
    if (const EventTile* t = pending ? nullptr : app_.store().eventTile(p.entry())) {
      title = t->heroTitle.empty() ? t->title : t->heroTitle;
      show = nullptr;
    }
    shadowed(c, title, lx, top + 30, Weight::Bold, 44, theme::white(), titleRight - lx);
    // Times, the guide's episode, progress and time left (the feed is under
    // Sources, not here). The episode stops at 360 px, so the progress and
    // time left keep their place.
    float rx = x;
    // Build 165, with the controls on a channel with replay: when the
    // programme started, or where the replay is and where live is (the
    // programme's bar is over the buttons, in the description's place).
    bool replayInfo = !pending && zone_ == Zone::Row && p.catchupDays() > 0;
    if (replayInfo && behind) {
      shadowed(c, "Watching from " + ui::clock(c, p.replayAt()) + " \xc2\xb7 live is " + ui::clock(c, c.now), rx, y,
               Weight::Medium, 22, theme::white(0.92f), titleRight - rx);
      return;
    }
    if (rb) {
      drawReplayTimes(c, *rb, rx, y, x, top, titleRight);
      return;
    }
    if (replayInfo && l) {
      long ago = static_cast<long>((c.now - l->start) / 60);
      std::string started = ago >= 60 ? "started " + std::to_string(ago / 60) + " h " + std::to_string(ago % 60) + " min ago"
                                      : "started " + std::to_string(std::max(0L, ago)) + " min ago";
      shadowed(c, ui::range(c, l->start, l->end, true) + " \xc2\xb7 " + started, rx, y, Weight::Medium, 22,
               theme::white(0.92f), titleRight - rx);
      return;
    }
    if (l) {
      rx += shadowed(c, ui::range(c, l->start, l->end, true), rx, y, Weight::Medium, 22, theme::white(0.92f));
      if (show && !show->episode.empty())
        rx += 10 + shadowed(c, "\xc2\xb7 " + show->episode, rx + 10, y, Weight::Regular, 22, theme::white(0.65f), 360);
      rx += 18;
      double frac = static_cast<double>(c.now - l->start) / static_cast<double>(std::max<time::Seconds>(1, l->end - l->start));
      ui::progress(c, {rx, y + 11, 420, 6}, frac, theme::white(0.28f), theme::white());
      rx += 420 + 22;
      long left = static_cast<long>((l->end - c.now + 59) / 60);
      if (left > 0) shadowed(c, std::to_string(left) + " min left", rx, y, Weight::Regular, 22, theme::white(0.82f));
    } else if (next && !strip) {
      shadowed(c, "Next: " + ui::clock(c, next->start) + " " + next->title, rx, y, Weight::Regular, 22,
               theme::white(0.82f), titleRight - rx);
    }
    // Build 119: the guide's description in the empty band under the times
    // (the card's height and everything on it as before), as wide as the
    // title, two lines at most; the banner's 5 s as without one.
    if (show && !show->overview.empty()) {
      float dw = std::min(1200.0f, titleRight - x);
      std::string about = show->overview.string();
      ui::paragraph(c, about, x, top + 146, dw, Weight::Regular, 20, theme::black(0.55f * 0.7f), 2, 1.4f);
      ui::paragraph(c, about, x, top + 144, dw, Weight::Regular, 20, theme::white(0.7f), 2, 1.4f);
    }
  }

  // A game's banner ("ID strip"): its logos (flags for players, the sport's
  // icon for a title), who's playing, your team's star and where it stands
  // by the clock; under it the league, round and venue; at the right the
  // channel and, in small type, its times and what's next. Never a score,
  // clock or result: the broadcaster's bug in the picture has those.
  //
  // Build 167: behind live on the game (its replay), REPLAY and how far in
  // by the title in place of the clock's status, no times or what's next,
  // and at the right the channel replaying with one muted "Live now" line
  // (views::gameReplayBanner). Caught up: as before.
  void drawGameBanner(ui::Ctx& c, const GuideChannel* g, const GameBanner& b) {
    float x = kLeft, right = gfx::kLayoutWidth - kLeft;
    float top = kGameCardTop + 22;  // the title's line
    sports::banner::Status st = sports::banner::status(b.start, b.end, b.postponed, c.now);
    Playback& p = app_.playback();
    engine::views::GameReplayBanner rb;
    // Behind live on the game shown, whether it was tuned as a game or as
    // the channel showing it (gameShown asks at the replay's minute).
    if (p.replay()) {
      // The channel whose archive plays (not the live source's).
      size_t copy = p.replay()->feed;
      if (copy < p.feeds().size())
        if (const GuideChannel* r = app_.store().channels()->find(app_.store().entryOfChannel(p.feeds()[copy].channelId)))
          g = r;
      const EventTile* t = app_.store().eventTile(b.id);
      bool gameLive = t && !t->finished && !t->upcoming;
      const Listing* on = g && !gameLive ? current(g->id, c.now, nullptr) : nullptr;
      rb = engine::views::gameReplayBanner(true, b.start, p.replayAt(), gameLive, on ? on->title : std::string());
      st = {};
    }

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
    if (rb.replay) {
      // Nothing of how it ends: no times, nothing after it.
    } else if (st.chip == Chip::soon) {
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
    if (rb.replay) sub = rb.liveNow;
    float subW = std::min(724.0f, ui::textWidth(c, sub, Weight::Regular, 19) + (rb.replay && !sub.empty() ? 20 : 0));
    float rightW = std::min(724.0f, std::max(rowW, subW));
    float rx = right - rowW;
    if (logo) {
      Rect r{rx, top, logoW, logoH};
      ui::logo(c, logoUrl, "", {r.x + 8, r.y + 5, r.w - 16, r.h - 10});
      rx += logoW + gap;
    }
    float nameY = top + (logoH - c.t.lineHeight(Weight::Bold, 22)) * 0.5f;
    shadowed(c, name, rx, nameY, Weight::Bold, 22, theme::white(0.9f), nameW + 1);
    if (coast) ui::coastChip(c, g->west, rx + nameW + gap, top + (logoH - 21) * 0.5f, 14);
    if (rb.replay && !sub.empty()) {
      // "● Live now: …", muted.
      float lx = right - subW;
      c.g.fill({lx, top + 58, 10, 10}, theme::hex(kRed, 0.8f), 5);
      shadowed(c, sub, lx + 20, top + 50, Weight::Regular, 19, theme::white(0.55f), subW - 19);
    } else if (!sub.empty()) {
      shadowed(c, sub, right - subW, top + 50, Weight::Regular, 19, theme::white(0.72f), subW + 1);
    }

    // At the left: the marks, 60 tall; the title after them.
    float my = top + 2;
    float tx = kGameText;
    if (b.sides.size() == 2) {
      const EventSide& a = b.sides[0];
      const EventSide& h = b.sides[1];
      auto person = [](const EventSide& s) { return !s.flags.empty() || !s.country.empty(); };
      auto hasLogo = [&](const EventSide& s) { return c.logos && !s.logoUrl.empty() && c.logos->get(s.logoUrl).id; };
      if (person(a) || person(h)) {
        float w = events_ui::mark(c, a, x, my, kGameMark);
        float wh = events_ui::mark(c, h, x + w + kGameMarkGap, my, kGameMark);
        tx = sports::banner::titleX(x, w, wh, kGameMarkGap, kGameTitleGap, kGameText);
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
    float room = right - rightW - 40 - tx;
    float starW = b.favorite ? ui::textWidth(c, "\xe2\x98\x85", Weight::Bold, 30) + 12 : 0;
    float chipW = st.chip == Chip::none ? 0 : ui::textWidth(c, st.text, Weight::Bold, 16) + 16 + 16;
    if (rb.replay)
      chipW = ui::textWidth(c, "REPLAY", Weight::Bold, 16) + 16 + 16 +
              (rb.in.empty() ? 0 : ui::textWidth(c, rb.in, Weight::Medium, 22) + 12);
    float lineH = c.t.lineHeight(Weight::Bold, 36);
    float titleW = shadowed(c, b.title, tx, top, Weight::Bold, 36, theme::white(), std::max(80.0f, room - starW - chipW));
    float cx = tx + titleW;
    if (b.favorite) {
      ui::label(c, "\xe2\x98\x85", cx + 12, top + (lineH - c.t.lineHeight(Weight::Bold, 30)) * 0.5f, Weight::Bold, 30,
                theme::hex(theme::kStar));
      cx += starW;
    }
    if (rb.replay) {
      float py = top + std::round((lineH - 26) * 0.5f);
      float pw = ui::textWidth(c, "REPLAY", Weight::Bold, 16) + 16;
      Rect pill{cx + 16, py, pw, 26};
      c.g.fill(pill, theme::hex(kBlue), 5);
      ui::labelIn(c, "REPLAY", pill, Weight::Bold, 16, theme::white(), Align::Center);
      if (!rb.in.empty())
        shadowed(c, rb.in, pill.x + pw + 12, top + (lineH - c.t.lineHeight(Weight::Medium, 22)) * 0.5f, Weight::Medium,
                 22, theme::white(0.85f));
    } else {
      events_ui::clockPill(c, st.chip, st.text, cx + 16, top + std::round((lineH - 26) * 0.5f));
    }
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
    return drawLaterList(c, "LATER ON THIS CHANNEL", later, true, x, y);
  }
  // The strip's programmes under `heading`; `live`: the first says how soon
  // it starts (Build 167: not for the replay's next ones).
  bool drawLaterList(ui::Ctx& c, const char* heading, const std::vector<const Listing*>& later, bool live, float x,
                     float y) {
    if (later.empty()) return false;
    shadowed(c, heading, x, y, Weight::Bold, 15, theme::white(0.6f));
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
      if (first && live) {
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
    // Build 165: a replay opening.
    if (p.replay() && !p.ready())
      return {"Opening the replay\xe2\x80\xa6", true, false};
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
    if (std::string back = backName(); !back.empty()) {
      keyCap(c, x, y, "\xe2\x96\xb6");
      item(40, "Back to", back);
    }
    float okW = keyCap(c, x, y, keys.clickpad ? "Click" : "OK");
    item(okW, "Subtitles & more", "");
    if (keys.colour) item(dot(c, x, y, kGreen), "CC", "");
    if (zone_ != Zone::None || x > gfx::kLayoutWidth - kLeft - 300) return;
    float right = gfx::kLayoutWidth - kLeft;
    // The load states, quietly, where "Hides in" sits once it plays.
    Status st = status(g);
    if (st.text.empty()) {
      int left = hidesIn(app_.ms());
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

  // Build 169: what the controls' buttons say when focused
  // (engine::controls::label).
  engine::controls::LabelIn labelIn(const GuideChannel* g) const {
    Playback& p = app_.playback();
    engine::controls::LabelIn in;
    if (const engine::Session::GameReplay* f = finalReplay()) in.finalChannel = f->channelName;
    Playback::SubtitleStatus st = p.subtitleStatus();
    in.subtitlesOn = st.on;
    in.subtitles = st.label;
    if (!p.feeds().empty()) {
      const Feed& f = p.feeds()[std::min(p.feedIndex(), p.feeds().size() - 1)];
      in.source = app_.store().providerName(f.channelId);
      in.quality = f.quality;
    }
    in.west = g && g->west;
    in.backTo = backName();
    in.game = app_.store().isEvent(p.entry());
    in.favorite = g && app_.store().isFavorite(g->id);
    return in;
  }
  // A button's icon in `r` (app/icons ctl-*, Rewind's ui-replay); Live is
  // its red dot. Without the icon file, the label's first letter.
  void drawGlyph(ui::Ctx& c, Ctl b, Rect r, Color ink, bool favorite, const std::string& title) {
    if (b == Ctl::Live) {
      float d = std::round(r.w * 0.6f);
      c.g.fill({r.x + (r.w - d) * 0.5f, r.y + (r.h - d) * 0.5f, d, d}, theme::hex(kRed), d * 0.5f);
      return;
    }
    const char* name = "";
    switch (b) {
      case Ctl::Pause: name = "ctl-pause"; break;
      case Ctl::Rewind: name = "ui-replay"; break;
      case Ctl::StartOver: name = "ctl-start"; break;
      case Ctl::SkipBreak: name = "ctl-skip"; break;
      case Ctl::Subtitles: name = "ctl-subtitles"; break;
      case Ctl::Sources: name = "ctl-sources"; break;
      case Ctl::Swap: name = "ctl-swap"; break;
      case Ctl::Cameras: name = "ctl-camera"; break;
      case Ctl::Back: name = "ctl-back"; break;
      case Ctl::Guide: name = "ctl-guide"; break;
      case Ctl::Favorite: name = favorite ? "ctl-star-on" : "ctl-star"; break;
      case Ctl::Settings: name = "ctl-settings"; break;
      case Ctl::Live: break;
    }
    if (unsigned icon = c.icons ? c.icons->get(name) : 0) {
      c.g.textureAlpha(icon, r, ink);
      return;
    }
    ui::labelIn(c, title.substr(0, 1), r, Weight::Bold, 24, ink, Align::Center);
  }

  // Build 169: the controls: round icon buttons in their three groups
  // (engine::controls), a thin divider between groups; the focused one (or
  // the one under the pointer) opens into a pill with its label and state,
  // the rest stay round, so the row always fits. A small dot on Subtitles'
  // icon while they're on. The programme's bar over them on a channel with
  // replay (not under a game's card).
  void drawPills(ui::Ctx& c, const GuideChannel* g) {
    buildPills();
    Playback& p = app_.playback();
    if (p.catchupDays() > 0 && cardTop_ == kCardTop) drawReplayBar(c, g);
    namespace ctl = engine::controls;
    constexpr float kIcon = 34, kPadLeft = 23, kIconGap = 14, kPadRight = 30, kStateGap = 12;
    constexpr int kTitlePx = 28, kStatePx = 24;
    const float y = kRowY - 6, room = gfx::kLayoutWidth - 2 * kLeft;
    ctl::LabelIn in = labelIn(g);
    if (startOverAt_) in.startOverFrom = ui::clock(c, *startOverAt_, false);
    // Build 177: "Skip break · about 2 min".
    if (engine::breaks::Moment bm = breakMoment(); bm.kind == engine::breaks::Moment::Kind::Skip)
      in.skipAbout = engine::breaks::about(bm.saves);
    std::vector<ctl::Label> labels;
    for (Ctl b : pills_) labels.push_back(ctl::label(b, in));
    bool focused = pill_ >= 0 && static_cast<size_t>(pill_) < pills_.size();
    float want = ctl::kSize;
    if (focused) {
      const ctl::Label& l = labels[static_cast<size_t>(pill_)];
      want = kPadLeft + kIcon + kIconGap + ui::textWidth(c, l.title, Weight::Bold, kTitlePx) + kPadRight;
      if (!l.state.empty()) want += kStateGap + ui::textWidth(c, l.state, Weight::Medium, kStatePx);
    }
    ctl::RowLayout row = ctl::layout(pills_, focused ? pill_ : -1, want, room);
    for (float d : row.dividers) c.g.fill({kLeft + d - 1, y + 22, 2, ctl::kSize - 44}, theme::white(0.25f), 1);
    for (size_t i = 0; i < pills_.size(); ++i) {
      const ctl::Slot& slot = row.slots[i];
      const ctl::Label& l = labels[i];
      Rect r{kLeft + slot.x, y, slot.w, ctl::kSize};
      c.hit(r, kHitPill + static_cast<int>(i));
      bool f = static_cast<int>(i) == pill_;
      float round = ctl::kSize * 0.5f;
      if (f) {
        c.g.fill(r, theme::white(), round);
        c.g.stroke({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, theme::black(0.85f), round + 4, 3);
      } else {
        c.g.fill(r, theme::hex(0x14161B, 0.85f), round);
        c.g.stroke(r, theme::white(0.3f), round, 1.5f);
      }
      Color ink = f ? theme::black() : theme::white();
      float iy = r.y + (ctl::kSize - kIcon) * 0.5f;
      Rect icon = f ? Rect{r.x + kPadLeft, iy, kIcon, kIcon} : Rect{r.x + (ctl::kSize - kIcon) * 0.5f, iy, kIcon, kIcon};
      drawGlyph(c, pills_[i], icon, ink, in.favorite, l.title);
      // Subtitles on: a small green dot at the icon's top right corner.
      if (l.dot) {
        Rect dot{icon.x + icon.w - 7, icon.y - 5, 14, 14};
        c.g.fill({dot.x - 2, dot.y - 2, dot.w + 4, dot.h + 4}, f ? theme::white() : theme::hex(0x14161B), 9);
        c.g.fill(dot, theme::hex(kGreen), 7);
      }
      if (!f) continue;
      float tx = icon.x + kIcon + kIconGap, right = r.x + r.w - kPadRight;
      tx += ui::label(c, l.title, tx, r.y + 22, Weight::Bold, kTitlePx, ink, right - tx);
      if (!l.state.empty() && right - tx - kStateGap > 24)
        ui::label(c, l.state, tx + kStateGap, r.y + 25, Weight::Medium, kStatePx, theme::black(0.6f),
                  right - tx - kStateGap);
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
    if (std::string back = backName(); !back.empty())
      edge({gfx::kLayoutWidth - 128, 460, 96, 160}, kHitLast, zone_ == Zone::EdgeLast, "\xe2\x96\xb6", back);
  }

  // 1C: once, the first time a channel plays (Settings › Help & info shows
  // it again).
  void drawTips(ui::Ctx& c) { drawGettingAround(c, app_.remoteKeys(), false, kHitTips); }

  uint32_t bannerFrom_ = 0, bannerUntil_ = 0, rowTouched_ = 0;
  // The banner's "Hides in" second (and its message) when last ticked (0:
  // hidden); when tick() next has something to show (wakeAt()), and the
  // spinners' step.
  int bannerSecond_ = 0;
  wake::Due wake_;
  wake::Steps spinner_;
  std::string pendingFlip_;  // channel keys: the channel the banner shows, tuned once they stop
  uint32_t flipAt_ = 0;
  bool wasReady_ = false, pictureSeen_ = false;  // this tune's picture: now, and ever
  uint32_t pictureAt_ = 0;                        // when this tune's first picture came
  bool noSubtitlesSaid_ = false;
  size_t pictureFeed_ = 0;  // the feed the picture last came from
  Zone zone_ = Zone::None;
  std::vector<Ctl> pills_;
  int pill_ = 0;
  int failButton_ = 0;
  // Build 165, replay. The paused panel's focused button (-1 none: the
  // pointer on the picture); the programme it paused in. The Rewind
  // timeline: open, what played before it, the channel it's for, the
  // playhead, the archive's start, the bar, the programmes on it, the key
  // last moved with and its repeats, the pointer on the bar or Play. The
  // clocks shown. Build 170: where the row's Start over goes (none: no
  // button) and what it was worked out for (channel, live or behind).
  int pausedButton_ = 0;
  time::Seconds pausedTitleFor_ = 0, pausedStart_ = 0;
  std::string pausedTitle_;
  bool timeline_ = false;
  bool headMoved_ = false;  // Build 175: a time picked on the timeline
  From timelineFrom_ = From::Live;
  std::string timelineEntry_;
  time::Seconds head_ = 0, earliest_ = 0;
  engine::replay::Reach reach_;
  engine::replay::Span span_;
  std::vector<engine::replay::Programme> programmes_;
  int pastSerial_ = 0;
  Key lastMove_ = Key::Unknown;
  int repeats_ = 0;
  int barHover_ = -1;
  bool playHover_ = false;
  std::string replayLook_;
  std::optional<time::Seconds> startOverAt_;
  std::string startOverFor_;
  // Build 169: the behind-live tag's few seconds after a jump; the replay
  // they're for (its channel and stretch start, "" none); how much shows.
  engine::replay::BehindTag behindTag_;
  std::string tagMark_;
  float tagShown_ = 0;
  // Build 177, Skip break: the game the replay shows and what it was
  // worked out for; its breaks (the timeline's as it opened) and the
  // timeline's stops at their ends; when they were last asked for; the
  // break the tag last came up for and the one just skipped (their starts,
  // 0 none); "OK again to go back": where to, until when (the notice's),
  // and when the skip was.
  std::string breakGame_, breakGameKey_;
  std::shared_ptr<const sports::plays::Breaks> breaks_, timelineBreaks_;
  std::vector<time::Seconds> breakStops_;
  bool breaksAsked_ = false;
  uint32_t breaksAskedMs_ = 0;
  time::Seconds breakTagFor_ = 0, skipped_ = 0, undoTo_ = 0;
  uint32_t undoUntil_ = 0, skippedMs_ = 0;
  // Build 167: a Final's replay past its final: "End of the game" until
  // then (app ms; 0 none), its focused button (0 Back to Live Events).
  uint32_t endCardUntil_ = 0;
  int endCardButton_ = 0;
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
  time::Seconds gameReplayAt_ = 0;  // the replay's minute game_ was asked at (0: live)
  uint64_t gameBoard_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makePlayer(App& app) { return std::make_unique<PlayerScreen>(app); }

}  // namespace marigold::app
