#include "core/catalog/cache.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "core/util/strings.hpp"

namespace marigold::catalog::cache {
namespace {

void setOpt(json::Value& v, const char* key, const std::optional<std::string>& s) {
  if (s) v.set(key, *s);
}
void setOpt(json::Value& v, const char* key, const std::optional<int>& n) {
  if (n) v.set(key, *n);
}

std::optional<std::string> optString(const json::Value& v, const char* key) {
  if (!v[key].isString()) return std::nullopt;
  return v[key].asString();
}
std::optional<int> optInt(const json::Value& v, const char* key) {
  if (!v[key].isNumber()) return std::nullopt;
  return static_cast<int>(v[key].asInt());
}

json::Value strings(const std::vector<std::string>& list) {
  json::Value a = json::Value::array();
  for (const std::string& s : list) a.push(s);
  return a;
}
std::vector<std::string> stringsFrom(const json::Value& v) {
  std::vector<std::string> out;
  for (const json::Value& item : v.items()) out.push_back(item.asString());
  return out;
}

std::string hexNoPad(uint64_t v) {
  static const char* const kDigits = "0123456789abcdef";
  std::string out;
  do {
    out.insert(out.begin(), kDigits[v & 15]);
    v >>= 4;
  } while (v);
  return out;
}

json::Value toJson(const EntryInfo& e) {
  json::Value v = json::Value::object();
  v.set("id", e.id);
  v.set("name", e.name);
  v.set("members", strings(e.members));
  v.set("category", rawValue(e.category));
  setOpt(v, "callsign", e.callsign);
  setOpt(v, "country", e.country);
  setOpt(v, "virtualNumber", e.virtualNumber);
  v.set("kind", rawValue(e.kind));
  setOpt(v, "market", e.market);
  setOpt(v, "core", e.core);
  v.set("scope", e.scope);
  if (!e.directoryId.empty()) v.set("directoryId", e.directoryId);
  if (!e.closed.empty()) v.set("closed", e.closed);
  if (!e.formerIds.empty()) v.set("formerIds", strings(e.formerIds));
  return v;
}

std::optional<EntryInfo> entryFromJson(const json::Value& v) {
  auto category = categoryFromRaw(v["category"].asString());
  if (!v["id"].isString() || !v["name"].isString() || !category) return std::nullopt;
  EntryInfo e;
  e.id = v["id"].asString();
  e.name = v["name"].asString();
  e.members = stringsFrom(v["members"]);
  e.category = *category;
  e.callsign = optString(v, "callsign");
  e.country = optString(v, "country");
  e.virtualNumber = optInt(v, "virtualNumber");
  e.kind = kindFromRaw(v["kind"].asString()).value_or(ChannelKind::channel);
  e.market = optString(v, "market");
  e.core = optString(v, "core");
  e.scope = v["scope"].isString() ? v["scope"].asString() : "national";
  e.directoryId = v["directoryId"].asString();
  e.closed = v["closed"].asString();
  e.formerIds = stringsFrom(v["formerIds"]);
  return e;
}

json::Value toJson(const EventFamily& f) {
  json::Value v = json::Value::object();
  v.set("id", f.id);
  v.set("name", f.name);
  v.set("feeds", strings(f.feeds));
  v.set("hasNames", f.hasNames);
  v.set("league", f.league);
  setOpt(v, "market", f.market);
  return v;
}

std::optional<EventFamily> familyFromJson(const json::Value& v) {
  if (!v["id"].isString() || !v["name"].isString()) return std::nullopt;
  EventFamily f;
  f.id = v["id"].asString();
  f.name = v["name"].asString();
  f.feeds = stringsFrom(v["feeds"]);
  f.hasNames = v["hasNames"].asBool();
  f.league = v["league"].asString();
  f.market = optString(v, "market");
  return f;
}

json::Value toJson(const LiveEvent& e) {
  json::Value v = json::Value::object();
  v.set("id", e.id);
  v.set("title", e.title);
  v.set("league", e.league);
  if (e.start) v.set("start", *e.start);
  v.set("feeds", strings(e.feeds));
  v.set("family", e.family);
  setOpt(v, "market", e.market);
  if (e.startGuessed) v.set("startGuessed", *e.startGuessed);
  return v;
}

std::optional<LiveEvent> eventFromJson(const json::Value& v) {
  if (!v["id"].isString() || !v["title"].isString()) return std::nullopt;
  LiveEvent e;
  e.id = v["id"].asString();
  e.title = v["title"].asString();
  e.league = v["league"].asString();
  if (v["start"].isNumber()) e.start = v["start"].asInt();
  e.feeds = stringsFrom(v["feeds"]);
  e.family = v["family"].asString();
  e.market = optString(v, "market");
  if (v["startGuessed"].isBool()) e.startGuessed = v["startGuessed"].asBool();
  return e;
}

}  // namespace

std::string fileName(std::string_view signature, Part part) {
  return "catalog-" + hexNoPad(str::fnv1a64(signature)) + "-" +
         (part == Part::first ? "first" : "full") + ".json";
}

json::Value toJson(const Channel& c) {
  json::Value v = json::Value::object();
  v.set("id", c.id);
  v.set("serverID", c.serverId);
  v.set("itemID", c.itemId);
  v.set("name", c.name);
  setOpt(v, "number", c.number);
  if (!c.imageUrl.empty()) v.set("imageURL", c.imageUrl);
  v.set("isFavorite", c.isFavorite);
  v.set("tags", strings(c.tags));
  v.set("isRadio", c.isRadio);
  setOpt(v, "group", c.group);
  v.set("shortName", c.shortName);
  setOpt(v, "epgID", c.epgId);
  return v;
}

std::optional<Channel> channelFromJson(const json::Value& v) {
  if (!v.isObject() || !v["id"].isString() || !v["name"].isString()) return std::nullopt;
  Channel c;
  c.id = v["id"].asString();
  c.serverId = v["serverID"].asString();
  c.itemId = v["itemID"].asString();
  c.name = v["name"].asString();
  c.number = optString(v, "number");
  c.imageUrl = v["imageURL"].asString();
  c.isFavorite = v["isFavorite"].asBool();
  c.tags = stringsFrom(v["tags"]);
  c.isRadio = v["isRadio"].asBool();
  c.group = optString(v, "group");
  c.shortName = v["shortName"].asString();
  c.epgId = optString(v, "epgID");
  return c;
}

json::Value toJson(const ChannelTraits& t) {
  json::Value v = json::Value::object();
  v.set("clusterKey", t.clusterKey);
  v.set("displayName", t.displayName);
  v.set("quality", t.quality);
  v.set("sourceTag", t.sourceTag);
  setOpt(v, "callsign", t.callsign);
  v.set("isWest", t.isWest);
  setOpt(v, "country", t.country);
  v.set("category", rawValue(t.category));
  setOpt(v, "familyKey", t.familyKey);
  setOpt(v, "familyNumber", t.familyNumber);
  setOpt(v, "eventText", t.eventText);
  v.set("hasSeparator", t.hasSeparator);
  v.set("kind", rawValue(t.kind));
  setOpt(v, "market", t.market);
  setOpt(v, "core", t.core);
  v.set("scope", t.scope);
  if (!t.directoryId.empty()) v.set("directoryId", t.directoryId);
  if (!t.closed.empty()) v.set("closed", t.closed);
  if (!t.formerKey.empty()) v.set("formerKey", t.formerKey);
  return v;
}

std::optional<ChannelTraits> traitsFromJson(const json::Value& v) {
  auto category = categoryFromRaw(v["category"].asString());
  if (!v.isObject() || !v["clusterKey"].isString() || !category) return std::nullopt;
  ChannelTraits t;
  t.clusterKey = v["clusterKey"].asString();
  t.displayName = v["displayName"].asString();
  t.quality = static_cast<int>(v["quality"].asInt(1));
  t.sourceTag = v["sourceTag"].asString();
  t.callsign = optString(v, "callsign");
  t.isWest = v["isWest"].asBool();
  t.country = optString(v, "country");
  t.category = *category;
  t.familyKey = optString(v, "familyKey");
  t.familyNumber = optInt(v, "familyNumber");
  t.eventText = optString(v, "eventText");
  t.hasSeparator = v["hasSeparator"].asBool();
  t.kind = kindFromRaw(v["kind"].asString()).value_or(ChannelKind::channel);
  t.market = optString(v, "market");
  t.core = optString(v, "core");
  t.scope = v["scope"].isString() ? v["scope"].asString() : "national";
  t.directoryId = v["directoryId"].asString();
  t.closed = v["closed"].asString();
  t.formerKey = v["formerKey"].asString();
  return t;
}

json::Value toJson(const CatalogResult& r) {
  json::Value v = json::Value::object();
  json::Value traits = json::Value::object();
  for (const auto& [id, t] : r.traits) traits.set(id, toJson(t));
  v.set("traits", traits);
  json::Value entries = json::Value::array();
  for (const EntryInfo& e : r.entries) entries.push(toJson(e));
  v.set("entries", entries);
  json::Value entryOf = json::Value::object();
  for (const auto& [id, e] : r.entryOf) entryOf.set(id, e);
  v.set("entryOf", entryOf);
  json::Value families = json::Value::array();
  for (const EventFamily& f : r.families) families.push(toJson(f));
  v.set("families", families);
  json::Value events = json::Value::array();
  for (const LiveEvent& e : r.events) events.push(toJson(e));
  v.set("events", events);
  return v;
}

std::optional<CatalogResult> catalogFromJson(const json::Value& v) {
  if (!v.isObject()) return std::nullopt;
  CatalogResult r;
  for (const auto& [id, t] : v["traits"].fields()) {
    auto traits = traitsFromJson(t);
    if (!traits) return std::nullopt;
    r.traits.emplace(id, std::move(*traits));
  }
  for (const json::Value& item : v["entries"].items()) {
    auto e = entryFromJson(item);
    if (!e) return std::nullopt;
    r.entries.push_back(std::move(*e));
  }
  for (const auto& [id, e] : v["entryOf"].fields()) r.entryOf.emplace(id, e.asString());
  for (const json::Value& item : v["families"].items()) {
    auto f = familyFromJson(item);
    if (!f) return std::nullopt;
    r.families.push_back(std::move(*f));
  }
  for (const json::Value& item : v["events"].items()) {
    auto e = eventFromJson(item);
    if (!e) return std::nullopt;
    r.events.push_back(std::move(*e));
  }
  return r;
}

SavedRef ref(const Saved& saved) {
  SavedRef r;
  r.version = saved.version;
  r.signature = saved.signature;
  r.savedAt = saved.savedAt;
  r.channels = &saved.channels;
  r.catalog = &saved.catalog;
  r.xtreamFormats = &saved.xtreamFormats;
  r.fingerprint = saved.fingerprint;
  return r;
}

namespace {

// An unordered map's entries in key order (json::Value objects are sorted).
template <typename Map>
std::vector<const typename Map::value_type*> sortedEntries(const Map& map) {
  std::vector<const typename Map::value_type*> out;
  out.reserve(map.size());
  for (const auto& item : map) out.push_back(&item);
  std::sort(out.begin(), out.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
  return out;
}

}  // namespace

bool writeSaved(const SavedRef& saved, const json::Writer::Sink& sink) {
  static const std::vector<Channel> kNoChannels;
  static const CatalogResult kNoCatalog;
  static const std::map<std::string, std::vector<std::string>> kNoFormats;
  const std::vector<Channel>& channels = saved.channels ? *saved.channels : kNoChannels;
  const CatalogResult& catalog = saved.catalog ? *saved.catalog : kNoCatalog;
  const auto& formats = saved.xtreamFormats ? *saved.xtreamFormats : kNoFormats;
  // Keys in sorted order: the bytes json::write() gave for the whole tree.
  json::Writer w(sink);
  w.beginObject();
  w.key("catalog");
  w.beginObject();
  w.key("entries");
  w.beginArray();
  for (const EntryInfo& e : catalog.entries) w.value(toJson(e));
  w.endArray();
  w.key("entryOf");
  w.beginObject();
  for (const auto* item : sortedEntries(catalog.entryOf)) {
    w.key(item->first);
    w.value(item->second);
  }
  w.endObject();
  w.key("events");
  w.beginArray();
  for (const LiveEvent& e : catalog.events) w.value(toJson(e));
  w.endArray();
  w.key("families");
  w.beginArray();
  for (const EventFamily& f : catalog.families) w.value(toJson(f));
  w.endArray();
  w.key("traits");
  w.beginObject();
  for (const auto* item : sortedEntries(catalog.traits)) {
    w.key(item->first);
    w.value(toJson(item->second));
  }
  w.endObject();
  w.endObject();
  w.key("channels");
  w.beginArray();
  for (const Channel& c : channels) w.value(toJson(c));
  w.endArray();
  if (saved.fingerprint) {
    // As hex text: a JSON number holds only 53 bits.
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(saved.fingerprint));
    w.key("fingerprint");
    w.value(std::string(hex));
  }
  w.key("savedAt");
  w.value(saved.savedAt);
  w.key("signature");
  w.value(saved.signature);
  w.key("version");
  w.value(saved.version);
  w.key("xtreamFormats");
  w.beginObject();
  for (const auto& [k, list] : formats) {
    w.key(k);
    w.value(strings(list));
  }
  w.endObject();
  w.endObject();
  return w.finish();
}

std::string toJson(const Saved& saved) {
  std::string out;
  writeSaved(ref(saved), [&out](std::string_view chunk) {
    out.append(chunk.data(), chunk.size());
    return true;
  });
  return out;
}

namespace {

template <typename Feed>
std::optional<Saved> readSavedWith(const Feed& feed, std::string_view signature) {
  Saved s;
  bool bad = false;
  int version = -1;
  std::string savedSignature;
  json::DocumentReader reader(
      {"channels", "catalog.entries", "catalog.entryOf", "catalog.events", "catalog.families", "catalog.traits"},
      [&](const std::string& key, json::Value&& v) {
        if (key == "version") version = static_cast<int>(v.asInt(-1));
        else if (key == "signature") savedSignature = v.asString();
        else if (key == "savedAt") s.savedAt = v.asInt();
        else if (key == "fingerprint") s.fingerprint = std::strtoull(v.asString().c_str(), nullptr, 16);
        else if (key == "xtreamFormats")
          for (const auto& [k, list] : v.fields()) s.xtreamFormats[k] = stringsFrom(list);
        return true;
      },
      [&](const std::string& key, const std::string& member, json::Value&& v) {
        if (key == "channels") {
          auto c = channelFromJson(v);
          if (!c) bad = true;
          else s.channels.push_back(std::move(*c));
          return !bad;
        }
        CatalogResult& r = s.catalog;
        if (key == "catalog.traits") {
          auto t = traitsFromJson(v);
          if (!t) bad = true;
          else r.traits.emplace(member, std::move(*t));
        } else if (key == "catalog.entries") {
          auto e = entryFromJson(v);
          if (!e) bad = true;
          else r.entries.push_back(std::move(*e));
        } else if (key == "catalog.entryOf") {
          r.entryOf.emplace(member, v.asString());
        } else if (key == "catalog.families") {
          auto f = familyFromJson(v);
          if (!f) bad = true;
          else r.families.push_back(std::move(*f));
        } else if (key == "catalog.events") {
          auto e = eventFromJson(v);
          if (!e) bad = true;
          else r.events.push_back(std::move(*e));
        }
        return !bad;
      });
  if (!feed(reader) || !reader.finish() || bad) return std::nullopt;
  if (version != kVersion || savedSignature != signature) return std::nullopt;
  s.version = kVersion;
  s.signature = std::string(signature);
  return s;
}

}  // namespace

std::optional<Saved> fromJson(std::string_view text, std::string_view signature) {
  return readSavedWith([text](json::DocumentReader& r) { return r.feed(text); }, signature);
}

std::optional<Saved> readSaved(std::istream& in, std::string_view signature) {
  return readSavedWith([&in](json::DocumentReader& r) { return json::feedAll(in, r); }, signature);
}

Saved firstPart(const Saved& saved, const std::set<std::string>& firstEntryIDs,
                const std::set<std::string>& firstFamilyIDs) {
  return firstPart(ref(saved), firstEntryIDs, firstFamilyIDs);
}

Saved firstPart(const SavedRef& ref, const std::set<std::string>& firstEntryIDs,
                const std::set<std::string>& firstFamilyIDs) {
  static const std::vector<Channel> kNoChannels;
  static const CatalogResult kNoCatalog;
  struct {
    const std::vector<Channel>& channels;
    const CatalogResult& catalog;
  } saved{ref.channels ? *ref.channels : kNoChannels, ref.catalog ? *ref.catalog : kNoCatalog};
  Saved first;
  first.version = ref.version;
  first.signature = ref.signature;
  first.savedAt = ref.savedAt;
  if (ref.xtreamFormats) first.xtreamFormats = *ref.xtreamFormats;
  std::set<std::string> keep;
  for (const EntryInfo& e : saved.catalog.entries) {
    if (!firstEntryIDs.count(e.id)) continue;
    first.catalog.entries.push_back(e);
    keep.insert(e.members.begin(), e.members.end());
  }
  std::set<std::string> familyKeys;
  for (const EventFamily& f : saved.catalog.families) {
    if (!firstFamilyIDs.count(f.id)) continue;
    first.catalog.families.push_back(f);
    keep.insert(f.feeds.begin(), f.feeds.end());
    familyKeys.insert(f.id.substr(std::min<size_t>(4, f.id.size())));  // drops "fam|"
  }
  for (const Channel& c : saved.channels) {
    if (keep.count(c.id)) first.channels.push_back(c);
  }
  for (const auto& [id, t] : saved.catalog.traits) {
    if (keep.count(id)) first.catalog.traits.emplace(id, t);
  }
  for (const auto& [id, e] : saved.catalog.entryOf) {
    if (keep.count(id)) first.catalog.entryOf.emplace(id, e);
  }
  for (const LiveEvent& e : saved.catalog.events) {
    if (familyKeys.count(e.family)) first.catalog.events.push_back(e);
  }
  return first;
}

}  // namespace marigold::catalog::cache
