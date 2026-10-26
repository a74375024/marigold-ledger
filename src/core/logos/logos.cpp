#include "core/logos/logos.hpp"

#include <algorithm>

#include "core/catalog/catalog.hpp"
#include "core/logos/table.hpp"
#include "core/util/json.hpp"
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

json::Value stringMap(const std::unordered_map<std::string, std::string>& m) {
  // json objects are sorted maps: stable output.
  json::Value o = json::Value::object();
  for (const auto& [k, v] : m) o.set(k, v);
  return o;
}

}  // namespace

LogoSpec spec(std::string_view name, const std::optional<std::string>& core, bool isWest,
              bool isLocal, const std::optional<std::string>& callsign,
              std::optional<int> virtualNumber, const std::string& provider,
              const std::optional<std::string>& epgID,
              std::optional<catalog::LineupCategory> category, const IptvOrgIndex* iptvOrg) {
  auto url = [](const std::string& file) { return std::string(kLogoBase) + file; };
  LogoSpec out;
  if (category) out.subtitle = std::string(catalog::genreTitle(*category));

  if (isLocal && callsign) {
    std::optional<std::string> network = catalog::networkWord(name);
    // The network's own logo first (consistent and made for dark
    // backgrounds), then the station's, then the provider's.
    if (network) {
      auto file = networkLogos().find(*network);
      if (file != networkLogos().end()) out.urls.push_back(url(file->second));
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
    if (file) out.urls.push_back(url(*file));
    if (!provider.empty()) out.urls.push_back(provider);
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
      out.urls.push_back(url(file->second));
      if (!provider.empty()) out.urls.push_back(provider);
      out.style = LogoSpec::Style::logoOnly;
      out.isWest = isWest;
      return out;
    }
  }

  // A local with no call sign ("FOX 5 San Diego"): its network's logo.
  if (category == catalog::LineupCategory::local) {
    if (auto network = catalog::networkWord(name)) {
      auto file = networkLogos().find(*network);
      if (file != networkLogos().end()) out.urls.push_back(url(file->second));
    }
  }
  if (!provider.empty()) out.urls.push_back(provider);
  if (iptvOrg) {
    if (auto outside = iptvOrg->url(epgID, name)) out.urls.push_back(*outside);
  }
  out.style = LogoSpec::Style::named;
  out.isWest = isWest;
  return out;
}

std::optional<IptvOrgIndex::Saved> IptvOrgIndex::build(std::string_view channelsCsv,
                                                       std::string_view logosCsv,
                                                       time::Seconds now) {
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

  Saved saved;
  saved.fetched = now;
  size_t need = std::max({*idCol, *nameCol, *countryCol});
  for (size_t r = 1; r < channelRows.size(); ++r) {
    const std::vector<std::string>& row = channelRows[r];
    if (row.size() <= need) continue;
    if (row[*countryCol] != "US") continue;
    if (closedCol && *closedCol < row.size() && !row[*closedCol].empty()) continue;
    const std::string& id = row[*idCol];
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
  if (saved.byID.empty()) return std::nullopt;
  return saved;
}

std::optional<std::string> IptvOrgIndex::url(const std::optional<std::string>& epgID,
                                             std::string_view name) const {
  if (epgID) {
    auto hit = saved_.byID.find(str::lower(*epgID));
    if (hit != saved_.byID.end()) return hit->second;
  }
  std::string key = catalog::coreKey(name).key;
  if (key.empty()) return std::nullopt;
  auto hit = saved_.byName.find(key);
  if (hit == saved_.byName.end()) return std::nullopt;
  return hit->second;
}

std::string IptvOrgIndex::toJson(const Saved& saved) {
  json::Value v = json::Value::object();
  v.set("fetched", saved.fetched);
  v.set("byID", stringMap(saved.byID));
  v.set("byName", stringMap(saved.byName));
  return json::write(v);
}

std::optional<IptvOrgIndex::Saved> IptvOrgIndex::fromJson(std::string_view text) {
  auto parsed = json::parse(text);
  if (!parsed || !parsed->isObject() || !(*parsed)["fetched"].isNumber()) return std::nullopt;
  Saved saved;
  saved.fetched = (*parsed)["fetched"].asInt();
  for (const auto& [k, v] : (*parsed)["byID"].fields()) saved.byID[k] = v.asString();
  for (const auto& [k, v] : (*parsed)["byName"].fields()) saved.byName[k] = v.asString();
  return saved;
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
