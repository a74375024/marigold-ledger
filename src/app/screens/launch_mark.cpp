#include "app/screens/launch_mark.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/util/pixels.hpp"

namespace marigold::app {
namespace {

using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;
namespace launch = engine::launch;

// The splash's colours.
const Color kOuterColour = theme::hex(0xD6781E);
const Color kInnerColour = theme::hex(0xF2A03D);
const Color kCoreColour = theme::hex(0x964614);
const Color kLight = theme::hex(0xFFB84D);  // glow and halo
const Color kWarmGround = theme::hex(0x2A190D);

Color mix(Color a, Color b, float t) {
  t = std::min(1.0f, std::max(0.0f, t));
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// The guide's bands, top to bottom, as they come in (hero, tabs and ruler,
// then the rows).
Rect band(int k) {
  if (k == 0) return {0, 0, gfx::kLayoutWidth, 330};
  if (k == 1) return {0, 330, gfx::kLayoutWidth, 70};
  float rows = (gfx::kLayoutHeight - 400) / static_cast<float>(launch::kGuideLayers - 2);
  return {0, 400 + rows * static_cast<float>(k - 2), gfx::kLayoutWidth, rows + 1};
}

// Small capitals with open tracking, centred on `cx`.
void tracked(ui::Ctx& c, const std::string& s, float cx, float y, int px, Color col, float tracking) {
  std::vector<std::string> glyphs;
  for (size_t i = 0; i < s.size();) {
    size_t n = 1;
    unsigned char b = static_cast<unsigned char>(s[i]);
    if (b >= 0xF0) n = 4;
    else if (b >= 0xE0) n = 3;
    else if (b >= 0xC0) n = 2;
    glyphs.push_back(s.substr(i, n));
    i += n;
  }
  float gap = tracking * static_cast<float>(px);
  float width = 0;
  std::vector<float> widths;
  for (const std::string& g : glyphs) {
    float w = g == " " ? static_cast<float>(px) * 0.3f : ui::textWidth(c, g, Weight::Medium, px);
    widths.push_back(w);
    width += w + gap;
  }
  width -= gap;
  float x = cx - width / 2;
  for (size_t i = 0; i < glyphs.size(); ++i) {
    if (glyphs[i] != " ") ui::label(c, glyphs[i], x, y, Weight::Medium, px, col);
    x += widths[i] + gap;
  }
}

}  // namespace

LaunchMark::~LaunchMark() { gfx::releaseTexture(soft_); }

void LaunchMark::update(uint32_t ms, const engine::LoadProgress& p, bool reading, bool ready) {
  if (!motion_.started()) motion_.start(ms);
  if (ready) motion_.ready(ms);
  else motion_.progress(ms, p, reading);
}

bool LaunchMark::covering(uint32_t ms) const { return !motion_.started() || motion_.frame(ms).covering; }

bool LaunchMark::done(uint32_t ms) const { return motion_.started() && motion_.frame(ms).done; }

void LaunchMark::ensureTexture() {
  if (soft_) return;
  // A white disc whose alpha falls off smoothly from the middle to the edge:
  // drawn at 2 x (radius + blur) it reads as a blurred circle, larger as a
  // glow or the halo.
  constexpr int kSize = 128;
  px::Image im;
  im.w = im.h = kSize;
  im.rgba.resize(static_cast<size_t>(kSize) * kSize * 4);
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      float dx = (static_cast<float>(x) + 0.5f) / kSize * 2 - 1;
      float dy = (static_cast<float>(y) + 0.5f) / kSize * 2 - 1;
      float d = std::sqrt(dx * dx + dy * dy);
      float a = d >= 1 ? 0 : 1 - d * d * (3 - 2 * d);  // smoothstep falloff
      uint8_t* px = &im.rgba[(static_cast<size_t>(y) * kSize + x) * 4];
      px[0] = px[1] = px[2] = 255;
      px[3] = static_cast<uint8_t>(std::lround(std::min(1.0f, a) * 255));
    }
  }
  soft_ = gfx::uploadTexture(im);
}

