// Divisions II and III college soccer, field hockey and volleyball, read
// from the NCAA's public scoreboard page for a sport, division and day. The
// page carries the day's games in its settings (teams, start, state, period,
// clock and score); one page is both the schedule and the live scores.
// Division I games come from the other scoreboards, so only II and III are
// asked for.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/sports/leagues.hpp"
#include "core/sports/scores.hpp"
#include "core/sports/types.hpp"

namespace marigold::sports::ncaa {

// The page for a league ("soccer-women/d3") and a US Eastern day
// ("20261028"); empty when the day isn't one.
std::string pageUrl(const League& league, std::string_view day);

// A school's name written out: "Plymouth St." is "Plymouth State",
// "Southern Me." "Southern Maine", "Mt. St. Joseph" "Mount Saint Joseph",
// "Rhode Island Col." "Rhode Island College"; brackets kept ("Wheaton
// (MA)").
std::string writtenOut(std::string_view name);
// Other names a channel may use: without the brackets, without a last
// "State" or "College", without the dots ("NC Wesleyan").
std::vector<std::string> otherNames(std::string_view name);

// The day's games (nullopt when the page has no games list), and their
// statuses by game id ("D3WSO|6613180").
std::optional<std::vector<SportsGame>> parseGames(std::string_view body, const League& league);
scores::Statuses statuses(std::string_view body, const League& league);

}  // namespace marigold::sports::ncaa
