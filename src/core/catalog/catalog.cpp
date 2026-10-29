#include "core/catalog/catalog.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

#include "core/catalog/directory.hpp"
#include "core/catalog/lineup.hpp"
#include "core/catalog/text.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::catalog {
namespace {

using text::contains;

// Prefilters: many patterns below need a character every one of their
// matches contains (a ':' or '(' literal, a digit). Checking for it first
// skips the regex on most names and gives the same results.
bool hasAnyOf(std::string_view s, const char* chars) {
  return s.find_first_of(chars) != std::string_view::npos;
}
// \d also matches non-ASCII digits (Unicode mode), so any non-ASCII byte
// counts as a possible digit.
bool mayHaveDigit(std::string_view s) {
  for (char ch : s) {
    unsigned char c = static_cast<unsigned char>(ch);
    if ((c >= '0' && c <= '9') || c >= 0x80) return true;
  }
  return false;
}
bool hasNonAscii(std::string_view s) {
  for (char ch : s) {
    if (static_cast<unsigned char>(ch) >= 0x80) return true;
  }
  return false;
}

const std::pair<const char*, int> kQualityTokens[] = {
    {"\\buhd\\b", 3}, {"\\b4k\\b", 3},  {"\\bfhd\\b", 2}, {"\\b1080\\b", 2},
    {"\\bhd\\b", 1},  {"\\b720\\b", 1}, {"\\bsd\\b", 0}};

// Country from the provider's group prefix ("US| ...", Xtream), the group's
// name, or a name prefix ("(US)", "MEX:", "UK |").
const std::unordered_set<std::string>& countryCodes() {
  static const std::unordered_set<std::string> kCodes = {
      "US", "UK", "CA", "MX", "AR", "ES", "FR", "DE", "IT", "NL", "SE", "NO", "DK", "FI", "PL",
      "PT", "BR", "TR", "AL", "IR", "IL", "GR", "CZ", "SK", "HU", "RO", "BG", "HR", "RS", "BA",
      "MK", "ME", "AT", "CH", "BE", "IE", "AU", "NZ", "PH", "ID", "TH", "IN", "PK", "RU", "UA",
      "MA", "AE", "SA", "EG", "CO", "VE", "PE", "CL", "EC", "DO", "PR", "VN", "JP", "KR", "CN",
      "HK", "TW", "MY", "SG", "LB", "QA", "KW", "TN", "DZ", "NG", "ZA", "KE", "GH", "HN", "GT",
      "SV", "NI", "CR", "PA", "UY", "PY", "BO", "CU", "JM", "LT", "LV", "EE", "SI", "CY", "IS",
      "LU", "MT", "AM", "GE", "AZ", "KZ", "UZ", "AF", "BD", "LK", "NP"};
  return kCodes;
}

const std::unordered_map<std::string, std::string>& threeLetterCodes() {
  static const std::unordered_map<std::string, std::string> kCodes = {
      {"USA", "US"}, {"MEX", "MX"}, {"CAN", "CA"}, {"GBR", "UK"}, {"ARG", "AR"}, {"ESP", "ES"},
      {"FRA", "FR"}, {"DEU", "DE"}, {"GER", "DE"}, {"ITA", "IT"}, {"BRA", "BR"}, {"POR", "PT"}};
  return kCodes;
}

std::optional<std::string> countryFor(std::string_view name, std::string_view lower,
                                      std::string_view rawGroup, std::string_view group,
                                      bool xtream) {
  if (xtream && hasAnyOf(rawGroup, "|")) {
    if (auto code = text::firstGroup("^\\s*([A-Z0-9]{2,5})\\s*\\|", rawGroup, 1)) {
      if (!code->empty() && (*code)[0] >= '0' && (*code)[0] <= '9') return std::nullopt;  // "4K|"
      // Marigold: "USA|", "MEX|", "GBR|" are the same countries as "US|"
      // (tvOS kept "USA" as its own code, so those channels never counted
      // as US: no core networks, no locals, and no "US" in the country list).
      auto three = threeLetterCodes().find(*code);
      if (three != threeLetterCodes().end()) return three->second;
      return *code == "GB" ? std::string("UK") : *code;
    }
  }
  static const std::pair<const char*, const char*> kGroupMap[] = {
      {"mexico", "MX"},   {"vietnam", "VN"},        {"arabic", "AR"},     {"indonesia", "ID"},
      {"thailand", "TH"}, {"united kingdom", "UK"}, {"canada", "CA"},     {"india", "IN"},
      {"brazil", "BR"},   {"spain", "ES"},          {"france", "FR"},     {"germany", "DE"},
      {"italy", "IT"},    {"portugal", "PT"},       {"philippines", "PH"}};
  for (const auto& [word, code] : kGroupMap) {
    if (contains(group, word)) return std::string(code);
  }
  if (str::startsWith(lower, "(pluto ")) {
    if (auto match = text::firstMatch("^\\(pluto ([a-z]+)\\)", lower)) {
      std::string place = str::replaceAll(str::replaceAll(*match, "(pluto ", ""), ")", "");
      return place == "usa" || place == "us" ? std::string("US") : str::upper(place);
    }
  }
  struct NamePattern {
    const char* pattern;
    const char* needs;  // one of these characters must be in the name
  };
  static const NamePattern kNamePatterns[] = {{"^\\s*\\(([A-Za-z]{2,3})\\)", "("},
                                              {"^\\s*([A-Z]{2,3})\\s*[:|]", ":|"},
                                              {"^\\s*(USA?)\\s", "U"}};
  for (const NamePattern& np : kNamePatterns) {
    if (!hasAnyOf(name, np.needs)) continue;
    auto found = text::firstGroup(np.pattern, name, 1);
    if (!found) continue;
    std::string code = str::upper(*found);
    auto three = threeLetterCodes().find(code);
    if (three != threeLetterCodes().end()) code = three->second;
    if (code == "VT") return std::string("VN");
    if (code == "GB") return std::string("UK");
    if (countryCodes().count(code)) return code;
  }
  if (str::startsWith(group, "us") || contains(group, "usa") || contains(group, "united states")) {
    return std::string("US");
  }
  return std::nullopt;
}

// Names in Latin script (accents, dashes and superscripts allowed): not
// Cyrillic, Greek, Hebrew, Arabic, Indic, Thai or CJK.
bool latinScript(std::string_view name) {
  for (char ch : name) {
    unsigned char c = static_cast<unsigned char>(ch);
    if ((c >= 0xCD && c <= 0xDF) || c == 0xE0 || (c >= 0xE3 && c <= 0xEF)) return false;
  }
  return true;
}

// US channels in Spanish (Univision, Telemundo, ESPN Deportes...).
bool isSpanishUS(std::string_view name, std::string_view group) {
  return rx::test(
             "\\b(univision|unimas|unimás|telemundo|galavision|galavisión|estrella|azteca|telehit|"
             "cine ?latino|deportes|tudn|vix|discovery familia|pasiones|de pel[ií]cula|sorpresa|"
             "latino|espa[nñ]ola?|noticias|universo|nuestra tele|wapa|tr3s|bandamax|ritmoson|cnn "
             "en espa)",
             name, true) ||
         rx::test("latino|telemundo|univision|spanish|espa[nñ]ol|hispan", group);
}

// US-carried channels in another language: the market they belong to, from
// a language tag after the country ("US (FR) France 24", "US (Spanish)
// beIN SPORTS en Español") or the channel's own name ("Zee TV", "ARY
// Digital"). nullopt: US English. ("(SP)" is a provider's source tag, not
// Spanish.)
std::optional<std::string> otherLanguageMarket(std::string_view name) {
  if (hasAnyOf(name, "(")) {
    if (rx::test("\\((fr|french|fran[cç]ais)\\)", name, true)) return std::string("FR");
    if (rx::test("\\((spanish|espa[nñ]ol)\\)", name, true)) return std::string("US-ES");
  }
  struct Language {
    const char* market;
    const char* pattern;
  };
  static const Language kLanguages[] = {
      {"US-ES",
       "\\b(antena 3|mexicanal|cinema dinamita|cine mexicano|sony cine|nat geo mundo|ntn ?24|n\\+ ?foro|"
       "tc internacional|telecentro|video ?rola|mega tv|hola!? tv|gol ?tv|pantaya|caracol|ecuavisa|"
       "multimedios|imagen tv|telefe|tv chile|canal sur|hitn|en espa)"},
      {"IN",
       "\\b(aastha|sanskar|zee (tv|cinema|bollywood|anmol|punjabi|smile)|sony (kal|sab|set|max)|"
       "star (plus|bharat|gold|india)|colors (tv|cineplex|rishtey)|b4u|tv asia|aaj tak|ndtv)\\b"},
      {"PK",
       "\\b(ary (digital|news|zindagi|musik|qtv)|geo (tv|news|entertainment|kahani)|"
       "hum (tv|masala|sitaray|news)|ptv (home|news|global)|express news)\\b"},
      {"AE", "\\b(sky news arabia|al arabiya)\\b"},
      {"UK", "\\b(nick jr\\.? ?too|tnt sports box office|bbc (one|two|three|four))\\b"},
      {"FR", "\\btv5 ?monde\\b"},
      {"IT", "\\brai italia\\b"},
      {"PL", "\\b(tv polonia|polsat)\\b"},
      {"PT", "\\brtp (internacional|africa)\\b"},
      {"BR", "\\b(record|globo|band) internacional\\b"},
      {"KR", "\\bkbs world\\b"},
      {"CN", "\\bcctv\\b"},
      {"PH", "\\b(tfc|gma (pinoy|life))\\b"},
  };
  for (const Language& l : kLanguages) {
    if (rx::test(l.pattern, name, true)) return std::string(l.market);
  }
  return std::nullopt;
}

// International news channels with feeds in several languages, by home
// market. Carried in the US under the plain name they're usually the
// English feed; beside a copy marked English, the plain one is the home
// language's ("FRANCE 24" next to "FRANCE 24 (ENGLISH)").
std::optional<std::string> multiLanguageHome(std::string_view name) {
  struct Home {
    const char* market;
    const char* pattern;
  };
  static const Home kHomes[] = {
      {"FR", "\\bfrance ?24\\b"},    {"DE", "\\b(dw|deutsche welle)\\b"}, {"AE", "\\bal ?jazeera\\b"},
      {"JP", "\\bnhk world\\b"},     {"CN", "\\bcgtn\\b"},
  };
  for (const Home& h : kHomes) {
    if (rx::test(h.pattern, name, true)) return std::string(h.market);
  }
  return std::nullopt;
}

bool marksEnglish(std::string_view name) { return rx::test("\\b(english|eng)\\b", name, true); }

// The market a directory language belongs to ("es" -> US Spanish, "ur" ->
// Pakistan); nullopt for English or a language this doesn't place.
std::optional<std::string> marketOfLanguage(std::string_view language) {
  static const std::unordered_map<std::string, std::string> kMarkets = {
      {"es", "US-ES"}, {"fr", "FR"}, {"pt", "BR"}, {"hi", "IN"}, {"pa", "IN"}, {"gu", "IN"}, {"ta", "IN"},
      {"te", "IN"},    {"ml", "IN"}, {"bn", "IN"}, {"ur", "PK"}, {"ar", "AE"}, {"zh", "CN"}, {"ko", "KR"},
      {"ja", "JP"},    {"it", "IT"}, {"de", "DE"}, {"pl", "PL"}, {"ru", "RU"}, {"vi", "VN"}, {"fa", "IR"},
      {"tl", "PH"},    {"he", "IL"}, {"el", "GR"}, {"tr", "TR"}};
  auto it = kMarkets.find(std::string(language));
  if (it == kMarkets.end()) return std::nullopt;
  return it->second;
}

// Event-only feeds a provider lists like channels: "NBC Sports 2", "NBC
// Sports 4K (Event Only)", "Tennis Channel Plus 9". Their brand ("NBC
// Sports"), as written.
std::optional<std::string> eventFeedBrand(std::string_view display) {
  if (auto m = rx::first("^(.{2,40}?)\\s*\\(?\\bevent only\\b\\)?\\s*$", display, true))
    return std::string(str::trimUnicode(m->group(1)));
  if (auto m = rx::first("^(nbc sports|tennis channel plus)\\s*\\d{1,3}$", display, true))
    return std::string(m->group(1));
  // "NBC Sports Network 2 (Feed)": a network's overflow game feeds.
  if (auto m = rx::first("^(.{2,40}?)\\s*\\d{1,2}\\s*\\(feed\\)", display, true))
    return std::string(str::trimUnicode(m->group(1)));
  return std::nullopt;
}

bool isEventish(std::string_view name) {
  return rx::test(
      "\\b(vs\\.?|v)\\b|\\s@\\s|\\bat\\b.*\\d|^(next|live|ended|end|now|upcoming)\\s*\\||\\b\\d{1,"
      "2}([:.]\\d\\d)?\\s*(am|pm)\\b|\\b\\d{1,2}:\\d{2}\\b|\\bevent\\s*\\d{1,3}\\b|\\bppv\\s*\\d{1,"
      "3}\\b",
      name, true);
}

// Names that use a network word but aren't a local station.
bool isNationalish(std::string_view name) {
  return rx::test(
      "\\b(sports?|news|soul|business|weather|kids|deportes|life|movies?|classics?|now|live)\\b",
      name, true);
}

// "team" (NFL Buffalo Bills), "regional:<place>" (FOX 5 [SAN DIEGO], NBC
// Sports Boston), or "national".
// `stationLike`: networkWord(display) && !isNationalish(display), worked out
// once by the caller.
template <typename StationLike>
std::string scopeFor(std::string_view display, StationLike stationLike) {
  // A regional sports network: its home places ("regional:a|b").
  if (auto places = regionalSportsPlaces(display)) return "regional:" + str::join(*places, "|");
  if (rx::test("^\\s*\\(?(nfl|nba|nhl|mlb|mls|wnba)\\)?\\s+(\\([A-Z]{2,4}\\)\\s+)?[A-Za-z]",
               display, true)) {
    return "team";
  }
  if (auto place = text::firstGroup(regionPattern(), display, 1, true)) {
    return "regional:" + str::lower(*place);
  }
  if (auto places = regionalChannelPlaces(display)) return "regional:" + str::join(*places, "|");
  if (stationLike()) return "regional:?";
  return "national";
}

// One alternation per word list, built once.
std::string alternation(std::initializer_list<const char*> words) {
  std::string p = "\\b(?:";
  bool first = true;
  for (const char* w : words) {
    if (!first) p += '|';
    p += w;
    first = false;
  }
  return p + ")\\b";
}

LineupCategory categoryFor(std::string_view lower, std::string_view group,
                           const std::optional<std::string>& country, bool hasCallsign) {
  static const std::string kRadio = alternation({"radio", "iheart", "fm"});
  static const std::string kFree =
      alternation({"pluto", "\\(rk\\)", "samsung tv plus", "xumo", "tubi", "plex"});
  // Racing feeds and onboard cameras ("(Apple F1 CAM) Ferrari ...", "... Driver Cam").
  static const std::string kRacing =
      alternation({"formula ?1", "formula one", "f1", "f1 cam", "f1 tv cam", "motogp", "nascar",
                   "indycar", "driver cam", "driver tracker", "driver data", "mixed on board"});
  static const std::string kSpanish =
      alternation({"univision", "unimas", "telemundo", "galavision", "estrella", "azteca",
                   "telehit", "cine latino", "deportes", "tudn", "vix", "discovery familia",
                   "cinelatino", "pasiones", "de pelicula", "de película", "canal 22", "sorpresa"});
  static const std::string kNews =
      alternation({"cnn", "msnbc", "fox news", "cnbc", "bloomberg", "newsmax", "news", "weather",
                   "c-span", "cspan", "hln", "newsnation", "fox business", "cheddar",
                   "yahoo finance", "scripps", "euronews", "al jazeera", "local now",
                   "necn", "ny1", "france 24"});
  static const std::string kSports = alternation(
      {"espn",       "espn2",       "espnu",       "fox sports",  "fs1",        "fs2",
       "nfl",        "nba",         "mlb",         "nhl",         "golf",       "tennis",
       "bein",       "sportsnet",   "sec network", "acc network", "big ten",    "cbs sports",
       "tnt sports", "yes network", "nesn",        "msg",         "fanduel",    "bally",
       "marquee",    "root sports", "altitude",    "nbc sports",  "olympic",    "outdoor",
       "racing",     "fight",       "ufc",         "wwe",         "dazn",       "pac 12",
       "pac-12",     "redbull",     "red bull",    "stadium",     "draftkings", "fubo sports"});
  static const std::string kKids = alternation(
      {"disney", "nick", "nickelodeon", "nicktoons", "teennick", "cartoon", "boomerang",
       "baby ?first", "pbs kids", "universal kids", "discovery family", "kids", "toon"});
  static const std::string kMovies =
      alternation({"hbo",       "cinemax",     "showtime", "starz",    "encore",
                   "mgm\\+?",   "epix",        "tmc",      "movie",    "movies",
                   "flix",      "sundance",    "ifc",      "tcm",      "fxm",
                   "lmn",       "cinebox",     "film",     "filmrise", "hallmark movies",
                   "screenpix", "paramount\\+"});
  static const std::string kMusic = alternation({"mtv", "mtv2", "cmt", "bet jams", "music", "vevo",
                                                 "stingray", "revolt", "axs", "palladia", "fuse"});

  if (contains(group, "radio") || rx::test(kRadio, lower)) return LineupCategory::radio;
  if (country && *country != "US") return LineupCategory::international;
  if (str::startsWith(lower, "(pluto") || contains(group, "roku") || contains(group, "free tv") ||
      rx::test(kFree, lower) || contains(lower, "(rk)")) {
    return LineupCategory::free;
  }
  if (hasCallsign) return LineupCategory::local;
  if (contains(group, "formula") || rx::test(kRacing, lower)) return LineupCategory::sports;
  if (rx::test(kSpanish, lower)) return LineupCategory::spanish;
  if (rx::test(kNews, lower)) return LineupCategory::news;
  if (contains(group, "sport") || rx::test(kSports, lower)) return LineupCategory::sports;
  if (rx::test(kKids, lower)) return LineupCategory::kids;
  if (rx::test(kMovies, lower)) return LineupCategory::movies;
  if (rx::test(kMusic, lower)) return LineupCategory::music;
  return LineupCategory::entertainment;
}

// One channel spelled two ways ("UP TV" / "UPtv", "FYI" / "FYI Network",
// "The Cowboy Channel" / "Cowboy Channel", "Baby TV" / "BabyTV"): the same
// key. No leading "the", no trailing "tv", "network", "channel" or
// "television" (also run into the name). Renamed channels are the channel
// directory's (its corrections: "Starz Family" is Starz Encore Family).
std::string sameChannelKey(const ChannelTraits& traits) {
  std::string lowered = str::replaceAll(str::lower(traits.displayName), "&", " and ");
  lowered = rx::replace(lowered, "[\\(\\[][^\\)\\]]*[\\)\\]]", " ");
  static const std::unordered_set<std::string> kDrop = {"hd", "fhd", "uhd", "sd", "4k", "us", "usa",
                                                        "east", "west", "pacific", "feed"};
  std::vector<std::string> words;
  std::string word;
  auto flush = [&] {
    if (!word.empty() && !kDrop.count(word)) words.push_back(word);
    word.clear();
  };
  for (char c : lowered) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      word += c;
    } else {
      flush();
    }
  }
  flush();
  for (std::string& w : words) {
    if (w == "xtra") w = "extra";  // "Willow Cricket Xtra", "The Movie Channel Xtra"
  }
  if (words.size() > 1 && words.front() == "the") words.erase(words.begin());
  std::string market = traits.market ? *traits.market : std::string("?");
  std::string west = traits.isWest ? "~west" : "";
  static const std::vector<std::string> kTails = {"television", "network", "channel", "tv"};
  for (bool again = true; again && !words.empty();) {
    again = false;
    std::string& last = words.back();
    for (const std::string& tail : kTails) {
      if (last == tail && words.size() > 1) {
        words.pop_back();
        again = true;
        break;
      }
      // "uptv", "babytv": at least two letters left.
      if (tail == "tv" && last.size() >= 4 && str::endsWith(last, tail)) {
        last.resize(last.size() - 2);
        again = true;
        break;
      }
    }
  }
  return market + ":" + str::join(words, "") + west;
}

