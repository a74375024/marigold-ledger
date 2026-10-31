// The left rail (Build 112), drawn the same on the guide and Live Events: a
// 104 px strip down the left in its own colour with the sections' icons,
// the neighborhood column inside the content (aligned with the hero, its
// header in the timeline row), and the full-height menu with labels over
// the dimmed screen. What the keys do is core/input/left_rail.hpp's.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/ui/ui.hpp"
#include "core/input/left_rail.hpp"

namespace marigold::app::left_rail {

constexpr float kStripW = 104;
// The hero and, at rest, the content start here; with the column open the
// content is pushed to kPushLeft.
constexpr float kRestLeft = 136;
constexpr float kColumnX = 136, kColumnW = 240;
constexpr float kPushLeft = kColumnX + kColumnW + 32;  // 408
constexpr float kMenuW = 440;
constexpr uint32_t kSlideMs = 180;

// Pointer ids, clear of the screens' own: the whole screen under
// everything (the pointer left a target), around the open menu, and the
// strip's, the menu's and the column's entries (+ index).
constexpr int kHitNothing = 1;
constexpr int kHitOutside = 2;
constexpr int kHitStrip = 200;
constexpr int kHitMenu = 300;
constexpr int kHitColumn = 400;
constexpr int kHitEnd = 500;

// A value that eases to its target in kSlideMs (the column's push, the
// menu's slide).
class Slide {
 public:
  void go(float target, uint32_t ms);
  float at(uint32_t ms) const;
  float target() const { return to_; }
  bool landed(uint32_t ms) const { return from_ == to_ || ms - start_ >= kSlideMs; }
  // Still moving, or just landed: one more frame draws where it ends.
  bool moving(uint32_t ms) const { return from_ != to_ && ms - start_ < kSlideMs + 60; }

 private:
  float from_ = 0, to_ = 0;
  uint32_t start_ = 0;
};

// The strip, full height, the current section marked with a bar. `hover`:
// the item the pointer is on, whose name shows in a pill (-1 for none).
void strip(ui::Ctx& c, const std::vector<rail::Item>& items, rail::Item current, int hover);

// The menu over everything on the left, `t` (0 to 1) of the way in.
void menu(ui::Ctx& c, const std::vector<rail::Item>& items, rail::Item current, int focus, float t);

// The column inside the content, faded by `alpha`: the section's name and
// `word` ("Jump to", "Sports") in the timeline row at `top`, the entries
// from `first` down, a thin rule against the grid. `origin` gets the red
// dot; `hover` (the pointer) a soft highlight. No counts of channels.
struct Entry {
  std::string title;
  int live = 0;  // "2 live" (Live Events' sports)
};
void column(ui::Ctx& c, const std::string& section, const char* word, const std::vector<Entry>& entries, int focus,
            int origin, int hover, float top, float first, float alpha);

}  // namespace marigold::app::left_rail
