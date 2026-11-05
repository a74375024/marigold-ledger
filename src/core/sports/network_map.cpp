#include "core/sports/network_map.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

#include "core/catalog/catalog.hpp"
#include "core/sports/languages.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/my_sports.hpp"
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
  if (rank == station_rank::abroad) return "From another country";
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
// longest first so "southeast" wins over "south". Build 143: "West" (FOX
// Sports West: Kings, Angels) and "SoCal" (Prime Ticket: Clippers, Ducks)
// are two networks, and "West" is read last, after "south west" and "mid
// west" are joined up ("FOX SPORTS MID WEST" is Midwest). SportsTime Ohio
// was FOX Sports Great Lakes.
const std::vector<Region>& fanDuelRegions() {
  static const std::vector<Region> kRegions = {
      {{"southeast", "se"}, "southeast"}, {{"southwest", "sw"}, "southwest"}, {{"sun"}, "sun"},
      {{"carolinas", "carolina"}, "carolinas"}, {{"socal", "prime ticket"}, "socal"},
      {{"san diego"}, "sandiego"}, {{"new orleans"}, "neworleans"},
      {{"kansas city", "kc"}, "kansascity"}, {{"great lakes", "sportstime ohio", "sportstime"}, "greatlakes"},
      {{"florida", "fl"}, "florida"}, {{"detroit"}, "detroit"}, {{"north"}, "north"},
      {{"ohio"}, "ohio"}, {{"midwest"}, "midwest"}, {{"oklahoma"}, "oklahoma"},
      {{"wisconsin"}, "wisconsin"}, {{"indiana"}, "indiana"}, {{"arizona"}, "arizona"},
      {{"cincinnati"}, "cincinnati"}, {{"tennessee"}, "south"}, {{"south"}, "south"},
      {{"west"}, "west"},
  };
  return kRegions;
}

