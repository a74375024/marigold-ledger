// The player's banner while watching a game (Build 112, "ID strip"): where
// the game stands by the clock alone, the small line of league, round and
// venue under the matchup, and which game a guide channel is showing.
//
// Nothing here reads the live scoreboard: the banner shows no score, clock,
// period, situation, sets or result, whatever the Scores setting is, and
// nothing is asked for while watching. The broadcaster's own bug in the
// picture carries the score; the banner can never disagree with it.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/sports/board.hpp"
#include "core/sports/types.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::banner {

enum class Chip : uint8_t {
  none,
  soon,        // "in 25 min", amber
  live,        // "LIVE"
  likelyOver,  // "Likely over", outlined (the tiles' word)
};

struct Status {
  Chip chip = Chip::none;
  std::string text;
};

// By the clock: "in 25 min" (or "in 1 h 20 min") before the start, LIVE
// until the scheduled end (the sport's usual length, or the guide listing's
// end when later), "Likely over" after it, for as long as it's watched:
// without scores nobody knows it's over, so never "Final" or a winner.
// No time listed, or postponed: no chip.
Status status(std::optional<Seconds> start, std::optional<Seconds> end, bool postponed, Seconds now);
Status status(const SportsEvent& event, Seconds now);

// The small-caps line under the matchup: the league, the week, the tag
// (the round, "Sunday Night", "Pan Pacific Open · R2"; the tile's, without
// its alerts), where (venue, court, course, circuit) and an F1 race's
// "LIGHTS OUT 2:00 PM". Upper case; nothing said twice.
std::string line(const SportsEvent& event, const std::string& tag, const time::Zone& zone, bool twelveHour);

// The board's game a guide channel is showing now: one that lists the
// channel (any of `entryIDs`: a channel and its other coast's version)
// and that by the clock is on, just past its usual end (until the sport's
// overrun is gone), or starting within the hour. On first, then the one
// past its end, then the next to start. Studio shows, whip-arounds and
// games the schedule already had as over aren't games on the channel.
// Empty for none (the channel keeps its usual banner).
std::string onChannel(const board::Board& board, const std::vector<std::string>& entryIDs, Seconds now);

}  // namespace marigold::sports::banner
