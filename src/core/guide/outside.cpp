#include "core/guide/outside.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <unordered_set>

#include "core/catalog/catalog.hpp"
#include "core/guide/genre.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::guide {
namespace {

constexpr int64_t kNoEnd = INT64_MIN / 4;  // Swift's Int.min for "no listings"

int64_t lastEnd(const std::vector<RawProgram>* list) {
  if (!list || list->empty()) return kNoEnd;
  int64_t best = kNoEnd;
  for (const RawProgram& p : *list) best = std::max<int64_t>(best, p.end());
  return best;
}

// String(v, radix: 16): lower-case, no padding.
std::string hexNoPad(uint64_t v) {
  static const char* const kDigits = "0123456789abcdef";
  std::string out;
  do {
    out.insert(out.begin(), kDigits[v & 15]);
    v >>= 4;
  } while (v);
  return out;
}

std::string twoDigits(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }

// The first `n` code points.
std::string prefixCodePoints(std::string_view s, size_t n) {
  size_t i = 0, count = 0;
  while (i < s.size() && count < n) {
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    ++count;
  }
  return std::string(s.substr(0, i));
}

// DateFormatter "EEE MMM d, h:mm a" (en_US).
std::string formatDay(time::Seconds t, const time::Zone& zone) {
  static const char* const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  time::Civil c = zone.toLocal(t);
  int h12 = c.hour % 12 == 0 ? 12 : c.hour % 12;
  return std::string(kDays[time::weekday(c.year, c.month, c.day)]) + " " +
         kMonths[(c.month + 11) % 12] + " " + std::to_string(c.day) + ", " + std::to_string(h12) +
         ":" + twoDigits(c.minute) + (c.hour < 12 ? " AM" : " PM");
}

void setOpt(json::Value& v, const char* key, const std::optional<std::string>& s) {
  if (s) v.set(key, *s);
}
void setOpt(json::Value& v, const char* key, const std::optional<int>& n) {
  if (n) v.set(key, *n);
}
void setOpt(json::Value& v, const char* key, const std::optional<int64_t>& n) {
  if (n) v.set(key, *n);
}
void setOpt(json::Value& v, const char* key, const std::optional<double>& n) {
  if (n) v.set(key, *n);
}
void setOpt(json::Value& v, const char* key, const std::optional<bool>& b) {
  if (b) v.set(key, *b);
}

std::optional<std::string> optString(const json::Value& v, const char* key) {
  if (!v[key].isString()) return std::nullopt;
  return v[key].asString();
}
std::optional<int> optInt(const json::Value& v, const char* key) {
  if (!v[key].isNumber()) return std::nullopt;
  return static_cast<int>(v[key].asInt());
}
std::optional<int64_t> optInt64(const json::Value& v, const char* key) {
  if (!v[key].isNumber()) return std::nullopt;
  return v[key].asInt();
}
std::optional<double> optDouble(const json::Value& v, const char* key) {
  if (!v[key].isNumber()) return std::nullopt;
  return v[key].asNumber();
}
std::optional<bool> optBool(const json::Value& v, const char* key) {
  if (!v[key].isBool()) return std::nullopt;
  return v[key].asBool();
}

json::Value stringMap(const std::map<std::string, std::string>& m) {
  json::Value o = json::Value::object();
  for (const auto& [k, v] : m) o.set(k, v);
  return o;
}

}  // namespace

// MARK: Sources

