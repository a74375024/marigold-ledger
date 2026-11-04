// Search's results as they show (Builds 113-119), shared by the Search
// screen and the player's search (Build 132): what the engine found
// (core/engine/search.hpp) as cells, their text made once per layout,
// minute or data change (label(): drawing makes no text), and a result's
// row and the top result's card drawn from that text. Never a score or a
// count.
#pragma once

#include <string>
#include <vector>

#include "app/store/store.hpp"
#include "app/ui/ui.hpp"
#include "core/engine/search.hpp"

namespace marigold::app::search_view {

namespace se = engine::search;

enum class CellKind { Channel, Team, Game, Show, Genre, Chip, Recent, ClearRecents, Instead, Follow, Scope };

struct Cell {
  CellKind kind = CellKind::Channel;
  std::string id;     // entry id, team key, event id, genre or chip key, recent id
  std::string title;  // chips, genres, recent searches
  std::string entryId;
  Listing listing;    // shows
  time::Seconds also = 0;
  bool top = false;   // the band's top result card
  bool outside = false;  // marked "Outside your lineup"
  bool family = false;   // a Channel cell for an event channel group (Build 119)
  // What it shows, made by label() after a layout, each minute and when
  // the data changes: drawing makes no text.
  struct Label {
    bool missing = false;  // its channel, game or team is gone: only the box
    std::string title, line, tag, ok;
    bool live = false, west = false, logo = false;
    std::string logoUrl, logoName;  // a channel's or show's logo; a game's channel
    std::vector<EventSide> sides;   // a game's two badges, or a team's one
    std::string cardTag, cardTitle, cardLineOk;  // the top result's card
    std::string cornerLogo, cornerName, cornerTitle, cornerLine;  // the Search screen's corner
  } label;
};

extern const char* const kOutside;  // "Outside your lineup"

// Live on the board (live, its dimmer LIVE, on now, delayed).
bool liveTile(const EventTile& t);
// OK plays a game: live in your channels, or (as on the board) starting
// within half an hour on a channel (views::startsSoonOnChannel()).
bool playsGame(const EventTile& t, time::Seconds now);
// "in 20 min", "Tonight 7:30 PM", "Tomorrow 7:30 PM", "Sat 7:30 PM".
std::string whenText(const ui::Ctx& c, time::Seconds t);

// A found item as a cell. `mark`: an item outside your lineup says so.
Cell cellOf(const se::Item& it, bool mark = true);
const se::TeamEntry* team(const se::Index& index, const std::string& key);
// The game a team row stands for: live, else next ("" for none).
std::string teamGame(const se::TeamEntry& t);

// What a recent search remembers a pick by ("c:" + a channel, "s:" + a
// show's folded title...; "" for chips).
std::string recentId(const Cell& cell);

// The cell's text, from the store and the index. `ok`: what OK does on it,
// written on it while it has the focus ("" for nothing).
void label(const ui::Ctx& c, Store& s, const se::Index& index, Cell& cell, std::string ok);

// Results (where logos and badges appear) take the calm focus and keep
// light text; the band's other cells (chips, Follow) the white one.
bool calm(CellKind k);
// A result in `box`, focused or not: its fill, its logo or badges, title
// and line (the top result: its card). `k` scales the sizes (1: the Search
// screen's; the player's card is smaller).
void drawCell(ui::Ctx& c, const Cell& cell, ui::Rect box, bool focused, float k = 1);

}  // namespace marigold::app::search_view
