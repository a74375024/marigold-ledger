// The UI kit: the shared look (LiveStyle in tvOS), a drawing context passed
// to every screen, and the small pieces screens are built from.
//
// Layout units are 1920x1080 points, like the tvOS app's, so sizes from the
// tvOS views carry over as they are.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/util/time.hpp"
#include "platform/gfx.hpp"
#include "platform/text.hpp"

namespace marigold::logos {
class LogoCache;
}

namespace marigold::ui {

using gfx::Color;
using gfx::Rect;
using text::Align;
using text::Weight;

namespace theme {
constexpr uint32_t kBackground = 0x121418;
constexpr uint32_t kWatching = 0xFF5252;  // accent red (1, 0.32, 0.32)
constexpr uint32_t kLive = 0xC62828;
constexpr uint32_t kNowLine = 0xE53935;
constexpr uint32_t kKeyRed = 0xE53935;
constexpr uint32_t kKeyGreen = 0x43A047;
constexpr uint32_t kKeyYellow = 0xFDD835;
constexpr uint32_t kKeyBlue = 0x1E88E5;
constexpr uint32_t kNew = 0x1E88E5;
constexpr uint32_t kStar = 0xE0A100;
constexpr uint32_t kToggleOn = 0x34C759;
constexpr uint32_t kWarning = 0xFF9F0A;

constexpr float kMarginX = 80;
constexpr float kRowRadius = 16;
constexpr float kPanelRadius = 26;
constexpr float kPanelRowHeight = 64;
constexpr float kInactiveOpacity = 0.45f;

inline Color white(float a = 1) { return Color{1, 1, 1, a}; }
inline Color black(float a = 1) { return Color{0, 0, 0, a}; }
inline Color hex(uint32_t rgb, float a = 1) { return Color::hex(rgb, a); }
}  // namespace theme

// Something the Magic Remote pointer can land on. Screens register these
// while drawing; the app maps pointer moves to ids.
struct HitRegion {
  Rect rect;
  int id = 0;
};

struct Ctx {
  gfx::Renderer& g;
  text::TextRenderer& t;
  logos::LogoCache* logos = nullptr;
  time::Seconds now = 0;        // wall clock
  uint32_t ms = 0;              // monotonic ms (animations)
  const time::Zone* zone = nullptr;
  std::vector<HitRegion>* hits = nullptr;
  bool pointerActive = false;
  bool clock24 = false;  // Options › Clock

  void hit(Rect r, int id) const {
    if (hits) hits->push_back({r, id});
  }
};

// One line of text with its top at y. Returns the drawn width.
float label(Ctx& c, const std::string& s, float x, float y, Weight w, int px, Color col,
            float maxWidth = 0, Align a = Align::Left);
// One line vertically centred in `r`.
float labelIn(Ctx& c, const std::string& s, Rect r, Weight w, int px, Color col,
              Align a = Align::Left);
// Word-wrapped text. Returns the height used.
float paragraph(Ctx& c, const std::string& s, float x, float y, float width, Weight w, int px,
                Color col, int maxLines, float lineGap = 1.35f);
float textWidth(Ctx& c, const std::string& s, Weight w, int px);

// A rounded chip with text: filled, or outlined when `outline`.
// Returns the chip width. Height is px * 1.5 rounded.
float chip(Ctx& c, const std::string& s, float x, float y, int px, Color fill, Color textCol,
           bool outline = false, Weight w = Weight::Bold);
float chipWidth(Ctx& c, const std::string& s, int px, Weight w = Weight::Bold);

// A channel logo fitted inside `r` (aspect kept, centred), or the fallback
// name in a pill until the logo is ready (or if there is none).
void logo(Ctx& c, const std::string& url, const std::string& fallback, Rect r,
          bool onLight = false, float opacity = 1);

// Progress bar (radius = h/2).
void progress(Ctx& c, Rect r, double fraction, Color track = theme::white(0.18f),
              Color fill = theme::white());

// The bottom colour-key bar: up to four (colour, label) pairs and a hint.
struct KeyHint {
  uint32_t color = 0;
  std::string label;
};
void keyBar(Ctx& c, const std::vector<KeyHint>& keys, const std::string& hint);

// A small spinner centred at (x, y).
void spinner(Ctx& c, float x, float y, float radius, Color col = theme::white(0.8f));

// The frosted side panel background (55% black over the UI, fading on the
// open edge). `leading` panels open from the left.
void sidePanel(Ctx& c, Rect r, bool leading);
// Full screen scrim.
void scrim(Ctx& c, float alpha);

// Time formatting in the TV's zone: "8:42 PM", "8:00 – 9:00", "Tonight".
std::string clock(const Ctx& c, time::Seconds t, bool withPeriod = true);
std::string range(const Ctx& c, time::Seconds a, time::Seconds b, bool withPeriod = false);
// "Tonight", "Tomorrow", "Sat", or "" for today before evening.
std::string dayWord(const Ctx& c, time::Seconds t);

}  // namespace marigold::ui
