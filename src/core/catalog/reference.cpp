#include "core/catalog/reference.hpp"

#include <algorithm>
#include <cstddef>

#include "core/catalog/directory.hpp"
#include "core/util/gunzip.hpp"
#include "core/util/json.hpp"
#include "core/util/strings.hpp"

// The build's reference data as bytes (lineup_reference_blob.cpp, made by CMake).
extern const unsigned char kMarigoldLineupReference[];
extern const std::size_t kMarigoldLineupReferenceSize;

namespace marigold::catalog::reference {
namespace {

std::vector<std::string> stringsFrom(const json::Value& v) {
  std::vector<std::string> out;
  for (const json::Value& item : v.items()) {
    if (item.isString()) out.push_back(item.asString());
  }
  return out;
}

void addUnique(std::unordered_map<std::string, std::vector<size_t>>& map, const std::string& key, size_t i) {
  if (key.empty()) return;
  std::vector<size_t>& list = map[key];
  if (std::find(list.begin(), list.end(), i) == list.end()) list.push_back(i);
}

}  // namespace

const Feed* Channel::feed(std::string_view which) const {
  for (const Feed& f : feeds) {
    if (f.name == which) return &f;
  }
  return nullptr;
}

std::string bareCallsign(std::string_view sign) {
  std::string upper = str::upper(std::string(str::trim(sign)));
  size_t dash = upper.find('-');
  return dash == std::string::npos ? upper : upper.substr(0, dash);
}

std::optional<Data> fromJson(std::string_view text) {
  auto doc = json::parse(text);
  if (!doc || !doc->isObject()) return std::nullopt;
  Data data;
  data.made = (*doc)["made"].asString();
  for (const json::Value& item : (*doc)["channels"].items()) {
    if (!item["i"].isString() || !item["n"].isString()) continue;
    Channel c;
    c.id = item["i"].asString();
    c.name = item["n"].asString();
    c.aliases = stringsFrom(item["a"]);
    c.category = categoryFromRaw(item["g"].asString()).value_or(LineupCategory::entertainment);
    c.kind = item["k"].asString();
    c.language = item["l"].asString();
    c.country = item["o"].asString();
    c.markets = stringsFrom(item["m"]);
    c.parent = item["p"].asString();
    c.logo = item["L"].asString();
    c.closed = item["x"].asInt() != 0;
    for (const auto& [name, aliases] : item["f"].fields()) c.feeds.push_back({name, stringsFrom(aliases)});
    data.channels.push_back(std::move(c));
  }
  data.gone = stringsFrom((*doc)["gone"]);
  for (const json::Value& item : (*doc)["stations"].items()) {
    if (!item["c"].isString()) continue;
    Station s;
    s.callsign = item["c"].asString();
    s.network = item["n"].asString();
    s.market = item["m"].asString();
    s.number = item["v"].asString();
    s.language = item["l"].asString();
    s.licence = item["k"].asString();
    for (const json::Value& sub : item["s"].items()) s.subChannels.push_back({sub["n"].asString(), sub["v"].asString()});
    s.former = stringsFrom(item["f"]);
    s.simulcasts = item["w"].asString();
    data.stations.push_back(std::move(s));
  }
  return data;
}

std::optional<Data> fromBytes(std::string_view bytes) {
  std::string text;
  gz::Gunzip reader([&](std::string_view chunk) {
    text.append(chunk);
    return true;
  }, size_t(64) << 20);
  if (!reader.feed(bytes) || !reader.finish()) return std::nullopt;
  return fromJson(text);
}

const Data& builtIn() {
  static const Data kData = [] {
    if (kMarigoldLineupReferenceSize == 0) return Data{};
    std::string_view bytes(reinterpret_cast<const char*>(kMarigoldLineupReference), kMarigoldLineupReferenceSize);
    return fromBytes(bytes).value_or(Data{});
  }();
  return kData;
}

Index::Index(const Data& data) : data_(&data) {
  for (size_t i = 0; i < data.channels.size(); ++i) {
    const Channel& c = data.channels[i];
    byId_.emplace(c.id, i);
    if (c.closed) gone_.emplace(directory::exactKey(c.name), i);
    if (c.kind == "broadcast") continue;
    addUnique(byName_, directory::exactKey(c.name), i);
    addUnique(byLoose_, directory::looseKey(c.name), i);
    for (const std::string& alias : c.aliases) {
      addUnique(byName_, directory::exactKey(alias), i);
      // Loosely only names of more than one word: "The Action Channel" is no "Action".
      std::string loose = directory::looseKey(alias);
      if (loose != directory::exactKey(alias) || loose.size() > 4) addUnique(byLoose_, loose, i);
    }
    for (size_t f = 0; f < c.feeds.size(); ++f) {
      for (const std::string& alias : c.feeds[f].aliases) {
        std::string key = directory::exactKey(alias);
        if (key.empty()) continue;
        auto& list = byFeed_[key];
        if (std::find(list.begin(), list.end(), std::pair{i, f}) == list.end()) list.emplace_back(i, f);
      }
    }
  }
  for (const std::string& name : data.gone) gone_.emplace(directory::exactKey(name), data.channels.size());
  for (size_t i = 0; i < data.stations.size(); ++i) {
    const Station& s = data.stations[i];
    std::string bare = bareCallsign(s.callsign);
    std::vector<size_t>& list = stationBySign_[bare];
    // Full power first.
    if (s.licence.empty()) list.insert(list.begin(), i);
    else list.push_back(i);
    if (bare != str::upper(s.callsign)) stationBySign_[str::upper(s.callsign)].push_back(i);
    for (const std::string& f : s.former) stationByFormer_.emplace(bareCallsign(f), i);
  }
}

Match Index::match(std::string_view name) const {
  std::string key = directory::exactKey(name);
  if (key.empty()) return {};
  // A feed's own name ("Showtime West") wins over its channel's.
  if (auto it = byFeed_.find(key); it != byFeed_.end()) {
    const auto& hits = it->second;
    const Channel& c = data_->channels[hits.front().first];
    if (hits.size() == 1) return {&c, &c.feeds[hits.front().second]};
    // "Epix" and "Epix West" (the key drops a coast): one channel, the
    // name's coast picks the feed.
    bool one = std::all_of(hits.begin(), hits.end(), [&](const auto& h) { return h.first == hits.front().first; });
    if (one) return {&c, nullptr};
  }
  auto it = byName_.find(key);
  if (it != byName_.end()) {
    if (it->second.size() != 1) return {};
    return {&data_->channels[it->second.front()], nullptr};
  }
  auto loose = byLoose_.find(directory::looseKey(name));
  if (loose == byLoose_.end() || loose->second.size() != 1) return {};
  return {&data_->channels[loose->second.front()], nullptr};
}

bool Index::gone(std::string_view name) const {
  std::string key = directory::exactKey(name);
  return !key.empty() && gone_.count(key) > 0;
}

const Station* Index::station(std::string_view callsign) const {
  std::string sign = str::upper(std::string(str::trim(callsign)));
  auto it = stationBySign_.find(sign);
  if (it == stationBySign_.end()) it = stationBySign_.find(bareCallsign(sign));
  if (it == stationBySign_.end() || it->second.empty()) return nullptr;
  return &data_->stations[it->second.front()];
}

const Station* Index::formerly(std::string_view callsign) const {
  auto it = stationByFormer_.find(bareCallsign(callsign));
  return it == stationByFormer_.end() ? nullptr : &data_->stations[it->second];
}

const Channel* Index::channel(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : &data_->channels[it->second];
}

std::vector<const Station*> Index::stationsIn(std::string_view market) const {
  std::vector<const Station*> out;
  for (const Station& s : data_->stations) {
    if (s.market == market) out.push_back(&s);
  }
  return out;
}

const Index& builtInIndex() {
  static const Index kIndex(builtIn());
  return kIndex;
}

}  // namespace marigold::catalog::reference
