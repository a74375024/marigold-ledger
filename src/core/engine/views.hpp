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

// Typed channel digits can't become a longer channel number ("702" when no
// channel is 7020-7029, or 4 digits): tune now rather than wait for more.
bool digitsSettled(const std::string& typed, const Channels& ch);

}  // namespace marigold::engine::views
