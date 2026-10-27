#include "app/ui/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "platform/icons.hpp"
#include "platform/logo_cache.hpp"

namespace marigold::ui {

float label(Ctx& c, const std::string& s, float x, float y, Weight w, int px, Color col,
            float maxWidth, Align a) {
  if (s.empty() || col.a <= 0) return 0;
  return c.t.draw(c.g, s, x, y, w, px, col, maxWidth, a);
}

float labelIn(Ctx& c, const std::string& s, Rect r, Weight w, int px, Color col, Align a) {
  float h = c.t.lineHeight(w, px);
  return label(c, s, r.x, r.y + (r.h - h) * 0.5f, w, px, col, r.w, a);
}

float paragraph(Ctx& c, const std::string& s, float x, float y, float width, Weight w, int px,
                Color col, int maxLines, float lineGap) {
  std::vector<std::string> lines = c.t.wrap(s, w, px, width, maxLines);
  float step = std::round(static_cast<float>(px) * lineGap);
  for (size_t i = 0; i < lines.size(); ++i)
    label(c, lines[i], x, y + step * static_cast<float>(i), w, px, col, width);
  return step * static_cast<float>(lines.size());
}

float textWidth(Ctx& c, const std::string& s, Weight w, int px) { return c.t.measure(s, w, px); }

float chipWidth(Ctx& c, const std::string& s, int px, Weight w) {
  return std::round(c.t.measure(s, w, px) + static_cast<float>(px) * 0.9f);
}

float chip(Ctx& c, const std::string& s, float x, float y, int px, Color fill, Color textCol,
           bool outline, Weight w) {
  float h = std::round(static_cast<float>(px) * 1.5f);
  float width = chipWidth(c, s, px, w);
  Rect r{x, y, width, h};
  if (outline) c.g.stroke(r, fill, h * 0.5f, 2);
  else c.g.fill(r, fill, std::min(h * 0.5f, 6.0f));
  labelIn(c, s, r, w, px, textCol, Align::Center);
  return width;
}

void logo(Ctx& c, const std::string& url, const std::string& fallback, Rect r, bool onLight,
          float opacity) {
  logos::Texture tex;
  if (c.logos && !url.empty()) tex = c.logos->get(url);
  if (tex.id && tex.w > 0 && tex.h > 0) {
    float s = std::min(r.w / static_cast<float>(tex.w), r.h / static_cast<float>(tex.h));
    float w = std::round(static_cast<float>(tex.w) * s), h = std::round(static_cast<float>(tex.h) * s);
    Rect at{std::round(r.x + (r.w - w) * 0.5f), std::round(r.y + (r.h - h) * 0.5f), w, h};
    if (onLight && tex.tone == px::Tone::light) {
      // White on a white cell: drawn as a dark silhouette.
      c.g.textureAlpha(tex.id, at, theme::black(0.85f * opacity));
      return;
    }
    if (onLight && tex.tone == px::Tone::mixed) {
      // Partly white: on a small dark plate, colours kept.
      c.g.fill({at.x - 6, at.y - 4, at.w + 12, at.h + 8}, theme::hex(theme::kBackground, opacity), 8);
    }
    c.g.image(tex.id, at, opacity);
    return;
  }
  if (fallback.empty()) return;
  // Name in bold, shrunk to fit, like the tvOS fallback pill.
  int px = static_cast<int>(std::min(r.h * 0.55f, 30.0f));
  while (px > 12 && c.t.measure(fallback, Weight::Bold, px) > r.w - 8) --px;
  Color col = onLight ? theme::black(0.85f * opacity) : theme::white(0.9f * opacity);
  labelIn(c, fallback, r, Weight::Bold, px, col, Align::Center);
}

void progress(Ctx& c, Rect r, double fraction, Color track, Color fill) {
  fraction = std::clamp(fraction, 0.0, 1.0);
  c.g.fill(r, track, r.h * 0.5f);
  if (fraction > 0)
    c.g.fill({r.x, r.y, std::max(r.h, std::round(r.w * static_cast<float>(fraction))), r.h}, fill,
             r.h * 0.5f);
}

float keyCap(Ctx& c, float x, float y, float h, const std::vector<std::string>& icons, const std::string& text,
             Color fill, Color ink) {
  float pad = h * 0.24f, icon = h * 0.62f, gap = h * 0.1f;
  float inner = 0;
  for (size_t i = 0; i < icons.size(); ++i) inner += icon + (i ? gap : 0);
  int px = static_cast<int>(h * 0.5f);
  if (icons.empty()) inner = textWidth(c, text, Weight::Bold, px);
  float w = std::max(h, inner + 2 * pad);
  c.g.fill({x, y, w, h}, fill, h * 0.23f);
  if (icons.empty()) {
    labelIn(c, text, {x, y, w, h}, Weight::Bold, px, ink, Align::Center);
    return w;
  }
  float ix = x + (w - inner) * 0.5f;
  for (const std::string& name : icons) {
    unsigned tex = c.icons ? c.icons->get(name) : 0;
    if (tex) c.g.textureAlpha(tex, {ix, y + (h - icon) * 0.5f, icon, icon}, ink);
    ix += icon + gap;
  }
  return w;
}

uint32_t keyColour(hints::Colour colour) {
  switch (colour) {
    case hints::Colour::Red: return theme::kKeyRed;
    case hints::Colour::Green: return theme::kKeyGreen;
    case hints::Colour::Yellow: return theme::kKeyYellow;
    case hints::Colour::Blue: return theme::kKeyBlue;
    case hints::Colour::None: break;
  }
  return 0;
}

void keyBar(Ctx& c, const hints::Bar& bar) {
  float y = gfx::kLayoutHeight - 30 - 44;
  float x = theme::kMarginX;
  Color col = theme::white(0.75f);
  for (const hints::Hint& k : bar.keys) {
    if (k.colour != hints::Colour::None) {
      c.g.fill({x, y + 13, 18, 18}, theme::hex(keyColour(k.colour)), 4);
      x += 28;
    } else {
      x += keyCap(c, x, y + 7, 30, k.icons, k.cap, theme::white(0.16f), theme::white()) + 10;
    }
    x += label(c, k.label, x, y + 22 - c.t.lineHeight(Weight::Medium, 19) * 0.5f, Weight::Medium, 19, col);
    x += 30;
  }
  if (!bar.right.empty()) {
    float w = gfx::kLayoutWidth - theme::kMarginX - x;
    labelIn(c, bar.right, {x, y, w, 44}, Weight::Medium, 19, col, Align::Right);
  }
}

void spinner(Ctx& c, float x, float y, float radius, Color col) {
  // Eight dots, the brightest one turning once a second.
  int lead = static_cast<int>((c.ms / 125) % 8);
  for (int i = 0; i < 8; ++i) {
    float ang = static_cast<float>(i) * 0.785398f;
    float d = radius * 0.22f;
    float px = x + std::cos(ang) * radius - d * 0.5f;
    float py = y + std::sin(ang) * radius - d * 0.5f;
    int age = (lead - i + 8) % 8;
    c.g.fill({px, py, d, d}, col.withAlpha(col.a * (1.0f - static_cast<float>(age) * 0.11f)), d * 0.5f);
  }
}

void sidePanel(Ctx& c, Rect r, bool leading) {
  // No blur on these GPUs: a dark fill with a soft ramp on the open edge.
  float ramp = std::round(r.w * 0.16f);
  constexpr int kSteps = 12;
  if (leading) {
    c.g.fill({r.x, r.y, r.w - ramp, r.h}, theme::black(0.82f));
    for (int i = 0; i < kSteps; ++i) {
      float a = 0.82f * (1.0f - static_cast<float>(i + 1) / (kSteps + 1));
      c.g.fill({r.x + r.w - ramp + ramp * static_cast<float>(i) / kSteps, r.y, ramp / kSteps + 1, r.h},
               theme::black(a));
    }
  } else {
    c.g.fill({r.x + ramp, r.y, r.w - ramp, r.h}, theme::black(0.82f));
    for (int i = 0; i < kSteps; ++i) {
      float a = 0.82f * static_cast<float>(i + 1) / (kSteps + 1);
      c.g.fill({r.x + ramp * static_cast<float>(i) / kSteps, r.y, ramp / kSteps + 1, r.h}, theme::black(a));
    }
  }
}

void scrim(Ctx& c, float alpha) {
  c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::black(alpha));
}

