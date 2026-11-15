// The languages of Live Events' sources and of the viewer (Build 132). Each
// channel and provider feed is tagged with a language code ("en", "es",
// "ar"...) when the channel index and the listings are made, from its
// market (which the reference data and the channel directory set), the
// marks in its name ("(ES)", "Español", "English", "Hindi") and the sports
// brand it is; "" when unsure. The viewer's languages are those of the
// countries and languages picked for the channel list. A game's sources then
// go yours first (Live Events never asks).
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/sports/types.hpp"

namespace marigold::sports::languages {

// The language the name says it is in: "(ES)", "Español", "Spanish",
// "Deportes", "English", "(EN)", "Hindi", "Tamil", "Urdu", "Arabic", Arabic
// script, "French"...; "" when it says none.
std::string marked(std::string_view name);

// A channel's language: its name's mark, else its market's ("US" English,
// "US-ES" and the Spanish category Spanish; "AR" Arabic for the Gulf's
// sports brands and Arabic names, Spanish for Argentina's, else unknown; "CA"
// French for RDS and TVA), else its sports brand's (brands.hpp); "" unsure.
std::string ofChannel(std::string_view name, const std::optional<std::string>& market,
                      catalog::LineupCategory category);
inline std::string ofChannel(const catalog::EntryInfo& info) {
  return ofChannel(info.name, info.market, info.category);
}
// A provider feed's: Spanish by its market ("US-ES") or a Spanish word in
// the title (as before), else the title's mark, else its market's.
std::string ofFeed(std::string_view title, std::string_view family, const std::optional<std::string>& market);

// The viewer's languages, from the countries and languages picked
// (LineupPrefs::countries): "US" English, "US-ES" Spanish, "MX" Spanish,
// "UK" English, "IN" Hindi and English, "PK" Urdu and English, "CA" English
// and French, "AR" Spanish and Arabic (channel lists use it for both)...
// English when none says.
std::set<std::string> ofViewer(const std::set<std::string>& countries);
// English: what Live Events assumes until it's told.
const std::set<std::string>& fallback();

// "Spanish", "Arabic", "German"; "" for a code it doesn't know.
std::string name(std::string_view code);

// How a source's language fits the viewer: one of theirs, not known, or
// another.
enum class Fit : uint8_t { yours = 0, unknown = 1, other = 2 };
Fit fit(std::string_view lang, const std::set<std::string>& viewer);
// In My Lineup (chosen, whatever its language), or in one of your languages
// or an unknown one (Build 132; Build 160 orders by tier() instead).
bool welcome(const EventSource& source, const std::set<std::string>& viewer);

// Build 160: the one language order of every list of a game's sources (the
// board's, Watch's and its backups', Where to Watch's, the tile's and the
// details card's): the viewer's first language, then not known (mostly
// English channels with no mark), then the viewer's other languages, then
// every other one the source names. My Lineup, kind, rank and reliability
// only order sources within a tier.
enum class Tier : uint8_t { first = 0, unknown = 1, yours = 2, other = 3 };
// The viewer's first language: English when it's one of theirs (or they
// have none), else the first of them.
std::string_view firstLanguage(const std::set<std::string>& viewer);
Tier tier(std::string_view lang, const std::set<std::string>& viewer);
// English or not known: what's listed above Where to Watch's "In <language>"
// sections.
inline bool upTop(Tier t) { return t <= Tier::unknown; }

// The source Watch starts on, part aside: of the best tier, My Lineup's
// channel, else another channel, else a feed (in the order given within
// each); a team's channel only when there's nothing else. nullptr: none.
const EventSource* lead(const std::vector<EventSource>& sources, const std::set<std::string>& viewer);

}  // namespace marigold::sports::languages
