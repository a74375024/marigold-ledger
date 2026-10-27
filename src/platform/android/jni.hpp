// The Java side of the Android platform (android/app/src/main/java/org/
// marigold/app/Bridge.java) as the native code sees it: the VM, the calling
// thread's JNIEnv (attached on first use, detached when the thread ends), the
// Bridge class and its static methods, and UTF-8 strings both ways. Strings
// cross as byte arrays: JNI's own "modified UTF-8" breaks on emoji, which
// channel and programme names carry.
#pragma once

#include <jni.h>

#include <string>
#include <string_view>

namespace marigold::android {

// JNI_OnLoad keeps the VM and looks the class up once (FindClass from a
// native thread would only see the system classes).
JavaVM* vm();
JNIEnv* env();          // this thread's, attaching it if needed
jclass bridge();        // org.marigold.app.Bridge (a global reference)
jmethodID method(const char* name, const char* signature);  // a static method of Bridge, cached

jbyteArray bytes(JNIEnv* e, std::string_view s);         // a new local byte[]
std::string text(JNIEnv* e, jbyteArray a);               // "" for null
jstring string(JNIEnv* e, std::string_view utf8);        // via Bridge.str(byte[])
std::string text(JNIEnv* e, jstring s);                  // via Bridge.utf8(String)

// Frees a Bitmap's pixels now rather than at the next GC.
void recycle(JNIEnv* e, jobject bitmap);

// Native threads never return to Java, so their local references are never
// freed for them: every one made there must be deleted (Local does it).

// Clears a pending Java exception; true if there was one (it is logged).
bool caught(JNIEnv* e, const char* where);

// A local reference released at the end of the scope.
template <typename T>
class Local {
 public:
  Local(JNIEnv* e, T ref) : e_(e), ref_(ref) {}
  ~Local() {
    if (ref_) e_->DeleteLocalRef(ref_);
  }
  Local(const Local&) = delete;
  Local& operator=(const Local&) = delete;
  T get() const { return ref_; }
  explicit operator bool() const { return ref_ != nullptr; }

 private:
  JNIEnv* e_;
  T ref_;
};

}  // namespace marigold::android