std::string bestDisplayName(const std::vector<std::string>& names) {
  // Prefer a mixed-case, reasonably short name (the first shortest wins).
  std::vector<const std::string*> mixed;
  for (const std::string& n : names) {
    if (n != str::upper(n)) mixed.push_back(&n);
  }
  std::vector<const std::string*> pool = mixed;
  if (pool.empty()) {
    for (const std::string& n : names) pool.push_back(&n);
  }
  const std::string* best = nullptr;
  for (const std::string* n : pool) {
    if (!best || text::count(*n) < text::count(*best)) best = n;
  }
  return best ? *best : std::string("Channel");
}

std::string familyDisplayName(const Channel* first, const std::string& family) {
  if (str::startsWith(family, "feeds:")) {
    // Event-only feeds: their brand as the first one writes it.
    if (first) {
      if (auto brand = eventFeedBrand(cleanName(first->name))) return *brand;
    }
    return text::capitalized(family.substr(6));
  }
  if (str::startsWith(family, "grp:")) {
    // A provider group of event slots: its name without the country prefix.
    std::string group = first && first->group ? *first->group : family.substr(4);
    std::string cleaned = cleanName(rx::replace(group, "^\\s*[A-Z0-9]{2,5}\\s*\\|\\s*", ""));
    cleaned = std::string(str::trimUnicode(rx::replace(cleaned, "\\s{2,}", " ")));
    return cleaned.empty() ? group : cleaned;
  }
  std::string name = str::replaceAll(str::replaceAll(family, "#", ""), "ⓧ", "");
  name = rx::replace(name, "[()|:]", " ");
  name = rx::replace(name, "\\s+", " ");
  name = std::string(str::trimUnicode(name));
  if (str::startsWith(name, "us ")) name.erase(0, 3);
  return str::upper(name) == name ? name : text::capitalized(name);
}

}  // namespace

