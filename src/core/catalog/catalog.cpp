#include "core/catalog/catalog.hpp"

#include <algorithm>
#include <map>
#include <unordered_set>

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
  if (rx::test("^\\s*\\(?(nfl|nba|nhl|mlb|mls|wnba)\\)?\\s+(\\([A-Z]{2,4}\\)\\s+)?[A-Za-z]",
               display, true)) {
    return "team";
  }
  if (auto place = text::firstGroup(regionPattern(), display, 1, true)) {
    return "regional:" + str::lower(*place);
  }
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
                   "yahoo finance", "scripps", "euronews", "al jazeera", "local now"});
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
    std::string group = groupOf(channel);
    bool strong = contains(str::lower(group), "local") ||
                  rx::test("^\\s*US\\s*\\|\\s*(NBC|FOX|CBS|ABC|CW|PBS)\\b", group);
    std::optional<std::string> network = networkWord(channel.name);
    for (const CallsignCandidate& candidate : callsignCandidates(channel.name)) {
      if (strong || (network && candidate.inParens)) known.insert(candidate.sign);
      if (network) counts[candidate.sign][*network] += 3;
    }
  }
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

// MARK: Build

CatalogResult build(const std::vector<Channel>& channels, time::Seconds now,
                    const std::vector<guide::Station>& stations) {
  CatalogResult result;
  result.traits.reserve(channels.size());
  // Pass 0: which call signs are real here, and each sign's usual network.
  CallsignContext context(channels, stations);
  // Pass 1: traits and candidate event families.
  std::unordered_map<std::string, int> familyCounts;
  std::unordered_map<std::string, int> familySeparators;
  for (const Channel& channel : channels) {
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
    if (str::startsWith(key, "grp:")) return true;
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
      traits.familyKey.reset();
      traits.familyNumber.reset();
      traits.eventText.reset();
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

  // Pass 2: entries (clusters of variants), in first-seen order.
  std::unordered_map<std::string, std::vector<const Channel*>> clusters;
  std::vector<std::string> order;
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
    auto& members = clusters[traits.clusterKey];
    if (members.empty()) order.push_back(traits.clusterKey);
    members.push_back(&channel);
  }
  result.entries.reserve(order.size());
  for (const std::string& key : order) {
    std::vector<const Channel*>& members = clusters[key];
    if (members.empty()) continue;
    const ChannelTraits& traits = result.traits[members.front()->id];
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
    if (traits.core) {
      entry.name = traits.isWest ? *traits.core + " West" : *traits.core;
    } else {
      std::vector<std::string> names;
      names.reserve(ranked.size());
      for (const Ranked& r : ranked) names.push_back(r.traits->displayName);
      entry.name = bestDisplayName(names);
    }
    for (const Ranked& r : ranked) entry.members.push_back(r.channel->id);
    entry.category = traits.category;
    entry.callsign = traits.callsign;
    entry.country = traits.country;
    entry.virtualNumber = virtualNumber(entry.name);
    entry.kind = traits.kind;
    entry.market = traits.market;
    entry.core = traits.core;
    entry.scope = traits.scope;
    for (const std::string& member : entry.members) result.entryOf[member] = entry.id;
    result.entries.push_back(std::move(entry));
  }

  // Pass 3: event families and events.
  EventsResult events = buildEvents(channels, result.traits, now);
  result.families = std::move(events.families);
  result.events = std::move(events.events);
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

ChannelTraits traitsFor(const Channel& channel, const CallsignContext& context) {
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
  bool isWest = rx::test("\\b(west|pacific)\\b", lower);
  std::optional<std::string> country =
      countryFor(name, lower, rawGroup, group, str::startsWith(channel.serverId, "xt:"));
  std::optional<std::string> market = country;
  if (country == std::string("US") && isSpanishUS(name, group)) market = "US-ES";
  std::string display = cleanName(name);
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

  // Event family?
  std::optional<std::string> familyKey;
  std::optional<int> familyNumber;
  std::optional<std::string> eventText;
  bool hasSeparator = false;
  bool isLocalish =
      contains(group, "local") || (hasAnyOf(lower, "(") && rx::test("\\([kw][a-z]{2,3}\\b", lower));
  if (!core && kind != ChannelKind::separator && (!isLocalish || kind == ChannelKind::event)) {
    if (auto family = familyParts(name)) {
      familyKey = family->key;
      familyNumber = family->number;
      eventText = family->rest;
      hasSeparator = family->separator;
    }
  }

  // Call sign: only real ones (station directory, local groups, or written
  // next to a network name), never for other countries.
  std::optional<std::string> callsign;
  if (kind == ChannelKind::channel && !core && !familyKey && (!country || *country == "US")) {
    bool localGroup = contains(group, "local");
    std::vector<CallsignCandidate> candidates;
    for (CallsignCandidate& c : callsignCandidates(name)) {
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
    // "FOX 2 LOCAL SAN FRANCISCO": network + city -> the station.
    if (!callsign && stationLike() && !context.stationsByNetwork.empty()) {
      callsign = guide::stationCallsign(display, context.stationsByNetwork);
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
      } else if (callsign || (str::startsWith(scope, "regional") && stationLike())) {
        category = LineupCategory::local;
      } else {
        category = categoryFor(lower, group, country, false);
      }
      break;
  }

  ChannelTraits traits;
  traits.clusterKey = std::move(key);
  traits.displayName = core ? (isWest ? core->name + " West" : core->name) : display;
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
  return traits;
}

// MARK: Classification

bool isEventGroup(std::string_view group) {
  return rx::test(
      "\\bppv\\b|event|league pass|espn ?\\+|espn "
      "plus|\\bdazn\\b|\\bflo|ncaa|uefa|formula|\\bf1\\b|"
      "hbo "
      "max|vidio|monomax|\\bnfl\\b|\\bnba\\b|\\bnhl\\b|\\bmlb\\b|\\bmls\\b|\\bwnba\\b|\\bmilb\\b|"
      "^football$|replay|sport on air",
      group);
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
  if (contains(name, "24/7") || contains(group, "24/7") ||
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

std::vector<CallsignCandidate> callsignCandidates(std::string_view name) {
  std::string up = str::upper(name);
  std::vector<CallsignCandidate> found;
  for (const rx::Match& m :
       rx::all("(?<![A-Z0-9])([KW][A-Z]{2,3})(?:[- ]?(DT|TV|LD|LP|CD|CA)\\d?)?(?![A-Z0-9])", up)) {
    std::string sign(m.group(1));
    size_t at = static_cast<size_t>(m.group(1).data() - up.data());
    bool inParens = at > 0 && up[at - 1] == '(';
    // Three-letter call signs (KGO, WGN) only in parentheses or with a suffix.
    if (!(sign.size() == 4 || inParens || m.has(2))) continue;
    bool seen = std::any_of(found.begin(), found.end(),
                            [&](const CallsignCandidate& c) { return c.sign == sign; });
    if (!seen) found.push_back({sign, inParens});
  }
  return found;
}

std::optional<std::string> networkWord(std::string_view name) {
  auto word = text::firstGroup(
      "\\b(ABC|CBS|NBC|FOX|CW|PBS|MYTV|MyNetworkTV|ION|Telemundo|Univision|UniMas|Independent|IND|"
      "TMO)\\b",
      name, 1, true);
  if (!word) return std::nullopt;
  std::string up = str::upper(*word);
  if (up == "TMO") return std::string("TELEMUNDO");
  if (up == "MYNETWORKTV") return std::string("MYTV");
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
  std::string key = coreKey(display).key;
  const auto& byAlias = coreByAlias();
  auto hit = byAlias.find(key);
  if (hit != byAlias.end()) return hit->second;
  // "NAT GEO WILD HD US": a leading / trailing US marker.
  std::vector<std::string> parts;
  for (std::string_view t : str::split(key, ' ')) {
    if (t != "us" && t != "usa") parts.emplace_back(t);
  }
  std::string trimmed = str::join(parts, " ");
  if (trimmed != key && !trimmed.empty()) {
    auto again = byAlias.find(trimmed);
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
                    "\\x{24B6}-\\x{24E9}\\x{02B0}-\\x{02FF}ⓧ]",
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
      {"^\\s*(USA?|US|REG|MEX|MX|UK|CA)\\s*[:|]\\s*", ":|"},  // US: / USA |
      {"^\\s*(USA?|US)\\s+", "Uu"},                           // US / USA
      {"^\\s*\\[[^\\]]{1,30}\\]\\s*", "["},                   // [Denver]
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
