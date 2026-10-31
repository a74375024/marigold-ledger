// What the screens list, decided once for every platform: the guide's rows
// for a tab and filter, the "on now" filter rule, the flip list, the show on
// now and next, channel search, and the mini and event guide lists. Pure
// functions over the store's snapshots; the screens only lay them out and
// draw them (positions, colours and upper-case headers are theirs).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/engine/events_model.hpp"
#include "core/engine/model.hpp"
#include "core/util/time.hpp"

namespace marigold::engine::views {

enum class Tab { Lineup, Favorites, Recent, Events, All };

struct Filter {
  enum class Kind { None, SportsNow, NewsNow, MoviesNow, KidsNow, Section, Group } kind = Kind::None;
  std::string name;  // the neighborhood (Section) or provider group (Group)
  bool operator==(const Filter& o) const { return kind == o.kind && name == o.name; }
};

// A tab's channels before any filter (indexes into Channels::all). A lineup
// that is not set up yet shows every channel; one provider group includes
// its hidden channels.
const std::vector<int>& tabRows(const Channels& ch, Tab tab, const Filter& filter);

// "On now": a show airing now or within 30 minutes with the genre tag; a
// channel with no tagged shows counts by its neighborhood.
bool passes(const GuideChannel& g, const Filter& filter, const Listings* li, time::Seconds now);

struct Line {
  enum class Kind { Header, Channel, Prompt } kind = Kind::Channel;
  int channel = -1;   // index into Channels::all
  std::string title;  // the neighborhood, for a header
};
struct Section {
  std::string title;
  int firstLine = 0;  // its header line
  int count = 0;      // channels under it
};
struct List {
  std::vector<Line> lines;
  std::vector<Section> sections;
};

// The guide grid: a setup prompt above a lineup not set up yet, then the
// tab's channels that pass the filter under neighborhood headers (none for
// Recent, which is in watch order).
List guide(const Channels& ch, const Listings* li, Tab tab, const Filter& filter, time::Seconds now);

// These channels in this order under neighborhood headers (the mini guide).
// Ids no longer in the list are skipped.
List grouped(const Channels& ch, const std::vector<std::string>& ids);

// The entries a list's channel lines hold, in order: what CH+ / CH- move
// through in the player.
std::vector<std::string> flipList(const Channels& ch, const List& list);

// The channels the player's guide lists (and CH+ / CH- go through) while
// `playing` plays: the list it was picked from (`flip`) when that holds it,
// else lineupList(). A list another screen left behind (a game's, from Live
// Events or a Search preview, or a guide filter without this channel)
// never empties the guide or loses the channel playing.
std::vector<std::string> playerList(const Channels& ch, const std::vector<std::string>& flip,
                                    const std::string& playing);
// My Lineup (every channel before it's set up), or every channel when the
// lineup doesn't have `playing` (Search finds channels outside it): what a
// channel played from a screen without a list of its own goes through.
std::vector<std::string> lineupList(const Channels& ch, const std::string& playing);

// The guide in the player (Build 116) names the list it shows and lists
// it again when it opens: the one the channel playing was picked from in a
// guide (`picked`, remembered with the flip list it played with,
// `pickedFlip`) while CH+ / CH- still go through that list and it holds
// the channel; else playerList()'s list, named for what it is (Favorites,
// All Channels) or My Lineup.
struct PlayerList {
  Tab tab = Tab::Lineup;
  std::vector<std::string> ids;
};
PlayerList playerOpen(const Channels& ch, const std::vector<std::string>& flip, const std::string& playing, Tab picked,
                      const std::vector<std::string>& pickedFlip);
// A list picked in the player guide's lists, whole: My Lineup (every
// channel before it's set up), Favorites, Recent (most recent first) or
// All Channels.
std::vector<std::string> playerTab(const Channels& ch, Tab tab);
// Laid out for the player's guide: under their neighborhoods (a list's
// neighborhoods are only those holding one of its channels), Recent in
// watch order without any.
List playerGrid(const Channels& ch, Tab tab, const std::vector<std::string>& ids);

// What the filter menu shows beside each choice.
struct Count {
  std::string title;
  Filter filter;
  size_t count = 0;
};
std::vector<Count> onNowCounts(const Channels& ch, const std::vector<int>& rows, const Listings* li, time::Seconds now);
std::vector<Count> sectionCounts(const Channels& ch, const std::vector<int>& rows);

// The show at `t` in a guide window: airing then, else the next one that
// starts inside the window, else the last one still visible; -1 for none.
int programAt(const std::vector<Listing>& v, time::Seconds t, time::Seconds windowStart, time::Seconds windowEnd);

// The show on now and the one after it (the next one to start when nothing
// is on). Pointers into `li`.
struct NowNext {
  const Listing* now = nullptr;
  const Listing* next = nullptr;
};
NowNext nowAndNext(const Listings& li, const std::string& entryId, time::Seconds now);

// Channels whose name, number, local station or neighborhood matches,
// best first: exact name, number, name start, station start, word start,
// anywhere in the name, the neighborhood; lineup and favourites a little
// higher. Only channels shown in the guide (not another country's kept for
// Live Events).
std::vector<std::string> searchChannels(const Channels& ch, const std::string& query, size_t max);

// Lower-case, without accents: how search compares text.
std::string folded(const std::string& s);

// The event guide: one board row's games you can watch, under its group
// titles ("Live", "Later Today").
struct EventLine {
  std::string header;
  const EventTile* tile = nullptr;  // into the board
};
std::vector<EventLine> eventGuide(const EventsBoard& board, size_t row);

// Where the board's focus goes when a new board comes in under it (the
// rows as shown, after any filter): the same game in the same row (by id);
// that row at the same place when the game left it; the game in another
// row when its row is gone; else the same place, kept inside the rows.
struct BoardFocus {
  int row = 0, tile = 0;
};
BoardFocus boardFocus(const std::vector<EventRow>& rows, const std::string& rowId, const std::string& tileId, int row,
                      int tile);

// Typed channel digits can't become a longer channel number ("702" when no
// channel is 7020-7029, or 4 digits): tune now rather than wait for more.
bool digitsSettled(const std::string& typed, const Channels& ch);

}  // namespace marigold::engine::views
