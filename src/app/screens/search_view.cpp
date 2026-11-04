#include "app/screens/search_view.hpp"

#include "app/screens/events_ui.hpp"
#include "core/engine/views.hpp"

namespace marigold::app::search_view {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;
namespace views = engine::views;

// A size scaled by `k` (1: as it is).
int px(int v, float k) { return static_cast<int>(static_cast<float>(v) * k + 0.5f); }

// The badge the board has for a team, when it has a game; else initials.
EventSide teamSide(Store& s, const se::TeamEntry& t) {
  for (const std::string& gid : {t.liveGame, t.nextGame}) {
    const EventTile* g = s.eventTile(gid);
    if (!g || g->sides.size() != 2) continue;
    for (const EventSide& side : g->sides) {
      if (se::fold(side.name).empty() || t.names.empty()) continue;
      if (t.names.front().find(se::fold(side.name)) != std::string::npos) return side;
    }
  }
  EventSide side;
  side.abbr = se::initials(se::words(se::fold(t.name)));
  for (char& ch : side.abbr) ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
  return side;
}

// "230 · LIVE Bulls at Harbor Kings · Q3" or "24 · The Evening Report".
std::string channelLine(const ui::Ctx& c, Store& s, const se::Index& index, const GuideChannel& g, bool& live) {
  std::string line = g.number ? std::to_string(*g.number) : std::string();
  if (g.style == GuideChannel::Style::Local && !g.subtitle.empty())
    line += (line.empty() ? "" : " \xc2\xb7 ") + g.subtitle;
  // A live game on it, by the board (its state, never a score).
  for (const se::GameEntry& x : index.games) {
    if (x.state != se::GameEntry::State::Live || !x.watchable) continue;
    const EventTile* t = s.eventTile(x.id);
    if (t && t->channelName == g.name) {
      live = true;
      return line + (line.empty() ? "" : " \xc2\xb7 ") + matchup(*t) + (t->status.empty() ? "" : " \xc2\xb7 " + t->status);
    }
  }
  views::NowNext nn = views::nowAndNext(*s.listings(), g.id, c.now);
  if (nn.now) line += (line.empty() ? "" : " \xc2\xb7 ") + nn.now->title;
  return line;
}

// "Tonight 10:00 PM · NBX 230 · also tomorrow 7:00 AM".
std::string showLine(const ui::Ctx& c, Store& s, const Cell& cell, const GuideChannel* g) {
  const Listing& l = cell.listing;
  std::string line;
  if (s.hasShowReminder(cell.entryId, l.start)) line = "Reminder set \xe2\x9c\x93 \xc2\xb7 ";
  line += l.isOn(c.now) ? "Now until " + ui::clock(c, l.end) : whenText(c, l.start);
  if (g) line += " \xc2\xb7 " + g->name + (g->number ? " " + std::to_string(*g->number) : "");
  if (cell.also) {
    std::string day = ui::dayWord(c, cell.also);
    for (char& ch : day) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
    line += " \xc2\xb7 also " + (day.empty() ? std::string("today") : day) + " " + ui::clock(c, cell.also);
  }
  if (!l.episode.empty() && l.isOn(c.now)) line += " \xc2\xb7 " + l.episode;
  return line;
}

// "Q3 · PBL · NBX Sports", "in 20 min · …", "Ended", "Not in your channels".
std::string gameLine(const ui::Ctx& c, Store& s, const EventTile& t) {
  std::string line;
  const char* dot = " \xc2\xb7 ";
  if (s.hasReminder(t.id) && t.upcoming) line = "Reminder set \xe2\x9c\x93 \xc2\xb7 ";
  if (t.finished) line += "Ended";
  else if (liveTile(t)) line += t.status;
  else if (t.start > 0) line += whenText(c, t.start);
  if (!t.league.empty()) line += (line.empty() ? "" : dot) + t.league;
  if (!t.watchable) line += std::string(dot) + (t.footer == "Streaming only" ? "Streaming only" : "Not in your channels");
  else if (!t.channelName.empty()) line += dot + t.channelName;
  return line;
}

// "PBL · LIVE Q3 · Bulls at Harbor Kings · NBX Sports", "PBL · Next: Sat
// 7:30 PM · …", "No game in the next week".
std::string teamLine(const ui::Ctx& c, Store& s, const se::TeamEntry& t, bool& live) {
  std::string line = t.league;
  const char* dot = " \xc2\xb7 ";
  if (!t.leaguePicked) return line + dot + "Add " + t.league + " in My Sports to see their games";
  if (const EventTile* g = s.eventTile(t.liveGame)) {
    live = true;
    line += dot + g->status + dot + matchup(*g);
    line += dot + (g->watchable ? g->channelName : std::string("Not in your channels"));
    return line;
  }
  if (const EventTile* g = s.eventTile(t.nextGame)) {
    line += std::string(dot) + "Next: " + whenText(c, g->start) + dot + matchup(*g);
    if (!g->watchable) line += std::string(dot) + (g->footer == "Streaming only" ? "Streaming only" : "Not in your channels");
    else if (!g->channelName.empty()) line += dot + g->channelName;
    return line;
  }
  return line + dot + "No game in the next week";
}

// Two side badges (or the sport's mark). Returns the width used.
float badges(ui::Ctx& c, const Cell::Label& l, float x, float y, float size, float k) {
  if (l.sides.size() == 2) {
    events_ui::badge(c, l.sides[0], x, y, size);
    events_ui::badge(c, l.sides[1], x + size + 8 * k, y, size);
    return 2 * size + 28 * k;
  }
  ui::logo(c, l.logoUrl, l.logoName, {x, y, size * 1.6f, size});
  return size * 1.6f + 18 * k;
}

float teamBadge(ui::Ctx& c, const Cell::Label& l, float x, float y, float k) {
  if (!l.sides.empty()) events_ui::badge(c, l.sides.front(), x, y, 52 * k);
  return 52 * k;
}

// The top result: a type tag, a big title and what's on, beside the corner.
// `white`: focused on a white fill (results focus calmly, text light).
void drawTopCard(ui::Ctx& c, const Cell& cell, Rect box, bool white, bool ok, float k) {
  const Cell::Label& l = cell.label;
  if (l.missing) return;
  Color ink = white ? theme::black() : theme::white();
  Color sub = white ? theme::black(0.65f) : theme::white(0.75f);
  float tx = box.x + 22 * k, ty = box.y + 64 * k;
  switch (cell.kind) {
    case CellKind::Channel:
      ui::logo(c, l.logoUrl, l.logoName, {tx, ty, 112 * k, 64 * k});
      tx += 132 * k;
      break;
    case CellKind::Game: tx += badges(c, l, tx, ty, 64 * k, k); break;
    case CellKind::Team: tx += teamBadge(c, l, tx, ty, k) + 18 * k; break;
    case CellKind::Show:
      if (l.logo) ui::logo(c, l.logoUrl, l.logoName, {tx, ty, 112 * k, 64 * k});
      tx += 132 * k;
      break;
    default: break;
  }
  ui::label(c, l.cardTag, box.x + 22 * k, box.y + 18 * k, Weight::Bold, px(18, k), sub, box.w - 44 * k);
  // A long title wraps to two lines here only.
  ui::paragraph(c, l.cardTitle, tx, ty, box.x + box.w - 22 * k - tx, Weight::Bold, px(32, k), ink, 2, 1.15f);
  float lx = box.x + 22 * k, ly = box.y + box.h - 48 * k;
  if (l.live) lx += ui::chip(c, "LIVE", lx, ly + 2 * k, px(18, k), theme::hex(theme::kLive), theme::white()) + 10 * k;
  ui::label(c, ok ? l.cardLineOk : l.line, lx, ly, Weight::Medium, px(22, k), sub, box.x + box.w - 22 * k - lx);
}

}  // namespace

