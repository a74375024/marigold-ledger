#include "core/logos/tune.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <utility>

namespace marigold::logos {
namespace {

// Neutrals (black, white, greys) flip to the other end, but stop short of
// pure black or white. A colour is neutral when its channels are within 12.
constexpr double kNearMin = 0.08, kNearMax = 0.96;
constexpr int kNeutral = 12;

struct Rgb {
  double r = 0, g = 0, b = 0;
};

Rgb unpack(uint32_t c) {
  return {static_cast<double>((c >> 16) & 0xff), static_cast<double>((c >> 8) & 0xff), static_cast<double>(c & 0xff)};
}

uint8_t byte(double v) { return static_cast<uint8_t>(std::nearbyint(std::clamp(v, 0.0, 255.0))); }

uint32_t pack(double r, double g, double b) {
  return static_cast<uint32_t>(byte(r)) << 16 | static_cast<uint32_t>(byte(g)) << 8 | byte(b);
}

double linear(double v) {
  v /= 255;
  return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double gamma(double v) {
  v = std::clamp(v, 0.0, 1.0);
  return 255 * (v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055);
}

bool neutral(uint32_t c) {
  int r = static_cast<int>((c >> 16) & 0xff), g = static_cast<int>((c >> 8) & 0xff), b = static_cast<int>(c & 0xff);
  return std::max({r, g, b}) - std::min({r, g, b}) <= kNeutral;
}

// Hue, lightness and saturation, 0 to 1 (as Python's colorsys).
struct Hls {
  double h = 0, l = 0, s = 0;
};

double wrap(double v) { return v - std::floor(v); }

Hls toHls(uint32_t c) {
  Rgb x = unpack(c);
  double r = x.r / 255, g = x.g / 255, b = x.b / 255;
  double hi = std::max({r, g, b}), lo = std::min({r, g, b});
  double sum = hi + lo, range = hi - lo;
  Hls out;
  out.l = sum / 2;
  if (hi == lo) return out;
  out.s = out.l <= 0.5 ? range / sum : range / (2.0 - sum);
  double rc = (hi - r) / range, gc = (hi - g) / range, bc = (hi - b) / range;
  double h = r == hi ? bc - gc : g == hi ? 2.0 + rc - bc : 4.0 + gc - rc;
  out.h = wrap(h / 6.0);
  return out;
}

double hueChannel(double m1, double m2, double hue) {
  hue = wrap(hue);
  if (hue < 1.0 / 6.0) return m1 + (m2 - m1) * hue * 6.0;
  if (hue < 0.5) return m2;
  if (hue < 2.0 / 3.0) return m1 + (m2 - m1) * (2.0 / 3.0 - hue) * 6.0;
  return m1;
}

uint32_t fromHls(double h, double l, double s) {
  l = std::clamp(l, 0.0, 1.0);
  if (s == 0) return pack(l * 255, l * 255, l * 255);
  double m2 = l <= 0.5 ? l * (1.0 + s) : l + s - l * s;
  double m1 = 2.0 * l - m2;
  return pack(hueChannel(m1, m2, h + 1.0 / 3.0) * 255, hueChannel(m1, m2, h) * 255,
              hueChannel(m1, m2, h - 1.0 / 3.0) * 255);
}

bool okOn(uint32_t c, const std::vector<uint32_t>& refs) {
  for (uint32_t b : refs) {
    if (contrast(c, b) < kTarget) return false;
  }
  return true;
}

double distance2(const Rgb& a, const Rgb& b) {
  double dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
  return dr * dr + dg * dg + db * db;
}

size_t pixels(const px::Image& im) {
  if (im.empty() || im.rgba.size() < static_cast<size_t>(im.w) * static_cast<size_t>(im.h) * 4) return 0;
  return static_cast<size_t>(im.w) * static_cast<size_t>(im.h);
}

Rgb at(const px::Image& im, size_t i) {
  return {static_cast<double>(im.rgba[i * 4]), static_cast<double>(im.rgba[i * 4 + 1]),
          static_cast<double>(im.rgba[i * 4 + 2])};
}

// Colours, 5 bits a channel: pixels of one flat colour share a bin.
struct Bin {
  size_t n = 0;
  double sum[3] = {0, 0, 0};
};

size_t binOf(const uint8_t* p) {
  return static_cast<size_t>(p[0] >> 3) << 10 | static_cast<size_t>(p[1] >> 3) << 5 | static_cast<size_t>(p[2] >> 3);
}

std::vector<Bin> histogram(const px::Image& im, size_t& visible) {
  std::vector<Bin> bins(1u << 15);
  visible = 0;
  for (size_t i = 0, n = pixels(im); i < n; ++i) {
    const uint8_t* p = &im.rgba[i * 4];
    if (p[3] < 128) continue;
    Bin& b = bins[binOf(p)];
    ++b.n;
    for (int c = 0; c < 3; ++c) b.sum[c] += p[c];
    ++visible;
  }
  return bins;
}

}  // namespace

double luminance(uint32_t rgb) {
  Rgb c = unpack(rgb);
  return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
}

double contrast(uint32_t a, uint32_t b) {
  double la = luminance(a), lb = luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

const std::vector<uint32_t>& darkSurfaces() {
  static const std::vector<uint32_t> kSurfaces = {
      0x121418,  // the screens' background
      0x232428,  // guide and Search cells (white 7%)
      0x27292D,  // Live Events tiles (white 9%)
      0x202226,  // ended or not watchable tiles (white 6%)
      0x25272A,  // My Sports tiles (white 8%)
      0x1E2024,  // the corner card (white 5%)
      0x15171C,  // the floating card over a dark picture
      0x14161B,  // the banner's old logo plate
      0x1C1F24,  // detail and game cards
      0x262A32,  // in-player guide cells
      0x30353F,  // in-player guide, the show on now
      kFocusFill,
  };
  return kSurfaces;
}

uint32_t tuneColour(uint32_t rgb, const std::vector<uint32_t>& refs, bool up) {
  rgb &= 0xFFFFFF;
  if (okOn(rgb, refs)) return rgb;
  Hls x = toHls(rgb);
  double l = x.l;
  if (neutral(rgb)) {
    double flipped = std::min(kNearMax, std::max(kNearMin, 1 - l));
    uint32_t cand = fromHls(x.h, flipped, x.s);
    if (okOn(cand, refs)) return cand;
    l = flipped;
  }
  // The least change that reaches the target: halve the interval forty times.
  double lo = up ? l : 0.0, hi = up ? 1.0 : l;
  for (int i = 0; i < 40; ++i) {
    double mid = (lo + hi) / 2;
    bool good = okOn(fromHls(x.h, mid, x.s), refs);
    if (up == good) hi = mid;
    else lo = mid;
  }
  double best = up ? hi : lo;
  uint32_t out = fromHls(x.h, best, x.s);
  // Rounding to whole channels can fall just short: a step further.
  for (int i = 0; i < 500 && !okOn(out, refs); ++i) {
    best += up ? 0.002 : -0.002;
    out = fromHls(x.h, best, x.s);
  }
  return out;
}

uint32_t desaturate(uint32_t rgb, double s) {
  Rgb c = unpack(rgb);
  double r = linear(c.r), g = linear(c.g), b = linear(c.b);
  return pack(gamma((0.213 + 0.787 * s) * r + (0.715 - 0.715 * s) * g + (0.072 - 0.072 * s) * b),
              gamma((0.213 - 0.213 * s) * r + (0.715 + 0.285 * s) * g + (0.072 - 0.072 * s) * b),
              gamma((0.213 - 0.213 * s) * r + (0.715 - 0.715 * s) * g + (0.072 + 0.928 * s) * b));
}

Disc disc(uint32_t colour, uint32_t alt) {
  Disc d;
  d.fill = colour & 0xFFFFFF;
  d.ring = okOn(d.fill, darkSurfaces()) ? alt & 0xFFFFFF : tuneColour(alt);
  d.letters = contrast(0x000000, d.fill) > contrast(0xFFFFFF, d.fill) ? 0x000000 : 0xFFFFFF;
  return d;
}

bool flagHairline(uint32_t field, uint32_t surface) { return contrast(field, surface) < 3.0; }

void flagSize(int w, int h, float box, float& outW, float& outH) {
  outW = outH = box;
  if (w <= 0 || h <= 0) return;
  float s = std::min(box / static_cast<float>(w), box / static_cast<float>(h));
  outW = static_cast<float>(w) * s;
  outH = static_cast<float>(h) * s;
}

uint32_t mainColour(const px::Image& im) {
  size_t visible = 0;
  std::vector<Bin> bins = histogram(im, visible);
  auto best = std::max_element(bins.begin(), bins.end(), [](const Bin& a, const Bin& b) { return a.n < b.n; });
  if (best == bins.end() || best->n == 0) return 0;
  double n = static_cast<double>(best->n);
  return pack(best->sum[0] / n, best->sum[1] / n, best->sum[2] / n);
}

bool removeOwnBox(px::Image& im) {
  const size_t n = pixels(im);
  if (n == 0 || im.w < 8 || im.h < 8) return false;
  // An opaque rectangle: under 8% of it clear.
  size_t clear = 0;
  for (size_t i = 0; i < n; ++i) clear += im.rgba[i * 4 + 3] < 32;
  if (clear * 100 >= n * 8) return false;
  // Its border all one colour, near-white or near-black.
  std::vector<size_t> border;
  for (int x = 0; x < im.w; ++x) {
    border.push_back(static_cast<size_t>(x));
    border.push_back(static_cast<size_t>(im.h - 1) * static_cast<size_t>(im.w) + static_cast<size_t>(x));
  }
  for (int y = 1; y + 1 < im.h; ++y) {
    border.push_back(static_cast<size_t>(y) * static_cast<size_t>(im.w));
    border.push_back(static_cast<size_t>(y) * static_cast<size_t>(im.w) + static_cast<size_t>(im.w - 1));
  }
  double sum[3] = {0, 0, 0};
  size_t opaque = 0;
  for (size_t i : border) {
    if (im.rgba[i * 4 + 3] < 224) continue;
    for (int c = 0; c < 3; ++c) sum[c] += im.rgba[i * 4 + static_cast<size_t>(c)];
    ++opaque;
  }
  if (opaque * 100 < border.size() * 98) return false;
  double ob = static_cast<double>(opaque);
  uint32_t box = pack(sum[0] / ob, sum[1] / ob, sum[2] / ob);
  double l = toHls(box).l;
  if (!neutral(box) || (l > kNearMin && l < kNearMax)) return false;
  Rgb b = unpack(box);
  auto matches = [&](size_t i) {
    const uint8_t* p = &im.rgba[i * 4];
    return p[3] >= 224 && std::fabs(p[0] - b.r) <= kNeutral && std::fabs(p[1] - b.g) <= kNeutral &&
           std::fabs(p[2] - b.b) <= kNeutral;
  };
  size_t same = 0;
  for (size_t i : border) same += matches(i);
  if (same * 100 < border.size() * 98) return false;

  // Keyed: the box from the edges in, and the box colour showing through
  // inside the mark (the insides of letters).
  px::Image before = im;
  std::vector<uint8_t> keyed(n, 0);
  std::vector<size_t> stack;
  for (size_t i : border) {
    if (matches(i) && !keyed[i]) {
      keyed[i] = 1;
      stack.push_back(i);
    }
  }
  while (!stack.empty()) {
    size_t i = stack.back();
    stack.pop_back();
    int x = static_cast<int>(i % static_cast<size_t>(im.w)), y = static_cast<int>(i / static_cast<size_t>(im.w));
    const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
    for (const auto& q : nb) {
      if (q[0] < 0 || q[1] < 0 || q[0] >= im.w || q[1] >= im.h) continue;
      size_t j = static_cast<size_t>(q[1]) * static_cast<size_t>(im.w) + static_cast<size_t>(q[0]);
      if (keyed[j] || !matches(j)) continue;
      keyed[j] = 1;
      stack.push_back(j);
    }
  }
  for (size_t i = 0; i < n; ++i) {
    if (!keyed[i] && matches(i)) keyed[i] = 2;
  }
  for (size_t i = 0; i < n; ++i) {
    if (keyed[i]) im.rgba[i * 4 + 3] = 0;
  }
  // Soft edges: within two pixels of the keyed box, the box colour is taken
  // out of each pixel (what is left becomes see-through in proportion).
  for (int y = 0; y < im.h; ++y) {
    for (int x = 0; x < im.w; ++x) {
      size_t i = static_cast<size_t>(y) * static_cast<size_t>(im.w) + static_cast<size_t>(x);
      if (keyed[i]) continue;
      bool near = false;
      for (int dy = -2; dy <= 2 && !near; ++dy) {
        for (int dx = -2; dx <= 2 && !near; ++dx) {
          int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= im.w || ny >= im.h) continue;
          near = keyed[static_cast<size_t>(ny) * static_cast<size_t>(im.w) + static_cast<size_t>(nx)] != 0;
        }
      }
      if (!near) continue;
      uint8_t* p = &im.rgba[i * 4];
      const double bc[3] = {b.r, b.g, b.b};
      double a = 0;
      for (int c = 0; c < 3; ++c) {
        double room = p[c] > bc[c] ? 255 - bc[c] : bc[c];
        if (room > 0) a = std::max(a, std::fabs(p[c] - bc[c]) / room);
      }
      a = std::min(a, 1.0);
      if (a >= 1.0) continue;
      if (a <= 0.0) {
        p[3] = 0;
        continue;
      }
      for (int c = 0; c < 3; ++c) p[c] = byte(bc[c] + (p[c] - bc[c]) / a);
      p[3] = byte(p[3] * a);
    }
  }
  // Nothing left but specks: not a mark in a box after all.
  size_t visible = 0;
  for (size_t i = 0; i < n; ++i) visible += im.rgba[i * 4 + 3] >= 128;
  if (visible * 100 < n * 2) {
    im = std::move(before);
    return false;
  }
  return true;
}

bool tuneImage(px::Image& im) {
  const size_t n = pixels(im);
  if (n == 0) return false;
  size_t visible = 0;
  std::vector<Bin> bins = histogram(im, visible);
  if (visible == 0) return false;

  // The logo's parts: its main colours, most used first. A colour within 40
  // of a part's belongs to it; small leftovers (anti-aliasing between
  // parts, noise) are no part of their own.
  constexpr size_t kMaxParts = 8;
  constexpr double kSame = 40, kMinShare = 0.005;
  std::vector<size_t> order;
  for (size_t i = 0; i < bins.size(); ++i) {
    if (bins[i].n) order.push_back(i);
  }
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return bins[a].n > bins[b].n || (bins[a].n == bins[b].n && a < b); });
  std::vector<Rgb> centre;
  for (size_t i : order) {
    const Bin& bin = bins[i];
    double cnt = static_cast<double>(bin.n);
    Rgb m{bin.sum[0] / cnt, bin.sum[1] / cnt, bin.sum[2] / cnt};
    bool near = false;
    for (const Rgb& c : centre) near = near || distance2(c, m) <= kSame * kSame;
    if (near) continue;
    if (cnt < kMinShare * static_cast<double>(visible) || centre.size() >= kMaxParts) continue;
    Rgb exact = unpack(pack(m.r, m.g, m.b));
    centre.push_back(exact);
  }
  const size_t parts = centre.size();
  std::vector<uint32_t> colour(parts);
  for (size_t k = 0; k < parts; ++k) colour[k] = pack(centre[k].r, centre[k].g, centre[k].b);

