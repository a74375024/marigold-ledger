#include "core/engine/launch_motion.hpp"

#include <algorithm>
#include <cmath>

namespace marigold::engine::launch {
namespace {

constexpr float kPi = 3.14159265f;

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }
float rad(float deg) { return deg * kPi / 180.0f; }

// The opening (ms from the first frame).
constexpr Ms kInhaleMs = 180;
constexpr float kWaveSpanMs = 460;  // the unfurl wave's spread
constexpr Ms kIntroEndMs = 1150;    // the opening never ends sooner
constexpr Ms kWaitingFromMs = 1500; // then the breathing loop, and lines
constexpr Ms kLightGapMs = 45;      // at most one petal lit this often
constexpr Ms kCompleteMs = 300;     // ready: the rest light within this

// Bloom-out (ms from the exit's start).
constexpr Ms kGuideFromMs = 200, kGuideStaggerMs = 50, kGuideFadeMs = 300;
constexpr Ms kExitMs = 700;

// When petal i of n starts to open: an accelerating wave round the ring.
float waveDelay(int i, int n, float from, float span) {
  return from + span * std::pow(static_cast<float>(i) / static_cast<float>(n), 0.62f);
}

}  // namespace

float spring(float t, float k, float c) {
  if (t <= 0) return 0;
  float w = std::sqrt(k);
  float z = c / (2 * w);
  if (z >= 1) {
    // Critically or over-damped: no overshoot.
    return 1 - (1 + w * t) * std::exp(-w * t);
  }
  float wd = w * std::sqrt(1 - z * z);
  return 1 - std::exp(-z * w * t) * (std::cos(wd * t) + z / std::sqrt(1 - z * z) * std::sin(wd * t));
}

float bezier(float x, float x1, float y1, float x2, float y2) {
  x = clamp01(x);
  // Solve for the curve parameter at x (Newton, then bisection), then y.
  auto bx = [&](float t) { return 3 * (1 - t) * (1 - t) * t * x1 + 3 * (1 - t) * t * t * x2 + t * t * t; };
  auto by = [&](float t) { return 3 * (1 - t) * (1 - t) * t * y1 + 3 * (1 - t) * t * t * y2 + t * t * t; };
  float lo = 0, hi = 1, t = x;
  for (int i = 0; i < 20; ++i) {
    float v = bx(t);
    if (std::fabs(v - x) < 1e-4f) break;
    if (v < x) lo = t;
    else hi = t;
    t = (lo + hi) / 2;
  }
  return by(t);
}

float overall(const LoadProgress& p) {
  switch (p.stage) {
    case LoadProgress::Stage::Download:
      return p.channels > 0 ? 0.15f : 0.0f;
    case LoadProgress::Stage::Recognize:
      return 0.2f + 0.6f * clamp01(p.within);
    case LoadProgress::Stage::Games:
      return 0.85f + 0.1f * clamp01(p.within);
    case LoadProgress::Stage::Done:
      return 1.0f;
  }
  return 0;
}

std::string statusFor(const LoadProgress& p, Ms waiting, bool reading) {
  if (waiting < kWaitingFromMs) return {};
  if (reading) return "OPENING YOUR CHANNELS";
  switch (p.stage) {
    case LoadProgress::Stage::Download:
      return waiting > 20000 ? "STILL GETTING YOUR CHANNELS" : "GETTING YOUR CHANNELS";
    case LoadProgress::Stage::Recognize:
      return "SORTING YOUR LINEUP";
    case LoadProgress::Stage::Games:
      return "FINDING THIS WEEK\xe2\x80\x99S GAMES";
    case LoadProgress::Stage::Done:
      return "ALMOST READY";
  }
  return {};
}

void Motion::start(Ms now) {
  *this = Motion();
  started_ = true;
  start_ = now;
}

void Motion::progress(Ms now, const LoadProgress& p, bool reading) {
  if (!started_) return;
  if (readyAt_) {
    light(now);  // the rest of the petals, then the exit
    return;
  }
  fraction_ = std::max(fraction_, overall(p));
  light(now);
  std::string line = statusFor(p, now - start_, reading);
  if (line != status_) {
    previous_ = status_;
    status_ = line;
    statusAt_ = now;
  }
}

