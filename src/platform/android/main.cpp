// The Android entry point: the activity (MarigoldActivity.java) starts the
// app thread once per process, and it runs the same app::run as the TV's
// main.cpp. When the app ends (Exit), Java finishes the activity.
#include <android/log.h>

#include <string>
#include <thread>

#include "app/launch.hpp"
#include "core/util/log.hpp"
#include "platform/android/jni.hpp"
#include "platform/storage.hpp"
#include "platform/video.hpp"
#include "platform/window.hpp"

#ifndef MARIGOLD_VERSION
#define MARIGOLD_VERSION "0.0.0"
#endif
#ifndef MARIGOLD_BUILD
#define MARIGOLD_BUILD "dev"
#endif

namespace {

void logcat(marigold::log::Level level, const std::string& line) {
  using marigold::log::Level;
  int priority = level == Level::Error  ? ANDROID_LOG_ERROR
                 : level == Level::Warn ? ANDROID_LOG_WARN
                 : level == Level::Info ? ANDROID_LOG_INFO
                                        : ANDROID_LOG_DEBUG;
  __android_log_write(priority, "marigold", line.c_str());
}

void appThread(std::string assetDir, std::string filesDir) {
  using namespace marigold;
  log::setMirror(logcat);
  app::prepareProcess();
  platform::Storage storage;
  app::openStorage(storage, filesDir);
  MLOG_I("app", "Marigold %s build %s (Android), data in %s", MARIGOLD_VERSION, MARIGOLD_BUILD,
         storage.dir().empty() ? "(nowhere writable)" : storage.dir().c_str());

  int code = 1;
  {
    platform::Window window;
    if (window.open("Marigold")) {
      app::Build build;
      build.version = MARIGOLD_VERSION;
      build.build = MARIGOLD_BUILD;
      build.system = window.runtime();
      platform::Video video;
      code = app::run(window, storage, video, assetDir, std::move(build));
    }
  }
  MLOG_I("app", "app thread done (%d)", code);
  log::close();
  if (JNIEnv* e = android::env()) {
    static jmethodID finish = android::method("finish", "()V");
    e->CallStaticVoidMethod(android::bridge(), finish);
    android::caught(e, "finish");
  }
}

}  // namespace

extern "C" JNIEXPORT void JNICALL Java_org_marigold_app_Bridge_nativeStart(JNIEnv* env, jclass, jbyteArray assetDir,
                                                                           jbyteArray filesDir) {
  static bool started = false;
  if (started) return;  // a new activity in the same process: the app is still running
  started = true;
  std::thread(appThread, marigold::android::text(env, assetDir), marigold::android::text(env, filesDir)).detach();
}
