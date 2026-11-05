#include "app/app.hpp"

#include <algorithm>
#include <chrono>

#include "app/crash_note.hpp"
#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/report/playback_log.hpp"
#include "core/util/log.hpp"

namespace marigold::app {
namespace {

constexpr uint32_t kToastMs = 3500;
constexpr uint32_t kReminderMs = 15000;  // the reminder card hides itself then
// OK held this long counts as a long press (Legacy's NV_HOLD_MS, as Android TV).
constexpr uint32_t kHoldMs = 700;
constexpr int kPoolThreads = 4;

}  // namespace

App::App(platform::Window& window, platform::Storage& storage, gfx::Renderer& g,
         text::TextRenderer& t, icons::IconSet& icons, platform::Video& video, Build build)
    : window_(window), storage_(storage), g_(g), t_(t), icons_(icons), video_(video), build_(std::move(build)),
      keys_(window.remoteKeys()) {
  // Wakes the loop when a worker has a result, so pump() runs promptly.
  pool_.start(kPoolThreads, [&window] { window.wake(); });
  std::string logoDir = storage_.dir().empty() ? std::string("/tmp/marigold-logos")
                                               : storage_.path("logos");
  logos_ = std::make_unique<logos::LogoCache>(pool_, logoDir);
  // What the shared engine needs from this app (see core/engine/host.hpp).
  engine::Host host{pool_, storage_};
  host.ms = [this] { return ms_; };
  host.zone = [this]() -> const time::Zone& { return zone_; };
  host.playingFull = [this] { return playback_ && playback_->active() && playback_->full(); };
  host.memoryNote = [this](const char* what) {
    if (playback_) playback_->logMemory(what);
  };
  host.idle = [this] { return ms_ - lastKeyMs_ >= 2000 && !playbackBusy_; };
  host.logoStatus = [this](const std::string& url) { return logos_->status(url); };
  host.requestsCancelled = [this] { logos_->abandonLoads(); };
  host.build = build_.build;
  store_ = std::make_unique<Store>(std::move(host));
  playback_ = std::make_unique<Playback>(*this);
  // The run's first line in the playback log: which build on what.
  playback_->logLaunch(report::launchDetail(build_.build, build_.version, build_.system, build_.device));
  // The previous run's crash, in the playback log Help exports (no links:
  // a signal, a build, addresses and symbols).
  if (!crash::previousSummary().empty()) playback_->logNote("previous run crashed", crash::previousSummary());
}

App::~App() {
  nav_ = Navigator();  // screens first: they may still use playback and the store
  playback_.reset();
  pool_.stop();
  store_.reset();
  logos_.reset();
}

void App::toast(const std::string& message, bool top) {
  toast_ = message;
  toastTop_ = top;
  toastUntil_ = window_.ticks() + kToastMs;
  dirty_ = true;
}

bool App::openKeyboard(const std::string& current) {
  if (!window_.keyboardAvailable()) return false;
  window_.openKeyboard(current);
  return true;
}

void App::closeKeyboard() { window_.closeKeyboard(); }
bool App::keyboardOpen() const { return window_.keyboardOpen(); }

void App::suspend() {
  // Leaving the app: the decoder goes back to the TV, and the playback log
  // writes what it holds.
  video_.stop();
  playback_->saveLogNow();
  dirty_ = true;
}

void App::dispatch(const KeyEvent& k) {
  Screen* top = nav_.top();
  dirty_ = true;
  lastKeyMs_ = ms_;
  // A reminder card takes OK (watch) and Back (dismiss) while it shows.
  if (const auto& due = store_->dueReminder()) {
    if (k.key == Key::Ok) {
      std::string id = due->channelId;
      store_->dismissReminder();
      // Already watching full screen: the player tunes in place (no second
      // player on top of it).
      bool watching = playback_->active() && playback_->full();
      if (playback_->tune(id)) {
        playback_->setFull();
        if (!watching) nav_.push(makePlayer(*this));
      }
      return;
    }
    if (k.key == Key::Back) {
      store_->dismissReminder();
      return;
    }
  }
  if (!top) return;
  bool used = top->key(k);
  if (!used && k.key == Key::Back) {
    if (nav_.size() > 1) nav_.pop();
    else window_.leave();
  }
  // Exit quits, unless the screen used it (the player clears its overlays).
  if (k.key == Key::Exit && !used) quit_ = true;
}

void App::handle(const platform::Event& e) {
  using Type = platform::Event::Type;
  if (e.type == Type::Wake) return;
  if (!e.lifecycle()) {
    if (Screen* top = nav_.top(); top && top->rawEvent(e)) {
      dirty_ = true;
      return;
    }
  }
  if (e.type == Type::Text) {
    if (Screen* top = nav_.top()) top->text(e.text, e.done, e.confirmed);
    dirty_ = true;
    return;
  }
  if (e.type == Type::Swallowed) return;
  if (e.type == Type::Char) {
    // The Mac's keyboard: Search takes letters (and digits, space, delete)
    // as text; elsewhere they are the keys they stand for.
    Screen* top = nav_.top();
    if (top && !store_->dueReminder() && top->typed(e.text)) {
      dirty_ = true;
      lastKeyMs_ = ms_;
      if (pointer_.key(window_.ticks())) pointerAway();
      hideArrow();
      return;
    }
    if (e.key == Key::Unknown) return;
    platform::Event k = e;
    k.type = Type::KeyDown;
    handle(k);
    k.type = Type::KeyUp;
    handle(k);
    return;
  }

  switch (e.type) {
    case Type::Quit:
      quit_ = true;
      break;
    case Type::Hidden:
      if (!keyboardOpen()) {
        hidden_ = true;
        suspend();
      }
      break;
    case Type::Shown:
      hidden_ = false;
      dirty_ = true;
      break;
    case Type::Background:
      hidden_ = true;
      suspend();
      break;
    case Type::KeyDown: {
      Key key = e.key;
      if (key == Key::CursorShown) {
        // The system put its arrow up. Still the jolt of a key press: down
        // again. Later it may be the hand: leave it, movement decides (hiding
        // it then stopped all motion on one TV, so it could never come back).
        arrowUp_ = true;
        if (pointer_.shownByKey(window_.ticks())) {
          hideArrow();
        } else if (arrowHidden_) {
          window_.systemCursor(true);
          arrowHidden_ = false;
        }
        break;
      }
      if (key == Key::CursorHidden) {
        arrowUp_ = false;
        arrowHidden_ = false;
        if (pointer_.hidden()) pointerAway();
        break;
      }
      if (key == Key::Unknown) break;
      if (keys_.saw(key)) dirty_ = true;  // its hints appear
      // Any key (OK too) puts the pointer away: the keys take over.
      if (pointer_.key(window_.ticks())) pointerAway();
      hideArrow();
      if (key == Key::Ok) {
        pressOk(e.repeat);
        break;
      }
      KeyEvent k{key, e.repeat, false};
      k.swipe = e.swipe;
      k.glide = e.glide;
      dispatch(k);
      break;
    }
    case Type::KeyUp: {
      if (e.key == Key::Ok && okDown_) {
        okDown_ = false;
        if (!okLongSent_) dispatch({Key::Ok, false, false});
      }
      break;
    }
    case Type::PointerMove: {
      arrowUp_ = true;  // motion comes only with the system's arrow up
      bool was = pointer_.active();
      // Away, it takes real movement to come back (pointer_mode.hpp).
      if (!pointer_.moved(e.x, e.y, window_.ticks())) break;
      if (!was) {
        dirty_ = true;
        if (arrowHidden_) window_.systemCursor(true);
        arrowHidden_ = false;
      }
      pointerX_ = e.x * gfx::kLayoutWidth / static_cast<float>(std::max(1, g_.drawableWidth()));
      pointerY_ = e.y * gfx::kLayoutHeight / static_cast<float>(std::max(1, g_.drawableHeight()));
      // Last registered region wins: panels are drawn over the grid.
      for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        const ui::Rect& r = it->rect;
        if (pointerX_ >= r.x && pointerX_ < r.x + r.w && pointerY_ >= r.y && pointerY_ < r.y + r.h) {
          if (Screen* top = nav_.top()) top->hover(it->id);
          dirty_ = true;
          break;
        }
      }
      break;
    }
    case Type::PointerDown: {
      // With the pointer away a click is the OK key: on the focused item,
      // whatever the arrow is over.
      if (!pointer_.click(window_.ticks())) {
        hideArrow();
        pressOk(false);
        break;
      }
      // A click is OK. Only where OK has a hold menu does a slow click count
      // as holding it; elsewhere it clicks on release, however slow.
      Screen* top = nav_.top();
      if (top && top->holdsOk()) {
        okDown_ = true;
        okLongSent_ = false;
        okDownAt_ = window_.ticks();
      } else {
        clickDown_ = true;
      }
      break;
    }
    case Type::PointerUp:
      if (okDown_) {
        okDown_ = false;
        if (!okLongSent_) dispatch({Key::Ok, false, false});
      } else if (clickDown_) {
        clickDown_ = false;
        dispatch({Key::Ok, false, false});
      }
      break;
    case Type::Wheel:
      if (e.wheel != 0) {
        KeyEvent k{e.wheel > 0 ? Key::Up : Key::Down, false, false};
        k.wheel = true;
        dispatch(k);
      }
      break;
    default:
      break;
  }
}

