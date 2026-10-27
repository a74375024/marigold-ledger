#include "app/launch.hpp"

#include <sys/stat.h>

#include <clocale>
#include <cstdio>
#include <memory>

#include "app/screens/guide_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/util/log.hpp"
#include "core/util/memory.hpp"
#include "platform/icons.hpp"

namespace marigold::app {

void prepareProcess() {
  memory::configure();  // before any thread starts
  // Data files (accounts, caches, logs) are the app's only.
  umask(077);
  // Numbers with a dot, always: JSON payloads are built with printf.
  std::setlocale(LC_NUMERIC, "C");
}

void openStorage(platform::Storage& storage, const std::string& appDir) {
  storage.init(appDir);
  if (storage.dir().empty()) return;
  // The previous session's log survives one launch, for crashes.
  std::string logPath = storage.path("marigold.log");
  std::rename(logPath.c_str(), storage.path("marigold-previous.log").c_str());
  log::open(logPath);
}

int run(platform::Window& window, platform::Storage& storage, platform::Video& video, const std::string& assetDir,
        Build build) {
  gfx::Renderer& g = gfx::renderer();
  if (!g.init(window.drawableWidth(), window.drawableHeight())) {
    MLOG_E("app", "renderer init failed");
    return 1;
  }
  text::TextRenderer text;
  text.init(assetDir + "/fonts");

  video.start();
  {
    icons::IconSet icons;  // GL textures: gone before the renderer shuts down
    icons.init(assetDir + "/icons");
    App app(window, storage, g, text, icons, video, std::move(build));
    if (app.store().accounts().empty()) app.nav().push(makeAccounts(app, true));
    else app.nav().push(std::make_unique<GuideScreen>(app));
    app.nav().settle();
    app.run();
  }

  MLOG_I("app", "exiting");
  video.shutdown();
  text.shutdown();
  g.shutdown();
  return 0;
}

}  // namespace marigold::app