std::string Source::id() const {
  std::string lowered = str::lower(name);
  std::string out;
  for (char c : lowered) {
    bool keep =
        (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (static_cast<unsigned char>(c) >= 0x80);
    out += keep ? c : '-';
  }
  return out;
}

bool Source::appliesTo(const std::set<std::string>& on) const {
  if (on.count(shard)) return true;
  for (const std::string& s : alsoShards) {
    if (on.count(s)) return true;
  }
  return false;
}

const std::vector<Shard>& shards() {
  static const std::vector<Shard> kShards = {
      {"us-locals", "US local stations",
       "ABC, CBS, NBC, FOX, PBS, CW and other locals, matched by call sign or by network and city",
       true},
      {"us", "US national & sports", "Cable networks, premium and sports channels", true},
      {"fast", "Free streaming channels", "Pluto TV, Samsung TV Plus, Plex and Roku channels",
       true},
      {"ca", "Canada", "Canadian networks and locals", false},
      {"uk", "United Kingdom", "UK channels", false}};
  return kShards;
}

const std::vector<Source>& sources() {
  static const std::vector<Source> kSources = [] {
    auto make = [](const char* name, const char* shard, const char* url) {
      Source s;
      s.name = name;
      s.shard = shard;
      s.url = url;
      return s;
    };
    std::vector<Source> list;
    Source locals = make("US locals", "us-locals",
                         "https://epgshare01.online/epgshare01/epg_ripper_US_LOCALS1.xml.gz");
    locals.locals = true;
    list.push_back(locals);
    list.push_back(
        make("US national", "us", "https://epgshare01.online/epgshare01/epg_ripper_US2.xml.gz"));
    list.push_back(make("US sports", "us",
                        "https://epgshare01.online/epgshare01/epg_ripper_US_SPORTS1.xml.gz"));
    list.push_back(make("Pluto TV", "fast", "https://i.mjh.nz/PlutoTV/us.xml.gz"));
    list.push_back(make("Samsung TV Plus", "fast", "https://i.mjh.nz/SamsungTVPlus/us.xml.gz"));
    list.push_back(make("Plex", "fast", "https://i.mjh.nz/Plex/us.xml.gz"));
    list.push_back(make("Roku", "fast", "https://i.mjh.nz/Roku/all.xml.gz"));
    list.push_back(
        make("Canada", "ca", "https://epgshare01.online/epgshare01/epg_ripper_CA2.xml.gz"));
    list.push_back(make("UK", "uk", "https://epgshare01.online/epgshare01/epg_ripper_UK1.xml.gz"));
    // Backups, used only while a main source is stale or short. EPGTalk
    // first (right times, several days ahead), then epg.pw (nearly every US
    // channel, one day ahead, times written 8 h late).
    Source b1 = make("Backup: EPGTalk US", "us",
                     "https://raw.githubusercontent.com/acidjesuz/EPGTalk/master/US_lite.xml.gz");
    b1.backup = true;
    list.push_back(b1);
    Source b2 =
        make("Backup: EPGTalk sports", "us",
             "https://raw.githubusercontent.com/acidjesuz/EPGTalk/master/Sports_guide.xml.gz");
    b2.backup = true;
    list.push_back(b2);
    Source b3 =
        make("Backup: EPGTalk locals", "us-locals",
             "https://raw.githubusercontent.com/acidjesuz/EPGTalk/master/US_local_guide.xml.gz");
    b3.backup = true;
    b3.stationNames = true;
    list.push_back(b3);
    Source b4 = make("Backup: epg.pw US", "us-locals", "https://epg.pw/xmltv/epg_US.xml.gz");
    b4.backup = true;
    b4.alsoShards = {"us"};
    b4.assumedShift = 480;
    b4.calibrate = true;
    list.push_back(b4);
    return list;
  }();
  return kSources;
}

std::set<std::string> defaultShards() {
  std::set<std::string> out;
  for (const Shard& s : shards()) {
    if (s.defaultOn) out.insert(s.id);
  }
  return out;
}

bool needsStationDirectory(const std::set<std::string>& on) {
  return on.count("us-locals") > 0 || on.count("ca") > 0;
}

// MARK: Data

std::string SourceState::line(const time::Zone& zone) const {
  std::vector<std::string> parts{name};
  if (lastModified) parts.push_back("file from " + formatDay(*lastModified, zone));
  if (horizonMedian) {
    parts.push_back("listings to " +
                    formatDay(static_cast<time::Seconds>(*horizonMedian) * 60, zone));
  }
  if (matched) parts.push_back(std::to_string(*matched) + " channels");
  if (shift && *shift != 0)
    parts.push_back("times corrected " + std::to_string(*shift / 60) + " h");
  if (backup) {
    if (used == true) {
      parts.push_back("in use" + (extended ? ", fills " + std::to_string(*extended) + " channels"
                                           : std::string()));
    } else {
      parts.push_back("standing by");
    }
  } else if (stale == true) {
    parts.push_back("out of date");
  }
  if (problem) parts.push_back("problem: " + *problem);
  return str::join(parts, " · ");
}

bool SourceState::operator==(const SourceState& o) const {
  return id == o.id && name == o.name && backup == o.backup && etag == o.etag &&
         lastModified == o.lastModified && checkedAt == o.checkedAt &&
         downloadedAt == o.downloadedAt && status == o.status && bytes == o.bytes &&
         problem == o.problem && guideChannels == o.guideChannels && matched == o.matched &&
         horizonMedian == o.horizonMedian && parseSeconds == o.parseSeconds && shift == o.shift &&
         shiftNote == o.shiftNote && stale == o.stale && used == o.used && extended == o.extended;
}

json::Value toJson(const RawProgram& p) {
  json::Value v = json::Value::object();
  v.set("s", p.s);
  v.set("m", p.m);
  v.set("t", p.t);
  setOpt(v, "st", p.st);
  setOpt(v, "d", p.d);
  v.set("f", p.f);
  return v;
}

std::optional<RawProgram> rawProgramFromJson(const json::Value& v) {
  if (!v.isObject() || !v["s"].isNumber() || !v["m"].isNumber() || !v["t"].isString()) {
    return std::nullopt;
  }
  RawProgram p;
  p.s = static_cast<int>(v["s"].asInt());
  p.m = static_cast<int>(v["m"].asInt());
  p.t = v["t"].asString();
  p.st = optString(v, "st");
  p.d = optString(v, "d");
  p.f = static_cast<int>(v["f"].asInt());
  return p;
}

json::Value toJson(const ProgramsById& programs) {
  json::Value o = json::Value::object();
  for (const auto& [id, list] : programs) {
    json::Value a = json::Value::array();
    for (const RawProgram& p : list) a.push(toJson(p));
    o.set(id, a);
  }
  return o;
}

ProgramsById programsFromJson(const json::Value& v) {
  ProgramsById out;
  for (const auto& [id, list] : v.fields()) {
    std::vector<RawProgram> programs;
    for (const json::Value& item : list.items()) {
      if (auto p = rawProgramFromJson(item)) programs.push_back(std::move(*p));
    }
    out[id] = std::move(programs);
  }
  return out;
}

json::Value toJson(const SourceState& s) {
  json::Value v = json::Value::object();
  v.set("id", s.id);
  v.set("name", s.name);
  v.set("backup", s.backup);
  setOpt(v, "etag", s.etag);
  setOpt(v, "lastModified", s.lastModified);
  setOpt(v, "checkedAt", s.checkedAt);
  setOpt(v, "downloadedAt", s.downloadedAt);
  setOpt(v, "status", s.status);
  setOpt(v, "bytes", s.bytes);
  setOpt(v, "problem", s.problem);
  setOpt(v, "guideChannels", s.guideChannels);
  setOpt(v, "matched", s.matched);
  setOpt(v, "horizonMedian", s.horizonMedian);
  setOpt(v, "parseSeconds", s.parseSeconds);
  setOpt(v, "shift", s.shift);
  setOpt(v, "shiftNote", s.shiftNote);
  setOpt(v, "stale", s.stale);
  setOpt(v, "used", s.used);
  setOpt(v, "extended", s.extended);
  return v;
}

std::optional<SourceState> sourceStateFromJson(const json::Value& v) {
  if (!v.isObject() || !v["id"].isString() || !v["name"].isString()) return std::nullopt;
  SourceState s;
  s.id = v["id"].asString();
  s.name = v["name"].asString();
  s.backup = v["backup"].asBool();
  s.etag = optString(v, "etag");
  s.lastModified = optInt64(v, "lastModified");
  s.checkedAt = optInt64(v, "checkedAt");
  s.downloadedAt = optInt64(v, "downloadedAt");
  s.status = optInt(v, "status");
  s.bytes = optInt64(v, "bytes");
  s.problem = optString(v, "problem");
  s.guideChannels = optInt(v, "guideChannels");
  s.matched = optInt(v, "matched");
  s.horizonMedian = optInt(v, "horizonMedian");
  s.parseSeconds = optDouble(v, "parseSeconds");
  s.shift = optInt(v, "shift");
  s.shiftNote = optString(v, "shiftNote");
  s.stale = optBool(v, "stale");
  s.used = optBool(v, "used");
  s.extended = optInt(v, "extended");
  return s;
}

std::string toJson(const Saved& saved) {
  json::Value v = json::Value::object();
  v.set("built", saved.built);
  json::Value shardList = json::Value::array();
  for (const std::string& s : saved.shards) shardList.push(s);
  v.set("shards", shardList);
  v.set("channelCount", saved.channelCount);
  v.set("programs", toJson(saved.programs));
  if (saved.notes) v.set("notes", stringMap(*saved.notes));
  if (saved.sources) {
    json::Value o = json::Value::object();
    for (const auto& [id, state] : *saved.sources) o.set(id, toJson(state));
    v.set("sources", o);
  }
  if (saved.nextCheck) v.set("nextCheck", *saved.nextCheck);
  setOpt(v, "targetsKey", saved.targetsKey);
  setOpt(v, "detailVersion", saved.detailVersion);
  return json::write(v);
}

std::optional<Saved> savedFromJson(std::string_view text) {
  auto parsed = json::parse(text);
  if (!parsed || !parsed->isObject()) return std::nullopt;
  const json::Value& v = *parsed;
  if (!v["built"].isNumber() || !v["shards"].isArray() || !v["programs"].isObject()) {
    return std::nullopt;
  }
  Saved s;
  s.built = v["built"].asInt();
  for (const json::Value& item : v["shards"].items()) s.shards.push_back(item.asString());
  s.channelCount = static_cast<int>(v["channelCount"].asInt());
  s.programs = programsFromJson(v["programs"]);
  if (v["notes"].isObject()) {
    std::map<std::string, std::string> notes;
    for (const auto& [k, n] : v["notes"].fields()) notes[k] = n.asString();
    s.notes = std::move(notes);
  }
  if (v["sources"].isObject()) {
    std::map<std::string, SourceState> states;
    for (const auto& [k, item] : v["sources"].fields()) {
      if (auto state = sourceStateFromJson(item)) states[k] = std::move(*state);
    }
    s.sources = std::move(states);
  }
  s.nextCheck = optInt64(v, "nextCheck");
  s.targetsKey = optString(v, "targetsKey");
  s.detailVersion = optInt(v, "detailVersion");
  return s;
}

std::string savedFileName(std::string_view signature) {
  // CharacterSet.alphanumerics, per code point; ASCII is what signatures hold.
  std::string safe;
  size_t count = 0;
  for (size_t i = 0; i < signature.size() && count < 80; ++count) {
    unsigned char c = static_cast<unsigned char>(signature[i]);
    size_t len = c < 0x80             ? 1
                 : (c & 0xE0) == 0xC0 ? 2
                 : (c & 0xF0) == 0xE0 ? 3
                 : (c & 0xF8) == 0xF0 ? 4
                                      : 1;
    bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    safe += alnum ? static_cast<char>(c) : '_';
    i += len;
  }
  return "listings-" + safe + ".json";
}

bool needsRebuild(const Saved* saved, const std::set<std::string>& on, int channelCount,
                  time::Seconds now) {
  if (!saved) return true;
  if (now - saved->built > kRefreshAge) return true;
  if (std::set<std::string>(saved->shards.begin(), saved->shards.end()) != on) return true;
  if (saved->detailVersion.value_or(1) < kDetailVersion) return true;
  if (saved->nextCheck && now >= *saved->nextCheck) return true;
  // A much bigger lineup than last time: new channels need matching.
  return channelCount > saved->channelCount + std::max(25, saved->channelCount / 10);
}

// MARK: Matching

namespace {

// A network's core name, with "~west" for a West / Pacific feed.
std::optional<std::string> coreKeyOf(const std::string& name) {
  std::string clean = catalog::cleanName(name);
  auto hit = catalog::coreMatch(clean);
  if (!hit) return std::nullopt;
  return catalog::coreKey(clean).west ? hit->name + "~west" : hit->name;
}

// normalize() without the words a guide adds or leaves out ("The Travel
// Channel", "Smithsonian HD Network").
std::string looseKey(const std::string& name) {
  std::string s = rx::replace(str::lower(name), "\\b(the|channel|network|television)\\b", " ");
  return normalize(s);
}

}  // namespace

Targets::Targets(const std::vector<Channel>& channels, const std::vector<Station>& stations) {
  stationsByNetwork = groupByNetwork(stations);
  for (const Channel& channel : channels) {
    bool isLocal = str::lower(channel.group.value_or("")).find("local") != std::string::npos;
    std::vector<std::string> signs = callsigns(channel.name, isLocal);
    // A call sign written alone in capitals ("(US) (Prime) KAGS").
    if (signs.empty()) {
      if (auto sign = bareCallsign(channel.name)) signs = {*sign};
    }
    if (signs.empty()) {
      if (auto sign = stationCallsign(channel.name, stationsByNetwork)) signs = {*sign};
    }
    // The provider's XMLTV id ("KGO.us", "ESPN.us") is another key.
    if (channel.epgId) {
      std::vector<std::string> fromEpg;
      for (std::string& s : callsigns(*channel.epgId, true)) {
        if (std::find(signs.begin(), signs.end(), s) == signs.end())
          fromEpg.push_back(std::move(s));
      }
      signs.insert(signs.end(), fromEpg.begin(), fromEpg.end());
    }
    for (const std::string& sign : signs) bySign[sign].push_back(channel.id);
    std::set<std::string> texts{channel.displayName(), channel.name};
    if (channel.epgId) texts.insert(rx::replace(*channel.epgId, "\\.[a-z]{2,3}$", "", true));
    for (const std::string& t : texts) {
      std::string key = normalize(t);
      if (key.size() >= 2) byName[key].push_back(channel.id);
    }
    if (auto core = coreKeyOf(channel.name)) byCore[*core].push_back(channel.id);
    std::string loose = looseKey(channel.displayName());
    if (loose.size() >= 2) byLoose[loose].push_back(channel.id);
  }
}

std::vector<Match> Targets::matches(const std::vector<std::string>& names, std::string_view xmltvID,
                                    bool locals, bool stationNames, bool deriveWest) const {
  std::vector<Match> found;
  std::vector<std::string> all = names;
  all.emplace_back(xmltvID);
  for (const std::string& sign : callsigns(str::join(all, " | "), locals)) {
    auto it = bySign.find(sign);
    if (it == bySign.end()) continue;
    for (const std::string& id : it->second) found.push_back({id, "by call sign " + sign, 0});
  }
  if (!found.empty()) return found;
  if (stationNames && !stationsByNetwork.empty()) {
    for (const std::string& name : names) {
      auto sign = stationCallsign(name, stationsByNetwork, true);
      if (!sign) continue;
      auto it = bySign.find(*sign);
      if (it == bySign.end()) continue;
      for (const std::string& id : it->second) found.push_back({id, "by station " + *sign, 0});
      return found;
    }
  }
  for (const std::string& name : names) {
    std::string key = normalize(name);
    if (key.size() < 2) continue;
    if (auto it = byName.find(key); it != byName.end()) {
      for (const std::string& id : it->second) found.push_back({id, "by name", 0});
    }
    if (deriveWest && !str::endsWith(key, "~west")) {
      if (auto it = byName.find(key + "~west"); it != byName.end()) {
        for (const std::string& id : it->second) {
          found.push_back({id, "by name, West feed 3 h later", 180});
        }
      }
    }
  }
  if (!found.empty()) return found;
  // A national network under another of its names.
  for (const std::string& name : names) {
    auto core = coreKeyOf(name);
    if (!core) continue;
    if (auto it = byCore.find(*core); it != byCore.end())
      for (const std::string& id : it->second) found.push_back({id, "by network " + *core, 0});
    if (deriveWest && !str::endsWith(*core, "~west")) {
      if (auto it = byCore.find(*core + "~west"); it != byCore.end())
        for (const std::string& id : it->second) found.push_back({id, "by network, West feed 3 h later", 180});
    }
    if (!found.empty()) return found;
  }
  // Last: the looser name ("UPtv" also tries "up").
  for (const std::string& name : names) {
    std::string key = looseKey(name);
    if (key.size() < 2) continue;
    std::vector<std::string> keys = {key};
    if (key.size() >= 4 && str::endsWith(key, "tv")) keys.push_back(key.substr(0, key.size() - 2));
    for (const std::string& k : keys) {
      if (auto it = byLoose.find(k); it != byLoose.end()) {
        for (const std::string& id : it->second) found.push_back({id, "by name", 0});
        return found;
      }
    }
  }
  return found;
}

// MARK: Merging

LearnedShift learnShift(const ProgramsById& reference, const ProgramsById& candidate, int sample) {
  auto key = [](const std::string& title) {
    std::string lowered = rx::replace(str::lower(title), "^(live|new)[: ]+", "");
    return prefixCodePoints(str::join(letterNumberTokens(lowered), ""), 12);
  };
  std::map<int, int> votes;
  std::map<std::string, int> perChannel;
  int checked = 0;
  // Swift walks a dictionary (hash order); here channels go in id order.
  for (const auto& [channel, programs] : candidate) {
    auto refIt = reference.find(channel);
    if (refIt == reference.end() || refIt->second.empty()) continue;
    checked += 1;
    if (checked > sample) break;
    std::unordered_map<std::string, int> refCount, refStart, candCount;
    for (const RawProgram& p : refIt->second) {
      std::string k = key(p.t);
      refCount[k] += 1;
      refStart[k] = p.s;
    }
    std::vector<std::string> keys;
    keys.reserve(programs.size());
    for (const RawProgram& p : programs) {
      keys.push_back(key(p.t));
      candCount[keys.back()] += 1;
    }
    std::map<int, int> local;
    for (size_t i = 0; i < programs.size(); ++i) {
      const std::string& k = keys[i];
      if (str::utf8Length(k) < 4 || candCount[k] != 1 || refCount[k] != 1) continue;
      auto start = refStart.find(k);
      if (start == refStart.end()) continue;
      int gap = programs[i].s - start->second;
      int hours = static_cast<int>(std::lround(static_cast<double>(gap) / 60.0));
      if (std::abs(gap - hours * 60) <= 10 && std::abs(hours) <= 14) local[hours] += 1;
    }
    if (local.empty()) continue;
    std::vector<std::pair<int, int>> ranked(local.begin(), local.end());
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
    const auto& top = ranked.front();
    int second = ranked.size() > 1 ? ranked[1].second : 0;
    if (top.second >= 2 && top.second > second) votes[top.first] += 1;
    if (top.second >= 3 && top.second > 2 * second) perChannel[channel] = top.first * 60;
  }
  std::vector<std::pair<int, int>> ranked(votes.begin(), votes.end());
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  std::vector<std::string> summaryParts;
  for (size_t i = 0; i < ranked.size() && i < 3; ++i) {
    summaryParts.push_back(std::to_string(ranked[i].first) +
                           " h: " + std::to_string(ranked[i].second));
  }
  std::string summary = str::join(summaryParts, ", ");
  LearnedShift out;
  if (ranked.empty()) {
    out.note = "no overlap to check against (" + std::to_string(checked) + " channels)";
    return out;
  }
  const auto& top = ranked.front();
  int second = ranked.size() > 1 ? ranked[1].second : 0;
  if (!(top.second >= 20 && top.second >= 3 * std::max(1, second))) {
    out.note = "unclear (" + summary + ")";
    return out;
  }
  // Channels that disagree with the overall shift only keep theirs when clear.
  out.shift = top.first * 60;
  for (const auto& [channel, shift] : perChannel) {
    if (shift != *out.shift) out.perChannel[channel] = shift;
  }
  out.note = "learned " + std::to_string(top.first) + " h from " + std::to_string(top.second) +
             " channels (" + summary + ")";
  return out;
}

std::vector<RawProgram> mergePrograms(const std::vector<RawProgram>& base,
                                      const std::vector<RawProgram>& extra) {
  auto byStart = [](const RawProgram& a, const RawProgram& b) { return a.s < b.s; };
  if (base.empty()) {
    std::vector<RawProgram> out = extra;
    std::stable_sort(out.begin(), out.end(), byStart);
    return out;
  }
  std::vector<RawProgram> result = base;
  for (const RawProgram& program : extra) {
    int end = program.end();
    bool clashes = std::any_of(result.begin(), result.end(), [&](const RawProgram& other) {
      return std::min(end, other.end()) - std::max(program.s, other.s) > 5;
    });
    if (!clashes) result.push_back(program);
  }
  std::stable_sort(result.begin(), result.end(), byStart);
  return result;
}

std::vector<Program> programs(const std::vector<RawProgram>& raw, const std::string& channelID,
                              time::Seconds from, time::Seconds to, std::string_view idPrefix,
                              bool outside) {
  int64_t low = from / 60;
  int64_t high = to / 60;
  std::vector<Program> out;
  for (const RawProgram& item : raw) {
    int64_t end = item.end();
    if (!(end > low && item.s < high)) continue;
    Program p;
    p.id = std::string(idPrefix) + "|" + channelID + "|" + std::to_string(item.s);
    p.channelId = channelID;
    p.title = item.t;
    p.episodeTitle = item.st;
    p.overview = item.d;
    p.start = static_cast<time::Seconds>(item.s) * 60;
    p.end = end * 60;
    p.genres = static_cast<uint8_t>(item.f & genre::kGenreBits);
    p.isLive = (item.f & 16) != 0;
    p.isPremiere = (item.f & 32) != 0;
    p.fromOutsideGuide = outside;
    // Listings saved before titles were read for genres.
    out.push_back((item.f & genre::kGenreBits) == 0 ? genre::withTitleFlags(p) : std::move(p));
  }
  return out;
}

// MARK: Building

std::string targetsKey(const std::vector<Channel>& channels, const std::set<std::string>& on) {
  // A stable hash (Swift's Hasher is seeded per launch, so it can't be saved).
  std::vector<const std::string*> ids;
  ids.reserve(channels.size());
  for (const Channel& c : channels) ids.push_back(&c.id);
  std::sort(ids.begin(), ids.end(),
            [](const std::string* a, const std::string* b) { return *a < *b; });
  uint64_t hash = 0xcbf29ce484222325ull;
  for (const std::string* id : ids) {
    for (char c : *id) hash = (hash ^ static_cast<unsigned char>(c)) * 0x100000001b3ull;
    hash = (hash ^ 0x0a) * 0x100000001b3ull;
  }
  std::vector<std::string> shardList(on.begin(), on.end());
  return std::to_string(channels.size()) + "|" + hexNoPad(hash) + "|" + str::join(shardList, ",");
}

BuildResult build(const std::vector<Channel>& channels, const std::set<std::string>& on,
                  const Saved* previous, const std::vector<Station>& stations, time::Seconds nowSec,
                  const BuildIO& io) {
  auto progress = [&](const std::string& line) {
    if (io.progress) io.progress(line);
  };
  auto cancelled = [&] { return io.cancelled && io.cancelled(); };
  std::vector<std::string> log;
  progress("Getting the station directory\xE2\x80\xA6");
  auto targets = std::make_shared<const Targets>(
      channels, needsStationDirectory(on) ? stations : std::vector<Station>());
  std::string key = targetsKey(channels, on);

  ProgramsById merged;
  std::map<std::string, std::string> notes;
  std::vector<std::string> problems;
  std::map<std::string, SourceState> states;
  if (previous && previous->sources) states = *previous->sources;
  const int now = static_cast<int>(nowSec / 60);
  const Window window{now - 180, now + kKeepHours * 60};
  std::vector<const Source*> chosen, mains, backups;
  for (const Source& s : sources()) {
    if (!s.appliesTo(on)) continue;
    chosen.push_back(&s);
    (s.backup ? backups : mains).push_back(&s);
  }
  auto state = [&](const Source& source) {
    auto it = states.find(source.id());
    if (it != states.end()) return it->second;
    SourceState fresh;
    fresh.id = source.id();
    fresh.name = source.name;
    fresh.backup = source.backup;
    return fresh;
  };
  auto addNote = [&](const std::string& channelID, const std::string& note) {
    auto it = notes.find(channelID);
    if (it == notes.end())
      notes[channelID] = note;
    else
      it->second += "; " + note;
  };
  auto mb = [](int64_t bytes) { return std::to_string(bytes / 1048576); };
  auto count = [](size_t n) { return std::to_string(n); };

  // 1. Main sources: ask for each file only if it changed.
  std::map<std::string, std::string> files;
  for (size_t index = 0; index < mains.size(); ++index) {
    if (cancelled()) break;
    const Source& source = *mains[index];
    progress("Checking listings: " + source.name + " (" + count(index + 1) + " of " +
             count(mains.size()) + ")\xE2\x80\xA6");
    SourceState value = state(source);
    value.checkedAt = nowSec;
    BuildIO::FetchOutcome got =
        io.fetch ? io.fetch(source, value.etag, value.lastModified, true) : BuildIO::FetchOutcome{};
    if (got.fetched) {
      const Fetched& fetched = *got.fetched;
      files[source.id()] = fetched.file;
      value.status = fetched.status;
      value.bytes = fetched.bytes;
      value.problem = fetched.problem;
      if (fetched.changed()) {
        value.etag = fetched.etag;
        value.lastModified = fetched.lastModified;
        value.downloadedAt = nowSec;
      }
      std::string how = fetched.status == 304 ? "unchanged"
                        : fetched.status == 0 ? "saved copy (" + fetched.problem.value_or("?") + ")"
                                              : "downloaded";
      std::string line = "guide " + source.name + ": " + how + " " + mb(fetched.bytes) + " MB";
      if (value.lastModified) {
        line += ", file from " + std::to_string((nowSec - *value.lastModified) / 3600) + " h ago";
      }
      log.push_back(line);
    } else {
      std::string error = got.error.empty() ? "no download" : got.error;
      value.status.reset();
      value.problem = error;
      problems.push_back(source.name + ": " + error);
      log.push_back("guide " + source.name + ": failed (" + error + ")");
    }
    states[source.id()] = value;
  }

  // 2. The main listings: reused when nothing changed, else read again.
  bool unchanged = std::all_of(mains.begin(), mains.end(), [&](const Source* s) {
    auto it = states.find(s->id());
    return it != states.end() && (it->second.status == 304 || it->second.status == 0);
  });
  bool reuse = unchanged && previous && previous->targetsKey == key &&
               previous->detailVersion.value_or(1) >= kDetailVersion;
  if (reuse) {
    for (const auto& [id, list] : previous->programs) {
      std::vector<RawProgram> kept;
      for (const RawProgram& p : list) {
        if (p.end() > window.low) kept.push_back(p);
      }
      if (!kept.empty()) merged[id] = std::move(kept);
    }
    notes = previous->notes.value_or(std::map<std::string, std::string>());
    log.push_back("guide main sources unchanged: saved listings reused (" + count(merged.size()) +
                  " channels)");
  } else {
    for (size_t index = 0; index < mains.size(); ++index) {
      if (cancelled()) break;
      const Source& source = *mains[index];
      auto file = files.find(source.id());
      if (file == files.end()) continue;
      progress("Updating listings: " + source.name + " (" + count(index + 1) + " of " +
               count(mains.size()) + ")\xE2\x80\xA6");
      BuildIO::ReadOutcome read =
          io.read ? io.read(source, file->second, targets, window) : BuildIO::ReadOutcome{};
      if (!read.result) {
        problems.push_back(source.name + ": " + (read.error.empty() ? "unreadable" : read.error));
        continue;
      }
      const SourceResult& found = *read.result;
      for (const auto& [channelID, entry] : found.programs) {
        auto existing = merged.find(channelID);
        merged[channelID] =
            mergePrograms(existing == merged.end() ? std::vector<RawProgram>() : existing->second,
                          entry.programs);
        addNote(channelID, source.name + ": " + entry.note);
      }
      SourceState value = state(source);
      value.guideChannels = found.guideChannels;
      value.matched = static_cast<int>(found.programs.size());
      value.horizonMedian = found.horizonMedian;
      value.parseSeconds = found.seconds;
      states[source.id()] = value;
      log.push_back("guide " + source.name + ": " + count(found.programs.size()) +
                    " of your channels from " + std::to_string(found.guideChannels) + ", read in " +
                    std::to_string(static_cast<int64_t>(found.seconds)) + " s, half end within " +
                    std::to_string(found.horizonMedian ? (*found.horizonMedian - now) / 60 : 0) +
                    " h");
    }
    // Never trade listings for shorter ones: what the last update had beyond
    // a channel's new end is kept.
    if (previous) {
      int kept = 0;
      for (const auto& [id, old] : previous->programs) {
        auto current = merged.find(id);
        int64_t newEnd = lastEnd(current == merged.end() ? nullptr : &current->second);
        std::vector<RawProgram> extra;
        for (const RawProgram& p : old) {
          if (p.s + 5 >= newEnd && p.end() > now && p.s < window.high) extra.push_back(p);
        }
        if (extra.empty()) continue;
        merged[id] = mergePrograms(
            current == merged.end() ? std::vector<RawProgram>() : current->second, extra);
        kept += 1;
      }
      if (kept > 0) {
        log.push_back("guide kept the last update's later listings for " + std::to_string(kept) +
                      " channels");
      }
    }
  }

  // 3. Which main sources are stale: an old file, or listings that end soon
  // (or no file at all). Short listings only count where a backup can help
  // (the free streaming guides are always short and refresh often).
  std::set<std::string> staleShards;
  for (const Source* source : mains) {
    SourceState value = state(*source);
    bool old = value.lastModified && nowSec - *value.lastModified > kStaleAge;
    bool backed = std::any_of(backups.begin(), backups.end(),
                              [&](const Source* b) { return b->appliesTo({source->shard}); });
    bool isShort = backed && value.horizonMedian && *value.horizonMedian < now + kShortHorizon;
    bool missing = files.find(source->id()) == files.end();
    value.stale = old || isShort || missing;
    states[source->id()] = value;
    if (*value.stale) staleShards.insert(source->shard);
  }

  // 4. Backups for the stale shards, each only filling what's missing.
  for (size_t index = 0; index < backups.size(); ++index) {
    if (cancelled()) break;
    const Source& source = *backups[index];
    SourceState value = state(source);
    if (!source.appliesTo(staleShards)) {
      value.used = false;
      value.extended = 0;
      states[source.id()] = value;
      continue;
    }
    progress("Filling gaps: " + source.name + " (" + count(index + 1) + " of " +
             count(backups.size()) + ")\xE2\x80\xA6");
    value.checkedAt = nowSec;
    BuildIO::FetchOutcome got = io.fetch ? io.fetch(source, value.etag, value.lastModified, false)
                                         : BuildIO::FetchOutcome{};
    std::optional<std::string> failure;
    if (!got.fetched) {
      failure = got.error.empty() ? "no download" : got.error;
    } else {
      const Fetched& fetched = *got.fetched;
      value.status = fetched.status;
      value.bytes = fetched.bytes;
      value.problem = fetched.problem;
      if (fetched.changed()) {
        value.etag = fetched.etag;
        value.lastModified = fetched.lastModified;
        value.downloadedAt = nowSec;
      }
      // A wider window, so times can still be moved into it.
      int shiftRoom = source.calibrate ? 16 * 60 : 0;
      Window wide{window.low - shiftRoom, window.high + shiftRoom};
      BuildIO::ReadOutcome read =
          io.read ? io.read(source, fetched.file, targets, wide) : BuildIO::ReadOutcome{};
      if (!read.result) {
        failure = read.error.empty() ? "unreadable" : read.error;
      } else {
        const SourceResult& found = *read.result;
        ProgramsById programsById;
        for (const auto& [id, entry] : found.programs) programsById[id] = entry.programs;
        std::map<std::string, int> perChannel;
        if (source.calibrate) {
          LearnedShift learned = learnShift(merged, programsById);
          int shift = learned.shift ? *learned.shift : value.shift.value_or(source.assumedShift);
          value.shift = shift;
          value.shiftNote =
              learned.note + (learned.shift ? "" : "; using " + std::to_string(shift / 60) + " h");
          perChannel = learned.perChannel;
          ProgramsById moved;
          for (const auto& [id, list] : programsById) {
            auto own = perChannel.find(id);
            int delta = own != perChannel.end() ? own->second : shift;
            std::vector<RawProgram> out;
            for (const RawProgram& p : list) {
              int s = p.s - delta;
              if (!(s + p.m > window.low && s < window.high)) continue;
              RawProgram q = p;
              q.s = s;
              out.push_back(std::move(q));
            }
            if (!out.empty()) moved[id] = std::move(out);
          }
          programsById = std::move(moved);
          log.push_back("guide " + source.name + " clock: " + value.shiftNote.value_or(""));
        }
        int extended = 0;
        std::string tag = source.name + ":";
        for (const auto& [channelID, list] : programsById) {
          auto current = merged.find(channelID);
          const std::vector<RawProgram>* base =
              current == merged.end() ? nullptr : &current->second;
          int64_t before = lastEnd(base);
          std::vector<RawProgram> after =
              mergePrograms(base ? *base : std::vector<RawProgram>(), list);
          if (!(lastEnd(&after) > before + 30)) continue;
          merged[channelID] = std::move(after);
          extended += 1;
          auto entry = found.programs.find(channelID);
          if (entry != found.programs.end()) {
            auto own = perChannel.find(channelID);
            int delta = own != perChannel.end() ? own->second : value.shift.value_or(0);
            std::string note = source.name + ": " + entry->second.note;
            if (delta != 0) note += ", times \xE2\x88\x92" + std::to_string(delta / 60) + " h";
            addNote(channelID, note);
          }
        }
        value.guideChannels = found.guideChannels;
        value.matched = static_cast<int>(programsById.size());
        int appliedShift = value.shift.value_or(0);
        if (found.horizonMedian)
          value.horizonMedian = *found.horizonMedian - appliedShift;
        else
          value.horizonMedian.reset();
        value.parseSeconds = found.seconds;
        value.used = true;
        // Counted from the notes, so fills kept from the last update count too.
        int fills = 0;
        for (const auto& [id, note] : notes) {
          if (note.find(tag) != std::string::npos) ++fills;
        }
        value.extended = fills;
        std::string how = fetched.status == 304 ? "unchanged"
                          : fetched.status == 0 ? "saved copy"
                                                : "downloaded";
        log.push_back("guide " + source.name + ": " + how + " " + mb(fetched.bytes) + " MB, " +
                      count(programsById.size()) + " of your channels, extended " +
                      std::to_string(extended) + " now, fills " + std::to_string(fills) +
                      " in all, read in " + std::to_string(static_cast<int64_t>(found.seconds)) +
                      " s");
      }
    }
    if (failure) {
      value.problem = *failure;
      value.used = false;
      log.push_back("guide " + source.name + ": failed (" + *failure + ")");
    }
    states[source.id()] = value;
  }
  // Sources no longer in use are forgotten.
  std::unordered_set<std::string> ids;
  for (const Source* s : chosen) ids.insert(s->id());
  for (auto it = states.begin(); it != states.end();) {
    it = ids.count(it->first) ? std::next(it) : states.erase(it);
  }

  bool retry = !staleShards.empty();
  BuildResult result;
  result.nextCheck = nowSec + (retry ? kStaleRetry : kRefreshAge);
  size_t ahead = 0;
  for (const auto& [id, list] : merged) {
    int64_t end = list.empty() ? 0 : lastEnd(&list);
    if (end > now + kShortHorizon) ++ahead;
  }
  std::string done = "guide update done in " +
                     std::to_string(static_cast<int64_t>(io.elapsed ? io.elapsed() : 0)) +
                     " s: " + count(merged.size()) + " channels listed, " + count(ahead) +
                     " reach 12 h+ ahead";
  if (retry) {
    std::vector<std::string> staleList(staleShards.begin(), staleShards.end());
    done += "; stale: " + str::join(staleList, ", ") + ", next check in 2 h";
  }
  log.push_back(done);
  result.programs = std::move(merged);
  result.problems = std::move(problems);
  result.notes = std::move(notes);
  result.states = std::move(states);
  result.targetsKey = std::move(key);
  result.log = std::move(log);
  return result;
}

}  // namespace marigold::guide