  // Each visible pixel's part, and the pieces each part is drawn in.
  auto nearest = [&](const Rgb& p, int& second) {
    int best = -1;
    second = -1;
    double d1 = 0, d2 = 0;
    for (size_t k = 0; k < parts; ++k) {
      double d = distance2(p, centre[k]);
      if (best < 0 || d < d1) {
        second = best;
        d2 = d1;
        best = static_cast<int>(k);
        d1 = d;
      } else if (second < 0 || d < d2) {
        second = static_cast<int>(k);
        d2 = d;
      }
    }
    return best;
  };
  std::vector<int> label(n, -1);
  for (size_t i = 0; i < n; ++i) {
    if (im.rgba[i * 4 + 3] < 128) continue;
    int second = -1;
    label[i] = nearest(at(im, i), second);
  }
  std::vector<int> piece(n, -1);
  std::vector<int> pieceLabel;
  std::vector<size_t> pieceArea;
  std::vector<size_t> stack;
  auto index = [&im](int x, int y) { return static_cast<size_t>(y) * static_cast<size_t>(im.w) + static_cast<size_t>(x); };
  for (size_t s = 0; s < n; ++s) {
    if (label[s] < 0 || piece[s] >= 0) continue;
    int id = static_cast<int>(pieceLabel.size());
    pieceLabel.push_back(label[s]);
    pieceArea.push_back(0);
    piece[s] = id;
    stack.push_back(s);
    while (!stack.empty()) {
      size_t i = stack.back();
      stack.pop_back();
      ++pieceArea[static_cast<size_t>(id)];
      int x = static_cast<int>(i % static_cast<size_t>(im.w)), y = static_cast<int>(i / static_cast<size_t>(im.w));
      const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
      for (const auto& q : nb) {
        if (q[0] < 0 || q[1] < 0 || q[0] >= im.w || q[1] >= im.h) continue;
        size_t j = index(q[0], q[1]);
        if (piece[j] >= 0 || label[j] != label[s]) continue;
        piece[j] = id;
        stack.push_back(j);
      }
    }
  }
  const size_t pieces = pieceLabel.size();
  // What each piece touches: the outside (clear pixels, the image's edge)
  // or other pieces.
  std::vector<size_t> outside(pieces, 0), touches(pieces, 0);
  std::vector<std::unordered_map<int, size_t>> neighbours(pieces);
  for (int y = 0; y < im.h; ++y) {
    for (int x = 0; x < im.w; ++x) {
      int p = piece[index(x, y)];
      if (p < 0) continue;
      const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
      for (const auto& q : nb) {
        int other = q[0] < 0 || q[1] < 0 || q[0] >= im.w || q[1] >= im.h ? -1 : piece[index(q[0], q[1])];
        if (other == p) continue;
        ++touches[static_cast<size_t>(p)];
        if (other < 0) ++outside[static_cast<size_t>(p)];
        else ++neighbours[static_cast<size_t>(p)][other];
      }
    }
  }
  // A piece almost all inside one larger piece is a detail drawn on it.
  std::vector<int> parent(pieces, -1);
  for (size_t p = 0; p < pieces; ++p) {
    if (touches[p] == 0 || outside[p] * 10 >= touches[p]) continue;
    int best = -1;
    size_t most = 0;
    for (const auto& [q, cnt] : neighbours[p]) {
      if (cnt > most || (cnt == most && q < best)) {
        best = q;
        most = cnt;
      }
    }
    if (best >= 0 && pieceArea[static_cast<size_t>(best)] > pieceArea[p]) parent[p] = best;
  }

