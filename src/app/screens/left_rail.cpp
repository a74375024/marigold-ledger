#include "app/screens/left_rail.hpp"

#include <algorithm>
#include <cmath>

#include "platform/icons.hpp"

namespace marigold::app::left_rail {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr uint32_t kNavColour = 0x1D2026;
constexpr float kTopY = 400;     // the first icon's centre, below the hero line
constexpr float kBottomY = 878;  // My Sports
constexpr float kStep = 72;
constexpr float kRuleY = 812;    // between the sections and the bottom group
constexpr float kColumnBottom = 996;
constexpr float kEntryH = 52, kEntryStep = 58;

const char* iconName(rail::Item i) {
  switch (i) {
    case rail::Item::Search: return "ui-search";
    case rail::Item::Lineup: return "ui-lineup";
    case rail::Item::Favorites: return "ui-favorites";
    case rail::Item::Recent: return "ui-recent";
    case rail::Item::Events: return "ui-events";
    case rail::Item::All: return "ui-all";
    case rail::Item::MySports: return "ui-sports";
    case rail::Item::Options: return "ui-settings";
  }
  return "";
}

// Each item's centre line: the sections from kTopY, My Sports and Options
// at the bottom.
float itemY(const std::vector<rail::Item>& items, size_t i) {
  int top = 0, bottom = 0;
  for (size_t k = 0; k < i; ++k) (rail::atBottom(items[k]) ? bottom : top)++;
  return rail::atBottom(items[i]) ? kBottomY + kStep * static_cast<float>(bottom) : kTopY + kStep * static_cast<float>(top);
}

void icon(ui::Ctx& c, rail::Item i, float x, float y, Color col) {
  if (unsigned t = c.icons ? c.icons->get(iconName(i)) : 0) {
    c.g.textureAlpha(t, {x, y, 32, 32}, col);
    return;
  }
  // Until the icon loads: a ring.
  c.g.stroke({x + 6, y + 6, 20, 20}, col, 10, 2.6f);
}

// The Marigold mark: three rings of the flower's colours.
void mark(ui::Ctx& c, float cx, float cy, float r) {
  c.g.fill({cx - r, cy - r, 2 * r, 2 * r}, theme::hex(0xE8891A), r);
  float m = r * 0.69f, i = r * 0.31f;
  c.g.fill({cx - m, cy - m, 2 * m, 2 * m}, theme::hex(0xF2A03D), m);
  c.g.fill({cx - i, cy - i, 2 * i, 2 * i}, theme::hex(0x7A3B00), i);
}

// The navigation colour's right edge: a hairline, then a soft shadow.
void edge(ui::Ctx& c, float x, float spread, float a) {
  c.g.fill({x, 0, 1, gfx::kLayoutHeight}, theme::white(0.14f));
  for (int k = 0; k < 4; ++k) {
    float w = spread / 4;
    c.g.fill({x + 1 + w * static_cast<float>(k), 0, w, gfx::kLayoutHeight}, theme::black(a * (1.0f - 0.25f * static_cast<float>(k))));
  }
}

}  // namespace

void Slide::go(float target, uint32_t ms) {
  if (target == to_) return;
  from_ = at(ms);
  to_ = target;
  start_ = ms;
}

float Slide::at(uint32_t ms) const {
  if (landed(ms)) return to_;
  float t = static_cast<float>(ms - start_) / static_cast<float>(kSlideMs);
  float e = 1 - (1 - t) * (1 - t) * (1 - t);  // ease out
  return from_ + (to_ - from_) * e;
}

void strip(ui::Ctx& c, const std::vector<rail::Item>& items, rail::Item current, int hover) {
  c.g.fill({0, 0, kStripW, gfx::kLayoutHeight}, theme::hex(kNavColour));
  edge(c, kStripW, 18, 0.12f);
  mark(c, 52, 60, 16);
  for (size_t i = 0; i < items.size(); ++i) {
    float y = itemY(items, i);
    bool sel = items[i] == current;
    c.hit({0, y - kStep * 0.5f, kStripW, kStep}, kHitStrip + static_cast<int>(i));
    icon(c, items[i], 36, y - 16, sel ? theme::white() : theme::white(0.5f));
    if (sel) c.g.fill({6, y - 14, 4, 28}, theme::white(), 2);
  }
  c.g.fill({30, kRuleY, 44, 1}, theme::white(0.12f));
  if (hover < 0 || hover >= static_cast<int>(items.size())) return;
  // The pointer on an icon: its name in a pill; nothing moves.
  float y = itemY(items, static_cast<size_t>(hover));
  rail::Item it = items[static_cast<size_t>(hover)];
  c.g.fill({22, y - 30, 60, 60}, theme::white(), 30);
  icon(c, it, 36, y - 16, theme::black());
  float w = ui::textWidth(c, rail::title(it), Weight::Bold, 25) + 48;
  Rect pill{96, y - 27, w, 54};
  c.g.fill({pill.x - 2, pill.y + 6, pill.w + 4, pill.h + 6}, theme::black(0.3f), 30);
  c.g.fill(pill, theme::white(), 27);
  ui::labelIn(c, rail::title(it), {pill.x + 24, pill.y, w - 48, pill.h}, Weight::Bold, 25, theme::black());
}

