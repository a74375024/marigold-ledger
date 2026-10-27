// The app's face of the engine's playback session (core/engine/session):
// the platform's video player (with the local TS proxy on webOS) behind
// engine::Player, and where the picture sits (full screen or the guide
// corner). Everything the session decides (feeds, backups, watched,
// previous, the log) is the engine's.
#pragma once

#include <cstdint>

#include "app/store/store.hpp"
#include "core/engine/session.hpp"
#include "platform/gfx.hpp"

namespace marigold::app {

class App;

class Playback : public engine::Session {
 public:
  explicit Playback(App& app);

  void setFull();
  void setCorner(gfx::Rect r);

  // UI thread, every loop: the session's rules, then the window re-applies.
  // True while something is pending.
  bool tick(uint32_t ms);

 private:
  gfx::Rect window() const;
  void scheduleReapply();

  App& app_;
  gfx::Rect corner_{};
  uint32_t reapplyAt_[3] = {0, 0, 0};
  bool reapplyOnReady_ = false;
};

}  // namespace marigold::app
