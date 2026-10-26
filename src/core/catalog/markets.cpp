#include "core/catalog/markets.hpp"

#include <algorithm>
#include <set>
#include <unordered_set>

#include "core/catalog/text.hpp"
#include "core/util/json.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::catalog::markets {
namespace {

std::vector<std::string> partsOf(const std::string& name) {
  MarketChoice choice;
  choice.name = name;
  return choice.cityParts();
}

}  // namespace

std::vector<Market> parseMarkets(std::string_view wikitext) {
  std::vector<Market> list;
  std::unordered_set<std::string> seen;
  for (const rx::Match& m : rx::all("\\[\\[\\s*(Template:[^|\\]]+)\\|([^\\]]+)\\]\\]", wikitext)) {
    std::string templ(str::trimUnicode(m.group(1)));
    std::string name = rx::replace(m.group(2), "\\s*\\(\\d+\\)\\s*$", "");
    name = std::string(str::trimUnicode(name));
    name = str::replaceAll(name, "&nbsp;", " ");
    // Skip navigation templates ("Template:TV lists" etc.).
    std::string lowerName = str::lower(name);
    if (name.empty() || text::count(name) >= 60 || text::contains(lowerName, "list") ||
        text::contains(lowerName, "template") || !seen.insert(lowerName).second) {
      continue;
    }
    list.push_back({name, templ});
  }
  return list;
}

std::vector<Market> fallbackMarkets(const std::vector<guide::Station>& stations) {
  std::vector<Market> list;
  std::unordered_set<std::string> seen;
  for (const guide::Station& station : stations) {
    std::vector<std::string> words;
    for (const std::string& t : station.cityTokens) words.push_back(text::capitalized(t));
    std::string city = str::join(words, " ");
    if (seen.insert(city).second) list.push_back({city, std::nullopt});
  }
  std::sort(list.begin(), list.end(),
            [](const Market& a, const Market& b) { return a.name < b.name; });
  return list;
}

std::vector<std::string> callsignsInWikitext(std::string_view wikitext) {
  std::vector<std::string> found;
  for (const rx::Match& m :
       rx::all("\\[\\[\\s*([KW][A-Z]{2,3})(?:-(?:TV|DT|LD|CD|LP|CA))?\\s*(?:\\|[^\\]]*)?\\]\\]",
               wikitext)) {
    std::string sign(m.group(1));
    if (std::find(found.begin(), found.end(), sign) == found.end()) found.push_back(sign);
  }
  return found;
}

std::vector<std::string> fallbackCallsigns(const Market& market,
                                           const std::vector<guide::Station>& stations) {
  std::vector<std::string> parts = partsOf(market.name);
  std::set<std::string> signs;
  for (const guide::Station& station : stations) {
    std::string city = str::join(station.cityTokens, " ");
    bool hit = std::any_of(parts.begin(), parts.end(), [&](const std::string& p) {
      return p == city || text::contains(p, city) || text::contains(city, p);
    });
    if (hit) signs.insert(station.callsign);
  }
  return std::vector<std::string>(signs.begin(), signs.end());
}

std::vector<Market> suggested(const std::vector<Market>& markets,
                              const std::vector<std::string>& channelNames, size_t limit) {
  std::vector<std::string> names;
  names.reserve(channelNames.size());
  for (const std::string& n : channelNames) names.push_back(str::lower(n));
  std::vector<std::pair<const Market*, int>> scored;
  for (const Market& market : markets) {
    std::vector<std::string> parts = partsOf(market.name);
    if (parts.empty()) continue;
    int hits = 0;
    for (const std::string& name : names) {
      bool any = std::any_of(parts.begin(), parts.end(),
                             [&](const std::string& p) { return text::contains(name, p); });
      if (any) ++hits;
    }
    if (hits > 0) scored.emplace_back(&market, hits);
  }
  // Ties keep the list's order (biggest market first).
  std::stable_sort(scored.begin(), scored.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  std::vector<Market> out;
  for (size_t i = 0; i < scored.size() && i < limit; ++i) out.push_back(*scored[i].first);
  return out;
}

std::string toJson(const std::vector<Market>& markets) {
  json::Value list = json::Value::array();
  for (const Market& m : markets) {
    json::Value v = json::Value::object();
    v.set("name", m.name);
    if (m.templateName) v.set("template", *m.templateName);
    list.push(v);
  }
  return json::write(list);
}

std::optional<std::vector<Market>> fromJson(std::string_view textIn) {
  auto parsed = json::parse(textIn);
  if (!parsed || !parsed->isArray()) return std::nullopt;
  std::vector<Market> out;
  for (const json::Value& v : parsed->items()) {
    if (!v["name"].isString()) return std::nullopt;
    Market m;
    m.name = v["name"].asString();
    if (v["template"].isString()) m.templateName = v["template"].asString();
    out.push_back(std::move(m));
  }
  return out;
}

}  // namespace marigold::catalog::markets
