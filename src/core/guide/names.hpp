// Channel-name keys shared by the catalog and the outside guide: the
// comparable form of a name, call signs, and the station a "network + city"
// name stands for. In tvOS these live in OutsideGuide (normalize,
// callsigns(in:), stationCallsign, Station) and ChannelCatalog
// (bareCallsign); they sit here so catalog and guide don't depend on each
// other.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace marigold::guide {

// A US / Canadian broadcast station from the station directory.
struct Station {
  std::string callsign;
  std::string network;  // "ABC", "MYNETWORKTV", "TELEMUNDO"...
  std::vector<std::string> cityTokens;
  std::string channelNumber;  // may be empty

  bool operator==(const Station& o) const {
    return callsign == o.callsign && network == o.network && cityTokens == o.cityTokens &&
           channelNumber == o.channelNumber;
  }
};

// Stations grouped by network, each group in directory order
// (Swift's Dictionary(grouping:by: \.network)).
using StationsByNetwork = std::unordered_map<std::string, std::vector<Station>>;
StationsByNetwork groupByNetwork(const std::vector<Station>& stations);

// Comparable form of a channel name. East is the default feed and is
// dropped; West / Pacific feeds keep a "~west" marker so they never match
// East.
std::string normalize(std::string_view name);

// Call signs ("WNBC", "KABC-2" for a sub-channel). A bare four-letter word
// only counts in a locals context, in parentheses, or with a -DT style
// suffix, so "WILD" or "KIDS" in a network name don't.
std::vector<std::string> callsigns(std::string_view text, bool strongContext);

// "NBC 5 Chicago" -> WMAQ: a network word plus the station's city in the
// name (the channel number breaks ties). `requireNumber`: a name with a
// channel number only matches a station on that number.
std::optional<std::string> stationCallsign(std::string_view name, const StationsByNetwork& stations,
                                           bool requireNumber = false);

// A call sign written on its own in capitals ("(US) (Prime) KAGS",
// "USA: WITI (FOX)"). Common words are excluded.
std::optional<std::string> bareCallsign(std::string_view name);

// Runs of Unicode letters and numbers (Swift's split on
// !(isLetter || isNumber)).
std::vector<std::string> letterNumberTokens(std::string_view text);

}  // namespace marigold::guide
