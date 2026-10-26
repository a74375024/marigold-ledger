#include "app/ui/panel.hpp"

#include <algorithm>
#include <cmath>

namespace marigold::ui {

float ListPanel::rowHeight(const PanelItem& it) const {
  switch (it.kind) {
    case PanelItem::Kind::Header: return 52;
    case PanelItem::Kind::Note: return 44;
    default: return it.detail.empty() ? theme::kPanelRowHeight : 84;
  }
}

void ListPanel::settle() {
  if (items.empty()) {
    focus_ = -1;
    return;
  }
  if (focus_ >= 0 && focus_ < static_cast<int>(items.size()) && items[static_cast<size_t>(focus_)].focusable())
    return;
  int start = std::clamp(focus_, 0, static_cast<int>(items.size()) - 1);
  for (int d = 0; d < static_cast<int>(items.size()); ++d) {
    for (int i : {start + d, start - d}) {
      if (i >= 0 && i < static_cast<int>(items.size()) && items[static_cast<size_t>(i)].focusable()) {
        focus_ = i;
        return;
      }
    }
  }
  focus_ = -1;
}

void ListPanel::focusFirst() {
  focus_ = 0;
  scroll_ = 0;
  settle();
}

void ListPanel::focusIndex(int i) {
  focus_ = i;
  settle();
}

bool ListPanel::key(Key k) {
  if (k == Key::Up || k == Key::Down) {
    int step = k == Key::Up ? -1 : 1;
    for (int i = focus_ + step; i >= 0 && i < static_cast<int>(items.size()); i += step) {
      if (items[static_cast<size_t>(i)].focusable()) {
        focus_ = i;
        return true;
      }
    }
    return true;  // at an end: stay
  }
  if (k == Key::Ok && focus_ >= 0 && focus_ < static_cast<int>(items.size())) {
    PanelItem& it = items[static_cast<size_t>(focus_)];
    if (it.focusable() && it.select) it.select();
    return true;
  }
  return false;
}

void ListPanel::hover(int id) {
  int i = id - kHitBase;
  if (i >= 0 && i < static_cast<int>(items.size()) && items[static_cast<size_t>(i)].focusable()) focus_ = i;
}

void ListPanel::draw(Ctx& c, Rect r, int hitBase) {
  float y = r.y;
  if (!subtitle.empty()) {
    label(c, subtitle, r.x, y, Weight::Bold, 16, theme::white(0.55f));
    y += 30;
  }
  if (!title.empty()) {
    label(c, title, r.x, y, Weight::Bold, 38, theme::white(), r.w);
    y += 66;
  }
  float hintH = hint.empty() ? 0 : 50;
  Rect list{r.x, y, r.w, r.y + r.h - y - hintH};

  // Keep the focused row in view, with a row of context where possible.
  float total = 0, focusTop = 0, focusH = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    float h = rowHeight(items[i]) + 6;
    if (static_cast<int>(i) == focus_) {
      focusTop = total;
      focusH = h;
    }
    total += h;
  }
  if (focus_ >= 0) {
    if (focusTop - 70 < scroll_) scroll_ = std::max(0.0f, focusTop - 70);
    if (focusTop + focusH + 70 > scroll_ + list.h) scroll_ = focusTop + focusH + 70 - list.h;
  }
  scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, total - list.h));

  c.g.pushClip(list);
  float ry = list.y - scroll_;
  for (size_t i = 0; i < items.size(); ++i) {
    const PanelItem& it = items[i];
    float h = rowHeight(it);
    Rect row{list.x, ry, list.w, h};
    ry += h + 6;
    if (row.y + row.h < list.y || row.y > list.y + list.h) continue;
    bool focused = static_cast<int>(i) == focus_;
    if (it.kind == PanelItem::Kind::Header) {
      label(c, it.title, row.x + 8, row.y + 20, Weight::Bold, 16, theme::white(0.55f));
      continue;
    }
    if (it.kind == PanelItem::Kind::Note) {
      labelIn(c, it.title, {row.x + 8, row.y, row.w - 16, row.h}, Weight::Regular, 18, theme::white(0.6f));
      continue;
    }
    c.hit(row, hitBase + static_cast<int>(i));
    float alpha = it.enabled ? 1.0f : 0.4f;
    Color fg = focused ? theme::black(alpha) : theme::white(alpha);
    Color sub = focused ? theme::black(0.6f * alpha) : theme::white(0.6f * alpha);
    c.g.fill(row, focused ? theme::white() : theme::white(0.07f), theme::kRowRadius);

    float x = row.x + 24;
    float right = row.x + row.w - 24;
    // Accessory, right to left.
    switch (it.kind) {
      case PanelItem::Kind::Toggle: {
        Rect knob{right - 66, row.y + (row.h - 38) * 0.5f, 66, 38};
        c.g.fill(knob, it.on ? theme::hex(theme::kToggleOn) : (focused ? theme::black(0.2f) : theme::white(0.25f)), 19);
        float kx = it.on ? knob.x + knob.w - 34 : knob.x + 4;
        c.g.fill({kx, knob.y + 4, 30, 30}, theme::white(), 15);
        right = knob.x - 16;
        break;
      }
      case PanelItem::Kind::Check:
        if (it.on) {
          labelIn(c, "\xe2\x9c\x93", {right - 30, row.y, 30, row.h}, Weight::Bold, 26, fg, Align::Right);
        }
        right -= 44;
        break;
      case PanelItem::Kind::Chevron:
        labelIn(c, "\xe2\x80\xba", {right - 20, row.y, 20, row.h}, Weight::Bold, 30, sub, Align::Right);
        right -= 34;
        break;
      default:
        break;
    }
    if (!it.value.empty()) {
      float vw = std::min(textWidth(c, it.value, Weight::Medium, 19), row.w * 0.4f);
      labelIn(c, it.value, {right - vw, row.y, vw, row.h}, Weight::Medium, 19, sub, Align::Right);
      right -= vw + 16;
    }
    if (it.dot) {
      c.g.fill({x, row.y + row.h * 0.5f - 6, 12, 12}, theme::hex(theme::kNowLine), 6);
      x += 22;
    }
    float tw = right - x;
    if (it.detail.empty()) {
      labelIn(c, it.title, {x, row.y, tw, row.h}, Weight::Bold, 25, fg);
    } else {
      label(c, it.title, x, row.y + 12, Weight::Bold, 25, fg, tw);
      label(c, it.detail, x, row.y + 46, Weight::Medium, 19, sub, tw);
    }
  }
  c.g.popClip();
  if (!hint.empty())
    labelIn(c, hint, {r.x, r.y + r.h - hintH, r.w, hintH}, Weight::Regular, 18, theme::white(0.55f));
}

}  // namespace marigold::ui
