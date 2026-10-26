// Marigold entry point: platform setup, then the main loop.
#include <SDL2/SDL.h>
#include <sys/stat.h>

#include <clocale>
#include <cstdio>
#include <string>

#include "app/test_screen.hpp"
#include "app_id.h"
#include "core/util/log.hpp"
#include "entrada_texto.h"
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
  // Data files (and the stream link the test screen saves) are the app's only.
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

  app::SystemInfo info;
  info.version = MARIGOLD_VERSION;
  info.build = MARIGOLD_BUILD;
  info.webosMajor = nv_webos_major();
  info.sdlRuntime = window.sdlRuntime();
  info.glRenderer = window.glRenderer();
  info.drawableW = window.drawableWidth();
  info.drawableH = window.drawableHeight();
  info.transparentSurface = window.transparentSurface();
  app::TestScreen screen(g, text, video, storage, info);

  bool running = true;
  while (running && !screen.wantsQuit()) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      uint32_t now = SDL_GetTicks();
      // The system keyboard sees text before any screen (Legacy main.c).
      texto_sistema_observar(&e);
      if (e.type == SDL_QUIT) {
        running = false;
      } else if (e.type == SDL_WINDOWEVENT) {
        if ((e.window.event == SDL_WINDOWEVENT_HIDDEN ||
             e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) &&
            !texto_sistema_aberto()) {
          screen.suspend();
        }
        MLOG_I("app", "window event %d", e.window.event);
      } else if (e.type == SDL_APP_WILLENTERBACKGROUND) {
        screen.suspend();
      }
      screen.handle(e, now);
    }
    texto_sistema_quadro();
    video.pump();
    screen.frame(SDL_GetTicks());
    window.swap();
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
