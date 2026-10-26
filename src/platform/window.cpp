#include "platform/window.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>

#include <cstdlib>
#include <cstring>

#include "core/util/log.hpp"
#include "sdlcompat.h"

namespace marigold::platform {

void prepareEnvironment(const char* appId) {
  // Without the app identity the webOS SDL registers the surface as "(null)"
  // and the compositor never shows the window (Legacy measured "Invalid appId
  // specified OR Unsupported Application Type").
  setenv("APPID", appId, 0);
  setenv("LS2_APPID", appId, 0);
  setenv("SDL_VIDEODRIVER", "wayland", 0);
  if (!getenv("XDG_RUNTIME_DIR")) setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 1);
}

Window::~Window() {
  if (context_) SDL_GL_DeleteContext(context_);
  if (window_) SDL_DestroyWindow(window_);
}

bool Window::open(const char* title) {
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
  window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920, 1080,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
  if (!window_) {
    MLOG_E("window", "SDL_CreateWindow: %s", SDL_GetError());
    return false;
  }
  // On webOS, SDL_ShowCursor(SDL_DISABLE) also stops mouse motion and button
  // events from the Magic Remote (Legacy, measured on a C9). Keep it on: the
  // arrow drawn is the system's own.
  SDL_ShowCursor(SDL_ENABLE);
  makeSurfaceTransparent();

  context_ = SDL_GL_CreateContext(window_);
  if (!context_) {
    MLOG_E("window", "SDL_GL_CreateContext: %s", SDL_GetError());
    return false;
  }
  SDL_GL_SetSwapInterval(1);
  SDL_GL_GetDrawableSize(window_, &drawableW_, &drawableH_);
  if (drawableW_ < 1 || drawableH_ < 1) {
    drawableW_ = 1920;
    drawableH_ = 1080;
  }
  const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  glRenderer_ = renderer ? renderer : "?";
  GLint alphaBits = -1;
  glGetIntegerv(GL_ALPHA_BITS, &alphaBits);
  MLOG_I("window", "GPU %s | %s, drawable %dx%d, alpha bits %d", glRenderer_.c_str(),
         version ? version : "?", drawableW_, drawableH_, alphaBits);
  if (alphaBits <= 0) MLOG_W("window", "framebuffer has no alpha: video cannot show through");
  return true;
}

void Window::makeSurfaceTransparent() {
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
  sdlRuntime_ = std::to_string(info->version.major) + "." + std::to_string(info->version.minor) +
                "." + std::to_string(info->version.patch);
  MLOG_I("window", "SDL runtime %s", sdlRuntime_.c_str());
  if (!SDL_GetWindowWMInfo(window_, info)) {
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
  transparent_ = true;
  MLOG_I("window", "surface marked non-opaque");
}

void Window::swap() { SDL_GL_SwapWindow(window_); }

}  // namespace marigold::platform