// MARK: Context

std::string groupOf(const Channel& channel) {
  if (channel.group) return *channel.group;
  return channel.tags.empty() ? std::string() : channel.tags.front();
}

CallsignContext::CallsignContext(const std::vector<Channel>& channels,
                                 const std::vector<guide::Station>& stations) {
  std::unordered_map<std::string, std::map<std::string, int>> counts;
  for (const guide::Station& station : stations) {
    known.insert(station.callsign);
    counts[station.callsign][station.network] += 1;
  }
  stationsByNetwork = guide::groupByNetwork(stations);
  for (const Channel& channel : channels) {
    if (auto home = multiLanguageHome(channel.name); home && marksEnglish(channel.name))
      englishFeeds.insert(channel.serverId + "|" + *home);
    std::string group = groupOf(channel);
    bool strong = contains(str::lower(group), "local") ||
                  rx::test("^\\s*USA?\\s*\\|\\s*(NBC|FOX|CBS|ABC|CW|PBS)\\b", group);
    std::optional<std::string> network = networkWord(channel.name);
    // Marigold: also a sign that starts the name before its network
    // ("WWOR MyNetworkTV 9", "KQED PBS 9").
    std::string upper = str::upper(str::trimUnicode(channel.name));
    for (const CallsignCandidate& candidate : callsignCandidates(channel.name)) {
      bool leads = network && candidate.sign.size() == 4 && str::startsWith(upper, candidate.sign + " ");
      if (strong || (network && candidate.inParens) || leads) known.insert(candidate.sign);
      if (network) counts[candidate.sign][*network] += 3;
    }
    // Marigold: a call sign alone in a locals group ("US WGN (A)").
    if (strong) {
      if (auto sign = text::firstGroup("^([KW][A-Z]{2,3})$", cleanName(channel.name), 1)) {
        if (!guide::isCallStopWord(*sign)) known.insert(*sign);
      }
    }
  }
  // Lists that mark countries mark the US too; when hardly any channel is
  // marked US, unmarked channels are the US ones.
  size_t us = 0;
  for (const Channel& channel : channels) {
    std::string rawGroup = groupOf(channel);
    auto country = countryFor(channel.name, str::lower(channel.name), rawGroup, str::lower(rawGroup),
                              str::startsWith(channel.serverId, "xt:"));
    if (country == std::string("US")) ++us;
  }
  unknownIsUS = !channels.empty() && us < std::max<size_t>(20, channels.size() / 100);
  // Each sign's most common network. Swift's max over a dictionary breaks
  // ties in hash order; here the alphabetically first network wins.
  for (const auto& [sign, byNetwork] : counts) {
    const std::string* best = nullptr;
    int bestCount = 0;
    for (const auto& [net, count] : byNetwork) {
      if (!best || count > bestCount) {
        best = &net;
        bestCount = count;
      }
    }
    if (best) networkOf[sign] = *best;
  }
}

std::optional<std::string> markedCountry(const Channel& channel) {
  std::string rawGroup = groupOf(channel);
  return countryFor(channel.name, str::lower(channel.name), rawGroup, str::lower(rawGroup),
                    str::startsWith(channel.serverId, "xt:"));
}

// MARK: Build

