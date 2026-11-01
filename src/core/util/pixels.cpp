#include "core/util/pixels.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

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

void bleedEdges(Image& im) {
  if (im.empty() || im.rgba.size() < static_cast<size_t>(im.w) * static_cast<size_t>(im.h) * 4) return;
  const size_t w = static_cast<size_t>(im.w), h = static_cast<size_t>(im.h), n = w * h;
  std::vector<uint8_t> coloured(n), next, across(n);
  for (size_t i = 0; i < n; ++i) coloured[i] = im.rgba[i * 4 + 3] > 0;
  for (int pass = 0; pass < 2; ++pass) {
    next = coloured;
    // Only the band beside the coloured pixels has anything to do: a pixel
    // is in it when a coloured one is within one step (rows first, then
    // columns), so the clear rest is skipped at a byte a pixel.
    // Eight pixels at a time (one byte each) where the row has room.
    for (size_t y = 0; y < h; ++y) {
      const uint8_t* c = &coloured[y * w];
      uint8_t* a = &across[y * w];
      a[0] = c[0] | (w > 1 ? c[1] : 0);
      size_t x = 1;
      for (; x + 9 <= w; x += 8) {
        uint64_t l, m, r;
        std::memcpy(&l, c + x - 1, 8);
        std::memcpy(&m, c + x, 8);
        std::memcpy(&r, c + x + 1, 8);
        uint64_t v = l | m | r;
        std::memcpy(a + x, &v, 8);
      }
      for (; x + 1 < w; ++x) a[x] = c[x - 1] | c[x] | c[x + 1];
      if (w > 1) a[w - 1] = c[w - 2] | c[w - 1];
    }
    for (size_t y = 0; y < h; ++y) {
      const uint8_t* row = &across[y * w];
      const uint8_t* above = y > 0 ? row - w : row;
      const uint8_t* below = y + 1 < h ? row + w : row;
      const uint8_t* own = &coloured[y * w];
      for (size_t x = 0; x < w; ++x) {
        if (x % 8 == 0 && x + 8 <= w) {
          // Nothing coloured near, or all coloured already: the next eight
          // have nothing to do.
          uint64_t here, up, down, done;
          std::memcpy(&here, row + x, 8);
          std::memcpy(&up, above + x, 8);
          std::memcpy(&down, below + x, 8);
          std::memcpy(&done, own + x, 8);
          if ((here | up | down) == 0 || done == 0x0101010101010101ull) {
            x += 7;
            continue;
          }
        }
        size_t i = y * w + x;
        if (!(row[x] | above[x] | below[x]) || coloured[i]) continue;
        // The average of the coloured neighbours, the visible ones counting
        // by how visible they are; whole numbers, rounded half up as
        // std::lround() rounds their exact quotient.
        uint32_t sum[3] = {0, 0, 0}, weight = 0;
        for (size_t ny = y > 0 ? y - 1 : 0; ny <= y + 1 && ny < h; ++ny) {
          for (size_t nx = x > 0 ? x - 1 : 0; nx <= x + 1 && nx < w; ++nx) {
            size_t j = ny * w + nx;
            if (j == i || !coloured[j]) continue;
            const uint8_t* q = &im.rgba[j * 4];
            uint32_t wgt = 1u + q[3];
            for (int ch = 0; ch < 3; ++ch) sum[ch] += q[ch] * wgt;
            weight += wgt;
          }
        }
        if (weight == 0) continue;
        for (int ch = 0; ch < 3; ++ch)
          im.rgba[i * 4 + static_cast<size_t>(ch)] = static_cast<uint8_t>((2 * sum[ch] + weight) / (2 * weight));
        next[i] = 1;
      }
    }
    coloured.swap(next);
  }
}