  // New colours: parts against the surfaces, details against their part's
  // new colour (lighter or darker, whichever side they are on).
  std::vector<uint32_t> onSurface(parts);
  for (size_t k = 0; k < parts; ++k) onSurface[k] = tuneColour(colour[k]);
  std::vector<uint32_t> tuned(pieces, 0);
  std::vector<uint8_t> done(pieces, 0);
  for (size_t p = 0; p < pieces; ++p) {
    if (parent[p] >= 0) continue;
    tuned[p] = onSurface[static_cast<size_t>(pieceLabel[p])];
    done[p] = 1;
  }
  // A black ground holding details that stand out by themselves (white
  // words in a black disc) stays as it is: on the dark cells it all but
  // vanishes and its details carry the logo; flipped, it would be a white
  // patch with the details lost on it.
  std::vector<size_t> detailArea(pieces, 0);
  std::vector<uint8_t> detailsShow(pieces, 1);
  for (size_t p = 0; p < pieces; ++p) {
    if (parent[p] < 0) continue;
    size_t q = static_cast<size_t>(parent[p]);
    detailArea[q] += pieceArea[p];
    if (!okOn(colour[static_cast<size_t>(pieceLabel[p])], darkSurfaces())) detailsShow[q] = 0;
  }
  for (size_t p = 0; p < pieces; ++p) {
    uint32_t own = colour[static_cast<size_t>(pieceLabel[p])];
    if (parent[p] < 0 && neutral(own) && toHls(own).l < 0.5 && detailsShow[p] && detailArea[p] * 10 >= pieceArea[p])
      tuned[p] = own;
  }
  std::unordered_map<uint64_t, uint32_t> nested;
  for (bool progress = true; progress;) {
    progress = false;
    for (size_t p = 0; p < pieces; ++p) {
      if (done[p] || !done[static_cast<size_t>(parent[p])]) continue;
      uint32_t base = tuned[static_cast<size_t>(parent[p])], own = colour[static_cast<size_t>(pieceLabel[p])];
      uint64_t key = static_cast<uint64_t>(own) << 32 | base;
      auto it = nested.find(key);
      if (it == nested.end()) {
        // Lighter or darker, whichever side of the part it is on; the other
        // way when there is no room (white on a part that turned white).
        bool up = luminance(own) > luminance(base);
        uint32_t t = tuneColour(own, {base}, up);
        if (contrast(t, base) < kTarget) t = tuneColour(own, {base}, !up);
        it = nested.emplace(key, t).first;
      }
      tuned[p] = it->second;
      done[p] = 1;
      progress = true;
    }
  }
  for (size_t p = 0; p < pieces; ++p) {
    if (done[p]) continue;  // a loop of details: on the surfaces
    tuned[p] = onSurface[static_cast<size_t>(pieceLabel[p])];
  }
  bool changed = false;
  for (size_t p = 0; p < pieces; ++p) changed = changed || tuned[p] != colour[static_cast<size_t>(pieceLabel[p])];
  if (!changed) return false;

