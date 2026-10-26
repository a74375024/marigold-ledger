#include "core/util/pixels.hpp"
#include "doctest.h"

using namespace marigold::px;

namespace {
Image solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  Image im;
  im.w = w;
  im.h = h;
  for (int i = 0; i < w * h; ++i) im.rgba.insert(im.rgba.end(), {r, g, b, a});
  return im;
}
}  // namespace

TEST_CASE("fitWithin keeps the aspect ratio and never upscales") {
  int w = 0, h = 0;
  fitWithin(400, 200, 200, 200, w, h);
  CHECK(w == 200);
  CHECK(h == 100);
  fitWithin(100, 50, 200, 200, w, h);
  CHECK(w == 100);
  CHECK(h == 50);
  fitWithin(1000, 10, 100, 100, w, h);
  CHECK(w == 100);
  CHECK(h == 1);
}

TEST_CASE("shrinkToFit averages and ignores transparent colour") {
  // Left half opaque white, right half fully transparent black.
  Image im;
  im.w = 4;
  im.h = 2;
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 4; ++x) {
      if (x < 2) im.rgba.insert(im.rgba.end(), {255, 255, 255, 255});
      else im.rgba.insert(im.rgba.end(), {0, 0, 0, 0});
    }
  Image out = shrinkToFit(im, 1, 1);
  REQUIRE(out.w == 1);
  REQUIRE(out.h == 1);
  CHECK(out.rgba[0] == 255);  // no dark fringe
  CHECK(out.rgba[3] == 128);  // half covered
}

TEST_CASE("shrinkToFit handles non-integer ratios") {
  Image out = shrinkToFit(solid(7, 5, 10, 20, 30, 255), 3, 3);
  CHECK(out.w == 3);
  CHECK(out.h == 2);
  for (size_t i = 0; i < out.rgba.size(); i += 4) {
    CHECK(out.rgba[i] == 10);
    CHECK(out.rgba[i + 3] == 255);
  }
  CHECK(shrinkToFit(Image{}, 3, 3).empty());
}

TEST_CASE("trimTransparent removes empty margins") {
  Image im = solid(5, 4, 0, 0, 0, 0);
  auto at = [&](int x, int y) { return &im.rgba[(static_cast<size_t>(y) * 5 + static_cast<size_t>(x)) * 4]; };
  at(1, 1)[3] = 255;
  at(3, 2)[3] = 255;
  Image out = trimTransparent(im);
  CHECK(out.w == 3);
  CHECK(out.h == 2);
  CHECK(trimTransparent(solid(2, 2, 0, 0, 0, 0)).w == 2);
}

namespace {

// A w x h image: `shape` pixels (from the top) in `rgb`, `extra` more in
// `rgb2`, the rest transparent (or `fill` when given).
marigold::px::Image toneImage(int shape, uint32_t rgb, int extra = 0, uint32_t rgb2 = 0, int opaqueRest = -1) {
  marigold::px::Image im;
  im.w = 10;
  im.h = 10;
  im.rgba.assign(400, 0);
  for (int i = 0; i < 100; ++i) {
    uint32_t c = 0;
    uint8_t a = 0;
    if (i < shape) c = rgb, a = 255;
    else if (i < shape + extra) c = rgb2, a = 255;
    else if (opaqueRest >= 0) c = static_cast<uint32_t>(opaqueRest), a = 255;
    im.rgba[static_cast<size_t>(i) * 4] = static_cast<uint8_t>(c >> 16);
    im.rgba[static_cast<size_t>(i) * 4 + 1] = static_cast<uint8_t>(c >> 8);
    im.rgba[static_cast<size_t>(i) * 4 + 2] = static_cast<uint8_t>(c);
    im.rgba[static_cast<size_t>(i) * 4 + 3] = a;
  }
  return im;
}

}  // namespace

TEST_CASE("logo tone: white shapes vanish on a white cell, colour and solid boxes don't") {
  using marigold::px::Tone;
  using marigold::px::toneOf;
  CHECK(toneOf(toneImage(40, 0xFFFFFF)) == Tone::light);              // white on transparency
  CHECK(toneOf(toneImage(40, 0xE6E6EA)) == Tone::light);              // pale grey too
  CHECK(toneOf(toneImage(20, 0xFFFFFF, 20, 0xD0021B)) == Tone::mixed);  // white words, red mark
  CHECK(toneOf(toneImage(40, 0x1E5BC6)) == Tone::normal);             // coloured
  CHECK(toneOf(toneImage(40, 0xFFFF00)) == Tone::normal);             // bright yellow is colour
  CHECK(toneOf(toneImage(40, 0xFFFFFF, 0, 0, 0x0A2A6B)) == Tone::normal);  // white on its own blue box
  CHECK(toneOf(marigold::px::Image{}) == Tone::normal);
}
