// The webOS entry point: the TV-specific set-up around app::run (launch.hpp).
#include <SDL2/SDL.h>

#include <cstdio>
#include <string>

#include "app/launch.hpp"
#include "app_id.h"
#include "core/util/log.hpp"
#include "platform/stdout_capture.hpp"
#include "platform/storage.hpp"
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
  app::prepareProcess();
  platform::prepareEnvironment(NV_APP_ID);

  const std::string appDir = installDir();
  platform::Storage storage;
  app::openStorage(storage, appDir);
  platform::captureStdout();
  MLOG_I("app", "Marigold %s build %s, data in %s", MARIGOLD_VERSION, MARIGOLD_BUILD,
         storage.dir().empty() ? "(nowhere writable)" : storage.dir().c_str());

  platform::Window window;
  if (!window.open("Marigold")) {
    platform::releaseStdout();
    return 1;
  }
  video_escala_definir(window.drawableWidth(), window.drawableHeight());

  app::Build build;
  build.version = MARIGOLD_VERSION;
  build.build = MARIGOLD_BUILD;
  build.system = "webOS " + std::to_string(nv_webos_major());
  platform::Video video;
  int code = app::run(window, storage, video, appDir, std::move(build));

  SDL_Quit();
  platform::releaseStdout();
  log::close();
  return code;
}