double strokeWidth(const Image& im) {
  if (im.empty() || im.rgba.size() < static_cast<size_t>(im.w) * static_cast<size_t>(im.h) * 4) return 0;
  auto solid = [&im](int x, int y) {
    if (x < 0 || y < 0 || x >= im.w || y >= im.h) return false;
    return im.rgba[(static_cast<size_t>(y) * static_cast<size_t>(im.w) + static_cast<size_t>(x)) * 4 + 3] >= 128;
  };
  size_t area = 0, outline = 0;
  for (int y = 0; y < im.h; ++y) {
    for (int x = 0; x < im.w; ++x) {
      if (!solid(x, y)) continue;
      ++area;
      if (!solid(x - 1, y) || !solid(x + 1, y) || !solid(x, y - 1) || !solid(x, y + 1)) ++outline;
    }
  }
  return outline ? 2.0 * static_cast<double>(area) / static_cast<double>(outline) : 0.0;
}

void thicken(Image& im) {
  if (im.empty() || im.rgba.size() < static_cast<size_t>(im.w) * static_cast<size_t>(im.h) * 4) return;
  Image src = im;
  auto at = [&src](int x, int y) {
    return &src.rgba[(static_cast<size_t>(y) * static_cast<size_t>(src.w) + static_cast<size_t>(x)) * 4];
  };
  for (int y = 0; y < im.h; ++y) {
    for (int x = 0; x < im.w; ++x) {
      // The most visible of this pixel and the ones left of and above it.
      const uint8_t* best = at(x, y);
      if (x > 0 && at(x - 1, y)[3] > best[3]) best = at(x - 1, y);
      if (y > 0 && at(x, y - 1)[3] > best[3]) best = at(x, y - 1);
      if (x > 0 && y > 0 && at(x - 1, y - 1)[3] > best[3]) best = at(x - 1, y - 1);
      uint8_t* o = &im.rgba[(static_cast<size_t>(y) * static_cast<size_t>(im.w) + static_cast<size_t>(x)) * 4];
      if (best != at(x, y)) std::copy(best, best + 4, o);
    }
  }
}

Shape shapeOf(const Image& im, uint8_t threshold) {
  Shape out;
  if (im.empty() || im.rgba.size() < static_cast<size_t>(im.w) * static_cast<size_t>(im.h) * 4) return out;
  auto at = [&im](int x, int y) {
    return &im.rgba[(static_cast<size_t>(y) * static_cast<size_t>(im.w) + static_cast<size_t>(x)) * 4];
  };
  int minX = im.w, minY = im.h, maxX = -1, maxY = -1;
  for (int y = 0; y < im.h; ++y) {
    for (int x = 0; x < im.w; ++x) {
      if (at(x, y)[3] <= threshold) continue;
      minX = std::min(minX, x);
      maxX = std::max(maxX, x);
      minY = std::min(minY, y);
      maxY = std::max(maxY, y);
    }
  }
  if (maxX < 0) return out;
  out.w = maxX - minX + 1;
  out.h = maxY - minY + 1;
  // Plain: the box is (nearly) all solid and (nearly) all one colour. A
  // one-colour wordmark on transparency is not: its letters leave holes.
  size_t box = static_cast<size_t>(out.w) * static_cast<size_t>(out.h);
  size_t solid = 0;
  double sum[3] = {0, 0, 0};
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const uint8_t* p = at(x, y);
      if (p[3] < 128) continue;
      ++solid;
      for (int i = 0; i < 3; ++i) sum[i] += p[i];
    }
  }
  if (solid * 100 < box * 97) return out;
  double mean[3] = {sum[0] / static_cast<double>(solid), sum[1] / static_cast<double>(solid),
                    sum[2] / static_cast<double>(solid)};
  size_t off = 0;
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const uint8_t* p = at(x, y);
      if (p[3] < 128) continue;
      for (int i = 0; i < 3; ++i) {
        if (std::fabs(p[i] - mean[i]) > 40) {
          ++off;
          break;
        }
      }
    }
  }
  out.plain = off * 100 < solid;
  return out;
}

}  // namespace marigold::px
