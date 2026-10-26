#include "core/util/pixels.hpp"

#include <algorithm>
#include <cmath>

namespace marigold::px {

void fitWithin(int w, int h, int maxW, int maxH, int& outW, int& outH) {
  outW = w;
  outH = h;
  if (w <= 0 || h <= 0 || maxW <= 0 || maxH <= 0) return;
  if (w <= maxW && h <= maxH) return;
  double s = std::min(static_cast<double>(maxW) / w, static_cast<double>(maxH) / h);
  outW = std::max(1, static_cast<int>(std::lround(w * s)));
  outH = std::max(1, static_cast<int>(std::lround(h * s)));
  outW = std::min(outW, maxW);
  outH = std::min(outH, maxH);
}

Image shrinkToFit(Image src, int maxW, int maxH) {
  if (src.empty() || src.rgba.size() < static_cast<size_t>(src.w) * static_cast<size_t>(src.h) * 4)
    return Image{};
  int dw = 0, dh = 0;
  fitWithin(src.w, src.h, maxW, maxH, dw, dh);
  if (dw == src.w && dh == src.h) return src;

  Image out;
  out.w = dw;
  out.h = dh;
  out.rgba.assign(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4, 0);
  const double sx = static_cast<double>(src.w) / dw;
  const double sy = static_cast<double>(src.h) / dh;
  for (int y = 0; y < dh; ++y) {
    double y0 = y * sy, y1 = (y + 1) * sy;
    int iy0 = static_cast<int>(y0), iy1 = std::min(src.h, static_cast<int>(std::ceil(y1)));
    for (int x = 0; x < dw; ++x) {
      double x0 = x * sx, x1 = (x + 1) * sx;
      int ix0 = static_cast<int>(x0), ix1 = std::min(src.w, static_cast<int>(std::ceil(x1)));
      double r = 0, g = 0, b = 0, a = 0, area = 0;
      for (int yy = iy0; yy < iy1; ++yy) {
        double wy = std::min(y1, yy + 1.0) - std::max(y0, static_cast<double>(yy));
        if (wy <= 0) continue;
        const uint8_t* row = &src.rgba[(static_cast<size_t>(yy) * static_cast<size_t>(src.w)) * 4];
        for (int xx = ix0; xx < ix1; ++xx) {
          double wx = std::min(x1, xx + 1.0) - std::max(x0, static_cast<double>(xx));
          if (wx <= 0) continue;
          double wgt = wx * wy;
          const uint8_t* p = row + static_cast<size_t>(xx) * 4;
          double pa = p[3] * wgt;
          r += p[0] * pa;
          g += p[1] * pa;
          b += p[2] * pa;
          a += pa;
          area += wgt;
        }
      }
      uint8_t* o = &out.rgba[(static_cast<size_t>(y) * static_cast<size_t>(dw) + static_cast<size_t>(x)) * 4];
      if (a > 0) {
        o[0] = static_cast<uint8_t>(std::lround(std::clamp(r / a, 0.0, 255.0)));
        o[1] = static_cast<uint8_t>(std::lround(std::clamp(g / a, 0.0, 255.0)));
        o[2] = static_cast<uint8_t>(std::lround(std::clamp(b / a, 0.0, 255.0)));
      }
      o[3] = area > 0 ? static_cast<uint8_t>(std::lround(std::clamp(a / area, 0.0, 255.0))) : 0;
    }
  }
  return out;
}

Image trimTransparent(Image src, uint8_t threshold) {
  if (src.empty() || src.rgba.size() < static_cast<size_t>(src.w) * static_cast<size_t>(src.h) * 4)
    return src;
  int minX = src.w, minY = src.h, maxX = -1, maxY = -1;
  for (int y = 0; y < src.h; ++y) {
    for (int x = 0; x < src.w; ++x) {
      if (src.rgba[(static_cast<size_t>(y) * static_cast<size_t>(src.w) + static_cast<size_t>(x)) * 4 + 3] > threshold) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
      }
    }
  }
  if (maxX < 0) return src;  // all transparent: leave it alone
  if (minX == 0 && minY == 0 && maxX == src.w - 1 && maxY == src.h - 1) return src;
  Image out;
  out.w = maxX - minX + 1;
  out.h = maxY - minY + 1;
  out.rgba.resize(static_cast<size_t>(out.w) * static_cast<size_t>(out.h) * 4);
  for (int y = 0; y < out.h; ++y) {
    const uint8_t* from = &src.rgba[((static_cast<size_t>(y + minY)) * static_cast<size_t>(src.w) + static_cast<size_t>(minX)) * 4];
    std::copy(from, from + static_cast<size_t>(out.w) * 4, &out.rgba[static_cast<size_t>(y) * static_cast<size_t>(out.w) * 4]);
  }
  return out;
}

Tone toneOf(const Image& im) {
  if (im.empty()) return Tone::normal;
  size_t total = static_cast<size_t>(im.w) * static_cast<size_t>(im.h);
  size_t visible = 0, light = 0, clear = 0;
  for (size_t i = 0; i < total; ++i) {
    const uint8_t* p = &im.rgba[i * 4];
    if (p[3] < 32) ++clear;
    if (p[3] < 128) continue;
    ++visible;
    uint8_t lo = std::min({p[0], p[1], p[2]}), hi = std::max({p[0], p[1], p[2]});
    if (lo >= 200 && hi - lo <= 40) ++light;
  }
  // Mostly opaque: the logo brings its own background.
  if (visible == 0 || clear * 100 < total * 8) return Tone::normal;
  if (light * 100 >= visible * 75) return Tone::light;
  if (light * 100 >= visible * 25) return Tone::mixed;
  return Tone::normal;
}

}  // namespace marigold::px
