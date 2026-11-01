// Logos on dark surfaces: colour tuning, own boxes, discs, flags, dimming
// and edges. The stand-ins are the owner-approved mock's (simple shapes in
// the same colours); the expected colours are the mock's tuning run against
// the app's surface list (core/logos/tune.cpp, darkSurfaces()).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/logos/tune.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::logos;

namespace {

px::Image blank(int w, int h, uint32_t rgb = 0, uint8_t a = 0) {
  px::Image im;
  im.w = w;
  im.h = h;
  for (int i = 0; i < w * h; ++i)
    im.rgba.insert(im.rgba.end(), {static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb), a});
  return im;
}

uint8_t* pix(px::Image& im, int x, int y) { return &im.rgba[(static_cast<size_t>(y) * static_cast<size_t>(im.w) + static_cast<size_t>(x)) * 4]; }

uint32_t rgbAt(px::Image& im, int x, int y) {
  const uint8_t* p = pix(im, x, y);
  return static_cast<uint32_t>(p[0]) << 16 | static_cast<uint32_t>(p[1]) << 8 | p[2];
}

// Paints a shape over the image with anti-aliased edges (4 x 4 samples a
// pixel), straight alpha. `inside(x, y)` in pixel coordinates.
template <class F>
void paint(px::Image& im, uint32_t rgb, F inside) {
  const double c[3] = {static_cast<double>((rgb >> 16) & 0xff), static_cast<double>((rgb >> 8) & 0xff), static_cast<double>(rgb & 0xff)};
  for (int y = 0; y < im.h; ++y) {
    for (int x = 0; x < im.w; ++x) {
      int hits = 0;
      for (int sy = 0; sy < 4; ++sy)
        for (int sx = 0; sx < 4; ++sx) hits += inside(x + (sx + 0.5) / 4, y + (sy + 0.5) / 4);
      if (!hits) continue;
      double a = hits / 16.0;
      uint8_t* p = pix(im, x, y);
      double da = p[3] / 255.0, oa = a + da * (1 - a);
      for (int k = 0; k < 3; ++k) p[k] = static_cast<uint8_t>(std::lround((c[k] * a + p[k] * da * (1 - a)) / oa));
      p[3] = static_cast<uint8_t>(std::lround(oa * 255));
    }
  }
}

void rect(px::Image& im, uint32_t rgb, double x0, double y0, double x1, double y1) {
  paint(im, rgb, [=](double x, double y) { return x >= x0 && x < x1 && y >= y0 && y < y1; });
}

void disc(px::Image& im, uint32_t rgb, double cx, double cy, double r) {
  paint(im, rgb, [=](double x, double y) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) < r * r; });
}

// A wordmark: five letter blocks, the middle one an "O" with a hole.
void word(px::Image& im, uint32_t rgb, double x0, double y0, double h) {
  for (int i = 0; i < 5; ++i) {
    double x = x0 + i * h * 0.8;
    if (i == 2) {
      paint(im, rgb, [=](double px_, double py) {
        double cx = x + h * 0.3, cy = y0 + h * 0.5;
        double d = (px_ - cx) * (px_ - cx) / (h * 0.3 * h * 0.3) + (py - cy) * (py - cy) / (h * 0.5 * h * 0.5);
        return d < 1.0 && d > 0.35;
      });
    } else {
      rect(im, rgb, x, y0, x + h * 0.6, y0 + h);
    }
  }
}

bool near(uint32_t a, uint32_t b, int tol = 2) {
  for (int s = 0; s <= 16; s += 8) {
    if (std::abs(static_cast<int>((a >> s) & 0xff) - static_cast<int>((b >> s) & 0xff)) > tol) return false;
  }
  return true;
}

std::string hex(uint32_t c) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%06X", c & 0xFFFFFF);
  return buf;
}

