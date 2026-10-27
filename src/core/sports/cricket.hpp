// Cricket, read from the scoreboard's live listing of every series under
// way (international tours, World Cups, the IPL, Big Bash and domestic
// cricket): matches and their scores. Scores come as text ("317 & 168/8
// (51.3 ov, target 313)", "465/7d", "162 & 215/5 (59.1 ov) (f/o)"); these
// turn that into what a tile shows.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/sports/leagues.hpp"
#include "core/sports/scores.hpp"
#include "core/sports/types.hpp"

namespace marigold::sports::cricket {

// The listing: every series' matches, newest data first.
std::string listingUrl();

// A side's score as the tile shows it: the innings without their details
// ("317 & 168/8"); empty before it bats.
std::string shown(std::string_view score);
// The side batting now: its score carries the overs ("(51.3 ov").
bool batting(std::string_view score);
// The footer for the side batting: a limited-overs chase ("Need 53 from 34
// balls"), a chase without an overs limit ("Need 145 to win"), else the
// overs ("51.3 ov", "16.2/20 ov"), with "follow-on" when it is one.
std::optional<std::string> situation(std::string_view battingScore);

// Matches (games: the two sides, the format, the last day) and their
// statuses, from the listing.
std::optional<std::vector<SportsGame>> parseMatches(std::string_view body, const League& league);
scores::Statuses statuses(std::string_view body, const League& league);

}  // namespace marigold::sports::cricket
