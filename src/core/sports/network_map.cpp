#include "core/sports/network_map.hpp"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>

#include "core/catalog/catalog.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/text.hpp"

namespace marigold::sports {

namespace {

// "KTVU-DT" -> "KTVU".
std::string baseSign(std::string_view sign) {
  size_t dash = sign.find('-');
  return str::upper(dash == std::string_view::npos ? sign : sign.substr(0, dash));
}

std::set<std::string> bases(const std::vector<std::string>& list) {
  std::set<std::string> out;
  for (const std::string& s : list) out.insert(baseSign(s));
  return out;
}

bool intersects(const std::set<std::string>& a, const std::set<std::string>& b) {
  for (const std::string& s : a) {
    if (b.count(s)) return true;
  }
  return false;
}

}  // namespace

std::optional<std::string> homeMarketName(const catalog::LineupPrefs& prefs) {
  if (prefs.homeMarket) {
    for (const auto& m : prefs.markets) {
      if (m.name == *prefs.homeMarket) return m.name;
    }
  }
  if (!prefs.markets.empty()) return prefs.markets.front().name;
  return std::nullopt;
}

StationMarkets stationMarkets(const catalog::LineupPrefs& prefs) {
  StationMarkets result;
  std::optional<std::string> homeName = homeMarketName(prefs);
  std::vector<std::string> homeSigns, otherSigns;
  bool foundHome = false;
  for (const auto& m : prefs.markets) {
    if (!foundHome && homeName && m.name == *homeName) {
      homeSigns = m.callsigns;
      foundHome = true;
    }
    if (!homeName || m.name != *homeName) otherSigns.insert(otherSigns.end(), m.callsigns.begin(), m.callsigns.end());
  }
  result.home = bases(homeSigns);
  for (const std::string& sign : bases(otherSigns)) {
    if (!result.home.count(sign)) result.others.insert(sign);
  }
  return result;
}

std::optional<std::string> nextHomeMarket(const catalog::LineupPrefs& prefs) {
  if (prefs.markets.size() <= 1) return std::nullopt;
  std::optional<std::string> home = homeMarketName(prefs);
  size_t index = 0;
  for (size_t i = 0; i < prefs.markets.size(); ++i) {
    if (home && prefs.markets[i].name == *home) {
      index = i;
      break;
    }
  }
  return prefs.markets[(index + 1) % prefs.markets.size()].name;
}

int stationRank(const catalog::EntryInfo& info, const StationMarkets& markets, bool inLineup) {
  int place = station_rank::elsewhere;
  if (info.category == catalog::LineupCategory::local ||
      info.category == catalog::LineupCategory::spanish) {
    std::vector<std::string> signs;
    if (info.callsign) {
      std::string current;
      for (char c : *info.callsign) {
        if (c == ' ' || c == ',') {
          if (!current.empty()) signs.push_back(current);
          current.clear();
        } else {
          current += c;
        }
      }
      if (!current.empty()) signs.push_back(current);
    }
    if (auto sign = regional::callsignInName(info.name)) signs.push_back(*sign);
    std::set<std::string> found = bases(signs);
    if (intersects(found, markets.home)) {
      place = station_rank::home;
    } else if (intersects(found, markets.others)) {
      place = station_rank::market;
    }
  }
  if (!inLineup) return place;
  return place == station_rank::home ? station_rank::lineupHome : station_rank::lineup;
}

std::string stationDetail(int rank) {
  if (rank <= station_rank::lineup) return "In My Lineup";
  if (station_rank::isLocal(rank)) return "Your local station";
  return "In your channels";
}

// MARK: Regional networks

namespace regional {

namespace {

struct Region {
  std::vector<std::string> words;
  std::string key;
};

// Regions of the FanDuel (formerly Bally, formerly FOX Sports) networks,
// longest first so "southeast" wins over "south".
const std::vector<Region>& fanDuelRegions() {
  static const std::vector<Region> kRegions = {
      {{"southeast", "se"}, "southeast"}, {{"southwest", "sw"}, "southwest"}, {{"sun"}, "sun"},
      {{"carolinas", "carolina"}, "carolinas"}, {{"prime ticket"}, "primeticket"},
      {{"socal", "west"}, "socal"}, {{"san diego"}, "sandiego"}, {{"new orleans"}, "neworleans"},
      {{"kansas city", "kc"}, "kansascity"}, {{"great lakes"}, "greatlakes"},
      {{"florida", "fl"}, "florida"}, {{"detroit"}, "detroit"}, {{"north"}, "north"},
      {{"ohio"}, "ohio"}, {{"midwest"}, "midwest"}, {{"oklahoma"}, "oklahoma"},
      {{"wisconsin"}, "wisconsin"}, {{"indiana"}, "indiana"}, {{"arizona"}, "arizona"},
      {{"cincinnati"}, "cincinnati"}, {{"tennessee"}, "south"}, {{"south"}, "south"},
  };
  return kRegions;
}

// Other regional networks: any of the phrases -> key ("" = not a network).
const std::vector<Region>& others() {
  static const std::vector<Region> kOthers = {
      {{"nbcs ba", "nbcsba", "nbc sports bay area", "nbcs bay area"}, "nbcs-ba"},
      {{"nbcs ca", "nbcsca", "nbc sports california", "nbcs california"}, "nbcs-ca"},
      {{"nbcs ph", "nbcsp", "nbc sports philadelphia", "nbcs philadelphia", "nbcs philly"}, "nbcs-ph"},
      {{"nbcs bos", "nbc sports boston", "nbcs boston"}, "nbcs-bos"},
      {{"msg2", "msg 2", "msg plus", "msg+"}, "msg2"},
      {{"msg sportsnet", "msg sn", "msg network", "msg"}, "msg"},
      {{"yes network", "yes hd", "dazn yes"}, "yes"},
      {{"nesn"}, "nesn"},
      {{"sny"}, "sny"},
      {{"marquee sports"}, "marquee"},
      {{"altitude"}, "altitude"},
      {{"monumental", "mnmt"}, "monumental"},
      {{"space city", "schn"}, "spacecity"},
      {{"sportsnet la", "spectrum sportsnet la", "snla"}, "snla"},
      {{"spectrum sportsnet", "spectrum sn"}, "spectrum"},
      {{"root sports", "root sports northwest"}, "root"},
      {{"sportsnet pittsburgh", "sportsnet pit", "att sportsnet pittsburgh",
        "at t sportsnet pittsburgh"},
       "snp"},
      {{"att sportsnet rocky mountain", "at t sportsnet rocky mountain", "root sports rocky mountain"},
       "rockymountain"},
      {{"chsn", "chicago sports network"}, "chsn"},
      {{"gulf coast sports", "gcsen"}, "gcsen"},
      {{"mlb tv"}, ""},
  };
  return kOthers;
}

bool has(const std::string& text, const std::string& phrase) {
  return text.find(" " + phrase + " ") != std::string::npos;
}

// `text`: lower-cased words between spaces (" spectrum sportsnet ").
std::optional<std::string> keyNormalized(const std::string& text) {
  // Bare names as the schedule writes them.
  if (text == " yes ") return std::string("yes");
  if (text == " marquee ") return std::string("marquee");
  // Provider names mix the two LA networks up: the team decides.
  if (has(text, "sportsnet") || has(text, "spectrum")) {
    if (has(text, "dodgers")) return std::string("snla");
    if (has(text, "lakers")) return std::string("spectrum");
  }
  if (has(text, "fdsn") || has(text, "fanduel") || has(text, "bally") || has(text, "fox sports") ||
      has(text, "fsn") || has(text, "fanduel sn")) {
    for (const Region& region : fanDuelRegions()) {
      for (const std::string& w : region.words) {
        if (has(text, w)) return "fd-" + region.key;
      }
    }
    return std::nullopt;
  }
  for (const Region& item : others()) {
    for (const std::string& p : item.words) {
      if (has(text, p)) {
        if (item.key.empty()) return std::nullopt;
        return item.key;
      }
    }
  }
  return std::nullopt;
}

const std::unordered_set<std::string>& notCallsigns() {
  static const std::unordered_set<std::string> kWords = {
      "WITH", "WORLD", "WEST", "WILD", "KIDS", "KING", "WHO",  "WEB",  "KISS", "WOW",
      "WAR",  "WIRE",  "WINE", "WORK", "WOMAN", "WOMEN", "WHL", "WDR",  "WOR",  "KBS",
      "WEEK", "KNOW",  "WALK", "WORD", "WAVE", "WARM",  "WALL", "WIND", "WISE",
  };
  return kWords;
}

}  // namespace

std::optional<std::string> key(std::string_view name) {
  std::string lowered = str::replaceAll(str::lower(name), "&", "");
  std::string spaced = rx::replace(lowered, "[^\\p{L}\\p{N}\\p{M}+]+", " ");
  std::string text = " " + std::string(str::trim(spaced)) + " ";
  return keyNormalized(str::replaceAll(text, " sports net ", " sportsnet "));
}

std::optional<std::string> callsignInName(std::string_view name) {
  if (auto m = rx::first("\\((?:[A-Za-z]+\\s+)?([KW][A-Z]{2,3})(?:[- ](?:TV|DT|LD|CD|CA|D\\d))?\\s*\\)", name)) {
    return std::string(m->group(1));
  }
  // After a state code: "UT KJZZ".
  if (auto m = rx::first("\\b[A-Z]{2}\\s+([KW][A-Z]{2,3})\\b", name)) return std::string(m->group(1));
  if (auto m = rx::first("(?:^|[\\s:])([KW][A-Z]{2,3})(?=\\s|$|-(?:TV|DT|LD|CD))", name)) {
    std::string sign(m->group(1));
    if (!notCallsigns().count(sign)) return sign;
  }
  return std::nullopt;
}

std::optional<std::string> callsign(std::string_view name) {
  std::string upper(str::trimUnicode(str::upper(name)));
  auto m = rx::first("^[KW][A-Z]{2,3}(?=$|[- ](?:TV|DT|LD|CD|CA)$|[- ](?:TV |DT )?\\d{1,2}$)", upper);
  if (!m) return std::nullopt;
  std::string sign(m->group(0));
  // "WEST 2" is a word and a number, not a station ("KING 5" is one).
  if (!upper.empty() && upper.back() >= '0' && upper.back() <= '9' && sign != "KING" &&
      notCallsigns().count(sign)) {
    return std::nullopt;
  }
  return sign;
}

std::optional<std::string> stationBrand(std::string_view name) {
  static const std::unordered_map<std::string, std::string> kBrands = {
      {"arizonasfamilycbs5", "KPHO"}, {"azfamilycbs5", "KPHO"},
      {"arizonasfamily3tv", "KTVK"},  {"azfamily3tv", "KTVK"},
  };
  std::string key;
  for (char c : str::lowerAscii(name)) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) key += c;
  }
  auto it = kBrands.find(key);
  if (it == kBrands.end()) return std::nullopt;
  return it->second;
}

}  // namespace regional