double hue(uint32_t c) {
  double r = ((c >> 16) & 0xff) / 255.0, g = ((c >> 8) & 0xff) / 255.0, b = (c & 0xff) / 255.0;
  double hi = std::max({r, g, b}), lo = std::min({r, g, b}), d = hi - lo;
  if (d == 0) return 0;
  double h = hi == r ? (g - b) / d : hi == g ? 2 + (b - r) / d : 4 + (r - g) / d;
  h *= 60;
  return h < 0 ? h + 360 : h;
}

const uint32_t kChecked[] = {0x121418, 0x262A32, 0x2A2E36, 0x30353F};

struct Probe {
  int x, y;
  uint32_t before, after;
};

struct StandIn {
  const char* name;
  px::Image image;
  std::vector<Probe> probes;  // the first is the logo's dominant colour
};

// The mock's stand-ins, 320 x 110 (team logos 200 x 200).
std::vector<StandIn> standIns() {
  std::vector<StandIn> out;
  auto mark = [&](const char* name, uint32_t from, uint32_t to) {
    px::Image im = blank(320, 110);
    word(im, from, 20, 25, 60);
    out.push_back({name, im, {{25, 50, from, to}}});
  };
  mark("white wordmark", 0xFFFFFF, 0xFFFFFF);
  mark("black wordmark", 0x0A0A0A, 0xF5F5F5);
  mark("navy wordmark", 0x0B2A5B, 0x3F80E8);
  mark("red wordmark", 0xE53935, 0xE74A46);
  mark("yellow wordmark", 0xFFD400, 0xFFD400);
  mark("green wordmark", 0x2EAD4B, 0x2EAD4B);
  mark("grey wordmark", 0x8A8F98, 0x8A8F98);
  mark("orange wide wordmark", 0xF57C00, 0xF57C00);
  {
    px::Image im = blank(320, 110);
    word(im, 0x263238, 20, 30, 60);
    rect(im, 0xE53935, 250, 10, 280, 40);
    rect(im, 0x1E88E5, 285, 10, 315, 40);
    rect(im, 0xFDD835, 250, 45, 280, 75);
    rect(im, 0x43A047, 285, 45, 315, 75);
    out.push_back({"multicolour", im,
                   {{25, 60, 0x263238, 0x668797}, {265, 25, 0xE53935, 0xE74A46}, {300, 25, 0x1E88E5, 0x1E88E5},
                    {265, 60, 0xFDD835, 0xFDD835}, {300, 60, 0x43A047, 0x43A047}}});
  }
  {
    px::Image im = blank(320, 110);
    disc(im, 0xD0021B, 55, 55, 45);
    rect(im, 0xFFFFFF, 50, 25, 62, 85);  // the "1" inside the disc
    word(im, 0xFFFFFF, 115, 30, 50);
    out.push_back({"white words + red mark", im, {{30, 55, 0xD0021B, 0xFD223C}, {56, 55, 0xFFFFFF, 0xFFFFFF}, {120, 55, 0xFFFFFF, 0xFFFFFF}}});
  }
  {
    px::Image im = blank(320, 110);
    disc(im, 0x0B2A5B, 55, 55, 45);
    rect(im, 0x4FC3F7, 25, 50, 85, 62);  // the wave inside the disc
    word(im, 0xFFFFFF, 115, 30, 50);
    out.push_back({"white words + navy mark", im, {{55, 25, 0x0B2A5B, 0x3F80E8}, {55, 56, 0x4FC3F7, 0xD2EFFD}, {120, 55, 0xFFFFFF, 0xFFFFFF}}});
  }
  {
    px::Image im = blank(200, 200);
    rect(im, 0x6A1B9A, 10, 10, 190, 190);
    out.push_back({"purple square", im, {{100, 100, 0x6A1B9A, 0xB05CE2}}});
  }
  {
    px::Image im = blank(64, 64);
    rect(im, 0x1E88E5, 0, 0, 64, 64);
    rect(im, 0xFFFFFF, 16, 12, 48, 20);
    rect(im, 0xFFFFFF, 28, 20, 36, 52);
    out.push_back({"small blue square with a white T", im, {{8, 40, 0x1E88E5, 0x1E88E5}, {32, 30, 0xFFFFFF, 0xFFFFFF}}});
  }
  {
    // A navy shield with a maroon band across it, edge to edge.
    px::Image im = blank(200, 200);
    rect(im, 0x0C2340, 20, 10, 180, 190);
    rect(im, 0x8B1E2B, 0, 80, 200, 120);
    out.push_back({"team crest, navy and maroon", im, {{100, 40, 0x0C2340, 0x3D83DB}, {100, 100, 0x8B1E2B, 0xDA5564}}});
  }
  {
    px::Image im = blank(200, 200);
    paint(im, 0x13294B, [](double x, double y) {
      double d = (x - 100) * (x - 100) + (y - 100) * (y - 100);
      return d < 90.0 * 90 && d > 60.0 * 60;
    });
    disc(im, 0xFFFFFF, 100, 100, 35);  // the star, in the ring's open middle
    out.push_back({"team logo, white star on a navy ring", im, {{100, 25, 0x13294B, 0x4F83D2}, {100, 100, 0xFFFFFF, 0xFFFFFF}}});
  }
  {
    px::Image im = blank(200, 200);
    disc(im, 0xD32F2F, 100, 100, 90);
    rect(im, 0xFFC107, 0, 95, 200, 105);
    out.push_back({"team logo, red and yellow", im, {{100, 40, 0xD32F2F, 0xDB5656}, {100, 100, 0xFFC107, 0xFFC107}}});
  }
  {
    px::Image im = blank(200, 200);
    disc(im, 0xFFFFFF, 100, 100, 90);
    out.push_back({"team logo, white crest", im, {{100, 100, 0xFFFFFF, 0xFFFFFF}}});
  }
  return out;
}

}  // namespace

