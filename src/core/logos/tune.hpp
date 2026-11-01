// How logos, team badges and flags look on the app's dark surfaces: every
// logo sits straight on its cell (no plates, chips or halos), so each one
// gets a single version made for dark surfaces, worked out once on a worker
// after the decode. Colours that would fade into the darkest-looking cells
// get just enough lightness to stand out, with their hue and saturation
// kept; a logo that brings its own plain white or black box loses the box.
// Focus never changes a logo, so there is no version for a white cell.
#pragma once

#include <cstdint>
#include <vector>

#include "core/util/pixels.hpp"

namespace marigold::logos {

// WCAG 2 relative luminance of 0xRRGGBB (0 to 1), and the contrast ratio of
// two colours (1 to 21).
double luminance(uint32_t rgb);
double contrast(uint32_t a, uint32_t b);

// Every dark surface a logo or badge can sit on, in every state: the screen
// background, guide, Search and Live Events cells and tiles, the cards and
// banners, the in-player guide's cells and the calm focus fill. Tuning against
// all of them makes one version that works everywhere.
const std::vector<uint32_t>& darkSurfaces();
// What tuning aims for: a little over 3:1, so rounding and the dimmed rows'
// loss of colour still leave 3:1.
constexpr double kTarget = 3.2;
// The calm focus: a focused cell where logos appear takes this fill with a
// 3 px white outline instead of turning white.
constexpr uint32_t kFocusFill = 0x2A2E36;

// One colour for dark surfaces: unchanged when it already reaches kTarget
// against every surface in `refs`; otherwise hue and saturation are kept and
// the lightness moved just far enough (`up`: lighter). Black, white and greys
// flip to the other end first.
uint32_t tuneColour(uint32_t rgb, const std::vector<uint32_t>& refs, bool up);
inline uint32_t tuneColour(uint32_t rgb) { return tuneColour(rgb, darkSurfaces(), true); }

// Dimmed rows and tiles: most of the colour taken out in linear light, so
// the brightness (and the contrast) stays. `saturation` 1 leaves it as is.
constexpr double kDimSaturation = 0.3;
uint32_t desaturate(uint32_t rgb, double saturation = kDimSaturation);

// A team's fallback disc: the fill keeps the team colour; the disc's own
// edge ring is tuned when the fill alone would not stand out; the letters
// are black or white, whichever contrasts more with the fill.
struct Disc {
  uint32_t fill = 0, ring = 0, letters = 0;
};
Disc disc(uint32_t colour, uint32_t alt);

// A flag gets a 1 px hairline on its own edge only where its field (its
// main colour) would blend into `surface`.
bool flagHairline(uint32_t field, uint32_t surface);
// A flag image (w x h, trimmed by the decode) fitted inside a box x box
// square at its own aspect ratio: never squashed into the square.
void flagSize(int w, int h, float box, float& outW, float& outH);

// What an image is, as the code asking for it knows: a logo is tuned and
// may lose its own box; a flag or a photo is never recoloured.
enum class Art : uint8_t { logo, flag, photo };

// What was done to a logo, for the channel report.
struct Treatment {
  bool boxRemoved = false;
  bool tuned = false;
};

// Logos in their own plain box: when the image is an opaque rectangle whose
// border is one near-white or near-black colour, that colour is keyed out
// (from the edges in, and inside letters), with soft edges, and the clear
// margin trimmed. False, and the image untouched, when unsure.
bool removeOwnBox(px::Image& im);
// The dark-surface version of a logo, in place. Its colours are grouped into
// parts; a part that touches the outside is tuned against the surfaces, a
// detail drawn inside another part against that part's new colour; a black
// ground whose details stand out by themselves (white words in a black disc)
// is left as it is. Anti-aliased pixels between two parts follow both. True
// when any colour changed.
bool tuneImage(px::Image& im);
// The colour covering the most of the visible image (a flag's field).
uint32_t mainColour(const px::Image& im);

// Thin logos (strokes under 4 px in the full-size texture) also get a
// ready-made small copy for small boxes: drawn 58 wide from 320 px, linear
// filtering skips texels and lines break up. Strokes still under 1.5 px in
// the copy are thickened by one. Empty for other logos.
constexpr int kSmallW = 96, kSmallH = 54;
px::Image smallCopy(const px::Image& im);

// Everything done to a freshly decoded image before upload, on the worker:
// box removal and tuning for logos, and for every kind clear pixels take the
// colour of their visible neighbours (px::bleedEdges).
Treatment prepare(px::Image& im, Art art);

}  // namespace marigold::logos
