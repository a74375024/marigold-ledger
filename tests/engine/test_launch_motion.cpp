// The launch mark's motion: the splash pose, light as progress, Bloom-out.
#include <doctest.h>

#include <cctype>
#include <cmath>

#include "core/engine/launch_motion.hpp"

using namespace marigold::engine;
using namespace marigold::engine::launch;

namespace {

int litPetals(const Motion& m, Ms now) {
  Frame f = m.frame(now);
  int n = 0;
  for (const Disc& d : f.outer) n += d.lit > 0.99f ? 1 : 0;
  return n;
}

LoadProgress at(LoadProgress::Stage s, float within = 0, size_t channels = 0) {
  LoadProgress p;
  p.stage = s;
  p.within = within;
  p.channels = channels;
  return p;
}

}  // namespace

TEST_CASE("launch: curves") {
  CHECK(spring(0, 210, 17) == doctest::Approx(0));
  CHECK(spring(3, 210, 17) == doctest::Approx(1).epsilon(0.001));
  // Underdamped: it overshoots before it settles.
  float peak = 0;
  for (float t = 0; t < 1; t += 0.005f) peak = std::max(peak, spring(t, 210, 17));
  CHECK(peak > 1.0f);
  CHECK(bezier(0, 0.4f, 0, 0.2f, 1) == doctest::Approx(0).epsilon(0.01));
  CHECK(bezier(1, 0.4f, 0, 0.2f, 1) == doctest::Approx(1).epsilon(0.01));
  CHECK(bezier(0.5f, 0.2f, 0.8f, 0.2f, 1) > 0.5f);  // eases out
}

TEST_CASE("launch: the first frame is the splash's pose") {
  Motion m;
  m.start(1000);
  Frame f = m.frame(1000);
  CHECK(f.outer[0].x == doctest::Approx(kCentreX));
  CHECK(f.outer[0].y == doctest::Approx(kCentreY - kOuterRing));
  CHECK(f.outer[0].r == doctest::Approx(kOuterRadius));
  CHECK(f.outer[3].x == doctest::Approx(kCentreX + kOuterRing).epsilon(0.001));
  CHECK(f.core.r == doctest::Approx(kCoreRadius));
  CHECK(f.inner[0].r == doctest::Approx(kInnerRadius));
  CHECK(f.opacity == doctest::Approx(1));
  CHECK(f.wordmark == doctest::Approx(1));
  CHECK(f.covering);
  CHECK_FALSE(f.done);
  CHECK(f.status.empty());
}

TEST_CASE("launch: a quick relaunch sweeps the light, then blooms out with the guide behind") {
  Motion m;
  m.start(0);
  for (Ms t = 0; t < 1200; t += 16) m.progress(t, at(LoadProgress::Stage::Download), true);
  CHECK(litPetals(m, 1200) == 0);
  m.ready(1200);
  for (Ms t = 1200; t < 1600; t += 16) m.progress(t, at(LoadProgress::Stage::Done), true), m.ready(t);
  // ready() lights the rest within ~300 ms; the exit follows.
  Motion n;
  n.start(0);
  n.ready(1200);
  Ms t = 1200;
  for (; t < 1600 && !n.exitAt(); t += 16) n.ready(t), n.progress(t, at(LoadProgress::Stage::Done), true);
  REQUIRE(n.exitAt());
  CHECK(*n.exitAt() >= 1150);
  CHECK(*n.exitAt() <= 1200 + 300 + 100);
  CHECK(litPetals(n, *n.exitAt() + 200) == kOuter);  // the flash fills any still springing
  // Bloom-out: the mark grows, blurs and fades; the guide comes in, top first.
  Frame mid = n.frame(*n.exitAt() + 350);
  CHECK(mid.opacity < 1);
  CHECK(mid.blur > 0);
  CHECK(mid.outer[0].r > kOuterRadius);
  CHECK(mid.guide[0] > mid.guide[7]);
  Frame end = n.frame(*n.exitAt() + 900);
  CHECK(end.done);
  CHECK(end.opacity == doctest::Approx(0).epsilon(0.01));
  for (float g : end.guide) CHECK(g == doctest::Approx(1).epsilon(0.01));
}

TEST_CASE("launch: the opening is never cut short, even when the guide is ready at once") {
  Motion m;
  m.start(0);
  m.ready(0);
  for (Ms t = 0; t < 2000 && !m.exitAt(); t += 16) m.ready(t), m.progress(t, at(LoadProgress::Stage::Done), true);
  // Petals light only once their own unfurl has begun: the wave sets the pace.
  REQUIRE(m.exitAt());
  CHECK(*m.exitAt() >= 1150);
}

TEST_CASE("launch: on a long first setup the petals light only as the loading earns them") {
  Motion m;
  m.start(0);
  Ms t = 0;
  auto run = [&](Ms until, LoadProgress p) {
    for (; t < until; t += 16) m.progress(t, p, false);
  };
  run(3000, at(LoadProgress::Stage::Download, 0, 500));
  CHECK(litPetals(m, t) <= 2);  // 15% of 12 petals
  run(6000, at(LoadProgress::Stage::Recognize, 0.5f));
  int mid = litPetals(m, t);
  CHECK(mid <= static_cast<int>(std::floor(overall(at(LoadProgress::Stage::Recognize, 0.5f)) * kOuter)));
  CHECK(mid >= 4);
  run(9000, at(LoadProgress::Stage::Games, 0.5f));
  CHECK(litPetals(m, t) > mid);
  CHECK(litPetals(m, t) < kOuter);
  CHECK_FALSE(m.exitAt());
  // Waiting, it says what's happening, never how many.
  Frame f = m.frame(t);
  CHECK(f.status == "FINDING THIS WEEK\xe2\x80\x99S GAMES");
  m.ready(t);
  for (Ms end = t + 400; t < end; t += 16) m.ready(t);
  CHECK(m.exitAt());
}

TEST_CASE("launch: status lines wait for a slow start and carry no numbers") {
  CHECK(statusFor(at(LoadProgress::Stage::Download, 0, 48213), 1000, false).empty());
  CHECK(statusFor(at(LoadProgress::Stage::Download), 2000, true) == "OPENING YOUR CHANNELS");
  CHECK(statusFor(at(LoadProgress::Stage::Download, 0, 48213), 2000, false) == "GETTING YOUR CHANNELS");
  CHECK(statusFor(at(LoadProgress::Stage::Download, 0, 48213), 25000, false) == "STILL GETTING YOUR CHANNELS");
  CHECK(statusFor(at(LoadProgress::Stage::Recognize, 0.4f), 5000, false) == "SORTING YOUR LINEUP");
  CHECK(statusFor(at(LoadProgress::Stage::Done), 5000, false) == "ALMOST READY");
  for (auto s : {LoadProgress::Stage::Download, LoadProgress::Stage::Recognize, LoadProgress::Stage::Games,
                 LoadProgress::Stage::Done})
    for (const char ch : statusFor(at(s, 0.5f, 48213), 30000, false)) CHECK_FALSE(std::isdigit(static_cast<unsigned char>(ch)));
}
