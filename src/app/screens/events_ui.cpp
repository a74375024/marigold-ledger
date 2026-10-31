#include "app/screens/events_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "core/sports/game_banner.hpp"
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
constexpr uint32_t kPlayerLive = 0xE5372B;  // the player banner's LIVE

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

// The rows inside a tile.
constexpr float kRowH = 34;   // a team, player or golf line
constexpr float kLineGap = 4;
constexpr float kMarkW = 16;  // the winner's caret or possession, after the score

// A team (or player) row, 34 tall: badge, rank, name, and the score at the
// right; tennis and volleyball sets in columns. `dim`: this side trails
// (live) or lost (final).
void sideLine(ui::Ctx& c, const EventSide& s, Rect r, bool focused, float alpha, bool dim, bool winner, bool live) {
  Color ink = focused ? theme::black(alpha) : theme::white(alpha);
  Color soft = ink.withAlpha(ink.a * (dim ? 0.5f : 1.0f));
  float x = r.x;
  if (!s.flags.empty()) {
    x += flags(c, s, x, r.y - 3, focused) + 10;
  } else if (!s.country.empty()) {
    countryChip(c, s.country, x, r.y + 6, focused);
    x += 42;
  } else {
    badge(c, s, x, r.y + 3, 28, focused);
    x += 38;
  }
  if (!s.rank.empty()) x += ui::label(c, s.rank, x, r.y + 8, Weight::Bold, 15, ink.withAlpha(ink.a * 0.55f)) + 6;
  float right = r.x + r.w;
  float scoreLeft = right;
  if (!s.score.empty()) {
    right -= kMarkW;
    if (s.score.find(' ') != std::string::npos) {
      // Sets: one column each, the set on now bold.
      std::vector<std::string> sets;
      for (size_t i = 0; i < s.score.size();) {
        size_t j = s.score.find(' ', i);
        if (j == std::string::npos) j = s.score.size();
        if (j > i) sets.push_back(s.score.substr(i, j - i));
        i = j + 1;
      }
      float cx = right + kMarkW;
      for (size_t k = sets.size(); k-- > 0;) {
        bool now = live && k + 1 == sets.size();
        ui::label(c, sets[k], cx - 26, r.y + 4, now ? Weight::Bold : Weight::Medium, 21,
                  now ? soft : soft.withAlpha(soft.a * 0.6f), 26, Align::Right);
        cx -= 32;
      }
      scoreLeft = cx + 6;
    } else {
      float w = ui::textWidth(c, s.score, Weight::Bold, 25);
      ui::label(c, s.score, right - w, r.y + 2, Weight::Bold, 25, soft, w + 2);
      scoreLeft = right - w;
      if (!s.pens.empty()) {
        std::string pens = "(" + s.pens + ")";
        float pw = ui::textWidth(c, pens, Weight::Bold, 16);
        ui::label(c, pens, scoreLeft - pw - 8, r.y + 7, Weight::Bold, 16, soft.withAlpha(soft.a * 0.85f));
        scoreLeft -= pw + 8;
      }
      // After the score: the winner's caret, or who has the ball.
      if (winner || (live && s.ball)) {
        // A small left-pointing triangle, in one-pixel columns.
        int half = winner ? 6 : 4;
        float bx = right + 5, by = r.y + 17;
        for (int i = 0; i <= half; ++i)
          c.g.fill({bx + static_cast<float>(i), by - static_cast<float>(i), 1.2f, 2.0f * static_cast<float>(i) + 1},
                   ink.withAlpha(ink.a * 0.85f));
      }
    }
  }
  float nameW = ui::label(c, s.name, x, r.y + 5, Weight::Bold, 21, soft, scoreLeft - x - 10 - (s.home ? 50 : 0));
  if (s.home) ui::label(c, "home", x + nameW + 8, r.y + 10, Weight::Regular, 15, ink.withAlpha(ink.a * 0.5f));
}

// A golf leaderboard line, 34 tall: position, flag, name, holes played and
// the score to par (the second line's dimmed).
void leaderLine(ui::Ctx& c, const EventLeader& l, Rect r, bool focused, float alpha, bool dim) {
  Color ink = focused ? theme::black(alpha) : theme::white(alpha);
  float x = r.x;
  ui::label(c, l.pos, x, r.y + 8, Weight::Bold, 15, ink.withAlpha(ink.a * 0.55f), 24);
  x += 26;
  logos::Texture tex;
  if (c.logos && !l.flagUrl.empty()) tex = c.logos->get(l.flagUrl);
  if (tex.id) c.g.image(tex.id, {x, r.y + 3, 28, 28});  // square, the flag across its middle
  else c.g.fill({x + 1, r.y + 8, 26, 17}, focused ? theme::black(0.1f) : theme::white(0.16f), 2);
  x += 38;
  float scoreW = ui::textWidth(c, l.score, Weight::Bold, 25);
  float right = r.x + r.w - kMarkW;
  std::string thru = l.thru.empty() ? std::string() : l.thru == "F" ? std::string("F") : "thru " + l.thru;
  float thruW = thru.empty() ? 0 : ui::textWidth(c, thru, Weight::Medium, 14) + 12;
  ui::label(c, l.name, x, r.y + 5, Weight::Bold, 21, ink, right - scoreW - thruW - x - 6);
  if (!thru.empty()) ui::label(c, thru, right - scoreW - thruW, r.y + 10, Weight::Medium, 14, ink.withAlpha(ink.a * 0.6f));
  ui::label(c, l.score, right - scoreW, r.y + 2, Weight::Bold, 25, ink.withAlpha(ink.a * (dim ? 0.55f : 1.0f)), scoreW + 2);
}

