#include "core/report/channel_report.hpp"

#include <algorithm>
#include <utility>

#include "core/guide/genre.hpp"
#include "core/report/csv.hpp"
#include "core/util/redact.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::report {
namespace {

template <typename Map>
const typename Map::mapped_type* find(const Map* map, const std::string& key) {
  if (!map) return nullptr;
  auto it = map->find(key);
  return it == map->end() ? nullptr : &it->second;
}

// The same for maps of shared lists.
template <typename Map>
const typename Map::mapped_type::element_type* findShared(const Map* map, const std::string& key) {
  if (!map) return nullptr;
  auto it = map->find(key);
  return it == map->end() ? nullptr : it->second.get();
}

// The first `count` characters (code points; Swift's prefix counts
// grapheme clusters, which only differs for combining marks and emoji).
std::string prefixChars(std::string_view s, size_t count) {
  size_t i = 0, seen = 0;
  while (i < s.size() && seen < count) {
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    ++seen;
  }
  return std::string(s.substr(0, i));
}

// `word` (ASCII case-insensitive) replaced everywhere in `text`.
std::string blank(std::string text, std::string_view word) {
  if (word.empty()) return text;
  std::string lowered = str::lowerAscii(text);
  std::string needle = str::lowerAscii(word);
  size_t at = lowered.find(needle);
  if (at == std::string::npos) return text;
  std::string out;
  size_t from = 0;
  while (at != std::string::npos) {
    out.append(text, from, at - from);
    out += "[hidden]";
    from = at + needle.size();
    at = lowered.find(needle, from);
  }
  out.append(text, from, std::string::npos);
  return out;
}

class Cells {
 public:
  explicit Cells(const std::vector<std::string>& words) {
    for (const std::string& w : words)
      if (str::utf8Length(str::trim(w)) >= 4) words_.emplace_back(str::trim(w));
    // Longest first, so a host isn't half-blanked by a shorter word in it.
    std::sort(words_.begin(), words_.end(),
              [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
  }

  // The safety pass: private words blanked, then redact().
  std::string safe(std::string_view value) const {
    std::string out(value);
    for (const std::string& w : words_) out = blank(std::move(out), w);
    return redact(out);
  }

  // Swift `cell`, with the safety pass first.
  std::string operator()(std::string_view value) const {
    if (value.empty()) return "\"\"";
    return csvCell(safe(value));
  }

 private:
  std::vector<std::string> words_;
};

// Programmes in the window [low, high) (minutes since 1970).
int count(const std::vector<guide::RawProgram>* raw, int low, int high) {
  if (!raw) return 0;
  int n = 0;
  for (const auto& p : *raw) n += p.s < high && p.s + p.m > low ? 1 : 0;
  return n;
}

int tagged(const std::vector<guide::RawProgram>* raw, int low, int high) {
  if (!raw) return 0;
  int n = 0;
  for (const auto& p : *raw)
    n += p.s < high && p.s + p.m > low && (p.f & guide::genre::kGenreBits) != 0 ? 1 : 0;
  return n;
}

// "yyyy-MM-dd HH:mm" in `zone` (en_US_POSIX DateFormatter).
std::string minuteStamp(time::Seconds t, const time::Zone& zone) {
  time::Civil c = zone.toLocal(t);
  auto pad = [](int v, size_t width) {
    std::string s = std::to_string(v);
    if (s.size() < width) s.insert(0, width - s.size(), '0');
    return s;
  };
  return pad(c.year, 4) + "-" + pad(c.month, 2) + "-" + pad(c.day, 2) + " " + pad(c.hour, 2) + ":" +
         pad(c.minute, 2);
}

const Program* current(const std::vector<Program>* list, time::Seconds now) {
  if (!list) return nullptr;
  for (const Program& p : *list)
    if (p.isOn(now)) return &p;
  return nullptr;
}

std::string providerGuideNote(const Channel& channel, bool isXtream,
                              const catalog::ChannelTraits* trait,
                              const ChannelReportInput& in) {
  if (!isXtream) return "";
  if (const std::string* note = find(in.providerNotes, channel.id)) return *note;
  if (trait && trait->familyKey) return "event feed";
  if (!channel.epgId || channel.epgId->empty()) return "no guide id";
  static const std::set<std::string> kNoCountries;
  if (!guide::provider::inScope(channel, in.providerCountries ? *in.providerCountries : kNoCountries))
    return "country not selected";
  const guide::provider::Status* status = find(in.providerStates, channel.serverId);
  if (!status) return "provider guide not loaded";
  switch (status->kind) {
    case guide::provider::Status::Kind::ready:
      return "not in provider guide";
    case guide::provider::Status::Kind::updating:
      return "provider guide loading";
    case guide::provider::Status::Kind::unavailable:
    case guide::provider::Status::Kind::failed:
      return "provider guide: " + status->message;
  }
  return "";
}

}  // namespace

bool covers(const std::vector<guide::RawProgram>& raw, time::Seconds windowStart,
            double guideHours) {
  int low = static_cast<int>(windowStart / 60);
  int high = low + static_cast<int>(guideHours * 60);
  return std::any_of(raw.begin(), raw.end(),
                     [&](const guide::RawProgram& p) { return p.s < high && p.s + p.m > low; });
}

std::vector<std::string> guideCheckIds(const std::vector<std::string>& entryIds,
                                       const catalog::CatalogResult& catalog,
                                       const std::function<bool(const std::string&)>& isRawChannel,
                                       const std::function<bool(const std::string&)>& providerCovers,
                                       int limit) {
  std::unordered_map<std::string, const catalog::EntryInfo*> infoById;
  infoById.reserve(catalog.entries.size());
  for (const auto& e : catalog.entries) infoById.emplace(e.id, &e);
  std::set<std::string> seen;
  std::vector<std::string> ids;
  for (const std::string& entry : entryIds) {
    if (static_cast<int>(ids.size()) >= limit) break;
    std::string member;
    auto info = infoById.find(entry);
    if (info != infoById.end() && !info->second->members.empty()) {
      member = info->second->members.front();
    } else if (isRawChannel && isRawChannel(entry)) {
      member = entry;
    } else {
      continue;
    }
    auto trait = catalog.traits.find(member);
    if (trait != catalog.traits.end() && trait->second.familyKey) continue;
    if (providerCovers && providerCovers(member)) continue;
    if (!seen.insert(member).second) continue;
    ids.push_back(member);
  }
  return ids;
}

const std::vector<std::string>& channelReportColumns() {
  static const std::vector<std::string> kColumns = {
      "number", "name", "group", "tags", "hidden_group", "favorite",
      "callsign_in_name", "station_lookup", "match_key",
      "server_guide", "server_programs_next_3h", "outside_listing", "outside_programs_next_3h",
      "now_title", "now_source", "entry", "category", "in_my_lineup", "event_family", "event_text",
      "provider", "country", "guide_id", "provider_guide", "provider_guide_programs_next_3h",
      "kind", "market", "core_network", "scope",
      "server_genre_tagged_next_3h", "outside_genre_tagged_next_3h",
      "provider_guide_genre_tagged_next_3h", "now_genres",
      "now_subtitle", "now_description", "outside_listings_end", "logo_sources", "logo_status"};
  return kColumns;
}

std::string channelReportCSV(const ChannelReportInput& in,
                             const std::function<void(int, int)>& progress) {
  static const std::vector<Channel> kNone;
  static const guide::StationsByNetwork kNoStations;
  const std::vector<Channel>& channels = in.channels ? *in.channels : kNone;
  const guide::StationsByNetwork& stations = in.stations ? *in.stations : kNoStations;

  const time::Seconds start = in.windowStart;
  const time::Seconds end = start + static_cast<time::Seconds>(in.guideHours * 3600);
  const int low = static_cast<int>(start / 60);
  const int high = static_cast<int>(end / 60);

  std::unordered_map<std::string, const catalog::EntryInfo*> infoById;
  if (in.catalog) {
    infoById.reserve(in.catalog->entries.size());
    for (const auto& e : in.catalog->entries) infoById.emplace(e.id, &e);
  }

  const Cells cell(in.privateWords);
  std::string out = str::join(channelReportColumns(), ",") + "\n";
  out.reserve(channels.size() * 300 + out.size());
  const int total = static_cast<int>(channels.size());

  for (size_t index = 0; index < channels.size(); ++index) {
    const Channel& channel = channels[index];
    static const std::vector<Program> kNoPrograms;
    const std::vector<Program>* serverFound = find(in.serverPrograms, channel.id);
    const std::vector<Program>& serverList = serverFound ? *serverFound : kNoPrograms;

    std::string serverState;
    const ServerGuideState* state = find(in.guideState, channel.id);
    switch (state ? *state : ServerGuideState::NotChecked) {
      case ServerGuideState::Loaded:
        serverState = serverList.empty() ? "none" : "yes";
        break;
      case ServerGuideState::Failed:
        serverState = "failed";
        break;
      case ServerGuideState::Loading:
        serverState = "loading";
        break;
      case ServerGuideState::NotChecked:
        serverState = "not checked";
        break;
    }

    const catalog::ChannelTraits* trait =
        in.catalog ? find(&in.catalog->traits, channel.id) : nullptr;
    const std::string* entryId = in.catalog ? find(&in.catalog->entryOf, channel.id) : nullptr;
    const bool isXtream = str::startsWith(channel.serverId, "xt:");
    const Program* now = current(findShared(in.programs, channel.id), in.now);

    std::string nowSource;
    if (now) {
      if (now->fromOutsideGuide)
        nowSource = "outside";
      else if (str::startsWith(now->id, "pg|") || now->id.find("|pg|") != std::string::npos)
        nowSource = "provider guide";
      else
        nowSource = isXtream ? "provider short guide" : "server";
    }

    int serverCount = 0, serverTagged = 0;
    for (const Program& p : serverList) {
      if (!(p.end > start && p.start < end)) continue;
      ++serverCount;
      if ((p.genres & (kGenreMovie | kGenreSports | kGenreNews | kGenreKids)) != 0) ++serverTagged;
    }

    std::string entryName;
    bool inLineup = false;
    if (entryId) {
      auto info = infoById.find(*entryId);
      if (info != infoById.end()) entryName = info->second->name;
      inLineup = in.lineupIds && in.lineupIds->count(*entryId) > 0;
    }

    const std::vector<guide::RawProgram>* outsideRaw = findShared(in.outsideMatches, channel.id);
    const std::vector<guide::RawProgram>* providerRaw =
        isXtream ? findShared(in.providerMatches, channel.id) : nullptr;
    std::string outsideNote;
    if (const std::string* note = find(in.outsideNotes, channel.id))
      outsideNote = *note;
    else
      outsideNote = outsideRaw ? "matched" : "no match";

    std::string nowGenres;
    if (now) {
      std::vector<std::string> g;
      if (now->genres & kGenreSports) g.emplace_back("sports");
      if (now->genres & kGenreNews) g.emplace_back("news");
      if (now->genres & kGenreMovie) g.emplace_back("movie");
      if (now->genres & kGenreKids) g.emplace_back("kids");
      nowGenres = str::join(g, " ");
    }
    std::string nowDescription;
    if (now && now->overview && !now->overview->empty()) {
      nowDescription = rx::replace(prefixChars(*now->overview, 160), "https?://\\S+", "");
      nowDescription = str::replaceAll(nowDescription, "\n", " ");
    }

    const std::string group = channel.group.value_or("");
    const bool hidden =
        in.hiddenGroups && in.hiddenGroups->count(channel.group.value_or(in.otherGroup)) > 0;
    const bool isLocal = str::lowerAscii(group).find("local") != std::string::npos;
    // The keys derived from the name are read from its safe form: they drop
    // the "://" redact() looks for (and change case and punctuation), so a
    // name holding a link or a host would otherwise leak it as
    // "httpprovider...". Ordinary names are unchanged.
    const std::string safeName = cell.safe(channel.name);
    const std::vector<std::string> signs = guide::callsigns(safeName, isLocal);
    std::optional<std::string> station;
    if (signs.empty()) station = guide::stationCallsign(safeName, stations);

    std::string listingsEnd;
    if (outsideRaw && !outsideRaw->empty()) {
      int last = outsideRaw->front().end();
      for (const auto& p : *outsideRaw) last = std::max(last, p.end());
      listingsEnd = minuteStamp(static_cast<time::Seconds>(last) * 60, in.zone);
    }

    const std::string* provider = find(in.providerNames, channel.serverId);
    const std::pair<std::string, std::string>* logo = entryId ? find(in.logos, *entryId) : nullptr;
    const std::string empty;
    std::vector<std::string> row = {
        cell(channel.number.value_or("")),
        cell(channel.name),
        cell(group),
        cell(str::join(channel.tags, " | ")),
        cell(hidden ? "yes" : ""),
        cell(channel.isFavorite ? "yes" : ""),
        cell(str::join(signs, " ")),
        cell(station.value_or("")),
        cell(guide::normalize(cell.safe(channel.displayName()))),
        cell(serverState),
        cell(std::to_string(serverCount)),
        cell(outsideNote),
        cell(std::to_string(count(outsideRaw, low, high))),
        cell(now ? now->title : empty),
        cell(nowSource),
        cell(entryName),
        cell(trait ? catalog::rawValue(trait->category) : ""),
        cell(inLineup ? "yes" : ""),
        cell(trait && trait->familyKey ? *trait->familyKey : empty),
        cell(trait && trait->eventText ? *trait->eventText : empty),
        cell(provider ? *provider : empty),
        cell(guide::provider::countryPrefix(channel.group).value_or("")),
        cell(channel.epgId.value_or("")),
        cell(providerGuideNote(channel, isXtream, trait, in)),
        cell(isXtream ? std::to_string(count(providerRaw, low, high)) : empty),
        cell(trait ? catalog::rawValue(trait->kind) : ""),
        cell(trait && trait->market ? *trait->market : empty),
        cell(trait && trait->core ? *trait->core : empty),
        cell(trait ? trait->scope : empty),
        cell(std::to_string(serverTagged)),
        cell(std::to_string(tagged(outsideRaw, low, high))),
        cell(isXtream ? std::to_string(tagged(providerRaw, low, high)) : empty),
        cell(nowGenres),
        cell(now && now->episodeTitle ? *now->episodeTitle : empty),
        cell(nowDescription),
        cell(listingsEnd),
        cell(logo ? logo->first : empty),
        cell(logo ? logo->second : empty),
    };
    out += str::join(row, ",");
    out += '\n';
    if (progress && index % 2000 == 1999) progress(static_cast<int>(index + 1), total);
  }
  return out;
}

}  // namespace marigold::report
