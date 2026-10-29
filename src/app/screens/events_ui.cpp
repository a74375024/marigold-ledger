#include "app/screens/events_ui.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "platform/icons.hpp"
#include "platform/logo_cache.hpp"

namespace marigold::app::events_ui {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr uint32_t kDelayed = 0xE0A100;
constexpr uint32_t kPostponed = 0xE07A1F;

bool isLight(uint32_t rgb) {
  float r = static_cast<float>((rgb >> 16) & 0xff), g = static_cast<float>((rgb >> 8) & 0xff),
        b = static_cast<float>(rgb & 0xff);
  return 0.299f * r + 0.587f * g + 0.114f * b > 170.0f;
}

float smallPill(ui::Ctx& c, const std::string& text, float x, float y, Color fill, Color fg, bool outline,
                int px = 16) {
  float h = 26;
  float w = std::round(ui::textWidth(c, text, Weight::Bold, px) + 16);
  Rect r{x, y, w, h};
  if (outline) c.g.stroke(r, fill, 5, 2);
  else c.g.fill(r, fill, 5);
  ui::labelIn(c, text, r, Weight::Bold, px, fg, Align::Center);
  return w;
}

// A base as a diamond `half` wide each way from its centre, drawn in thin
// rows (the renderer has no rotation): filled for a runner, an outline
// otherwise.
void base(ui::Ctx& c, float cx, float cy, float half, bool runner, Color ink) {
  constexpr float kRow = 1.0f, kEdge = 2.0f;
  for (float dy = -half; dy < half; dy += kRow) {
    float mid = dy + kRow * 0.5f;
    float w = half - std::fabs(mid);
    if (w <= 0) continue;
    float y = cy + dy;
    if (runner || w <= kEdge * 1.5f) {
      c.g.fill({cx - w, y, 2 * w, kRow}, runner ? ink : ink.withAlpha(0.45f));
    } else {
      c.g.fill({cx - w, y, kEdge, kRow}, ink.withAlpha(0.45f));
      c.g.fill({cx + w - kEdge, y, kEdge, kRow}, ink.withAlpha(0.45f));
    }
  }
}

// Runners on base: second at the top, third left, first right (30 x 24).
void bases(ui::Ctx& c, const std::vector<bool>& on, float x, float y, Color ink) {
  constexpr float kHalf = 6.0f;
  base(c, x + 15.0f, y + 6.5f, kHalf, on[1], ink);
  base(c, x + 6.5f, y + 15.5f, kHalf, on[2], ink);
  base(c, x + 23.5f, y + 15.5f, kHalf, on[0], ink);
}

void countryChip(ui::Ctx& c, const std::string& code, float x, float y, bool focused) {
  Rect r{x, y, 32, 22};
  c.g.fill(r, focused ? theme::black(0.12f) : theme::white(0.16f), 4);
  ui::labelIn(c, code, r, Weight::Bold, 13, focused ? theme::black() : theme::white(), Align::Center);
}

// A tennis player's country flag, or a doubles pair's two, in a row 40 tall
// at (x, y). The images are square with the flag across the middle, so the
// square is drawn whole. The country chip (or blank flags) until they load.
// Returns the width used.
float flags(ui::Ctx& c, const EventSide& s, float x, float y, bool focused) {
  bool pair = s.flags.size() >= 2;
  float size = pair ? 24.0f : 38.0f, step = size + 2;
  size_t n = pair ? 2 : 1;
  bool loaded = true;
  std::vector<logos::Texture> tex(n);
  for (size_t i = 0; i < n; ++i) {
    if (c.logos) tex[i] = c.logos->get(s.flags[i]);
    loaded = loaded && tex[i].id;
  }
  if (!loaded) {
    if (!pair && !s.country.empty()) {
      countryChip(c, s.country, x + 3, y + 9, focused);
    } else {
      Color blank = focused ? theme::black(0.1f) : theme::white(0.16f);
      for (size_t i = 0; i < n; ++i) c.g.fill({x + step * static_cast<float>(i) + 1, y + 12.5f, size - 2, size * 0.64f}, blank, 2);
    }
    return pair ? step * 2 - 2 : size - 2;
  }
  for (size_t i = 0; i < n; ++i) c.g.image(tex[i].id, {x + step * static_cast<float>(i), y + 20 - size * 0.5f, size, size});
  return pair ? step * 2 - 2 : size - 2;
}

void sideLine(ui::Ctx& c, const EventSide& s, Rect r, bool focused) {
  Color ink = focused ? theme::black() : theme::white();
  float x = r.x;
  if (!s.flags.empty()) {
    x += flags(c, s, x, r.y, focused) + 11;
  } else if (!s.country.empty()) {
    countryChip(c, s.country, x, r.y + 9, focused);
    x += 43;
  } else {
    badge(c, s, x, r.y + 4, 32, focused);
    x += 43;
  }
  if (!s.rank.empty()) x += ui::label(c, s.rank, x, r.y + 9, Weight::Bold, 18, ink.withAlpha(0.55f)) + 6;
  float scoreW = s.score.empty() ? 0 : ui::textWidth(c, s.score, Weight::Bold, 27) + 8;
  std::string pens = s.pens.empty() || s.score.empty() ? std::string() : "(" + s.pens + ")";
  float pensW = pens.empty() ? 0 : ui::textWidth(c, pens, Weight::Bold, 19) + 8;
  float right = r.x + r.w - scoreW - pensW - (s.ball ? 18 : 0);
  float nameW = ui::label(c, s.name, x, r.y + 6, Weight::Bold, 23, ink, right - x - (s.home ? 50 : 0));
  if (s.home) ui::label(c, "home", x + nameW + 8, r.y + 12, Weight::Regular, 16, ink.withAlpha(0.5f));
  if (!s.score.empty()) {
    if (s.ball) {
      // Possession: a small left-pointing triangle.
      float bx = r.x + r.w - scoreW - 14, by = r.y + 20;
      for (int i = 0; i < 5; ++i)
        c.g.fill({bx + static_cast<float>(i) * 1.6f, by - static_cast<float>(i), 1.8f, static_cast<float>(i) * 2.0f + 1}, ink.withAlpha(0.85f));
    }
    ui::label(c, s.score, r.x + r.w - scoreW, r.y + 2, Weight::Bold, 27, ink.withAlpha(s.trails ? 0.55f : 1.0f), scoreW,
              Align::Right);
    if (!pens.empty())
      ui::label(c, pens, r.x + r.w - scoreW - pensW, r.y + 9, Weight::Bold, 19, ink.withAlpha(s.trails ? 0.5f : 0.85f));
  }
}

// A golf leaderboard line: position, flag, name, holes played, score to
// par (the second line's score dimmed).
void leaderLine(ui::Ctx& c, const EventLeader& l, Rect r, bool focused, bool dim) {
  Color ink = focused ? theme::black() : theme::white();
  float x = r.x;
  ui::label(c, l.pos, x, r.y + 9, Weight::Bold, 18, ink.withAlpha(0.55f), 26);
  x += 30;
  logos::Texture tex;
  if (c.logos && !l.flagUrl.empty()) tex = c.logos->get(l.flagUrl);
  if (tex.id) c.g.image(tex.id, {x, r.y + 5, 30, 30});  // square, the flag across its middle
  else c.g.fill({x + 1, r.y + 10.5f, 28, 19}, focused ? theme::black(0.1f) : theme::white(0.16f), 2);
  x += 40;
  float scoreW = ui::textWidth(c, l.score, Weight::Bold, 27) + 8;
  std::string thru = l.thru.empty() ? std::string() : l.thru == "F" ? std::string("F") : "thru " + l.thru;
  float thruW = thru.empty() ? 0 : ui::textWidth(c, thru, Weight::Medium, 16) + 10;
  float right = r.x + r.w - scoreW - thruW;
  ui::label(c, l.name, x, r.y + 6, Weight::Bold, 23, ink, right - x);
  if (!thru.empty()) ui::label(c, thru, right, r.y + 12, Weight::Medium, 16, ink.withAlpha(0.6f));
  ui::label(c, l.score, r.x + r.w - scoreW, r.y + 2, Weight::Bold, 27, ink.withAlpha(dim ? 0.55f : 1.0f), scoreW, Align::Right);
}

}  // namespace

