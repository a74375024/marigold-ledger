// Marigold entry point: platform setup, then the main loop.
#include <SDL2/SDL.h>
#include <sys/stat.h>

#include <clocale>
#include <cstdio>
#include <string>

#include "app/app.hpp"
#include "app/screens/guide_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "app_id.h"
#include "core/util/log.hpp"
#include "core/util/memory.hpp"
#include "platform/gfx.hpp"
#include "platform/stdout_capture.hpp"
#include "platform/storage.hpp"
#include "platform/text.hpp"
#include "platform/video.hpp"
#include "platform/window.hpp"
#include "video.h"
#include "webosver.h"

#ifndef MARIGOLD_VERSION
#define MARIGOLD_VERSION "0.0.0"
#endif
#ifndef MARIGOLD_BUILD
#define MARIGOLD_BUILD "dev"
#endif

namespace {

std::string installDir() {
  char* base = SDL_GetBasePath();
  std::string dir = base ? base : "./";
  SDL_free(base);
  while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
  return dir;
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  using namespace marigold;
  memory::configure();  // before any thread starts
  // Data files (accounts, caches, logs) are the app's only.
  umask(077);
  // Numbers with a dot, always: JSON payloads are built with printf.
  std::setlocale(LC_NUMERIC, "C");
  platform::prepareEnvironment(NV_APP_ID);

  const std::string appDir = installDir();
  platform::Storage storage;
  storage.init(appDir);
  if (!storage.dir().empty()) {
    // The previous session's log survives one launch, for crashes.
    std::string logPath = storage.path("marigold.log");
    std::rename(logPath.c_str(), storage.path("marigold-previous.log").c_str());
    log::open(logPath);
  }
  platform::captureStdout();
  MLOG_I("app", "Marigold %s build %s, data in %s", MARIGOLD_VERSION, MARIGOLD_BUILD,
         storage.dir().empty() ? "(nowhere writable)" : storage.dir().c_str());

  platform::Window window;
  if (!window.open("Marigold")) {
    platform::releaseStdout();
    return 1;
  }
  video_escala_definir(window.drawableWidth(), window.drawableHeight());

  gfx::Renderer& g = gfx::renderer();
  if (!g.init(window.drawableWidth(), window.drawableHeight())) {
    MLOG_E("app", "renderer init failed");
    platform::releaseStdout();
    return 1;
  }
  text::TextRenderer text;
  text.init(appDir + "/fonts");

  platform::Video video;
  video.start();

  app::Build build;
  build.version = MARIGOLD_VERSION;
  build.build = MARIGOLD_BUILD;
  build.webosMajor = nv_webos_major();
  {
    app::App app(window, storage, g, text, video, build);
    if (app.store().accounts().empty()) app.nav().push(app::makeAccounts(app, true));
    else app.nav().push(std::make_unique<app::GuideScreen>(app));
    app.nav().settle();
    app.run();
  }

  MLOG_I("app", "exiting");
  video.shutdown();
  text.shutdown();
  g.shutdown();
  SDL_Quit();
  platform::releaseStdout();
  log::close();
  return 0;
}
