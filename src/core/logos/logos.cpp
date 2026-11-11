#include "core/logos/logos.hpp"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "core/catalog/catalog.hpp"
#include "core/catalog/reference.hpp"
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

// A channel name's words for the library's file names: lower case, "&"
// as "and", "+" as "plus", without quality and feed words.
std::vector<std::string> logoSetTokens(std::string_view name) {
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
  return tokens;
}

// The same words as the library more likely writes them, asked only when
// the name as written has no file: "Xtra" for "Extra" ("The Movie Channel
// Extra"), without a coast ("Starz Kids West": a West feed shows its
// network's logo), a language feed's last word ("YES Network Latin") or a
// country mark the provider left in front ("CA Fox Sports Racing").
std::vector<std::string> plainerTokens(std::vector<std::string> tokens) {
  for (std::string& t : tokens) {
    if (t == "extra") t = "xtra";
  }
  tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                              [](const std::string& t) { return t == "west" || t == "pacific"; }),
               tokens.end());
  if (tokens.size() > 1 && (tokens.back() == "latin" || tokens.back() == "espanol" || tokens.back() == "spanish"))
    tokens.pop_back();
  if (tokens.size() > 1 && (tokens.front() == "ca" || tokens.front() == "uk")) tokens.erase(tokens.begin());
  return tokens;
}

// "MTV2" -> "mtv2", "mtv-2"; "LAW & CRIME" -> "law-and-crime", then with
// "-network" etc.: the file names the library might use for a channel name.
std::vector<std::string> logoSetKeys(const std::vector<std::string>& tokens) {
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
  // The library splits some names the channels write as one word
  // ("byu-tv" for BYUtv, "me-tv" for MeTV): its names without dashes.
  static const std::unordered_map<std::string, std::string_view> kJoined = [] {
    std::unordered_map<std::string, std::string_view> m;
    for (std::string_view n : logoSetNames()) m.emplace(str::replaceAll(std::string(n), "-", ""), n);
    return m;
  }();
  auto lookup = [](const std::vector<std::string>& keys) -> std::optional<std::string> {
    for (const std::string& key : keys) {
      if (kNames.count(key)) return key + "-us.png";
    }
    for (const std::string& key : keys) {
      auto it = kJoined.find(str::replaceAll(key, "-", ""));
      if (it != kJoined.end()) return std::string(it->second) + "-us.png";
    }
    return std::nullopt;
  };
  std::vector<std::string> tokens = logoSetTokens(name);
  if (auto file = lookup(logoSetKeys(tokens))) return file;
  std::vector<std::string> plainer = plainerTokens(tokens);
  if (plainer != tokens) {
    if (auto file = lookup(logoSetKeys(plainer))) return file;
  }
  // Names the library spells otherwise, as the providers write them.
  static const std::unordered_map<std::string, std::string> kSpelledOtherwise = [] {
    std::unordered_map<std::string, std::string> m;
    for (auto [provider, file] :
         {std::pair{"All Reality WE tv", "we-tv"}, std::pair{"Starz Kids", "starz-kids-and-family"},
          std::pair{"StarzKids", "starz-kids-and-family"}, std::pair{"TMC", "the-movie-channel"},
          std::pair{"TMC Xtra", "the-movie-channel-xtra"}, std::pair{"TMC Extra", "the-movie-channel-xtra"}}) {
      m.emplace(catalog::coreKey(provider).key, std::string(file) + "-us.png");
    }
    return m;
  }();
  std::string key = catalog::coreKey(name).key;
  if (auto other = kSpelledOtherwise.find(key); !key.empty() && other != kSpelledOtherwise.end()) return other->second;
  return std::nullopt;
}

std::optional<std::string> handPicked(std::string_view name) {
  // Free streaming channels no logo database has: each one's own logo as
  // the streaming services show it (made for dark backgrounds). A channel
  // with a parent network's look borrows the parent's from the library.
  static const std::unordered_map<std::string, std::string> kPicked = [] {
    const std::string pluto = "https://images.pluto.tv/channels/";
    const std::string xumo = "https://image.xumo.com/v1/channels/channel/";
    const std::string onBlack = "/600x450.png?type=color_onBlack";
    std::unordered_map<std::string, std::string> m;
    for (auto [channel, link] : {
             std::pair<const char*, std::string>{"Mayday: Air Disaster",
                                                 pluto + "64d2b6bf9b414d0008187d83/colorLogoPNG_1780955414910.png"},
             std::pair<const char*, std::string>{"Big A Anime",
                                                 pluto + "6a3c5fa46276c42ac6726b27/colorLogoPNG_1785178474278.png"},
             std::pair<const char*, std::string>{"Torque by History",
                                                 pluto + "6aab1ef220ec28a21adca712/colorLogoPNG_1791210850118.png"},
             std::pair<const char*, std::string>{"Torque",
                                                 pluto + "6aab1ef220ec28a21adca712/colorLogoPNG_1791210850118.png"},
             std::pair<const char*, std::string>{"Bowling TV", xumo + "99951510" + onBlack},
             std::pair<const char*, std::string>{"Reelz Famous and Infamous", xumo + "99991368" + onBlack},
             std::pair<const char*, std::string>{
                 "Stingray Spa",
                 "https://tvpnlogopus.samsungcloud.tv/platform/image/sourcelogo/vc/00/02/34/"
                 "USBD3500013DG_20250630T232449SQUARE.png"},
             std::pair<const char*, std::string>{"NBC Sports California Plus",
                                                 std::string(kLogoBase) + "nbcsn-california-us.png"},
             std::pair<const char*, std::string>{"NBC Sports California Plus 2",
                                                 std::string(kLogoBase) + "nbcsn-california-us.png"},
         }) {
      m.emplace(catalog::coreKey(channel).key, std::move(link));
    }
    return m;
  }();
  std::string key = catalog::coreKey(name).key;
  if (key.empty()) return std::nullopt;
  auto hit = kPicked.find(key);
  if (hit == kPicked.end()) return std::nullopt;
  return hit->second;
}