void Motion::ready(Ms now) {
  if (!started_) return;
  if (readyAt_) {
    light(now);
    return;
  }
  readyAt_ = now;
  fraction_ = 1;
  light(now);
}

void Motion::light(Ms now) {
  int lit = 0;
  for (const auto& at : litAt_) lit += at ? 1 : 0;
  int target = readyAt_ ? kOuter : static_cast<int>(std::floor(fraction_ * kOuter + 1e-4f));
  // Once ready, the rest light in a quick wave that fits kCompleteMs (the
  // pace is set when it became ready).
  if (readyAt_ && !completeGap_)
    completeGap_ = lit < kOuter ? std::min<Ms>(kLightGapMs, kCompleteMs / static_cast<Ms>(kOuter - lit)) : 1;
  Ms gap = readyAt_ ? completeGap_ : kLightGapMs;
  for (int i = 0; i < kOuter && lit < target; ++i) {
    if (litAt_[i]) continue;
    // Never before its own petal has started to open, and one at a time.
    Ms opens = start_ + static_cast<Ms>(waveDelay(i, kOuter, kInhaleMs, kWaveSpanMs)) + 80;
    if (now < opens) break;
    if (lastLit_ && now < lastLit_ + gap) break;
    litAt_[i] = now;
    // Paced by `gap`, not by frames: a slow frame lights two.
    lastLit_ = lastLit_ && now - lastLit_ < 2 * gap ? lastLit_ + gap : now;
    ++lit;
  }
  if (lit == kOuter && !exitAt_) exitAt_ = std::max<Ms>(now + 60, start_ + kIntroEndMs);
}

