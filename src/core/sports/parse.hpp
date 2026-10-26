// Scoreboard JSON helpers shared by the schedule and live scores parsers
// (SportsParse in SportsData.swift). Internal.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/sports/types.hpp"
#include "core/util/json.hpp"

namespace marigold::sports::parse {

// Largest single element (one event) the streaming reader materialises. A
// tennis tournament with every match is the largest; a few MB at most.
constexpr size_t kMaxEventBytes = 16u << 20;

// Calls `fn` for each element of the body's top-level "events" array, one
// at a time (the body is never built as one tree). False when the body isn't
// JSON or has no "events" array; `fn` may have run for some elements.
bool forEachEvent(std::string_view body, const std::function<void(const json::Value&)>& fn);

// Scalars of the top-level objects "week" ({"number": 8}) and "season"
// ({"type": 2}), read in a separate token pass.
struct TopFields {
  std::optional<std::string> weekNumber;
  std::optional<std::string> seasonType;
};
TopFields topFields(std::string_view body);

// A poll rank worth showing (1-25; the schedule says 99 for unranked).
std::optional<int> pollRank(const std::optional<std::string>& value);
// "00274c" or "#00274c" -> "00274C"; anything that isn't 6 hex digits -> nil.
std::optional<std::string> hexColor(const std::optional<std::string>& value);

// SportsParse.team: a team object ({"id", "displayName", ...}).
std::optional<SportsTeam> team(const json::Value& item);
// A fighter (competitor.athlete or .team): id "f<id>", headshot (or flag)
// as badge, country code from the flag file name.
std::optional<SportsTeam> fighter(const json::Value& competitor);
// ".../countries/500/aus.png" -> "AUS".
std::optional<std::string> countryFromFlag(const std::optional<std::string>& href);
// TV networks of a competition, national first, without repeats.
std::vector<std::string> networks(const json::Value& competition);
// "Venue Name, City" (the city only when the name doesn't already say it).
std::optional<std::string> venue(const json::Value& item);

// A tennis event's matches: those of its groupings (singles, doubles...),
// else its own competitions.
std::vector<const json::Value*> tennisCompetitions(const json::Value& event);

// "early", "prelims", "main" from a bout's card segment or type, or nil.
std::optional<std::string> cardPart(const json::Value& competition);
std::optional<std::string> cardPartOfText(std::string_view text);
// A bout's start minute (startDate, else date), minutes since the epoch.
std::optional<int64_t> boutMinute(const json::Value& bout);
// Each bout's card part key. With parts named in the data, those ("main"
// for unnamed bouts); otherwise by start time: the latest is the main card,
// the one before the prelims, earlier ones the early prelims; "" when the
// card isn't split.
std::vector<std::string> partKeys(const std::vector<json::Value>& bouts);

}  // namespace marigold::sports::parse
