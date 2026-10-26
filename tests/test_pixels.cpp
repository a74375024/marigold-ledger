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