CatalogResult build(const std::vector<Channel>& channels, time::Seconds now,
                    const std::vector<guide::Station>& stations, const directory::Index* directory,
                    BuildProgress* progress) {
  CatalogResult result;
  result.traits.reserve(channels.size());
  // A shared count, bumped now and then.
  auto step = [progress](size_t done) {
    if (progress && (done & 255) == 0) progress->done.store(done, std::memory_order_relaxed);
  };
  // Pass 0: which call signs are real here, and each sign's usual network.
  if (progress) progress->start(BuildProgress::callSigns, channels.size());
  CallsignContext context(channels, stations);
  context.directory = directory;
  // Pass 1: traits and candidate event families.
  std::unordered_map<std::string, int> familyCounts;
  std::unordered_map<std::string, int> familySeparators;
  if (progress) progress->start(BuildProgress::traits, channels.size());
  size_t seen = 0;
  for (const Channel& channel : channels) {
    step(++seen);
    ChannelTraits traits = traitsFor(channel, context);
    if (traits.familyKey) {
      familyCounts[*traits.familyKey] += 1;
      if (traits.hasSeparator) familySeparators[*traits.familyKey] += 1;
    }
    result.traits[channel.id] = std::move(traits);
  }
  // A family is either large (15+ numbered feeds) or clearly event slots (5+
  // feeds, most named "NN: ..." / "NN | ..."). This keeps numbered regular
  // channels ("beIN Sports 4", "Fox 23") out. Event slots grouped by their
  // provider group always count.
  auto isFamily = [&](const std::string& key) {
    if (str::startsWith(key, "grp:") || str::startsWith(key, "feeds:")) return true;
    auto c = familyCounts.find(key);
    int count = c == familyCounts.end() ? 0 : c->second;
    auto s = familySeparators.find(key);
    int separators = s == familySeparators.end() ? 0 : s->second;
    return count >= 15 || (count >= 5 && separators * 10 >= count * 4);
  };
  for (const Channel& channel : channels) {
    auto it = result.traits.find(channel.id);
    if (it == result.traits.end()) continue;
    ChannelTraits& traits = it->second;
    if (traits.familyKey && !isFamily(*traits.familyKey)) {
      // Marigold: not a family after all, so read the name again without
      // one. tvOS only dropped the family and kept the event kind it had
      // implied, so "US: Telemundo 47" or "Fox 23" with no local group
      // became event slots instead of channels.
      traits = traitsFor(channel, context, false);
    }
    // Event slots (and placeholders in event groups) outside a numbered
    // family: one family per provider group.
    std::string group = str::lower(groupOf(channel));
    if (!traits.familyKey && (traits.kind == ChannelKind::event ||
                              (traits.kind == ChannelKind::placeholder && isEventGroup(group)))) {
      traits.familyKey = "grp:" + group;
      traits.familyNumber = trailingNumber(channel.name);
      traits.eventText = channel.name;
      traits.hasSeparator = true;
    }
    if (traits.familyKey && traits.kind == ChannelKind::channel) traits.kind = ChannelKind::event;
  }

  // Pass 2: entries (clusters of variants), in first-seen order. Names
  // that are one channel spelled two ways join the first one seen, which
  // keeps its id.
  // A spelling the directory doesn't know joins the directory's row for
  // another spelling of it ("ONE TV" with "TV One"), told apart by the key
  // it would have had without the directory.
  std::unordered_map<std::string, std::vector<const Channel*>> clusters;
  std::unordered_map<std::string, std::string> clusterOfSame;
  std::vector<std::string> order;
  auto sameKeyOf = [](const ChannelTraits& traits) {
    if (traits.formerKey.empty()) return sameChannelKey(traits);
    ChannelTraits before = traits;
    std::string market = traits.formerKey.substr(2, traits.formerKey.find(':', 2) - 2);
    before.market = market == "?" ? std::nullopt : std::optional<std::string>(market);
    return sameChannelKey(before);
  };
  std::unordered_map<std::string, std::string> directoryRowOf;  // former key -> d: key
  for (const Channel& channel : channels) {
    auto it = result.traits.find(channel.id);
    if (it == result.traits.end() || !str::startsWith(it->second.clusterKey, "d:")) continue;
    clusterOfSame.emplace(sameKeyOf(it->second), it->second.clusterKey);
    if (!it->second.formerKey.empty()) directoryRowOf.emplace(it->second.formerKey, it->second.clusterKey);
  }
  for (const Channel& channel : channels) {
    auto it = result.traits.find(channel.id);
    if (it == result.traits.end()) continue;
    const ChannelTraits& traits = it->second;
    if (traits.familyKey) continue;
    // Placeholders and headings outside event groups are hidden.
    if (traits.kind == ChannelKind::placeholder || traits.kind == ChannelKind::separator ||
        traits.kind == ChannelKind::event) {
      continue;
    }
    std::string clusterKey = traits.clusterKey;
    if (str::startsWith(clusterKey, "n:")) {
      auto row = directoryRowOf.find(clusterKey);
      clusterKey = row != directoryRowOf.end() ? row->second
                                               : clusterOfSame.emplace(sameChannelKey(traits), clusterKey).first->second;
    }
    auto& members = clusters[clusterKey];
    if (members.empty()) order.push_back(clusterKey);
    members.push_back(&channel);
  }
  result.entries.reserve(order.size());
  if (progress) progress->start(BuildProgress::entries, order.size());
  seen = 0;
  for (const std::string& key : order) {
    step(++seen);
    std::vector<const Channel*>& members = clusters[key];
    if (members.empty()) continue;
    // A directory row is told by a copy the directory knows ("TV One", not
    // the "ONE TV" seen first).
    const Channel* told = members.front();
    if (str::startsWith(key, "d:")) {
      auto known = std::find_if(members.begin(), members.end(),
                                [&](const Channel* c) { return result.traits[c->id].clusterKey == key; });
      if (known != members.end()) told = *known;
    }
    const ChannelTraits& traits = result.traits[told->id];
    struct Ranked {
      const Channel* channel;
      const ChannelTraits* traits;
      size_t nameLength;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(members.size());
    for (const Channel* m : members) {
      ranked.push_back({m, &result.traits[m->id], text::count(m->name)});
    }
    // Stable, like Swift's sort in practice.
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
      // Xtream versions first, server (Emby / Jellyfin) as backup.
      bool xa = str::startsWith(a.channel->serverId, "xt:");
      bool xb = str::startsWith(b.channel->serverId, "xt:");
      if (xa != xb) return xa;
      if (a.channel->isFavorite != b.channel->isFavorite) return a.channel->isFavorite;
      if (a.traits->isWest != b.traits->isWest) return !a.traits->isWest;
      if (a.traits->quality != b.traits->quality) return a.traits->quality > b.traits->quality;
      return a.nameLength < b.nameLength;
    });
    EntryInfo entry;
    entry.id = "e|" + key;
    const directory::Entry* listed = nullptr;
    if (directory && !traits.directoryId.empty() && str::startsWith(key, "d:")) {
      listed = directory->entryById(traits.directoryId);
    }
    if (traits.core) {
      entry.name = traits.isWest ? *traits.core + " West" : *traits.core;
    } else if (listed) {
      // The directory's name for it ("Hallmark Mystery", not "HALLMARK M & M").
      std::string shown = directory::shownName(*listed);
      entry.name = shown + (traits.isWest && !rx::test("\\b(west|pacific)\\b", shown, true) ? " West" : "");
    } else {
      std::vector<std::string> names;
      names.reserve(ranked.size());
      for (const Ranked& r : ranked) names.push_back(r.traits->displayName);
      entry.name = bestDisplayName(names);
      // A West version is named so, as the core networks' are.
      if (traits.isWest && !rx::test("\\b(west|pacific)\\b", entry.name, true)) entry.name += " West";
    }
    for (const Ranked& r : ranked) entry.members.push_back(r.channel->id);
    entry.category = traits.category;
    entry.callsign = traits.callsign;
    entry.country = traits.country;
    entry.virtualNumber = virtualNumber(entry.name);
    entry.kind = traits.kind;
    // Marigold: the copies decide together, not the first one. A channel the
    // provider also lists under Roku, Pluto, Tubi... is free streaming even
    // when its first copy sits in a catch-all group ("US| AT&T"); a premium
    // channel never is, whatever group a copy turns up in.
    if (!traits.core && !traits.callsign && traits.category != LineupCategory::local) {
      if (premiumName(entry.name)) {
        if (entry.kind == ChannelKind::fast) {
          entry.kind = ChannelKind::channel;
          auto regular = std::find_if(ranked.begin(), ranked.end(),
                                      [](const Ranked& r) { return r.traits->kind != ChannelKind::fast; });
          entry.category = regular != ranked.end() ? regular->traits->category : LineupCategory::movies;
        }
        // Filed by its words as entertainment ("5StarMax"): it's premium.
        if (entry.category == LineupCategory::entertainment) entry.category = LineupCategory::movies;
      } else if (entry.kind == ChannelKind::channel &&
                 (freeStreamingName(entry.name) ||
                  std::any_of(ranked.begin(), ranked.end(), [](const Ranked& r) { return freeStreamingCopy(*r.channel); }))) {
        entry.kind = ChannelKind::fast;
        entry.category = LineupCategory::free;
      }
    }
    entry.market = traits.market;
    entry.core = traits.core;
    entry.scope = traits.scope;
    entry.directoryId = traits.directoryId;
    entry.closed = traits.closed;
    // Its members' rows under the rules alone: the ids the user's choices
    // may have been made under.
    std::set<std::string> former;
    for (const Ranked& r : ranked) {
      std::string was = r.traits->formerKey.empty() ? r.traits->clusterKey : r.traits->formerKey;
      if ("e|" + was != entry.id) former.insert("e|" + was);
    }
    entry.formerIds.assign(former.begin(), former.end());
    for (const std::string& member : entry.members) result.entryOf[member] = entry.id;
    result.entries.push_back(std::move(entry));
  }

  // Pass 3: event families and events.
  if (progress) progress->start(BuildProgress::events, 0);
  EventsResult events = buildEvents(channels, result.traits, now);
  result.families = std::move(events.families);
  result.events = std::move(events.events);
  if (progress) progress->start(BuildProgress::finished, 0);
  return result;
}

