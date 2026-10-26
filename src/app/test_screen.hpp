// M0 test screen: proves on a real TV that drawing, every remote key, the
// Magic Remote pointer, the LG keyboard and hardware video all work.
#pragma once

#include <SDL2/SDL.h>

#include <array>
#include <deque>
#include <string>

#include "core/input/keys.hpp"
#include "platform/gfx.hpp"
#include "platform/storage.hpp"
#include "platform/text.hpp"
#include "platform/video.hpp"

namespace marigold::app {

struct SystemInfo {
  std::string version;
  std::string build;
  int webosMajor = 0;
  std::string sdlRuntime;
  std::string glRenderer;
  int drawableW = 0, drawableH = 0;
  bool transparentSurface = false;
};

class TestScreen {
 public:
  TestScreen(gfx::Renderer& g, text::TextRenderer& t, platform::Video& v, platform::Storage& s,
             SystemInfo info);

  void handle(const SDL_Event& e, uint32_t nowMs);
  void frame(uint32_t nowMs);
  bool wantsQuit() const { return quit_; }
  // The app went to the background: release the decoder.
  void suspend();

 private:
  enum class Button { EnterLink, PlayFull, PlayCorner, LoadMode, Stop, Count };
  enum class VideoView { None, Full, Corner };
  struct KeyLine {
    std::string text;
    uint32_t at = 0;
  };

  void onKey(const SDL_KeyboardEvent& k, uint32_t nowMs);
  void activate(Button b, uint32_t nowMs);
  void startPlayback(VideoView view, uint32_t nowMs);
  void stopPlayback();
  void toast(const std::string& text, uint32_t nowMs);
  std::string linkSummary() const;

  void drawHeader();
  void drawKeys(uint32_t nowMs);
  void drawPlayback(const platform::VideoState& vs, uint32_t nowMs);
  void drawLog();
  void drawFullScreenOverlay(const platform::VideoState& vs, uint32_t nowMs);
  void section(float x, float y, const char* title);

  gfx::Renderer& g_;
  text::TextRenderer& t_;
  platform::Video& video_;
  platform::Storage& storage_;
  SystemInfo info_;

  std::array<bool, 64> seen_{};
  Key held_ = Key::Unknown;
  std::deque<KeyLine> lastKeys_;
  bool pointerVisible_ = false;
  int pointerX_ = 0, pointerY_ = 0, clicks_ = 0, wheelUp_ = 0, wheelDown_ = 0;

  int focus_ = 0;
  std::string link_;
  bool keyboardOpen_ = false;
  int loadMode_ = 0;
  VideoView view_ = VideoView::None;
  uint32_t playStartedMs_ = 0;
  uint32_t firstFrameMs_ = 0;
  uint32_t backPressedMs_ = 0;
  std::string toast_;
  uint32_t toastUntil_ = 0;
  bool quit_ = false;
};

}  // namespace marigold::app
