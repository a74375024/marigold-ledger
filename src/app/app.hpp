// The app: owns the platform services, the data store and the screen stack,
// and runs the main loop. Drawing happens only when something changed (an
// event, a finished request, an animation, the clock), so an idle guide costs
// almost nothing.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "app/screen.hpp"
#include "core/util/time.hpp"
#include "platform/gfx.hpp"
#include "platform/http.hpp"
#include "platform/logo_cache.hpp"
#include "platform/storage.hpp"
#include "platform/text.hpp"
#include "platform/video.hpp"
#include "platform/window.hpp"

union SDL_Event;

namespace marigold::app {

class Store;
class Playback;

struct Build {
  std::string version, build;
  int webosMajor = 0;
};

class App {
 public:
  App(platform::Window& window, platform::Storage& storage, gfx::Renderer& g,
      text::TextRenderer& t, platform::Video& video, Build build);
  ~App();

  void run();
  void quit() { quit_ = true; }

  // Services for screens.
  Navigator& nav() { return nav_; }
  http::Client& pool() { return pool_; }
  logos::LogoCache& logos() { return *logos_; }
  platform::Video& video() { return video_; }
  platform::Storage& storage() { return storage_; }
  platform::Window& window() { return window_; }
  gfx::Renderer& renderer() { return g_; }
  text::TextRenderer& textRenderer() { return t_; }
  Store& store() { return *store_; }
  Playback& playback() { return *playback_; }
  const Build& build() const { return build_; }
  const time::Zone& zone() const { return zone_; }

  // A short message in a capsule (3.5 s).
  void toast(const std::string& message, bool top = false);
  // Ask for a redraw (from the UI thread).
  void invalidate() { dirty_ = true; }

  // Opens the TV keyboard on `current`; the top screen gets text().
  // Returns false when the TV has no system keyboard.
  bool openKeyboard(const std::string& current);
  void closeKeyboard();
  bool keyboardOpen() const;

  uint32_t ms() const { return ms_; }

 private:
  void handle(const SDL_Event& e);
  void dispatch(const KeyEvent& k);
  void frame();
  void suspend();
  void drawToast(ui::Ctx& c);
  void drawReminder(ui::Ctx& c);

  platform::Window& window_;
  platform::Storage& storage_;
  gfx::Renderer& g_;
  text::TextRenderer& t_;
  platform::Video& video_;
  Build build_;
  time::Zone zone_ = time::Zone::device();

  http::Client pool_;
  std::unique_ptr<logos::LogoCache> logos_;
  std::unique_ptr<Store> store_;
  std::unique_ptr<Playback> playback_;
  Navigator nav_;

  bool quit_ = false;
  bool dirty_ = true;
  uint32_t ms_ = 0;
  time::Seconds lastMinute_ = 0;
  uint32_t wakeEvent_ = 0;

  // OK is reported on release, or as a long press after NV_HOLD_MS.
  bool okDown_ = false;
  bool okLongSent_ = false;
  uint32_t okDownAt_ = 0;

  bool pointerShown_ = false;
  float pointerX_ = 0, pointerY_ = 0;
  std::vector<ui::HitRegion> hits_;

  std::string toast_;
  bool toastTop_ = false;
  uint32_t toastUntil_ = 0;
};

}  // namespace marigold::app
