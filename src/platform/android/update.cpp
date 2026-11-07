// Android and Fire TV (Build 148): the update's APK goes to the system's
// installer (Updates.java), which replaces the app. The download goes to the
// app's cache, where the FileProvider serves it from.
#include "platform/update.hpp"

#include "platform/android/jni.hpp"

namespace marigold::platform::update {

std::string asset() { return "marigold-android.apk"; }

std::string file() {
  JNIEnv* e = android::env();
  static jmethodID id = android::method("updateFile", "()[B");
  if (!e || !id) return {};
  android::Local<jbyteArray> path(e, static_cast<jbyteArray>(e->CallStaticObjectMethod(android::bridge(), id)));
  if (android::caught(e, "updateFile")) return {};
  return android::text(e, path.get());
}

engine::UpdateInstall install(const std::string& path) {
  engine::UpdateInstall out;
  JNIEnv* e = android::env();
  static jmethodID id = android::method("updateInstall", "([B)[B");
  if (!e || !id) {
    out.why = "no Java side";
    return out;
  }
  android::Local<jbyteArray> arg(e, android::bytes(e, path));
  android::Local<jbyteArray> why(
      e, static_cast<jbyteArray>(e->CallStaticObjectMethod(android::bridge(), id, arg.get())));
  if (android::caught(e, "updateInstall")) {
    out.why = "the installer couldn't start";
    return out;
  }
  out.why = android::text(e, why.get());
  out.ok = out.why.empty();
  return out;
}

}  // namespace marigold::platform::update