// Other regional networks: any of the phrases -> key ("" = not a network).
// Build 143: MSG's networks apart (MSG Network: Knicks, Rangers; MSG
// Sportsnet, formerly MSG+: Devils, Islanders; MSG Western New York:
// Sabres), MASN, SNY by its full name, Marquee written "Sports Net", ABTV,
// Midco Sports (not its streaming Plus).
const std::vector<Region>& others() {
  static const std::vector<Region> kOthers = {
      {{"nbcs ba", "nbcsba", "nbc sports bay area", "nbcs bay area"}, "nbcs-ba"},
      {{"nbcs ca", "nbcsca", "nbc sports california", "nbcs california"}, "nbcs-ca"},
      {{"nbcs ph", "nbcsp", "nbc sports philadelphia", "nbc sports philadelfia", "nbcs philadelphia",
        "nbcs philly", "nbc sports phil"},
       "nbcs-ph"},
      {{"nbcs bos", "nbc sports boston", "nbcs boston"}, "nbcs-bos"},
      {{"msg sportszone", "msg sports zone"}, ""},
      {{"msg sportsnet", "msg sn", "msgsn", "msg plus", "msg+"}, "msgsn"},
      {{"msg2", "msg 2", "ms2"}, "msg2"},
      {{"msgb", "msg buffalo", "msg western new york", "msg wny"}, "msgb"},
      {{"msg network", "madison square garden", "msg"}, "msg"},
      {{"yes network", "yes hd", "dazn yes"}, "yes"},
      {{"nesn"}, "nesn"},
      {{"sny", "sportsnet new york"}, "sny"},
      {{"marquee sports", "marquee sportsnet"}, "marquee"},
      {{"mid atlantic sports", "masn", "masn2"}, "masn"},
      {{"altitude"}, "altitude"},
      {{"monumental", "mnmt", "mnmt2"}, "monumental"},
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
      {{"sportstime ohio"}, "fd-greatlakes"},
      {{"abtv", "angels broadcast"}, "abtv"},
      {{"midco sports plus"}, ""},
      {{"midco sports"}, "midco"},
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
      has(text, "fsn") || has(text, "fanduel sn") || has(text, "prime sports")) {
    for (const Region& region : fanDuelRegions()) {
      for (const std::string& w : region.words) {
        if (has(text, w)) return "fd-" + region.key;
      }
    }
    // Build 143: no region ("FOX SPORTS YES HD"): another network's, or none.
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

// The second byte of a two-byte UTF-8 Latin-1 letter (after 0xC3), as a
// plain letter ("á" -> 'a', "Ñ" -> 'n'); 0 for anything else.
char foldLatin(unsigned char b) {
  if (b >= 0xA0 && b <= 0xBF) b = static_cast<unsigned char>(b - 0x20);
  if (b >= 0x80 && b <= 0x85) return 'a';
  if (b == 0x87) return 'c';
  if (b >= 0x88 && b <= 0x8B) return 'e';
  if (b >= 0x8C && b <= 0x8F) return 'i';
  if (b == 0x91) return 'n';
  if (b >= 0x92 && b <= 0x96) return 'o';
  if (b >= 0x99 && b <= 0x9C) return 'u';
  if (b == 0x9D) return 'y';
  return 0;
}

bool asciiAlnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

// What a broadcaster's name is: a network's or station's key ("core:Golf
// Channel", "rsn:msgsn", "call:KUPX", "net:TELEMUNDO"), a streaming service
// (no channel), or neither ("" and not streaming: a name known to name no
// channel of yours).
struct Broadcaster {
  std::string key;
  bool streaming = false;
};

// Build 143: broadcaster names as the schedule writes them, and the names
// of channels that are those networks, in one table, read by their letters
// and digits only (folded()): spellings the rules don't read ("Golf Chnl",
// "MSGSN", "FanDuel SN WI", "Utah 16", "UniMás") and the streaming services
// and team apps the schedule names beside them.
const std::unordered_map<std::string, Broadcaster>& broadcasters() {
  struct Row {
    const char* name;
    const char* key;
  };
  // clang-format off
  static const Row kRows[] = {
      // National networks.
      {"Golf Chnl", "core:Golf Channel"},
      {"Ryz Sports Network", "core:Ryz Sports"}, {"Ryz Sports", "core:Ryz Sports"},
      {"NBCSN", "core:NBCSN"}, {"NBC Sports Network", "core:NBCSN"}, {"NBC SN", "core:NBCSN"},
      // ION is one national feed (its stations carry it whole): wherever it is.
      {"ION", "core:ION"},
      // Spanish-language networks.
      {"ESPN Deportes", "core:ESPN Deportes"},
      {"Universo", "core:NBC Universo"}, {"NBC Universo", "core:NBC Universo"},
      {"Tele", "net:TELEMUNDO"}, {"UniMás", "net:UNIMAS"},
      {"Fox Deportes", "core:Fox Deportes"}, {"Fox Sports Deportes", "core:Fox Deportes"},
      {"TUDN", "core:TUDN"}, {"TUDN USA", "core:TUDN"},
      {"Galavisión", "core:Galavision"},
      {"beIN Sports en Español", "core:beIN Sports en Espanol"},
      {"beIN Sports Español", "core:beIN Sports en Espanol"},
      {"beIN Español", "core:beIN Sports en Espanol"},
      // Regional networks as the schedule shortens them.
      {"NBC Sports Phil", "rsn:nbcs-ph"}, {"NBC Sports Phil +", "rsn:nbcs-ph"},
      {"NBC Sports BA", "rsn:nbcs-ba"}, {"NBC Sports BA +", "rsn:nbcs-ba"},
      {"NBC Sports CA", "rsn:nbcs-ca"}, {"NBC Sports CA +", "rsn:nbcs-ca"}, {"NBCSCA+", "rsn:nbcs-ca"},
      {"NBC Sports BO", "rsn:nbcs-bos"},
      {"MSGSN", "rsn:msgsn"}, {"MSGB", "rsn:msgb"}, {"MS2", "rsn:msg2"},
      {"MNMT2", "rsn:monumental"},
      {"Marquee Sports Net", "rsn:marquee"},
      {"MASN", "rsn:masn"}, {"MASN2", "rsn:masn"},
      {"SNY", "rsn:sny"}, {"YES", "rsn:yes"},
      {"ABTV", "rsn:abtv"}, {"Midco Sports Net", "rsn:midco"},
      {"FanDuel SN IN", "rsn:fd-indiana"},
      {"FanDuel SN WI", "rsn:fd-wisconsin"}, {"FanDuel SN WIX", "rsn:fd-wisconsin"},
      {"FanDuel SN OK", "rsn:fd-oklahoma"},
      {"FanDuel SN DET", "rsn:fd-detroit"}, {"FanDuel SN DET Ext", "rsn:fd-detroit"},
      {"FanDuel SN OH", "rsn:fd-ohio"}, {"FanDuel SN MW", "rsn:fd-midwest"},
      // Stations by their brand.
      {"Arizona's Family CBS5", "call:KPHO"}, {"AZFamily CBS5", "call:KPHO"},
      {"Arizona's Family 3TV", "call:KTVK"}, {"AZFamily 3TV", "call:KTVK"},
      {"Utah 16", "call:KUPX"},
      {"The Spot - MTN", "call:WXPX"},
      {"Vegas 34", "call:KMCC"},
      {"CW33", "call:KDAF"},
      {"PeachtreeTV", "call:WPCH"},
      {"Atlanta News First", "call:WANF"},
      {"CW26", "call:WCIU"},
      {"Fox 5 New York", "call:WNYW"},
      {"PIX 11", "call:WPIX"}, {"CW PIX 11", "call:WPIX"}, {"CW PIX", "call:WPIX"},
      // A station's own streaming service, not the station.
      {"KPIX+", ""},
  };
  static const char* const kStreaming[] = {
      "ESPN+", "ESPN Unlmtd", "Paramount+", "Peacock", "Apple TV", "Apple TV+", "Prime Video",
      "Prime Video (Local)", "Prime Video-Seattle", "Hulu", "Disney+", "HBO Max", "Max", "Netflix",
      "YouTube", "YouTube TV", "NFL+", "Victory+", "Victory+ ATL", "Victory+ MIN", "MLB.TV",
      "NBA League Pass", "WNBA League Pass", "League Pass", "NBA G League TV on Tubi",
      "Suns Live", "Jazz+", "BlazerVision", "ClipperVision", "MavsTV", "Thunder App", "Bucks App",
      "Pelicans.com", "Blues App", "BN+", "Mammoth+", "Merc+", "Liberty Live", "BravesVision",
      "FloSports", "FloHockey", "FloFootball", "FloBaseball", "B1G+", "ACCNX", "SECN+", "MW+",
      "Midco Sports Plus", "SWAC TV", "MEAC Network", "NEC Front Row", "Fandango", "ERADM",
      "Summit League Network", "Patriot League TV", "NCHCHockey.com",
  };
  // clang-format on
  static const std::unordered_map<std::string, Broadcaster> kMap = [] {
    std::unordered_map<std::string, Broadcaster> map;
    for (const Row& row : kRows) map.emplace(folded(row.name), Broadcaster{row.key, false});
    for (const char* name : kStreaming) map.emplace(folded(name), Broadcaster{"", true});
    return map;
  }();
  return kMap;
}

// Stations a name stands for only with the team ("Fox4" is KDFW for the
// Stars, WDAF for the Chiefs): name -> (team's abbreviation, call sign).
// Station groups too ("Scripps Sports": the Golden Knights on KMCC).
const std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>>& teamStations() {
  struct Row {
    const char* name;
    const char* team;
    const char* sign;
  };
  static const Row kRows[] = {
      {"Fox4", "DAL", "KDFW"},   {"Fox4", "KC", "WDAF"},   {"My27", "DAL", "KDFI"},
      {"NBC 10", "PHI", "WCAU"}, {"NBC 10", "BOS", "WBTS"}, {"Scripps Sports", "VGK", "KMCC"},
  };
  static const std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> kMap = [] {
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> map;
    for (const Row& row : kRows) map[folded(row.name)].emplace_back(row.team, row.sign);
    return map;
  }();
  return kMap;
}

// A call sign anywhere in a schedule's station name, alone: "KTVD-TV
// (My20)", "KUSA-TV (9NEWS)", "ALT2/KTVD", "KCTV5", "Fox 5 WTTG", "KFMB 8.1
// (CBS)", "WTHR Channel 13". Four capitals (a channel number may follow);
// words and two different signs are no sign.
std::optional<std::string> callsignAnywhere(std::string_view name) {
  std::optional<std::string> found;
  size_t i = 0;
  while (i < name.size()) {
    while (i < name.size() && !asciiAlnum(name[i])) ++i;
    size_t start = i;
    while (i < name.size() && asciiAlnum(name[i])) ++i;
    std::string_view token = name.substr(start, i - start);
    if (token.size() < 4 || token.size() > 6 || (token[0] != 'K' && token[0] != 'W')) continue;
    bool sign = true;
    for (size_t k = 1; k < token.size(); ++k) {
      bool letter = token[k] >= 'A' && token[k] <= 'Z';
      bool digit = token[k] >= '0' && token[k] <= '9';
      if (k < 4 ? !letter : !digit) sign = false;
    }
    if (!sign) continue;
    std::string s(token.substr(0, 4));
    if (notCallsigns().count(s) || s == "WNBA") continue;
    if (found && *found != s) return std::nullopt;
    found = std::move(s);
  }
  return found;
}

}  // namespace

// Lower-case letters and digits only, "+" as "plus", accents dropped and
// quality words left out: "UniMás" -> "unimas", "NBCSCA+" -> "nbcscaplus",
// "ESPN Deportes HD" -> "espndeportes".
std::string folded(std::string_view name) {
  static const std::unordered_set<std::string> kQuality = {"hd", "fhd", "uhd", "sd", "4k", "raw", "east"};
  std::string out, word;
  auto flush = [&] {
    if (!word.empty() && !kQuality.count(word)) out += word;
    word.clear();
  };
  for (size_t i = 0; i < name.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(name[i]);
    if (c >= 'A' && c <= 'Z') {
      word += static_cast<char>(c + 32);
    } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      word += static_cast<char>(c);
    } else if (c == 0xC3 && i + 1 < name.size()) {
      if (char f = foldLatin(static_cast<unsigned char>(name[++i]))) word += f;
    } else {
      flush();
      if (c == '+') out += "plus";
    }
  }
  flush();
  return out;
}

