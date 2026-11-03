#include "core/sports/my_sports.hpp"

#include <algorithm>
#include <set>

#include "core/catalog/text.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/matcher.hpp"
#include "core/util/json.hpp"
#include "core/util/strings.hpp"

namespace marigold::sports {

std::string toJson(const MySportsPrefs& prefs) {
  json::Value v = json::Value::object();
  v.set("configured", prefs.configured);
  json::Value leaguesList = json::Value::array();
  for (const std::string& l : prefs.leagues) leaguesList.push(l);
  v.set("leagues", leaguesList);
  json::Value teams = json::Value::array();
  for (const std::string& t : prefs.teams) teams.push(t);
  v.set("teams", teams);
  v.set("reminders", prefs.reminders);
  v.set("prompted", prefs.prompted);
  json::Value college = json::Value::object();
  for (const auto& [league, pick] : prefs.college) {
    json::Value c = json::Value::object();
    c.set("top25", pick.top25);
    c.set("everyGame", pick.everyGame);
    json::Value conferences = json::Value::array();
    for (const std::string& k : pick.conferences) conferences.push(k);
    c.set("conferences", conferences);
    college.set(league, c);
  }
  v.set("college", college);
  json::Value hidden = json::Value::array();
  for (const std::string& h : prefs.hidden) hidden.push(h);
  v.set("hidden", hidden);
  v.set("version", static_cast<int64_t>(prefs.version));
  v.set("updateNoted", prefs.updateNoted);
  return json::write(v);
}

std::optional<MySportsPrefs> mySportsFromJson(std::string_view text) {
  auto doc = json::parse(text);
  if (!doc || !doc->isObject()) return std::nullopt;
  const json::Value& v = *doc;
  MySportsPrefs prefs;
  auto readBool = [&](const char* key, bool& into) {
    if (!v.has(key)) return true;
    if (!v[key].isBool()) return false;
    into = v[key].asBool();
    return true;
  };
  auto stringsOf = [](const json::Value& list, std::vector<std::string>& into) {
    if (!list.isArray()) return false;
    into.clear();
    for (const json::Value& item : list.items()) {
      if (!item.isString()) return false;
      into.push_back(item.asString());
    }
    return true;
  };
  auto readList = [&](const char* key, std::vector<std::string>& into) {
    return !v.has(key) || stringsOf(v[key], into);
  };
  if (!readBool("configured", prefs.configured) || !readList("leagues", prefs.leagues) ||
      !readList("teams", prefs.teams) || !readBool("reminders", prefs.reminders) ||
      !readBool("prompted", prefs.prompted) || !readList("hidden", prefs.hidden) ||
      !readBool("updateNoted", prefs.updateNoted)) {
    return std::nullopt;
  }
  // Build 127's fields. A file from before has no version: 1.
  prefs.version = 1;
  if (v.has("version")) {
    if (!v["version"].isNumber()) return std::nullopt;
    prefs.version = static_cast<int>(v["version"].asInt());
  }
  if (v.has("college")) {
    if (!v["college"].isObject()) return std::nullopt;
    for (const auto& [league, c] : v["college"].fields()) {
      if (!c.isObject()) return std::nullopt;
      CollegePick pick;
      if (c.has("top25")) {
        if (!c["top25"].isBool()) return std::nullopt;
        pick.top25 = c["top25"].asBool();
      }
      if (c.has("everyGame")) {
        if (!c["everyGame"].isBool()) return std::nullopt;
        pick.everyGame = c["everyGame"].asBool();
      }
      if (c.has("conferences") && !stringsOf(c["conferences"], pick.conferences)) return std::nullopt;
      prefs.college[league] = std::move(pick);
    }
  }
  return prefs;
}

MySportsChange compare(const MySportsPrefs& before, const MySportsPrefs& after) {
  MySportsChange change;
  change.changed = before != after;
  change.leaguesChanged = after.leagues != before.leagues || after.configured != before.configured ||
                          after.hidden != before.hidden;
  return change;
}

std::string teamKey(std::string_view league, std::string_view teamID) {
  return std::string(league) + "|" + std::string(teamID);
}

bool isFavorite(const MySportsPrefs& prefs, const SportsGame& game) {
  if (prefs.teams.empty()) return false;
  auto has = [&](const std::string& key) {
    return std::find(prefs.teams.begin(), prefs.teams.end(), key) != prefs.teams.end();
  };
  return has(teamKey(game.leagueShort, game.home.id)) || has(teamKey(game.leagueShort, game.away.id));
}

const std::vector<OtherSport>& otherSports() {
  static const std::vector<OtherSport> kSports = {
      {"S:Fighting", "Fighting", "UFC, PFL & boxing"},
      // Off unless picked: shown in Live Events only then (proWrestlingOn).
      {kProWrestlingPick, "Pro Wrestling", "WWE, AEW, NXT, TNA & ROH"},
      // Off unless picked: then its own row and Live now (highSchoolOn).
      {kHighSchoolPick, kHighSchoolRow, "High-school games"},
      {"S:Motorsport", "Motorsport", "Racing"},
      {"S:Tennis", "Tennis", "Tennis"},
      {"S:Golf", "Golf", "Golf"},
      {"S:Rugby", "Rugby", "Rugby"},
      {"S:Cricket", "Cricket", "Cricket"},
      {"S:Volleyball", "Volleyball", "Volleyball"},
      {"S:Lacrosse", "Lacrosse", "Lacrosse"},
      {"S:FieldHockey", "Field Hockey", "Field hockey"},
      {"S:Softball", "Softball", "Softball"},
      {"S:Darts", "Darts", "Darts"},
  };
  return kSports;
}

bool proWrestlingOn(const MySportsPrefs& prefs) {
  return std::find(prefs.leagues.begin(), prefs.leagues.end(), kProWrestlingPick) != prefs.leagues.end();
}

bool highSchoolOn(const MySportsPrefs& prefs) {
  return std::find(prefs.leagues.begin(), prefs.leagues.end(), kHighSchoolPick) != prefs.leagues.end();
}

std::optional<std::string> nameOf(std::string_view id) {
  for (const League& l : leagues()) {
    if (l.shortName == id) return l.name;
  }
  for (const OtherSport& s : otherSports()) {
    if (s.id == id) return s.name;
  }
  for (const SoccerCompetition& c : soccerCompetitions()) {
    if (c.id == id) return c.name;
  }
  return std::nullopt;
}

std::optional<std::string> pickGroup(std::string_view id) {
  for (const League& l : leagues()) {
    if (l.shortName == id) return l.group;
  }
  for (const OtherSport& s : otherSports()) {
    if (s.id == id) return s.group;
  }
  // USA, Mexico, every other competition: the Soccer row.
  if (isSoccerPick(id)) return std::string("Soccer");
  return std::nullopt;
}

std::vector<std::string> pickedGroups(const MySportsPrefs& prefs) {
  std::vector<std::string> groups;
  for (const std::string& id : prefs.leagues) {
    std::optional<std::string> group = pickGroup(id);
    if (!group || std::find(groups.begin(), groups.end(), *group) != groups.end()) continue;
    groups.push_back(*group);
  }
  return groups;
}

// MARK: Build 127: picks v2

namespace {

bool contains(const std::vector<std::string>& list, std::string_view id) {
  return std::find(list.begin(), list.end(), id) != list.end();
}

void addOnce(std::vector<std::string>& list, const std::string& id) {
  if (!contains(list, id)) list.push_back(id);
}

void removeAll(std::vector<std::string>& list, std::string_view id) {
  list.erase(std::remove(list.begin(), list.end(), id), list.end());
}

bool spanish(const std::set<std::string>& countries) { return countries.count("US-ES") || countries.count("MX"); }

// The countries whose languages tick cricket, and the language a green line names.
const std::vector<std::pair<std::string, std::string>>& cricketCountries() {
  static const std::vector<std::pair<std::string, std::string>> kCountries = {
      {"IN", "Hindi"}, {"PK", "Urdu"}, {"BD", "Bengali"}, {"LK", "Tamil"}};
  return kCountries;
}

}  // namespace

MySportsPrefs defaultPicks(const std::set<std::string>& countries) {
  MySportsPrefs prefs;
  prefs.leagues = {"NFL", "NCAAF", "NBA", "WNBA", "NCAAM", "NCAAW", "MLB", "NHL", "EPL", "UCL", "MLS", "NWSL",
                   kUSAPick};
  if (spanish(countries)) {
    prefs.leagues.push_back("LMX");
    prefs.leagues.push_back(kMexicoPick);
  }
  prefs.leagues.push_back("S:Volleyball");
  bool cricket = std::any_of(cricketCountries().begin(), cricketCountries().end(),
                             [&](const auto& c) { return countries.count(c.first) > 0; });
  if (cricket) prefs.leagues.push_back("S:Cricket");
  return prefs;
}

MySportsPrefs effectivePicks(const MySportsPrefs& prefs, const std::set<std::string>& countries) {
  if (prefs.configured) return prefs;
  MySportsPrefs out = prefs;
  out.leagues = defaultPicks(countries).leagues;
  return out;
}

std::string pickReason(std::string_view id, const std::set<std::string>& countries) {
  if ((id == "LMX" || id == kMexicoPick) && spanish(countries)) return "For Espa\xC3\xB1ol";
  if (id == "S:Cricket") {
    for (const auto& [country, language] : cricketCountries()) {
      if (countries.count(country)) return "For " + language;
    }
  }
  return "";
}

bool isHidden(const MySportsPrefs& prefs, std::string_view id) { return !id.empty() && contains(prefs.hidden, id); }

std::string collegeKey(std::string_view leagueShort) {
  static const std::map<std::string, std::string, std::less<>> kKeys = {
      {"FCS", "NCAAF"},     {"D2/D3", "NCAAF"},   {"D2MBB", "NCAAM"},   {"D3MBB", "NCAAM"},
      {"D2WBB", "NCAAW"},   {"D3WBB", "NCAAW"},   {"D2WVB", "NCAAWV"},  {"D3WVB", "NCAAWV"},
      {"D3MHK", "NCAAMH"},  {"D3WHK", "NCAAWH"},  {"D2FH", "NCAAFH"},   {"D3FH", "NCAAFH"}};
  auto it = kKeys.find(leagueShort);
  return it == kKeys.end() ? std::string(leagueShort) : it->second;
}

bool isCollegeLeague(std::string_view leagueShort) {
  const League* l = leagueByShort(leagueShort);
  return l && !l->soccer && (l->college() || l->everyGame);
}

CollegePick collegePick(const MySportsPrefs& prefs, std::string_view leagueShort) {
  auto it = prefs.college.find(collegeKey(leagueShort));
  return it == prefs.college.end() ? CollegePick() : it->second;
}

bool inCollegeRow(const MySportsPrefs& prefs, const SportsGame& game) {
  CollegePick pick = collegePick(prefs, game.leagueShort);
  if (pick.everyGame) return true;
  std::string key = collegeKey(game.leagueShort);
  for (const SportsTeam* side : {&game.home, &game.away}) {
    if (pick.top25 && side->rank && *side->rank >= 1 && *side->rank <= 25) return true;
    if (side->conference && contains(pick.conferences, *side->conference)) return true;
    if (!prefs.teams.empty() &&
        (contains(prefs.teams, teamKey(game.leagueShort, side->id)) || contains(prefs.teams, teamKey(key, side->id))))
      return true;
  }
  return false;
}

std::string collegeSubtitle(const CollegePick& pick) {
  if (pick.everyGame) return "Every Division I game";
  if (!pick.top25) return "Your conferences and schools";
  if (!pick.conferences.empty()) return "Top 25, your conferences and schools";
  return "Top 25 and your schools";
}

void migrate(MySportsPrefs& prefs) {
  if (prefs.version >= kMySportsVersion) return;
  prefs.version = kMySportsVersion;
  prefs.updateNoted = false;
  if (!prefs.configured) return;
  // Any soccer picked fetched the European club competitions and every
  // other soccer game too: the Champions League and the US teams stay.
  bool soccer = std::any_of(prefs.leagues.begin(), prefs.leagues.end(), [](const std::string& id) {
    const League* l = leagueByShort(id);
    return l && l->soccer;
  });
  if (soccer) {
    addOnce(prefs.leagues, "UCL");
    addOnce(prefs.leagues, kUSAPick);
  }
  // College: the Top 25 and your schools (the defaults).
  prefs.college.clear();
}

const std::vector<SoccerCompetition>& soccerCompetitions() {
  static const std::vector<SoccerCompetition> kList = {
      {"EPL", "Premier League", "England", {"Popular", "England"}},
      {"UCL", "Champions League", "Europe", {"Popular", "Rest of Europe"}},
      {"MLS", "MLS", "USA \xC2\xB7 its own row", {"Popular", "Mexico and the Americas"}},
      {"NWSL", "NWSL", "USA \xC2\xB7 women's", {"Popular", "Mexico and the Americas", "Women's"}},
      {kUSAPick, "USA men and women", "World Cup, Gold Cup, friendlies", {"Popular", "National teams", "Women's"}},
      {"LMX", "Liga MX", "Mexico \xC2\xB7 mostly in Spanish", {"Popular", "Mexico and the Americas"}},
      {kMexicoPick, "Mexico", "National team", {"Popular", "National teams", "Mexico and the Americas"}},
      {"LIGA", "La Liga", "Spain", {"Popular", "Spain"}},
      {"UEL", "Europa League", "Europe", {"Popular", "Rest of Europe"}},
      {"BUN", "Bundesliga", "Germany", {"Popular", "Italy and Germany"}},
      {"SA", "Serie A", "Italy", {"Popular", "Italy and Germany"}},
      {"L1", "Ligue 1", "France", {"Rest of Europe"}},
      {"UECL", "Conference League", "Europe", {"Rest of Europe"}},
      {"EFL", "Championship", "England", {"England"}},
      {"POR", "Liga Portugal", "Portugal", {"Rest of Europe"}},
      {"ERE", "Eredivisie", "Netherlands", {"Rest of Europe"}},
      {kSoccerAllPick, "Every other competition", "Any league, any level", {"Popular", "Every other"}},
  };
  return kList;
}

bool isSoccerPick(std::string_view id) {
  return std::any_of(soccerCompetitions().begin(), soccerCompetitions().end(),
                     [&](const SoccerCompetition& c) { return c.id == id; });
}

std::optional<std::string> nationalTeam(const SportsTeam& team) {
  // The full name as the listing has it ("United States", "Mexico",
  // "United States Women"); youth sides ("Mexico U17") are not the team.
  std::string name = str::lower(str::foldDiacritics(team.name));
  for (const char* tail : {" women", " (w)", " w"}) {
    if (str::endsWith(name, tail)) {
      name = name.substr(0, name.size() - std::string_view(tail).size());
      break;
    }
  }
  if (name == "united states" || name == "usa" || name == "usmnt" || name == "uswnt") return std::string(kUSAPick);
  if (name == "mexico") return std::string(kMexicoPick);
  return std::nullopt;
}

std::string soccerPickOfCompetition(std::string_view leagueID, std::string_view name) {
  // The all-soccer listing's league ids (its events' "uid" "s:600~l:700~e:...").
  static const std::map<std::string, std::string, std::less<>> kByID = {
      {"700", "EPL"},  {"775", "UCL"},  {"776", "UEL"},  {"20296", "UECL"}, {"740", "LIGA"},
      {"730", "SA"},   {"720", "BUN"},  {"710", "L1"},   {"760", "LMX"},    {"770", "MLS"},
      {"8301", "NWSL"}, {"725", "ERE"}, {"715", "POR"},  {"3914", "EFL"}};
  // Its competition names, lower case (the note before the round).
  static const std::map<std::string, std::string, std::less<>> kByName = {
      {"english premier league", "EPL"}, {"uefa champions league", "UCL"}, {"uefa europa league", "UEL"},
      {"uefa conference league", "UECL"}, {"uefa europa conference league", "UECL"}, {"laliga", "LIGA"},
      {"la liga", "LIGA"}, {"serie a", "SA"}, {"italian serie a", "SA"}, {"bundesliga", "BUN"},
      {"german bundesliga", "BUN"}, {"ligue 1", "L1"}, {"french ligue 1", "L1"}, {"liga mx", "LMX"},
      {"mexican liga mx", "LMX"}, {"mls", "MLS"}, {"major league soccer", "MLS"}, {"nwsl", "NWSL"},
      {"eredivisie", "ERE"}, {"dutch eredivisie", "ERE"}, {"liga portugal", "POR"},
      {"portuguese primeira liga", "POR"}, {"efl championship", "EFL"}, {"english league championship", "EFL"}};
  if (!leagueID.empty()) {
    auto it = kByID.find(leagueID);
    if (it != kByID.end()) return it->second;
  }
  auto it = kByName.find(str::lower(str::trim(name)));
  return it == kByName.end() ? std::string() : it->second;
}

std::string soccerCompetition(const SportsGame& game) {
  // National teams: in whatever competition (World Cup, Gold Cup,
  // friendlies); the US before Mexico when they play each other.
  std::optional<std::string> home = nationalTeam(game.home), away = nationalTeam(game.away);
  if (home == std::optional<std::string>(kUSAPick) || away == std::optional<std::string>(kUSAPick)) return kUSAPick;
  if (home || away) return home ? *home : *away;
  if (game.competition) return *game.competition;
  const League* l = leagueByShort(game.leagueShort);
  if (l && l->soccer && !l->ncaaPage && game.leagueShort != kSoccerAll) return game.leagueShort;
  if (game.leagueShort == kSoccerAll && game.tournament) {
    std::string pick = soccerPickOfCompetition("", *game.tournament);
    if (!pick.empty()) return pick;
  }
  return kSoccerAllPick;
}

namespace {

// The other sport ("S:Golf") whose row a group is.
std::string sportOfGroup(std::string_view group) {
  for (const OtherSport& s : otherSports()) {
    if (s.group == group) return s.id;
  }
  return "";
}

bool isLeaguePick(std::string_view id) {
  return std::any_of(leagues().begin(), leagues().end(), [&](const League& l) { return l.shortName == id; });
}

bool soccerEvent(const SportsEvent& e) {
  if (e.game) {
    const League* l = leagueByShort(e.game->leagueShort);
    return (l && l->soccer) || e.game->league == "Soccer";
  }
  return e.league == "Soccer";
}

}  // namespace

std::string competitionOf(const SportsEvent& e) {
  if (e.highSchool) return kHighSchoolPick;
  if (soccerEvent(e)) {
    if (e.game) return soccerCompetition(*e.game);
    // An event no schedule has: its league when one is named, else any soccer.
    const League* l = leagueByShort(e.leagueShort);
    if (l && l->soccer && !l->ncaaPage && e.leagueShort != kSoccerAll) return e.leagueShort;
    return kSoccerAllPick;
  }
  const std::string& shortName = e.game ? e.game->leagueShort : e.leagueShort;
  const std::string& group = e.game ? e.game->league : e.league;
  if (isLeaguePick(shortName)) return shortName;
  std::string sport = sportOfGroup(group);
  if (!sport.empty()) return sport;
  if (!shortName.empty() && leagueByShort(shortName)) return shortName;
  return "";
}

std::string competitionName(std::string_view id) {
  static const std::map<std::string, std::string, std::less<>> kNames = {
      {"S:Fighting", "Boxing and UFC"}, {kProWrestlingPick, "Pro wrestling"}, {kHighSchoolPick, "High school"},
      {"S:Motorsport", "Motorsport"},   {"S:Tennis", "Tennis"},              {"S:Golf", "Golf"},
      {"S:Rugby", "Rugby"},             {"S:Cricket", "Cricket"},            {"S:Volleyball", "College volleyball"},
      {"S:Lacrosse", "Lacrosse"},       {"S:FieldHockey", "Field hockey"},   {"S:Softball", "Softball"},
      {"S:Darts", "Darts"},             {kSoccerAllPick, "Other soccer"}};
  auto named = kNames.find(id);
  if (named != kNames.end()) return named->second;
  for (const SoccerCompetition& c : soccerCompetitions()) {
    if (c.id == id) return c.name;
  }
  const League* l = leagueByShort(id);
  if (!l) return std::string(id);
  // A college league names its sport: "Men's college basketball", "Women's
  // Division II soccer", "FCS football".
  std::string name = l->name;
  if (!isCollegeLeague(id) && !l->ncaaPage) return name;
  std::string group = l->group;
  if (str::startsWith(group, "College ")) group = group.substr(8);
  std::string sport = str::lower(group);
  // "College Football" -> "college football", then the first letter up.
  for (const std::string& word : {std::string("College"), group}) {
    for (size_t at = name.find(word); at != std::string::npos; at = name.find(word, at + word.size()))
      name.replace(at, word.size(), str::lower(word));
  }
  if (name.find(sport) == std::string::npos) name += " " + sport;
  if (!name.empty() && name[0] >= 'a' && name[0] <= 'z') name[0] = static_cast<char>(name[0] - 'a' + 'A');
  return name;
}

bool eventHidden(const MySportsPrefs& prefs, const SportsEvent& e) {
  if (prefs.hidden.empty()) return false;
  if (isHidden(prefs, competitionOf(e))) return true;
  const std::string& shortName = e.game ? e.game->leagueShort : e.leagueShort;
  const std::string& group = e.game ? e.game->league : e.league;
  return isHidden(prefs, shortName) || isHidden(prefs, sportOfGroup(group));
}

namespace {

const std::map<std::string, std::string, std::less<>>& footballConferences() {
  // The schedule's football group ids: FBS, then FCS. The Missouri Valley
  // and the CAA play football as conferences of their own (other schools
  // than basketball's), so they are named so and not put as basketball's.
  static const std::map<std::string, std::string, std::less<>> kNames = {
      {"1", "ACC"},          {"4", "Big 12"},          {"5", "Big Ten"},          {"8", "SEC"},
      {"9", "Pac-12"},       {"12", "Conference USA"}, {"15", "MAC"},             {"17", "Mountain West"},
      {"18", "FBS Independents"}, {"37", "Sun Belt"},  {"151", "American"},       {"20", "Big Sky"},
      {"21", "Missouri Valley Football"}, {"22", "Ivy League"}, {"24", "MEAC"},            {"25", "NEC"},
      {"27", "Patriot League"}, {"28", "Pioneer League"}, {"29", "Southern"},     {"30", "Southland"},
      {"31", "SWAC"},        {"32", "FCS Independents"}, {"48", "CAA Football"},           {"177", "UAC"},
      {"179", "Big South-OVC"}};
  return kNames;
}

const std::map<std::string, std::string, std::less<>>& basketballConferences() {
  // The schedule's Division I basketball group ids (the Summit League is
  // 49 for men, 47 for women).
  static const std::map<std::string, std::string, std::less<>> kNames = {
      {"1", "America East"}, {"2", "ACC"},             {"3", "Atlantic 10"},   {"4", "Big East"},
      {"5", "Big Sky"},      {"6", "Big South"},       {"7", "Big Ten"},       {"8", "Big 12"},
      {"9", "Big West"},     {"10", "CAA"},            {"11", "Conference USA"}, {"12", "Ivy League"},
      {"13", "MAAC"},        {"14", "MAC"},            {"16", "MEAC"},         {"18", "Missouri Valley"},
      {"19", "NEC"},         {"20", "OVC"},            {"21", "Pac-12"},       {"22", "Patriot League"},
      {"23", "SEC"},         {"24", "Southern"},       {"25", "Southland"},    {"26", "SWAC"},
      {"27", "Sun Belt"},    {"29", "WCC"},            {"30", "UAC"},          {"44", "Mountain West"},
      {"45", "Horizon"},     {"46", "ASUN"},           {"47", "Summit League"}, {"49", "Summit League"},
      {"62", "American"}};
  return kNames;
}

}  // namespace

std::string ncaaConferenceName(std::string_view seo) {
  // Slugs that are an acronym, or say more than their words.
  static const std::map<std::string, std::string, std::less<>> kWhole = {
      {"socon", "SoCon"}, {"cusa", "C-USA"}, {"pacwest", "PacWest"}, {"pac-12", "Pac-12"},
      {"g-mac", "G-MAC"}, {"ne10", "NE10"},  {"c2c", "C2C"},         {"uac-", "UAC"},
      {"ind", "Independents"}};
  static const std::set<std::string, std::less<>> kAcronyms = {
      "aac",   "acc",   "amcc",  "asc",   "asun",  "caa",   "cacc",  "ccaa",  "ccs",   "cciw",  "cfhc",
      "ciaa",  "cne",   "cunyac", "dii",  "diii",  "ecac",  "ecc",   "gac",   "glvc",  "gliac", "gnac",
      "hcac",  "iiac",  "mac",   "mascac", "meac", "mec",   "miac",  "mvc",   "mwc",   "nacc",  "ncac",
      "ncha",  "nec",   "nescac", "newmac", "njac", "nsic", "nwc",   "oac",   "odac",  "ovc",   "pac",
      "psac",  "rmac",  "saa",   "sac",   "scac",  "sciac", "siac",  "sliac", "sunyac", "swac", "uaa",
      "uchc",  "umac",  "usa",   "wcc",   "wiac",  "wac",   "sec",   "aec",   "ii",    "iii",
      "ncaa",  "necc",  "cccwih"};
  auto whole = kWhole.find(seo);
  if (whole != kWhole.end()) return whole->second;
  std::string out;
  std::string word;
  auto flush = [&]() {
    if (word.empty()) return;
    std::string bare = word;
    // "(men)": the brackets stay, the word inside is the word.
    std::string open, close;
    if (!bare.empty() && bare.front() == '(') {
      open = "(";
      bare = bare.substr(1);
    }
    if (!bare.empty() && bare.back() == ')') {
      close = ")";
      bare.pop_back();
    }
    bool vowel = bare.find_first_of("aeiouy") != std::string::npos;
    if (kAcronyms.count(bare) || (!vowel && !bare.empty() && !(bare[0] >= '0' && bare[0] <= '9'))) {
      bare = str::upperAscii(bare);
    } else if (!bare.empty() && bare[0] >= 'a' && bare[0] <= 'z') {
      bare[0] = static_cast<char>(bare[0] - 'a' + 'A');
    }
    if (!out.empty()) out += " ";
    out += open + bare + close;
    word.clear();
  };
  for (char c : seo) {
    if (c == '-' || c == ' ') flush();
    else word += c;
  }
  flush();
  return out;
}

std::optional<std::string> conferenceName(std::string_view leagueShort, std::string_view key) {
  if (str::startsWith(key, "ncaa:")) {
    // The pages' "every conference" filter and the teams outside the NCAA
    // aren't conferences to pick.
    std::string_view seo = key.substr(5);
    if (seo == "all-conf" || seo == "non-ncaa-org") return std::nullopt;
    std::string name = ncaaConferenceName(seo);
    if (name.empty()) return std::nullopt;
    return name;
  }
  const auto& names = collegeKey(leagueShort) == "NCAAF" ? footballConferences() : basketballConferences();
  auto it = names.find(key);
  if (it == names.end()) return std::nullopt;
  return it->second;
}

std::optional<std::string> basketballConferenceKey(std::string_view footballKey) {
  auto named = footballConferences().find(footballKey);
  if (named == footballConferences().end()) return std::nullopt;
  for (const auto& [key, name] : basketballConferences()) {
    if (name == named->second) return key;
  }
  return std::nullopt;
}

std::vector<HiddenPick> hiddenPicks(const MySportsPrefs& prefs) {
  std::vector<HiddenPick> out;
  for (const std::string& id : prefs.hidden) out.push_back({id, competitionName(id), "Hidden from a game card"});
  for (const char* id : {kProWrestlingPick, kHighSchoolPick}) {
    if (contains(prefs.leagues, id) || contains(prefs.hidden, id)) continue;
    out.push_back({id, competitionName(id), "Hidden unless you turn it on"});
  }
  return out;
}

namespace {

// Changing the picks by hand sets them up: unconfigured ones become the
// defaults the board was showing.
void startFromDefaults(MySportsPrefs& prefs, const std::set<std::string>& countries) {
  if (prefs.configured) return;
  prefs.leagues = defaultPicks(countries).leagues;
  prefs.configured = true;
}

}  // namespace

void hidePick(MySportsPrefs& prefs, const std::string& id, const std::set<std::string>& countries) {
  if (id.empty()) return;
  startFromDefaults(prefs, countries);
  removeAll(prefs.leagues, id);
  addOnce(prefs.hidden, id);
}

void showPick(MySportsPrefs& prefs, const std::string& id, const std::set<std::string>& countries) {
  if (id.empty()) return;
  startFromDefaults(prefs, countries);
  removeAll(prefs.hidden, id);
  // Picks get their row back; a league that isn't one ("NCAAMH") is only
  // shown again.
  if (pickGroup(id)) addOnce(prefs.leagues, id);
}

void followTeam(MySportsPrefs& prefs, const std::string& key, bool follow, const std::set<std::string>& countries) {
  if (key.empty() || contains(prefs.teams, key) == follow) return;
  startFromDefaults(prefs, countries);
  if (follow) prefs.teams.push_back(key);
  else removeAll(prefs.teams, key);
}

std::map<std::string, int> suggestionCounts(const std::vector<catalog::LiveEvent>& events) {
  std::map<std::string, int> counts;
  for (const catalog::LiveEvent& event : events) {
    std::string text = str::lower(event.title + " " + event.family);
    std::set<std::string> family;
    auto f = matcher::sportFamilies().find(event.league);
    if (f != matcher::sportFamilies().end()) family = f->second;
    for (const League& league : leagues()) {
      if (!(league.group == event.league || family.count(league.group) ||
            (league.soccer && event.league == "Soccer"))) {
        continue;
      }
      if (!league.keywords.empty()) {
        bool named = std::any_of(league.keywords.begin(), league.keywords.end(),
                                 [&](const std::string& k) { return catalog::text::hasWord(text, k); });
        if (!named) continue;
      } else if (league.shortName == "NCAAM" && text.find("women") != std::string::npos) {
        continue;
      } else if (league.soccer && league.group == "Soccer") {
        continue;
      }
      counts[league.shortName] += 1;
    }
    for (const OtherSport& sport : otherSports()) {
      if (sport.group == event.league) counts[sport.id] += 1;
    }
  }
  return counts;
}

bool scheduleTeamReminders(const MySportsPrefs& prefs, const std::vector<SportsEvent>& events,
                           time::Seconds now, const ReminderTargetFor& targetFor,
                           Reminders& reminders, std::vector<std::string>& reminded) {
  if (!prefs.reminders || prefs.teams.empty() || !targetFor) return false;
  std::set<std::string> done(reminded.begin(), reminded.end());
  bool changed = false;
  for (const SportsEvent& event : events) {
    if (!event.game || !isFavorite(prefs, *event.game) || !event.start) continue;
    if (*event.start <= now || *event.start >= now + 24 * 3600 || done.count(event.id)) continue;
    auto target = targetFor(event);
    if (!target) continue;
    if (!reminders.isSet(target->channelID, target->start)) reminders.toggle(*target);
    done.insert(event.id);
    reminded.push_back(event.id);
    changed = true;
  }
  // Oldest first, so trimming drops the oldest.
  if (changed && reminded.size() > kRemindedCap) {
    reminded.erase(reminded.begin(), reminded.end() - static_cast<std::ptrdiff_t>(kRemindedCap));
  }
  return changed;
}

}  // namespace marigold::sports
