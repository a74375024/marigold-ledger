// The launch mark on screen: the Marigold flower that covers opening the
// app, lights its petals as the channel list loads, and blooms out while the
// guide comes in behind it. The motion is core/engine/launch_motion; this
// draws it with the renderer's rounded fills and one soft disc texture (glow,
// halo and the Bloom-out blur), the same on every platform.
#pragma once

#include <cstdint>

#include "app/ui/ui.hpp"
#include "core/engine/launch_motion.hpp"
#include "core/engine/model.hpp"

namespace marigold::app {

class LaunchMark {
 public:
  ~LaunchMark();

  // Every frame until done(): how loading is going, and whether the guide
  // can show (its list is on screen, or there is nothing to load).
  void update(uint32_t ms, const engine::LoadProgress& p, bool reading, bool ready);

  // The mark still hides the guide completely: draw only backdrop() and
  // mark().
  bool covering(uint32_t ms) const;
  // Nothing left to draw.
  bool done(uint32_t ms) const;

  // The warm ground and halo, under the mark while it covers.
  void backdrop(ui::Ctx& c, uint32_t ms);
  // During the exit: the parts of the guide not in yet, covered (draw it
  // after the guide).
  void veil(ui::Ctx& c, uint32_t ms) const;
  // The flower, the wordmark and the status line.
  void mark(ui::Ctx& c, uint32_t ms);

 private:
  void ensureTexture();

  engine::launch::Motion motion_;
  unsigned soft_ = 0;  // a soft white disc (alpha), for glow, halo and blur
  int wordPx_ = 0;     // the wordmark's size, matched to the splash
};

}  // namespace marigold::app
