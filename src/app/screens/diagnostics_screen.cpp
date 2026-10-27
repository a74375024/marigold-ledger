// The M0 test screen, reachable from Options › Help: every remote key, the
// pointer, the keyboard and the video plane, for checking a new TV.
#include <SDL2/SDL.h>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/test_screen.hpp"
#include "webosver.h"

namespace marigold::app {
namespace {

class DiagnosticsScreen : public Screen {
 public:
  explicit DiagnosticsScreen(App& app) : Screen(app), test_(app.renderer(), app.textRenderer(), app.video(), app.storage(), info(app)) {
    app.playback().stop();
  }

  bool rawEvent(const platform::Event& e) override {
    if (e.native) test_.handle(*static_cast<const SDL_Event*>(e.native), SDL_GetTicks());
    return true;
  }
  bool key(const KeyEvent&) override { return true; }
  bool tick(uint32_t) override {
    if (test_.wantsQuit()) {
      test_.suspend();
      app_.nav().pop();
    }
    return true;  // it animates its own key log and video state
  }
  void draw(ui::Ctx&) override { test_.frame(SDL_GetTicks()); }

 private:
  static SystemInfo info(App& app) {
    SystemInfo i;
    i.version = app.build().version;
    i.build = app.build().build;
    i.webosMajor = nv_webos_major();
    i.sdlRuntime = app.window().runtime();
    i.glRenderer = app.window().glRenderer();
    i.drawableW = app.window().drawableWidth();
    i.drawableH = app.window().drawableHeight();
    i.transparentSurface = app.window().transparentSurface();
    return i;
  }
  TestScreen test_;
};

}  // namespace

std::unique_ptr<Screen> makeDiagnostics(App& app) { return std::make_unique<DiagnosticsScreen>(app); }

}  // namespace marigold::app