TEST_CASE("tuneColour: the mock's colours, and colours that pass stay") {
  CHECK(tuneColour(0x0B2A5B) == 0x3F80E8);
  CHECK(tuneColour(0x0A0A0A) == 0xF5F5F5);
  CHECK(tuneColour(0x000000) == 0xF5F5F5);
  CHECK(tuneColour(0x6A1B9A) == 0xB05CE2);
  CHECK(tuneColour(0x263238) == 0x668797);
  CHECK(tuneColour(0x0C2340) == 0x3D83DB);
  CHECK(tuneColour(0x8B1E2B) == 0xDA5564);
  CHECK(tuneColour(0x3A3D44) == 0xBBBEC5);
  CHECK(tuneColour(0x0000FF) == 0x7171FF);
  for (uint32_t c : {0xFFFFFFu, 0xFFD400u, 0x2EAD4Bu, 0x8A8F98u, 0xF57C00u, 0x1E88E5u, 0xFFB300u, 0x00FF00u, 0xB0B0B0u})
    CHECK(tuneColour(c) == c);
  // Against one part (a detail on it), darker as well as lighter.
  CHECK(tuneColour(0x4FC3F7, {0x3F80E8}, true) == 0xD2EFFD);
  uint32_t darker = tuneColour(0x9AA0A8, {0xFFFFFF}, false);
  CHECK(contrast(darker, 0xFFFFFF) >= kTarget);
  CHECK(luminance(darker) < luminance(0x9AA0A8));
}

TEST_CASE("tuneColour keeps the hue and reaches 3:1 on every dark surface") {
  for (uint32_t c : {0x0B2A5Bu, 0x6A1B9Au, 0x0C2340u, 0x8B1E2Bu, 0x13294Bu, 0xD32F2Fu, 0xD0021Bu, 0x263238u, 0x1A237Eu, 0x004D40u}) {
    uint32_t t = tuneColour(c);
    INFO(hex(c) << " -> " << hex(t));
    double dh = std::fabs(hue(c) - hue(t));
    CHECK(std::min(dh, 360 - dh) <= 3.0);
    for (uint32_t s : darkSurfaces()) CHECK(contrast(t, s) >= 3.0);
    CHECK(luminance(t) > luminance(c));
  }
}