void badge(ui::Ctx& c, const EventSide& side, float x, float y, float size, bool focused, float opacity) {
  logos::Texture tex;
  if (c.logos && !side.logoUrl.empty()) tex = c.logos->get(side.logoUrl);
  if (tex.id) {
    ui::logo(c, side.logoUrl, "", {x, y, size, size}, focused, opacity);
    return;
  }
  Rect r{x, y, size, size};
  if (side.plain) {
    // A person: initials in a plain circle, no colours of a team.
    Color ink = focused ? theme::black() : theme::white();
    Rect in{x + 2, y + 2, size - 4, size - 4};
    c.g.fill(in, ink.withAlpha((focused ? 0.1f : 0.16f) * opacity), in.w * 0.5f);
    ui::labelIn(c, side.abbr, in, Weight::Bold, static_cast<int>(size * 0.38f), ink.withAlpha(0.85f * opacity), Align::Center);
    return;
  }
  c.g.fill(r, theme::hex(side.color, opacity), size * 0.5f);
  float ring = std::max(2.0f, size * 0.06f);
  c.g.stroke(r, theme::hex(side.alt, opacity), size * 0.5f, ring);
  int px = static_cast<int>(size * (side.abbr.size() > 3 ? 0.28f : 0.34f));
  ui::labelIn(c, side.abbr, r, Weight::Bold, px,
              (isLight(side.color) ? theme::black() : theme::white()).withAlpha(opacity), Align::Center);
}

