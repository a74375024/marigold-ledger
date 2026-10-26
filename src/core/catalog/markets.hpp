// US TV markets (Nielsen DMAs) and the stations in each, from Wikipedia
// (MarketDirectory in tvOS): the market list is fetched once, and only the
// picked market's station list after that, falling back to station-directory
// cities when Wikipedia can't be reached. The page text is the input here;
// the caller does the requests (guide::stations::wikiRequestUrl) and keeps
// the cache file.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/lineup.hpp"
#include "core/guide/names.hpp"
#include "core/util/time.hpp"

namespace marigold::catalog::markets {

struct Market {
  std::string name;
  std::optional<std::string> templateName;
  const std::string& id() const { return name; }
  bool operator==(const Market& o) const {
    return name == o.name && templateName == o.templateName;
  }
};

// The Wikipedia page that lists every market.
constexpr const char* kListPage = "List of television stations in North America by media market";
// The saved market list is used for this long.
constexpr time::Seconds kMaxAge = 90 * 24 * 3600;
inline bool cacheFresh(time::Seconds modified, time::Seconds now, bool empty) {
  return !empty && now - modified < kMaxAge;
}

// Markets are template links on the list page:
// `[[Template:San Francisco Bay Area TV|San Francisco–Oakland–San Jose]]`.
// Biggest first (the page's DMA ranking order).
std::vector<Market> parseMarkets(std::string_view wikitext);

// Marigold: every US market (Nielsen DMAs, biggest first) built in, for when
// the list page can't be read and nothing is saved, so "Where do you watch?"
// never comes up empty. No template names: station call signs then come
// from the station directory (fallbackCallsigns).
const std::vector<Market>& builtinMarkets();

// When the list page can't be read: cities from the station directory,
// sorted by name.
std::vector<Market> fallbackMarkets(const std::vector<guide::Station>& stations);

// Call signs of the stations on a market's template page.
std::vector<std::string> callsignsInWikitext(std::string_view wikitext);

// When the template page can't be read (or lists none): station-directory
// stations whose city is in the market name, sorted.
std::vector<std::string> fallbackCallsigns(const Market& market,
                                           const std::vector<guide::Station>& stations);

// Markets the user's channel names point to most ("San Francisco" appears
// in 30 local channel names -> that market first).
std::vector<Market> suggested(const std::vector<Market>& markets,
                              const std::vector<std::string>& channelNames, size_t limit = 6);

std::string toJson(const std::vector<Market>& markets);
std::optional<std::vector<Market>> fromJson(std::string_view text);

}  // namespace marigold::catalog::markets