TEST_CASE("tuneImage: every stand-in matches the mock, and stands out on the dark surfaces") {
  for (StandIn& s : standIns()) {
    INFO(s.name);
    for (const Probe& p : s.probes) REQUIRE(near(rgbAt(s.image, p.x, p.y), p.before, 0));
    bool changed = false;
    for (const Probe& p : s.probes) changed = changed || p.before != p.after;
    CHECK(tuneImage(s.image) == changed);
    for (const Probe& p : s.probes) {
      uint32_t got = rgbAt(s.image, p.x, p.y);
      INFO(hex(p.before) << " -> " << hex(got) << ", the mock: " << hex(p.after));
      CHECK(near(got, p.after));
      if (p.before == p.after) CHECK(got == p.before);  // colours that pass are untouched
    }
    uint32_t dominant = rgbAt(s.image, s.probes[0].x, s.probes[0].y);
    for (uint32_t surface : kChecked) CHECK(contrast(dominant, surface) >= 3.0);
    // Dimmed, it keeps its brightness: still 3:1.
    for (uint32_t surface : kChecked) CHECK(contrast(desaturate(dominant), surface) >= 3.0);
  }
}

TEST_CASE("tuneImage: a dark detail on a light part keeps its contrast with that part") {
  // Black words, and a white disc with a black letter in it: the words turn
  // light on the cell; the letter stays dark on its disc.
  px::Image im = blank(320, 110);
  word(im, 0x0A0A0A, 120, 30, 50);
  disc(im, 0xFFFFFF, 55, 55, 45);
  rect(im, 0x0A0A0A, 45, 25, 65, 85);
  CHECK(tuneImage(im));
  CHECK(near(rgbAt(im, 125, 55), 0xF5F5F5));
  CHECK(rgbAt(im, 55, 55) == 0x0A0A0A);
  CHECK(rgbAt(im, 30, 55) == 0xFFFFFF);
}

TEST_CASE("tuneImage: a black disc with white words in it is not turned into a white patch") {
  px::Image im = blank(200, 200);
  disc(im, 0x0A0A0A, 100, 100, 95);
  word(im, 0xFFFFFF, 30, 75, 50);
  CHECK_FALSE(tuneImage(im));
  CHECK(rgbAt(im, 100, 20) == 0x0A0A0A);
  CHECK(rgbAt(im, 35, 100) == 0xFFFFFF);
  // A black disc with only a small detail is the mark itself: it turns light.
  px::Image dot = blank(200, 200);
  disc(dot, 0x0A0A0A, 100, 100, 95);
  disc(dot, 0xFFFFFF, 100, 100, 10);
  CHECK(tuneImage(dot));
  CHECK(near(rgbAt(dot, 100, 20), 0xF5F5F5));
  CHECK(contrast(rgbAt(dot, 100, 100), 0xF5F5F5) >= 3.0);  // the dot turned dark on it
}

TEST_CASE("tuneImage: edges between parts and towards clear pixels follow the new colours") {
  px::Image im = blank(320, 110);
  word(im, 0x0B2A5B, 20, 25, 60);
  tuneImage(im);
  // Every pixel of the word, its soft edge included, is the new navy.
  for (int y = 0; y < im.h; ++y)
    for (int x = 0; x < im.w; ++x)
      if (pix(im, x, y)[3] > 0) CHECK(rgbAt(im, x, y) == 0x3F80E8);
}

