// Drawing shared by the Live Events screens: the 360x156 tile, team badges,
// status pills and the hero backdrop (LiveEventTile, TeamBadge in tvOS).
#pragma once

#include "app/store/store.hpp"
#include "app/ui/ui.hpp"

namespace marigold::sports::banner {
enum class Chip : uint8_t;
}

namespace marigold::app {
class App;
}

namespace marigold::app::events_ui {

constexpr float kTileW = 360;
constexpr float kTileH = 156;

// A team badge: the logo when it has loaded (its dark-surface version,
// nothing behind it), else a disc in the team colour with the abbreviation.
// `size` is the diameter. `faded`: on a dimmed tile or row, the badge loses
// its colour but keeps its brightness. Focus never changes a badge.
void badge(ui::Ctx& c, const EventSide& side, float x, float y, float size, bool faded = false, float opacity = 1);
// The status pill (LIVE, DELAYED, Final...), on a dark tile or row (focused
// ones included). Returns its width (0 = none).
float pill(ui::Ctx& c, const EventTile& t, float x, float y);
// A side's mark `size` tall, as the player's game banner draws it: the
// country flag (a doubles pair's two), the country code until it loads,
// else the badge. Returns the width used.
float mark(ui::Ctx& c, const EventSide& side, float x, float y, float size);
// The game banner's status by the clock, in the tiles' pills: "in 25 min"
// amber, LIVE, "Likely over" outlined. Returns its width (0 = none).
float clockPill(ui::Ctx& c, sports::banner::Chip chip, const std::string& text, float x, float y);
// Live by the scoreboard or the clock (LIVE, or the dimmer LIVE of an old
// score): shown by red text only (Build 124), no pill and no red icon.
bool live(const EventTile& t);
// That red: dimmer for an old score's LIVE (LiveOutline).
ui::Color liveRed(const EventTile& t, float alpha = 1);
// A game's state and status on a dark row, as the tile's top line has them
// (the hero, the detail card, the event guide): live, `text` in red (its
// clock, "2nd · 8:41"), or "LIVE" in red when it has none; otherwise the
// pill (`pillDy` below `y`) and `gap`, then `text` in `ink`. Returns the
// width used.
float status(ui::Ctx& c, const EventTile& t, const std::string& text, float x, float y, ui::Weight weight, int px,
             ui::Color ink, float maxW, float pillDy, float gap);
// The sport's chip (Build 124): its icon, white, in a rounded square of its
// colour (`size` square; the icon about two thirds of it), faded with
// `alpha`. Never red: live is the status text's.
void sportChip(ui::Ctx& c, const EventTile& t, float x, float y, float size, float alpha = 1);
// `mixed`: a row of several sports (Live now, Your teams), where the top
// line names the sport; in a sport's own row it gives the competition only.
// Focused: lifted, with the calm focus (ui::calmFill()). Its top line starts
// with the sport's chip (Build 124; none for a show of no sport known).
void tile(ui::Ctx& c, const EventTile& t, ui::Rect r, bool focused, bool mixed);
// The focused game's two badges, big and faded, over a blue glow (top
// right). Only the glow when `badges` is off (the corner preview is there).
void backdrop(ui::Ctx& c, const EventTile& t, bool badges = true);
// The tile that ends a row the viewer can change (Build 127): "Choose" and
// `what` ("schools", "competitions") on an outlined tile; focused, the
// calm focus.
void chooseTile(ui::Ctx& c, const std::string& what, ui::Rect r, bool focused);
// Hide on a game card (Build 127): what hiding `t`'s competition does
// ("Its matches leave your Soccer row. Bring it back in Settings, Hidden.").
std::string hideLine(App& app, const EventTile& t);
// Hides a competition from the board and leaves a note to undo it.
void hide(App& app, const std::string& id, const std::string& name);

}  // namespace marigold::app::events_ui
