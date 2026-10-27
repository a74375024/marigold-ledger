// The app window: its GLES2 context, its events and the system keyboard.
// Each platform supplies one behind this header (webOS: window.cpp over the
// TV's SDL, set up the way Legacy's main.c does; Android: android/window.cpp
// over EGL and the activity; tvOS: tvos/window.mm over UIKit and Metal). The app sees only platform::Event.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "core/input/keys.hpp"

namespace marigold::platform {

struct Event {
  enum class Type : uint8_t {
    None,         // nothing the app acts on
    Wake,         // wake() from a worker
    Quit,
    Hidden,       // the window lost focus or was hidden
    Shown,        // shown, exposed or focused again: redraw
    Background,   // the app is going to the background
    KeyDown,
    KeyUp,
    PointerMove,  // x, y
    PointerDown,  // the pointer's button (OK)
    PointerUp,
    Wheel,        // wheel: +1 up, -1 down
    Text,         // the system keyboard's field value
    Swallowed,    // a key the open system keyboard uses
  };
  Type type = Type::None;
  Key key = Key::Unknown;
  bool repeat = false;  // a held key repeating
  float x = 0, y = 0;   // pointer, in drawable pixels
  int wheel = 0;
  // Text: the whole field value; `done` when the keyboard closed, with
  // `confirmed` when it closed on OK.
  std::string text;
  bool done = false, confirmed = false;
  // The platform's own event (webOS: the SDL_Event), valid until the next
  // nextEvent(). For the webOS key test screen only.
  const void* native = nullptr;

  bool lifecycle() const {
    return type == Type::Quit || type == Type::Hidden || type == Type::Shown || type == Type::Background;
  }
};

class Window {
 public:
  Window();
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // Creates the fullscreen 1920x1080 surface with an alpha channel (the video
  // shows through where the UI clears it) and makes its GL context current.
  bool open(const char* title);
  void swap();

  // The next event, waiting up to `timeoutMs` (0: don't wait). False when
  // none came.
  bool nextEvent(Event& e, int timeoutMs);
  // Any thread: makes a waiting nextEvent() return (a Wake event).
  void wake();
  // Monotonic milliseconds.
  uint32_t ticks() const;
  // Once per loop, on the UI thread (the system keyboard's own timing).
  void frame();

  // The system keyboard. It sends Text events with the whole field value.
  bool keyboardAvailable() const;
  void openKeyboard(const std::string& value);
  void closeKeyboard();
  bool keyboardOpen() const;

  // Back with nothing left to close: Android and tvOS go to the home screen
  // (the app keeps running, as TV apps do) and return true; webOS does
  // nothing.
  bool leave();
  // Whether Back on the guide leaves at once (tvOS: Apple's rule for the
  // top screen) instead of after "Press Back again to exit".
  bool leaveOnFirstBack() const;

  // The keys this platform's remote is known to have (webOS: the LG
  // remote's; Android: a Fire TV remote's on Amazon devices, else none
  // known). The app adds the ones it sees pressed.
  RemoteKeys remoteKeys() const;

  int drawableWidth() const;
  int drawableHeight() const;
  // What runs underneath, for Help and the logs: the TV's SDL version on
  // webOS ("2.0.5"), "Android 9 (API 28)" on Android.
  const std::string& runtime() const;
  const std::string& glRenderer() const;
  bool transparentSurface() const;

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

// webOS: the process environment the TV's SDL and LS2 need before SDL_Init.
// Call first. Nothing to do elsewhere.
void prepareEnvironment(const char* appId);

}  // namespace marigold::platform
