// Android: the window is the activity's UI SurfaceView (translucent, over
// the video's SurfaceView, fixed at 1920x1080 and scaled by the display).
// The app thread owns EGL; Java's UI thread hands over the surface and the
// events (keys, lifecycle, the keyboard's text) through one queue. When the
// surface goes (the app is in the background), the context stays current on
// a 1x1 pbuffer, so textures survive and drawing is harmless.
#include "platform/window.hpp"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window_jni.h>
#include <sys/system_properties.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

#include "core/util/log.hpp"
#include "platform/android/jni.hpp"

namespace marigold::platform {
namespace {

// What Java hands over, from before the window opens to after it closes.
struct Shared {
  std::mutex mu;
  std::condition_variable cv;
  std::deque<Event> events;
  ANativeWindow* offered = nullptr;  // the surface Java gave last (null: gone)
  bool changed = false;              // offered differs from what the app thread uses
  ANativeWindow* inUse = nullptr;    // what the app thread draws on
  bool wake = false;
  bool keyboard = false;
};

Shared& shared() {
  static Shared s;
  return s;
}

void push(Event e) {
  Shared& s = shared();
  {
    std::lock_guard<std::mutex> lock(s.mu);
    if (e.type == Event::Type::Text && e.done) s.keyboard = false;
    s.events.push_back(std::move(e));
  }
  s.cv.notify_all();
}

std::string property(const char* name) {
  char value[PROP_VALUE_MAX] = {0};
  __system_property_get(name, value);
  return value;
}

}  // namespace

struct Window::Impl {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLConfig config = nullptr;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface pbuffer = EGL_NO_SURFACE;
  EGLSurface surface = EGL_NO_SURFACE;
  ANativeWindow* window = nullptr;
  int drawableW = 1920, drawableH = 1080;
  std::string runtime, glRenderer;
  bool amazon = false;  // a Fire TV: its remote's keys are known
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

  // App thread: takes up the surface Java offered (or lets the old one go).
  // Called with the lock held.
  void adopt(Shared& s) {
    if (!s.changed) return;
    s.changed = false;
    if (surface != EGL_NO_SURFACE) {
      eglMakeCurrent(display, pbuffer, pbuffer, context);
      eglDestroySurface(display, surface);
      surface = EGL_NO_SURFACE;
    }
    if (window) ANativeWindow_release(window);
    window = nullptr;
    s.inUse = nullptr;
    if (s.offered) {
      window = s.offered;
      ANativeWindow_acquire(window);
      surface = eglCreateWindowSurface(display, config, window, nullptr);
      if (surface == EGL_NO_SURFACE || !eglMakeCurrent(display, surface, surface, context)) {
        MLOG_E("window", "surface not usable (EGL 0x%x)", eglGetError());
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        surface = EGL_NO_SURFACE;
        eglMakeCurrent(display, pbuffer, pbuffer, context);
      } else {
        eglSwapInterval(display, 1);
        EGLint w = 0, h = 0;
        eglQuerySurface(display, surface, EGL_WIDTH, &w);
        eglQuerySurface(display, surface, EGL_HEIGHT, &h);
        MLOG_I("window", "surface %dx%d", w, h);
        Event shown;
        shown.type = Event::Type::Shown;
        s.events.push_back(shown);
      }
      s.inUse = window;
    }
    s.cv.notify_all();  // nativeSurface(null) waits for this
  }
};

Window::Window() : impl_(std::make_unique<Impl>()) {}

Window::~Window() {
  Impl& m = *impl_;
  if (m.display == EGL_NO_DISPLAY) return;
  eglMakeCurrent(m.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (m.surface != EGL_NO_SURFACE) eglDestroySurface(m.display, m.surface);
  if (m.pbuffer != EGL_NO_SURFACE) eglDestroySurface(m.display, m.pbuffer);
  if (m.context != EGL_NO_CONTEXT) eglDestroyContext(m.display, m.context);
  eglTerminate(m.display);
  {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.mu);
    if (m.window) ANativeWindow_release(m.window);
    s.inUse = nullptr;
    s.cv.notify_all();
  }
}

bool Window::open(const char*) {
  Impl& m = *impl_;
  m.runtime = "Android " + property("ro.build.version.release") + " (API " + property("ro.build.version.sdk") + ")";
  std::string maker = property("ro.product.manufacturer");
  m.amazon = maker == "Amazon";
  MLOG_I("window", "%s, %s %s", m.runtime.c_str(), maker.c_str(), property("ro.product.model").c_str());
  m.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (m.display == EGL_NO_DISPLAY || !eglInitialize(m.display, nullptr, nullptr)) {
    MLOG_E("window", "eglInitialize failed");
    return false;
  }
  // An alpha channel is what lets the video's surface show through the UI.
  const EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                            EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                            EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE};
  EGLint count = 0;
  if (!eglChooseConfig(m.display, attribs, &m.config, 1, &count) || count < 1) {
    MLOG_E("window", "no RGBA8888 ES2 config");
    return false;
  }
  const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  m.context = eglCreateContext(m.display, m.config, EGL_NO_CONTEXT, contextAttribs);
  const EGLint pbufferAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  m.pbuffer = eglCreatePbufferSurface(m.display, m.config, pbufferAttribs);
  if (m.context == EGL_NO_CONTEXT || m.pbuffer == EGL_NO_SURFACE ||
      !eglMakeCurrent(m.display, m.pbuffer, m.pbuffer, m.context)) {
    MLOG_E("window", "EGL context failed (0x%x)", eglGetError());
    return false;
  }
  // The activity offers its surface as soon as it is laid out.
  {
    Shared& s = shared();
    std::unique_lock<std::mutex> lock(s.mu);
    s.cv.wait_for(lock, std::chrono::seconds(10), [&] { return s.changed; });
    m.adopt(s);
  }
  const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  m.glRenderer = renderer ? renderer : "?";
  MLOG_I("window", "GPU %s | %s, drawable %dx%d", m.glRenderer.c_str(), version ? version : "?", m.drawableW,
         m.drawableH);
  return true;
}

