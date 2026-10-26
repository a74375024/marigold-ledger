#include "core/catalog/lineup.hpp"

#include <algorithm>

#include "core/catalog/text.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::catalog {
namespace {

json::Value stringArray(const std::set<std::string>& values) {
  json::Value a = json::Value::array();
  for (const std::string& v : values) a.push(v);
  return a;
}

// Reads an optional array of strings; false when present with a wrong type.
bool readStrings(const json::Value& v, const char* key, std::set<std::string>& out) {
  if (!v.has(key) || v[key].isNull()) return true;
  if (!v[key].isArray()) return false;
  std::set<std::string> values;
  for (const json::Value& item : v[key].items()) {
    if (!item.isString()) return false;
    values.insert(item.asString());
  }
  out = std::move(values);
  return true;
}

bool readBool(const json::Value& v, const char* key, bool& out) {
  if (!v.has(key) || v[key].isNull()) return true;
  if (!v[key].isBool()) return false;
  out = v[key].asBool();
  return true;
}

}  // namespace

std::vector<std::string> MarketChoice::cityParts() const {
  std::string lowered = str::lower(name);
  // Split on any of "–—-/,&()".
  std::vector<std::string> pieces;
  std::string current;
  size_t i = 0;
  while (i < lowered.size()) {
    if (lowered.compare(i, 3, "–") == 0 || lowered.compare(i, 3, "—") == 0) {
      pieces.push_back(current);
      current.clear();
      i += 3;
      continue;
    }
    char c = lowered[i];
    if (c == '-' || c == '/' || c == ',' || c == '&' || c == '(' || c == ')') {
      pieces.push_back(current);
      current.clear();
    } else {
      current += c;
    }
    ++i;
  }
  pieces.push_back(current);
  std::vector<std::string> out;
  for (const std::string& piece : pieces) {
    std::string part(str::trimUnicode(rx::replace(piece, "\\b(ft|st)\\.?\\b", "")));
    if (text::count(part) >= 4) out.push_back(std::move(part));
  }
  return out;
}

json::Value toJson(const MarketChoice& m) {
  json::Value v = json::Value::object();
  v.set("name", m.name);
  if (m.templateName) v.set("template", *m.templateName);
  json::Value signs = json::Value::array();
  for (const std::string& s : m.callsigns) signs.push(s);
  v.set("callsigns", signs);
  return v;
}

std::optional<MarketChoice> marketChoiceFromJson(const json::Value& v) {
  if (!v.isObject() || !v["name"].isString() || !v["callsigns"].isArray()) return std::nullopt;
  MarketChoice m;
  m.name = v["name"].asString();
  if (v["template"].isString()) {
    m.templateName = v["template"].asString();
  } else if (v.has("template") && !v["template"].isNull()) {
    return std::nullopt;
  }
  for (const json::Value& s : v["callsigns"].items()) {
    if (!s.isString()) return std::nullopt;
    m.callsigns.push_back(s.asString());
  }
  return m;
}

std::set<std::string> LineupPrefs::guideCountries() const {
  std::set<std::string> out;
  for (const std::string& c : countries) {
    size_t dash = c.find('-');
    out.insert(dash == std::string::npos ? c : c.substr(0, dash));
  }
  return out;
}

bool LineupPrefs::includes(const std::optional<std::string>& market) const {
  return market && countries.count(*market) > 0;
}

std::set<std::string> LineupPrefs::marketCallsigns() const {
  std::set<std::string> out;
  for (const MarketChoice& m : markets) out.insert(m.callsigns.begin(), m.callsigns.end());
  return out;
}

std::vector<std::string> LineupPrefs::marketCityParts() const {
  std::vector<std::string> out;
  for (const MarketChoice& m : markets) {
    std::vector<std::string> parts = m.cityParts();
    out.insert(out.end(), parts.begin(), parts.end());
  }
  return out;
}