void menu(ui::Ctx& c, const std::vector<rail::Item>& items, rail::Item current, int focus, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  // Over the dimmed hero and grid; a click there closes it.
  c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::black(0.55f * t));
  c.hit({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, kHitOutside);
  float dx = std::round(-kMenuW * (1 - t));
  c.g.fill({dx, 0, kMenuW, gfx::kLayoutHeight}, theme::hex(kNavColour));
  edge(c, dx + kMenuW, 60, 0.16f * t);
  c.hit({dx, 0, kMenuW, gfx::kLayoutHeight}, kHitNothing);
  mark(c, dx + 49, 60, 13);
  ui::labelIn(c, "Marigold", {dx + 74, 44, 300, 32}, Weight::Bold, 24, theme::white(0.85f));
  for (size_t i = 0; i < items.size(); ++i) {
    float y = itemY(items, i);
    bool f = static_cast<int>(i) == focus;
    bool sel = items[i] == current;
    c.hit({dx, y - kStep * 0.5f, kMenuW, kStep}, kHitMenu + static_cast<int>(i));
    if (f) c.g.fill({dx + 20, y - 30, kMenuW - 44, 60}, theme::white(), 30);
    Color col = f ? theme::black() : sel ? theme::white() : theme::white(0.66f);
    icon(c, items[i], dx + 36, y - 16, col);
    ui::labelIn(c, rail::title(items[i]), {dx + 90, y - 30, kMenuW - 120, 60}, Weight::Bold, 26, col);
    if (sel && !f) c.g.fill({dx + 6, y - 14, 4, 28}, theme::white(), 2);
  }
  c.g.fill({dx + 36, kRuleY, kMenuW - 72, 1}, theme::white(0.12f));
}

void column(ui::Ctx& c, const std::string& section, const char* word, const std::vector<Entry>& entries, int focus,
            int origin, int hover, float top, float first, float alpha) {
  alpha = std::clamp(alpha, 0.0f, 1.0f);
  std::string head = section;
  for (char& ch : head) ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
  ui::labelIn(c, head, {kColumnX, top, kColumnW, 40}, Weight::Bold, 17, theme::white(0.55f * alpha));
  ui::labelIn(c, word, {kColumnX, top, kColumnW, 40}, Weight::Medium, 16, theme::white(0.4f * alpha), Align::Right);
  c.g.fill({kColumnX + kColumnW + 14, top, 1, kColumnBottom - top}, theme::white(0.1f * alpha));
  // The list scrolls to keep the focus in view.
  int n = static_cast<int>(entries.size());
  int visible = std::max(1, static_cast<int>((kColumnBottom - first + kEntryStep - kEntryH) / kEntryStep));
  int from = std::clamp(focus - visible / 2, 0, std::max(0, n - visible));
  float y = first;
  for (int i = from; i < n && i < from + visible; ++i, y += kEntryStep) {
    const Entry& e = entries[static_cast<size_t>(i)];
    Rect pill{kColumnX - 12, y, kColumnW + 4, kEntryH};
    bool f = i == focus;
    c.hit(pill, kHitColumn + i);
    if (f) c.g.fill(pill, theme::white(alpha), kEntryH * 0.5f);
    else if (i == hover) c.g.fill(pill, theme::white(0.12f * alpha), kEntryH * 0.5f);
    float x = pill.x + (f ? 18 : 12);
    float right = pill.x + pill.w - (f ? 18 : 12);
    if (i == origin) {
      c.g.fill({x, y + kEntryH * 0.5f - 5, 10, 10}, theme::hex(theme::kNowLine, alpha), 5);
      x += 20;
    }
    if (e.live > 0) {
      std::string live = std::to_string(e.live) + " live";
      float lw = ui::textWidth(c, live, Weight::Bold, 17);
      ui::labelIn(c, live, {right - lw, y, lw, kEntryH}, Weight::Bold, 17,
                  theme::hex(f ? theme::kLive : theme::kWatching, alpha));
      right -= lw + 10;
    }
    ui::labelIn(c, e.title, {x, y, right - x, kEntryH}, Weight::Bold, 22, f ? theme::black(alpha) : theme::white(0.8f * alpha));
  }
}

}  // namespace marigold::app::left_rail