void App::pressOk(bool repeat) {
  Screen* top = nav_.top();
  if (!top || !top->holdsOk()) {
    // No hold menu here: act now, not on release.
    if (!repeat) dispatch({Key::Ok, false, false});
    return;
  }
  if (!repeat) {
    okDown_ = true;
    okLongSent_ = false;
    okDownAt_ = window_.ticks();
  }
  // Reported on release or after the hold time (run()).
}

void App::pointerAway() {
  dirty_ = true;  // hover highlights go
  if (Screen* top = nav_.top()) top->pointerGone();
}

void App::hideArrow() {
  if (!arrowUp_) return;
  arrowUp_ = false;
  arrowHidden_ = true;
  window_.systemCursor(false);
}

void App::drawToast(ui::Ctx& c) {
  if (toast_.empty()) return;
  if (ms_ >= toastUntil_) {
    toast_.clear();
    return;
  }
  float w = ui::textWidth(c, toast_, ui::Weight::Medium, 24) + 56;
  w = std::min(w, 1400.0f);
  float y = toastTop_ ? 40.0f : gfx::kLayoutHeight - 150.0f;
  ui::Rect r{(gfx::kLayoutWidth - w) * 0.5f, y, w, 60};
  c.g.fill(r, ui::theme::hex(0x2A2D33, 0.96f), 30);
  ui::labelIn(c, toast_, {r.x + 28, r.y, r.w - 56, r.h}, ui::Weight::Medium, 24, ui::theme::white(),
              ui::Align::Center);
}