float pill(ui::Ctx& c, const EventTile& t, float x, float y, bool focused) {
  Color ink = focused ? theme::black() : theme::white();
  Color outline = ink.withAlpha(0.6f);
  switch (t.pill) {
    case EventTile::Pill::None: return 0;
    case EventTile::Pill::Live: return smallPill(c, "LIVE", x, y, theme::hex(theme::kWatching), theme::white(), false);
    case EventTile::Pill::LiveOutline:
      return smallPill(c, "LIVE", x, y, theme::hex(theme::kWatching, 0.8f),
                       focused ? theme::hex(theme::kWatching) : theme::hex(0xF4B4AE), true);
    case EventTile::Pill::OnNow: return smallPill(c, "ON NOW", x, y, outline, ink, true);
    case EventTile::Pill::Delayed:
      return smallPill(c, t.pillText.empty() ? "DELAYED" : t.pillText, x, y, theme::hex(kDelayed), theme::black(), false);
    case EventTile::Pill::LikelyOver: return smallPill(c, "Likely over", x, y, outline, ink, true);
    case EventTile::Pill::Postponed: return smallPill(c, "POSTPONED", x, y, theme::hex(kPostponed), theme::black(), false);
    case EventTile::Pill::Final:
      return smallPill(c, t.pillText.empty() ? "Ended" : t.pillText, x, y, outline, ink, true);
    case EventTile::Pill::NoTime: return smallPill(c, "Time not listed", x, y, outline, ink, true);
    case EventTile::Pill::NotStarted: return smallPill(c, "Not started", x, y, outline, ink, true);
    case EventTile::Pill::NotLive: return smallPill(c, "Not live", x, y, outline, ink, true);
  }
  return 0;
}