std::optional<std::string> stationNetwork(std::string_view name, const std::string& callsign,
                                          const catalog::reference::Index& reference) {
  if (auto network = catalog::networkWord(name)) return network;
  // Only a network the name could have said ("UNIMAS", "INDEPENDENT"),
  // never another affiliation's words ("Story Television").
  if (const catalog::reference::Station* station = reference.station(callsign)) {
    return catalog::networkWord(station->network);
  }
  return std::nullopt;
}

std::string referenceIdOf(std::string_view rowId) {
  constexpr std::string_view kPrefix = "e|r:";
  if (rowId.substr(0, kPrefix.size()) != kPrefix) return std::string();
  std::string_view rest = rowId.substr(kPrefix.size());
  return std::string(rest.substr(0, rest.find('~')));
}

LogoSpec spec(std::string_view name, const std::optional<std::string>& core, bool isWest,
              bool isLocal, const std::optional<std::string>& callsign,
              std::optional<int> virtualNumber, const std::string& provider,
              const std::optional<std::string>& epgID,
              std::optional<catalog::LineupCategory> category, const IptvOrgIndex* iptvOrg,
              const std::string& directoryIptvOrg, const std::string& referenceId) {
  auto url = [](const std::string& file) { return std::string(kLogoBase) + file; };
  LogoSpec out;
  auto add = [&out](std::string link, const char* from) {
    out.urls.push_back(std::move(link));
    out.sources.push_back(from);
  };
  if (category) out.subtitle = std::string(catalog::genreTitle(*category));

  if (isLocal && callsign) {
    std::optional<std::string> network = stationNetwork(name, *callsign, catalog::reference::builtInIndex());
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
    // Neither: the network the provider names beside the call sign, in
    // the same library ("MeTV Plus (WZME) HD").
    if (std::string sign = str::upper(*callsign); out.urls.empty() && rx::test("^[A-Z0-9]{2,8}$", sign)) {
      std::string brand = rx::replace(std::string(name), "\\(\\s*" + sign + "\\b[^()]*\\)|\\b" + sign + "\\b", " ", true);
      if (auto brandFile = logoSetFile(brand)) add(url(*brandFile), "logo set");
    }
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

  // The reference data's logo for a channel it knows.
  if (category != catalog::LineupCategory::local && category != catalog::LineupCategory::international) {
    // By the row's own reference id first: its name can be a provider's
    // ("BLOOMBERG TV+" for Bloomberg Television).
    const catalog::reference::Index& index = catalog::reference::builtInIndex();
    const catalog::reference::Channel* known = referenceId.empty() ? nullptr : index.channel(referenceId);
    if (!known) known = index.match(name).channel;
    std::optional<std::string> file;
    if (known && !known->logo.empty()) file = known->logo;
    // None of its own: by its name or another name in the library ("TMC
    // Xtra" is "The Movie Channel Extra").
    for (size_t i = 0; known && !file && i <= known->aliases.size(); ++i) {
      file = logoSetFile(i == 0 ? known->name : known->aliases[i - 1]);
    }
    if (file) {
      add(url(*file), "logo set");
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
  if (category != catalog::LineupCategory::local && category != catalog::LineupCategory::international) {
    if (auto picked = handPicked(name)) {
      add(*picked, "hand-picked");
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
  // Marigold: these stand alone too when the one that loads is good
  // (fillsCard()); a local keeps its name beside its network's logo.
  bool alone = !out.urls.empty() && category != catalog::LineupCategory::local;
  out.style = alone ? LogoSpec::Style::logoOnly : LogoSpec::Style::named;
  out.isWest = isWest;
  return out;
}

bool fillsCard(const px::Shape& shape) {
  // The guide cell's logo box, and how far a logo may be blown up in it.
  constexpr double kCardW = 154, kCardH = 60, kMaxUpscale = 2;
  constexpr int kMinSide = 48;
  if (shape.w <= 0 || shape.h <= 0 || shape.plain) return false;
  if (std::max(shape.w, shape.h) < kMinSide) return false;  // a favicon-sized speck
  if (shape.h > 2 * shape.w) return false;                   // a sliver in a wide card
  if (shape.w > 10 * shape.h) return false;                  // a thread across it
  double scale = std::min(kCardW / shape.w, kCardH / shape.h);
  return scale <= kMaxUpscale;
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
                                std::pair{"Al Jazeera English", "aljazeera.qa"},
                                std::pair{"Euronews", "euronewsenglish.fr"}}) {
      m.emplace(catalog::coreKey(provider).key, id);
    }
    return m;
  }();
  if (auto other = kSpelledOtherwise.find(key); other != kSpelledOtherwise.end()) {
    if (auto byId = saved_.byID.find(other->second); byId != saved_.byID.end()) return byId->second;
  }
  // The same name as the database more likely writes it: without a
  // country mark left in front ("CA Fox Sports Racing"), "TV" for
  // "Television" ("Jewelry TV"), with or without a last "TV" or "Channel"
  // ("TheGrio TV", "Victory Channel").
  std::string bare = rx::replace(key, "^(ca|uk|us|usa) (?=\\S)", "");
  std::string tv = rx::replace(bare, "\\btelevision\\b", "tv");
  std::string plain = rx::replace(tv, " (tv|channel|network)$", "");
  for (const std::string& k : {bare, tv, plain, plain + " tv", plain + " channel"}) {
    if (k == key) continue;
    if (auto found = saved_.byName.find(k); found != saved_.byName.end()) return found->second;
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
