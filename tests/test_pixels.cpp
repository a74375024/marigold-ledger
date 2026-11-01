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

TEST_CASE("bleedEdges: clear pixels take their visible neighbours' colour") {
  // A red pixel in a clear (black) 5 x 5: its neighbours, two deep, turn red
  // and stay clear.
  Image im = solid(5, 5, 0, 0, 0, 0);
  auto at = [&im](int x, int y) { return &im.rgba[(static_cast<size_t>(y) * 5 + static_cast<size_t>(x)) * 4]; };
  at(1, 1)[0] = 200;
  at(1, 1)[3] = 255;
  bleedEdges(im);
  CHECK(at(0, 0)[0] == 200);
  CHECK(at(2, 2)[0] == 200);
  CHECK(at(3, 3)[0] == 200);
  CHECK(at(4, 4)[0] == 0);  // three away
  CHECK(at(2, 2)[3] == 0);
  CHECK(at(1, 1)[0] == 200);
}

TEST_CASE("strokeWidth and thicken") {
  Image line = solid(20, 20, 255, 255, 255, 0);
  for (int y = 2; y < 18; ++y) line.rgba[(static_cast<size_t>(y) * 20 + 5) * 4 + 3] = 255;  // a 1 px line
  CHECK(strokeWidth(line) == doctest::Approx(2.0));
  thicken(line);
  CHECK(line.rgba[(static_cast<size_t>(10) * 20 + 6) * 4 + 3] == 255);
  CHECK(line.rgba[(static_cast<size_t>(10) * 20 + 7) * 4 + 3] == 0);
  CHECK(strokeWidth(solid(10, 10, 0, 0, 0, 255)) > 4);
  CHECK(strokeWidth(Image{}) == 0);
}

TEST_CASE("shapeOf: the visible box at full size, and blank one-colour tiles") {
  // A 40 x 10 mark in a 100 x 60 canvas with wide transparent margins.
  Image framed = solid(100, 60, 0, 0, 0, 0);
  auto at = [&framed](int x, int y) {
    return &framed.rgba[(static_cast<size_t>(y) * 100 + static_cast<size_t>(x)) * 4];
  };
  for (int y = 20; y < 30; ++y)
    for (int x = 30; x < 70; ++x) {
      uint8_t* p = at(x, y);
      p[0] = 255;
      p[3] = ((x + y) % 3) ? 255 : 0;  // letters leave holes
    }
  Shape s = shapeOf(framed);
  CHECK(s.w == 40);
  CHECK(s.h == 10);
  CHECK_FALSE(s.plain);  // one colour, but a wordmark on transparency

  Shape tile = shapeOf(solid(64, 64, 40, 40, 40, 255));
  CHECK(tile.w == 64);
  CHECK(tile.h == 64);
  CHECK(tile.plain);

  // A solid box with a mark in another colour is a logo.
  Image boxed = solid(20, 20, 0, 0, 200, 255);
  for (int i = 0; i < 40; ++i) boxed.rgba[static_cast<size_t>(i) * 4] = 255;
  CHECK_FALSE(shapeOf(boxed).plain);

  Shape none = shapeOf(solid(8, 8, 255, 255, 255, 0));
  CHECK(none.w == 0);
  CHECK(none.h == 0);
  CHECK(shapeOf(Image{}).w == 0);
}