const char* const kOutside = "Outside your lineup";

bool liveTile(const EventTile& t) {
  if (t.upcoming || t.finished) return false;
  using P = EventTile::Pill;
  return t.pill == P::Live || t.pill == P::LiveOutline || t.pill == P::OnNow || t.pill == P::Delayed;
}

bool playsGame(const EventTile& t, time::Seconds now) {
  return (t.watchable && liveTile(t)) || views::startsSoonOnChannel(t, now);
}

std::string whenText(const ui::Ctx& c, time::Seconds t) {
  time::Seconds d = t - c.now;
  if (d > 0 && d <= 3600) return "in " + std::to_string((d + 59) / 60) + " min";
  std::string day = ui::dayWord(c, t);
  return (day.empty() ? std::string("Today") : day) + " " + ui::clock(c, t);
}

Cell cellOf(const se::Item& it, bool mark) {
  Cell c;
  switch (it.kind) {
    case se::Kind::Channel: c.kind = CellKind::Channel; break;
    case se::Kind::Team: c.kind = CellKind::Team; break;
    case se::Kind::Game: c.kind = CellKind::Game; break;
    case se::Kind::Show: c.kind = CellKind::Show; break;
    case se::Kind::Genre: c.kind = CellKind::Genre; break;
  }
  c.id = it.id;
  c.title = it.name;
  c.entryId = it.entryId;
  if (it.listing) c.listing = *it.listing;
  c.also = it.also;
  c.outside = mark && it.outside;
  c.family = it.family;
  return c;
}

