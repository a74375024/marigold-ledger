// The launch mark: the Marigold flower that covers opening the app, lights
// its petals as the channel list loads, then blooms out so the guide can
// appear behind it ("Lumen" with Unfurl's opening and the Bloom-out exit;
// the motion spec is docs/launch-motion.md).
//
// Only the motion lives here: where each circle of the flower is, how big
// and lit, and how far the guide has come in, at a given time. Drawing it is
// the app's (app/screens/launch_mark.cpp). The flower starts exactly on the
// pose of the system splash (app/splash.png): 12 outer circles, 10 inner
// ones, the core, and "Marigold" below.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "core/engine/model.hpp"

namespace marigold::engine::launch {

using Ms = uint32_t;

constexpr int kOuter = 12;  // the progress petals
constexpr int kInner = 10;
constexpr int kGuideLayers = 8;  // the bands of the guide that come in, top to bottom

// The splash's geometry, in layout units around the flower's centre.
constexpr float kCentreX = 960, kCentreY = 480;
constexpr float kOuterRing = 67, kOuterRadius = 36;
constexpr float kInnerRing = 45, kInnerRadius = 32, kInnerStartDeg = 18;
constexpr float kCoreRadius = 27;
constexpr float kWordmarkY = 640;  // top of "Marigold"
constexpr float kStatusY = 770;    // the status line under it

// One circle of the flower.
struct Disc {
  float x = 0, y = 0;  // centre
  float r = 0;
  float lit = 0;       // 0 dim .. 1 lit (outer petals); 1 for the rest
  float glow = 0;      // a soft light behind it, 0..1
};

struct Frame {
  std::array<Disc, kOuter> outer;
  std::array<Disc, kInner> inner;
  Disc core;
  float opacity = 1;   // the whole mark
  float blur = 0;      // px, Bloom-out (drawn as soft copies)
  float halo = 0;      // the warm light behind the flower, 0..1
  float warm = 0;      // how warm the background has turned, 0..1
  float wordmark = 1;  // "Marigold" opacity
  // The status line ("" for none) and its opacity / rise (px) while it
  // cross-fades.
  std::string status;
  float statusOpacity = 0, statusRise = 0;
  // How far each band of the guide has come in, 0 hidden .. 1 shown.
  std::array<float, kGuideLayers> guide{};
  bool covering = true;  // the guide is still hidden behind the mark
  bool done = false;     // nothing left to draw
};

// The line for where loading has got to; "" when no line is due. Never a
// number, never a provider.
std::string statusFor(const LoadProgress& p, Ms waiting, bool reading);

// Loading as one fraction for the petals (0..1).
float overall(const LoadProgress& p);

class Motion {
 public:
  // The first frame drawn.
  void start(Ms now);
  // Every frame while loading: how far it has got, and whether this is the
  // saved list being opened (a relaunch) rather than a download.
  void progress(Ms now, const LoadProgress& p, bool reading);
  // The guide can be shown. Call it every frame from then on (it lights the
  // remaining petals, then starts the exit).
  void ready(Ms now);
  bool started() const { return started_; }

  Frame frame(Ms now) const;

  // When the exit starts (nullopt until it's known).
  std::optional<Ms> exitAt() const { return exitAt_; }

 private:
  void light(Ms now);

  bool started_ = false;
  Ms start_ = 0;
  std::optional<Ms> readyAt_;
  std::optional<Ms> exitAt_;
  float fraction_ = 0;
  std::array<std::optional<Ms>, kOuter> litAt_{};
  Ms lastLit_ = 0;
  Ms completeGap_ = 0;  // the final wave's pace, once ready
  // The status line and the one before it (for the cross-fade).
  std::string status_, previous_;
  Ms statusAt_ = 0;
};

// Damped spring from 0 to 1 (unit mass, stiffness k, damping c), t seconds.
float spring(float t, float k, float c);
// cubic-bezier(x1, y1, x2, y2) at progress x in 0..1.
float bezier(float x, float x1, float y1, float x2, float y2);

}  // namespace marigold::engine::launch