TEST_CASE("removeOwnBox: plain white and black boxes go, anything else stays") {
  SUBCASE("a white box with a navy word") {
    px::Image im = blank(320, 180, 0xFFFFFF, 255);
    word(im, 0x0B2A5B, 60, 60, 60);
    CHECK(removeOwnBox(im));
    CHECK(pix(im, 0, 0)[3] == 0);
    CHECK(pix(im, 160, 10)[3] == 0);
    CHECK(pix(im, 60 + 2 * 48 + 18, 90)[3] == 0);  // inside the "O"
    CHECK(pix(im, 70, 90)[3] == 255);
    CHECK(rgbAt(im, 70, 90) == 0x0B2A5B);
    // Then tuned as any logo.
    Treatment t;
    {
      px::Image again = blank(320, 180, 0xFFFFFF, 255);
      word(again, 0x0B2A5B, 60, 60, 60);
      t = prepare(again, Art::logo);
      CHECK(again.w < 320);  // the clear margin trimmed
      CHECK(near(rgbAt(again, 10, 30), 0x3F80E8));
    }
    CHECK(t.boxRemoved);
    CHECK(t.tuned);
  }
  SUBCASE("a near-black box with white words and an amber moon") {
    px::Image im = blank(320, 180, 0x0E0F12, 255);
    word(im, 0xFFFFFF, 40.5, 110, 40);
    disc(im, 0xFFB300, 160, 55, 30);
    disc(im, 0x0E0F12, 175, 45, 26);  // the moon's crescent
    CHECK(removeOwnBox(im));
    CHECK(pix(im, 5, 5)[3] == 0);
    CHECK(pix(im, 175, 45)[3] == 0);
    CHECK(rgbAt(im, 45, 130) == 0xFFFFFF);
    CHECK(pix(im, 45, 130)[3] == 255);
    CHECK(rgbAt(im, 140, 60) == 0xFFB300);
    // Soft edges keep the mark's colour, not the box's.
    bool soft = false;
    for (int x = 30; x < 50; ++x) {
      const uint8_t* p = pix(im, x, 130);
      if (p[3] > 0 && p[3] < 255) {
        soft = true;
        CHECK(p[0] > 200);
      }
    }
    CHECK(soft);
  }
  SUBCASE("a border that is not one colour") {
    px::Image im = blank(320, 180, 0xFFFFFF, 255);
    rect(im, 0x808080, 160, 0, 320, 180);
    word(im, 0x0B2A5B, 60, 60, 60);
    px::Image before = im;
    CHECK_FALSE(removeOwnBox(im));
    CHECK(im.rgba == before.rgba);
  }
  SUBCASE("a coloured box is the logo itself") {
    px::Image im = blank(64, 64, 0x1E88E5, 255);
    rect(im, 0xFFFFFF, 16, 12, 48, 20);
    CHECK_FALSE(removeOwnBox(im));
  }
  SUBCASE("a round logo on its own disc") {
    px::Image im = blank(200, 200);
    disc(im, 0xFFFFFF, 100, 100, 99);
    rect(im, 0x0B2A5B, 80, 50, 120, 150);
    px::Image before = im;
    CHECK_FALSE(removeOwnBox(im));
    CHECK(im.rgba == before.rgba);
  }
  SUBCASE("a blank white tile") {
    px::Image im = blank(100, 60, 0xFFFFFF, 255);
    CHECK_FALSE(removeOwnBox(im));
    CHECK(pix(im, 50, 30)[3] == 255);
  }
}

TEST_CASE("prepare: flags and photos are never recoloured") {
  px::Image flag = blank(64, 42, 0x0D1B3E, 255);
  rect(flag, 0xFFFFFF, 0, 18, 64, 24);
  px::Image before = flag;
  Treatment t = prepare(flag, Art::flag);
  CHECK_FALSE(t.tuned);
  CHECK_FALSE(t.boxRemoved);
  CHECK(flag.rgba == before.rgba);
  px::Image photo = blank(40, 40, 0xFFFFFF, 255);
  disc(photo, 0x101010, 20, 20, 10);
  before = photo;
  t = prepare(photo, Art::photo);
  CHECK(photo.rgba == before.rgba);
  CHECK(mainColour(flag) == 0x0D1B3E);
}

TEST_CASE("desaturate takes the colour out and keeps the brightness") {
  CHECK(near(desaturate(0x3F80E8), 0x7381A9));
  CHECK(near(desaturate(0xE74A46), 0xA97574));
  CHECK(near(desaturate(0xB05CE2), 0x9278A7));
  CHECK(desaturate(0xFFFFFF) == 0xFFFFFF);
  CHECK(desaturate(0x3F80E8, 1.0) == 0x3F80E8);
  for (uint32_t c : {0x3F80E8u, 0xE74A46u, 0xB05CE2u, 0x668797u}) {
    INFO(hex(c));
    CHECK(std::fabs(luminance(desaturate(c)) - luminance(c)) < 0.01);
  }
}