std::set<std::string> LineupPrefs::marketPlaces() const {
  std::vector<std::string> parts = marketCityParts();
  std::set<std::string> places(parts.begin(), parts.end());
  struct Alias {
    const char* city;
    std::vector<const char*> extra;
  };
  static const std::vector<Alias> kAliases = {
      {"san francisco", {"bay area", "northern california", "norcal", "california"}},
      {"sacramento", {"northern california", "norcal", "california"}},
      {"los angeles", {"southern california", "socal", "california"}},
      {"san diego", {"southern california", "socal", "california"}},
      {"new york", {"tri state", "new york", "long island", "brooklyn", "bronx", "queens"}},
      {"boston", {"new england", "massachusetts"}},
      {"chicago", {"illinois", "great lakes"}},
      {"washington", {"washington dc", "mid atlantic"}},
      {"philadelphia", {"pennsylvania", "mid atlantic"}},
      {"dallas", {"texas", "southwest"}},
      {"houston", {"texas", "southwest"}},
      {"atlanta", {"georgia", "southeast"}},
      {"miami", {"florida", "sun"}},
      {"tampa", {"florida", "sun"}},
      {"seattle", {"washington", "northwest"}},
      {"denver", {"colorado", "rocky mountain"}},
      {"phoenix", {"arizona"}},
      {"detroit", {"michigan"}},
      {"minneapolis", {"minnesota"}}};
  // Each alias tests the places as they grow (Swift mutates the set in the loop).
  for (const Alias& alias : kAliases) {
    bool here = std::any_of(places.begin(), places.end(), [&](const std::string& p) {
      return p.find(alias.city) != std::string::npos;
    });
    if (here) places.insert(alias.extra.begin(), alias.extra.end());
  }
  return places;
}

bool LineupPrefs::operator==(const LineupPrefs& o) const {
  return configured == o.configured && markets == o.markets && categories == o.categories &&
         requireGuide == o.requireGuide && suggested == o.suggested && added == o.added &&
         removed == o.removed && showHidden == o.showHidden && builtAt == o.builtAt &&
         countries == o.countries && homeMarket == o.homeMarket;
}

std::string toJson(const LineupPrefs& prefs) {
  json::Value v = json::Value::object();
  v.set("configured", prefs.configured);
  json::Value markets = json::Value::array();
  for (const MarketChoice& m : prefs.markets) markets.push(toJson(m));
  v.set("markets", markets);
  json::Value categories = json::Value::array();
  for (LineupCategory c : prefs.categories) categories.push(rawValue(c));
  v.set("categories", categories);
  v.set("requireGuide", prefs.requireGuide);
  v.set("suggested", stringArray(prefs.suggested));
  v.set("added", stringArray(prefs.added));
  v.set("removed", stringArray(prefs.removed));
  v.set("showHidden", prefs.showHidden);
  if (prefs.builtAt) v.set("builtAt", *prefs.builtAt);
  v.set("countries", stringArray(prefs.countries));
  if (prefs.homeMarket) v.set("homeMarket", *prefs.homeMarket);
  return json::write(v);
}

