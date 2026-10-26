#include "core/catalog/work.hpp"

#include <algorithm>
#include <cmath>

#include "core/catalog/text.hpp"
#include "core/guide/genre.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::catalog {

std::string sizeText(int64_t bytes) {
  if (bytes >= 1000000) {
    int64_t tenths = static_cast<int64_t>(std::llround(static_cast<double>(bytes) / 100000.0));
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MB";
  }
  return std::to_string(std::max<int64_t>(1, bytes / 1000)) + " KB";
}

std::string ageText(time::Seconds then, time::Seconds now) {
  int64_t seconds = now - then;
  if (seconds < 120) return std::to_string(seconds) + " s";
  if (seconds < 7200) return std::to_string(seconds / 60) + " min";
  return std::to_string(seconds / 3600) + " h";
}

std::vector<Channel> annotated(const std::vector<Channel>& list) {
  static const char* const kPattern =
      "^\\s*(?:\\[([^\\]]{1,24})\\]|\\(([^)]{1,24})\\)|([^:|\\[\\(]{1,24}?)\\s*[:|])\\s*(.+)$";
  struct Parsed {
    std::optional<std::string> prefix;  // lower-cased key
    std::string rest;
  };
  std::vector<Parsed> parsed;
  parsed.reserve(list.size());
  std::unordered_map<std::string, int> prefixCounts;
  std::unordered_map<std::string, std::string> display;
  for (const Channel& channel : list) {
    const std::string& name = channel.name;
    if (auto m = rx::first(kPattern, name)) {
      std::optional<std::string> prefix;
      for (size_t g = 1; g <= 3; ++g) {
        if (m->has(g)) {
          prefix = std::string(str::trimUnicode(m->group(g)));
          break;
        }
      }
      std::string rest = m->has(4) ? std::string(str::trimUnicode(m->group(4))) : name;
      if (prefix && !prefix->empty() && !rest.empty()) {
        std::string key = str::lower(*prefix);
        prefixCounts[key] += 1;
        display.try_emplace(key, *prefix);
        parsed.push_back({key, std::move(rest)});
        continue;
      }
    }
    parsed.push_back({std::nullopt, name});
  }
  std::vector<Channel> out;
  out.reserve(list.size());
  for (size_t i = 0; i < list.size(); ++i) {
    Channel updated = list[i];
    const Parsed& info = parsed[i];
    if (!list[i].tags.empty()) {
      updated.group = list[i].tags.front();
    } else if (info.prefix && prefixCounts[*info.prefix] >= 3) {
      updated.group = display[*info.prefix];
    }
    updated.shortName = info.prefix && updated.group ? info.rest : std::string();
    out.push_back(std::move(updated));
  }
  return out;
}

std::optional<std::vector<Channel>> renamedEventSlots(
    const std::vector<Channel>& old, const std::vector<Channel>& fresh,
    const std::unordered_map<std::string, ChannelTraits>& traits) {
  if (old.size() != fresh.size() || old.empty()) return std::nullopt;
  std::unordered_map<std::string, const Channel*> oldByID;
  oldByID.reserve(old.size());
  for (const Channel& c : old) oldByID[c.id] = &c;
  std::vector<Channel> renamed;
  for (const Channel& channel : fresh) {
    auto it = oldByID.find(channel.id);
    if (it == oldByID.end()) return std::nullopt;
    const Channel& previous = *it->second;
    if (previous.group != channel.group || previous.tags != channel.tags ||
        previous.epgId != channel.epgId) {
      return std::nullopt;
    }
    if (previous.name != channel.name) {
      auto t = traits.find(channel.id);
      if (t == traits.end() || !t->second.familyKey) return std::nullopt;
      renamed.push_back(channel);
    }
  }
  return renamed;
}

time::Seconds windowStart(time::Seconds date, const time::Zone& zone) {
  time::Civil local = zone.toLocal(date);
  local.minute = local.minute < 30 ? 0 : 30;
  local.second = 0;
  return zone.fromLocal(local);
}

std::vector<Program> listing(const std::string& id, const std::vector<Program>* server,
                             const std::vector<guide::RawProgram>* provider,
                             const std::vector<guide::RawProgram>* outside, time::Seconds start) {
  time::Seconds from = start - 3600;
  time::Seconds to = from + 14 * 3600;
  std::vector<Program> shortList = server ? *server : std::vector<Program>();
  std::vector<Program> own = shortList;
  if (provider) {
    std::vector<Program> full = guide::programs(*provider, id, from, to, "pg", false);
    if (!full.empty()) own = bestListing(&full, &shortList, start);
  }
  if (!outside) return bestListing(&own, nullptr, start);
  std::vector<Program> outsideList = guide::programs(*outside, id, from, to);
  return bestListing(&own, &outsideList, start);
}

std::vector<Program> bestListing(const std::vector<Program>* serverIn,
                                 const std::vector<Program>* outside, time::Seconds start) {
  std::vector<Program> server = serverIn ? *serverIn : std::vector<Program>();
  if (!outside || outside->empty()) return server;
  if (server.empty()) return *outside;
  time::Seconds end = start + static_cast<time::Seconds>(kGuideHours * 3600);
  auto coverage = [&](const std::vector<Program>& list) {
    time::Seconds total = 0;
    for (const Program& p : list)
      total += std::max<time::Seconds>(0, std::min(p.end, end) - std::max(p.start, start));
    return total;
  };
  bool outsideWins = coverage(*outside) > coverage(server) + 15 * 60;
  const std::vector<Program>& winner = outsideWins ? *outside : server;
  const std::vector<Program>& secondary = outsideWins ? server : *outside;
  std::vector<Program> result = guide::genre::mergeFlags(winner, secondary);
  for (const Program& program : secondary) {
    bool clashes = std::any_of(result.begin(), result.end(), [&](const Program& other) {
      return std::min(program.end, other.end) - std::max(program.start, other.start) > 300;
    });
    if (!clashes) result.push_back(program);
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const Program& a, const Program& b) { return a.start < b.start; });
  return result;
}

