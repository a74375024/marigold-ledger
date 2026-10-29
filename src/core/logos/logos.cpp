#include "core/logos/logos.hpp"

#include <algorithm>
#include <set>
#include <unordered_set>

#include "core/catalog/catalog.hpp"
#include "core/logos/table.hpp"
#include "core/util/json.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::logos {
namespace {

std::string displayNetwork(const std::string& word) {
  if (word == "MYTV") return "MyNetworkTV";
  if (word == "TELEMUNDO") return "Telemundo";
  if (word == "UNIVISION") return "Univision";
  if (word == "UNIMAS") return "UniMás";
  if (word == "INDEPENDENT") return "Ind.";
  return word;
}

std::optional<size_t> column(const std::vector<std::string>& head, std::string_view name) {
  auto it = std::find(head.begin(), head.end(), name);
  if (it == head.end()) return std::nullopt;
  return static_cast<size_t>(it - head.begin());
}

// "MTV2" -> "mtv2", "mtv-2"; "LAW & CRIME" -> "law-and-crime", then with
// "-network" etc.: the file names the library might use for a channel name.
std::vector<std::string> logoSetKeys(std::string_view name) {
  static const std::set<std::string> kDrop = {"hd",  "fhd", "uhd",   "sd",    "4k",    "8k",   "hevc",
                                              "raw", "vip", "60fps", "50fps", "1080p", "720p", "east",
                                              "feed", "alt", "backup", "us",  "usa"};
  std::string lowered = str::replaceAll(
      str::replaceAll(str::replaceAll(str::lower(name), "&", " and "), "+", " plus "), "'", "");
  std::vector<std::string> tokens;
  std::string token;
  auto flush = [&] {
    if (!token.empty() && !kDrop.count(token)) tokens.push_back(token);
    token.clear();
  };
  for (char c : lowered) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) token += c;
    else flush();
  }
  flush();
  std::vector<std::string> out;
  if (tokens.empty()) return out;
  auto add = [&out](std::string key) {
    if (!key.empty() && std::find(out.begin(), out.end(), key) == out.end()) out.push_back(std::move(key));
  };
  std::string base = str::join(tokens, "-");
  add(base);
  if (tokens.size() > 1 && tokens.front() == "the")
    add(str::join(std::vector<std::string>(tokens.begin() + 1, tokens.end()), "-"));
  // A number joined to a word: "mtv2" -> "mtv-2".
  std::vector<std::string> split;
  for (const std::string& t : tokens) split.push_back(rx::replace(t, "([a-z])([0-9])", "$1-$2"));
  add(str::join(split, "-"));
  for (const char* word : {"channel", "network", "tv"}) {
    add(base + "-" + word);
    if (tokens.size() > 1 && tokens.back() == word)
      add(str::join(std::vector<std::string>(tokens.begin(), tokens.end() - 1), "-"));
  }
  return out;
}

}  // namespace

std::string chain(const std::vector<std::string>& urls) {
  std::string out;
  for (const std::string& u : urls) {
    if (u.empty() || u.find('\n') != std::string::npos) continue;
    if (!out.empty()) out += '\n';
    out += u;
  }
  return out;
}

std::optional<std::string> logoSetFile(std::string_view name) {
  static const std::unordered_set<std::string_view> kNames(logoSetNames().begin(), logoSetNames().end());
  for (const std::string& key : logoSetKeys(name)) {
    if (kNames.count(key)) return key + "-us.png";
  }
  return std::nullopt;
}

