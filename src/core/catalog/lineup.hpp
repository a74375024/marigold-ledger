// The user's My Lineup choices (LineupPrefs) and the rules for which
// channels the suggested lineup holds (LineupEngine), from tvOS
// LiveTVLineup. Market choices (MarketChoice) live here too, since the
// prefs hold them.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/util/json.hpp"
#include "core/util/time.hpp"

namespace marigold::catalog {

// A TV market the user picked for My Lineup, with its stations' call signs.
struct MarketChoice {
  std::string name;
  std::optional<std::string> templateName;  // the Wikipedia template ("template" in Swift)
  std::vector<std::string> callsigns;

  const std::string& id() const { return name; }
  // City names in the market name ("San Francisco–Oakland–San Jose" ->
  // san francisco, oakland, san jose), for regional sports channels.
  std::vector<std::string> cityParts() const;
  bool operator==(const MarketChoice& o) const {
    return name == o.name && templateName == o.templateName && callsigns == o.callsigns;
  }
};

json::Value toJson(const MarketChoice& m);
std::optional<MarketChoice> marketChoiceFromJson(const json::Value& v);

// Channels with an East and a West feed (HBO, HBO West): which one My
// Lineup suggests. Automatic goes by the home market's time zone.
enum class Coast : int { automatic, east, west, both };

struct LineupPrefs {
  bool configured = false;
  std::vector<MarketChoice> markets;
  std::set<LineupCategory> categories = defaultOnCategories();
  // National channels only join the suggested lineup when they have a guide.
  bool requireGuide = true;
  // Suggested members from the last build (entry ids), kept stable so the
  // lineup doesn't shift as guide data loads.
  std::set<std::string> suggested;
  std::set<std::string> added;
  std::set<std::string> removed;
  bool showHidden = false;
  std::optional<time::Seconds> builtAt;
  // Countries / languages whose channels can be suggested and browsed: "US"
  // (US English), "US-ES" (US Spanish), "MX", "UK"... US English by default;
  // the rest are opt-in.
  std::set<std::string> countries = defaultCountries();
  // The market Live Events treats as home (its stations come first); empty =
  // the first market picked.
  std::optional<std::string> homeMarket;
  Coast coast = Coast::automatic;

  static std::set<std::string> defaultCountries() { return {"US"}; }

  // Country prefixes the provider's own guide should cover ("US-ES" -> "US").
  std::set<std::string> guideCountries() const;
  // Whether a channel / event from `market` belongs to the chosen countries.
  bool includes(const std::optional<std::string>& market) const;
  std::set<std::string> marketCallsigns() const;
  std::vector<std::string> marketCityParts() const;
  // Places that count as "here" for regional channels: the market's cities
  // plus common names for the area ("Bay Area", "California").
  std::set<std::string> marketPlaces() const;
  // The home market (homeMarket, else the first picked).
  const MarketChoice* home() const;

  bool operator==(const LineupPrefs& o) const;
};

// Tolerates older saved prefs: missing fields keep their defaults, so an
// update never resets someone's lineup. Like Swift's decoder, a field of the
// wrong type (or an unknown category) fails the whole decode: nullopt, and
// the caller starts from LineupPrefs(). Dates are Unix seconds.
std::string toJson(const LineupPrefs& prefs);
std::optional<LineupPrefs> lineupPrefsFromJson(std::string_view text);

// Whether a market is on Pacific, Mountain, Alaska or Hawaii time, where
// cable carries the West feeds (a network's West feed is its Pacific
// schedule; Mountain systems mostly take it too). nullopt: a market this
// doesn't know.
std::optional<bool> marketIsWest(std::string_view marketName);

// The coast Automatic stands for: the home market's, else the device's time
// zone (`winterOffset`: its UTC offset in January, seconds; -7 h or less is
// West).
Coast resolveCoast(const LineupPrefs& prefs, int winterOffset);
Coast resolveCoast(const LineupPrefs& prefs);  // with the device's zone

// The entries that have both an East and a West version ("e|core:HBO" and
// "e|core:HBO~west"), and the coast whose version is suggested.
struct CoastFeeds {
  std::set<std::string> paired;  // the East entry ids
  Coast coast = Coast::both;

  static CoastFeeds of(const std::vector<EntryInfo>& entries, Coast resolved);
  // A version of a pair that isn't this coast's.
  bool skips(const EntryInfo& entry) const;
};

// Whether an entry belongs in the suggested lineup (LineupEngine.suggests).
// Marigold: only one of a channel's East and West versions, unless the user
// asked for both.
bool suggests(const EntryInfo& entry, const LineupPrefs& prefs, bool hasGuide,
              std::string_view name, const CoastFeeds& coasts = {});

}  // namespace marigold::catalog
