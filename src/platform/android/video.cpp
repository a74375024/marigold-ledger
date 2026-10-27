// Android: the video player is ExoPlayer (androidx.media3) in Bridge's
// Player, drawing on the SurfaceView under the UI. ExoPlayer lives on the
// UI thread; Java keeps a snapshot of its state that state() reads here.
#include "platform/video.hpp"

#include <cmath>

#include "core/util/log.hpp"
#include "core/util/redact.hpp"
#include "platform/android/jni.hpp"

namespace marigold::platform {
namespace {

// A Bridge method that takes nothing.
void call(const char* name) {
  JNIEnv* e = android::env();
  if (!e) return;
  e->CallStaticVoidMethod(android::bridge(), android::method(name, "()V"));
  android::caught(e, name);
}

void window(gfx::Rect r, bool force) {
  JNIEnv* e = android::env();
  if (!e) return;
  static jmethodID id = android::method("videoWindow", "(IIIIZ)V");
  e->CallStaticVoidMethod(android::bridge(), id, static_cast<jint>(std::lround(r.x)), static_cast<jint>(std::lround(r.y)),
                          static_cast<jint>(std::lround(r.w)), static_cast<jint>(std::lround(r.h)),
                          static_cast<jboolean>(force));
  android::caught(e, "videoWindow");
}

}  // namespace

bool Video::start() {
  started_ = true;
  return true;
}

bool Video::play(const std::string& url, const std::string& headers, int loadMode) {
  if (url.empty()) return false;
  JNIEnv* e = android::env();
  if (!e) return false;
  static jmethodID id = android::method("videoPlay", "([B[B)V");
  android::Local<jbyteArray> u(e, android::bytes(e, url));
  android::Local<jbyteArray> h(e, android::bytes(e, headers));
  e->CallStaticVoidMethod(android::bridge(), id, u.get(), h.get());
  bool ok = !android::caught(e, "videoPlay");
  MLOG_I("video", "play (mode %d): %s", loadMode, ok ? "sent" : "refused");
  return ok;
}

void Video::stop() { call("videoStop"); }
void Video::pump() {}
void Video::setWindow(gfx::Rect r) { window(r, false); }
void Video::reapplyWindow(gfx::Rect r) { window(r, true); }

void Video::shutdown() {
  if (started_) call("videoRelease");
  started_ = false;
}

VideoState Video::state() const {
  VideoState s;
  s.registered = started_;
  JNIEnv* e = android::env();
  if (!e) return s;
  // Bridge.videoState fills: flags, bufferingMs, position, bufferEnd, width, height.
  static jmethodID id = android::method("videoState", "([D)[B");
  android::Local<jdoubleArray> values(e, e->NewDoubleArray(6));
  android::Local<jbyteArray> error(e, static_cast<jbyteArray>(e->CallStaticObjectMethod(android::bridge(), id, values.get())));
  if (android::caught(e, "videoState")) return s;
  double v[6] = {0, 0, 0, 0, 0, 0};
  e->GetDoubleArrayRegion(values.get(), 0, 6, v);
  int flags = static_cast<int>(v[0]);
  s.active = flags & 1;
  s.ready = flags & 2;
  s.playing = flags & 4;
  s.failed = flags & 8;
  s.ended = flags & 16;
  s.decoderSeen = flags & 32;
  s.audioUnsupported = flags & 64;
  s.bufferingMs = static_cast<unsigned>(v[1]);
  s.position = v[2];
  s.bufferEnd = v[3];
  s.width = static_cast<int>(v[4]);
  s.height = static_cast<int>(v[5]);
  s.error = redact(android::text(e, error.get()));
  return s;
}

}  // namespace marigold::platform