std::optional<LineupPrefs> lineupPrefsFromJson(std::string_view textIn) {
  auto parsed = json::parse(textIn);
  if (!parsed || !parsed->isObject()) return std::nullopt;
  const json::Value& v = *parsed;
  LineupPrefs p;
  if (!readBool(v, "configured", p.configured)) return std::nullopt;
  if (v.has("markets") && !v["markets"].isNull()) {
    if (!v["markets"].isArray()) return std::nullopt;
    for (const json::Value& item : v["markets"].items()) {
      auto m = marketChoiceFromJson(item);
      if (!m) return std::nullopt;
      p.markets.push_back(std::move(*m));
    }
  }
  if (v.has("categories") && !v["categories"].isNull()) {
    if (!v["categories"].isArray()) return std::nullopt;
    std::set<LineupCategory> categories;
    for (const json::Value& item : v["categories"].items()) {
      auto c = item.isString() ? categoryFromRaw(item.asString()) : std::nullopt;
      if (!c) return std::nullopt;
      categories.insert(*c);
    }
    p.categories = std::move(categories);
  }
  if (!readBool(v, "requireGuide", p.requireGuide)) return std::nullopt;
  if (!readStrings(v, "suggested", p.suggested)) return std::nullopt;
  if (!readStrings(v, "added", p.added)) return std::nullopt;
  if (!readStrings(v, "removed", p.removed)) return std::nullopt;
  if (!readBool(v, "showHidden", p.showHidden)) return std::nullopt;
  if (v.has("builtAt") && !v["builtAt"].isNull()) {
    if (!v["builtAt"].isNumber()) return std::nullopt;
    p.builtAt = v["builtAt"].asInt();
  }
  if (!readStrings(v, "countries", p.countries)) return std::nullopt;
  if (v.has("homeMarket") && !v["homeMarket"].isNull()) {
    if (!v["homeMarket"].isString()) return std::nullopt;
    p.homeMarket = v["homeMarket"].asString();
  }
  return p;
}

bool suggests(const EntryInfo& entry, const LineupPrefs& prefs, bool hasGuide,
              std::string_view name) {
  // Only the chosen countries / languages.
  if (!prefs.includes(entry.market)) return false;
  auto on = [&](LineupCategory c) { return prefs.categories.count(c) > 0; };
  switch (entry.kind) {
    case ChannelKind::channel:
      break;
    case ChannelKind::fast:
    case ChannelKind::loop247:
      // Free streaming and 24/7 channels are opt-in.
      return on(LineupCategory::free) && (hasGuide || !prefs.requireGuide);
    case ChannelKind::radio:
      return on(LineupCategory::radio) && hasGuide;
    case ChannelKind::event:
    case ChannelKind::placeholder:
    case ChannelKind::separator:
      return false;
  }
  auto baseSign = [](const std::string& sign) {
    size_t dash = sign.find('-');
    return dash == std::string::npos ? sign : sign.substr(0, dash);
  };
  // Core national networks: always, in the categories picked.
  if (entry.core) return on(entry.category);
  if (entry.scope == "team") return false;
  // Other countries / US Spanish: the user chose them; channels with a guide.
  if (entry.market != std::string("US")) {
    if (entry.callsign) {
      // A station (Univision, Telemundo...): only the user's market's.
      return on(LineupCategory::local) &&
             prefs.marketCallsigns().count(baseSign(*entry.callsign)) > 0;
    }
    return hasGuide || !prefs.requireGuide;
  }
  std::string lower = str::lower(name);
  auto namesMarketCity = [&] {
    for (const std::string& part : prefs.marketCityParts()) {
      if (lower.find(part) != std::string::npos) return true;
    }
    return false;
  };
  // Regional channels (another city's locals, regional sports) only for their market.
  if (str::startsWith(entry.scope, "regional")) {
    std::string place = entry.scope.size() > 9 ? entry.scope.substr(9) : std::string();
    bool here = prefs.marketPlaces().count(place) > 0 || (place == "?" && namesMarketCity());
    if (!here ||
        !on(entry.category == LineupCategory::local ? LineupCategory::local : entry.category)) {
      return false;
    }
    return hasGuide || entry.category == LineupCategory::sports ||
           entry.category == LineupCategory::local;
  }
  switch (entry.category) {
    case LineupCategory::local: {
      if (!on(LineupCategory::local) || !entry.callsign) return false;
      return prefs.marketCallsigns().count(baseSign(*entry.callsign)) > 0;
    }
    case LineupCategory::sports:
      if (!on(LineupCategory::sports)) return false;
      // Regional sports for the user's market join even without a guide.
      if (namesMarketCity()) return true;
      return hasGuide || !prefs.requireGuide;
    case LineupCategory::international:
    case LineupCategory::radio:
      return on(entry.category) && hasGuide;
    default:
      return on(entry.category) && (hasGuide || !prefs.requireGuide);
  }
}

}  // namespace marigold::catalog
