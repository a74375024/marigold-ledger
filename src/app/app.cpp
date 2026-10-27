#include "app/app.hpp"

#include <algorithm>

#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/util/log.hpp"

namespace marigold::app {
namespace {

constexpr uint32_t kToastMs = 3500;
// OK held this long counts as a long press (Legacy's NV_HOLD_MS, as Android TV).
constexpr uint32_t kHoldMs = 700;
constexpr int kPoolThreads = 4;

}  // namespace

App::App(platform::Window& window, platform::Storage& storage, gfx::Renderer& g,
         text::TextRenderer& t, icons::IconSet& icons, platform::Video& video, Build build)
    : window_(window), storage_(storage), g_(g), t_(t), icons_(icons), video_(video), build_(std::move(build)) {
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
  host.logoStatus = [this](const std::string& url) { return logos_->status(url); };
  host.requestsCancelled = [this] { logos_->abandonLoads(); };
  host.build = build_.build;
  store_ = std::make_unique<Store>(std::move(host));
  playback_ = std::make_unique<Playback>(*this);
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
  // Leaving the app: the decoder goes back to the TV.
  video_.stop();
  dirty_ = true;
}

void App::dispatch(const KeyEvent& k) {
  Screen* top = nav_.top();
  dirty_ = true;
  // A reminder card takes OK (watch) and Back (dismiss) while it shows.
  if (const auto& due = store_->dueReminder()) {
    if (k.key == Key::Ok) {
      std::string id = due->channelId;
      store_->dismissReminder();
      if (playback_->tune(id)) {
        playback_->setFull();
        nav_.push(makePlayer(*this));
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
  }
  if (k.key == Key::Exit) quit_ = true;
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

  switch (e.type) {
    case Type::Quit:
      quit_ = true;
      break;
    case Type::Hidden:
      if (!keyboardOpen()) suspend();
      break;
    case Type::Shown:
      dirty_ = true;
      break;
    case Type::Background:
      suspend();
      break;
    case Type::KeyDown: {
      Key key = e.key;
      if (key == Key::CursorShown || key == Key::CursorHidden) {
        pointerShown_ = key == Key::CursorShown;
        dirty_ = true;
        break;
      }
      if (key == Key::Unknown) break;
      if (pointerShown_ && key != Key::Ok) pointerShown_ = false;  // arrows take over
      if (key == Key::Ok) {
        if (!e.repeat) {
          okDown_ = true;
          okLongSent_ = false;
          okDownAt_ = window_.ticks();
        }
        break;  // reported on release or after the hold time (frame())
      }
      dispatch({key, e.repeat, false});
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
      pointerShown_ = true;
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
    case Type::PointerDown:
      okDown_ = true;
      okLongSent_ = false;
      okDownAt_ = window_.ticks();
      break;
    case Type::PointerUp:
      if (okDown_) {
        okDown_ = false;
        if (!okLongSent_) dispatch({Key::Ok, false, false});
      }
      break;
    case Type::Wheel:
      if (e.wheel != 0) dispatch({e.wheel > 0 ? Key::Up : Key::Down, false, false});
      break;
    default:
      break;
  }
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
  if (ms_ - store_->dueReminderAt() > 15000) {
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
  ui::Ctx c{g_, t_, logos_.get(), &icons_, time::now(), ms_, &zone_, &hits_, pointerShown_, store_->settings().clock24};
  g_.beginFrame(ui::theme::hex(ui::theme::kBackground));
  nav_.draw(c);
  drawToast(c);
  drawReminder(c);
  logos_->frame();
  window_.swap();
}

void App::run() {
  while (!quit_ && !nav_.empty()) {
    ms_ = window_.ticks();
    Screen* top = nav_.top();
    bool animating = top && top->tick(ms_);
    // Failover runs whatever is on top (a panel over the player too).
    if (playback_->tick(ms_)) animating = true;
    if (!toast_.empty()) animating = animating || ms_ < toastUntil_ + 50;
    if (store_->dueReminder()) animating = true;
    bool okHeld = okDown_ && !okLongSent_;
    // Idle: sleep until an event, a worker result or the next clock minute.
    int timeout = animating ? 0 : okHeld ? 50 : dirty_ ? 0 : 500;
    platform::Event e;
    if (window_.nextEvent(e, timeout)) {
      handle(e);
      while (window_.nextEvent(e, 0)) handle(e);
    }
    ms_ = window_.ticks();
    if (okDown_ && !okLongSent_ && ms_ - okDownAt_ >= kHoldMs) {
      okLongSent_ = true;
      dispatch({Key::Ok, false, true});
    }
    window_.frame();
    if (pool_.pump() > 0) dirty_ = true;
    if (store_->tick(ms_)) dirty_ = true;
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
    if (dirty_ || animating) {
      dirty_ = false;
      frame();
    }
  }
  MLOG_I("app", "main loop ended");
  video_.stop();
}

}  // namespace marigold::app
