// tvOS: the window is the view controller's Metal layer (main.mm), fixed at
// 1920x1080 over the video view. The app thread draws on it (gfx_metal.mm)
// and reads events from one queue that UIKit's main thread fills: presses,
// swipes, the scene's life and the keyboard's text.
#include "platform/window.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>
#include <sys/sysctl.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

#include "core/util/log.hpp"
#include "platform/tvos/bridge.hpp"
#include "platform/tvos/metal.hpp"

namespace marigold::platform {
namespace {

struct Shared {
  std::mutex mu;
  std::condition_variable cv;
  std::deque<Event> events;
  bool wake = false;
  bool keyboard = false;
};

Shared& shared() {
  static Shared s;
  return s;
}

std::string model() {
  size_t size = 0;
  if (sysctlbyname("hw.machine", nullptr, &size, nullptr, 0) != 0 || size == 0) return "?";
  std::string out(size, '\0');
  if (sysctlbyname("hw.machine", out.data(), &size, nullptr, 0) != 0) return "?";
  out.resize(size > 0 ? size - 1 : 0);
  return out;
}

}  // namespace

struct Window::Impl {
  std::string runtime, glRenderer;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

Window::Window() : impl_(std::make_unique<Impl>()) {}
Window::~Window() { tvos::metal::close(); }

bool Window::open(const char*) {
  Impl& m = *impl_;
  NSString* version = nil;
  CAMetalLayer* layer = nil;
  tvos::onMainSync([&] {
    version = UIDevice.currentDevice.systemVersion;
    layer = tvos::uiLayer();
  });
  m.runtime = "tvOS " + std::string(version.UTF8String ?: "?") + " (" + model() + ")";
  MLOG_I("window", "%s", m.runtime.c_str());
  if (!layer || !tvos::metal::open(layer)) {
    MLOG_E("window", "no Metal layer");
    return false;
  }
  m.glRenderer = tvos::metal::deviceName();
  MLOG_I("window", "GPU %s, drawable 1920x1080", m.glRenderer.c_str());
  return true;
}

void Window::swap() { tvos::metal::present(); }

bool Window::nextEvent(Event& e, int timeoutMs) {
  Shared& s = shared();
  std::unique_lock<std::mutex> lock(s.mu);
  auto ready = [&] { return s.wake || !s.events.empty(); };
  if (!ready() && timeoutMs > 0) s.cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready);
  if (s.wake) {
    s.wake = false;
    e = Event{};
    e.type = Event::Type::Wake;
    return true;
  }
  if (s.events.empty()) return false;
  e = std::move(s.events.front());
  s.events.pop_front();
  return true;
}

void Window::wake() {
  Shared& s = shared();
  {
    std::lock_guard<std::mutex> lock(s.mu);
    s.wake = true;
  }
  s.cv.notify_all();
}

uint32_t Window::ticks() const {
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - impl_->start).count());
}

void Window::frame() {}

bool Window::keyboardAvailable() const { return true; }

void Window::openKeyboard(const std::string& value) {
  {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.mu);
    s.keyboard = true;
  }
  tvos::openKeyboard(value);
}

void Window::closeKeyboard() {
  tvos::closeKeyboard();
  Shared& s = shared();
  std::lock_guard<std::mutex> lock(s.mu);
  s.keyboard = false;
}

bool Window::keyboardOpen() const {
  Shared& s = shared();
  std::lock_guard<std::mutex> lock(s.mu);
  return s.keyboard;
}

bool Window::leave() {
  tvos::leaveToHome();
  return true;
}

// Apple's rule for an app's top screen: Back goes to the Home screen.
bool Window::leaveOnFirstBack() const { return true; }

// The Siri Remote (a TV's own remote over HDMI adds its keys as they're pressed).
RemoteKeys Window::remoteKeys() const { return RemoteKeys::appleTv(); }

int Window::drawableWidth() const { return 1920; }
int Window::drawableHeight() const { return 1080; }
const std::string& Window::runtime() const { return impl_->runtime; }
const std::string& Window::glRenderer() const { return impl_->glRenderer; }
bool Window::transparentSurface() const { return true; }

void prepareEnvironment(const char*) {}

}  // namespace marigold::platform

namespace marigold::tvos {

void push(platform::Event e) {
  platform::Shared& s = platform::shared();
  {
    std::lock_guard<std::mutex> lock(s.mu);
    if (e.type == platform::Event::Type::Text && e.done) s.keyboard = false;
    s.events.push_back(std::move(e));
  }
  s.cv.notify_all();
}

}  // namespace marigold::tvos
