// The Live Events report (Settings › Help & info › Send a report): a
// spreadsheet (CSV) of every event slot in the provider's event groups,
// what the app read it as, and what became of it: the tile it is on (and
// the rows that tile is in), or why it was left out, at each step:
//   - the channel list: an empty, placeholder or "ended" slot, a name with
//     no title, or a time long past or days away;
//   - matching: a replay, a show, a leftover, a fight card that's over
//     (matcher::Stats::hidden);
//   - the board: high school or pro wrestling off, a studio show, over, not
//     in your sports, hidden in Settings › Live Events › Hidden, not in
//     your soccer competitions, a college game without a ranked team or one
//     you follow (board::Board::left). Build 127: a tile in More events
//     says why it isn't in its sport's row (board::Board::moreWhy).
// Build 125: a last column, "schedule", says whether the slot went to a
// scheduled game ("on the schedule"), was left out as a leftover
// ("leftover"), or no schedule has it ("no schedule"); empty for a slot
// not read as an event. Build 136: "not matched yet" for a slot new or
// renamed since Live Events last matched (`matched`).
// For checking why something odd is (or isn't) on the board.
//
// No stream links, logins or server addresses: slot names and titles only,
// and every cell goes through redact(). Pure: every pointer may be null
// (= empty) and is read only during the call.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/sports/board.hpp"
#include "core/sports/types.hpp"
#include "core/util/time.hpp"

namespace marigold::report {

struct LiveEventsReportInput {
  const catalog::CatalogResult* catalog = nullptr;
  // The last rebuild, its reasons for left-out feeds (raw feed id -> why),
  // and the board built from it.
  const std::vector<sports::SportsEvent>* events = nullptr;
  const std::map<std::string, std::string>* hidden = nullptr;
  const sports::board::Board* board = nullptr;
  // Build 136: the feeds that rebuild was given, with their event's title
  // then (matchedFeeds()). The channel list is read again every half hour,
  // Live Events open or not, and the provider renames its event slots;
  // Live Events matches again only when it's open. A slot new or renamed
  // since says so ("new or renamed since Live Events last matched")
  // instead of being blamed on matching or shown on its old name's tile.
  // Empty: nothing matched this session (a board saved last time). Null:
  // not known (every feed taken as matched).
  const std::vector<std::pair<uint64_t, uint64_t>>* matched = nullptr;
  // A provider group's starts this week ("1 of 4 started"), and whether it
  // keeps failing (its tiles go last).
  std::function<std::string(const std::string& familyKey)> groupStarts;
  std::function<bool(const std::string& familyKey)> groupFails;
  // Pro wrestling is left out of the board before it's built unless picked.
  bool proWrestlingOn = false;
  time::Seconds now = 0;
  time::Zone zone = time::Zone::device();
};

std::string liveEventsCsv(const LiveEventsReportInput& in);

// The feeds of these events (the channel list's, as given to a rebuild):
// (fnv1a64 of the feed id, fnv1a64 of its event's title), sorted. Small (16
// bytes a feed), kept with the rebuild's result for the report.
std::vector<std::pair<uint64_t, uint64_t>> matchedFeeds(const std::vector<catalog::LiveEvent>& events);

}  // namespace marigold::report