// MARK: Network map

namespace network_map {

namespace {

// Short names used in TV listings for networks, as the channel is named.
const std::unordered_map<std::string, std::string>& networkAliases() {
  static const std::unordered_map<std::string, std::string> kAliases = {
      {"nfl net", "NFL Network"},      {"nfl netwrk", "NFL Network"}, {"mlb net", "MLB Network"},
      {"nhl net", "NHL Network"},      {"usa net", "USA Network"},    {"usa", "USA Network"},
      {"cbssn", "CBS Sports Network"}, {"btn", "Big Ten Network"},    {"secn", "SEC Network"},
      {"accn", "ACC Network"},         {"espnews", "ESPNews"},        {"golf", "Golf Channel"},
  };
  return kAliases;
}

const std::set<std::string>& broadcastNetworks() {
  static const std::set<std::string> kNetworks = {"ABC", "CBS",       "NBC",       "FOX",
                                                  "CW",  "TELEMUNDO", "UNIVISION", "UNIMAS"};
  return kNetworks;
}

std::string topCounts(const std::map<std::string, int>& counts) {
  std::vector<std::pair<std::string, int>> list(counts.begin(), counts.end());
  std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
    return a.second != b.second ? a.second > b.second : a.first < b.first;
  });
  std::vector<std::string> parts;
  for (size_t i = 0; i < list.size() && i < 12; ++i) {
    parts.push_back(list[i].first + " " + std::to_string(list[i].second));
  }
  return str::join(parts, ", ");
}

}  // namespace

