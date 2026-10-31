// Search's keyboard (Build 113), drawn the same on every TV: the field, 🎤
// Speak where dictation works, the 6x6 alphabetical grid, ⌫ Delete · Space
// · Clear under it, and a ▶ tab at the grid's right edge while there are
// results. Where a key press goes is core/input/search_keys.hpp's; every
// key is a pointer target.
#pragma once

#include <string>

#include "app/ui/ui.hpp"
#include "core/input/search_keys.hpp"

namespace marigold::ui::keyboard {

constexpr float kLeft = 136;                // the content's left edge, beside the strip
constexpr Rect kField{136, 48, 486, 72};
constexpr Rect kSpeak{634, 48, 118, 72};
constexpr float kKeyW = 80, kKeyH = 68, kStepX = 88, kStepY = 76;
constexpr float kKeysTop = 144;
constexpr float kWideW = 168;
constexpr Rect kTab{672, 300, 64, 150};
constexpr float kBottom = kKeysTop + 6 * kStepY + kKeyH;  // under the wide keys

// Pointer ids: the field, Speak, the tab, then the keys (row * 6 + col)
// and the wide keys. `base` offsets them.
constexpr int kHitField = 0, kHitSpeak = 1, kHitTab = 2, kHitKeys = 10, kHitWide = 50, kHitCount = 60;

struct Look {
  std::string query;    // what's typed
  bool focused = true;  // the focus is on the keyboard (else it dims)
  bool tab = false;     // results to the right
  bool caret = true;
};
void draw(Ctx& c, const search_keys::Grid& grid, const Look& look, int base);

// A pointer id back to a spot on the grid (false for the field and the tab).
bool spotFor(int id, int base, search_keys::Grid::Spot& spot);

}  // namespace marigold::ui::keyboard
