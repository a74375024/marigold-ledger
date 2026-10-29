// Drawing shared by the Live Events screens: the 360x230 tile, team badges,
// status pills and the hero backdrop (LiveEventTile, TeamBadge in tvOS).
#pragma once

#include "app/store/store.hpp"
#include "app/ui/ui.hpp"

namespace marigold::app::events_ui {

constexpr float kTileW = 360;
constexpr float kTileH = 230;

// A team badge: the logo when it has loaded, else a colour circle with the
// abbreviation. `size` is the diameter.
void badge(ui::Ctx& c, const EventSide& side, float x, float y, float size, bool focused, float opacity = 1);
// The status pill (LIVE, DELAYED, Final...). Returns its width (0 = none).
float pill(ui::Ctx& c, const EventTile& t, float x, float y, bool focused);
// `mixed`: a row of several sports (Live now, Your teams), where the top
// line names the sport; in a sport's own row it gives the competition only.
void tile(ui::Ctx& c, const EventTile& t, ui::Rect r, bool focused, bool mixed);
// The focused game's two badges, big and faded, over a blue glow (top
// right). Only the glow when `badges` is off (the corner preview is there).
void backdrop(ui::Ctx& c, const EventTile& t, bool badges = true);

}  // namespace marigold::app::events_ui