Frame Motion::frame(Ms now) const {
  Frame f;
  float t = static_cast<float>(now - start_);  // ms
  float ts = t / 1000.0f;
  float inhale = bezier(t / kInhaleMs, 0.45f, 0, 0.55f, 1);
  float waiting = t > kWaitingFromMs ? (t - kWaitingFromMs) / 1000.0f : 0.0f;
  float breath = 1 + 0.01f * std::sin(2 * kPi * waiting / 4.0f);

  // The exit, Bloom-out.
  float e = exitAt_ && now >= *exitAt_ ? static_cast<float>(now - *exitAt_) : -1.0f;
  float flash = e >= 0 ? bezier(e / 150, 0.2f, 0.8f, 0.2f, 1) : 0.0f;
  float release = e >= 0 ? bezier((e - 100) / 500, 0.4f, 0, 0.2f, 1) : 0.0f;
  float scale = breath * (1 + 0.12f * release);

  // Rings and core.
  float outerTurn = 18 * spring((t - kInhaleMs) / 1000.0f, 90, 14);
  float innerTurn = -10 * spring((t - 260) / 1000.0f, 140, 16) - 1.5f * waiting;
  float core = t < 380 ? 1 - 0.1f * inhale : 0.9f + 0.1f * spring((t - 380) / 1000.0f, 260, 16);

  // Unearned petals dim, so the lit ones read as progress.
  float dimmed = 1 - 0.55f * bezier((t - kInhaleMs) / 420.0f, 0.4f, 0, 0.2f, 1);
  int litCount = 0;
  for (const auto& at : litAt_) litCount += at ? 1 : 0;
  float share = fraction_ * kOuter - std::floor(fraction_ * kOuter);

  auto petal = [&](int i, int n, float ring, float radius, float startDeg, float turn, float wave, bool progress) {
    Disc d;
    float delay = waveDelay(i, n, kInhaleMs + wave, kWaveSpanMs);
    float open = t >= delay ? spring((t - delay) / 1000.0f, 210, 17) : 0.0f;
    float tuck = inhale * (1 - open);
    float angle = startDeg + 360.0f * static_cast<float>(i) / static_cast<float>(n) + turn - 10 * tuck;
    float radial = ring - 8 * tuck;
    float r = radius * (1 - 0.14f * tuck);
    // The light rides the unfurl wave.
    d.glow = t >= delay ? 0.12f * std::max(0.0f, 1 - (t - delay) / 350.0f) : 0.0f;
    if (progress) {
      float lit = 0;
      if (litAt_[i]) {
        float since = static_cast<float>(now - *litAt_[i]) / 1000.0f;
        lit = std::min(1.0f, spring(since, 200, 16));
        r *= 1 + std::exp(-6 * since) * std::sin(10 * since) * 0.08f;  // the pop as it lights
      } else if (i == litCount && !readyAt_) {
        // The next petal fills with its share, with a slow shimmer.
        lit = share * 0.6f * (1 + 0.06f * std::sin(2 * kPi * 0.5f * ts));
      }
      lit = std::max(lit, flash);
      d.lit = dimmed + (1 - dimmed) * lit;
      d.glow = std::max(d.glow, 0.25f * lit);
    } else {
      d.lit = 1;
    }
    // Bloom-out: an accelerating release outward.
    if (e >= 0 && progress) {
      float go = spring((e - waveDelay(i, n, 100, 200)) / 1000.0f, 140, 18);
      radial += 24 * go;
      r *= 1 + 0.1f * go;
    } else if (e >= 0) {
      radial += 12 * release;
    }
    d.x = kCentreX + std::cos(rad(angle - 90)) * radial * scale;
    d.y = kCentreY + std::sin(rad(angle - 90)) * radial * scale;
    d.r = r * scale;
    return d;
  };
  for (int i = 0; i < kOuter; ++i) f.outer[i] = petal(i, kOuter, kOuterRing, kOuterRadius, 0, outerTurn, 0, true);
  for (int j = 0; j < kInner; ++j)
    f.inner[j] = petal(j, kInner, kInnerRing, kInnerRadius, kInnerStartDeg, innerTurn, 40, false);
  f.core.x = kCentreX;
  f.core.y = kCentreY;
  f.core.r = kCoreRadius * core * scale;
  f.core.lit = 1;

  // Light and ground.
  float halo = t < kInhaleMs ? 0.18f - 0.08f * inhale : 0.10f + 0.30f * fraction_;
  if (e >= 0) {
    float flare = e < 200 ? halo + (0.5f - halo) * bezier(e / 200, 0.2f, 0.8f, 0.2f, 1)
                          : 0.5f * (1 - bezier((e - 200) / 500, 0.4f, 0, 0.2f, 1));
    halo = flare;
  }
  f.halo = halo;
  f.warm = bezier((t - 200) / 900.0f, 0.2f, 0.7f, 0.2f, 1) * (0.4f + 0.6f * fraction_);
  f.blur = 14 * release;
  f.opacity = 1 - release;
  f.wordmark = f.opacity;
  if (e >= 0) f.warm *= f.opacity;

  // The status line: the old one out (250 ms, rising), then the new one in.
  if (!status_.empty() || !previous_.empty()) {
    float since = static_cast<float>(now - statusAt_);
    if (since < 250 && !previous_.empty()) {
      f.status = previous_;
      f.statusOpacity = 1 - since / 250;
      f.statusRise = 6 * since / 250;
    } else if (!status_.empty()) {
      f.status = status_;
      f.statusOpacity = bezier((since - (previous_.empty() ? 0 : 250)) / 450, 0.2f, 0.8f, 0.2f, 1);
    }
    f.statusOpacity *= f.opacity;
  }

  // The guide comes in behind, band by band from the top.
  for (int k = 0; k < kGuideLayers; ++k) {
    float from = static_cast<float>(kGuideFromMs + kGuideStaggerMs * k);
    f.guide[k] = e >= 0 ? bezier((e - from) / kGuideFadeMs, 0.2f, 0.8f, 0.2f, 1) : 0.0f;
  }
  f.covering = e < static_cast<float>(kGuideFromMs);
  f.done = e >= static_cast<float>(std::max<Ms>(kExitMs, kGuideFromMs + kGuideStaggerMs * (kGuideLayers - 1) +
                                                             kGuideFadeMs));
  return f;
}

}  // namespace marigold::engine::launch