  // A part's new colour near a pixel: the piece it is in, else the nearest
  // piece of that part within two pixels, else the part's colour for the
  // surfaces.
  auto tunedNear = [&](int x, int y, int k) {
    int found = -1;
    for (int r = 0; r <= 2 && found < 0; ++r) {
      for (int dy = -r; dy <= r && found < 0; ++dy) {
        for (int dx = -r; dx <= r && found < 0; ++dx) {
          if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
          int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= im.w || ny >= im.h) continue;
          int p = piece[index(nx, ny)];
          if (p >= 0 && pieceLabel[static_cast<size_t>(p)] == k) found = p;
        }
      }
    }
    return unpack(found >= 0 ? tuned[static_cast<size_t>(found)] : onSurface[static_cast<size_t>(k)]);
  };
  px::Image src = im;
  for (int y = 0; y < im.h; ++y) {
    for (int x = 0; x < im.w; ++x) {
      size_t i = index(x, y);
      if (src.rgba[i * 4 + 3] == 0) continue;
      Rgb p = at(src, i);
      int j = -1;
      int k = nearest(p, j);
      if (k < 0) continue;
      const Rgb& ck = centre[static_cast<size_t>(k)];
      Rgb tk = tunedNear(x, y, k);
      Rgb out{p.r + tk.r - ck.r, p.g + tk.g - ck.g, p.b + tk.b - ck.b};
      if (j >= 0) {
        // Between two parts (anti-aliasing): the same share of the way
        // between their new colours.
        const Rgb& cj = centre[static_cast<size_t>(j)];
        Rgb d{cj.r - ck.r, cj.g - ck.g, cj.b - ck.b};
        double len2 = d.r * d.r + d.g * d.g + d.b * d.b;
        double u = len2 > 0 ? ((p.r - ck.r) * d.r + (p.g - ck.g) * d.g + (p.b - ck.b) * d.b) / len2 : 0;
        Rgb on{ck.r + u * d.r, ck.g + u * d.g, ck.b + u * d.b};
        double off = distance2(p, on);
        double slack = std::max(12.0, 0.25 * std::sqrt(len2));
        if (u > 0 && u < 1 && off <= slack * slack) {
          Rgb tj = tunedNear(x, y, j);
          out = {tk.r + u * (tj.r - tk.r) + p.r - on.r, tk.g + u * (tj.g - tk.g) + p.g - on.g,
                 tk.b + u * (tj.b - tk.b) + p.b - on.b};
        }
      }
      uint8_t* o = &im.rgba[i * 4];
      o[0] = byte(out.r);
      o[1] = byte(out.g);
      o[2] = byte(out.b);
    }
  }
  return true;
}

px::Image smallCopy(const px::Image& im) {
  if (pixels(im) == 0 || px::strokeWidth(im) >= 4.0) return {};
  px::Image small = px::shrinkToFit(im, kSmallW, kSmallH);
  if (small.w == im.w && small.h == im.h) return {};
  if (px::strokeWidth(small) < 1.5) px::thicken(small);
  px::bleedEdges(small);
  return small;
}

Treatment prepare(px::Image& im, Art art) {
  Treatment t;
  if (art == Art::logo) {
    t.boxRemoved = removeOwnBox(im);
    if (t.boxRemoved) im = px::trimTransparent(std::move(im));
    t.tuned = tuneImage(im);
  }
  px::bleedEdges(im);
  return t;
}

}  // namespace marigold::logos