const se::TeamEntry* team(const se::Index& index, const std::string& key) {
  auto it = index.teamByKey.find(key);
  return it == index.teamByKey.end() ? nullptr : &index.teams[it->second];
}

std::string recentId(const Cell& cell) {
  switch (cell.kind) {
    case CellKind::Channel: return "c:" + cell.id;
    case CellKind::Team: return "t:" + cell.id;
    case CellKind::Game: return "g:" + cell.id;
    case CellKind::Show: return "s:" + se::fold(cell.listing.title);
    case CellKind::Genre: return "x:" + cell.id;
    default: return {};
  }
}

std::string teamGame(const se::TeamEntry& t) { return !t.liveGame.empty() ? t.liveGame : t.nextGame; }

void label(const ui::Ctx& c, Store& s, const se::Index& index, Cell& cell, std::string ok) {
  Cell::Label& l = cell.label;
  l = Cell::Label{};
  l.ok = std::move(ok);
  // The board's tile for the corner's card (a game, a team's game).
  const EventTile* corner = nullptr;
  switch (cell.kind) {
    case CellKind::Channel: {
      if (cell.family) {
        // An event channel group: its name, "Event channel" under it.
        l.logoName = l.cornerName = l.cornerTitle = l.title = l.cardTitle = cell.title;
        l.line = l.cornerLine = cell.outside ? std::string("Event channel \xc2\xb7 ") + kOutside : "Event channel";
        l.cardTag = "EVENT CHANNEL";
        break;
      }
      const GuideChannel* g = s.channels()->find(cell.id);
      if (!g) {
        l.missing = true;
        break;
      }
      l.logo = true;
      l.logoUrl = l.cornerLogo = g->logoUrl;
      l.logoName = l.cornerName = l.cornerTitle = g->name;
      l.title = l.cardTitle = g->name;
      l.line = channelLine(c, s, index, *g, l.live);
      if (cell.outside) {
        // "2401 · Outside your lineup".
        l.line = (g->number ? std::to_string(*g->number) + " \xc2\xb7 " : std::string()) + kOutside;
        l.live = false;
      }
      l.west = g->west;
      l.cardTag = "CHANNEL" + std::string(g->number ? " \xc2\xb7 " + std::to_string(*g->number) : "") +
                  (g->inLineup ? " \xc2\xb7 MY LINEUP" : "");
      views::NowNext nn = views::nowAndNext(*s.listings(), g->id, c.now);
      if (nn.now) l.cornerLine = nn.now->title;
      if (cell.outside) l.cornerLine = l.line;
      break;
    }
    case CellKind::Show: {
      const GuideChannel* g = s.channels()->find(cell.entryId);
      if (g) {
        l.logo = true;
        l.logoUrl = l.cornerLogo = g->logoUrl;
        l.logoName = l.cornerName = g->name;
      }
      const Listing& x = cell.listing;
      l.title = l.cardTitle = l.cornerTitle = x.title;
      l.live = x.isOn(c.now);
      l.tag = se::showTag(se::fold(x.title));
      l.line = showLine(c, s, cell, g);
      if (cell.outside) l.line += std::string(" \xc2\xb7 ") + kOutside;
      l.cardTag = "SHOW" + (l.tag.empty() ? std::string() : " \xc2\xb7 " + l.tag);
      l.cornerLine = x.isOn(c.now) ? "Now until " + ui::clock(c, x.end) : whenText(c, x.start);
      if (!x.isOn(c.now))
        l.cornerLine += s.hasShowReminder(cell.entryId, x.start) ? " \xc2\xb7 Reminder set \xe2\x9c\x93" : " \xc2\xb7 OK: Remind me";
      break;
    }
    case CellKind::Game: {
      const EventTile* t = s.eventTile(cell.id);
      if (!t) {
        l.missing = true;
        break;
      }
      corner = t;
      if (t->sides.size() == 2) l.sides = t->sides;
      l.logoUrl = t->channelLogo;
      l.logoName = t->channelName;
      l.title = l.cardTitle = matchup(*t);
      l.live = liveTile(*t);
      l.line = gameLine(c, s, *t);
      l.cardTag = "GAME" + (t->league.empty() ? std::string() : " \xc2\xb7 " + t->league) +
                  (t->favorite ? " \xc2\xb7 YOUR TEAM \xe2\x98\x85" : "");
      break;
    }
    case CellKind::Team: {
      const se::TeamEntry* t = team(index, cell.id);
      if (!t) {
        l.missing = true;
        break;
      }
      l.sides = {teamSide(s, *t)};
      l.title = t->name + (s.followsTeam(t->key) ? " \xe2\x98\x85" : "");
      l.cardTitle = l.cornerTitle = t->name;
      l.line = teamLine(c, s, *t, l.live);
      // Its games say why already when they're not in your channels.
      if (cell.outside && l.line.find("Not in your channels") == std::string::npos &&
          l.line.find("Streaming only") == std::string::npos)
        l.line += std::string(" \xc2\xb7 ") + kOutside;
      l.cardTag = "TEAM \xc2\xb7 " + t->league;
      corner = s.eventTile(teamGame(*t));
      break;
    }
    case CellKind::Genre:
      l.title = l.cardTitle = l.cornerTitle = cell.title;
      l.line = "Live now and later today";
      l.cardTag = cell.id.rfind("genre:", 0) == 0 ? "GENRE" : cell.id.rfind("sport:", 0) == 0 ? "SPORT" : "LEAGUE";
      break;
    case CellKind::Scope:
      if (cell.id == "wide") {
        l.title = "More outside your lineup";
        l.line = cell.title;  // the first few names
      } else {
        l.title = "Only your lineup and Live Events";
        l.line = "Hide what\xe2\x80\x99s outside your lineup";
      }
      l.cornerTitle = l.title;
      break;
    case CellKind::Follow:
      l.title = "\xe2\x98\x85 Follow your teams";
      l.line = "Their games show up here first. OK opens My Sports.";
      l.cornerTitle = cell.title;
      break;
    default: l.cornerTitle = cell.title; break;
  }
  l.cardLineOk = l.line;
  if (!l.ok.empty()) l.cardLineOk += (l.line.empty() ? "" : " \xc2\xb7 ") + l.ok;
  if (const EventTile* t = corner) {
    l.cornerTitle = matchup(*t);
    l.cornerLogo = t->channelLogo;
    l.cornerName = t->channelName;
    if (!t->watchable)
      l.cornerLine = std::string(t->footer == "Streaming only" ? "Streaming only" : "Not in your channels") +
                     " \xc2\xb7 OK: Where to Watch";
    else if (t->finished) l.cornerLine = "Ended";
    else if (liveTile(*t)) l.cornerLine = "LIVE " + t->status;
    else if (t->start && playsGame(*t, c.now))
      l.cornerLine = whenText(c, t->start) + " \xc2\xb7 OK: Watch";
    else if (t->start)
      l.cornerLine = whenText(c, t->start) + (s.hasReminder(t->id) ? " \xc2\xb7 Reminder set \xe2\x9c\x93" : " \xc2\xb7 OK: Remind me");
  }
}

