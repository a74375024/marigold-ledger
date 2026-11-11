#include "core/logos/logos.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "core/catalog/catalog.hpp"
#include "core/catalog/reference.hpp"
#include "core/logos/table.hpp"
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

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLetter(char c) { return c >= 'a' && c <= 'z'; }

bool allDigits(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return isDigit(c); });
}

// Whether `word` (upper case) stands in `text` (upper case) as a word of
// its own: "TELEMUNDO" in "NBC (KSTS) TELEMUNDO", not "CBS" in "KCBS".
bool hasWord(std::string_view text, std::string_view word) {
  auto alnum = [](char c) { return (c >= 'A' && c <= 'Z') || isDigit(c); };
  for (size_t at = text.find(word); at != std::string_view::npos; at = text.find(word, at + 1)) {
    size_t end = at + word.size();
    if ((at == 0 || !alnum(text[at - 1])) && (end == text.size() || !alnum(text[end]))) return true;
  }
  return false;
}

// A channel name's words for the library's file names: lower case, accents
// folded, "&" as "and", "+" as "plus", without quality and feed words.
std::vector<std::string> logoSetTokens(std::string_view name) {
  static const std::unordered_set<std::string> kDrop = {"hd",  "fhd",  "uhd", "sd",     "4k", "8k",  "hevc",
                                                        "raw", "vip",  "60fps", "50fps", "1080p", "720p", "east",
                                                        "feed", "alt", "backup", "us",  "usa"};
  bool plain = std::all_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
  std::string lowered = plain ? str::lowerAscii(name) : str::lower(str::foldDiacritics(name));
  std::vector<std::string> tokens;
  std::string token;
  auto flush = [&] {
    if (!token.empty() && !kDrop.count(token)) tokens.push_back(token);
    token.clear();
  };
  for (char c : lowered) {
    if (isLetter(c) || isDigit(c)) {
      token += c;
    } else if (c == '\'') {
      // "Dr. Phil's" -> "phils"
    } else {
      flush();
      if (c == '&') tokens.emplace_back("and");
      if (c == '+') tokens.emplace_back("plus");
    }
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

std::string joined(const std::vector<std::string>& tokens, std::string_view sep) { return str::join(tokens, sep); }

// "MTV2" -> "mtv2", "mtv-2"; "LAW & CRIME" -> "law-and-crime", then with
// "-network" etc.: the file names the library might use for a channel name.
std::vector<std::string> logoSetKeys(const std::vector<std::string>& tokens) {
  std::vector<std::string> out;
  if (tokens.empty()) return out;
  auto add = [&out](std::string key) {
    if (!key.empty() && std::find(out.begin(), out.end(), key) == out.end()) out.push_back(std::move(key));
  };
  std::string base = joined(tokens, "-");
  add(base);
  if (tokens.size() > 1 && tokens.front() == "the")
    add(joined(std::vector<std::string>(tokens.begin() + 1, tokens.end()), "-"));
  // A number joined to a word: "mtv2" -> "mtv-2".
  std::string split;
  for (size_t i = 0; i < base.size(); ++i) {
    if (i > 0 && isDigit(base[i]) && isLetter(base[i - 1])) split += '-';
    split += base[i];
  }
  add(std::move(split));
  for (const char* word : {"channel", "network", "tv"}) {
    add(base + "-" + word);
    if (tokens.size() > 1 && tokens.back() == word)
      add(joined(std::vector<std::string>(tokens.begin(), tokens.end() - 1), "-"));
  }
  return out;
}

// A national file by any of `keys`, or (`joined`) by a key with its dashes
// run together ("byu-tv" for BYUtv, "me-tv" for MeTV).
std::optional<std::string> nationalFile(const std::vector<std::string>& keys, bool joined = true) {
  static const std::unordered_set<std::string_view> kNames(logoSetNames().begin(), logoSetNames().end());
  static const std::unordered_map<std::string, std::string_view> kJoined = [] {
    std::unordered_map<std::string, std::string_view> m;
    for (std::string_view n : logoSetNames()) m.emplace(str::replaceAll(n, "-", ""), n);
    return m;
  }();
  for (const std::string& key : keys) {
    if (kNames.count(key)) return key + "-us.png";
  }
  if (!joined) return std::nullopt;
  for (const std::string& key : keys) {
    auto it = kJoined.find(str::replaceAll(key, "-", ""));
    if (it != kJoined.end()) return std::string(it->second) + "-us.png";
  }
  return std::nullopt;
}

// Channels the library has under another name, by their words as
// logoSetTokens() gives them (space-separated): file names under kLogoBase.
// Old brands only where the channel is still that brand or the file shows
// its current one ("nbcsn-bay-area" is NBC Sports Bay Area's peacock).
std::optional<std::string> otherNameFile(const std::vector<std::string>& tokens) {
  static const std::unordered_map<std::string, std::string> kFiles = [] {
    std::unordered_map<std::string, std::string> m;
    for (auto [name, file] : std::initializer_list<std::pair<const char*, const char*>>{
             {"All Reality WE tv", "we-tv"},
             {"Starz Kids", "starz-kids-and-family"},
             {"StarzKids", "starz-kids-and-family"},
             {"TMC", "the-movie-channel"},
             {"TMC Xtra", "the-movie-channel-xtra"},
             {"TMC Extra", "the-movie-channel-xtra"},
             {"Family Movie Classics", "fmc-family-movie-classics"},
             {"FMC", "fmc-family-movie-classics"},
             {"IndiePlex", "plex-indieplex"},
             {"RetroPlex", "plex-retroplex"},
             {"MoviePlex", "plex-movieplex"},
             {"LiveNOW from FOX", "live-now-fox"},
             {"LiveNOW", "live-now-fox"},
             {"beIN Sports en Español", "bein-sports-espanol"},
             {"beIN Español", "bein-sports-espanol"},
             {"beIN Sports Xtra en Español", "bein-sports-xtra-espanol"},
             {"Jewelry Television", "jtv"},
             {"Jewelry TV", "jtv"},
             {"Sho x Bet", "sho-bet"},
             {"SHO×BET", "sho-bet"},
             {"Showtime x BET", "sho-bet"},
             {"Great Entertainment Television", "get-tv"},
             {"NBC Sports Bay Area", "nbcsn-bay-area"},
             {"NBC Sports Bay Area Plus", "nbcsn-bay-area"},
             {"NBCS Bay Area", "nbcsn-bay-area"},
             {"Sports Bay Area NSBA", "nbcsn-bay-area"},
             {"NBC Sports California", "nbcsn-california"},
             {"NBC Sports California Plus", "nbcsn-california"},
             {"NBCS California", "nbcsn-california"},
             {"Sports NBC California", "nbcsn-california"},
             {"NBC Sports Boston", "nbcsn-boston"},
             {"NBC Sports Philadelphia", "nbcsn-philadelphia"},
             {"NBC Sports Philadelphia Plus", "nbcsn-philadelphia"},
             {"SportsNet LA", "spectrum-sportsnet-la"},
             {"SportsNet LA Dodgers", "spectrum-sportsnet-la"},
             {"Spectrum SportsNet LA Dodgers", "spectrum-sportsnet-la"},
             {"SportsNet LA Lakers", "spectrum-sportsnet"},
             {"Spectrum SportsNet Lakers", "spectrum-sportsnet"},
             {"Bally Sports Prime Ticket", "bally-sports-socal"},
             {"Universo", "nbc-universo"},
             {"FOX Deportes", "fox-sports-deportes"},
         }) {
      m.emplace(joined(logoSetTokens(name), " "), std::string(file) + "-us.png");
    }
    return m;
  }();
  auto it = kFiles.find(joined(tokens, " "));
  if (it == kFiles.end()) return std::nullopt;
  return it->second;
}

// A free streaming channel's file in misc/vod, by its words run together
// exactly as the file's ("afvtv" is "afv-tv-vod.png").
std::optional<std::string> vodFile(const std::vector<std::string>& tokens) {
  static const std::unordered_map<std::string, std::string_view> kFiles = [] {
    std::unordered_map<std::string, std::string_view> m;
    for (std::string_view n : vodNames()) {
      std::string_view key = n;
      for (std::string_view end : {"-vod-us", "-vod"}) {
        if (str::endsWith(key, end)) {
          key.remove_suffix(end.size());
          break;
        }
      }
      m.emplace(str::replaceAll(key, "-", ""), n);
    }
    return m;
  }();
  if (tokens.empty()) return std::nullopt;
  auto it = kFiles.find(joined(tokens, ""));
  if (it == kFiles.end()) return std::nullopt;
  return "misc/vod/" + std::string(it->second) + ".png";
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

std::string logoSetUrl(const std::string& file) {
  if (str::startsWith(file, "misc/")) return std::string(kLibraryRoot) + file;
  return std::string(kLogoBase) + file;
}

std::string wideLogoSetUrl(const std::string& file) {
  static const std::unordered_set<std::string_view> kWide(horizontalNames().begin(), horizontalNames().end());
  constexpr std::string_view kEnd = "-us.png";
  if (str::endsWith(file, kEnd) && file.find('/') == std::string::npos) {
    std::string_view stem(file.data(), file.size() - kEnd.size());
    if (kWide.count(stem)) return std::string(kLogoBase) + std::string(stem) + "-hz-us.png";
  }
  return logoSetUrl(file);
}

std::optional<std::string> logoSetFile(std::string_view name) {
  std::vector<std::string> tokens = logoSetTokens(name);
  if (tokens.empty()) return std::nullopt;
  if (auto file = nationalFile(logoSetKeys(tokens))) return file;
  std::vector<std::string> plainer = plainerTokens(tokens);
  if (plainer != tokens) {
    if (auto file = nationalFile(logoSetKeys(plainer))) return file;
  }
  // Names the library spells otherwise, as the providers write them.
  if (auto file = otherNameFile(tokens)) return file;
  if (plainer != tokens) {
    if (auto file = otherNameFile(plainer)) return file;
  }
  // A free streaming channel's, by its exact name.
  if (auto file = vodFile(tokens)) return file;
  if (plainer != tokens) {
    if (auto file = vodFile(plainer)) return file;
  }
  // A numbered, extra or alternate feed: its parent's ("ESPN College
  // Extra 6" is ESPN College Extra's, "beIN Sports Connect 5" beIN
  // Sports'). Only a feed's number (up to 20: "The First 48" is a channel
  // of its own) or those words come off the end, and the parent's name
  // must be the file's exactly ("La 1" is not LATV).
  static const std::unordered_set<std::string> kFeedWords = {"alternate", "extra", "overflow", "connect"};
  std::vector<std::string> parent = tokens;
  while (parent.size() > 1) {
    const std::string& last = parent.back();
    bool number = allDigits(last) && last.size() <= 2 && std::atoi(last.c_str()) <= 20;
    if (!number && !kFeedWords.count(last)) break;
    parent.pop_back();
    if (auto file = nationalFile(logoSetKeys(parent), false)) return file;
    if (auto file = otherNameFile(parent)) return file;
  }
  return std::nullopt;
}

std::optional<std::string> formerBrandFile(std::string_view name) {
  // FanDuel Sports Network took over the Bally Sports networks in 2024;
  // the library still has them as Bally Sports.
  static const std::unordered_set<std::string_view> kNames(logoSetNames().begin(), logoSetNames().end());
  std::vector<std::string> tokens = logoSetTokens(name);
  if (tokens.size() < 3 || tokens[0] != "fanduel" || tokens[1] != "sports") return std::nullopt;
  size_t from = tokens[2] == "network" || tokens[2] == "net" ? 3 : 2;
  if (from >= tokens.size()) return std::nullopt;
  std::string file = "bally-sports-" + joined(std::vector<std::string>(tokens.begin() + static_cast<long>(from),
                                                                       tokens.end()),
                                              "-");
  if (!kNames.count(file)) return std::nullopt;
  return file + "-us.png";
}

std::optional<std::string> handPicked(std::string_view name) {
  // Free streaming channels no logo database has: each one's own logo as
  // the streaming services show it (made for dark backgrounds), from hosts
  // the app already loads logos from.
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
             std::pair<const char*, std::string>{"Always Funny",
                                                 pluto + "69eaa34817801c84fca05d94/colorLogoPNG_1777656411046.png"},
             std::pair<const char*, std::string>{"Always Funny Videos",
                                                 pluto + "69eaa34817801c84fca05d94/colorLogoPNG_1777656411046.png"},
             std::pair<const char*, std::string>{"Confess by Nosey",
                                                 pluto + "634ee8f95aa9870007248333/colorLogoPNG.png"},
             std::pair<const char*, std::string>{"ZenLIFE by Stingray",
                                                 pluto + "696998dfa2b623e8b20125a3/colorLogoPNG_1770231337178.png"},
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
  std::optional<std::string> fromName = catalog::networkWord(name);
  // Only a network the name could have said ("UNIMAS", "INDEPENDENT"),
  // never another affiliation's words ("Story Television").
  const catalog::reference::Station* station = reference.station(callsign);
  std::optional<std::string> fromReference = station ? catalog::networkWord(station->network) : std::nullopt;
  if (fromName && fromReference && *fromName != *fromReference && hasWord(str::upperAscii(name), *fromReference))
    return fromReference;
  return fromName ? fromName : fromReference;
}

std::string referenceIdOf(std::string_view rowId) {
  constexpr std::string_view kPrefix = "e|r:";
  if (rowId.substr(0, kPrefix.size()) != kPrefix) return std::string();
  std::string_view rest = rowId.substr(kPrefix.size());
  return std::string(rest.substr(0, rest.find('~')));
}

namespace {

// A station's name without its call sign, for the network the provider
// names beside it ("MeTV Plus (WZME) HD" -> "MeTV Plus  HD"): the sign's
// brackets and what they hold, or the sign as a word.
std::string withoutCallsign(std::string_view name, const std::string& sign) {
  std::string upper = str::upperAscii(name);
  std::string out(name);
  for (size_t at = upper.find(sign); at != std::string::npos; at = upper.find(sign, at + 1)) {
    size_t end = at + sign.size();
    auto alnum = [](char c) { return (c >= 'A' && c <= 'Z') || isDigit(c); };
    if ((at > 0 && alnum(upper[at - 1])) || (end < upper.size() && alnum(upper[end]))) continue;
    // Inside brackets: the whole bracket.
    size_t open = upper.rfind('(', at), close = upper.find(')', end);
    bool inside = open != std::string::npos && close != std::string::npos &&
                  upper.find_first_not_of(' ', open + 1) == at && upper.find_first_of("()", end) == close;
    size_t from = inside ? open : at, to = inside ? close + 1 : end;
    out.replace(from, to - from, std::string(to - from, ' '));
    upper.replace(from, to - from, std::string(to - from, ' '));
  }
  return out;
}

bool plainCallsign(const std::string& sign) {
  if (sign.size() < 2 || sign.size() > 8) return false;
  return std::all_of(sign.begin(), sign.end(), [](char c) { return (c >= 'A' && c <= 'Z') || isDigit(c); });
}

}  // namespace

LogoSpec spec(std::string_view name, const std::optional<std::string>& core, bool isWest,
              bool isStation, const std::optional<std::string>& callsign,
              std::optional<int> virtualNumber, const std::string& provider,
              const std::optional<std::string>& epgID,
              std::optional<catalog::LineupCategory> category, const IptvOrgIndex* iptvOrg,
              const std::string& directoryIptvOrg, const std::string& referenceId,
              const std::string& directoryLogo) {
  LogoSpec out;
  auto add = [&out](std::string link, const char* from) {
    out.urls.push_back(std::move(link));
    out.sources.push_back(from);
  };
  // A logo standing alone in the cell: the rest of the chain is the
  // provider's.
  auto alone = [&](std::string link, const char* from) {
    add(std::move(link), from);
    if (!provider.empty()) add(provider, "provider");
    out.style = LogoSpec::Style::logoOnly;
    out.isWest = isWest;
    return out;
  };
  if (category) out.subtitle = std::string(catalog::genreTitle(*category));

  if (isStation && callsign) {
    std::optional<std::string> network = stationNetwork(name, *callsign, catalog::reference::builtInIndex());
    // The network's own logo first (consistent and made for dark
    // backgrounds), then the station's, then the provider's.
    if (network) {
      auto file = networkLogos().find(*network);
      if (file != networkLogos().end()) add(logoSetUrl(file->second), "logo set");
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
    if (file) add(logoSetUrl(*file), "logo set");
    // Neither: the network the provider names beside the call sign, in
    // the same library ("MeTV Plus (WZME) HD").
    if (std::string sign = str::upper(*callsign); out.urls.empty() && plainCallsign(sign)) {
      if (auto brandFile = logoSetFile(withoutCallsign(name, sign))) add(logoSetUrl(*brandFile), "logo set");
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
    if (file != coreLogos().end()) return alone(wideLogoSetUrl(file->second), "logo set");
  }

  bool national = category != catalog::LineupCategory::local && category != catalog::LineupCategory::international;
  // The reference data's logo for a channel it knows.
  if (national) {
    // By the row's own reference id first: its name can be a provider's
    // ("BLOOMBERG TV+" for Bloomberg Television).
    const catalog::reference::Index& index = catalog::reference::builtInIndex();
    const catalog::reference::Channel* known = referenceId.empty() ? nullptr : index.channel(referenceId);
    if (!known) known = index.match(name).channel;
    // Its own file, else by its name or another name in the library ("TMC
    // Xtra" is "The Movie Channel Extra"); an overflow or alternate feed
    // with none of its own, its parent's ("Big Ten Network Alternate").
    auto fileOf = [](const catalog::reference::Channel* channel) -> std::optional<std::string> {
      if (!channel->logo.empty()) return channel->logo;
      for (size_t i = 0; i <= channel->aliases.size(); ++i) {
        if (auto file = logoSetFile(i == 0 ? channel->name : channel->aliases[i - 1])) return file;
      }
      return std::nullopt;
    };
    std::optional<std::string> file = known ? fileOf(known) : std::nullopt;
    if (known && !file && !known->parent.empty()) {
      if (const catalog::reference::Channel* parent = index.channel(known->parent)) file = fileOf(parent);
    }
    if (file) return alone(wideLogoSetUrl(*file), "logo set");
  }
  // A local with no call sign ("FOX 5 San Diego"): its network's logo.
  if (category == catalog::LineupCategory::local) {
    if (auto network = catalog::networkWord(name)) {
      auto file = networkLogos().find(*network);
      if (file != networkLogos().end()) add(logoSetUrl(file->second), "logo set");
    }
  }
  if (national) {
    // Marigold: a national channel the tables don't cover, by its name in the
    // same library ("MTV2", "Fox Weather", "Cinemax Hits"): shown like the
    // curated ones, the provider's logo behind it.
    if (auto file = logoSetFile(name)) return alone(wideLogoSetUrl(*file), "logo set");
    if (auto picked = handPicked(name)) return alone(*picked, "hand-picked");
  }
  // The provider's logo always comes last: it is usually the worst.
  if (iptvOrg) {
    if (auto outside = iptvOrg->url(epgID, name, directoryIptvOrg)) add(*outside, "public database");
  }
  if (!directoryLogo.empty()) add(directoryLogo, "Wikidata");
  if (national && out.urls.empty()) {
    if (auto former = formerBrandFile(name)) add(wideLogoSetUrl(*former), "former brand");
  }
  if (!provider.empty()) add(provider, "provider");
  // Marigold: these stand alone too when the one that loads is good
  // (fillsCard()); a local keeps its name beside its network's logo.
  bool shown = !out.urls.empty() && category != catalog::LineupCategory::local;
  out.style = shown ? LogoSpec::Style::logoOnly : LogoSpec::Style::named;
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

std::optional<int> logoQuality(std::string_view format, std::string_view url, std::string_view width,
                               std::string_view height, std::string_view tags, bool main) {
  std::string kind = str::upperAscii(format);
  std::string lower = str::lowerAscii(url);
  if (kind.empty()) {
    if (str::endsWith(lower, ".png")) kind = "PNG";
    else if (str::endsWith(lower, ".jpg") || str::endsWith(lower, ".jpeg")) kind = "JPEG";
  }
  if (kind == "JPG") kind = "JPEG";
  if ((kind != "PNG" && kind != "JPEG") || !str::startsWith(lower, "https://")) return std::nullopt;
  // A search engine's thumbnail.
  if (lower.find("encrypted-tbn") != std::string::npos) return std::nullopt;
  int w = std::atoi(std::string(width).c_str()), h = std::atoi(std::string(height).c_str());
  bool sized = w > 0 && h > 0;
  if (sized && kind == "JPEG" && std::max(w, h) < 300) return std::nullopt;
  int score = 0;
  // A shape fillsCard() turns down whatever is in it: only next to the
  // name, and below any other.
  if (sized && (std::max(w, h) < 48 || h > 2 * w || w > 10 * h)) score -= 20;
  if (kind == "PNG") score += 3;
  if (main) score += 4;
  if (sized) {
    double aspect = static_cast<double>(w) / h;
    if (aspect >= 1.3 && aspect <= 5) score += 2;
    if (std::max(w, h) >= 200) score += 1;
  }
  if (str::icontains(tags, "white") || str::icontains(tags, "transparent")) score += 1;
  return score;
}

namespace {

// Each iptv-org channel's best logo (logoQuality()), by its id as written.
std::unordered_map<std::string, std::string> bestLogos(const std::vector<std::vector<std::string>>& logoRows,
                                                       const std::vector<std::string>* prefixes) {
  std::unordered_map<std::string, std::string> out;
  if (logoRows.empty()) return out;
  const std::vector<std::string>& head = logoRows.front();
  auto channelCol = column(head, "channel"), urlCol = column(head, "url");
  if (!channelCol || !urlCol) return out;
  auto feedCol = column(head, "feed"), formatCol = column(head, "format"), inUseCol = column(head, "in_use"),
       widthCol = column(head, "width"), heightCol = column(head, "height"), tagsCol = column(head, "tags");
  auto cell = [](const std::vector<std::string>& row, const std::optional<size_t>& col) -> std::string_view {
    return col && *col < row.size() ? std::string_view(row[*col]) : std::string_view();
  };
  std::unordered_map<std::string, int> scoreOf;
  for (size_t r = 1; r < logoRows.size(); ++r) {
    const std::vector<std::string>& row = logoRows[r];
    if (row.size() <= std::max(*channelCol, *urlCol)) continue;
    if (str::upperAscii(cell(row, inUseCol)) == "FALSE") continue;
    const std::string& link = row[*urlCol];
    if (prefixes && std::none_of(prefixes->begin(), prefixes->end(),
                                 [&](const std::string& p) { return str::startsWith(link, p); }))
      continue;
    std::optional<int> score = logoQuality(cell(row, formatCol), link, cell(row, widthCol), cell(row, heightCol),
                                           cell(row, tagsCol), cell(row, feedCol).empty());
    // Other countries' only when it can stand alone in the cell.
    if (!score || (prefixes && *score < 0)) continue;
    const std::string& id = row[*channelCol];
    auto [it, fresh] = scoreOf.try_emplace(id, *score);
    if (!fresh && it->second >= *score) continue;  // the first of equals
    it->second = *score;
    out[id] = link;
  }
  return out;
}

}  // namespace

std::optional<IptvOrgIndex::Saved> IptvOrgIndex::build(std::string_view channelsCsv,
                                                       std::string_view logosCsv, std::string_view feedsCsv) {
  std::vector<std::vector<std::string>> channelRows = csvRows(channelsCsv);
  std::vector<std::vector<std::string>> logoRows = csvRows(logosCsv);
  if (channelRows.empty() || logoRows.empty()) return std::nullopt;
  const std::vector<std::string>& channelHead = channelRows.front();
  const std::vector<std::string>& logoHead = logoRows.front();
  auto idCol = column(channelHead, "id"), nameCol = column(channelHead, "name"),
       countryCol = column(channelHead, "country");
  if (!idCol || !nameCol || !countryCol || !column(logoHead, "channel") || !column(logoHead, "url"))
    return std::nullopt;
  auto altCol = column(channelHead, "alt_names");
  auto closedCol = column(channelHead, "closed");
  std::unordered_map<std::string, std::string> logoOf = bestLogos(logoRows, nullptr);

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
      saved.byID[str::lower(id)] = logo->second;
      std::vector<std::string> names{row[*nameCol]};
      if (altCol && *altCol < row.size() && !row[*altCol].empty()) {
        for (std::string_view alt : str::split(row[*altCol], ';')) names.emplace_back(alt);
      }
      for (const std::string& n : names) {
        std::string key = catalog::coreKey(n).key;
        if (!key.empty()) saved.byName.try_emplace(key, logo->second);
      }
    }
    if (us && saved.byID.empty()) return std::nullopt;
  }
  return saved;
}

const std::vector<std::string>& IptvOrgIndex::abroadHosts() {
  // Hosts the US index's logos come from already: the TV calls no other.
  static const std::vector<std::string> kHosts = {
      "https://i.imgur.com/",
      "https://upload.wikimedia.org/wikipedia/commons/thumb/",
      "https://upload.wikimedia.org/wikipedia/commons/",
      "https://upload.wikimedia.org/wikipedia/",
      "https://images.pluto.tv/channels/",
      "https://images.pluto.tv/",
      "https://jiotvimages.cdn.jio.com/dare_images/images/",
      "https://jiotvimages.cdn.jio.com/",
      "https://i.ibb.co/",
      "https://static.wikia.nocookie.net/",
      "https://i.postimg.cc/",
      "https://provider-static.plex.tv/epg/cms/production/",
      "https://provider-static.plex.tv/",
      "https://xstreamcp-assets-msp.streamready.in/",
      "https://thumb.wikimedia.org/wikipedia/commons/thumb/",
      "https://yt3.googleusercontent.com/",
      "https://images.fubo.tv/",
      "https://raw.githubusercontent.com/",
  };
  return kHosts;
}

IptvOrgIndex::Abroad IptvOrgIndex::buildAbroad(std::string_view channelsCsv, std::string_view logosCsv,
                                               const Saved& known) {
  Abroad out;
  std::vector<std::vector<std::string>> channelRows = csvRows(channelsCsv);
  if (channelRows.empty()) return out;
  const std::vector<std::string>& head = channelRows.front();
  auto idCol = column(head, "id");
  if (!idCol) return out;
  auto closedCol = column(head, "closed"), nsfwCol = column(head, "is_nsfw");
  const std::vector<std::string>& hosts = abroadHosts();
  std::unordered_map<std::string, std::string> logoOf = bestLogos(csvRows(logosCsv), &hosts);
  std::vector<std::pair<std::string, std::string>> lines;  // id, link
  for (size_t r = 1; r < channelRows.size(); ++r) {
    const std::vector<std::string>& row = channelRows[r];
    if (row.size() <= *idCol) continue;
    if (closedCol && *closedCol < row.size() && !row[*closedCol].empty()) continue;
    if (nsfwCol && *nsfwCol < row.size() && str::upperAscii(row[*nsfwCol]) == "TRUE") continue;
    std::string id = str::lower(row[*idCol]);
    // Words: neither may hold a space (Abroad's separator).
    if (id.empty() || id.find_first_of(" \t\n") != std::string::npos || known.byID.count(id)) continue;
    auto logo = logoOf.find(row[*idCol]);
    if (logo == logoOf.end() || logo->second.find_first_of(" \t\n") != std::string::npos) continue;
    lines.emplace_back(std::move(id), logo->second);
  }
  std::sort(lines.begin(), lines.end());
  lines.erase(std::unique(lines.begin(), lines.end(),
                          [](const auto& a, const auto& b) { return a.first == b.first; }),
              lines.end());
  // The beginnings in use (abroadHosts() lists a longer one before one
  // it begins with).
  std::vector<size_t> used(hosts.size(), 0);
  for (const auto& [id, link] : lines) {
    for (size_t i = 0; i < hosts.size(); ++i) {
      if (str::startsWith(link, hosts[i])) {
        ++used[i];
        break;
      }
    }
  }
  std::vector<size_t> code(hosts.size(), 0);
  for (size_t i = 0; i < hosts.size(); ++i) {
    if (!used[i]) continue;
    code[i] = out.prefixes.size();
    out.prefixes.push_back(hosts[i]);
  }
  constexpr std::string_view kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";
  for (const auto& [id, link] : lines) {
    for (size_t i = 0; i < hosts.size(); ++i) {
      if (!str::startsWith(link, hosts[i]) || code[i] >= kDigits.size()) continue;
      if (!out.words.empty()) out.words += ' ';
      out.words += id;
      out.words += ' ';
      out.words += kDigits[code[i]];
      out.words.append(link, hosts[i].size(), std::string::npos);
      break;
    }
  }
  return out;
}

IptvOrgIndex::IptvOrgIndex(Saved saved) : saved_(std::move(saved)) {
  std::string_view text = saved_.abroadWords;
  if (text.empty() || text.size() >= UINT32_MAX) return;
  size_t ids = (static_cast<size_t>(std::count(text.begin(), text.end(), ' ')) + 2) / 2;
  size_t size = 16;
  while (size < ids + ids / 2) size *= 2;  // at most two thirds full
  slots_.assign(size, 0);
  // Id, link, id, link...
  for (size_t at = 0; at < text.size();) {
    size_t idEnd = std::min(text.find(' ', at), text.size());
    size_t linkEnd = idEnd < text.size() ? std::min(text.find(' ', idEnd + 1), text.size()) : text.size();
    if (idEnd > at && linkEnd > idEnd + 2) {
      size_t slot = str::fnv1a64(text.substr(at, idEnd - at)) & (size - 1);
      while (slots_[slot]) slot = (slot + 1) & (size - 1);
      slots_[slot] = static_cast<uint32_t>(at + 1);
      ++abroadCount_;
    }
    at = linkEnd + 1;
  }
}

std::optional<std::string> IptvOrgIndex::abroad(std::string_view id) const {
  if (slots_.empty() || id.empty()) return std::nullopt;
  std::string key = str::lowerAscii(id);
  std::string_view text = saved_.abroadWords;
  size_t mask = slots_.size() - 1;
  for (size_t slot = str::fnv1a64(key) & mask; slots_[slot]; slot = (slot + 1) & mask) {
    size_t at = slots_[slot] - 1;
    if (text.compare(at, key.size(), key) != 0 || at + key.size() + 2 >= text.size() || text[at + key.size()] != ' ')
      continue;
    size_t from = at + key.size() + 1;
    size_t end = std::min(text.find(' ', from), text.size());
    char c = text[from];
    size_t index = isDigit(c) ? static_cast<size_t>(c - '0') : isLetter(c) ? static_cast<size_t>(c - 'a' + 10) : 99;
    if (index >= saved_.abroadPrefixes.size()) return std::nullopt;
    return saved_.abroadPrefixes[index] + std::string(text.substr(from + 1, end - from - 1));
  }
  return std::nullopt;
}

std::optional<std::string> IptvOrgIndex::url(const std::optional<std::string>& epgID, std::string_view name,
                                             const std::string& directoryIptvOrg) const {
  for (const std::optional<std::string>& id : {epgID, std::optional<std::string>(directoryIptvOrg)}) {
    if (!id || id->empty()) continue;
    auto hit = saved_.byID.find(str::lower(*id));
    if (hit != saved_.byID.end()) return hit->second;
  }
  std::string key = catalog::coreKey(name).key;
  if (!key.empty()) {
    if (auto hit = saved_.byName.find(key); hit != saved_.byName.end()) return hit->second;
    // Providers' names for channels the database spells otherwise.
    static const std::unordered_map<std::string, std::string> kSpelledOtherwise = [] {
      std::unordered_map<std::string, std::string> m;
      for (auto [provider, id] : {std::pair{"EarthX", "earthxtv.us"}, std::pair{"FlixFling Now", "flixfling.us"},
                                  std::pair{"The Victory Channel", "victorychannel.us"},
                                  std::pair{"Fox Sports Racing", "foxsportsracing.us"},
                                  std::pair{"Al Jazeera English", "aljazeera.qa"},
                                  std::pair{"Euronews", "euronewsenglish.fr"},
                                  // US feeds of other countries' channels.
                                  std::pair{"Antena 3 Internacional", "antena3internacional.es"},
                                  std::pair{"Cinema Dinamita", "cinemadinamita.mx"},
                                  std::pair{"Mexicanal", "mexicanal.mx"},
                                  std::pair{"Video Rola", "videorola.mx"},
                                  std::pair{"WAPA América", "wapatv41.pr"}}) {
        m.emplace(catalog::coreKey(provider).key, id);
      }
      return m;
    }();
    if (auto other = kSpelledOtherwise.find(key); other != kSpelledOtherwise.end()) {
      if (auto byId = saved_.byID.find(other->second); byId != saved_.byID.end()) return byId->second;
      if (auto found = abroad(other->second)) return found;
    }
    // The same name as the database more likely writes it: without a
    // country mark left in front ("CA Fox Sports Racing"), "TV" for
    // "Television" ("Jewelry TV"), with or without a last "TV" or "Channel"
    // ("TheGrio TV", "Victory Channel").
    std::string bare = key;
    for (std::string_view mark : {"ca ", "uk ", "us ", "usa "}) {
      if (str::startsWith(bare, mark) && bare.size() > mark.size()) {
        bare.erase(0, mark.size());
        break;
      }
    }
    std::string tv;
    for (std::string_view word : str::split(bare, ' ')) {
      if (!tv.empty()) tv += ' ';
      tv += word == "television" ? std::string_view("tv") : word;
    }
    std::string plain = tv;
    for (std::string_view end : {" tv", " channel", " network"}) {
      if (str::endsWith(plain, end) && plain.size() > end.size()) {
        plain.resize(plain.size() - end.size());
        break;
      }
    }
    for (const std::string& k : {bare, tv, plain, plain + " tv", plain + " channel"}) {
      if (k == key) continue;
      if (auto found = saved_.byName.find(k); found != saved_.byName.end()) return found->second;
    }
  }
  // Another country's channel, by its guide id only.
  if (epgID) {
    if (auto found = abroad(*epgID)) return found;
  }
  if (!directoryIptvOrg.empty()) return abroad(directoryIptvOrg);
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
