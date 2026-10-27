// Android: Options › Help › Diagnostics shows what the app runs on. (The TV's
// key and video test screen, app/screens/diagnostics_screen.cpp, reads raw
// SDL events and is webOS only.)
#include <string>
#include <vector>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "core/util/log.hpp"
#include "platform/video.hpp"

namespace marigold::app {
namespace {

class DiagnosticsScreen : public Screen {
 public:
  explicit DiagnosticsScreen(App& app) : Screen(app) {}

  bool key(const KeyEvent& e) override {
    if (e.key == Key::Back || e.key == Key::Ok) app_.nav().pop();
    return true;
  }
  bool tick(uint32_t) override { return true; }  // the video state and log move

  void draw(ui::Ctx& c) override {
    using ui::theme::white;
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, ui::theme::hex(ui::theme::kBackground));
    float x = 120, y = 90;
    ui::label(c, "Diagnostics", x, y, ui::Weight::Bold, 44, white());
    y += 90;
    platform::VideoState v = app_.video().state();
    const platform::Window& w = app_.window();
    std::vector<std::string> lines = {
        "Marigold " + app_.build().version + " (build " + app_.build().build + ")",
        app_.build().system,
        "GPU " + w.glRenderer() + ", " + std::to_string(w.drawableWidth()) + "x" + std::to_string(w.drawableHeight()),
        std::string("Video: ") + (v.active ? (v.playing ? "playing" : v.failed ? "failed" : "starting") : "idle") +
            (v.width ? ", " + std::to_string(v.width) + "x" + std::to_string(v.height) : std::string()) +
            (v.error.empty() ? std::string() : ", " + v.error),
    };
    for (const std::string& line : lines) {
      ui::label(c, line, x, y, ui::Weight::Medium, 26, white(0.9f), gfx::kLayoutWidth - 2 * x);
      y += 44;
    }
    y += 30;
    ui::label(c, "LOG", x, y, ui::Weight::Bold, 20, white(0.55f));
    y += 40;
    for (const std::string& line : log::recent(14)) {
      ui::label(c, line, x, y, ui::Weight::Regular, 19, white(0.7f), gfx::kLayoutWidth - 2 * x);
      y += 32;
    }
    ui::label(c, "Back closes", x, gfx::kLayoutHeight - 80, ui::Weight::Regular, 20, white(0.5f));
  }
};

}  // namespace

std::unique_ptr<Screen> makeDiagnostics(App& app) { return std::make_unique<DiagnosticsScreen>(app); }
const char* diagnosticsTitle() { return "Diagnostics"; }

}  // namespace marigold::app