std::optional<std::string> networkKey(std::string_view name) {
  if (auto sign = regional::stationBrand(name)) return "call:" + *sign;
  std::string key = catalog::coreKey(name).key;
  auto alias = networkAliases().find(key);
  if (alias != networkAliases().end()) return "core:" + alias->second;
  if (auto core = catalog::coreMatch(name)) return "core:" + core->name;
  std::string word = str::upper(str::replaceAll(key, " ", ""));
  if (broadcastNetworks().count(word)) return "net:" + word;
  if (auto region = regional::key(name)) return "rsn:" + *region;
  if (auto sign = regional::callsign(name)) return "call:" + *sign;
  return std::nullopt;
}

std::string ChannelIndex::summary() const {
  return std::to_string(byKey.size()) + " keys, " + std::to_string(regional) + " regional, " +
         std::to_string(callsigns) + " stations by call sign, " + std::to_string(teamChannels) +
         " team channels, " + std::to_string(fightChannels) + " fight card channels";
}

ChannelIndex channelIndex(const std::set<std::string>& lineup, const std::vector<Channel>& lineupEntries,
                          const std::vector<Channel>& browse, const std::vector<Channel>& all,
                          const std::unordered_map<std::string, catalog::EntryInfo>& entryInfo,
                          const std::set<std::string>& hidden,
                          const std::vector<matcher::RosterLeague>& rosters,
                          const StationMarkets& markets) {
  using catalog::LineupCategory;
  ChannelIndex index;
  // Team channels ("NBA Miami Heat", "Atlanta Falcons", "Spectrum SportsNet
  // Lakers"): pro and soccer teams, one team named.
  std::vector<std::pair<std::string, matcher::TeamIndex>> teamLists;
  for (const matcher::RosterLeague& roster : rosters) {
    if (!roster.college) {
      teamLists.emplace_back(roster.shortName, matcher::TeamIndex::fromTeams(roster.teams, false));
    }
  }
  std::unordered_set<std::string> seen;
  const std::vector<Channel>* passes[] = {&lineupEntries, &browse, &all};
  for (int pass = 0; pass < 3; ++pass) {
    for (const Channel& entry : *passes[pass]) {
      if (!seen.insert(entry.id).second) continue;
      auto found = entryInfo.find(entry.id);
      if (found == entryInfo.end()) continue;
      const catalog::EntryInfo& info = found->second;
      if (hidden.count(entry.group.value_or("Other")) || info.kind != catalog::ChannelKind::channel) {
        continue;
      }
      // From the full list, only sports channels of unknown country.
      if (pass == 2 && (info.market || info.category != LineupCategory::sports)) continue;
      bool spanish = info.category == LineupCategory::spanish || info.market == std::string("US-ES");
      bool inLineup = lineup.count(entry.id) > 0;
      ChannelIndex::Item item{entry.id, inLineup, spanish, stationRank(info, markets, inLineup)};
      auto add = [&](const std::string& key) { index.byKey[key].push_back(item); };
      if (info.core) {
        add("core:" + *info.core);
        continue;
      }
      if (info.category != LineupCategory::sports && info.category != LineupCategory::local &&
          info.category != LineupCategory::spanish) {
        continue;
      }
      if (info.category == LineupCategory::local) {
        if (auto word = catalog::networkWord(info.name)) add("net:" + *word);
      }
      if (auto region = regional::key(info.name)) {
        add("rsn:" + *region);
        index.regional += 1;
      }
      if (info.category == LineupCategory::local) {
        std::optional<std::string> sign;
        if (info.callsign) sign = regional::callsign(*info.callsign);
        if (!sign) sign = regional::callsignInName(info.name);
        if (sign) {
          add("call:" + *sign);
          index.callsigns += 1;
        }
      }
      // A promotion's own channel for a part of its cards ("UFC PPV Main
      // Card", "UFC PPV Prelims"): no event number in the name.
      if (info.category == LineupCategory::sports &&
          rx::test("^ (ppv )?(ufc|pfl|bkfc) ", matcher::normalized(info.name))) {
        auto key = matcher::fightKey(info.name);
        if (key && key->number.empty() && key->part) {
          add("card:" + key->org + "|" + *key->part);
          index.fightChannels += 1;
        }
      }
      if (info.category == LineupCategory::sports) {
        std::vector<std::string> named;
        for (const auto& list : teamLists) {
          if (auto team = list.second.single(info.name, list.first)) {
            named.push_back("team:" + list.first + "|" + team->id);
          }
        }
        if (named.size() == 1) {
          add(named.front());
          index.teamChannels += 1;
        }
      }
    }
  }
  return index;
}

