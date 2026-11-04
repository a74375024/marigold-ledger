#include "app/launch.hpp"

#include <sys/resource.h>
#include <sys/stat.h>

#include <clocale>
#include <cstdio>
#include <memory>

#include "app/crash_note.hpp"
#include "app/screens/guide_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/engine/disk.hpp"
#include "core/util/log.hpp"
#include "core/util/memory.hpp"
#include "platform/icons.hpp"

namespace marigold::app {

void prepareProcess() {
  // Never a core dump: hundreds of MB with logins and stream links in it,
  // written to the working folder (on webOS the install folder, on the
  // partition every developer app shares). A crash leaves a short note.
  struct rlimit noCore;
  noCore.rlim_cur = noCore.rlim_max = 0;
  setrlimit(RLIMIT_CORE, &noCore);
  // Before Legacy's IME guard saves the handlers it hands faults on to.
  crash::install();
  memory::configure();  // before any thread starts
  // Data files (accounts, caches, logs) are the app's only.
  umask(077);
  // Numbers with a dot, always: JSON payloads are built with printf.
  std::setlocale(LC_NUMERIC, "C");
}

void openStorage(platform::Storage& storage, const std::string& appDir) {
  storage.init(appDir);
  // Core dumps older builds left in the working folder (the install folder
  // on webOS) and the data folder: a week of crashes filled the TV's app
  // partition with them.
  engine::disk::Swept cores = engine::disk::removeCoreDumps(appDir);
  if (storage.dir().empty()) return;
  if (storage.dir() != appDir) {
    engine::disk::Swept more = engine::disk::removeCoreDumps(storage.dir());
    cores.files += more.files;
    cores.bytes += more.bytes;
  }
  // Writes a crash or a full disk cut short (nothing writes yet).
  engine::disk::Swept temps = engine::disk::removeStaleTemps(storage.dir());
  // The previous session's log survives one launch, for crashes.
  std::string logPath = storage.path("marigold.log");
  std::rename(logPath.c_str(), storage.path("marigold-previous.log").c_str());
  log::open(logPath, engine::disk::kLogBytes);
  if (cores.files)
    MLOG_W("disk", "removed %zu core dump(s), %llu MB", cores.files,
           static_cast<unsigned long long>(cores.bytes / engine::disk::kMB));
  if (temps.files)
    MLOG_I("disk", "removed %zu unfinished file(s), %llu MB", temps.files,
           static_cast<unsigned long long>(temps.bytes / engine::disk::kMB));
  crash::openNotes(storage.dir());
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
    if (app.store().accounts().empty()) app.nav().push(makeWelcome(app));
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
