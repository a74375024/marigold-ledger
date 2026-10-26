#include "app/app.hpp"

#include <SDL2/SDL.h>

#include <algorithm>

#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/util/log.hpp"
#include "entrada_texto.h"
#include "layout.h"  // NV_HOLD_MS

namespace marigold::app {
namespace {

constexpr uint32_t kToastMs = 3500;
constexpr int kPoolThreads = 4;

}  // namespace

App::App(platform::Window& window, platform::Storage& storage, gfx::Renderer& g,
         text::TextRenderer& t, platform::Video& video, Build build)
    : window_(window), storage_(storage), g_(g), t_(t), video_(video), build_(std::move(build)) {
  wakeEvent_ = SDL_RegisterEvents(1);
  uint32_t wake = wakeEvent_;
  pool_.start(kPoolThreads, [wake] {
    // Thread safe: wakes SDL_WaitEventTimeout so pump() runs promptly.
    SDL_Event e;
    SDL_zero(e);
    e.type = wake;
    SDL_PushEvent(&e);
  });
  std::string logoDir = storage_.dir().empty() ? std::string("/tmp/marigold-logos")
                                               : storage_.path("logos");
  logos_ = std::make_unique<logos::LogoCache>(pool_, logoDir);
  store_ = std::make_unique<Store>(*this);
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
  toastUntil_ = SDL_GetTicks() + kToastMs;
  dirty_ = true;
}

bool App::openKeyboard(const std::string& current) {
  if (!(texto_sistema_disponivel() & TS_TECLADO)) return false;
  texto_sistema_abrir(current.c_str(), 0);
  return true;
}

void App::closeKeyboard() { texto_sistema_fechar(); }
bool App::keyboardOpen() const { return texto_sistema_aberto() != 0; }

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

void App::handle(const SDL_Event& e) {
  // The system keyboard sees text before any screen (Legacy main.c).
  texto_sistema_observar(&e);
  if (e.type == wakeEvent_) return;
  bool lifecycle = e.type == SDL_QUIT || e.type == SDL_WINDOWEVENT || e.type == SDL_APP_WILLENTERBACKGROUND;
  if (!lifecycle) {
    if (Screen* top = nav_.top(); top && top->rawEvent(e)) {
      dirty_ = true;
      return;
    }
  }
  if (e.type == texto_sistema_evento()) {
    if (Screen* top = nav_.top()) {
      bool closed = e.user.code == TS_EV_FIM;
      top->text(texto_sistema_valor(), closed, closed && e.user.data1 != nullptr);
    }
    dirty_ = true;
    return;
  }
  if (keyboardOpen() && texto_sistema_engole(&e)) return;

  switch (e.type) {
    case SDL_QUIT:
      quit_ = true;
      break;
    case SDL_WINDOWEVENT:
      if ((e.window.event == SDL_WINDOWEVENT_HIDDEN ||
           e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) &&
          !keyboardOpen())
        suspend();
      if (e.window.event == SDL_WINDOWEVENT_SHOWN || e.window.event == SDL_WINDOWEVENT_EXPOSED ||
          e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
        dirty_ = true;
      break;
    case SDL_APP_WILLENTERBACKGROUND:
      suspend();
      break;
    case SDL_KEYDOWN: {
      Key key = keyFromSdl(static_cast<int>(e.key.keysym.scancode), e.key.keysym.sym);
      if (key == Key::CursorShown || key == Key::CursorHidden) {
        pointerShown_ = key == Key::CursorShown;
        dirty_ = true;
        break;
      }
      if (key == Key::Unknown) break;
      if (pointerShown_ && key != Key::Ok) pointerShown_ = false;  // arrows take over
      if (key == Key::Ok) {
        if (!e.key.repeat) {
          okDown_ = true;
          okLongSent_ = false;
          okDownAt_ = SDL_GetTicks();
        }
        break;  // reported on release or after the hold time (frame())
      }
      dispatch({key, e.key.repeat != 0, false});
      break;
    }
    case SDL_KEYUP: {
      Key key = keyFromSdl(static_cast<int>(e.key.keysym.scancode), e.key.keysym.sym);
      if (key == Key::Ok && okDown_) {
        okDown_ = false;
        if (!okLongSent_) dispatch({Key::Ok, false, false});
      }
      break;
    }
    case SDL_MOUSEMOTION: {
      pointerShown_ = true;
      pointerX_ = static_cast<float>(e.motion.x) * gfx::kLayoutWidth / static_cast<float>(std::max(1, g_.drawableWidth()));
      pointerY_ = static_cast<float>(e.motion.y) * gfx::kLayoutHeight / static_cast<float>(std::max(1, g_.drawableHeight()));
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
    case SDL_MOUSEBUTTONDOWN:
      if (e.button.button == SDL_BUTTON_LEFT) {
        okDown_ = true;
        okLongSent_ = false;
        okDownAt_ = SDL_GetTicks();
      }
      break;
    case SDL_MOUSEBUTTONUP:
      if (e.button.button == SDL_BUTTON_LEFT && okDown_) {
        okDown_ = false;
        if (!okLongSent_) dispatch({Key::Ok, false, false});
      }
      break;
    case SDL_MOUSEWHEEL:
      if (e.wheel.y != 0) dispatch({e.wheel.y > 0 ? Key::Up : Key::Down, false, false});
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
  ui::Ctx c{g_, t_, logos_.get(), time::now(), ms_, &zone_, &hits_, pointerShown_, store_->settings().clock24};
  g_.beginFrame(ui::theme::hex(ui::theme::kBackground));
  nav_.draw(c);
  drawToast(c);
  drawReminder(c);
  logos_->frame();
  window_.swap();
}

void App::run() {
  while (!quit_ && !nav_.empty()) {
    ms_ = SDL_GetTicks();
    Screen* top = nav_.top();
    bool animating = top && top->tick(ms_);
    // Failover runs whatever is on top (a panel over the player too).
    if (playback_->tick(ms_)) animating = true;
    if (!toast_.empty()) animating = animating || ms_ < toastUntil_ + 50;
    if (store_->dueReminder()) animating = true;
    bool okHeld = okDown_ && !okLongSent_;
    // Idle: sleep until an event, a worker result or the next clock minute.
    int timeout = animating ? 0 : okHeld ? 50 : dirty_ ? 0 : 500;
    SDL_Event e;
    if (timeout == 0 ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, timeout)) {
      handle(e);
      while (SDL_PollEvent(&e)) handle(e);
    }
    ms_ = SDL_GetTicks();
    if (okDown_ && !okLongSent_ && ms_ - okDownAt_ >= NV_HOLD_MS) {
      okLongSent_ = true;
      dispatch({Key::Ok, false, true});
    }
    texto_sistema_quadro();
    if (pool_.pump() > 0) dirty_ = true;
    if (store_->tick(ms_)) dirty_ = true;
    video_.pump();
    if (nav_.settle()) dirty_ = true;
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
