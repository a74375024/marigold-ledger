// The Live Events report (Options › Help › Export Live Events Report): a
// spreadsheet (CSV) of every event slot in the provider's event groups,
// what the app read it as, and what became of it: the tile it is on (and
// the rows that tile is in), or why it was left out, at each step:
//   - the channel list: an empty, placeholder or "ended" slot, a name with
//     no title, or a time long past or days away;
//   - matching: a replay, a show, a leftover, a fight card that's over
//     (matcher::Stats::hidden);
//   - the board: high school or pro wrestling off, a studio show, over, not
//     in your sports (board::Board::left).
// For checking why something odd is (or isn't) on the board.
//
// No stream links, logins or server addresses: slot names and titles only,
// and every cell goes through redact(). Pure: every pointer may be null
// (= empty) and is read only during the call.
#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
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

}  // namespace marigold::report