std::unordered_map<std::string, std::vector<Program>> mergedListings(
    const std::set<std::string>& ids,
    const std::unordered_map<std::string, std::vector<Program>>& server,
    const std::unordered_map<std::string, std::vector<guide::RawProgram>>& provider,
    const std::unordered_map<std::string, std::vector<guide::RawProgram>>& outside,
    time::Seconds start) {
  std::unordered_map<std::string, std::vector<Program>> merged;
  merged.reserve(ids.size());
  auto find = [](const auto& map, const std::string& id) {
    auto it = map.find(id);
    return it == map.end() ? nullptr : &it->second;
  };
  for (const std::string& id : ids) {
    std::vector<Program> list =
        listing(id, find(server, id), find(provider, id), find(outside, id), start);
    if (!list.empty()) merged[id] = std::move(list);
  }
  return merged;
}

LineupOutput lineup(const LineupInput& input) {
  const LineupPrefs& prefs = input.prefs;
  auto member = [&](const std::string& id) -> const Channel* {
    auto it = input.indexByID.find(id);
    if (it == input.indexByID.end() || it->second >= input.channels.size()) return nullptr;
    return &input.channels[it->second];
  };
  LineupOutput out;
  for (const EntryInfo& info : input.infos) {
    if (prefs.removed.count(info.id)) continue;
    bool favorite =
        std::any_of(info.members.begin(), info.members.end(), [&](const std::string& m) {
          const Channel* c = member(m);
          return c && c->isFavorite;
        });
    if (favorite || prefs.added.count(info.id)) {
      out.lineupIDs.insert(info.id);
    } else if (prefs.configured) {
      bool hasGuide =
          std::any_of(info.members.begin(), info.members.end(),
                      [&](const std::string& m) { return input.guideIDs.count(m) > 0; });
      // Suggestions are worked out live, so channels join as their listings
      // load (and core networks / market locals always).
      if (suggests(info, prefs, hasGuide, info.name)) out.lineupIDs.insert(info.id);
    }
  }
  // No channel numbers: channels come and go, and the remote can't dial.
  std::vector<const EntryInfo*> ordered;
  ordered.reserve(input.infos.size());
  for (const EntryInfo& info : input.infos) ordered.push_back(&info);
  std::sort(ordered.begin(), ordered.end(), [](const EntryInfo* a, const EntryInfo* b) {
    int ra = rank(a->category), rb = rank(b->category);
    if (ra != rb) return ra < rb;
    if (a->category == LineupCategory::local) {
      int na = a->virtualNumber.value_or(999), nb = b->virtualNumber.value_or(999);
      if (na != nb) return na < nb;
    }
    int byName = text::standardCompare(a->name, b->name);
    if (byName != 0) return byName < 0;
    return a->id < b->id;
  });
  std::unordered_map<std::string, std::string> marketOf;
  for (const EntryInfo* info : ordered) {
    std::vector<const Channel*> members;
    for (const std::string& m : info->members) {
      if (const Channel* c = member(m)) members.push_back(c);
    }
    if (members.empty()) continue;
    const Channel& primary = *members.front();
    if (info->market) marketOf[info->id] = *info->market;
    Channel entry;
    entry.id = info->id;
    entry.serverId = primary.serverId;
    entry.itemId = primary.itemId;
    entry.name = info->name;
    for (const Channel* c : members) {
      if (!c->imageUrl.empty()) {
        entry.imageUrl = c->imageUrl;
        break;
      }
    }
    entry.isFavorite =
        std::any_of(members.begin(), members.end(), [](const Channel* c) { return c->isFavorite; });
    entry.tags = primary.tags;
    entry.isRadio = primary.isRadio;
    entry.group = primary.group;
    entry.shortName = info->name;
    out.entries.push_back(std::move(entry));
  }
  for (size_t i = 0; i < out.entries.size(); ++i) out.entryIndex.try_emplace(out.entries[i].id, i);
  for (const Channel& e : out.entries) {
    if (out.lineupIDs.count(e.id)) out.lineupEntries.push_back(e);
  }
  for (const Channel& e : out.entries) {
    auto market = marketOf.find(e.id);
    std::optional<std::string> m =
        market == marketOf.end() ? std::nullopt : std::optional<std::string>(market->second);
    if (out.lineupIDs.count(e.id) || e.isFavorite || prefs.includes(m))
      out.browseEntries.push_back(e);
  }
  std::map<std::string, int> counts;
  for (const Channel& e : out.browseEntries) counts[e.group ? *e.group : input.otherGroup] += 1;
  out.groups.assign(counts.begin(), counts.end());
  std::sort(out.groups.begin(), out.groups.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) return a.second > b.second;
    int c = text::standardCompare(a.first, b.first);
    return c != 0 ? c < 0 : a.first < b.first;
  });
  return out;
}

}  // namespace marigold::catalog
