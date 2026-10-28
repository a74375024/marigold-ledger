// Wikipedia's US TV-market list, each market's stations and the networks'
// affiliate lists, built into the app: the build fetches and reads them
// (tools/wikidata, at most weekly in CI), so a TV never contacts Wikipedia.
// A build made without them has none: the built-in market names stand in
// and station call signs come from what's left (fallbackCallsigns).
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/markets.hpp"
#include "core/guide/names.hpp"

namespace marigold::catalog::wiki {

struct Data {
  std::string made;  // "2026-10-28", when the build read the pages
  // Biggest first, with their station-list page.
  std::vector<markets::Market> markets;
  // markets::nameKey(market name) -> the call signs on its page.
  std::map<std::string, std::vector<std::string>> callsigns;
  std::vector<guide::Station> stations;

  bool empty() const { return markets.empty() && stations.empty(); }
  const std::vector<std::string>* callsignsOf(std::string_view marketName) const;
};

std::string toJson(const Data& data);
std::optional<Data> fromJson(std::string_view text);

// The data built into this app (read once); empty when the build had none.
const Data& builtIn();

}  // namespace marigold::catalog::wiki