void App::drawReminder(ui::Ctx& c) {
  const auto& due = store_->dueReminder();
  if (!due) return;
  // The card hides itself after 15 s.
  if (ms_ - store_->dueReminderAt() > kReminderMs) {
    store_->dismissReminder();
    return;
  }
  ui::Rect r{gfx::kLayoutWidth - 80 - 760, 60, 760, 250};
  c.g.fill(r, ui::theme::hex(0x1C1F25, 0.97f), ui::theme::kPanelRadius);
  ui::label(c, "STARTING NOW", r.x + 36, r.y + 30, ui::Weight::Bold, 18, ui::theme::hex(ui::theme::kWatching));
  ui::label(c, due->title, r.x + 36, r.y + 62, ui::Weight::Bold, 30, ui::theme::white(), r.w - 72);
  ui::label(c, due->channelName, r.x + 36, r.y + 106, ui::Weight::Regular, 22, ui::theme::white(0.7f), r.w - 72);
  std::string watch = "Watch on " + due->channelName;
  float w = ui::chipWidth(c, watch, 22) + 40;
  ui::Rect b{r.x + 36, r.y + 156, std::min(w, r.w - 280), 60};
  c.g.fill(b, ui::theme::white(), 30);
  ui::labelIn(c, watch, {b.x + 20, b.y, b.w - 40, b.h}, ui::Weight::Bold, 22, ui::theme::black(), ui::Align::Center);
  ui::label(c, "OK watches \xc2\xb7 Back dismisses", b.x + b.w + 24, b.y + 18, ui::Weight::Regular, 18,
            ui::theme::white(0.55f));
}

void App::frame() {
  hits_.clear();
  ui::Ctx c{g_, t_, logos_.get(), &icons_, time::now(), ms_, &zone_, &hits_, pointer_.active(), store_->settings().clock24};
  g_.beginFrame(ui::theme::hex(ui::theme::kBackground));
  nav_.draw(c);
  drawToast(c);
  drawReminder(c);
  logos_->frame();
  window_.swap();
}

