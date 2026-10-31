// Search's keyboard (Build 115), drawn the same on every TV: the field, 🎤
// Speak where dictation works, the page (letters: the 6x5 alphabetical
// grid with Space after z; 123: a phone keypad centred on a faint panel,
// Space across its row), ⌫ Delete · 123/abc · Clear under it, and a ▶ tab
// at the keys' right edge while there are results. Where a key press goes
// is core/input/search_keys.hpp's; every key is a pointer target.
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
// The 123 page: the keypad's panel, its keys (132 x 62, 142 and 70 apart)
// and Space's row under it.
constexpr Rect kPanel{136, 144, 520, 296};
constexpr float kPadX = 188, kPadY = 156, kPadW = 132, kPadH = 62, kPadStepX = 142, kPadStepY = 70;
constexpr Rect kTab{672, 300, 64, 150};
constexpr float kBottom = kKeysTop + 5 * kStepY + kKeyH;  // under the wide keys

// Pointer ids: the field, Speak, the tab, then the keys (search_keys'
// targets). `base` offsets them.
constexpr int kHitField = 0, kHitSpeak = 1, kHitTab = 2, kHitKeys = 10, kHitCount = kHitKeys + search_keys::kTargets;

struct Look {
  std::string query;    // what's typed
  bool focused = true;  // the focus is on the keyboard (else it dims)
  bool tab = false;     // results to the right
  bool caret = true;
};
void draw(Ctx& c, const search_keys::Grid& grid, const Look& look, int base);

// A pointer id back to a spot on `page` (false for the field, the tab and
// the other page's keys).
bool spotFor(int id, int base, search_keys::Page page, search_keys::Grid::Spot& spot);

}  // namespace marigold::ui::keyboard
