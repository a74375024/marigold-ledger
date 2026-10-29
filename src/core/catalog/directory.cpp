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

namespace {

// A name's words without a leading "the" and trailing "tv", "network",
// "channel" or "television".
std::vector<std::string> looseWords(std::string_view name) {
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
  return words;
}

}  // namespace

std::string looseKey(std::string_view name) { return str::join(looseWords(name), ""); }

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
      {"Family Entertainment Television", "FETV"},
      {"FETV Family Entertainment Television", "FETV"},
      {"Oxygen True Crime", "Oxygen"},
      // Names the lists don't give (iptv-org has them as rows of their own).
      {"FXM", "FX Movie Channel"},
      {"Discovery Science", "Science Channel"},
      {"Hallmark Movies & Mysteries", "Hallmark Mystery"},
      {"Hallmark Movies/Mysteries", "Hallmark Mystery"},
      {"Hallmark M & M", "Hallmark Mystery"},
      {"Cinemax ThrillerMax", "ThrillerMax"},
      // Renamed in 2025; providers still list the old names.
      {"5StarMax", "Cinemax Classics"},
      {"ActionMax", "Cinemax Action"},
      {"MoreMax", "Cinemax Hits"},
      {"Cinemax MoreMax", "Cinemax Hits"},
      // Providers' spellings.
      {"Willow Cricket", "Willow"},
      {"Willow Cricket Extra", "Willow Xtra"},
      {"SportsNet LA Dodgers", "Spectrum SportsNet LA"},
      {"SportsNet LA", "Spectrum SportsNet LA"},
      {"NBC Sports Philadelfia", "NBC Sports Philadelphia"},
      {"Fox Sports Prime Ticket", "FanDuel Sports Network SoCal"},
  };
  return kList;
}

std::string shownName(const Entry& entry) {
  // Rows that link to their owner's article.
  static const std::unordered_map<std::string, std::string> kShown = {
      {"Blaze Media", "BlazeTV"},
  };
  auto it = kShown.find(entry.name);
  return it == kShown.end() ? entry.name : it->second;
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
  for (const auto& [field, map] : {std::pair{"logos", &data.logos}, std::pair{"logosByName", &data.logosByName}}) {
    json::Value logos = json::Value::object();
    for (const auto& [key, url] : *map) logos.set(key, url);
    v.set(field, logos);
  }
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
  for (const auto& [field, map] : {std::pair{"logos", &data.logos}, std::pair{"logosByName", &data.logosByName}}) {
    for (const auto& [key, url] : (*doc)[field].fields()) {
      if (url.isString()) (*map)[key] = url.asString();
    }
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
    byId_.emplace(e.id, i);
    bool free = e.list == "free";
    addUnique(free ? freeByName_ : byName_, exactKey(e.name), i);
    addUnique(free ? freeByLoose_ : byLoose_, looseKey(e.name), i);
    std::string own = looseKey(e.name);
    for (const std::string& alias : e.aliases) {
      // Its name and more ("GritXtra" leads to Grit's article) is a channel
      // of its own, not another name.
      std::string loose = looseKey(alias);
      if (!own.empty() && loose.size() > own.size() && str::startsWith(loose, own)) continue;
      addUnique(free ? freeByAlias_ : byAlias_, exactKey(alias), i);
      // Loosely, an other name of one word is any channel's ("The Action
      // Channel" is no "Action").
      if (looseWords(alias).size() > 1) addUnique(free ? freeByLoose_ : byLoose_, loose, i);
    }
  }
  // A correction's channel: a listed one, else one of the free tier.
  for (const Correction& c : corrections()) {
    std::string is = exactKey(c.is);
    const std::vector<size_t>* target = nullptr;
    if (auto it = byName_.find(is); it != byName_.end()) {
      target = &it->second;
    } else if (auto free = freeByName_.find(is); free != freeByName_.end()) {
      target = &free->second;
    }
    if (target && target->size() == 1) byCorrection_[exactKey(c.name)] = target->front();
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
  std::string loose = looseKey(name);
  if (auto it = byLoose_.find(loose); it != byLoose_.end()) return pick(it->second, MatchKind::loose);
  if (auto it = freeByName_.find(key); it != freeByName_.end()) return pick(it->second, MatchKind::exact);
  if (auto it = freeByAlias_.find(key); it != freeByAlias_.end()) return pick(it->second, MatchKind::alias);
  if (auto it = freeByLoose_.find(loose); it != freeByLoose_.end()) return pick(it->second, MatchKind::loose);
  if (auto it = data_->elsewhere.find(key); it != data_->elsewhere.end()) m.elsewhere = it->second;
  return m;
}

const Entry* Index::entryById(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &data_->entries[it->second];
}

const Index& builtInIndex() {
  static const Index kIndex(builtIn());
  return kIndex;
}

}  // namespace marigold::catalog::directory
