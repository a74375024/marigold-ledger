#include "platform/android/jni.hpp"

#include <pthread.h>

#include <mutex>
#include <unordered_map>

#include "core/util/log.hpp"

namespace marigold::android {
namespace {

JavaVM* gVm = nullptr;
jclass gBridge = nullptr;
pthread_key_t gDetachKey;

std::mutex gMethodsMu;
std::unordered_map<std::string, jmethodID> gMethods;

void detach(void*) {
  if (gVm) gVm->DetachCurrentThread();
}

}  // namespace

JavaVM* vm() { return gVm; }

JNIEnv* env() {
  if (!gVm) return nullptr;
  JNIEnv* e = nullptr;
  if (gVm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_OK) return e;
  if (gVm->AttachCurrentThread(&e, nullptr) != JNI_OK) return nullptr;
  // Detached again when the thread ends (a worker or the app thread).
  pthread_setspecific(gDetachKey, e);
  return e;
}

jclass bridge() { return gBridge; }

jmethodID method(const char* name, const char* signature) {
  std::string key = std::string(name) + signature;
  std::lock_guard<std::mutex> lock(gMethodsMu);
  auto it = gMethods.find(key);
  if (it != gMethods.end()) return it->second;
  JNIEnv* e = env();
  jmethodID id = e ? e->GetStaticMethodID(gBridge, name, signature) : nullptr;
  if (!id) {
    if (e) e->ExceptionClear();
    MLOG_E("jni", "Bridge.%s%s missing", name, signature);
  }
  gMethods.emplace(std::move(key), id);
  return id;
}

jbyteArray bytes(JNIEnv* e, std::string_view s) {
  jbyteArray a = e->NewByteArray(static_cast<jsize>(s.size()));
  if (a && !s.empty())
    e->SetByteArrayRegion(a, 0, static_cast<jsize>(s.size()), reinterpret_cast<const jbyte*>(s.data()));
  return a;
}

std::string text(JNIEnv* e, jbyteArray a) {
  if (!a) return {};
  jsize n = e->GetArrayLength(a);
  std::string out(static_cast<size_t>(n), '\0');
  if (n > 0) e->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(out.data()));
  return out;
}

jstring string(JNIEnv* e, std::string_view utf8) {
  static jmethodID str = method("str", "([B)Ljava/lang/String;");
  Local<jbyteArray> b(e, bytes(e, utf8));
  auto s = static_cast<jstring>(e->CallStaticObjectMethod(gBridge, str, b.get()));
  caught(e, "str");
  return s;
}

std::string text(JNIEnv* e, jstring s) {
  if (!s) return {};
  static jmethodID utf8 = method("utf8", "(Ljava/lang/String;)[B");
  Local<jbyteArray> b(e, static_cast<jbyteArray>(e->CallStaticObjectMethod(gBridge, utf8, s)));
  caught(e, "utf8");
  return text(e, b.get());
}

void recycle(JNIEnv* e, jobject bitmap) {
  static jmethodID id = method("recycle", "(Landroid/graphics/Bitmap;)V");
  e->CallStaticVoidMethod(gBridge, id, bitmap);
  caught(e, "recycle");
}

bool caught(JNIEnv* e, const char* where) {
  if (!e->ExceptionCheck()) return false;
  e->ExceptionDescribe();  // to logcat; never a link (Java side catches its own)
  e->ExceptionClear();
  MLOG_W("jni", "Java exception in %s", where);
  return true;
}

}  // namespace marigold::android

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
  using namespace marigold::android;
  gVm = vm;
  pthread_key_create(&gDetachKey, detach);
  JNIEnv* e = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
  jclass local = e->FindClass("org/marigold/app/Bridge");
  if (!local) return JNI_ERR;
  gBridge = static_cast<jclass>(e->NewGlobalRef(local));
  e->DeleteLocalRef(local);
  return JNI_VERSION_1_6;
}