bool calm(CellKind k) {
  return k == CellKind::Channel || k == CellKind::Show || k == CellKind::Game || k == CellKind::Team ||
         k == CellKind::Genre;
}

void drawCell(ui::Ctx& c, const Cell& cell, Rect box, bool f, float k) {
  bool white = f && !calm(cell.kind);
  if (f && !white) ui::calmFocus(c, box, 18 * k);
  else c.g.fill(box, f ? theme::white() : theme::white(0.07f), 18 * k);
  Color ink = white ? theme::black() : theme::white();
  Color sub = white ? theme::black(0.65f) : theme::white(0.7f);
  const Cell::Label& l = cell.label;
  bool ok = f && !l.ok.empty();
  float okW = ok ? ui::textWidth(c, l.ok, Weight::Bold, px(20, k)) + 24 * k : 0;
  if (cell.top) {
    drawTopCard(c, cell, box, white, ok, k);
    return;
  }
  float tx = box.x + 22 * k, right = box.x + box.w - 22 * k - okW;
  if (ok) ui::labelIn(c, l.ok, {right + 12 * k, box.y, okW - 12 * k, box.h}, Weight::Bold, px(20, k), ink, Align::Right);
  if (l.missing) return;
  switch (cell.kind) {
    case CellKind::Channel:
      ui::logo(c, l.logoUrl, l.logoName, {tx, box.y + (box.h - 54 * k) * 0.5f, 96 * k, 54 * k});
      tx += 116 * k;
      break;
    case CellKind::Show:
      if (l.logo) ui::logo(c, l.logoUrl, l.logoName, {tx, box.y + (box.h - 48 * k) * 0.5f, 84 * k, 48 * k});
      tx += 104 * k;
      break;
    case CellKind::Game: tx += badges(c, l, tx, box.y + (box.h - 52 * k) * 0.5f, 52 * k, k); break;
    case CellKind::Team: tx += teamBadge(c, l, tx, box.y + (box.h - 52 * k) * 0.5f, k) + 18 * k; break;
    default: break;
  }
  float ty = box.y + box.h * 0.5f - 36 * k;
  float lx = tx;
  if (l.live) lx += ui::chip(c, "LIVE", lx, ty + 4 * k, px(17, k), theme::hex(theme::kLive), theme::white()) + 10 * k;
  if (l.west) lx += ui::coastChip(c, true, lx, ty + 4 * k, px(17, k)) + 10 * k;
  if (!l.tag.empty())
    lx += ui::chip(c, l.tag, lx, ty + 4 * k, px(17, k), white ? theme::black(0.15f) : theme::white(0.18f), ink) + 10 * k;
  ui::label(c, l.title, lx, ty, Weight::Bold, px(26, k), ink, right - lx);
  ui::label(c, l.line, tx, ty + 38 * k, Weight::Medium, px(22, k), sub, right - tx);
}

}  // namespace marigold::app::search_view
