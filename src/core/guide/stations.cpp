#include "core/guide/stations.hpp"

#include <unordered_set>

#include "core/util/json.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::guide::stations {
namespace {

const char* const kCallLink =
    "\\[\\[\\s*((?:[KW][A-Z]{2,3})(?:-(?:TV|DT|LD|CD|LP))?)\\s*(?:\\|[^\\]]*)?\\]\\]";
const char* const kAnyLink = "\\[\\[([^\\]|#]+)(?:\\|([^\\]]+))?\\]\\]";
const char* const kChannelNumber = "\\b(\\d{1,2})(?:\\.\\d)?\\b";

std::vector<std::string_view> splitOn(std::string_view s, std::string_view sep) {
  std::vector<std::string_view> out;
  size_t start = 0;
  while (true) {
    size_t at = s.find(sep, start);
    if (at == std::string_view::npos) {
      out.push_back(s.substr(start));
      return out;
    }
    out.push_back(s.substr(start, at - start));
    start = at + sep.size();
  }
}

}  // namespace

const std::vector<Page>& pages() {
  static const std::vector<Page> kPages = {
      {"ABC",
       {"List of ABC television affiliates (by U.S. state)", "List of ABC television affiliates"}},
      {"CBS",
       {"List of CBS television affiliates (by U.S. state)", "List of CBS television affiliates"}},
      {"NBC",
       {"List of NBC television affiliates (by U.S. state)", "List of NBC television affiliates"}},
      {"FOX",
       {"List of Fox television affiliates (by U.S. state)", "List of Fox television affiliates"}},
      {"CW", {"List of The CW affiliates", "List of The CW television affiliates (by U.S. state)"}},
      {"MYNETWORKTV",
       // The list pages are gone (2026); the network's article lists its own stations.
       {"List of MyNetworkTV affiliates (by U.S. state)", "List of MyNetworkTV affiliates", "MyNetworkTV"}},
      {"PBS", {"List of PBS member stations"}},
      {"TELEMUNDO",
       {"List of Telemundo affiliates (by U.S. state)", "List of Telemundo affiliates"}},
      {"UNIVISION", {"List of Univision affiliates", "List of Univision television affiliates"}},
      {"ION",
       {"List of Ion Television affiliates", "List of Ion Television affiliates (by U.S. state)"}}};
  return kPages;
}

std::vector<Station> parse(std::string_view text, const std::string& network) {
  std::vector<Station> stations;
  std::vector<std::string> cities;  // carried over to the next rows (rowspan)
  for (std::string_view row : splitOn(text, "\n|-")) {
    auto call = rx::first(kCallLink, row);
    if (!call) continue;
    // The city: the first ordinary link before the call sign. Marigold:
    // with every other city linked in the same cell ("Newark–New York
    // City"), each its own entry.
    std::string_view before = row.substr(0, call->begin);
    std::vector<std::string> found;
    size_t lineEnd = std::string_view::npos;
    for (const rx::Match& link : rx::all(kAnyLink, before)) {
      std::string target(link.group(1));
      if (target.find(':') != std::string::npos || rx::test(kCallLink, "[[" + target + "]]")) {
        continue;
      }
      size_t at = static_cast<size_t>(link.begin);
      if (found.empty()) {
        // The cell ends at the line's end or an inline "||" cell break.
        lineEnd = std::min(before.find('\n', at), before.find("||", at));
      } else if (lineEnd != std::string_view::npos && at > lineEnd) {
        break;  // the next cell (the state)
      }
      std::string label = link.has(2) ? std::string(link.group(2)) : target;
      size_t comma = label.find(',');
      found.emplace_back(str::trimUnicode(comma == std::string::npos ? label : label.substr(0, comma)));
    }
    if (!found.empty()) cities = std::move(found);
    std::string sign(call->group(1));
    size_t dash = sign.find('-');
    if (dash != std::string::npos) sign.resize(dash);
    std::string after = rx::replace(row.substr(call->end), "\\[\\[[^\\]]*\\]\\]", " ");
    std::string number;
    if (auto n = rx::first(kChannelNumber, after)) number = std::string(n->group(1));
    for (const std::string& city : cities) {
      std::vector<std::string> tokens;
      for (std::string& t : letterNumberTokens(str::lower(city))) {
        if (t != "city" && t != "the") tokens.push_back(std::move(t));
      }
      if (tokens.empty()) continue;
      stations.push_back({sign, network, std::move(tokens), number});
    }
  }
  return stations;
}

std::vector<Station> collect(const std::vector<std::pair<std::string, std::string>>& pageTexts) {
  std::vector<Station> out;
  std::unordered_set<std::string> seen;
  for (const auto& [network, text] : pageTexts) {
    for (Station& station : parse(text, network)) {
      std::string city = str::join(station.cityTokens, " ");
      if (seen.insert(station.callsign + "|" + network + "|" + city).second) out.push_back(std::move(station));
    }
  }
  return out;
}

std::string toJson(const std::vector<Station>& stations) {
  json::Value list = json::Value::array();
  for (const Station& s : stations) {
    json::Value v = json::Value::object();
    v.set("callsign", s.callsign);
    v.set("network", s.network);
    json::Value tokens = json::Value::array();
    for (const std::string& t : s.cityTokens) tokens.push(t);
    v.set("cityTokens", tokens);
    v.set("channelNumber", s.channelNumber);
    list.push(v);
  }
  return json::write(list);
}

std::optional<std::vector<Station>> fromJson(std::string_view text) {
  auto parsed = json::parse(text);
  if (!parsed || !parsed->isArray()) return std::nullopt;
  std::vector<Station> out;
  for (const json::Value& v : parsed->items()) {
    if (!v["callsign"].isString() || !v["network"].isString()) return std::nullopt;
    Station s;
    s.callsign = v["callsign"].asString();
    s.network = v["network"].asString();
    for (const json::Value& t : v["cityTokens"].items()) s.cityTokens.push_back(t.asString());
    s.channelNumber = v["channelNumber"].asString();
    out.push_back(std::move(s));
  }
  return out;
}

std::string wikiRequestUrl(std::string_view title) {
  static const char* const kHex = "0123456789ABCDEF";
  std::string encoded;
  for (char ch : title) {
    unsigned char c = static_cast<unsigned char>(ch);
    bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '-' || c == '.' || c == '_' || c == '~';
    if (plain) {
      encoded += ch;
    } else {
      encoded += '%';
      encoded += kHex[c >> 4];
      encoded += kHex[c & 15];
    }
  }
  return "https://en.wikipedia.org/w/api.php?action=parse&page=" + encoded +
         "&prop=wikitext&format=json&redirects=1";
}

std::optional<std::string> wikitextFromResponse(std::string_view body) {
  auto parsed = json::parse(body);
  if (!parsed) return std::nullopt;
  const json::Value& text = (*parsed)["parse"]["wikitext"]["*"];
  if (!text.isString()) return std::nullopt;
  return text.asString();
}

}  // namespace marigold::guide::stations
