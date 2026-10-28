#include "core/catalog/directory.hpp"

#include <algorithm>
#include <cstddef>
#include <unordered_set>

#include "core/util/json.hpp"
#include "core/util/strings.hpp"

// The build's directory file as bytes (channel_directory_blob.cpp, made by CMake).
extern const unsigned char kMarigoldChannelDirectory[];
extern const std::size_t kMarigoldChannelDirectorySize;

namespace marigold::catalog::directory {
namespace {

// The accented letters channel names use, as plain ones ("Español"), also
// when a provider's list mangled them ("EspaÃ±ol").
std::string plainLetters(std::string_view s) {
  static const std::pair<const char*, char> kAccents[] = {
      {"Ã±", 'n'}, {"Ã¡", 'a'}, {"Ã©", 'e'}, {"Ã­", 'i'}, {"Ã³", 'o'}, {"Ãº", 'u'}, {"Ã¼", 'u'},
      {"á", 'a'}, {"à", 'a'}, {"â", 'a'}, {"ä", 'a'}, {"ã", 'a'}, {"é", 'e'}, {"è", 'e'}, {"ê", 'e'},
      {"ë", 'e'}, {"í", 'i'}, {"ì", 'i'}, {"î", 'i'}, {"ï", 'i'}, {"ó", 'o'}, {"ò", 'o'}, {"ô", 'o'},
      {"ö", 'o'}, {"õ", 'o'}, {"ú", 'u'}, {"ù", 'u'}, {"û", 'u'}, {"ü", 'u'}, {"ñ", 'n'}, {"ç", 'c'},
      {"Á", 'a'}, {"É", 'e'}, {"Í", 'i'}, {"Ó", 'o'}, {"Ú", 'u'}, {"Ñ", 'n'}, {"Ü", 'u'}, {"Ç", 'c'}};
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (static_cast<unsigned char>(s[i]) >= 0x80) {
      bool replaced = false;
      for (const auto& [from, to] : kAccents) {
        size_t n = std::char_traits<char>::length(from);
        if (s.substr(i, n) == from) {
          out += to;
          i += n;
          replaced = true;
          break;
        }
      }
      if (replaced) continue;
    }
    out += s[i++];
  }
  return out;
}

std::vector<std::string> nameWords(std::string_view name) {
  static const std::unordered_set<std::string> kQuality = {"hd",  "fhd",    "uhd",  "sd",    "4k",    "8k",
                                                           "hevc", "raw",   "vip",  "1080p", "720p",  "60fps",
                                                           "50fps", "backup", "feed", "alt"};
  std::string lowered = str::lower(plainLetters(name));
  lowered = str::replaceAll(str::replaceAll(lowered, "&", " and "), "+", " plus ");
  std::vector<std::string> words;
  std::string word;
  int depth = 0;
  auto flush = [&] {
    if (!word.empty() && !kQuality.count(word)) words.push_back(word);
    word.clear();
  };
  for (char c : lowered) {
    if (c == '(' || c == '[') {
      flush();
      ++depth;
    } else if (c == ')' || c == ']') {
      depth = std::max(0, depth - 1);
    } else if (depth > 0) {
      continue;
    } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      word += c;
    } else {
      flush();
    }
  }
  flush();
  // A feed's coast at the end, a US marker at either end.
  while (words.size() > 1 && (words.back() == "east" || words.back() == "west" || words.back() == "pacific"))
    words.pop_back();
  auto marker = [](const std::string& w) { return w == "us" || w == "usa"; };
  if (words.size() > 1 && marker(words.back())) words.pop_back();
  // ("USA Network" keeps its own.)
  if (words.size() > 1 && marker(words.front()) && !(words.size() == 2 && words[1] == "network"))
    words.erase(words.begin());
  return words;
}

void addUnique(std::unordered_map<std::string, std::vector<size_t>>& map, const std::string& key, size_t i) {
  if (key.empty()) return;
  std::vector<size_t>& list = map[key];
  if (std::find(list.begin(), list.end(), i) == list.end()) list.push_back(i);
}

json::Value strings(const std::vector<std::string>& list) {
  json::Value a = json::Value::array();
  for (const std::string& s : list) a.push(s);
  return a;
}

std::vector<std::string> stringsFrom(const json::Value& v) {
  std::vector<std::string> out;
  for (const json::Value& item : v.items()) {
    if (item.isString()) out.push_back(item.asString());
  }
  return out;
}

}  // namespace

bool Entry::operator==(const Entry& o) const {
  return id == o.id && name == o.name && aliases == o.aliases && language == o.language && genre == o.genre &&
         list == o.list && places == o.places && closed == o.closed && wikidata == o.wikidata && iptvOrg == o.iptvOrg;
}

std::string exactKey(std::string_view name) { return str::join(nameWords(name), ""); }

std::string looseKey(std::string_view name) {
  std::vector<std::string> words = nameWords(name);
  if (words.size() > 1 && words.front() == "the") words.erase(words.begin());
  static const char* const kTails[] = {"television", "network", "channel", "tv"};
  for (bool again = true; again && !words.empty();) {
    again = false;
    std::string& last = words.back();
    for (const char* tail : kTails) {
      if (last == tail && words.size() > 1) {
        words.pop_back();
        again = true;
        break;
      }
      // Run into the name ("UPtv", "BabyTV"): two letters left at least.
      if (std::string_view(tail) == "tv" && last.size() >= 4 && str::endsWith(last, "tv")) {
        last.resize(last.size() - 2);
        again = true;
        break;
      }
    }
  }
  return str::join(words, "");
}