void tile(ui::Ctx& c, const EventTile& t, Rect r, bool focused, bool mixed) {
  if (focused) {
    // Scale 1.04 with a shadow.
    float dw = r.w * 0.02f, dh = r.h * 0.02f;
    c.g.fill({r.x - dw + 4, r.y - dh + 14, r.w + 2 * dw - 8, r.h + 2 * dh}, theme::black(0.35f), 20);
    r = {r.x - dw, r.y - dh, r.w + 2 * dw, r.h + 2 * dh};
  }
  float alpha = t.watchable || focused ? 1.0f : 0.6f;
  Color fill = focused ? theme::white() : theme::white(t.watchable && !t.finished ? 0.09f : 0.06f);
  c.g.fill(r, fill, 16);
  Color ink = focused ? theme::black(alpha) : theme::white(alpha);
  float x = r.x + 16, w = r.w - 32;

  // Top line: the sport's icon and name, then the competition ("CRICKET ·
  // 3rd Test"); in a sport's own row, the competition alone. A sport the
  // app doesn't know is "LIVE EVENT" by a dashed ring, never a guess.
  float ey = r.y + 14;
  float right = x + w;
  if (t.favorite) {
    ui::label(c, "\xe2\x98\x85", right - 18, ey - 2, Weight::Bold, 18, theme::hex(theme::kStar));
    right -= 26;
  }
  if (t.tagAlert && !t.tag.empty()) {
    // "RED ZONE", "WHIP-AROUND": an alert, at the right.
    float tw = std::min(ui::textWidth(c, t.tag, Weight::Bold, 14) + 12, w * 0.45f);
    Rect tr{right - tw, ey - 2, tw, 22};
    c.g.fill(tr, theme::hex(0xC62828), 5);
    ui::labelIn(c, t.tag, {tr.x + 6, tr.y, tr.w - 12, tr.h}, Weight::Bold, 14, theme::white(), Align::Center);
    right -= tw + 8;
  }
  float ex = x;
  std::string comp = t.competition;
  if (mixed) {
    Rect ib{ex, ey - 1, 20, 20};
    if (t.sport.empty()) {
      // A dashed ring: eight dots round a circle.
      for (int i = 0; i < 8; ++i) {
        float a = static_cast<float>(i) * 0.785398f;
        c.g.fill({ib.x + 10 + 8 * std::cos(a) - 1.5f, ib.y + 10 + 8 * std::sin(a) - 1.5f, 3, 3}, ink.withAlpha(0.7f), 1.5f);
      }
    } else if (unsigned icon = c.icons ? c.icons->get(t.icon) : 0) {
      c.g.textureAlpha(icon, ib, ink);
    } else {
      ui::labelIn(c, t.symbol.empty() ? "\xe2\x97\x8f" : t.symbol, ib, Weight::Bold, 14, ink, Align::Center);
    }
    ex += 28;
    std::string word = !t.sport.empty() ? t.sport : t.highSchool ? "HIGH SCHOOL" : "LIVE EVENT";
    if (t.highSchool && !t.sport.empty()) comp = comp.empty() ? "HIGH SCHOOL" : "HIGH SCHOOL \xc2\xb7 " + comp;
    ex += ui::label(c, word, ex, ey, Weight::Bold, 16, ink, right - ex);
    if (!comp.empty()) comp = " \xc2\xb7 " + comp;
  } else if (t.highSchool) {
    comp = comp.empty() ? "HIGH SCHOOL" : "HIGH SCHOOL \xc2\xb7 " + comp;
  }
  if (!comp.empty() && ex < right) ui::label(c, comp, ex, ey, Weight::Medium, 16, ink.withAlpha(0.6f), right - ex);

  // Status line: a red dot and LIVE, then the clock; other states keep
  // their pill (Final, DELAYED, BREAK, Not live...).
  float sy = r.y + 42;
  float px = x;
  if (t.pill == EventTile::Pill::Live || t.pill == EventTile::Pill::LiveOutline) {
    // Not in your channels: the same, dimmer.
    Color red = t.pill == EventTile::Pill::Live ? (focused ? theme::hex(0xD32F2F) : theme::hex(theme::kWatching))
                                               : theme::hex(theme::kWatching, 0.6f);
    c.g.fill({px, sy + 8, 11, 11}, red, 5.5f);
    px += 17;
    px += ui::label(c, "LIVE", px, sy + 2, Weight::Bold, 19, red) + 10;
  } else {
    float pw = pill(c, t, px, sy + 1, focused);
    if (pw > 0) px += pw + 10;
  }
  if (!t.status.empty()) {
    bool bold = t.statusBold || t.pill == EventTile::Pill::Live || t.pill == EventTile::Pill::LiveOutline;
    float tw = ui::label(c, t.status, px, sy + 2, bold ? Weight::Bold : Weight::Medium, 19,
                         ink.withAlpha((bold ? 1.0f : 0.8f) * alpha), x + w - px);
    if (!t.statusExtra.empty() && px + tw + 8 < x + w) {
      Color extra = t.soon ? (focused ? theme::hex(0x9A5B00) : theme::hex(kDelayed)) : ink.withAlpha(0.7f * alpha);
      ui::label(c, t.statusExtra, px + tw + 8, sy + 2, t.soon ? Weight::Bold : Weight::Regular, 19, extra,
                x + w - px - tw - 8);
    }
  }

  // Middle.
  float my = r.y + 72;
  if (!t.parts.empty()) {
    ui::label(c, t.title, x, my, Weight::Bold, 24, ink, w);
    if (!t.subtitle.empty()) ui::label(c, t.subtitle, x, my + 34, Weight::Regular, 18, ink.withAlpha(0.7f * alpha), w);
    float pw = (w - 8 * static_cast<float>(t.parts.size() - 1)) / static_cast<float>(t.parts.size());
    float bx = x;
    for (const EventPart& p : t.parts) {
      Rect bar{bx, my + 68, pw, 6};
      c.g.fill(bar, ink.withAlpha(0.14f), 3);
      if (p.fill > 0) c.g.fill({bx, bar.y, std::max(6.0f, pw * static_cast<float>(p.fill)), 6}, ink.withAlpha(p.current ? 1 : 0.4f), 3);
      ui::label(c, p.label, bx, my + 80, p.current ? Weight::Bold : Weight::Regular, 16, ink.withAlpha(p.current ? 1 : 0.65f), pw);
      bx += pw + 8;
    }
  } else if (!t.leaders.empty()) {
    for (size_t i = 0; i < t.leaders.size() && i < 2; ++i)
      leaderLine(c, t.leaders[i], {x, my - 4 + 42 * static_cast<float>(i), w, 40}, focused, i > 0);
  } else if (t.sides.size() == 2) {
    sideLine(c, t.sides[0], {x, my - 4, w, 40}, focused);
    sideLine(c, t.sides[1], {x, my + 38, w, 40}, focused);
  } else {
    Rect box{x, my + 6, 52, 52};
    c.g.fill(box, ink.withAlpha(focused ? 0.08f : 0.12f), 12);
    // The sport's icon in the ink colour; its glyph if the file is missing.
    if (unsigned icon = c.icons ? c.icons->get(t.icon) : 0) {
      c.g.textureAlpha(icon, {box.x + 11, box.y + 11, 30, 30}, ink);
    } else {
      std::string glyph = t.symbol.empty() ? "\xe2\x97\x8f" : t.symbol;
      ui::labelIn(c, glyph, box, Weight::Bold, 22, ink, Align::Center);
    }
    std::vector<std::string> lines = c.t.wrap(t.title, Weight::Bold, 24, w - 66, t.subtitle.empty() ? 2 : 1);
    float ty = my + (lines.size() > 1 || !t.subtitle.empty() ? 4 : 18);
    for (const std::string& line : lines) {
      ui::label(c, line, x + 66, ty, Weight::Bold, 24, ink);
      ty += 30;
    }
    if (!t.subtitle.empty()) {
      ui::label(c, t.subtitle, x + 66, ty, Weight::Regular, 18, ink.withAlpha(0.7f * alpha), w - 66);
      ty += 26;
    }
    // A finished session's top three: "1 VER  2 NOR  3 LEC" in team colours.
    float podX = x + 66;
    for (size_t i = 0; i < t.podium.size(); ++i) {
      std::string place = std::to_string(i + 1);
      podX += ui::label(c, place, podX, ty + 2, Weight::Bold, 16, ink.withAlpha(0.55f * alpha)) + 4;
      c.g.fill({podX, ty + 4, 4, 18}, theme::hex(t.podium[i].color, alpha), 2);
      podX += 8;
      podX += ui::label(c, t.podium[i].code, podX, ty, Weight::Bold, 19, ink) + 14;
    }
  }

  // Bottom line: the game's own detail (runners, the chase, down and
  // distance, the court, "5 boards"); nothing about where it plays.
  float fy = r.y + r.h - 15 - 28;
  float fx = x;
  if (t.bases.size() == 3) {
    bases(c, t.bases, fx, fy + 2, ink.withAlpha(0.95f * alpha));
    fx += 30 + 10;
  }
  if (!t.situation.empty()) {
    Color sc = t.redZone ? (focused ? theme::hex(0xBF1A26) : theme::hex(theme::kWatching)) : ink.withAlpha(0.85f * alpha);
    ui::label(c, t.situation, fx, fy + 3, Weight::Bold, 18, sc, x + w - fx);
  } else if (!t.detail.empty()) {
    ui::label(c, t.detail, fx, fy + 3, Weight::Regular, 18, ink.withAlpha(0.75f * alpha), x + w - fx);
  }

  // Live progress along the bottom edge.
  if (t.progress >= 0) {
    c.g.pushClip(r);
    Rect strip{r.x, r.y + r.h - 6, r.w, 6};
    c.g.fill(strip, focused ? theme::black(0.12f) : theme::white(0.15f));
    c.g.fill({r.x, strip.y, r.w * static_cast<float>(std::clamp(t.progress, 0.0, 1.0)), 6},
             focused ? theme::black() : theme::white(0.75f));
    c.g.popClip();
  }
}

void backdrop(ui::Ctx& c, const EventTile& t, bool badges) {
  // A soft blue glow, top right (concentric translucent ovals).
  for (int i = 0; i < 10; ++i) {
    float k = static_cast<float>(i) / 10.0f;
    float w = 1100 * (1 - k * 0.6f), h = 380 * (1 - k * 0.6f);
    c.g.fill({gfx::kLayoutWidth - w * 0.85f, -h * 0.2f, w, h}, theme::hex(0x2850A0, 0.05f), h * 0.5f);
  }
  if (!badges || t.sides.size() != 2) return;
  EventSide a = t.sides[0], b = t.sides[1];
  float size = 230, y = 40, x = gfx::kLayoutWidth - 160 - size * 2 - 120;
  badge(c, a, x, y, size, false, 0.4f);
  ui::label(c, "VS", x + size + 18, y + size * 0.5f - 28, Weight::Bold, 44, theme::white(0.3f));
  badge(c, b, x + size + 120, y, size, false, 0.4f);
}

}  // namespace marigold::app::events_ui
