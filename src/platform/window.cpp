#include "platform/window.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>

#include <cstdlib>
#include <cstring>

#include "core/util/log.hpp"
#include "entrada_texto.h"
#include "sdlcompat.h"

namespace marigold::platform {

struct Window::Impl {
  SDL_Window* window = nullptr;
  void* context = nullptr;
  int drawableW = 1920;
  int drawableH = 1080;
  std::string sdlRuntime;
  std::string glRenderer;
  bool transparent = false;
  uint32_t wakeEvent = 0;
  SDL_Event current;  // the last event, for Event::native

  void makeSurfaceTransparent();
};

void prepareEnvironment(const char* appId) {
  // Without the app identity the webOS SDL registers the surface as "(null)"
  // and the compositor never shows the window (Legacy measured "Invalid appId
  // specified OR Unsupported Application Type").
  setenv("APPID", appId, 0);
  setenv("LS2_APPID", appId, 0);
  setenv("SDL_VIDEODRIVER", "wayland", 0);
  if (!getenv("XDG_RUNTIME_DIR")) setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 1);
}

Window::Window() : impl_(std::make_unique<Impl>()) {}

Window::~Window() {
  if (impl_->context) SDL_GL_DeleteContext(impl_->context);
  if (impl_->window) SDL_DestroyWindow(impl_->window);
}

bool Window::open(const char* title) {
  Impl& m = *impl_;
  // The compositor swallows Back (and opens the app bar) unless the surface
  // asks for the key. LG's SDL reads this hint only when the window is
  // created; Back then arrives as webOS scancode 482.
  SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    MLOG_E("window", "SDL_Init: %s", SDL_GetError());
    return false;
  }
  // Before any thread: see nv_blindar_formatos in sdlcompat.h (Legacy #65).
  nv_blindar_formatos();

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  // An alpha channel is what lets the video plane show through the UI.
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

  // The webOS compositor fixes a native app's surface at 1080p and ignores
  // larger requests (measured by Legacy on webOS 4.10), so ask for 1080p.
  m.window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920, 1080,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
  if (!m.window) {
    MLOG_E("window", "SDL_CreateWindow: %s", SDL_GetError());
    return false;
  }
  // On webOS, SDL_ShowCursor(SDL_DISABLE) also stops mouse motion and button
  // events from the Magic Remote (Legacy, measured on a C9). Keep it on: the
  // arrow drawn is the system's own.
  SDL_ShowCursor(SDL_ENABLE);
  m.makeSurfaceTransparent();

  m.context = SDL_GL_CreateContext(m.window);
  if (!m.context) {
    MLOG_E("window", "SDL_GL_CreateContext: %s", SDL_GetError());
    return false;
  }
  SDL_GL_SetSwapInterval(1);
  SDL_GL_GetDrawableSize(m.window, &m.drawableW, &m.drawableH);
  if (m.drawableW < 1 || m.drawableH < 1) {
    m.drawableW = 1920;
    m.drawableH = 1080;
  }
  const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  m.glRenderer = renderer ? renderer : "?";
  GLint alphaBits = -1;
  glGetIntegerv(GL_ALPHA_BITS, &alphaBits);
  MLOG_I("window", "GPU %s | %s, drawable %dx%d, alpha bits %d", m.glRenderer.c_str(),
         version ? version : "?", m.drawableW, m.drawableH, alphaBits);
  if (alphaBits <= 0) MLOG_W("window", "framebuffer has no alpha: video cannot show through");
  m.wakeEvent = SDL_RegisterEvents(1);
  return true;
}