const char* rawValue(MatchKind kind) {
  switch (kind) {
    case MatchKind::exact: return "exact";
    case MatchKind::alias: return "other name";
    case MatchKind::loose: return "loose";
    case MatchKind::ambiguous: return "ambiguous";
    case MatchKind::none: break;
  }
  return "none";
}

const std::vector<Correction>& corrections() {
  static const std::vector<Correction> kList = {
      {"WGN America", "NewsNation"},
      {"Starz Family", "Starz Encore Family"},
      {"FETV", "Family Entertainment Television"},
      {"Oxygen True Crime", "Oxygen"},
  };
  return kList;
}

std::string toJson(const Data& data) {
  json::Value v = json::Value::object();
  v.set("version", 1);
  v.set("made", data.made);
  json::Value list = json::Value::array();
  for (const Entry& e : data.entries) {
    json::Value item = json::Value::object();
    item.set("id", e.id);
    item.set("name", e.name);
    if (!e.aliases.empty()) item.set("aliases", strings(e.aliases));
    if (!e.language.empty()) item.set("language", e.language);
    if (!e.genre.empty()) item.set("genre", e.genre);
    if (!e.list.empty()) item.set("list", e.list);
    if (!e.places.empty()) item.set("places", strings(e.places));
    if (!e.closed.empty()) item.set("closed", e.closed);
    if (!e.wikidata.empty()) item.set("wikidata", e.wikidata);
    if (!e.iptvOrg.empty()) item.set("iptvOrg", e.iptvOrg);
    list.push(item);
  }
  v.set("entries", list);
  json::Value elsewhere = json::Value::object();
  for (const auto& [key, country] : data.elsewhere) elsewhere.set(key, country);
  v.set("elsewhere", elsewhere);
  return json::write(v);
}

std::optional<Data> fromJson(std::string_view text) {
  auto doc = json::parse(text);
  if (!doc || !doc->isObject()) return std::nullopt;
  Data data;
  if ((*doc)["made"].isString()) data.made = (*doc)["made"].asString();
  for (const json::Value& item : (*doc)["entries"].items()) {
    if (!item["id"].isString() || !item["name"].isString() || item["name"].asString().empty()) continue;
    Entry e;
    e.id = item["id"].asString();
    e.name = item["name"].asString();
    e.aliases = stringsFrom(item["aliases"]);
    e.language = item["language"].asString();
    e.genre = item["genre"].asString();
    e.list = item["list"].asString();
    e.places = stringsFrom(item["places"]);
    e.closed = item["closed"].asString();
    e.wikidata = item["wikidata"].asString();
    e.iptvOrg = item["iptvOrg"].asString();
    data.entries.push_back(std::move(e));
  }
  for (const auto& [key, country] : (*doc)["elsewhere"].fields()) {
    if (country.isString()) data.elsewhere[key] = country.asString();
  }
  return data;
}

const Data& builtIn() {
  static const Data kData = [] {
    if (kMarigoldChannelDirectorySize == 0) return Data{};
    std::string_view text(reinterpret_cast<const char*>(kMarigoldChannelDirectory), kMarigoldChannelDirectorySize);
    return fromJson(text).value_or(Data{});
  }();
  return kData;
}

Index::Index(const Data& data) : data_(&data) {
  for (size_t i = 0; i < data.entries.size(); ++i) {
    const Entry& e = data.entries[i];
    addUnique(byName_, exactKey(e.name), i);
    addUnique(byLoose_, looseKey(e.name), i);
    for (const std::string& alias : e.aliases) {
      addUnique(byAlias_, exactKey(alias), i);
      addUnique(byLoose_, looseKey(alias), i);
    }
  }
  for (const Correction& c : corrections()) {
    auto target = byName_.find(exactKey(c.is));
    if (target != byName_.end() && target->second.size() == 1) byCorrection_[exactKey(c.name)] = target->second.front();
  }
}

Match Index::match(std::string_view name) const {
  Match m;
  std::string key = exactKey(name);
  if (key.empty()) return m;
  auto pick = [&](const std::vector<size_t>& list, MatchKind kind) {
    if (list.size() == 1) {
      m.entry = &data_->entries[list.front()];
      m.kind = kind;
    } else {
      m.kind = MatchKind::ambiguous;
    }
    return m;
  };
  if (auto it = byCorrection_.find(key); it != byCorrection_.end()) {
    m.entry = &data_->entries[it->second];
    m.kind = MatchKind::alias;
    return m;
  }
  if (auto it = byName_.find(key); it != byName_.end()) return pick(it->second, MatchKind::exact);
  if (auto it = byAlias_.find(key); it != byAlias_.end()) return pick(it->second, MatchKind::alias);
  if (auto it = byLoose_.find(looseKey(name)); it != byLoose_.end()) return pick(it->second, MatchKind::loose);
  if (auto it = data_->elsewhere.find(key); it != data_->elsewhere.end()) m.elsewhere = it->second;
  return m;
}

const Index& builtInIndex() {
  static const Index kIndex(builtIn());
  return kIndex;
}

}  // namespace marigold::catalog::directory
