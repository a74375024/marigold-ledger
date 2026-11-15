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
  for (const json::Value& item : (*doc)["gone"].items()) {
    if (item.isString()) {
      data.gone.push_back({item.asString(), std::string()});
    } else if (item["n"].isString()) {
      data.gone.push_back({item["n"].asString(), item["p"].asString()});
    }
  }
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
  // Build 161: a name that says a coast ("Showtime West", "Max East") is
  // filed under its key plus the coast (directory::NameCoast), so only a
  // name that says that coast finds it.
  auto exact = [](std::string_view name) {
    return directory::exactKey(name) + directory::coastSuffix(directory::nameCoast(name));
  };
  auto loosely = [](std::string_view name) {
    return directory::looseKey(name) + directory::coastSuffix(directory::nameCoast(name));
  };
  for (size_t i = 0; i < data.channels.size(); ++i) {
    const Channel& c = data.channels[i];
    byId_.emplace(c.id, i);
    if (c.closed) gone_.emplace(exact(c.name), i);
    if (c.kind == "broadcast") continue;
    addUnique(byName_, exact(c.name), i);
    addUnique(byLoose_, loosely(c.name), i);
    for (const std::string& alias : c.aliases) {
      addUnique(byName_, exact(alias), i);
      if (c.closed) gone_.emplace(exact(alias), i);
      // Loosely only names of more than one word: "The Action Channel" is no "Action".
      std::string loose = directory::looseKey(alias);
      if (loose != directory::exactKey(alias) || loose.size() > 4) addUnique(byLoose_, loosely(alias), i);
    }
    for (size_t f = 0; f < c.feeds.size(); ++f) {
      for (const std::string& alias : c.feeds[f].aliases) {
        if (directory::exactKey(alias).empty()) continue;
        auto& list = byFeed_[exact(alias)];
        if (std::find(list.begin(), list.end(), std::pair{i, f}) == list.end()) list.emplace_back(i, f);
      }
    }
  }
  for (const Gone& g : data.gone) {
    std::string key = exact(g.name);
    gone_.emplace(key, data.channels.size());
    if (auto parent = byId_.find(g.parent); !g.parent.empty() && parent != byId_.end()) {
      if (!data.channels[parent->second].closed) goneParent_.emplace(key, parent->second);
    }
  }
  for (const Station& s : data.stations) {
    if (!s.network.empty()) stationsByNetwork_[s.network].push_back({&s, s.number});
    for (const SubChannel& sub : s.subChannels) {
      if (!sub.network.empty() && sub.network != s.network) stationsByNetwork_[sub.network].push_back({&s, sub.number});
    }
  }
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

namespace {

// A map's entry for a key: first the key with the name's coast ("max~east"),
// then the bare key. `inName`: the first.
template <typename Map>
const typename Map::mapped_type* findKey(const Map& map, const std::string& key, const std::string& suffix,
                                         bool* inName) {
  if (!suffix.empty()) {
    if (auto it = map.find(key + suffix); it != map.end()) {
      *inName = true;
      return &it->second;
    }
  }
  *inName = false;
  auto it = map.find(key);
  return it == map.end() ? nullptr : &it->second;
}

}  // namespace

Match Index::match(std::string_view name) const {
  directory::NameCoast coast = directory::NameCoast::none;
  std::string key = directory::exactKey(name, &coast);
  if (key.empty()) return {};
  const std::string suffix = directory::coastSuffix(coast);
  bool inName = false;
  // A feed's own name ("Showtime West") wins over its channel's.
  if (const auto* hits = findKey(byFeed_, key, suffix, &inName)) {
    const Channel& c = data_->channels[hits->front().first];
    // "Showtime West" is the West feed's own name: the coast is the feed's.
    if (hits->size() == 1) return {&c, &c.feeds[hits->front().second], false};
    // "Epix" and "Epix East": one channel, the name's coast picks the feed.
    bool one = std::all_of(hits->begin(), hits->end(), [&](const auto& h) { return h.first == hits->front().first; });
    if (one) return {&c, nullptr, false};
  }
  if (const auto* hits = findKey(byName_, key, suffix, &inName)) {
    if (hits->size() != 1) return {};
    return {&data_->channels[hits->front()], nullptr, inName};
  }
  const auto* loose = findKey(byLoose_, directory::looseKey(name), suffix, &inName);
  if (!loose || loose->size() != 1) return {};
  return {&data_->channels[loose->front()], nullptr, inName};
}

bool Index::gone(std::string_view name) const {
  directory::NameCoast coast = directory::NameCoast::none;
  std::string key = directory::exactKey(name, &coast);
  if (key.empty()) return false;
  bool inName = false;
  return findKey(gone_, key, directory::coastSuffix(coast), &inName) != nullptr;
}

const Channel* Index::goneParent(std::string_view name) const {
  std::string key = directory::exactKey(name);
  if (key.empty()) return nullptr;
  bool inName = false;
  const size_t* parent = findKey(goneParent_, key, directory::coastSuffix(directory::nameCoast(name)), &inName);
  return parent ? &data_->channels[*parent] : nullptr;
}

const std::vector<Index::Carrier>& Index::stationsOf(const std::string& network) const {
  static const std::vector<Carrier> kNone;
  auto it = stationsByNetwork_.find(network);
  return it == stationsByNetwork_.end() ? kNone : it->second;
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
