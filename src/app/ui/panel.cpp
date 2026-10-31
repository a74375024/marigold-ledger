#include "app/ui/panel.hpp"

#include <algorithm>
#include <cmath>

namespace marigold::ui {

namespace {
constexpr int kNoteLines = 6;
constexpr int kNotePx = 18, kNoteTitlePx = 21;
constexpr float kNoteLineH = 24;

// The second line: the reason a dimmed row can't be used, else its detail.
const std::string& secondLine(const PanelItem& it) { return !it.enabled && !it.why.empty() ? it.why : it.detail; }
}  // namespace

float ListPanel::rowHeight(Ctx& c, const PanelItem& it, float width) const {
  switch (it.kind) {
    case PanelItem::Kind::Header: return 52;
    case PanelItem::Kind::Note: {
      // Notes wrap; a note with a detail is a title over its text.
      Weight w = it.detail.empty() ? Weight::Regular : Weight::Bold;
      int px = it.detail.empty() ? kNotePx : kNoteTitlePx;
      size_t lines = c.t.wrap(it.title, w, px, width - 16, kNoteLines).size();
      if (!it.detail.empty()) lines += c.t.wrap(it.detail, Weight::Regular, kNotePx, width - 16, kNoteLines).size();
      return std::max(44.0f, static_cast<float>(lines) * kNoteLineH + 20);
    }
    default: return secondLine(it).empty() ? theme::kPanelRowHeight : 84;
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
  int hovered = k == Key::Ok ? hoveredChoice_ : -1;
  hoveredChoice_ = -1;
  followFocus_ = true;
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
  if (focus_ < 0 || focus_ >= static_cast<int>(items.size())) return false;
  PanelItem& it = items[static_cast<size_t>(focus_)];
  if (it.kind == PanelItem::Kind::Choice && !it.choices.empty() &&
      (k == Key::Left || k == Key::Right || k == Key::Ok)) {
    int n = static_cast<int>(it.choices.size());
    int next = k == Key::Left ? std::max(0, it.choice - 1)
               : k == Key::Right ? std::min(n - 1, it.choice + 1)
               : hovered >= 0 && hovered < n ? hovered  // a click on one of them
                                 : (it.choice + 1) % n;
    if (next != it.choice && it.enabled) {
      it.choice = next;
      if (it.pick) it.pick(next);
    }
    return true;
  }
  if (k == Key::Ok) {
    if (it.focusable() && it.select) it.select();
    return true;
  }
  return false;
}

void ListPanel::hover(int id) {
  int i = id - kHitBase, choice = -1;
  if (i >= kChoiceHits) {
    choice = (i - kChoiceHits) % 8;
    i = (i - kChoiceHits) / 8;
  }
  if (i >= 0 && i < static_cast<int>(items.size()) && items[static_cast<size_t>(i)].focusable()) {
    focus_ = i;
    hoveredChoice_ = choice;
    // The rows stay where they are under the pointer.
    followFocus_ = false;
  }
}

void ListPanel::scrollBy(float dy) {
  followFocus_ = false;
  scroll_ = std::clamp(scroll_ + dy, 0.0f, maxScroll_);
}

void ListPanel::draw(Ctx& c, Rect r, int hitBase, bool showFocus) {
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
    float h = rowHeight(c, items[i], r.w) + 6;
    if (static_cast<int>(i) == focus_) {
      focusTop = total;
      focusH = h;
    }
    total += h;
  }
  if (focus_ >= 0 && followFocus_) {
    if (focusTop - 70 < scroll_) scroll_ = std::max(0.0f, focusTop - 70);
    if (focusTop + focusH + 70 > scroll_ + list.h) scroll_ = focusTop + focusH + 70 - list.h;
  }
  maxScroll_ = std::max(0.0f, total - list.h);
  scroll_ = std::clamp(scroll_, 0.0f, maxScroll_);

  c.g.pushClip(list);
  float ry = list.y - scroll_;
  for (size_t i = 0; i < items.size(); ++i) {
    const PanelItem& it = items[i];
    float h = rowHeight(c, it, list.w);
    Rect row{list.x, ry, list.w, h};
    ry += h + 6;
    if (row.y + row.h < list.y || row.y > list.y + list.h) continue;
    bool focused = showFocus && static_cast<int>(i) == focus_;
    if (it.kind == PanelItem::Kind::Header) {
      label(c, it.title, row.x + 8, row.y + 20, Weight::Bold, 16, theme::white(0.55f));
      continue;
    }
    if (it.kind == PanelItem::Kind::Note) {
      float ny = row.y + 10;
      if (it.detail.empty()) {
        paragraph(c, it.title, row.x + 8, ny, row.w - 16, Weight::Regular, kNotePx, theme::white(0.6f), kNoteLines,
                  kNoteLineH / kNotePx);
      } else {
        for (const std::string& line : c.t.wrap(it.title, Weight::Bold, kNoteTitlePx, row.w - 16, kNoteLines)) {
          label(c, line, row.x + 8, ny, Weight::Bold, kNoteTitlePx, theme::white(0.9f));
          ny += kNoteLineH;
        }
        paragraph(c, it.detail, row.x + 8, ny, row.w - 16, Weight::Regular, kNotePx, theme::white(0.6f), kNoteLines,
                  kNoteLineH / kNotePx);
      }
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
      case PanelItem::Kind::Choice: {
        float segW = 0;
        for (const std::string& s : it.choices) segW += textWidth(c, s, Weight::Bold, 18) + 28;
        Rect box{right - segW - 8, row.y + (row.h - 44) * 0.5f, segW + 8, 44};
        c.g.fill(box, focused ? theme::black(0.08f) : theme::white(0.08f), 12);
        float sx = box.x + 4;
        for (size_t k = 0; k < it.choices.size(); ++k) {
          float w = textWidth(c, it.choices[k], Weight::Bold, 18) + 28;
          Rect seg{sx, box.y + 4, w, 36};
          if (k < 8 && it.enabled)
            c.hit({seg.x, row.y, seg.w, row.h}, hitBase + kChoiceHits + static_cast<int>(i * 8 + k));
          bool on = static_cast<int>(k) == it.choice;
          if (on) c.g.fill(seg, focused ? theme::black() : theme::white(0.22f), 9);
          Color ink = on ? theme::white() : (focused ? theme::black(0.55f) : theme::white(0.6f));
          labelIn(c, it.choices[k], seg, Weight::Bold, 18, ink, Align::Center);
          sx += w;
        }
        right = box.x - 16;
        break;
      }
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
    const std::string& second = secondLine(it);
    float titleY = second.empty() ? row.y + (row.h - c.t.lineHeight(Weight::Bold, 25)) * 0.5f : row.y + 12;
    float titleW = label(c, it.title, x, titleY, Weight::Bold, 25, fg, tw);
    if (it.coast >= 0) coastChip(c, it.coast == 1, x + titleW + 12, titleY + 5, 13);
    if (!it.tag.empty()) chip(c, it.tag, x + titleW + 12, titleY + 3, 14, theme::hex(theme::kNew), theme::white());
    if (&second == &it.why) label(c, second, x, row.y + 46, Weight::Medium, 17, theme::hex(theme::kWarning), tw);
    else if (!second.empty()) label(c, second, x, row.y + 46, Weight::Medium, 19, sub, tw);
  }
  c.g.popClip();
  if (!hint.empty())
    labelIn(c, hint, {r.x, r.y + r.h - hintH, r.w, hintH}, Weight::Regular, 18, theme::white(0.55f));
}

}  // namespace marigold::ui
