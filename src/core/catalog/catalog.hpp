// Turns a raw channel list into entries, event families and events
// (ChannelCatalog in tvOS). Works from naming conventions common to IPTV
// lineups rather than any one provider's exact format.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/catalog/core_channels.hpp"
#include "core/catalog/types.hpp"
#include "core/guide/names.hpp"
#include "core/model/channel.hpp"
#include "core/util/time.hpp"

namespace marigold::catalog {

// Which call signs are real in this lineup, and each one's network.
struct CallsignContext {
  std::unordered_set<std::string> known;
  std::unordered_map<std::string, std::string> networkOf;
  guide::StationsByNetwork stationsByNetwork;
  // Marigold: the list doesn't mark its US channels ("ESPN", "FOX 5 New
  // York" with no "US|" group or "US:" prefix), so a channel of unknown
  // country in Latin script is taken as US. tvOS left them without a
  // country, so such lists had no core networks, locals or US count.
  bool unknownIsUS = false;

  CallsignContext() = default;
  // The station directory, plus signs seen in local groups or next to a
  // network name.
  CallsignContext(const std::vector<Channel>& channels,
                  const std::vector<guide::Station>& stations);
};

// The provider group a channel is in: its group, else its first tag.
std::string groupOf(const Channel& channel);

// The country a channel is marked with (group prefix, group words, name
// prefix), nullopt when it isn't marked.
std::optional<std::string> markedCountry(const Channel& channel);

// Pass 0: call-sign context; pass 1: traits and event families; pass 2:
// entries (clusters of variants); pass 3: event families and events.
CatalogResult build(const std::vector<Channel>& channels, time::Seconds now,
                    const std::vector<guide::Station>& stations = {});

struct EventsResult {
  std::vector<EventFamily> families;
  std::vector<LiveEvent> events;
};

// Event families (numbered event feeds and event-slot groups) and the events
// they carry, from channels whose traits name a family.
EventsResult buildEvents(const std::vector<Channel>& channels,
                         const std::unordered_map<std::string, ChannelTraits>& traits,
                         time::Seconds now);

// After a refresh in which only event slots were renamed (the same
// channels, regular channels unchanged): re-reads those slots and rebuilds
// Live Events, without classifying everything again.
void refreshEvents(CatalogResult& result, const std::vector<Channel>& channels,
                   const std::vector<Channel>& renamed, time::Seconds now);

// `numberedFamilies` false reads the name as if no numbered event family
// matched (build() uses it when a family turns out too small).
ChannelTraits traitsFor(const Channel& channel, const CallsignContext& context = CallsignContext(),
                        bool numberedFamilies = true);

// MARK: Classification

// Provider groups (lower-cased) that hold event slots rather than channels.
bool isEventGroup(std::string_view group);
// A copy in a free streaming app's group (Roku, Pluto, Tubi, Samsung TV
// Plus, Plex, Xumo...) or named like one ("RK: ...", "(Pluto TV) ...").
bool freeStreamingCopy(const Channel& channel);
// HBO, Cinemax, Showtime, Starz, Epix, MGM+, AMC+, Paramount+.
bool premiumName(std::string_view name);
// Provider groups (any case) for sports: event slots, pay-per-view, sports
// channels ("UK| SKY SPORTS", "DE| SOCCER PPV", "CA| TSN+", "SE| HOCKEY").
bool sportsGroup(std::string_view group);
ChannelKind kindFor(std::string_view name, std::string_view lower, std::string_view group,
                    bool isCore);

struct CallsignCandidate {
  std::string sign;
  bool inParens = false;
};
// Call sign candidates ("KTVU", "KGO" in parentheses, "KABC-DT") and whether
// each was written in parentheses.
std::vector<CallsignCandidate> callsignCandidates(std::string_view name, bool allowShort = false);

// The broadcast network named in a channel name, normalized ("TMO" ->
// "TELEMUNDO", "MyNetworkTV" -> "MYTV", "IND" -> "INDEPENDENT").
std::optional<std::string> networkWord(std::string_view name);

struct CoreKey {
  std::string key;
  bool west = false;
};
// A channel name reduced for matching core networks: lower-case words,
// "&" -> "and", "+" -> "plus", quality tags and "East" dropped.
CoreKey coreKey(std::string_view display);
std::optional<CoreHit> coreMatch(std::string_view display);

std::optional<int> trailingNumber(std::string_view name);
// guide::bareCallsign, under its tvOS name.
inline std::optional<std::string> bareCallsign(std::string_view name) {
  return guide::bareCallsign(name);
}

struct FamilyParts {
  std::string key;
  int number = 0;
  std::optional<std::string> rest;
  bool separator = false;
};
// "US (ESPN+ 006) | The Pat McAfee Show...", "ESPN+ 48: Title", "PPV 03",
// "DAZN UK | Event 7", "NCAAF 02 Florida @ Texas 12PM".
std::optional<FamilyParts> familyParts(std::string_view name);

bool isPlaceholder(std::string_view text);
// Display name without provider decorations: country / source prefixes,
// quality tags and source markers.
std::string cleanName(std::string_view name);
std::optional<int> virtualNumber(std::string_view name);

// MARK: Events (events.cpp)

// The zone a provider writes unmarked times in, from its country tag.
std::optional<time::Zone> eventZone(const std::optional<std::string>& market);

struct ParsedEvent {
  std::string title;
  std::optional<time::Seconds> start;
  // The title gave only a clock time, so the day is a guess (today).
  bool guessed = false;
};
// Title and start time from an event name. Times without a zone are US
// Eastern unless written European-style ("22 October 2026 17:45", taken as
// UK time); a provider's country tag sets the zone for unmarked times.
ParsedEvent parseEventDetailed(std::string_view text, time::Seconds now,
                               const std::optional<std::string>& market = std::nullopt);
std::pair<std::string, std::optional<time::Seconds>> parseEvent(
    std::string_view text, time::Seconds now,
    const std::optional<std::string>& market = std::nullopt);

// Deduplication key: normalized title plus the half-hour slot of the start.
std::string eventKey(std::string_view title, const std::optional<time::Seconds>& start);
std::string leagueFor(std::string_view family, std::string_view group, std::string_view title);

// Fight promotions and terms, as whole words ("ufc_ppv" and "ufc333" count;
// "Boxing Day" football doesn't).
extern const char* const kFightWords;
// Pro wrestling (WWE, AEW, NXT, TNA, ROH...): its own league, "Pro Wrestling".
extern const char* const kProWrestling;
extern const char* const kProWrestlingWords;

}  // namespace marigold::catalog
