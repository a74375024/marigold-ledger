#include "core/guide/names.hpp"

#include <algorithm>
#include <unordered_set>

#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::guide {
namespace {

bool asciiAlnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

const std::unordered_set<std::string>& dropTokens() {
  static const std::unordered_set<std::string> kDrop = {
      "hd",   "fhd",  "uhd", "sd",    "4k",    "hevc",   "h265", "h264", "1080p",
      "720p", "1080", "720", "50fps", "60fps", "backup", "alt",  "vip",  "raw",
      "feed", "usa",  "us",  "ca",    "uk",    "tv",     "east", "west", "pacific"};
  return kDrop;
}

const std::unordered_set<std::string>& callStop() {
  static const std::unordered_set<std::string> kStop = {"WWE",  "WNBA", "WEST", "WILD", "KIDS",
                                                        "WORK", "WIRE", "WAVE", "WARS", "WOW",
                                                        "KICK", "WINE", "WORD", "WEEK"};
  return kStop;
}

// Network words as they show up in IPTV channel names.
const std::unordered_map<std::string, std::string>& networkWords() {
  static const std::unordered_map<std::string, std::string> kWords = {
      {"abc", "ABC"},
      {"cbs", "CBS"},
      {"nbc", "NBC"},
      {"fox", "FOX"},
      {"cw", "CW"},
      {"mynetworktv", "MYNETWORKTV"},
      {"mytv", "MYNETWORKTV"},
      {"pbs", "PBS"},
      {"telemundo", "TELEMUNDO"},
      {"univision", "UNIVISION"},
      {"ion", "ION"}};
  return kWords;
}

}  // namespace

StationsByNetwork groupByNetwork(const std::vector<Station>& stations) {
  StationsByNetwork out;
  for (const Station& s : stations) out[s.network].push_back(s);
  return out;
}

std::vector<std::string> letterNumberTokens(std::string_view text) {
  std::vector<std::string> out;
  // \p{M}: a combining accent belongs to its letter (one Swift Character).
  for (const rx::Match& m : rx::all("[\\p{L}\\p{N}][\\p{L}\\p{N}\\p{M}]*", text)) {
    out.emplace_back(m.group(0));
  }
  return out;
}

std::string normalize(std::string_view name) {
  std::string s = str::replaceAll(str::replaceAll(str::lower(name), "&", " and "), "+", " plus ");
  bool west = rx::test("\\b(west|pacific)\\b", s);
  s = rx::replace(s, "[\\[\\(][^\\]\\)]*[\\]\\)]", " ");
  s = rx::replace(s, "^\\s*[^:|]{1,24}[:|]\\s*", "");
  std::string key;
  size_t i = 0;
  const auto& drop = dropTokens();
  while (i < s.size()) {
    while (i < s.size() && !asciiAlnum(s[i])) ++i;
    size_t start = i;
    while (i < s.size() && asciiAlnum(s[i])) ++i;
    if (i > start) {
      std::string token = s.substr(start, i - start);
      if (!drop.count(token)) key += token;
    }
  }
  return west && !key.empty() ? key + "~west" : key;
}

std::vector<std::string> callsigns(std::string_view text, bool strongContext) {
  static const char* const kPattern =
      "(?<![A-Z0-9])([KW][A-Z]{2,3})(?:[- ]?(DT|TV|LD|LP|CD|CA|HD)(\\d)?)?(?![A-Z])";
  std::string up = str::upper(text);
  std::vector<std::string> found;
  for (const rx::Match& m : rx::all(kPattern, up)) {
    std::string sign(m.group(1));
    if (callStop().count(sign)) continue;
    bool hasSuffix = m.has(2);
    bool inParens = str::endsWith(str::trimUnicode(std::string_view(up).substr(0, m.begin)), "(");
    if (!strongContext && !hasSuffix && !inParens) continue;
    std::string key = !m.has(3) || m.group(3) == "1" ? sign : sign + "-" + std::string(m.group(3));
    if (std::find(found.begin(), found.end(), key) == found.end()) found.push_back(key);
  }
  return found;
}

std::optional<std::string> stationCallsign(std::string_view name, const StationsByNetwork& stations,
                                           bool requireNumber) {
  if (stations.empty()) return std::nullopt;
  std::string lowered = rx::replace(str::lower(name), "^\\s*[^:|]{1,24}[:|]\\s*", "");
  // "PBS12" -> "pbs 12", "[Denver]" -> "denver"
  lowered = rx::replace(lowered, "([a-z])(\\d)", "$1 $2");
  std::vector<std::string> tokens = letterNumberTokens(lowered);
  // "NBC Sports Chicago" / "CBS News Chicago" are their own channels.
  for (const std::string& t : tokens) {
    if (t == "sports" || t == "news" || t == "network" || t == "plus") return std::nullopt;
  }
  const std::string* network = nullptr;
  for (const std::string& t : tokens) {
    auto it = networkWords().find(t);
    if (it != networkWords().end()) {
      network = &it->second;
      break;
    }
  }
  if (!network) return std::nullopt;
  auto candidates = stations.find(*network);
  if (candidates == stations.end()) return std::nullopt;
  std::unordered_set<std::string> words(tokens.begin(), tokens.end());
  std::unordered_set<std::string> numbers;
  for (const std::string& t : tokens) {
    if (rx::test("^\\p{N}+$", t)) numbers.insert(t);
  }
  std::optional<std::string> best;
  int bestScore = 0;
  for (const Station& station : candidates->second) {
    bool all = std::all_of(station.cityTokens.begin(), station.cityTokens.end(),
                           [&](const std::string& c) { return words.count(c) > 0; });
    if (!all) continue;
    // Strict: a name with a channel number only matches that channel.
    if (requireNumber && !numbers.empty() && !station.channelNumber.empty() &&
        !numbers.count(station.channelNumber)) {
      continue;
    }
    int score = static_cast<int>(station.cityTokens.size()) * 10;
    if (!station.channelNumber.empty() && numbers.count(station.channelNumber)) score += 5;
    if (!best || score > bestScore) {
      best = station.callsign;
      bestScore = score;
    }
  }
  return best;
}

std::optional<std::string> bareCallsign(std::string_view name) {
  static const std::unordered_set<std::string> kStop = {
      "WWE",  "WNBA", "WEST", "WILD", "KIDS", "WORK", "WIRE", "WAVE",
      "WARS", "WOW",  "KICK", "WINE", "WORD", "WEEK", "WGN",  "KING"};
  static const char* const kPattern =
      "(?<![A-Za-z0-9])([KW][A-Z]{2,3})(?:-(?:DT|TV|LD|CD)\\d?)?(?![A-Za-z0-9])";
  for (const rx::Match& m : rx::all(kPattern, name)) {
    std::string sign(m.group(1));
    if (!kStop.count(sign) && sign.size() == 4) return sign;
  }
  return std::nullopt;
}

}  // namespace marigold::guide