LogoSpec spec(std::string_view name, const std::optional<std::string>& core, bool isWest,
              bool isLocal, const std::optional<std::string>& callsign,
              std::optional<int> virtualNumber, const std::string& provider,
              const std::optional<std::string>& epgID,
              std::optional<catalog::LineupCategory> category, const IptvOrgIndex* iptvOrg,
              const std::string& directoryIptvOrg) {
  auto url = [](const std::string& file) { return std::string(kLogoBase) + file; };
  LogoSpec out;
  auto add = [&out](std::string link, const char* from) {
    out.urls.push_back(std::move(link));
    out.sources.push_back(from);
  };
  if (category) out.subtitle = std::string(catalog::genreTitle(*category));

  if (isLocal && callsign) {
    std::optional<std::string> network = catalog::networkWord(name);
    // The network's own logo first (consistent and made for dark
    // backgrounds), then the station's, then the provider's.
    if (network) {
      auto file = networkLogos().find(*network);
      if (file != networkLogos().end()) add(url(file->second), "logo set");
    }
    static const std::map<std::string, std::string> kNone;
    auto stationIt = stationLogos().find(str::lower(*callsign));
    const std::map<std::string, std::string>& stations =
        stationIt == stationLogos().end() ? kNone : stationIt->second;
    const std::string* file = nullptr;
    if (network) {
      auto byNetwork = stations.find(*network);
      if (byNetwork != stations.end()) file = &byNetwork->second;
    }
    if (!file && stations.size() == 1) file = &stations.begin()->second;
    if (file) add(url(*file), "logo set");
    if (!provider.empty()) add(provider, "provider");
    std::vector<std::string> parts;
    if (network) parts.push_back(displayNetwork(*network));
    if (virtualNumber) parts.push_back(std::to_string(*virtualNumber));
    parts.erase(std::remove(parts.begin(), parts.end(), std::string()), parts.end());
    out.style = LogoSpec::Style::local;
    out.callsign = str::upper(*callsign);
    out.line = str::join(parts, " ");
    out.isWest = false;
    return out;
  }

  if (core) {
    auto file = coreLogos().find(*core);
    if (file != coreLogos().end()) {
      add(url(file->second), "logo set");
      if (!provider.empty()) add(provider, "provider");
      out.style = LogoSpec::Style::logoOnly;
      out.isWest = isWest;
      return out;
    }
  }

  // A local with no call sign ("FOX 5 San Diego"): its network's logo.
  if (category == catalog::LineupCategory::local) {
    if (auto network = catalog::networkWord(name)) {
      auto file = networkLogos().find(*network);
      if (file != networkLogos().end()) add(url(file->second), "logo set");
    }
  }
  // Marigold: a national channel the tables don't cover, by its name in the
  // same library ("MTV2", "Fox Weather", "Cinemax Hits"): shown like the
  // curated ones, the provider's logo behind it.
  if (category != catalog::LineupCategory::local && category != catalog::LineupCategory::international) {
    if (auto file = logoSetFile(name)) {
      add(url(*file), "logo set");
      if (!provider.empty()) add(provider, "provider");
      out.style = LogoSpec::Style::logoOnly;
      out.isWest = isWest;
      return out;
    }
  }
  // The provider's logo always comes last: it is usually the worst.
  if (iptvOrg) {
    if (auto outside = iptvOrg->url(epgID, name, directoryIptvOrg)) add(*outside, "public database");
  }
  if (!provider.empty()) add(provider, "provider");
  out.style = LogoSpec::Style::named;
  out.isWest = isWest;
  return out;
}

std::optional<IptvOrgIndex::Saved> IptvOrgIndex::build(std::string_view channelsCsv,
                                                       std::string_view logosCsv, std::string_view feedsCsv) {
  std::vector<std::vector<std::string>> channelRows = csvRows(channelsCsv);
  std::vector<std::vector<std::string>> logoRows = csvRows(logosCsv);
  if (channelRows.empty() || logoRows.empty()) return std::nullopt;
  const std::vector<std::string>& channelHead = channelRows.front();
  const std::vector<std::string>& logoHead = logoRows.front();
  auto idCol = column(channelHead, "id"), nameCol = column(channelHead, "name"),
       countryCol = column(channelHead, "country");
  auto logoChannel = column(logoHead, "channel"), urlCol = column(logoHead, "url");
  if (!idCol || !nameCol || !countryCol || !logoChannel || !urlCol) return std::nullopt;
  auto altCol = column(channelHead, "alt_names");
  auto closedCol = column(channelHead, "closed");
  auto feedCol = column(logoHead, "feed");
  auto formatCol = column(logoHead, "format");
  auto inUseCol = column(logoHead, "in_use");

  // Best logo per channel: the channel's own (no feed) first.
  struct Logo {
    std::string url;
    bool main = false;
  };
  std::unordered_map<std::string, Logo> logoOf;
  for (size_t r = 1; r < logoRows.size(); ++r) {
    const std::vector<std::string>& row = logoRows[r];
    if (row.size() <= std::max(*logoChannel, *urlCol)) continue;
    if (inUseCol && *inUseCol < row.size() && str::upper(row[*inUseCol]) == "FALSE") continue;
    const std::string& link = row[*urlCol];
    std::string format = formatCol && *formatCol < row.size() ? str::upper(row[*formatCol]) : "";
    std::string lower = str::lower(link);
    bool isRaster = format == "PNG" || format == "JPEG" || format == "JPG" ||
                    ((str::endsWith(lower, ".png") || str::endsWith(lower, ".jpg") ||
                      str::endsWith(lower, ".jpeg")) &&
                     format.empty());
    if (!isRaster || !str::startsWith(lower, "https://")) continue;
    bool main = !feedCol || *feedCol >= row.size() || row[*feedCol].empty();
    const std::string& id = row[*logoChannel];
    auto existing = logoOf.find(id);
    if (existing != logoOf.end() && (existing->second.main || !main)) continue;
    logoOf[id] = {link, main};
  }

  // Other countries' channels with an English feed, which US providers
  // carry too ("Stingray Hit List", "France 24", "BabyTV").
  std::unordered_set<std::string> english;
  if (!feedsCsv.empty()) {
    std::vector<std::vector<std::string>> feedRows = csvRows(feedsCsv);
    if (!feedRows.empty()) {
      auto feedChannel = column(feedRows.front(), "channel"), languages = column(feedRows.front(), "languages");
      for (size_t r = 1; feedChannel && languages && r < feedRows.size(); ++r) {
        const std::vector<std::string>& row = feedRows[r];
        if (row.size() <= std::max(*feedChannel, *languages)) continue;
        for (std::string_view l : str::split(row[*languages], ';')) {
          if (l == "eng") english.insert(row[*feedChannel]);
        }
      }
    }
  }

  Saved saved;
  size_t need = std::max({*idCol, *nameCol, *countryCol});
  // US channels first: a name another country's channel shares is the US one's.
  for (bool us : {true, false}) {
    for (size_t r = 1; r < channelRows.size(); ++r) {
      const std::vector<std::string>& row = channelRows[r];
      if (row.size() <= need) continue;
      const std::string& id = row[*idCol];
      if ((row[*countryCol] == "US") != us || (!us && !english.count(id))) continue;
      if (closedCol && *closedCol < row.size() && !row[*closedCol].empty()) continue;
      auto logo = logoOf.find(id);
      if (logo == logoOf.end()) continue;
      saved.byID[str::lower(id)] = logo->second.url;
      std::vector<std::string> names{row[*nameCol]};
      if (altCol && *altCol < row.size() && !row[*altCol].empty()) {
        for (std::string_view alt : str::split(row[*altCol], ';')) names.emplace_back(alt);
      }
      for (const std::string& n : names) {
        std::string key = catalog::coreKey(n).key;
        if (!key.empty()) saved.byName.try_emplace(key, logo->second.url);
      }
    }
    if (us && saved.byID.empty()) return std::nullopt;
  }
  return saved;
}

