// Reference data for the lineup: US channels with their other names and
// feeds, and US stations with their network, market and channel number.
// Built into the app (cmake/lineup-reference.cmake); a TV never fetches it.
// Where it knows a channel, it decides the row, name and section; the
// rules and the channel directory decide the rest.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/catalog/types.hpp"

namespace marigold::catalog::reference {

struct Feed {
  std::string name;                  // "east", "west", "plus", "alternate"
  std::vector<std::string> aliases;  // names only this feed goes by
};

struct Channel {
  std::string id;  // stable ("hbo-hits")
  std::string name;
  std::vector<std::string> aliases;  // other and former names
  LineupCategory category = LineupCategory::entertainment;
  std::string kind;      // "", "premium", "broadcast", "regional"
  std::string language;  // "es", "hi"...; empty: English
  std::string country;   // its home country when not the US
  std::vector<std::string> markets;  // regional: the TV markets it serves
  std::string parent;                // another channel's id ("hbo")
  std::string logo;                  // a file in the logo library
  bool closed = false;
  std::vector<Feed> feeds;

  const Feed* feed(std::string_view which) const;
};

struct SubChannel {
  std::string network;  // "NBC"
  std::string number;   // "12.2"
};

struct Station {
  std::string callsign;  // as licensed ("WBOC-TV")
  std::string network;   // "NBC", "INDEPENDENT"...
  std::string market;    // the app's market name
  std::string number;    // "12", "30.1"
  std::string language;  // empty: English
  std::string licence;   // "", "A" (Class A), "LP" (low power)
  std::vector<SubChannel> subChannels;
  std::vector<std::string> former;  // earlier call signs
  std::string simulcasts;           // the sister station it carries
};

struct Data {
  std::string made;
  std::vector<Channel> channels;
  std::vector<std::string> gone;  // channels that shut down and have no row
  std::vector<Station> stations;

  bool empty() const { return channels.empty() && stations.empty(); }
};

std::optional<Data> fromJson(std::string_view text);
// The packed form (gzip of the JSON), or the JSON itself.
std::optional<Data> fromBytes(std::string_view bytes);
// The data built into this app (empty when it was built without).
const Data& builtIn();

// "KBMT-DT2" -> "KBMT".
std::string bareCallsign(std::string_view sign);

struct Match {
  const Channel* channel = nullptr;
  const Feed* feed = nullptr;  // a name only this feed goes by
};

class Index {
 public:
  explicit Index(const Data& data);
  // A channel's cleaned name: its name, another name or a feed's name
  // (directory::exactKey), else loosely (directory::looseKey: "Cowboy
  // Channel" is The Cowboy Channel). A name two channels share matches
  // neither. The broadcast networks themselves never match: a provider's
  // "CBS" is a station.
  Match match(std::string_view name) const;
  // A name of a channel that shut down: one without a row, or a closed row.
  bool gone(std::string_view name) const;
  // A station by call sign, bare or full; of two licences with one bare
  // sign (WBOC-TV, WBOC-LD) the full-power one.
  const Station* station(std::string_view callsign) const;
  // The station that holds a call sign now, by one it had before.
  const Station* formerly(std::string_view callsign) const;
  const Channel* channel(const std::string& id) const;
  // The stations in a market, by the app's market name.
  std::vector<const Station*> stationsIn(std::string_view market) const;
  const Data& data() const { return *data_; }

 private:
  const Data* data_;
  std::unordered_map<std::string, std::vector<size_t>> byName_;
  std::unordered_map<std::string, std::vector<size_t>> byLoose_;
  std::unordered_map<std::string, std::vector<std::pair<size_t, size_t>>> byFeed_;  // channel, feed
  std::unordered_map<std::string, size_t> byId_;
  std::unordered_map<std::string, size_t> gone_;
  std::unordered_map<std::string, std::vector<size_t>> stationBySign_;
  std::unordered_map<std::string, size_t> stationByFormer_;
};

// The built-in data's index, made on first use.
const Index& builtInIndex();

}  // namespace marigold::catalog::reference
