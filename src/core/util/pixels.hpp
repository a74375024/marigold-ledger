// RGBA8 images in memory and the downscale used for logos: decoded once on
// a worker, shrunk to the size they are drawn at, then uploaded.
#pragma once

#include <cstdint>
#include <vector>

namespace marigold::px {

struct Image {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;  // w * h * 4, straight (not premultiplied) alpha
  bool empty() const { return w <= 0 || h <= 0; }
};

// Size that fits inside maxW x maxH keeping the aspect ratio; never upscales.
void fitWithin(int w, int h, int maxW, int maxH, int& outW, int& outH);

// Area-average downscale. Colour is weighted by alpha so transparent pixels
// do not bleed dark fringes into logo edges. Returns `src` unchanged when it
// already fits.
Image shrinkToFit(Image src, int maxW, int maxH);

// Trims fully transparent borders (many channel logos ship with wide empty
// margins, which makes them look small next to their neighbours).
Image trimTransparent(Image src, uint8_t threshold = 8);

}  // namespace marigold::px
