// US broadcast stations (call sign -> network, city, channel) from
// Wikipedia's affiliate lists, so "NBC 5 Chicago" can be matched to its
// station (StationDirectory in tvOS). The app fetches the pages about once a
// month; here the page text is the input and the caller does the requests.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/guide/names.hpp"
#include "core/util/time.hpp"

namespace marigold::guide::stations {

// Each network's affiliate-list pages, tried in order until one loads.
struct Page {
  std::string network;
  std::vector<std::string> titles;
};
const std::vector<Page>& pages();

constexpr time::Seconds kMaxAge = 30 * 24 * 3600;
// Wikipedia rate-limits bursts: the app waits this long between page requests.
constexpr int kRequestSpacingMs = 1200;

// Each table row: the first call-sign link, the city linked before it
// (carried over from the previous row for rowspans), and the channel number.
std::vector<Station> parse(std::string_view wikitext, const std::string& network);

// One directory from each network's page text (network, wikitext); the first
// station per (call sign, network) wins.
std::vector<Station> collect(const std::vector<std::pair<std::string, std::string>>& pageTexts);

// Whether a saved directory written at `modified` is still fresh.
inline bool isFresh(time::Seconds modified, time::Seconds now, bool empty) {
  return !empty && now - modified < kMaxAge;
}

std::string toJson(const std::vector<Station>& stations);
std::optional<std::vector<Station>> fromJson(std::string_view text);

// Wikimedia asks every client for a User-Agent with contact information
// (https://meta.wikimedia.org/wiki/User-Agent_policy) and rate-limits ones
// without it: every Wikipedia request sends this.
constexpr const char* kUserAgent = "Marigold/0.1 (https://github.com/a74375024/marigold-ledger; webOS TV app)";

// The Wikipedia API request for a page's wikitext, and the text out of its
// answer ({"parse": {"wikitext": {"*": "..."}}}).
std::string wikiRequestUrl(std::string_view title);
std::optional<std::string> wikitextFromResponse(std::string_view body);

}  // namespace marigold::guide::stations