std::optional<std::string> IptvOrgIndex::url(const std::optional<std::string>& epgID, std::string_view name,
                                             const std::string& directoryIptvOrg) const {
  for (const std::optional<std::string>& id : {epgID, std::optional<std::string>(directoryIptvOrg)}) {
    if (!id || id->empty()) continue;
    auto hit = saved_.byID.find(str::lower(*id));
    if (hit != saved_.byID.end()) return hit->second;
  }
  std::string key = catalog::coreKey(name).key;
  if (key.empty()) return std::nullopt;
  auto hit = saved_.byName.find(key);
  if (hit != saved_.byName.end()) return hit->second;
  // Providers' names for channels the database spells otherwise.
  static const std::unordered_map<std::string, std::string> kSpelledOtherwise = [] {
    std::unordered_map<std::string, std::string> m;
    for (auto [provider, id] : {std::pair{"EarthX", "earthxtv.us"}, std::pair{"FlixFling Now", "flixfling.us"},
                                std::pair{"The Victory Channel", "victorychannel.us"},
                                std::pair{"Fox Sports Racing", "foxsportsracing.us"},
                                std::pair{"Al Jazeera English", "aljazeera.qa"}}) {
      m.emplace(catalog::coreKey(provider).key, id);
    }
    return m;
  }();
  if (auto other = kSpelledOtherwise.find(key); other != kSpelledOtherwise.end()) {
    if (auto byId = saved_.byID.find(other->second); byId != saved_.byID.end()) return byId->second;
  }
  return std::nullopt;
}

std::vector<std::vector<std::string>> csvRows(std::string_view text) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string field;
  bool quoted = false;
  size_t i = 0;
  while (i < text.size()) {
    char c = text[i++];
    if (quoted) {
      if (c == '"') {
        if (i < text.size()) {
          if (text[i] == '"') {
            field += '"';
            ++i;
          } else {
            quoted = false;  // the next character is read unquoted
          }
        } else {
          quoted = false;
        }
      } else {
        field += c;
      }
      continue;
    }
    switch (c) {
      case '"':
        quoted = true;
        break;
      case ',':
        row.push_back(std::move(field));
        field.clear();
        break;
      case '\n':
        row.push_back(std::move(field));
        field.clear();
        rows.push_back(std::move(row));
        row.clear();
        break;
      case '\r':
        break;
      default:
        field += c;
        break;
    }
  }
  if (!field.empty() || !row.empty()) {
    row.push_back(std::move(field));
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace marigold::logos