void Window::Impl::makeSurfaceTransparent() {
  // By default the compositor treats the window as opaque and drops its alpha
  // channel; the hole the UI clears for the video would exist in the
  // framebuffer and still show nothing. Legacy's two measured traps:
  //  1. this SDL writes a SDL_SysWMinfo LARGER than the header declares, so the
  //     struct goes in a roomy buffer;
  //  2. `version` must come from SDL_GetVersion(), or SDL refuses silently.
  static union {
    SDL_SysWMinfo info;
    char bytes[512];
  } wm;
  SDL_SysWMinfo* info = &wm.info;
  const char* infoBuf = wm.bytes;
  SDL_GetVersion(&info->version);
  sdlRuntime = std::to_string(info->version.major) + "." + std::to_string(info->version.minor) +
                "." + std::to_string(info->version.patch);
  MLOG_I("window", "SDL runtime %s", sdlRuntime.c_str());
  if (!SDL_GetWindowWMInfo(window, info)) {
    MLOG_W("window", "SDL_GetWindowWMInfo failed: %s", SDL_GetError());
    return;
  }
  // The SDK's SDL_config.h has the Wayland driver off, so info.wl is not in
  // the header, but the TV's SDL IS Wayland. Read by offset: version takes 3
  // bytes (aligned to 4), subsystem sits at 4, and the union starts at 8. For
  // Wayland it is {display, surface, ...}.
  int subsystem = 0;
  std::memcpy(&subsystem, infoBuf + 4, sizeof subsystem);
  void* fields[2] = {nullptr, nullptr};
  std::memcpy(fields, infoBuf + 8, sizeof fields);
  void* surface = fields[1];
  bool versionOk = info->version.major > 2 ||
                   (info->version.major == 2 && (info->version.minor > 0 || info->version.patch >= 2));
  if (subsystem != static_cast<int>(SDL_SYSWM_WAYLAND) || !versionOk || !surface) {
    MLOG_W("window", "not a usable Wayland surface (subsystem %d): skipping set_opaque_region",
           subsystem);
    return;
  }
  void* wl = dlopen("libwayland-client.so.0", RTLD_NOW);
  using Marshal = void (*)(void*, unsigned, ...);
  Marshal marshal = nullptr;
  if (wl) {
    void* sym = dlsym(wl, "wl_proxy_marshal");
    std::memcpy(&marshal, &sym, sizeof marshal);
  }
  if (!marshal) {
    MLOG_W("window", "no libwayland-client: video will not show");
    return;
  }
  // wl_surface opcode 4 is set_opaque_region; NULL means "nothing is opaque".
  // No commit here: the next SwapWindow commits.
  marshal(surface, 4, nullptr);
  transparent = true;
  MLOG_I("window", "surface marked non-opaque");
}

void Window::swap() { SDL_GL_SwapWindow(impl_->window); }

bool Window::nextEvent(Event& out, int timeoutMs) {
  SDL_Event& e = impl_->current;
  if (!(timeoutMs == 0 ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, timeoutMs))) return false;
  out = Event{};
  out.native = &e;
  // The system keyboard sees text before anything else (Legacy main.c).
  texto_sistema_observar(&e);
  if (e.type == impl_->wakeEvent) {
    out.type = Event::Type::Wake;
    return true;
  }
  if (e.type == texto_sistema_evento()) {
    out.type = Event::Type::Text;
    out.done = e.user.code == TS_EV_FIM;
    out.confirmed = out.done && e.user.data1 != nullptr;
    out.text = texto_sistema_valor();
    return true;
  }
  if (texto_sistema_aberto() && texto_sistema_engole(&e)) {
    out.type = Event::Type::Swallowed;
    return true;
  }
  switch (e.type) {
    case SDL_QUIT:
      out.type = Event::Type::Quit;
      break;
    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_HIDDEN || e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        out.type = Event::Type::Hidden;
      else if (e.window.event == SDL_WINDOWEVENT_SHOWN || e.window.event == SDL_WINDOWEVENT_EXPOSED ||
               e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
        out.type = Event::Type::Shown;
      break;
    case SDL_APP_WILLENTERBACKGROUND:
      out.type = Event::Type::Background;
      break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      out.type = e.type == SDL_KEYDOWN ? Event::Type::KeyDown : Event::Type::KeyUp;
      out.key = keyFromSdl(static_cast<int>(e.key.keysym.scancode), e.key.keysym.sym);
      out.repeat = e.key.repeat != 0;
      break;
    case SDL_MOUSEMOTION:
      out.type = Event::Type::PointerMove;
      out.x = static_cast<float>(e.motion.x);
      out.y = static_cast<float>(e.motion.y);
      break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      if (e.button.button == SDL_BUTTON_LEFT)
        out.type = e.type == SDL_MOUSEBUTTONDOWN ? Event::Type::PointerDown : Event::Type::PointerUp;
      break;
    case SDL_MOUSEWHEEL:
      out.type = Event::Type::Wheel;
      out.wheel = e.wheel.y;
      break;
    default:
      break;
  }
  return true;
}

void Window::wake() {
  // Thread safe: wakes SDL_WaitEventTimeout.
  SDL_Event e;
  SDL_zero(e);
  e.type = impl_->wakeEvent;
  SDL_PushEvent(&e);
}

uint32_t Window::ticks() const { return SDL_GetTicks(); }
void Window::frame() { texto_sistema_quadro(); }

bool Window::keyboardAvailable() const { return (texto_sistema_disponivel() & TS_TECLADO) != 0; }
void Window::openKeyboard(const std::string& value) { texto_sistema_abrir(value.c_str(), 0); }
void Window::closeKeyboard() { texto_sistema_fechar(); }
bool Window::keyboardOpen() const { return texto_sistema_aberto() != 0; }

bool Window::leave() { return false; }

int Window::drawableWidth() const { return impl_->drawableW; }
int Window::drawableHeight() const { return impl_->drawableH; }
const std::string& Window::runtime() const { return impl_->sdlRuntime; }
const std::string& Window::glRenderer() const { return impl_->glRenderer; }
bool Window::transparentSurface() const { return impl_->transparent; }

}  // namespace marigold::platform