std::optional<std::string> key(std::string_view name) {
  std::string lowered = str::replaceAll(str::lower(name), "&", "");
  std::string spaced = rx::replace(lowered, "[^\\p{L}\\p{N}\\p{M}+]+", " ");
  std::string text = " " + std::string(str::trim(spaced)) + " ";
  text = str::replaceAll(text, " sports net ", " sportsnet ");
  // Build 143: two-word regions written apart ("FanDuel Sports Network
  // South West", "FOX SPORTS MID WEST") are the one-word regions.
  static const std::pair<const char*, const char*> kJoined[] = {
      {" south west ", " southwest "}, {" south east ", " southeast "}, {" mid west ", " midwest "},
      {" north west ", " northwest "}};
  for (const auto& [apart, joined] : kJoined) {
    if (text.find(apart) != std::string::npos) text = str::replaceAll(text, apart, joined);
  }
  return keyNormalized(text);
}

std::optional<std::string> callsignInName(std::string_view name) {
  if (auto m = rx::first("\\((?:[A-Za-z]+\\s+)?([KW][A-Z]{2,3})(?:[- ](?:TV|DT|LD|CD|CA|D\\d))?\\s*\\)", name)) {
    return std::string(m->group(1));
  }
  // After a state code: "UT KJZZ".
  if (auto m = rx::first("\\b[A-Z]{2}\\s+([KW][A-Z]{2,3})\\b", name)) return std::string(m->group(1));
  // Build 143: not "HONG KONG FIGHT CLUB".
  if (auto m = rx::first("(?:^|[\\s:])(?<!HONG )([KW][A-Z]{2,3})(?=\\s|$|-(?:TV|DT|LD|CD))", name)) {
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
  auto it = broadcasters().find(folded(name));
  if (it == broadcasters().end() || !str::startsWith(it->second.key, "call:")) return std::nullopt;
  return it->second.key.substr(5);
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
  static const std::set<std::string> kNetworks = {"ABC", "CBS",       "NBC",       "FOX",   "CW",
                                                  "ION", "TELEMUNDO", "UNIVISION", "UNIMAS"};
  return kNetworks;
}

// Build 143: networks that show several games at once, a region each (Big
// Ten Network): like a broadcast network's regional coverage, a guide
// listing says which a channel has. Also any network you have alternate
// channels of ("Big Ten Network Overflow 2").
bool splitsByRegion(const std::string& key) { return key == "core:Big Ten Network"; }

// A team known by one word, a place ("Arizona", "Pittsburgh": a college
// side): a channel is its own only when it says nothing else ("Chelsea
// TV", not "ARIZONA COYOTES" or "SPORTSNET PITTSBURGH ALTERNATE").
bool onlyNames(const std::string& channel, const SportsTeam& team, const std::string& league) {
  std::vector<std::string> name = text::components(str::trim(matcher::normalized(team.name)), " ");
  if (name.size() != 1) return true;
  static const std::unordered_set<std::string> kGeneric = {"tv", "hd", "fhd", "uhd", "sd", "4k", "raw", "channel",
                                                          "us", "usa"};
  std::string leagueWord = str::lower(league);
  for (const std::string& word : text::components(str::trim(matcher::normalized(channel)), " ")) {
    if (word.empty() || word == name.front() || word == leagueWord || kGeneric.count(word)) continue;
    return false;
  }
  return true;
}

// The leagues catalog::proTeamChannel knows the teams of.
bool proLeague(const std::string& league) {
  return league == "NBA" || league == "WNBA" || league == "NHL" || league == "MLB" || league == "NFL";
}

const regional::Broadcaster* broadcaster(std::string_view name) {
  auto it = regional::broadcasters().find(regional::folded(name));
  return it == regional::broadcasters().end() ? nullptr : &it->second;
}

// A station the name stands for with one of the game's teams.
std::optional<std::string> teamStation(std::string_view name, const SportsGame& game) {
  const auto& table = regional::teamStations();
  if (table.empty()) return std::nullopt;
  auto it = table.find(regional::folded(name));
  if (it == table.end()) return std::nullopt;
  for (const SportsTeam* team : {&game.home, &game.away}) {
    if (!team->shortName) continue;
    for (const auto& [abbr, sign] : it->second) {
      if (abbr == *team->shortName) return sign;
    }
  }
  return std::nullopt;
}

// A network named outright ("ESPN", "BIG TEN NETWORK ALT", "ABC"; no rules
// that read more): its key and whether it's the network's alternate.
std::optional<std::pair<std::string, bool>> networkNamed(std::string_view text) {
  std::string rest(str::trimUnicode(text));
  if (rest.empty() || rest.size() > 32) return std::nullopt;
  bool alternate = false;
  std::string lower = str::lowerAscii(rest);
  for (const char* word : {" alternate", " alt", " overflow"}) {
    size_t at = lower.rfind(word);
    if (at != std::string::npos && at + std::strlen(word) <= lower.size() &&
        lower.find_first_not_of("0123456789 ", at + std::strlen(word)) == std::string::npos) {
      rest = std::string(str::trimUnicode(std::string_view(rest).substr(0, at)));
      alternate = true;
      break;
    }
  }
  if (rest.empty()) return std::nullopt;
  // Most slots' texts are titles: a hashed look at the name first.
  static const std::unordered_set<std::string> kNames = [] {
    std::unordered_set<std::string> names;
    for (const catalog::CoreChannel& c : catalog::coreChannels()) {
      names.insert(regional::folded(c.name));
      for (const std::string& alias : c.aliases) names.insert(regional::folded(alias));
    }
    for (const std::string& word : broadcastNetworks()) names.insert(regional::folded(word));
    return names;
  }();
  std::string folded = regional::folded(rest);
  if (!kNames.count(folded) && !regional::broadcasters().count(folded)) return std::nullopt;
  if (auto core = catalog::coreMatch(rest)) return std::make_pair("core:" + core->name, alternate);
  std::string word = str::upper(str::replaceAll(catalog::coreKey(rest).key, " ", ""));
  if (broadcastNetworks().count(word)) return std::make_pair("net:" + word, alternate);
  if (const regional::Broadcaster* hit = broadcaster(rest)) {
    if (!hit->key.empty() && !str::startsWith(hit->key, "call:")) return std::make_pair(hit->key, alternate);
  }
  return std::nullopt;
}

// The league an event family is for, by its name ("grp:us| ncaaf ppv",
// "ncaaf #" -> "NCAAF"); "" for any.
std::string slotLeague(const std::string& familyKey) {
  static const std::pair<const char*, const char*> kLeagues[] = {
      {"ncaaf", "NCAAF"}, {"ncaab", "NCAAM"}, {"ncaam", "NCAAM"}, {"ncaaw", "NCAAW"}, {"wnba", "WNBA"},
      {"nfl", "NFL"},     {"nba", "NBA"},     {"nhl", "NHL"},     {"mlb", "MLB"}};
  for (const auto& [word, league] : kLeagues) {
    if (rx::test(std::string("\\b") + word + "\\b", familyKey, true)) return league;
  }
  return "";
}

// The network an event slot is named after: a group's slot "NCAAF 01: ESPN"
// (its text is the whole name: the network after the number), or a
// numbered family's slot whose text is a network alone ("NCAAF 04: ABC"
// read as family "ncaaf #", slot 4, "ABC").
std::optional<std::pair<std::string, bool>> slotNetwork(const catalog::ChannelTraits& t) {
  if (!t.eventText || !t.familyKey) return std::nullopt;
  std::string_view name = *t.eventText;
  size_t colon = name.find(':');
  if (colon != std::string_view::npos && colon >= 3 && colon <= 24) {
    // "<tag> NN:": a number right before the colon.
    size_t digits = 0;
    while (digits < colon && name[colon - 1 - digits] >= '0' && name[colon - 1 - digits] <= '9') ++digits;
    if (digits > 0 && digits <= 3 && colon - digits >= 2 && name[colon - digits - 1] == ' ') {
      return networkNamed(name.substr(colon + 1));
    }
  }
  if (t.familyNumber && t.familyKey->find('#') != std::string::npos) return networkNamed(name);
  return std::nullopt;
}

// A network's alternate channel ("Big Ten Network Alternate 2", "BIG TEN
// NETWORK OVERFLOW 3"): the network's core name.
std::optional<std::string> alternateOf(const std::string& name) {
  std::string lower = str::lowerAscii(name);
  if (lower.find("alternate") == std::string::npos && lower.find("overflow") == std::string::npos &&
      lower.find(" alt") == std::string::npos) {
    return std::nullopt;
  }
  std::string bare(str::trimUnicode(rx::replace(name, "\\s+(alternate|alt|overflow)\\b.*$", "", true)));
  if (bare.empty() || bare.size() == name.size()) return std::nullopt;
  if (auto core = catalog::coreMatch(bare)) return core->name;
  return std::nullopt;
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
  if (const regional::Broadcaster* hit = broadcaster(name)) {
    if (hit->key.empty()) return std::nullopt;
    return hit->key;
  }
  std::string key = catalog::coreKey(name).key;
  auto alias = networkAliases().find(key);
  if (alias != networkAliases().end()) return "core:" + alias->second;
  if (auto core = catalog::coreMatch(name)) return "core:" + core->name;
  std::string word = str::upper(str::replaceAll(key, " ", ""));
  if (broadcastNetworks().count(word)) return "net:" + word;
  if (auto region = regional::key(name)) return "rsn:" + *region;
  if (auto sign = regional::callsign(name)) return "call:" + *sign;
  if (auto sign = regional::callsignAnywhere(name)) return "call:" + *sign;
  return std::nullopt;
}

bool streamingName(std::string_view name) {
  if (const regional::Broadcaster* hit = broadcaster(name)) return hit->streaming;
  // A team's own service ("Cardinals.TV", "Pelicans.com", "Thunder App").
  std::string lower = str::lowerAscii(str::trimUnicode(name));
  return str::endsWith(lower, ".tv") || str::endsWith(lower, ".com") || str::endsWith(lower, " app");
}

std::string ChannelIndex::summary() const {
  return std::to_string(byKey.size()) + " keys, " + std::to_string(regional) + " regional, " +
         std::to_string(callsigns) + " stations by call sign, " + std::to_string(teamChannels) +
         " team channels, " + std::to_string(fightChannels) + " fight card channels, " +
         std::to_string(alternates) + " alternate channels, " + std::to_string(slotCopies) +
         " event slots named for a network";
}

ChannelIndex channelIndex(const std::set<std::string>& lineup, const std::vector<Channel>& lineupEntries,
                          const std::vector<Channel>& browse, const std::vector<Channel>& all,
                          const std::unordered_map<std::string, catalog::EntryInfo>& entryInfo,
                          const std::set<std::string>& hidden,
                          const std::vector<matcher::RosterLeague>& rosters,
                          const StationMarkets& markets,
                          const std::unordered_map<std::string, catalog::ChannelTraits>* slots) {
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
      // From the full list, only sports channels of unknown country and
      // (Build 143) the US channels in Spanish: Live Events reads them
      // whether or not that list is picked, ranked after English ones.
      bool spanishUS = info.market == std::string("US-ES") && info.category == LineupCategory::spanish;
      if (pass == 2 && !spanishUS && (info.market || info.category != LineupCategory::sports)) continue;
      if (!info.core && info.category != LineupCategory::sports && info.category != LineupCategory::local &&
          info.category != LineupCategory::spanish) {
        continue;
      }
      bool inLineup = lineup.count(entry.id) > 0;
      ChannelIndex::Item item{entry.id, inLineup, languages::ofChannel(info), stationRank(info, markets, inLineup)};
      std::vector<std::string> added;
      auto add = [&](const std::string& key) {
        if (std::find(added.begin(), added.end(), key) != added.end()) return;
        added.push_back(key);
        index.byKey[key].push_back(item);
      };
      if (info.core) {
        add("core:" + *info.core);
        continue;
      }
      // Build 143: Spanish stations (Telemundo, Univision, UniMás) like
      // local ones: by network and call sign.
      bool station = info.category == LineupCategory::local || info.category == LineupCategory::spanish;
      if (station) {
        if (auto word = catalog::networkWord(info.name)) {
          if (info.category == LineupCategory::local || *word == "TELEMUNDO" || *word == "UNIVISION" ||
              *word == "UNIMAS") {
            add("net:" + *word);
          }
          // An ION station by its call sign carries the national feed.
          if (*word == "ION" && info.callsign) add("core:ION");
        }
      }
      // The broadcaster table by the channel's name ("NBCSN", "ESPN
      // Deportes", "CW PIX 11 HD"): a network or station the rules don't read.
      const regional::Broadcaster* listed = broadcaster(info.name);
      if (listed && (str::startsWith(listed->key, "core:") || str::startsWith(listed->key, "rsn:"))) add(listed->key);
      if (auto region = regional::key(info.name)) {
        add("rsn:" + *region);
        index.regional += 1;
      }
      if (station) {
        std::optional<std::string> sign;
        if (info.callsign) sign = regional::callsign(*info.callsign);
        if (!sign) sign = regional::callsignInName(info.name);
        if (!sign && listed && str::startsWith(listed->key, "call:")) sign = listed->key.substr(5);
        if (sign) {
          add("call:" + *sign);
          index.callsigns += 1;
        }
      }
      // A network's alternate channel: a backup when several games are on
      // the network at once.
      if (info.category == LineupCategory::sports) {
        if (auto core = alternateOf(info.name)) {
          add("alt:core:" + *core);
          index.alternates += 1;
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
        // Build 143: a pro team's channel ("AT&T: SAN FRANCISCO GIANTS") is
        // that pro team's, never a college side's of the same place; no
        // channel is a college team's (a school is named by its place).
        bool proTeam = catalog::proTeamChannel(info.name);
        std::vector<std::string> named;
        for (const auto& list : teamLists) {
          if (proTeam && !proLeague(list.first)) continue;
          if (isCollegeLeague(list.first)) continue;
          if (auto team = list.second.single(info.name, list.first)) {
            if (team->conference || !onlyNames(info.name, *team, list.first)) continue;
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
  // Build 143: event slots named after a network ("NCAAF 01: ESPN", "NCAAF
  // 04: ABC", "NCAAF 10: BIG TEN NETWORK ALT"), as backups of it.
  if (slots) {
    for (const auto& [id, t] : *slots) {
      if (t.kind != catalog::ChannelKind::event || !t.familyKey) continue;
      if (t.market && *t.market != "US" && *t.market != "US-ES") continue;
      auto network = slotNetwork(t);
      if (!network) continue;
      ChannelIndex::Item item{id, false, languages::ofChannel(t.displayName, t.market, t.category),
                              station_rank::elsewhere};
      // A league's slots ("US| NCAAF PPV") carry the network for its games only.
      index.byKey[(network->second ? "alt:" : "backup:") + network->first + "|" + slotLeague(*t.familyKey)]
          .push_back(std::move(item));
      index.slotCopies += 1;
    }
    // Slots in a stable order (the traits map has none).
    for (auto& [key, items] : index.byKey) {
      if (str::startsWith(key, "backup:") || str::startsWith(key, "alt:")) {
        std::stable_sort(items.begin(), items.end(), [](const ChannelIndex::Item& a, const ChannelIndex::Item& b) {
          return a.rank != b.rank ? a.rank < b.rank : a.id < b.id;
        });
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
         topCounts(missing) + "]; unknown names: [" + topCounts(unknown) + "]; streaming: [" +
         topCounts(streaming) + "]";
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
  auto keyFor = [&](const std::string& network, const SportsGame& game) -> std::optional<std::string> {
    if (auto sign = teamStation(network, game)) return "call:" + *sign;
    return cachedKey(network);
  };
  auto find = [&](const std::string& key) -> const std::vector<ChannelIndex::Item>* {
    auto it = index.byKey.find(key);
    return it == index.byKey.end() ? nullptr : &it->second;
  };
  // A network that splits games by region: a broadcast network (several
  // NFL games on FOX at 1 PM) and (Build 143) one with alternate channels.
  auto regionalSplits = [&](const std::string& key) {
    return str::startsWith(key, "net:") ||
           (str::startsWith(key, "core:") && (splitsByRegion(key) || find("alt:" + key)));
  };
  // Such networks' games only count when a guide listing confirms which one
  // your station has. Build 143: every such network a game names, not only
  // its first.
  std::map<std::string, std::vector<Seconds>> byNetwork;
  for (const SportsGame& game : games) {
    if (game.completed || game.networks.empty()) continue;
    std::vector<std::string> counted;
    for (const std::string& network : game.networks) {
      auto key = keyFor(network, game);
      if (!key || !regionalSplits(*key)) continue;
      if (std::find(counted.begin(), counted.end(), *key) != counted.end()) continue;
      counted.push_back(*key);
      byNetwork[*key].push_back(game.start);
    }
  }
  for (const SportsGame& game : games) {
    if (game.completed) continue;
    std::vector<EventSource> sources;
    // Best first (My Lineup, home market, your markets, elsewhere);
    // `nearby` keeps only your own markets' stations (three at most),
    // otherwise two (`limit` when given).
    auto take = [&](const std::vector<ChannelIndex::Item>& entries, bool nearby, bool team,
                    const std::string& teamDetail, const std::string& teamLineupDetail, size_t limit = 0) {
      std::vector<ChannelIndex::Item> ranked;
      for (const auto& item : entries) {
        if (!nearby || item.rank <= station_rank::market) ranked.push_back(item);
      }
      std::stable_sort(ranked.begin(), ranked.end(),
                       [](const auto& a, const auto& b) { return a.rank < b.rank; });
      if (limit == 0) limit = nearby ? 3 : 2;
      for (size_t i = 0; i < ranked.size() && i < limit; ++i) {
        const auto& item = ranked[i];
        bool known = std::any_of(sources.begin(), sources.end(),
                                 [&](const EventSource& s) { return s.channelID == item.id; });
        if (known) continue;
        EventSource source;
        source.channelID = item.id;
        source.kind = team ? EventSource::Kind::team
                           : (item.inLineup ? EventSource::Kind::lineup : EventSource::Kind::guide);
        source.lang = item.lang;
        source.detail = team ? (item.inLineup ? teamLineupDetail : teamDetail) : stationDetail(item.rank);
        source.rank = item.rank;
        sources.push_back(std::move(source));
      }
    };
    for (const std::string& network : game.networks) {
      auto key = keyFor(network, game);
      if (!key) {
        if (streamingName(network)) {
          stats.streaming[network] += 1;
        } else {
          stats.unknown[network] += 1;
          stats.unknownLeagues[network].insert(game.leagueShort);
        }
        continue;
      }
      const auto* entries = find(*key);
      // Alternate channels, then event slots: any league's and this one's.
      const std::string league = collegeKey(game.leagueShort);
      const std::vector<ChannelIndex::Item>* alternates[] = {find("alt:" + *key), find("alt:" + *key + "|"),
                                                              find("alt:" + *key + "|" + league)};
      const std::vector<ChannelIndex::Item>* backups[] = {find("backup:" + *key + "|"),
                                                           find("backup:" + *key + "|" + league)};
      auto any = [](const auto& lists) {
        return std::any_of(std::begin(lists), std::end(lists), [](const auto* l) { return l != nullptr; });
      };
      if (!entries && !any(alternates) && !any(backups)) {
        stats.missing[network] += 1;
        continue;
      }
      if (regionalSplits(*key)) {
        // Several games on the network at once (regional coverage): only a
        // channel's guide can say which one it has.
        auto starts = byNetwork.find(*key);
        auto together = starts == byNetwork.end()
                            ? 0
                            : std::count_if(starts->second.begin(), starts->second.end(),
                                            [&](Seconds s) { return std::llabs(s - game.start) < 90 * 60; });
        if (together > 1) {
          stats.split += 1;
          // Build 143: a network with alternate channels (Big Ten Network):
          // it and its alternates are likely ways to watch, the guide
          // listings say which for sure.
          if (str::startsWith(*key, "core:")) {
            static const std::string kLikely = "Several games on this network at once";
            static const std::string kLikelyLineup = "Several games on this network at once \xC2\xB7 In My Lineup";
            if (entries) take(*entries, false, true, kLikely, kLikelyLineup, 1);
            size_t room = 3;
            for (const auto* list : alternates) {
              if (!list || room == 0) continue;
              size_t before = sources.size();
              take(*list, false, true, kLikely, kLikelyLineup, room);
              room -= std::min(room, sources.size() - before);
            }
          }
          continue;
        }
      }
      if (str::startsWith(*key, "net:")) {
        // A national game: your own markets' stations.
        if (entries) take(*entries, true, false, "", "");
        stats.national += 1;
      } else {
        // A network or station that is the broadcast itself (a sports
        // network, a regional network, a station): wherever it is.
        if (entries) take(*entries, false, false, "", "");
        if (str::startsWith(*key, "rsn:")) {
          stats.regional += 1;
        } else if (str::startsWith(*key, "call:")) {
          stats.station += 1;
        } else {
          stats.national += 1;
        }
      }
      // An event slot carrying the network (Build 143): one, after it.
      for (const auto* list : backups) {
        if (!list) continue;
        size_t before = sources.size();
        take(*list, false, false, "", "", 1);
        if (sources.size() > before) break;
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