TEST_CASE("disc: the fill keeps the team colour; ring and letters stand out") {
  Disc d = disc(0x1C1C1C, 0x333333);  // dark fill, dark ring
  CHECK(d.fill == 0x1C1C1C);
  CHECK(d.ring == 0xCCCCCC);
  CHECK(d.letters == 0xFFFFFF);
  d = disc(0xF2C94C, 0x1B2A4A);  // a light fill shows by itself: its ring stays
  CHECK(d.ring == 0x1B2A4A);
  CHECK(d.letters == 0x000000);
  d = disc(0x1A237E, 0xFFFFFF);
  CHECK(d.ring == 0xFFFFFF);
  CHECK(d.letters == 0xFFFFFF);
  d = disc(0xE07A1F, 0xFFFFFF);  // orange: black letters read better than white
  CHECK(d.letters == 0x000000);
  d = disc(0x3A3D44, 0xFFFFFF);
  CHECK(d.ring == 0xFFFFFF);
  d = disc(0x3A3D44, 0x3A3D44);
  for (uint32_t s : darkSurfaces()) CHECK(contrast(d.ring, s) >= 3.0);
}

TEST_CASE("flags: real aspect ratio, and a hairline only where the field blends in") {
  // A square flag image with clear bands above and below, trimmed by the
  // decode: drawn at 3:2, not squashed into a square.
  px::Image im = blank(64, 64);
  rect(im, 0xC62828, 0, 11, 32, 53);
  rect(im, 0x2E7D32, 32, 11, 64, 53);
  im = px::trimTransparent(im);
  float w = 0, h = 0;
  flagSize(im.w, im.h, 38, w, h);
  CHECK(w == doctest::Approx(38));
  CHECK(h == doctest::Approx(38.0 * 42 / 64));
  flagSize(42, 64, 38, w, h);
  CHECK(h == doctest::Approx(38));
  CHECK(w < 38);
  CHECK(flagHairline(0x0D1B3E, 0x27292D));
  CHECK(flagHairline(0xC62828, 0x27292D));
  CHECK_FALSE(flagHairline(0xC62828, 0x15171C));
  CHECK_FALSE(flagHairline(0xFFFFFF, 0x27292D));
  CHECK_FALSE(flagHairline(0xFFFFFF, kFocusFill));
}

TEST_CASE("edges: no clear texel next to a visible one stays black") {
  px::Image im = blank(120, 60);
  word(im, 0xF57C00, 10, 10, 40);
  prepare(im, Art::logo);
  for (int y = 0; y < im.h; ++y) {
    for (int x = 0; x < im.w; ++x) {
      if (pix(im, x, y)[3] != 0) continue;
      bool byVisible = false;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          int nx = x + dx, ny = y + dy;
          if (nx >= 0 && ny >= 0 && nx < im.w && ny < im.h && pix(im, nx, ny)[3] == 255) byVisible = true;
        }
      if (byVisible) CHECK(rgbAt(im, x, y) != 0);
    }
  }
}

TEST_CASE("smallCopy: thin logos get a small copy with unbroken lines") {
  px::Image thin = blank(320, 84);
  for (int i = 0; i < 6; ++i) rect(thin, 0xFFFFFF, 10 + i * 50, 10, 13 + i * 50, 74);  // 3 px strokes
  rect(thin, 0xFFFFFF, 10, 10, 300, 13);
  px::Image small = smallCopy(thin);
  REQUIRE_FALSE(small.empty());
  CHECK(small.w <= kSmallW);
  CHECK(small.h <= kSmallH);
  CHECK(px::strokeWidth(small) >= 1.5);
  px::Image bold = blank(320, 84);
  rect(bold, 0xFFFFFF, 10, 10, 300, 74);
  CHECK(smallCopy(bold).empty());
}