EventsResult buildEvents(const std::vector<Channel>& channels,
                         const std::unordered_map<std::string, ChannelTraits>& traits,
                         time::Seconds now) {
  std::unordered_map<std::string, std::vector<const Channel*>> familyMembers;
  std::vector<std::string> familyOrder;
  auto traitOf = [&](const Channel* c) -> const ChannelTraits* {
    auto it = traits.find(c->id);
    return it == traits.end() ? nullptr : &it->second;
  };
  for (const Channel& channel : channels) {
    const ChannelTraits* t = traitOf(&channel);
    if (!t || !t->familyKey) continue;
    auto& members = familyMembers[*t->familyKey];
    if (members.empty()) familyOrder.push_back(*t->familyKey);
    members.push_back(&channel);
  }
  EventsResult out;
  std::unordered_map<std::string, LiveEvent> eventsByKey;
  for (const std::string& family : familyOrder) {
    std::vector<const Channel*> sorted = familyMembers[family];
    auto numberOf = [&](const Channel* c) {
      const ChannelTraits* t = traitOf(c);
      return t && t->familyNumber ? *t->familyNumber : 0;
    };
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const Channel* a, const Channel* b) { return numberOf(a) < numberOf(b); });
    std::optional<std::string> market;
    for (const Channel* c : sorted) {
      const ChannelTraits* t = traitOf(c);
      if (t && t->market) {
        market = t->market;
        break;
      }
    }
    int named = 0;
    std::string headGroup = sorted.empty() || !sorted.front()->group ? "" : *sorted.front()->group;
    std::string league = leagueFor(family, headGroup, "");
    for (const Channel* channel : sorted) {
      const ChannelTraits* t = traitOf(channel);
      if (!t || !t->eventText || isPlaceholder(*t->eventText) ||
          t->kind == ChannelKind::placeholder ||
          rx::test("^\\W*(ended|end)\\b", *t->eventText, true)) {
        continue;
      }
      named += 1;
      ParsedEvent parsed = parseEventDetailed(*t->eventText, now, market);
      if (parsed.title.empty()) continue;
      if (parsed.start && now - *parsed.start > 8 * 3600) continue;       // long over
      if (parsed.start && *parsed.start - now > 6 * 24 * 3600) continue;  // stale or far off
      std::string eventLeague =
          leagueFor(family, channel->group ? *channel->group : "", parsed.title);
      std::string key = eventKey(parsed.title, parsed.start);
      auto existing = eventsByKey.find(key);
      if (existing != eventsByKey.end()) {
        existing->second.feeds.push_back(channel->id);
      } else {
        LiveEvent event;
        event.id = "ev|" + key;
        event.title = parsed.title;
        event.league = eventLeague;
        event.start = parsed.start;
        event.feeds = {channel->id};
        event.family = family;
        event.market = market;
        // Always set when there's a time (unset means "saved by an older build").
        if (parsed.start) event.startGuessed = parsed.guessed;
        eventsByKey.emplace(key, std::move(event));
      }
    }
    EventFamily fam;
    fam.id = "fam|" + family;
    fam.name = familyDisplayName(sorted.empty() ? nullptr : sorted.front(), family);
    for (const Channel* c : sorted) fam.feeds.push_back(c->id);
    fam.hasNames = static_cast<size_t>(named) * 4 >= sorted.size();
    fam.league = league;
    fam.market = market;
    out.families.push_back(std::move(fam));
  }
  out.events.reserve(eventsByKey.size());
  for (auto& [key, event] : eventsByKey) out.events.push_back(std::move(event));
  // Timed events first, by start; then untimed by title. Swift leaves equal
  // starts in hash order; here they fall back to title, then id.
  std::sort(out.events.begin(), out.events.end(), [](const LiveEvent& a, const LiveEvent& b) {
    if (a.start && b.start && *a.start != *b.start) return *a.start < *b.start;
    if (a.start && !b.start) return true;
    if (!a.start && b.start) return false;
    if (a.title != b.title) return a.title < b.title;
    return a.id < b.id;
  });
  return out;
}

void refreshEvents(CatalogResult& result, const std::vector<Channel>& channels,
                   const std::vector<Channel>& renamed, time::Seconds now) {
  for (const Channel& channel : renamed) {
    auto it = result.traits.find(channel.id);
    if (it == result.traits.end() || !it->second.familyKey) continue;
    ChannelTraits& traits = it->second;
    std::string family = *traits.familyKey;
    ChannelTraits fresh = traitsFor(channel, CallsignContext());
    if (str::startsWith(family, "grp:")) {
      traits.eventText = channel.name;
      traits.familyNumber = trailingNumber(channel.name);
    } else if (fresh.familyKey == family) {
      traits.eventText = fresh.eventText;
      traits.familyNumber = fresh.familyNumber;
    } else {
      traits.eventText = channel.name;
    }
    switch (fresh.kind) {
      case ChannelKind::placeholder:
        traits.kind = ChannelKind::placeholder;
        break;
      case ChannelKind::channel:
      case ChannelKind::event:
        traits.kind = ChannelKind::event;
        break;
      default:
        traits.kind = fresh.kind;
        break;
    }
    traits.displayName = fresh.displayName;
  }
  EventsResult events = buildEvents(channels, result.traits, now);
  result.families = std::move(events.families);
  result.events = std::move(events.events);
}

// MARK: Traits

