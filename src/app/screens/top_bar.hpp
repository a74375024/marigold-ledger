// The top of the guide, shared with Live Events so both screens put things
// in the same place: the clock, the corner preview and the round search,
// filter and options buttons at the right of the tab row.
#pragma once

#include <cstdint>
#include <string>

#include "app/ui/ui.hpp"
#include "core/input/preview_pace.hpp"

namespace marigold::app {
class Playback;
}

namespace marigold::app::top_bar {

constexpr ui::Rect kPreview{1440, 80, 400, 225};
// Focus rests this long before the preview tunes, and previews start at
// most this often (scrolling past tiles or rows doesn't tune them):
// core/input/preview_pace.hpp.
constexpr uint32_t kPreviewDelayMs = preview::kDelayMs;
constexpr uint32_t kPreviewGapMs = preview::kGapMs;
constexpr float kTabsY = 318;

// The clock, right-aligned over the preview corner.
void clock(ui::Ctx& c);
// The corner: the picture while it plays, else `logoUrl` / `name` with a
// spinner while it tunes.
void preview(ui::Ctx& c, const Playback& p, const std::string& logoUrl, const std::string& name);
// Search (0), filter (1) and options (2); `focused` is -1 for none. Each
// button's hit id is `hitBase` + its index. A red dot on the filter while
// one is set.
void buttons(ui::Ctx& c, int focused, bool filtered, int hitBase);
// The small round filter button of the left rail's layout (Build 112),
// beside the guide's "Now ▼" and on Live Events' first row: 40 px, a red dot
// while a filter is set.
void filterButton(ui::Ctx& c, ui::Rect r, bool focused, bool filtered, int hit);

}  // namespace marigold::app::top_bar