// A card's or race weekend's parts as a track: each part a short bar with
// its name under it. Done parts dim, the one on now filled (red while
// live), the rest empty; the current one's name bold.
void timeline(ui::Ctx& c, const std::vector<EventPart>& parts, Rect r, bool focused, float alpha, bool live) {
  Color ink = focused ? theme::black(alpha) : theme::white(alpha);
  Color red = focused ? theme::hex(0xD32F2F, alpha) : theme::hex(theme::kWatching, alpha);
  constexpr float kGap = 6;
  float n = static_cast<float>(parts.size());
  float w = (r.w - kGap * (n - 1)) / n;
  float x = r.x;
  for (const EventPart& p : parts) {
    Rect bar{x, r.y + 2, w, 5};
    c.g.fill(bar, ink.withAlpha(ink.a * 0.14f), 2.5f);
    double fill = std::clamp(p.fill, 0.0, 1.0);
    if (fill > 0) {
      Color col = p.current ? (live ? red : ink) : ink.withAlpha(ink.a * 0.45f);
      c.g.fill({x, bar.y, std::max(5.0f, w * static_cast<float>(fill)), 5}, col, 2.5f);
    }
    ui::label(c, p.label, x, r.y + 12, p.current ? Weight::Bold : Weight::Medium, 14,
              ink.withAlpha(ink.a * (p.current ? 1.0f : 0.55f)), w);
    x += w + kGap;
  }
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

float mark(ui::Ctx& c, const EventSide& s, float x, float y, float size) {
  if (!s.flags.empty()) {
    // The images are square with the flag across the middle: drawn whole.
    bool pair = s.flags.size() >= 2;
    float each = pair ? std::round(size * 0.62f) : size, gap = pair ? 4.0f : 0.0f;
    size_t n = pair ? 2 : 1;
    std::vector<logos::Texture> tex(n);
    bool loaded = true;
    for (size_t i = 0; i < n; ++i) {
      if (c.logos) tex[i] = c.logos->get(s.flags[i]);
      loaded = loaded && tex[i].id;
    }
    float fy = y + (size - each) * 0.5f;
    for (size_t i = 0; i < n; ++i) {
      float fx = x + (each + gap) * static_cast<float>(i);
      if (loaded) {
        c.g.image(tex[i].id, {fx, fy, each, each});
      } else {
        Rect blank{fx + 1, fy + each * 0.17f, each - 2, each * 0.66f};
        c.g.fill(blank, theme::white(0.16f), 4);
        if (!pair && !s.country.empty())
          ui::labelIn(c, s.country, blank, Weight::Bold, static_cast<int>(size * 0.3f), theme::white(), Align::Center);
      }
    }
    return each * static_cast<float>(n) + gap * static_cast<float>(n - 1);
  }
  if (!s.country.empty()) {
    Rect r{x, y + size * 0.17f, size, size * 0.66f};
    c.g.fill(r, theme::white(0.16f), 6);
    ui::labelIn(c, s.country, r, Weight::Bold, static_cast<int>(size * 0.3f), theme::white(), Align::Center);
    return size;
  }
  badge(c, s, x, y, size, false);
  return size;
}

float clockPill(ui::Ctx& c, sports::banner::Chip chip, const std::string& text, float x, float y) {
  using sports::banner::Chip;
  switch (chip) {
    case Chip::none: return 0;
    case Chip::soon: return smallPill(c, text, x, y, theme::hex(kDelayed, 0.92f), theme::black(), false);
    case Chip::live: return smallPill(c, text, x, y, theme::hex(kPlayerLive), theme::white(), false);
    case Chip::likelyOver: return smallPill(c, text, x, y, theme::white(0.6f), theme::white(), true);
  }
  return 0;
}

void tile(ui::Ctx& c, const EventTile& t, Rect r, bool focused, bool mixed) {
  if (focused) {
    // Scale 1.04 with a shadow.
    float dw = r.w * 0.02f, dh = r.h * 0.02f;
    c.g.fill({r.x - dw + 4, r.y - dh + 12, r.w + 2 * dw - 8, r.h + 2 * dh}, theme::black(0.35f), 20);
    r = {r.x - dw, r.y - dh, r.w + 2 * dw, r.h + 2 * dh};
  }
  float alpha = t.watchable || focused ? 1.0f : 0.6f;
  Color fill = focused ? theme::white() : theme::white(t.watchable && !t.finished ? 0.09f : 0.06f);
  c.g.fill(r, fill, 16);
  Color ink = focused ? theme::black(alpha) : theme::white(alpha);
  float x = r.x + 16, w = r.w - 32;
  bool live = t.pill == EventTile::Pill::Live || t.pill == EventTile::Pill::LiveOutline;

  // Top line: the sport's icon (red while live: it is the live mark), the
  // state, and at the right the competition, an alert and the star.
  float ty = r.y + 14;
  Color red = t.pill == EventTile::Pill::LiveOutline ? theme::hex(theme::kWatching, 0.6f)
              : focused                            ? theme::hex(0xD32F2F)
                                                   : theme::hex(theme::kWatching);
  Color mark = live ? red : ink;
  Rect ib{x, ty, 21, 21};
  if (t.sport.empty() && !t.highSchool) {
    // A sport the app doesn't know: a dashed ring, never a guess.
    for (int i = 0; i < 8; ++i) {
      float a = static_cast<float>(i) * 0.785398f;
      c.g.fill({ib.x + 10.5f + 8 * std::cos(a) - 1.5f, ib.y + 10.5f + 8 * std::sin(a) - 1.5f, 3, 3}, mark, 1.5f);
    }
  } else if (unsigned icon = c.icons ? c.icons->get(t.icon) : 0) {
    c.g.textureAlpha(icon, ib, mark);
  } else {
    ui::labelIn(c, t.symbol.empty() ? "\xe2\x97\x8f" : t.symbol, ib, Weight::Bold, 14, mark, Align::Center);
  }
  float px = x + 30;
  constexpr int kStatusPx = 17;
  if (live) {
    px += ui::label(c, t.status.empty() ? std::string("LIVE") : t.status, px, ty + 1, Weight::Bold, kStatusPx, red);
  } else if (t.pill == EventTile::Pill::Final) {
    px += ui::label(c, t.pillText.empty() ? std::string("Final") : t.pillText, px, ty + 1, Weight::Bold, kStatusPx, ink);
  } else {
    float pw = pill(c, t, px, ty - 2, focused);
    if (pw > 0) px += pw + 8;
    if (!t.status.empty()) {
      bool bold = t.statusBold || pw == 0;
      px += ui::label(c, t.status, px, ty + 1, bold ? Weight::Bold : Weight::Medium, kStatusPx,
                      ink.withAlpha(ink.a * (bold ? 1.0f : 0.7f)), x + w - px);
    }
    if (!t.statusExtra.empty()) {
      Color extra = t.soon ? (focused ? theme::hex(0x9A5B00) : theme::hex(kDelayed)) : ink.withAlpha(ink.a * 0.7f);
      px += ui::label(c, " \xc2\xb7 " + t.statusExtra, px, ty + 1, t.soon ? Weight::Bold : Weight::Regular, kStatusPx, extra,
                      x + w - px);
    }
  }
  float right = x + w;
  if (t.favorite) {
    ui::label(c, "\xe2\x98\x85", right - 17, ty - 1, Weight::Bold, 17, theme::hex(theme::kStar));
    right -= 25;
  }
  if (t.tagAlert && !t.tag.empty()) {
    // "RED ZONE", "WHIP-AROUND": an alert, at the right.
    float tw = std::min(ui::textWidth(c, t.tag, Weight::Bold, 13) + 12, w * 0.4f);
    Rect tr{right - tw, ty - 1, tw, 22};
    c.g.fill(tr, theme::hex(0xC62828), 5);
    ui::labelIn(c, t.tag, {tr.x + 6, tr.y, tr.w - 12, tr.h}, Weight::Bold, 13, theme::white(), Align::Center);
    right -= tw + 8;
  }
  std::string comp = t.competition;
  if (comp.empty()) comp = !t.sport.empty() && mixed ? t.sport : t.sport.empty() && !t.highSchool ? "LIVE EVENT" : "";
  if (t.highSchool) comp = comp.empty() ? "HIGH SCHOOL" : "HIGH SCHOOL \xc2\xb7 " + comp;
  float room = right - px - 14;
  if (!comp.empty() && room > 40)
    ui::label(c, comp, right - room, ty + 3, Weight::Medium, 14, ink.withAlpha(ink.a * 0.55f), room, Align::Right);

  // The middle, centred under the top line: team rows, golf leaders, or a
  // title with its subtitle, podium or the event's timeline; then the
  // game's own detail when there is one.
  enum class Part : uint8_t { Side0, Side1, Leader0, Leader1, Title0, Title1, Subtitle, Podium, Timeline, Detail };
  struct Item {
    Part part;
    float h;
  };
  std::array<Item, 8> items{};
  size_t n = 0;
  std::vector<std::string> titleLines;
  if (t.sides.size() == 2) {
    items[n++] = {Part::Side0, kRowH};
    items[n++] = {Part::Side1, kRowH};
  } else if (!t.leaders.empty()) {
    items[n++] = {Part::Leader0, kRowH};
    if (t.leaders.size() > 1) items[n++] = {Part::Leader1, kRowH};
  } else {
    bool alone = t.subtitle.empty() && t.podium.empty() && t.parts.empty();
    titleLines = c.t.wrap(t.title, Weight::Bold, 22, w, alone ? 2 : 1);
    if (!titleLines.empty()) items[n++] = {Part::Title0, 30};
    if (titleLines.size() > 1) items[n++] = {Part::Title1, 30};
    if (!t.podium.empty()) items[n++] = {Part::Podium, 28};
    else if (!t.parts.empty()) items[n++] = {Part::Timeline, 32};
    else if (!t.subtitle.empty()) items[n++] = {Part::Subtitle, 24};
  }
  bool hasDetail = !t.situation.empty() || !t.detail.empty() || t.bases.size() == 3;
  if (hasDetail && n < items.size()) items[n++] = {Part::Detail, 22};
  float total = 0;
  for (size_t i = 0; i < n; ++i) total += items[i].h + (i ? kLineGap : 0);
  float top = r.y + 44, bottom = r.y + r.h - 14;
  float y = top + std::max(0.0f, (bottom - top - total) * 0.5f);
  for (size_t i = 0; i < n; ++i) {
    const Item& it = items[i];
    Rect row{x, y, w, it.h};
    switch (it.part) {
      case Part::Side0:
      case Part::Side1: {
        const EventSide& s = t.sides[it.part == Part::Side0 ? 0 : 1];
        sideLine(c, s, row, focused, alpha, s.trails && (live || t.finished), t.finished && s.leads, live);
        break;
      }
      case Part::Leader0:
      case Part::Leader1:
        leaderLine(c, t.leaders[it.part == Part::Leader0 ? 0 : 1], row, focused, alpha, it.part == Part::Leader1);
        break;
      case Part::Title0:
      case Part::Title1:
        ui::label(c, titleLines[it.part == Part::Title0 ? 0 : 1], x, y + 2, Weight::Bold, 22, ink, w);
        break;
      case Part::Subtitle:
        ui::label(c, t.subtitle, x, y + 2, Weight::Regular, 16, ink.withAlpha(ink.a * 0.7f), w);
        break;
      case Part::Podium: {
        // A finished session's top three: "1 VER  2 NOR  3 LEC" in team colours.
        float podX = x;
        for (size_t k = 0; k < t.podium.size(); ++k) {
          podX += ui::label(c, std::to_string(k + 1), podX, y + 5, Weight::Bold, 15, ink.withAlpha(ink.a * 0.55f)) + 4;
          c.g.fill({podX, y + 6, 4, 17}, theme::hex(t.podium[k].color, alpha), 2);
          podX += 9;
          podX += ui::label(c, t.podium[k].code, podX, y + 3, Weight::Bold, 18, ink) + 16;
        }
        break;
      }
      case Part::Timeline:
        timeline(c, t.parts, row, focused, alpha, live);
        break;
      case Part::Detail: {
        // The game's own detail: runners, the chase, down and distance,
        // the court, "5 boards"; never where it plays.
        float fx = x;
        if (t.bases.size() == 3) {
          bases(c, t.bases, fx, y - 1, ink.withAlpha(ink.a * 0.95f));
          fx += 30 + 8;
        }
        const std::string& line = !t.situation.empty() ? t.situation : t.detail;
        Color sc = t.redZone && !t.situation.empty() ? (focused ? theme::hex(0xBF1A26) : theme::hex(0xFF6B61))
                                                     : ink.withAlpha(ink.a * 0.75f);
        if (!line.empty()) ui::label(c, line, fx, y + 2, Weight::Bold, 15, sc, x + w - fx);
        break;
      }
    }
    y += it.h + kLineGap;
  }

  // Live progress along the bottom edge.
  if (t.progress >= 0) {
    c.g.pushClip(r);
    Rect strip{r.x, r.y + r.h - 5, r.w, 5};
    c.g.fill(strip, focused ? theme::black(0.12f) : theme::white(0.15f));
    c.g.fill({r.x, strip.y, r.w * static_cast<float>(std::clamp(t.progress, 0.0, 1.0)), 5},
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
