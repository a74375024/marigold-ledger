// What the screens list, decided once for every platform: the guide's rows
// for a tab and filter, the "on now" filter rule, the flip list, the show on
// now and next, channel search, and the mini and event guide lists. Pure
// functions over the store's snapshots; the screens only lay them out and
// draw them (positions, colours and upper-case headers are theirs).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/events_model.hpp"
#include "core/engine/model.hpp"
#include "core/util/time.hpp"

namespace marigold::engine::views {

enum class Tab { Lineup, Favorites, Recent, Events, All };

struct Filter {
  // CanGoBack (Build 167): channels with replay (GuideChannel::replayDays).
  enum class Kind { None, SportsNow, NewsNow, MoviesNow, KidsNow, Section, Group, CanGoBack } kind = Kind::None;
  std::string name;  // the neighborhood (Section) or provider group (Group)
  bool operator==(const Filter& o) const { return kind == o.kind && name == o.name; }
};

// A tab's channels before any filter (indexes into Channels::all). A lineup
// that is not set up yet shows every channel; one provider group includes
// its hidden channels.
const std::vector<int>& tabRows(const Channels& ch, Tab tab, const Filter& filter);

// "On now": a show airing now or within 30 minutes with the genre tag; a
// channel with no tagged shows counts by its neighborhood. "Can go back":
// a channel with replay.
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

// A neighborhood's header line in a grid, the guide's and the player's
// alike: its name in capitals ("LOCAL", "NEW YORK").
std::string headerTitle(const std::string& section);

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
// Build 167: the guide filter's "Can go back", offered when any of the
// rows has replay (never how many).
constexpr const char* kCanGoBack = "Can go back";
bool anyReplay(const Channels& ch, const std::vector<int>& rows);

// Build 167, the replay mark on the guide's channel cells. Plain boxes in
// layout points, so the decision is tested without a renderer.
struct Box {
  float x = 0, y = 0, w = 0, h = 0;
};
// A picture of shape `aspect` (width / height) fitted inside `area`, its
// shape kept, centred and rounded to whole points: where ui::logo draws a
// logo.
Box fitted(Box area, float aspect);
// The mark in a cell: 16 points at its bottom left (x + 8, y + h - 22), as
// the star sits top right and EAST / WEST bottom right.
Box replayMark(Box cell);
// The mark's corner: the mark and a point round it.
Box replayCorner(Box cell);
// Where a logo-only cell fits its logo. `area`: the logo area (154 x 60 at
// +14, +12 on the guide's 182 x 84 cell). As today, unless the channel has
// replay and its logo fitted in the area would overlap the mark's corner:
// then that logo is drawn about 15% smaller (more only when that isn't
// enough to clear it), centred across, at the area's top.
Box logoArea(Box area, float aspect, Box corner, bool replay);

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
// Build 167: the channel banner while a replay plays behind live, worked
// out when the minute shown changes (never per frame). `shows`: the
// channel's programmes around the moment, sorted (mergedListings() of the
// guide's, the past it keeps for replay channels and the copy's own);
// `live`: the programme on now (null: none known).
struct ReplayBanner {
  const Listing* show = nullptr;  // on at the moment (into `shows`); null: none known
  double progress = 0;            // through `show` at the moment, 0..1
  std::string inLeft;             // "21 min in · 39 min left"
  std::string liveLine;           // "Live now: Anderson Cooper 360 · 59 min behind"
  // "Next in the replay": up to three starting after `show` ends (after
  // the moment when no show is known).
  std::vector<const Listing*> next;
};
ReplayBanner replayBanner(const std::vector<Listing>& shows, const Listing* live, time::Seconds at,
                          time::Seconds now);
// Programme lists merged: `base`'s where it has them, `extra`'s filling
// what it lacks (one that overlaps `base`'s by more than half of itself is
// left out). Sorted by start.
std::vector<Listing> mergedListings(std::vector<Listing> base, const std::vector<Listing>& extra);
// "59 min behind", "1 h 5 min behind".
std::string behindLine(time::Seconds behind);
// Build 167, a channel's About in Edit My Lineup: its replay by provider
// (Store::replayByProvider), "2 days on Dream, 1 day on Trex. Pause, rewind
// and start over work on it, and its games can be watched from the start."
// ("" when it has none). Never a count of channels.
std::string replayAbout(const std::vector<std::pair<std::string, uint16_t>>& byProvider);
// Build 169: the guide's focused-show chip, the replay window after its
// icon: "2 days", "1 day" ("" when the channel has none).
std::string replayWindow(uint16_t days);

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

// Build 167, replay in Live Events. The game banner while a game plays
// behind live (its replay, from the board or Start over): a REPLAY chip by
// the title with how far into the game it is ("38 min in", from its
// scheduled start), none of what the clock would say ("likely over"), no
// "was scheduled until" or what's next; at the right, the channel and one
// muted line, "Live now: <the channel's listing now>" ("Live now: this
// game" while the game itself is still on). Caught up (`behind` false):
// as before, `replay` false. Never a score (the banner shows none).
struct GameReplayBanner {
  bool replay = false;
  std::string in;       // "38 min in"
  std::string liveNow;  // "Live now: NBA GameTime" ("" none known)
};
GameReplayBanner gameReplayBanner(bool behind, std::optional<time::Seconds> start, time::Seconds at, bool gameLive,
                                  const std::string& liveTitle);
// A Final's replay tag, from the game's scheduled start: "48:10 in",
// "1:02:05 in" (never below zero).
std::string replayIn(time::Seconds start, time::Seconds at);
// Go live during a Final's replay: the channel's live ("NBA TV live").
std::string channelLive(const std::string& channel);

// The player's Sources for a game (Where to Watch with ticks), Build 135:
// the source playing the raw channel `playing`. The one picked (`picked`,
// an entry or a raw feed, Session::pickedSource) while it plays; else the
// first whose own channels (WatchSource::channels) hold it: the backup that
// took over. Null when none does.
const WatchSource* playingSource(const WhereToWatch& w, const std::string& playing, const std::string& picked);

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