std::string SourceStats::summary() const {
  return std::to_string(national) + " national, " + std::to_string(regional) + " regional, " +
         std::to_string(station) + " by call sign, " + std::to_string(team) +
         " team channel links, " + std::to_string(fightCards) + " fight card channel links, " +
         std::to_string(split) + " regional splits waiting for the guide; not in your channels: [" +
         topCounts(missing) + "]; unknown names: [" + topCounts(unknown) + "]";
}

NetworkSources networkSources(const std::vector<SportsGame>& games, const ChannelIndex& index) {
  NetworkSources out;
  SourceStats& stats = out.stats;
  if (games.empty() || index.byKey.empty()) return out;
  std::unordered_map<std::string, std::optional<std::string>> keys;
  auto cachedKey = [&](const std::string& network) -> std::optional<std::string> {
    auto it = keys.find(network);
    if (it != keys.end()) return it->second;
    auto value = networkKey(network);
    keys.emplace(network, value);
    return value;
  };
  // Broadcast networks split some games by region (several NFL games on
  // FOX at 1 PM): those only count when a guide listing confirms which one
  // your station has.
  std::map<std::string, std::vector<Seconds>> byNetwork;
  for (const SportsGame& game : games) {
    if (game.completed || game.networks.empty()) continue;
    auto key = cachedKey(game.networks.front());
    if (key && str::startsWith(*key, "net:")) byNetwork[*key].push_back(game.start);
  }
  for (const SportsGame& game : games) {
    if (game.completed) continue;
    std::vector<EventSource> sources;
    // Best first (My Lineup, home market, your markets, elsewhere);
    // `nearby` keeps only your own markets' stations (three at most),
    // otherwise two.
    auto take = [&](const std::vector<ChannelIndex::Item>& entries, bool nearby, bool team,
                    const std::string& teamDetail, const std::string& teamLineupDetail) {
      std::vector<ChannelIndex::Item> ranked;
      for (const auto& item : entries) {
        if (!nearby || item.rank <= station_rank::market) ranked.push_back(item);
      }
      std::stable_sort(ranked.begin(), ranked.end(),
                       [](const auto& a, const auto& b) { return a.rank < b.rank; });
      size_t limit = nearby ? 3 : 2;
      for (size_t i = 0; i < ranked.size() && i < limit; ++i) {
        const auto& item = ranked[i];
        bool known = std::any_of(sources.begin(), sources.end(),
                                 [&](const EventSource& s) { return s.channelID == item.id; });
        if (known) continue;
        EventSource source;
        source.channelID = item.id;
        source.kind = team ? EventSource::Kind::team
                           : (item.inLineup ? EventSource::Kind::lineup : EventSource::Kind::guide);
        source.spanish = item.spanish;
        source.detail = team ? (item.inLineup ? teamLineupDetail : teamDetail) : stationDetail(item.rank);
        source.rank = item.rank;
        sources.push_back(std::move(source));
      }
    };
    for (const std::string& network : game.networks) {
      auto key = cachedKey(network);
      if (!key) {
        stats.unknown[network] += 1;
        continue;
      }
      auto entries = index.byKey.find(*key);
      if (entries == index.byKey.end()) {
        stats.missing[network] += 1;
        continue;
      }
      if (str::startsWith(*key, "net:")) {
        // Several games on the network at once (regional coverage): only a
        // station's guide can say which one it has.
        const auto& starts = byNetwork[*key];
        auto together = std::count_if(starts.begin(), starts.end(),
                                      [&](Seconds s) { return std::llabs(s - game.start) < 90 * 60; });
        if (together > 1) {
          stats.split += 1;
          continue;
        }
        // A national game: your own markets' stations.
        take(entries->second, true, false, "", "");
        stats.national += 1;
      } else {
        // A network or station that is the broadcast itself (a sports
        // network, a regional network, a station): wherever it is.
        take(entries->second, false, false, "", "");
        if (str::startsWith(*key, "rsn:")) {
          stats.regional += 1;
        } else if (str::startsWith(*key, "call:")) {
          stats.station += 1;
        } else {
          stats.national += 1;
        }
      }
    }
    for (const SportsTeam* team : {&game.home, &game.away}) {
      auto entries = index.byKey.find("team:" + game.leagueShort + "|" + team->id);
      if (entries == index.byKey.end()) continue;
      take(entries->second, false, true, "Team channel", "Team channel \xC2\xB7 In My Lineup");
      stats.team += 1;
    }
    // A numbered card's part on the promotion's own channel for it.
    if (game.cardKey) {
      std::vector<std::string> card = text::components(*game.cardKey, "|");
      if (card.size() == 3 && !card[1].empty()) {
        auto entries = index.byKey.find("card:" + card[0] + "|" + (card[2].empty() ? "main" : card[2]));
        if (entries != index.byKey.end()) {
          take(entries->second, false, true, "Fight channel", "Fight channel \xC2\xB7 In My Lineup");
          stats.fightCards += 1;
        }
      }
    }
    if (!sources.empty()) out.sources[game.id] = std::move(sources);
  }
  return out;
}

}  // namespace network_map

}  // namespace marigold::sports
