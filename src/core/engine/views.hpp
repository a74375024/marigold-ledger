// What the screens list, decided once for every platform: the guide's rows
// for a tab and filter, the "on now" filter rule, the flip list, the show on
// now and next, channel search, and the mini and event guide lists. Pure
// functions over the store's snapshots; the screens only lay them out and
// draw them (positions, colours and upper-case headers are theirs).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
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

// The guide's top for the focused show (Build 126), worked out when focus
// moves or the minute turns, never per frame. `focused` indexes `v` (-1:
// none). The preview always plays the channel's show on now, so while a
// later (or earlier) show is focused it names what it plays.
struct GuideTop {
  enum class When : uint8_t { None, On, Later, Ended };
  When when = When::None;
  const Listing* next = nullptr;     // the show after the focused one ("NEXT 9:00 PM · …")
  const Listing* playing = nullptr;  // on now when the focused show isn't (the preview's "NOW" label)
};
GuideTop guideTop(const std::vector<Listing>& v, int focused, time::Seconds now);
// "18 min left" for a show on now (whole minutes, rounded up); "" otherwise.
std::string minutesLeft(const Listing& l, time::Seconds now);
// "Starts in 18 min", "Starts in 1 h 20 min", "Starts in 2 h"; "" once it
// has started.
std::string startsIn(const Listing& l, time::Seconds now);
// "Title · Episode", or the title alone.
std::string titleAndEpisode(const Listing& l);
// After the preview's "NOW": "Title · 18 min left".
std::string nowPlaying(const Listing& playing, time::Seconds now);

// What a guide row (or the top for its channel) says when it has no show
// to draw in the window (Build 130). `later`: a window after now.
// `listed`: the row has listings, just none here. `askedEmpty`: its own
// request came back with none. `onTheWay`: Store::listingsOnTheWay.
// `savedReading`: Store::savedListingsReading (a relaunch).
enum class EmptyRow : uint8_t {
  NoneForThisTime,  // "No listings for this time"
  Placeholders,     // the saved guide is being read: pulsing cells, no words
  OnTheWay,         // "Listings on the way"
  NoGuideInfo,      // "No guide info": nothing more is coming
};
EmptyRow emptyRow(bool later, bool listed, bool askedEmpty, bool onTheWay, bool savedReading);
constexpr const char* kListingsOnTheWay = "Listings on the way";

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

// OK on a game (the board, Search): plays it when it is on (live, likely
// over, untimed) with somewhere to watch, or (Build 123) when it starts
// within kPlaysBeforeStart and its best source is a channel
// (EventTile::channelFirst: the pre-game is usually on). Otherwise the
// screen's own: the detail card, Where to Watch or a reminder.
constexpr time::Seconds kPlaysBeforeStart = 30 * 60;
bool startsSoonOnChannel(const EventTile& t, time::Seconds now);
bool okPlays(const EventTile& t, time::Seconds now);

// Where the board's focus goes when a new board comes in under it (the
// rows as shown, after any filter): the same game in the same row (by id);
// that row at the same place when the game left it; the game in another
// row when its row is gone; else the same place, kept inside the rows.
struct BoardFocus {
  int row = 0, tile = 0;
};
BoardFocus boardFocus(const std::vector<EventRow>& rows, const std::string& rowId, const std::string& tileId, int row,
                      int tile);

// A board row as a screen shows it, pointing into the board (Build 119:
// a new board every few seconds is never copied): the row, its tiles shown
// in order across its groups, the groups with a tile shown, and the counts
// in its header (the row's own, or a filter's).
struct ShownRow {
  const EventRow* row = nullptr;
  std::vector<const EventTile*> tiles;
  struct Group {
    const EventGroup* group = nullptr;
    size_t begin = 0, end = 0;  // its tiles in `tiles`
  };
  std::vector<Group> groups;
  int count = 0, live = 0;
};
// Every tile of the row, with its counts.
ShownRow shownRow(const EventRow& row);
// The tiles `keep` keeps (groups left with none are left out), counted
// afresh: `live` says which of them count as live.
ShownRow shownRow(const EventRow& row, const std::function<bool(const EventTile&)>& keep,
                  const std::function<bool(const EventTile&)>& live);
// boardFocus() over rows as shown.
BoardFocus boardFocusShown(const std::vector<ShownRow>& rows, const std::string& rowId, const std::string& tileId,
                           int row, int tile);

// Show on the board (Build 119): the board's rows as the board screen, its
// SPORTS column and the games rail show them. More events, Time not listed
// and Event channels only when switched on; a row holding `keep` (the game
// on screen) stays, and so does one the board always shows
// (EventRow::alwaysShown, Build 127: More events when My Sports has no
// picks). The board itself keeps every row (Search, Where to Watch and
// Up/Down read it). boardHides() says whether any row is left
// out; boardHidden() names the rows left out, in board order, for the
// empty board's line. The screens take boardShownRows(): the rows shown as
// indices into the board's (boardShown() is the same rows as copies).
bool boardRowShown(const EventRow& row, const BoardShows& shows, const std::string& keep = {});
bool boardHides(const std::vector<EventRow>& rows, const BoardShows& shows, const std::string& keep = {});
std::vector<size_t> boardShownRows(const std::vector<EventRow>& rows, const BoardShows& shows,
                                   const std::string& keep = {});
std::vector<EventRow> boardShown(const std::vector<EventRow>& rows, const BoardShows& shows,
                                 const std::string& keep = {});
std::vector<std::string> boardHidden(const std::vector<EventRow>& rows, const BoardShows& shows);
// "More events, Time not listed and Event channels are hidden." ("" for none).
std::string hiddenLine(const std::vector<std::string>& titles);

// Typed channel digits can't become a longer channel number ("702" when no
// channel is 7020-7029, or 4 digits): tune now rather than wait for more.
bool digitsSettled(const std::string& typed, const Channels& ch);

}  // namespace marigold::engine::views
