// The app window and its GLES2 context, set up the way Legacy's main.c does
// on webOS (see the comments in window.cpp for why each step is there).
#pragma once

#include <string>

struct SDL_Window;

namespace marigold::platform {

class Window {
 public:
  Window() = default;
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // Creates the 1920x1080 fullscreen window, makes the surface non-opaque so
  // the video plane shows through, and creates the GL context.
  bool open(const char* title);
  void swap();

  SDL_Window* sdl() const { return window_; }
  int drawableWidth() const { return drawableW_; }
  int drawableHeight() const { return drawableH_; }
  // "2.0.5", the SDL that is actually loaded (the TV's, not the SDK's).
  const std::string& sdlRuntime() const { return sdlRuntime_; }
  const std::string& glRenderer() const { return glRenderer_; }
  bool transparentSurface() const { return transparent_; }

 private:
  void makeSurfaceTransparent();

  SDL_Window* window_ = nullptr;
  void* context_ = nullptr;
  int drawableW_ = 1920;
  int drawableH_ = 1080;
  std::string sdlRuntime_;
  std::string glRenderer_;
  bool transparent_ = false;
};

// Process environment the webOS SDL and LS2 need before SDL_Init. Call first.
void prepareEnvironment(const char* appId);

}  // namespace marigold::platform