ChannelTraits traitsFor(const Channel& channel, const CallsignContext& context, bool numberedFamilies) {
  const std::string& name = channel.name;
  std::string lower = str::lower(name);
  std::string rawGroup = groupOf(channel);
  std::string group = str::lower(rawGroup);

  // Source marker: trailing "(A)" style, or the leading prefix style.
  std::string sourceTag;
  std::optional<std::string> tag;
  if (hasAnyOf(lower, "(")) tag = text::firstMatch("\\((a|h|d|s|p|sp|rk|gt|dz|xv|prime)\\)", lower);
  if (tag) {
    sourceTag = *tag;
  } else if (hasAnyOf(lower, ":|")) {
    if (auto p = text::firstMatch("^\\s*(usa?|us|reg|mex|mx|uk|ca)\\s*[:|]", lower)) {
      sourceTag = std::string(str::trimUnicode(*p));
    }
  }

  int quality = 1;
  for (const auto& [pattern, value] : kQualityTokens) {
    if (rx::test(pattern, lower)) {
      quality = value;
      break;
    }
  }
  bool isWest = guide::namesWestFeed(lower);
  std::optional<std::string> country =
      countryFor(name, lower, rawGroup, group, str::startsWith(channel.serverId, "xt:"));
  if (!country && latinScript(name)) {
    // Unmarked: US when the list marks hardly anything as US, or when the
    // name is a US national network's ("ESPN", "HGTV"; marked locals with
    // unmarked cable channels is a common provider layout).
    if (context.unknownIsUS || coreMatch(cleanName(name))) country = "US";
  }
  std::optional<std::string> market = country;
  if (country == std::string("US")) {
    if (auto other = otherLanguageMarket(name)) {
      market = *other;
    } else if (auto home = multiLanguageHome(name);
               home && !marksEnglish(name) && context.englishFeeds.count(channel.serverId + "|" + *home)) {
      market = *home;
    } else if (isSpanishUS(name, group)) {
      market = "US-ES";
    }
  }
  std::string display = cleanName(name);
  // "[Denver] PBS12", "[Seattle] Oxygen": the provider's note of where the
  // feed comes from.
  std::optional<std::string> bracketCity;
  if (hasAnyOf(name, "[")) bracketCity = text::firstGroup("\\[([^\\]]{2,30})\\]", name, 1);
  // A network word in a name that isn't a national one ("FOX 5 Testville"):
  // asked up to three times below, worked out once.
  std::optional<bool> stationLikeMemo;
  auto stationLike = [&] {
    if (!stationLikeMemo)
      stationLikeMemo = networkWord(display).has_value() && !isNationalish(display);
    return *stationLikeMemo;
  };
  std::optional<CoreHit> core = market == std::string("US") ? coreMatch(display) : std::nullopt;
  ChannelKind kind = kindFor(name, lower, group, core.has_value());
  // A national channel's feed from a West city is its West feed.
  if (!isWest && bracketCity && !stationLike() && marketIsWest(*bracketCity).value_or(false)) isWest = true;

  // Event family?
  std::optional<std::string> familyKey;
  std::optional<int> familyNumber;
  std::optional<std::string> eventText;
  bool hasSeparator = false;
  bool isLocalish =
      contains(group, "local") || (hasAnyOf(lower, "(") && rx::test("\\([kw][a-z]{2,3}\\b", lower));
  if (numberedFamilies && !core && kind != ChannelKind::separator && (!isLocalish || kind == ChannelKind::event)) {
    if (auto family = familyParts(name)) {
      familyKey = family->key;
      familyNumber = family->number;
      eventText = family->rest;
      hasSeparator = family->separator;
    }
  }
  if (!familyKey && !core && kind != ChannelKind::separator && kind != ChannelKind::placeholder) {
    if (auto brand = eventFeedBrand(display)) {
      familyKey = "feeds:" + str::lower(*brand);
      familyNumber = trailingNumber(display);
      hasSeparator = true;
      kind = ChannelKind::event;
    }
  }

  // Call sign: only real ones (station directory, local groups, or written
  // next to a network name), never for other countries.
  std::optional<std::string> callsign;
  if (kind == ChannelKind::channel && !core && !familyKey && (!country || *country == "US")) {
    bool localGroup = contains(group, "local");
    std::vector<CallsignCandidate> candidates;
    // Marigold: "WLS ABC 7", or "KGO" alone in a locals group.
    bool shortOk = networkWord(name).has_value() ||
                   (localGroup && (rx::test("^\\s*[KW][A-Z]{2}\\s*$", name) ||
                                   rx::test("^[KW][A-Z]{2}$", display)));
    for (CallsignCandidate& c : callsignCandidates(name, shortOk)) {
      if (c.sign.size() == 3 && !c.inParens && !context.known.count(c.sign)) continue;
      if (context.known.count(c.sign) || localGroup) candidates.push_back(std::move(c));
    }
    // The first written in parentheses, else the first.
    auto inParens = std::find_if(candidates.begin(), candidates.end(),
                                 [](const CallsignCandidate& c) { return c.inParens; });
    if (inParens != candidates.end()) {
      callsign = inParens->sign;
    } else if (!candidates.empty()) {
      callsign = candidates.front().sign;
    }
    // "WGN9": a known sign and its channel number.
    if (!callsign) {
      if (auto sign = text::firstGroup("^([KW][A-Z]{2,3}) ?\\d{1,2}$", display, 1)) {
        if (context.known.count(*sign)) callsign = *sign;
      }
    }
    // "FOX 2 LOCAL SAN FRANCISCO", "[Denver] PBS12": network + city -> the station.
    if (!callsign && stationLike() && !context.stationsByNetwork.empty()) {
      callsign = guide::stationCallsign(bracketCity ? display + " " + *bracketCity : display,
                                        context.stationsByNetwork);
    }
  }
  // The channel directory, for channels that aren't stations: one it knows
  // by name, another name or our corrections (not a loose match) is told
  // by it. One on its distant-locals list is a station (WGN-TV); a name
  // only another country's channels have, in a language a US English
  // lineup doesn't carry under the same name, is that country's.
  const directory::Entry* listed = nullptr;
  const std::optional<std::string> marketBefore = market;
  bool usMarket = market == std::string("US") || market == std::string("US-ES") || !country;
  if (context.directory && kind == ChannelKind::channel && !core && !familyKey && !callsign && usMarket &&
      !stationLike()) {
    directory::Match m = context.directory->match(display);
    if (m.entry && (m.kind == directory::MatchKind::exact || m.kind == directory::MatchKind::alias)) {
      if (m.entry->list == "local") {
        if (auto sign = text::firstGroup("^([KW][A-Z]{2,3})(-[A-Z]{2})?$", m.entry->name, 1)) callsign = *sign;
      } else {
        listed = m.entry;
        if (market == std::string("US")) {
          if (auto other = marketOfLanguage(listed->language)) market = *other;
        }
      }
    } else if (!m.entry && !m.elsewhere.empty() && market == std::string("US")) {
      std::string place = m.elsewhere.substr(0, m.elsewhere.find(':'));
      std::string language = m.elsewhere.substr(m.elsewhere.find(':') + 1);
      static const std::unordered_set<std::string> kApart = {"ur", "hi", "ar", "pa", "gu", "ta", "te", "ml", "bn",
                                                             "zh", "ko", "ja", "fa", "ru", "vi", "he", "el", "tr"};
      if (kApart.count(language)) market = place;
    }
  }

  if (familyKey && kind == ChannelKind::channel) kind = ChannelKind::event;

  std::string key;
  if (kind == ChannelKind::event || kind == ChannelKind::placeholder ||
      kind == ChannelKind::separator) {
    key = "x:" + channel.id;
  } else if (core) {
    key = "core:" + core->name + (isWest ? "~west" : "");
  } else if (callsign) {
    // Subchannels and sister stations stay apart: WGBC-NBC != WGBC-FOX.
    std::string network;
    if (auto word = networkWord(name)) {
      network = *word;
    } else if (auto it = context.networkOf.find(*callsign); it != context.networkOf.end()) {
      network = it->second;
    }
    key = "cs:" + *callsign + "|" + network;
  } else {
    std::string normalized = guide::normalize(display);
    // Never merged across countries: a Mexican feed isn't a backup for a US one.
    key = "n:" + (market ? *market : std::string("?")) + ":" +
          (normalized.empty() ? lower : normalized) +
          (isWest && !str::endsWith(normalized, "~west") ? "~west" : "");
  }
  // A directory channel's copies are one row, whatever each is called; the
  // key the rules alone gave stays as the row's earlier id.
  std::string formerKey;
  if (str::startsWith(key, "n:") && (listed || market != marketBefore)) {
    formerKey = "n:" + (marketBefore ? *marketBefore : std::string("?")) + key.substr(key.find(':', 2));
    if (listed) key = "d:" + listed->id + (isWest ? "~west" : "");
  }

  std::string scope;
  if (core) {
    scope = "national";
  } else if (callsign) {
    scope = "local";
  } else if (market == std::string("US") || market == std::string("US-ES")) {
    scope = scopeFor(display, stationLike);
  } else {
    scope = "national";
  }
  // A regional channel the directory knows is its area's.
  if (listed && scope == "national" &&
      (!listed->places.empty() || rx::test("\\bregional\\b", listed->genre, true))) {
    scope = "regional:" + (listed->places.empty() ? std::string("?") : str::join(listed->places, "|"));
  }

  LineupCategory category;
  switch (kind) {
    case ChannelKind::fast:
    case ChannelKind::loop247:
      category = LineupCategory::free;
      break;
    case ChannelKind::radio:
      category = LineupCategory::radio;
      break;
    default:
      if (market && *market != "US" && *market != "US-ES") {
        category = LineupCategory::international;
      } else if (market == std::string("US-ES")) {
        category = LineupCategory::spanish;
      } else if (core) {
        category = core->category;
      } else if (regionalSportsPlaces(display)) {
        category = LineupCategory::sports;
      } else if (callsign || (str::startsWith(scope, "regional") && stationLike())) {
        category = LineupCategory::local;
      } else {
        category = categoryFor(lower, group, country, false);
      }
      break;
  }

  ChannelTraits traits;
  traits.clusterKey = std::move(key);
  // A provider's code in capitals isn't part of the name ("ROAR (ROARTV)",
  // "The Nest (THENEST)"); a station keeps its call sign ("NBC 11 (KNTV)").
  std::string shown = display;
  if (!callsign && hasAnyOf(shown, "(")) {
    std::string bare(str::trimUnicode(rx::replace(shown, "\\s*\\((?![KW][A-Z]{2,3}\\))[A-Z][A-Z0-9]{2,9}\\)", "")));
    if (!bare.empty()) shown = std::move(bare);
  }
  traits.displayName = core ? (isWest ? core->name + " West" : core->name) : shown;
  traits.quality = quality;
  traits.sourceTag = std::move(sourceTag);
  traits.callsign = std::move(callsign);
  traits.isWest = isWest;
  traits.country = std::move(country);
  traits.category = category;
  traits.familyKey = std::move(familyKey);
  traits.familyNumber = familyNumber;
  traits.eventText = std::move(eventText);
  traits.hasSeparator = hasSeparator;
  traits.kind = kind;
  traits.market = std::move(market);
  if (core) traits.core = core->name;
  traits.scope = std::move(scope);
  if (listed) {
    traits.directoryId = listed->id;
    traits.closed = listed->closed;
  }
  traits.formerKey = std::move(formerKey);
  return traits;
}

