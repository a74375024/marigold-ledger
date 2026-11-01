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

// Clear pixels next to visible ones take their colour (alpha stays 0), two
// pixels deep. Decoders leave clear pixels black, and linear filtering
// between a visible edge and a clear neighbour then mixes in that black: a
// dark fringe on every edge, worst on thin and light logos.
void bleedEdges(Image& im);

// How thick a logo's strokes are, in pixels: twice its visible area over its
// outline's length (0 when nothing is visible).
double strokeWidth(const Image& im);
// Thickens every visible shape by one pixel (right and down), for strokes
// too thin to survive drawing small.
void thicken(Image& im);

// What a logo is made of, measured once on the full-size decode (before
// trimming and shrinking), for logos::fillsCard().
struct Shape {
  // The visible part (alpha above the trim threshold), in source pixels;
  // 0 x 0 when nothing is visible.
  int w = 0, h = 0;
  // One flat colour over the whole visible box: a blank tile.
  bool plain = false;
};
Shape shapeOf(const Image& image, uint8_t threshold = 8);

}  // namespace marigold::px
