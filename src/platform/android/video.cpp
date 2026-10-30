// Android: the video player is ExoPlayer (androidx.media3) in Bridge's
// Player, drawing on the SurfaceView under the UI. ExoPlayer lives on the
// UI thread; Java keeps a snapshot of its state that state() reads here.
#include "platform/video.hpp"

#include <cmath>
#include <cstdlib>

#include "core/util/log.hpp"
#include "core/util/redact.hpp"
#include "core/util/strings.hpp"
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
  s.reportsLoading = true;  // the buffered position
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

// ExoPlayer decodes the captions (CEA-608 in the video, WebVTT, DVB text);
// no view of its own draws them: the app does, from textLines().
std::vector<Video::TextTrack> Video::textTracks() const {
  std::vector<TextTrack> out;
  JNIEnv* e = android::env();
  if (!e) return out;
  static jmethodID id = android::method("videoTextTracks", "()[B");
  android::Local<jbyteArray> b(e, static_cast<jbyteArray>(e->CallStaticObjectMethod(android::bridge(), id)));
  if (android::caught(e, "videoTextTracks")) return out;
  // One track a line: id, language, 608 channel (tab separated).
  std::string all = android::text(e, b.get());
  for (std::string_view line : str::split(all, '\n')) {
    std::vector<std::string_view> f = str::split(line, '\t', true);
    if (f.size() < 3 || f[0].empty()) continue;
    TextTrack t;
    t.id = std::string(f[0]);
    t.language = std::string(f[1]);
    t.channel = std::atoi(std::string(f[2]).c_str());
    out.push_back(std::move(t));
  }
  return out;
}

void Video::selectText(const std::string& track, bool on) {
  JNIEnv* e = android::env();
  if (!e) return;
  static jmethodID id = android::method("videoSelectText", "([BZ)V");
  android::Local<jbyteArray> t(e, android::bytes(e, track));
  e->CallStaticVoidMethod(android::bridge(), id, t.get(), static_cast<jboolean>(on));
  android::caught(e, "videoSelectText");
}

std::vector<std::string> Video::textLines(uint64_t* version) const {
  if (version) *version = 0;
  JNIEnv* e = android::env();
  if (!e) return {};
  static jmethodID id = android::method("videoTextLines", "([J)[B");
  android::Local<jlongArray> v(e, e->NewLongArray(1));
  android::Local<jbyteArray> b(e, static_cast<jbyteArray>(e->CallStaticObjectMethod(android::bridge(), id, v.get())));
  if (android::caught(e, "videoTextLines")) return {};
  jlong n = 0;
  e->GetLongArrayRegion(v.get(), 0, 1, &n);
  if (version) *version = static_cast<uint64_t>(n);
  std::string all = android::text(e, b.get());
  std::vector<std::string> out;
  for (std::string_view line : str::split(all, '\n', true)) out.emplace_back(line);
  return out;
}

bool Video::drawsText() { return false; }
void Video::setTextStyle(int, int) {}

}  // namespace marigold::platform
