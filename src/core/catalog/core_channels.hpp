// Core US national networks (CoreChannels in tvOS): anyone with these gets
// them in My Lineup, in the categories they picked. Each alias is matched
// against catalog::coreKey of a channel's cleaned name, so every provider's
// spelling ("US: ESPN FHD", "AT&T: A&E ᴿᴬᵂ", "USA  HBO EAST") lands on one
// entry with a clean name. Data, not logic.
//
// Also RegionWords: US states, cities and regions. A channel named after one
// ("FOX 5 [SAN DIEGO]", "NBC Sports Boston") is regional and only suggested
// to viewers in that market.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/catalog/types.hpp"

namespace marigold::catalog {

struct CoreChannel {
  std::string name;
  LineupCategory category;
  std::vector<std::string> aliases;
};

struct CoreHit {
  std::string name;
  LineupCategory category;
};

const std::vector<CoreChannel>& coreChannels();
// Alias (core key) -> network; the first network listing an alias wins.
const std::unordered_map<std::string, CoreHit>& coreByAlias();

const std::vector<std::string>& regionWords();
// Marigold: a regional sports network's home places ("YES Network" ->
// new york; "NBC Sports California" -> the Bay Area and Sacramento), as
// city parts of the markets it serves; nullopt for anything else.
std::optional<std::vector<std::string>> regionalSportsPlaces(std::string_view name);
// Other channels made for one area that don't name it the way locals do:
// regional cable news and local cable channels ("NECN" -> boston, "OC16"
// -> honolulu). "?" when the area isn't in the name ("Spectrum News 1" is
// a different channel in each of its cities).
std::optional<std::vector<std::string>> regionalChannelPlaces(std::string_view name);
// One alternation, longest first ("new york" before "york"), group 1.
const std::string& regionPattern();

}  // namespace marigold::catalog