namespace {
time::Civil local(const Ctx& c, time::Seconds t) {
  return c.zone ? c.zone->toLocal(t) : time::Zone::device().toLocal(t);
}
}  // namespace

std::string clock(const Ctx& c, time::Seconds t, bool withPeriod) {
  time::Civil l = local(c, t);
  char buf[24];
  if (c.clock24) {
    std::snprintf(buf, sizeof buf, "%02d:%02d", l.hour, l.minute);
    return buf;
  }
  int h = l.hour % 12;
  if (h == 0) h = 12;
  if (withPeriod) std::snprintf(buf, sizeof buf, "%d:%02d %s", h, l.minute, l.hour < 12 ? "AM" : "PM");
  else std::snprintf(buf, sizeof buf, "%d:%02d", h, l.minute);
  return buf;
}

std::string range(const Ctx& c, time::Seconds a, time::Seconds b, bool withPeriod) {
  return clock(c, a, false) + " \xe2\x80\x93 " + clock(c, b, withPeriod);
}

std::string dayWord(const Ctx& c, time::Seconds t) {
  time::Civil n = local(c, c.now), l = local(c, t);
  int64_t dn = time::daysFromCivil(n.year, n.month, n.day);
  int64_t dl = time::daysFromCivil(l.year, l.month, l.day);
  if (dl == dn) return l.hour >= 17 ? "Tonight" : "";
  if (dl == dn + 1) return "Tomorrow";
  static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  int wd = time::weekday(l.year, l.month, l.day);
  return wd >= 0 && wd < 7 ? kDays[wd] : "";
}

}  // namespace marigold::ui