// MARK: Classification

bool freeStreamingCopy(const Channel& channel) {
  // Prime, Netflix and Peacock are left out: their groups mix in paid
  // channels (HBO Latino, MGM+), so a copy there says nothing.
  std::string group = groupOf(channel);
  return rx::test("\\b(roku|tubi|pluto|samsung|plex|xumo|vizio|local now|distro|stirr|free tv|fast)\\b", group, true) ||
         (hasAnyOf(channel.name, "(:") &&
          rx::test("^\\s*\\((pluto[^)]*|rk|roku|tubi|samsung[^)]*|plex|xumo)\\)|^\\s*(rk|pluto|roku|tubi|samsung|plex|xumo)[^:]{0,8}:",
                   channel.name, true));
}

bool premiumName(std::string_view name) {
  // Also the Cinemax and Starz Encore channels that don't say so
  // ("5StarMax", "ActionMax", "MoviePlex") and Showtime's "Sho x BET".
  return rx::test("\\b(hbo|cinemax|showtime|starz|epix)\\b|\\bmgm ?(\\+|plus)|\\bamc ?(\\+|plus)|\\bparamount ?(\\+|plus)|"
                  "\\b(5 ?star|action|more|thriller|movie|outer) ?max\\b|\\b(movie|indie|retro)plex\\b|\\bsho ?x ?bet\\b",
                  name, true);
}

// Free streaming channels the lists and groups don't tell: ones the
// provider files beside its cable channels ("US| AT&T"), and its own
// movie loops ("CINEBOX: HORROR").
bool freeStreamingName(std::string_view name) {
  return rx::test(
      "^(awe plus|horror machine|horror plus|westerns plus|big a anime|bowling tv|mayday:? air disaster|air (&|and) space channel|"
      "torque( by history)?|reelz famo?us( & infamous)?|tribeca festival\\+?|flixfling( now)?|earthx(tv)?|curiosity now|"
      "infast|travelxp|grjngo( - western movies)?|great american romcoms|moviesphere gold|frasier|the film detective|"
      "family movie classics)( hd)?$",
      name, true);
}

bool isEventGroup(std::string_view group) {
  return rx::test(
      "\\bppv\\b|event|league pass|espn ?\\+|espn "
      "plus|\\bdazn\\b|\\bflo|ncaa|uefa|formula|\\bf1\\b|"
      "hbo "
      "max|vidio|monomax|\\bnfl\\b|\\bnba\\b|\\bnhl\\b|\\bmlb\\b|\\bmls\\b|\\bwnba\\b|\\bmilb\\b|"
      "^football$|replay|sport on air",
      group);
}

bool sportsGroup(std::string_view group) {
  std::string lower = str::lower(group);
  return isEventGroup(lower) ||
         rx::test("sport|soccer|football|futbol|fútbol|calcio|hockey|cricket|rugby|tennis|golf|"
                  "racing|motor|boxing|\\bufc\\b|\\bwwe\\b|fight|\\btsn|bein|eurosport|setanta|"
                  "premier|bundesliga|\\bliga\\b|serie a|ligue ?1|eredivisie|champions|olymp|"
                  "\\bmatch",
                  lower);
}

ChannelKind kindFor(std::string_view name, std::string_view lower, std::string_view group,
                    bool isCore) {
  // Each pattern below only runs when the name holds a character or word
  // every one of its alternatives needs (a cheap scan; same results).
  bool separatorChars = name.find_first_of("#=-*~") != std::string_view::npos ||
                        contains(name, "▬") || contains(name, "━") || contains(name, "═");
  if (separatorChars && rx::test("^\\s*(#{2,}|={2,}|-{3,}|\\*{2,}|▬|━|═|~{2,})|#{3,}\\s*$", name)) {
    return ChannelKind::separator;
  }
  bool placeholderWords = contains(lower, "no") || contains(lower, "off") ||
                          contains(lower, "coming") || contains(lower, "end") ||
                          contains(lower, "tvg-id") || contains(lower, "209");
  if (placeholderWords &&
      rx::test("no event|no stream|no games?\\b|no live|offline|off ?air|coming "
               "soon|^\\W*ended\\b|^\\W*end\\s*\\||tvg-id=|2098 12 31|2099 01 0",
               lower)) {
    return ChannelKind::placeholder;
  }
  // A numbered slot ("NCAAF 16: USA NETWORK") is an event even when it names a network.
  bool digits = mayHaveDigit(name);
  if (digits && hasAnyOf(name, ":|") &&
      rx::test("^\\W*[A-Za-z+][A-Za-z+ ()]{1,20}?\\s*\\d{1,3}\\s*\\)?\\s*[:|]", name)) {
    return ChannelKind::event;
  }
  if (isCore) return ChannelKind::channel;
  if (isEventGroup(group) &&
      (rx::test("\\b\\d{1,3}\\b", name) || isEventish(name) || text::count(cleanName(name)) < 4)) {
    return ChannelKind::event;
  }
  bool slotWords = hasAnyOf(lower, "|") || contains(lower, "event") || contains(lower, "ppv");
  if (slotWords && rx::test("^(next|live|ended|end|now|upcoming)\\s*\\||\\bevent\\s*\\d{1,3}\\b|"
                            "\\bppv\\s*\\d{1,3}\\b",
                            lower)) {
    return ChannelKind::event;
  }
  // "NCAAF 16 Auburn at Georgia 3:30PM": league word, slot number, event.
  if (digits && rx::test("^[A-Za-z+]{2,12}( [A-Za-z+]{2,12})?\\s+\\d{1,3}\\s+", name) &&
      isEventish(name)) {
    return ChannelKind::event;
  }
  // The provider's own movie loops ("CINEBOX: HORROR") are loops too.
  if (contains(name, "24/7") || contains(group, "24/7") || str::startsWith(lower, "cinebox:") ||
      (contains(lower, "247") && rx::test("\\b247\\b", lower))) {
    return ChannelKind::loop247;
  }
  if (rx::test(
          "\\b(prime|roku|tubi|pluto|samsung|plex|xumo|free tv|fast|netflix on air|vizio|local "
          "now|distro|stirr)\\b",
          group) ||
      (hasAnyOf(name, "(:") &&
       rx::test("^\\s*\\((pluto[^)]*|rk|roku|tubi|samsung[^)]*|plex|xumo)\\)|^\\s*(pluto|roku|tubi|"
                "prime|samsung|plex|xumo)[^:]{0,8}:",
                name, true))) {
    return ChannelKind::fast;
  }
  if (contains(group, "radio") || ((contains(lower, "radio") || contains(lower, "iheart")) &&
                                   rx::test("\\b(radio|iheart)\\b", lower))) {
    return ChannelKind::radio;
  }
  return ChannelKind::channel;
}

std::vector<CallsignCandidate> callsignCandidates(std::string_view name, bool allowShort) {
  std::string up = str::upper(name);
  std::vector<CallsignCandidate> found;
  for (const rx::Match& m :
       rx::all("(?<![A-Z0-9])([KW][A-Z]{2,3})(?:[- ]?(DT|TV|LD|LP|CD|CA)\\d?)?(?![A-Z0-9])", up)) {
    std::string sign(m.group(1));
    size_t at = static_cast<size_t>(m.group(1).data() - up.data());
    bool inParens = at > 0 && up[at - 1] == '(';
    // Three-letter call signs (KGO, WGN) only in parentheses or with a
    // suffix, or when the caller vouches for them (a network word in the
    // name, or the whole name in a locals group); the caller still needs the
    // sign to be a known station.
    if (!(sign.size() == 4 || inParens || m.has(2) || allowShort)) continue;
    bool seen = std::any_of(found.begin(), found.end(),
                            [&](const CallsignCandidate& c) { return c.sign == sign; });
    if (!seen) found.push_back({sign, inParens});
  }
  return found;
}

std::optional<std::string> networkWord(std::string_view name) {
  // Marigold: also run into the channel number ("ABC7", "PBS12"), and
  // MyNetworkTV as "MYNET" or "MY 40".
  auto word = text::firstGroup(
      "\\b(ABC|CBS|NBC|FOX|CW|PBS|MYTV|MyNetworkTV|MYNET|ION|Telemundo|Univision|UniMas|Independent|IND|"
      "TMO|MY(?= ?\\d))(?=\\d|\\b)",
      name, 1, true);
  if (!word) return std::nullopt;
  std::string up = str::upper(*word);
  if (up == "TMO") return std::string("TELEMUNDO");
  if (up == "MYNETWORKTV" || up == "MYNET" || up == "MY") return std::string("MYTV");
  if (up == "IND") return std::string("INDEPENDENT");
  return up;
}

