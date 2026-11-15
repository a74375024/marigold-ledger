// Build 160: an event of several parts (a fight card's early prelims,
// prelims and main card; a race weekend's sessions) and the part on now,
// decided when you press play or open Where to Watch (and when the board is
// built, for the tile). Each of a game's sources then has a part tier: the
// part on now first (with the whole-night feeds on it), the next part, the
// ones after, and the parts that are over last. Within a part tier the
// language tier (languages::tier) and the rest order them.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/sports/scores.hpp"
#include "core/sports/types.hpp"

namespace marigold::sports::parts {

// A part still on past the next one's start: that long before the clock
// moves on without a status saying so (a card's undercard runs late).
constexpr Seconds kGrace = 30 * 60;
// Slots of a one-part fight this far apart are two parts (the prelims and
// the main event of a boxing card).
constexpr Seconds kSlotGap = 45 * 60;
// Part tiers: the part on now 0, the next 1, the one after 2...; a part
// that's over kOver and on (the one just over first).
constexpr int kOver = 100;

// A part's key in its card ("early", "prelims", "main"; a race session's
// "fp1", "race"); "" for an event that isn't a part.
std::string kind(const SportsEvent& part);

// The part on now, by position in `parts` (in order by start):
// - a part the live scoreboard says is being played (the latest of them);
// - else the first that isn't over (its status final, or completed), moved
//   on by the clock to a later part once that one has started and the one
//   before had kGrace to finish (none when its listings say it ended),
//   unless the scoreboard says the later part hasn't started.
// A race weekend: the session on now (or next), by its status, else its
// usual length. nullopt when there are no parts.
std::optional<size_t> onNow(const std::vector<SportsEvent>& parts, Seconds now, const scores::Statuses& statuses);

// The part tier of part `part` when `current` is on.
int tierOf(size_t part, size_t current);

// Every source's part tier, in the order of event.sources: a card's
// (`parts`, the board's for this tile; the best of the parts a source is
// in), else a fight's on one part by its slots' own times (a "Prelims"
// slot hours before the main event's is over once that one has run kGrace),
// else 0. `current`, when given, is set to the part on now (a card's).
std::vector<int> sourceTiers(const SportsEvent& event, const std::vector<SportsEvent>* parts, Seconds now,
                             const scores::Statuses& statuses, std::optional<size_t>* current = nullptr);

}  // namespace marigold::sports::parts