void Window::swap() {
  Impl& m = *impl_;
  if (m.surface == EGL_NO_SURFACE) return;
  if (!eglSwapBuffers(m.display, m.surface)) {
    EGLint err = eglGetError();
    if (err == EGL_BAD_SURFACE || err == EGL_BAD_NATIVE_WINDOW) {
      MLOG_W("window", "surface lost (0x%x)", err);
      eglMakeCurrent(m.display, m.pbuffer, m.pbuffer, m.context);
      eglDestroySurface(m.display, m.surface);
      m.surface = EGL_NO_SURFACE;
    }
  }
}

bool Window::nextEvent(Event& e, int timeoutMs) {
  Shared& s = shared();
  std::unique_lock<std::mutex> lock(s.mu);
  auto ready = [&] { return s.changed || s.wake || !s.events.empty(); };
  if (!ready() && timeoutMs > 0) s.cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready);
  impl_->adopt(s);
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
  JNIEnv* e = android::env();
  static jmethodID open = android::method("keyboardOpen", "([B)V");
  android::Local<jbyteArray> v(e, android::bytes(e, value));
  e->CallStaticVoidMethod(android::bridge(), open, v.get());
  android::caught(e, "keyboardOpen");
}

void Window::closeKeyboard() {
  JNIEnv* e = android::env();
  static jmethodID close = android::method("keyboardClose", "()V");
  e->CallStaticVoidMethod(android::bridge(), close);
  android::caught(e, "keyboardClose");
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
  JNIEnv* e = android::env();
  static jmethodID leave = android::method("leave", "()V");
  e->CallStaticVoidMethod(android::bridge(), leave);
  android::caught(e, "leave");
  return true;
}

// Other Android TV remotes differ (a Chromecast remote has no menu or media
// keys); their keys are learned as they are pressed.
RemoteKeys Window::remoteKeys() const { return impl_->amazon ? RemoteKeys::fireTv() : RemoteKeys{}; }

int Window::drawableWidth() const { return impl_->drawableW; }
int Window::drawableHeight() const { return impl_->drawableH; }
const std::string& Window::runtime() const { return impl_->runtime; }
const std::string& Window::glRenderer() const { return impl_->glRenderer; }
bool Window::transparentSurface() const { return true; }

void prepareEnvironment(const char*) {}

}  // namespace marigold::platform

// Java's side (Bridge.java). All on the activity's UI thread.
extern "C" {

JNIEXPORT void JNICALL Java_org_marigold_app_Bridge_nativeSurface(JNIEnv* env, jclass, jobject surface) {
  using namespace marigold::platform;
  Shared& s = shared();
  ANativeWindow* w = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
  std::unique_lock<std::mutex> lock(s.mu);
  if (s.offered) ANativeWindow_release(s.offered);
  s.offered = w;
  s.changed = true;
  s.cv.notify_all();
  // A surface going away must not be drawn on once this returns: wait for
  // the app thread to let it go (it checks every loop, well under a second).
  if (!w && s.inUse) s.cv.wait_for(lock, std::chrono::seconds(3), [&] { return !s.inUse; });
}

JNIEXPORT jboolean JNICALL Java_org_marigold_app_Bridge_nativeKey(JNIEnv*, jclass, jint code, jboolean down,
                                                                  jint repeat) {
  using namespace marigold;
  Key key = keyFromAndroid(code);
  if (key == Key::Unknown) return JNI_FALSE;  // volume and the like stay the system's
  platform::Event e;
  e.type = down ? platform::Event::Type::KeyDown : platform::Event::Type::KeyUp;
  e.key = key;
  e.repeat = repeat > 0;
  platform::push(std::move(e));
  return JNI_TRUE;
}

// 0 quit, 1 hidden, 2 shown, 3 background.
JNIEXPORT void JNICALL Java_org_marigold_app_Bridge_nativeLifecycle(JNIEnv*, jclass, jint what) {
  using Type = marigold::platform::Event::Type;
  marigold::platform::Event e;
  const Type types[] = {Type::Quit, Type::Hidden, Type::Shown, Type::Background};
  if (what < 0 || what > 3) return;
  e.type = types[what];
  marigold::platform::push(std::move(e));
}

JNIEXPORT void JNICALL Java_org_marigold_app_Bridge_nativeText(JNIEnv* env, jclass, jbyteArray value, jboolean done,
                                                               jboolean confirmed) {
  marigold::platform::Event e;
  e.type = marigold::platform::Event::Type::Text;
  e.text = marigold::android::text(env, value);
  e.done = done;
  e.confirmed = confirmed;
  marigold::platform::push(std::move(e));
}

}  // extern "C"