CoreKey coreKey(std::string_view display) {
  static const std::unordered_set<std::string> kDrop = {
      "hd",    "fhd",   "uhd",   "sd",   "4k",   "8k",  "hevc",   "h265", "h264", "raw", "vip",
      "60fps", "50fps", "1080p", "720p", "1080", "720", "backup", "east", "feed", "alt"};
  std::string lowered =
      str::replaceAll(str::replaceAll(str::lower(display), "&", " and "), "+", " plus ");
  CoreKey out;
  std::vector<std::string> kept;
  size_t i = 0;
  auto alnum = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
  };
  while (i < lowered.size()) {
    while (i < lowered.size() && !alnum(lowered[i])) ++i;
    size_t start = i;
    while (i < lowered.size() && alnum(lowered[i])) ++i;
    if (i == start) continue;
    std::string token = lowered.substr(start, i - start);
    if (token == "west" || token == "pacific") {
      out.west = true;
      continue;
    }
    if (!kDrop.count(token)) kept.push_back(std::move(token));
  }
  out.key = str::join(kept, " ");
  return out;
}

std::optional<CoreHit> coreMatch(std::string_view display) {
  // Marigold: "The Movie Channel (TMC)", "Oxygen True Crime (OXYGEN)",
  // "National Geographic (NGCHD)": a provider's code in brackets.
  if (hasAnyOf(display, "([")) {
    std::string bare = rx::replace(display, "\\s*[\\(\\[][^\\)\\]]*[\\)\\]]", " ");
    if (bare != display) {
      if (auto hit = coreMatch(bare)) return hit;
    }
  }
  std::string key = coreKey(display).key;
  const auto& byAlias = coreByAlias();
  auto hit = byAlias.find(key);
  if (hit != byAlias.end()) return hit->second;
  // "NAT GEO WILD HD US": a leading / trailing US marker. Marigold: only
  // markers at the ends, one end then both, so the name's own "USA" stays
  // ("USA Network US" -> "usa network"; tvOS dropped every "usa").
  std::vector<std::string> parts;
  for (std::string_view t : str::split(key, ' ')) parts.emplace_back(t);
  auto marker = [](const std::string& t) { return t == "us" || t == "usa"; };
  std::vector<std::vector<std::string>> tries;
  if (parts.size() > 1 && marker(parts.back())) tries.emplace_back(parts.begin(), parts.end() - 1);
  if (parts.size() > 1 && marker(parts.front())) tries.emplace_back(parts.begin() + 1, parts.end());
  if (parts.size() > 2 && marker(parts.front()) && marker(parts.back())) tries.emplace_back(parts.begin() + 1, parts.end() - 1);
  for (const auto& t : tries) {
    auto again = byAlias.find(str::join(t, " "));
    if (again != byAlias.end()) return again->second;
  }
  return std::nullopt;
}

std::optional<int> trailingNumber(std::string_view name) {
  auto m = text::firstMatch("\\d{1,4}(?=\\D*$)", name);
  return m ? text::toInt(*m) : std::nullopt;
}

std::optional<FamilyParts> familyParts(std::string_view name) {
  std::string trimmed(str::trimUnicode(name));
  if (!mayHaveDigit(trimmed)) return std::nullopt;  // both forms need a number
  static const char* const kPatterns[] = {
      // head ending in a number, then ":" or "|" and the event (or nothing)
      "^(.{1,40}?\\d{1,3}\\s*ⓧ?\\)?)\\s*(?:[:|]\\s*(.*))?$",
      // "NCAAF 02 Florida @ Texas 12PM": short word prefix, number, text
      "^([A-Za-z+]{2,12}(?: [A-Za-z+]{2,12})?\\s+\\d{1,3})\\s+(.+)$"};
  for (int index = 0; index < 2; ++index) {
    auto match = rx::first(kPatterns[index], trimmed);
    if (!match || !match->has(1)) continue;
    std::string head(match->group(1));
    auto numberText = text::firstMatch("\\d{1,3}(?=\\D*$)", head);
    std::optional<int> number = numberText ? text::toInt(*numberText) : std::nullopt;
    if (!number) continue;
    std::string key = rx::replace(rx::replace(str::lower(head), "\\d+", "#"), "\\s+", " ");
    key = std::string(str::trimUnicode(key));
    std::optional<std::string> rest;
    if (match->has(2)) rest = std::string(str::trimUnicode(match->group(2)));
    if (index == 1) {
      // Word-prefix form only counts when the rest looks like an event.
      if (!rest || !rx::test("\\b(vs\\.?|v\\.?|at)\\b|@|\\b\\d{1,2}([:.]\\d{2})?\\s*(am|pm)\\b",
                             *rest, true)) {
        continue;
      }
    }
    bool separator = index == 1 || rx::test("^.{1,40}?\\d{1,3}\\s*ⓧ?\\)?\\s*[:|]", trimmed);
    FamilyParts parts;
    parts.key = std::move(key);
    parts.number = *number;
    if (rest && !rest->empty()) parts.rest = std::move(rest);
    parts.separator = separator;
    return parts;
  }
  return std::nullopt;
}

bool isPlaceholder(std::string_view textIn) {
  std::string lower = str::lower(textIn);
  if (rx::test("no (scheduled )?events?|no games?|no live|not live|off ?air|2098 12 "
               "31|^\\W*$|^\\d+\\.?\\s*$",
               lower)) {
    return true;
  }
  return text::count(str::trimUnicode(lower, true)) < 3;
}

std::string cleanName(std::string_view name) {
  // Superscript markers ("ᴿᴬᵂ ⁶⁰ᶠᵖˢ ⱽᴵᴾ"), "ⓧ".
  std::string s(name);
  if (hasNonAscii(s)) {
    s = rx::replace(s,
                    "[\\x{1D2C}-\\x{1D6A}\\x{1D78}-\\x{1DBF}\\x{2070}-\\x{209F}\\x{2C7D}"
                    "\\x{24B6}-\\x{24E9}\\x{02B0}-\\x{02FF}\\x{00B2}\\x{00B3}\\x{00B9}ⓧ]",
                    "");
  }
  // Provider prefixes in capitals: "TV:", "AT&T:", "PRIME:", "PL VIP:".
  if (hasAnyOf(s, ":")) {
    s = rx::replace(s, "^\\s*[A-Z0-9&+]{1,8}(\\s[A-Z0-9&+]{1,6})?\\s*:\\s+(?=\\S{2})", "");
  }
  struct Pattern {
    const char* pattern;
    const char* needs;  // skipped unless one of these is in the name (nullptr: always run)
  };
  static const Pattern kPatterns[] = {
      {"^\\s*(\\([A-Za-z0-9 ]{1,14}\\)\\s*)+", "("},          // (US) (A)
      {"^\\s*\\|\\s*[A-Za-z]{2,3}\\s*\\|\\s*", "|"},          // |US|
      {"^\\s*(USA?|US|REG|MEX|MX|UK|CA)\\s*[:|]\\s*", ":|"},  // US: / USA |
      // US / USA, but not USA Network's own name.
      {"^\\s*(USA?|US)\\s+(?!network\\b)", "Uu"},
      {"^\\s*\\[[^\\]]{1,30}\\]\\s*", "["},                   // [Denver]
      {"^\\s*(\\([A-Za-z0-9 ]{1,14}\\)\\s*)+", "("},          // US (English) ...
      {"\\s*\\((A|H|D|S|P|SP|RK|GT|DZ|XV|F|X|Prime|SD|HD|FHD|UHD|4K|Backup)\\)", "("},
      {"\\s*\\(\\d{3,4}p[^)]*\\)", "("},  // (720p29 @ 3.2Mbps)
      {"\\b(FHD|UHD|4K|8K|HEVC|RAW|VIP|60FPS|50FPS)\\b", nullptr},
      {"\\bBackup\\b", nullptr},
      {"\\s{2,}", nullptr}};
  constexpr size_t kCount = sizeof(kPatterns) / sizeof(kPatterns[0]);
  for (size_t i = 0; i < kCount; ++i) {
    if (kPatterns[i].needs && !hasAnyOf(s, kPatterns[i].needs)) continue;
    s = rx::replace(s, kPatterns[i].pattern, i == kCount - 1 ? " " : "", true);
  }
  std::string trimmed(str::trimUnicode(s, false, ":|-"));
  return trimmed.empty() ? std::string(name) : trimmed;
}

std::optional<int> virtualNumber(std::string_view name) {
  auto n = text::firstGroup(
      "\\b(?:ABC|CBS|NBC|FOX|PBS|CW|MyTV|MyNet|IND|TMO|Univision|Telemundo|UniMas)\\s+(\\d{1,2})"
      "\\b",
      name, 1, true);
  return n ? text::toInt(*n) : std::nullopt;
}

}  // namespace marigold::catalog