void LaunchMark::backdrop(ui::Ctx& c, uint32_t ms) {
  launch::Frame f = motion_.frame(ms);
  ensureTexture();
  c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, mix(theme::hex(theme::kBackground), kWarmGround, f.warm));
  if (soft_ && f.halo > 0) {
    float r = 420;
    c.g.textureAlpha(soft_, {launch::kCentreX - r, launch::kCentreY - r, 2 * r, 2 * r}, kLight.withAlpha(0.35f * f.halo));
  }
}

void LaunchMark::veil(ui::Ctx& c, uint32_t ms) const {
  launch::Frame f = motion_.frame(ms);
  if (f.done) return;
  // The same warm ground the mark stood on, so nothing jumps as it goes.
  Color ground = mix(theme::hex(theme::kBackground), kWarmGround, f.warm);
  for (int k = 0; k < launch::kGuideLayers; ++k) {
    float hidden = 1 - f.guide[k];
    if (hidden > 0.001f) c.g.fill(band(k), ground.withAlpha(hidden));
  }
}

void LaunchMark::mark(ui::Ctx& c, uint32_t ms) {
  launch::Frame f = motion_.frame(ms);
  if (f.done || f.opacity <= 0.001f) return;
  ensureTexture();
  // During the exit the halo is drawn here too (the backdrop is gone).
  if (!f.covering && soft_ && f.halo > 0) {
    float r = 420;
    c.g.textureAlpha(soft_, {launch::kCentreX - r, launch::kCentreY - r, 2 * r, 2 * r},
                     kLight.withAlpha(0.35f * f.halo));
  }
  // Bloom-out: hard circles give way to soft ones as the blur grows.
  float soft = std::min(1.0f, f.blur / 14.0f);
  auto disc = [&](const launch::Disc& d, Color col) {
    if (soft < 1) c.g.fill({d.x - d.r, d.y - d.r, 2 * d.r, 2 * d.r}, col.withAlpha(col.a * f.opacity * (1 - soft)), d.r);
    if (soft > 0 && soft_) {
      float r = d.r + f.blur;
      c.g.textureAlpha(soft_, {d.x - r, d.y - r, 2 * r, 2 * r}, col.withAlpha(col.a * f.opacity * soft));
    }
  };
  // Lit petals glow.
  if (soft_) {
    for (const launch::Disc& d : f.outer) {
      if (d.glow <= 0.01f) continue;
      float r = d.r * 1.9f;
      c.g.textureAlpha(soft_, {d.x - r, d.y - r, 2 * r, 2 * r}, kLight.withAlpha(0.55f * d.glow * f.opacity));
    }
  }
  Color ground = mix(theme::hex(theme::kBackground), kWarmGround, f.warm);
  for (const launch::Disc& d : f.outer) disc(d, mix(mix(kOuterColour, ground, 0.55f), kOuterColour, d.lit));
  for (const launch::Disc& d : f.inner) disc(d, kInnerColour);
  disc(f.core, kCoreColour);

  // "Marigold", sized like the splash's (224 px wide).
  if (f.wordmark > 0.001f) {
    if (!wordPx_) {
      float w = ui::textWidth(c, "Marigold", Weight::Medium, 62);
      wordPx_ = w > 0 ? static_cast<int>(std::lround(62 * 224 / w)) : 62;
    }
    float y = launch::kWordmarkY - static_cast<float>(wordPx_) * 0.22f;
    ui::label(c, "Marigold", 0, y, Weight::Medium, wordPx_, theme::white(f.wordmark), gfx::kLayoutWidth,
              ui::Align::Center);
  }
  if (!f.status.empty() && f.statusOpacity > 0.001f)
    tracked(c, f.status, launch::kCentreX, launch::kStatusY - f.statusRise, 22, theme::white(0.55f * f.statusOpacity),
            0.16f);
}

}  // namespace marigold::app