bool App::tickOwn(wake::Due& due) {
  bool draw = false;
  // What the screens show of playback (a picture coming up, a note, giving
  // up): drawn again as it changes.
  if (engine::Session::Look look = playback_->look(); look != look_) {
    look_ = std::move(look);
    draw = true;
  }
  // A channel opening: its spinner (the player's, a corner's) moves on
  // every 125 ms; nothing is drawn in between.
  if (playback_->opening()) {
    if (spinner_.moved(ms_)) draw = true;
    due.at(spinner_.next(ms_));
  }
  // The toast and the reminder card stay still: drawn as they come, and
  // once more as they go.
  if (!toast_.empty()) {
    if (ms_ >= toastUntil_) draw = true;
    else due.at(toastUntil_);
  }
  if (store_->dueReminder()) {
    uint32_t at = store_->dueReminderAt();
    if (at != reminderAt_) draw = true;
    reminderAt_ = at;
    if (ms_ - at > kReminderMs) draw = true;
    else due.at(at + kReminderMs + 1);
  }
  return draw;
}

void App::run() {
  using Clock = std::chrono::steady_clock;
  auto since = [](Clock::time_point t) {
    return std::chrono::duration<float, std::milli>(Clock::now() - t).count();
  };
  using Part = report::LoopStats::Part;
  while (!quit_ && !nav_.empty()) {
    // Where each step's time goes (once a minute in the log): the screen's
    // and playback's ticks, the event pump (keys, workers' results, the
    // video), the store's tick and the draw.
    report::LoopStats::Times spent{};
    ms_ = window_.ticks();
    Clock::time_point t = Clock::now();
    Screen* top = nav_.top();
    bool animating = top && top->tick(ms_);
    // Failover runs whatever is on top (a panel over the player too).
    playbackBusy_ = playback_->tick(ms_);
    // A screen waiting for a moment is woken then (not drawn meanwhile).
    wake::Due due;
    if (top) due.at(top->wakeAt());
    if (tickOwn(due)) animating = true;
    spent[Part::Screen] = since(t);
    // Idle: sleep until an event, a worker result, the next moment due or
    // the next clock minute. While a channel opens the loop looks at it
    // every frame, but draws only what changes.
    wake::Loop loop;
    loop.now = ms_;
    loop.animating = animating;
    loop.okHeld = okDown_ && !okLongSent_;
    loop.dirty = dirty_;
    loop.poll = playbackBusy_;
    loop.due = due.get();
    platform::Event e;
    bool woke = window_.nextEvent(e, wake::sleepMs(loop));
    t = Clock::now();
    if (woke) {
      handle(e);
      while (window_.nextEvent(e, 0)) handle(e);
    }
    ms_ = window_.ticks();
    if (okDown_ && !okLongSent_ && ms_ - okDownAt_ >= kHoldMs) {
      okLongSent_ = true;
      dispatch({Key::Ok, false, true});
    }
    window_.frame();
    bool results = pool_.pump() > 0;
    if (results) dirty_ = true;
    spent[Part::Events] = since(t);
    t = Clock::now();
    bool stored = store_->tick(ms_);
    if (stored) dirty_ = true;
    spent[Part::Store] = since(t);
    t = Clock::now();
    video_.pump();
    if (nav_.settle()) {
      dirty_ = true;
      // A corner preview belongs to the guide: a full screen over it (Live
      // Events, search, setup) ends it; side panels leave it playing.
      if (playback_->active() && !playback_->full()) {
        const Screen* under = nav_.topOpaque();
        if (!under || !under->keepsPreview()) playback_->stop();
      }
    }
    time::Seconds minute = time::now() / 60;
    if (minute != lastMinute_) {
      lastMinute_ = minute;
      dirty_ = true;  // clock and the now line
    }
    spent[Part::Events] += since(t);
    bool drew = dirty_ || animating;
    if (drew) {
      t = Clock::now();
      dirty_ = false;
      frame();
      spent[Part::Draw] = since(t);
    }
    loopStats_.step(spent, drew, hidden_, drew || stored || results);
    Screen* shown = nav_.top();
    std::string line = loopStats_.line(ms_, shown ? shown->name() : "none");
    if (!line.empty()) MLOG_I("loop", "%s", line.c_str());
  }
  MLOG_I("app", "main loop ended");
  video_.stop();
}

}  // namespace marigold::app
