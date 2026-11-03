// Build 127, choosing your sports v2: the picks model (college settings,
// hidden ids, version and the update note in the JSON, migration from
// version 1), the defaults by the lineup's countries and why each is
// ticked, college rows' rule and subtitles, soccer competitions, what a
// game card hides, conference names, the Hidden list, hide / show / follow.
#include <algorithm>

#include "core/sports/leagues.hpp"
#include "core/sports/my_sports.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using sportsdata::eventOf;
using sportsdata::game;
using sportsdata::plainEvent;
using sportsdata::team;
using sportsdata::utc;

namespace {

bool has(const std::vector<std::string>& list, const std::string& id) {
  return std::find(list.begin(), list.end(), id) != list.end();
}

SportsTeam ranked(const std::string& id, const std::string& name, std::optional<int> rank,
                  std::optional<std::string> conference = std::nullopt) {
  SportsTeam t = team(id, name);
  t.rank = rank;
  t.conference = std::move(conference);
  return t;
}

SportsGame college(const std::string& league, SportsTeam home, SportsTeam away) {
  const League* l = leagueByShort(league);
  return game(league, l ? l->group : "College Football", "1", std::move(home), std::move(away),
              utc(2026, 10, 31, 17, 0));
}

}  // namespace

TEST_CASE("picks v2 JSON: college settings, hidden ids, version and the note round trip") {
  MySportsPrefs prefs;
  prefs.configured = true;
  prefs.leagues = {"NCAAF", "EPL", kUSAPick};
  prefs.teams = {"NCAAF|25"};
  CollegePick football;
  football.top25 = false;
  football.conferences = {"5", "9"};
  CollegePick hoops;
  hoops.everyGame = true;
  prefs.college = {{"NCAAF", football}, {"NCAAM", hoops}};
  prefs.hidden = {"LMX", "S:Golf"};
  prefs.updateNoted = false;
  auto back = mySportsFromJson(toJson(prefs));
  REQUIRE(back);
  CHECK(*back == prefs);
  CHECK(back->version == kMySportsVersion);
  CHECK(back->college.at("NCAAF").conferences == std::vector<std::string>{"5", "9"});
  CHECK(back->college.at("NCAAM").everyGame);
  CHECK_FALSE(back->updateNoted);
  // Any change to the new fields is a change.
  CHECK(compare(prefs, *back).changed == false);
  MySportsPrefs other = prefs;
  other.college["NCAAF"].conferences.push_back("8");
  CHECK(compare(prefs, other).changed);
  CHECK_FALSE(compare(prefs, other).leaguesChanged);  // college settings only filter the rows
  other = prefs;
  other.hidden.push_back("NBA");
  CHECK(compare(prefs, other).changed);
  CHECK(compare(prefs, other).leaguesChanged);  // hidden ids aren't looked up
  other = prefs;
  other.leagues.push_back(kSoccerAllPick);
  CHECK(compare(prefs, other).leaguesChanged);  // a soccer pick changes what is looked up
  other = prefs;
  other.updateNoted = true;
  CHECK(compare(prefs, other).changed);
  CHECK_FALSE(compare(prefs, other).leaguesChanged);
  // Wrong types fail the decode.
  CHECK_FALSE(mySportsFromJson(R"({"hidden": "LMX"})"));
  CHECK_FALSE(mySportsFromJson(R"({"college": {"NCAAF": {"top25": "yes"}}})"));
  CHECK_FALSE(mySportsFromJson(R"({"college": {"NCAAF": {"conferences": [8]}}})"));
  CHECK_FALSE(mySportsFromJson(R"({"version": "2"})"));
  CHECK_FALSE(mySportsFromJson(R"({"updateNoted": 1})"));
}

TEST_CASE("a version 1 file reads as version 1 and is migrated: picks kept, UCL and USA with soccer, the note") {
  // Build 126's file: no version, no college, no hidden.
  auto old = mySportsFromJson(
      R"({"configured": true, "leagues": ["NFL", "LIGA", "NCAAF"], "teams": ["NFL|1"], "reminders": false, "prompted": true})");
  REQUIRE(old);
  CHECK(old->version == 1);
  CHECK(old->updateNoted);
  MySportsPrefs migrated = *old;
  migrate(migrated);
  CHECK(migrated.version == kMySportsVersion);
  CHECK_FALSE(migrated.updateNoted);
  CHECK(migrated.leagues == std::vector<std::string>{"NFL", "LIGA", "NCAAF", "UCL", kUSAPick});
  CHECK(migrated.teams == std::vector<std::string>{"NFL|1"});
  CHECK(migrated.college.empty());  // college: the Top 25 and your schools
  CHECK(collegePick(migrated, "NCAAF") == CollegePick());
  // Migrating again changes nothing.
  MySportsPrefs again = migrated;
  migrate(again);
  CHECK(again == migrated);
  // UCL already there is not added twice; no soccer, nothing added.
  MySportsPrefs ucl;
  ucl.configured = true;
  ucl.version = 1;
  ucl.leagues = {"UCL", "MLS"};
  migrate(ucl);
  CHECK(ucl.leagues == std::vector<std::string>{"UCL", "MLS", kUSAPick});
  MySportsPrefs noSoccer;
  noSoccer.configured = true;
  noSoccer.version = 1;
  noSoccer.leagues = {"NBA"};
  migrate(noSoccer);
  CHECK(noSoccer.leagues == std::vector<std::string>{"NBA"});
  CHECK_FALSE(noSoccer.updateNoted);
  // Unconfigured: only the version and the note.
  auto unset = mySportsFromJson(R"({"configured": false, "prompted": true})");
  REQUIRE(unset);
  MySportsPrefs u = *unset;
  migrate(u);
  CHECK(u.version == kMySportsVersion);
  CHECK_FALSE(u.updateNoted);
  CHECK(u.leagues.empty());
  CHECK(u.prompted);
  // A new viewer (no file) is version 2 with no note.
  CHECK(MySportsPrefs().version == kMySportsVersion);
  CHECK(MySportsPrefs().updateNoted);
}

TEST_CASE("default picks by the lineup's countries, and why each was ticked") {
  std::vector<std::string> everyone = {"NFL", "NCAAF", "NBA", "WNBA", "NCAAM", "NCAAW", "MLB",
                                       "NHL", "EPL",   "UCL", "MLS",  "NWSL",  kUSAPick, "S:Volleyball"};
  MySportsPrefs english = defaultPicks({"US"});
  CHECK(english.leagues == everyone);
  CHECK_FALSE(english.configured);
  CHECK(english.version == kMySportsVersion);
  CHECK(english.updateNoted);
  CHECK(defaultPicks({}).leagues == everyone);
  for (const char* spanish : {"US-ES", "MX"}) {
    MySportsPrefs es = defaultPicks({"US", spanish});
    CHECK(has(es.leagues, "LMX"));
    CHECK(has(es.leagues, kMexicoPick));
    CHECK_FALSE(has(es.leagues, "S:Cricket"));
    CHECK(pickReason("LMX", {"US", spanish}) == "For Espa\xC3\xB1ol");
    CHECK(pickReason(kMexicoPick, {spanish}) == "For Espa\xC3\xB1ol");
  }
  CHECK_FALSE(has(english.leagues, "LMX"));
  CHECK_FALSE(has(english.leagues, "S:Cricket"));
  for (const char* country : {"IN", "PK", "BD", "LK"}) CHECK(has(defaultPicks({"US", country}).leagues, "S:Cricket"));
  CHECK(pickReason("S:Cricket", {"US", "IN"}) == "For Hindi");
  CHECK(pickReason("S:Cricket", {"PK"}) == "For Urdu");
  CHECK(pickReason("S:Cricket", {"BD"}) == "For Bengali");
  CHECK(pickReason("S:Cricket", {"LK"}) == "For Tamil");
  // Ticked for everyone, or not at all: no reason.
  CHECK(pickReason("NFL", {"US", "US-ES"}) == "");
  CHECK(pickReason("LMX", {"US"}) == "");
  CHECK(pickReason("S:Cricket", {"US"}) == "");
  CHECK(pickReason("S:Golf", {"IN"}) == "");
  // Unconfigured, the board and the schedule go by the defaults; set up, by
  // the picks.
  MySportsPrefs unset;
  unset.hidden = {"EPL"};
  MySportsPrefs effective = effectivePicks(unset, {"MX"});
  CHECK(has(effective.leagues, "LMX"));
  CHECK(effective.hidden == std::vector<std::string>{"EPL"});
  CHECK_FALSE(effective.configured);
  MySportsPrefs set;
  set.configured = true;
  set.leagues = {"NBA"};
  CHECK(effectivePicks(set, {"MX"}).leagues == std::vector<std::string>{"NBA"});
  set.leagues.clear();  // "I don't watch sports"
  CHECK(effectivePicks(set, {"US"}).leagues.empty());
}

TEST_CASE("college rows: every game, the Top 25, your conferences, your schools") {
  MySportsPrefs prefs;
  SportsGame plain = college("NCAAF", ranked("1", "Harbor State", std::nullopt, std::string("5")),
                             ranked("2", "Valley Tech", std::nullopt, std::string("8")));
  SportsGame top = college("NCAAF", ranked("3", "Ridge", 4), ranked("4", "Coast", std::nullopt));
  // The default: the Top 25 and your schools.
  CHECK(inCollegeRow(prefs, top));
  CHECK_FALSE(inCollegeRow(prefs, plain));
  // A rank past 25 (or none) is not the Top 25.
  SportsGame odd = college("NCAAF", ranked("5", "Bay", 30), ranked("6", "Point", 0));
  CHECK_FALSE(inCollegeRow(prefs, odd));
  // Your school, followed under the league or the college sport it follows.
  prefs.teams = {"NCAAF|2"};
  CHECK(inCollegeRow(prefs, plain));
  SportsGame fcs = plain;
  fcs.leagueShort = "FCS";
  CHECK(inCollegeRow(prefs, fcs));
  prefs.teams = {"FCS|1"};
  CHECK(inCollegeRow(prefs, fcs));
  CHECK_FALSE(inCollegeRow(prefs, plain));
  prefs.teams.clear();
  // Your conference.
  prefs.college["NCAAF"].conferences = {"8"};
  CHECK(inCollegeRow(prefs, plain));
  CHECK(inCollegeRow(prefs, fcs));  // FCS follows college football's settings
  // Top 25 off: a ranked team alone isn't enough.
  prefs.college["NCAAF"].top25 = false;
  CHECK_FALSE(inCollegeRow(prefs, top));
  // Every Division I game.
  prefs.college["NCAAF"].everyGame = true;
  CHECK(inCollegeRow(prefs, top));
  CHECK(inCollegeRow(prefs, odd));
  // Each college sport on its own.
  SportsGame hoops = college("NCAAM", ranked("1", "Harbor State", std::nullopt), ranked("2", "Valley Tech", std::nullopt));
  CHECK_FALSE(inCollegeRow(prefs, hoops));
  SportsGame d3 = hoops;
  d3.leagueShort = "D3MBB";
  prefs.college["NCAAM"].conferences = {"ncaa:little-east"};
  d3.home.conference = std::string("ncaa:little-east");
  CHECK(inCollegeRow(prefs, d3));  // the small-college pages follow men's basketball
  // Which leagues filter, and whose settings they follow.
  CHECK(isCollegeLeague("NCAAF"));
  CHECK(isCollegeLeague("FCS"));
  CHECK(isCollegeLeague("NCAAWV"));
  CHECK(isCollegeLeague("NCAAMH"));
  CHECK(isCollegeLeague("D2WBB"));
  CHECK(isCollegeLeague("NCAASB"));
  CHECK_FALSE(isCollegeLeague("D3WSO"));  // college soccer: every other competition
  CHECK_FALSE(isCollegeLeague("PLL"));
  CHECK_FALSE(isCollegeLeague("NFL"));
  CHECK(collegeKey("D2/D3") == "NCAAF");
  CHECK(collegeKey("D2WBB") == "NCAAW");
  CHECK(collegeKey("D3WVB") == "NCAAWV");
  CHECK(collegeKey("D3WHK") == "NCAAWH");
  CHECK(collegeKey("D2FH") == "NCAAFH");
  CHECK(collegeKey("NCAAMV") == "NCAAMV");
  // Subtitles.
  CHECK(collegeSubtitle(CollegePick()) == "Top 25 and your schools");
  CollegePick p;
  p.conferences = {"5"};
  CHECK(collegeSubtitle(p) == "Top 25, your conferences and schools");
  p.top25 = false;
  CHECK(collegeSubtitle(p) == "Your conferences and schools");
  p.conferences.clear();
  CHECK(collegeSubtitle(p) == "Your conferences and schools");
  p.everyGame = true;
  CHECK(collegeSubtitle(p) == "Every Division I game");
}

TEST_CASE("soccer competitions for the picker, in Popular order") {
  const auto& list = soccerCompetitions();
  REQUIRE(list.size() >= 12);
  std::vector<std::string> popular;
  for (const SoccerCompetition& c : list) {
    if (has(c.groups, "Popular")) popular.push_back(c.id);
  }
  CHECK(popular == std::vector<std::string>{"EPL", "UCL", "MLS", "NWSL", kUSAPick, "LMX", kMexicoPick, "LIGA",
                                            "UEL", "BUN", "SA", kSoccerAllPick});
  CHECK(list.front().name == "Premier League");
  CHECK(list.front().place == "England");
  auto find = [&](const std::string& id) -> const SoccerCompetition& {
    return *std::find_if(list.begin(), list.end(), [&](const SoccerCompetition& c) { return c.id == id; });
  };
  CHECK(find(kUSAPick).name == "USA men and women");
  CHECK(find(kUSAPick).place == "World Cup, Gold Cup, friendlies");
  CHECK(has(find(kUSAPick).groups, "National teams"));
  CHECK(has(find(kMexicoPick).groups, "National teams"));
  CHECK(has(find(kMexicoPick).groups, "Mexico and the Americas"));
  CHECK(find(kSoccerAllPick).name == "Every other competition");
  CHECK(find(kSoccerAllPick).place == "Any league, any level");
  CHECK(has(find("NWSL").groups, "Women's"));
  // Every soccer league is a competition; each id once.
  for (const League& l : leagues()) {
    if (l.soccer) CHECK(isSoccerPick(l.shortName));
  }
  std::set<std::string> ids;
  for (const SoccerCompetition& c : list) CHECK(ids.insert(c.id).second);
  // The new picks are named and go in the Soccer row.
  CHECK(nameOf(kUSAPick) == std::string("USA men and women"));
  CHECK(pickGroup(kMexicoPick) == std::string("Soccer"));
  CHECK(pickGroup(kSoccerAllPick) == std::string("Soccer"));
  MySportsPrefs prefs;
  prefs.leagues = {"MLS", kUSAPick, "NFL", "EPL"};
  CHECK(pickedGroups(prefs) == std::vector<std::string>{"MLS", "Soccer", "NFL"});
  // National teams by name.
  CHECK(nationalTeam(team("1", "United States")) == std::string(kUSAPick));
  CHECK(nationalTeam(team("1", "USA")) == std::string(kUSAPick));
  CHECK(nationalTeam(team("1", "United States Women")) == std::string(kUSAPick));
  CHECK(nationalTeam(team("1", "M\xC3\xA9xico")) == std::string(kMexicoPick));
  CHECK(nationalTeam(team("1", "Mexico Women")) == std::string(kMexicoPick));
  CHECK_FALSE(nationalTeam(team("1", "Mexico U20")));
  CHECK_FALSE(nationalTeam(team("1", "United States U17")));
  CHECK_FALSE(nationalTeam(team("1", "Harbor United")));
}

TEST_CASE("what a game card hides, and its name") {
  Seconds start = utc(2026, 10, 31, 17, 0);
  auto ev = [&](const std::string& league, const std::string& group) {
    return eventOf(game(league, group, "1", team("1", "Harbor"), team("2", "Valley"), start));
  };
  CHECK(competitionOf(ev("LMX", "Soccer")) == "LMX");
  CHECK(competitionName("LMX") == "Liga MX");
  CHECK(competitionOf(ev("MLS", "MLS")) == "MLS");
  CHECK(competitionOf(ev("NBA", "NBA")) == "NBA");
  CHECK(competitionName("NBA") == "NBA");
  CHECK(competitionOf(ev("PGA", "Golf")) == "S:Golf");
  CHECK(competitionName("S:Golf") == "Golf");
  CHECK(competitionOf(ev("UFC", "Fighting")) == "S:Fighting");
  CHECK(competitionOf(ev("NCAAWV", "Volleyball")) == "S:Volleyball");
  CHECK(competitionOf(ev("NCAAMH", "Hockey")) == "NCAAMH");
  CHECK(competitionName("NCAAMH") == "Men's college hockey");
  CHECK(competitionName("NCAAM") == "Men's college basketball");
  CHECK(competitionName("NCAAF") == "College football");
  CHECK(competitionName("FCS") == "FCS football");
  CHECK(competitionName("D2WBB") == "Women's Division II basketball");
  CHECK(competitionName("D3WSO") == "Women's Division III soccer");
  CHECK(competitionName("NCAASB") == "College softball");
  CHECK(competitionName(kUSAPick) == "USA men and women");
  CHECK(competitionName(kProWrestlingPick) == "Pro wrestling");
  CHECK(competitionName(kHighSchoolPick) == "High school");
  CHECK(competitionName("NOPE") == "NOPE");
  // The all-soccer listing's games, by their competition or national team.
  SportsGame all = game(kSoccerAll, "Soccer", "2", team("1", "Harbor"), team("2", "Valley"), start);
  all.competition = std::string("LIGA");
  CHECK(competitionOf(eventOf(all)) == "LIGA");
  all.competition.reset();
  CHECK(competitionOf(eventOf(all)) == kSoccerAllPick);
  all.home = team("3", "United States");
  CHECK(competitionOf(eventOf(all)) == kUSAPick);
  // Events no schedule has: their sport's pick, or any soccer.
  CHECK(competitionOf(plainEvent("g", "Golf: Final Round", "Golf", start)) == "S:Golf");
  CHECK(competitionOf(plainEvent("s", "Harbor FC vs Bay FC", "Soccer", start)) == kSoccerAllPick);
  SportsEvent hs = plainEvent("h", "Harbor vs Valley", "Football", start);
  hs.highSchool = true;
  CHECK(competitionOf(hs) == kHighSchoolPick);
  CHECK(competitionOf(plainEvent("o", "Something", "Other", start)) == "");
  // Hidden: by the competition, the league or the sport.
  MySportsPrefs prefs;
  prefs.hidden = {"LMX"};
  CHECK(eventHidden(prefs, ev("LMX", "Soccer")));
  CHECK_FALSE(eventHidden(prefs, ev("EPL", "Soccer")));
  prefs.hidden = {"S:Volleyball"};
  CHECK(eventHidden(prefs, ev("D2WVB", "Volleyball")));
  prefs.hidden = {"NCAAMH"};
  CHECK(eventHidden(prefs, ev("NCAAMH", "Hockey")));
  CHECK_FALSE(eventHidden(prefs, ev("NHL", "NHL")));
}

TEST_CASE("conference names: football's and basketball's group ids, the NCAA pages' slugs") {
  CHECK(conferenceName("NCAAF", "8") == std::string("SEC"));
  CHECK(conferenceName("NCAAF", "5") == std::string("Big Ten"));
  CHECK(conferenceName("NCAAF", "1") == std::string("ACC"));
  CHECK(conferenceName("FCS", "48") == std::string("CAA Football"));
  CHECK(conferenceName("FCS", "21") == std::string("Missouri Valley Football"));
  CHECK(conferenceName("D2/D3", "5") == std::string("Big Ten"));  // football's table
  CHECK(conferenceName("NCAAM", "8") == std::string("Big 12"));
  CHECK(conferenceName("NCAAW", "23") == std::string("SEC"));
  CHECK(conferenceName("NCAAM", "1") == std::string("America East"));
  CHECK(conferenceName("NCAAWV", "7") == std::string("Big Ten"));  // basketball's (volleyball has none)
  CHECK(conferenceName("NCAAW", "47") == std::string("Summit League"));
  CHECK_FALSE(conferenceName("NCAAF", "104"));  // a Division II conference not known: matched, not listed
  CHECK(conferenceName("D3MBB", "ncaa:little-east") == std::string("Little East"));
  CHECK(basketballConferenceKey("8") == std::string("23"));  // the SEC
  CHECK(basketballConferenceKey("5") == std::string("7"));   // the Big Ten
  CHECK_FALSE(basketballConferenceKey("18"));                // football's independents
  // Football conferences with other schools than basketball's of a like name.
  CHECK_FALSE(basketballConferenceKey("21"));
  CHECK_FALSE(basketballConferenceKey("48"));
  CHECK(ncaaConferenceName("little-east") == "Little East");
  CHECK(ncaaConferenceName("sunyac") == "SUNYAC");
  CHECK(ncaaConferenceName("mascac") == "MASCAC");
  CHECK(ncaaConferenceName("mac-commonwealth") == "MAC Commonwealth");
  CHECK(ncaaConferenceName("great-northeast") == "Great Northeast");
  CHECK(ncaaConferenceName("ne10") == "NE10");
  CHECK(ncaaConferenceName("ccs") == "CCS");  // no vowel: an acronym
  CHECK(ncaaConferenceName("socon") == "SoCon");
  CHECK(ncaaConferenceName("cusa") == "C-USA");
  CHECK(ncaaConferenceName("usa-south") == "USA South");
  CHECK(ncaaConferenceName("atlantic-10") == "Atlantic 10");
  CHECK(ncaaConferenceName("uchc-(men)") == "UCHC (Men)");
  CHECK(ncaaConferenceName("centennial") == "Centennial");
  CHECK(ncaaConferenceName("division-iii-independents") == "Division III Independents");
  CHECK(ncaaConferenceName("dii-independent") == "DII Independent");
  CHECK(ncaaConferenceName("ind") == "Independents");
  CHECK(ncaaConferenceName("cccwih") == "CCCWIH");
  CHECK(ncaaConferenceName("non-ncaa-org") == "Non NCAA Org");
  // Not conferences to pick (still matched).
  CHECK_FALSE(conferenceName("D3WVB", "ncaa:all-conf"));
  CHECK_FALSE(conferenceName("D3WVB", "ncaa:non-ncaa-org"));
}

TEST_CASE("hide, show again and follow: the Hidden list and the picks") {
  MySportsPrefs prefs;
  prefs.configured = true;
  prefs.leagues = {"NBA", "LMX", "EPL"};
  // Pro wrestling and high school are listed while not picked.
  std::vector<HiddenPick> list = hiddenPicks(prefs);
  REQUIRE(list.size() == 2);
  CHECK(list[0].id == kProWrestlingPick);
  CHECK(list[0].name == "Pro wrestling");
  CHECK(list[0].why == "Hidden unless you turn it on");
  CHECK(list[1].name == "High school");
  // Hidden from a game card: out of the picks, first in the list.
  hidePick(prefs, "LMX", {"US"});
  CHECK(prefs.leagues == std::vector<std::string>{"NBA", "EPL"});
  CHECK(prefs.hidden == std::vector<std::string>{"LMX"});
  list = hiddenPicks(prefs);
  REQUIRE(list.size() == 3);
  CHECK(list[0].id == "LMX");
  CHECK(list[0].name == "Liga MX");
  CHECK(list[0].why == "Hidden from a game card");
  // A league that isn't a pick is only hidden.
  hidePick(prefs, "NCAAMH", {"US"});
  CHECK(prefs.hidden == std::vector<std::string>{"LMX", "NCAAMH"});
  showPick(prefs, "NCAAMH", {"US"});
  CHECK(prefs.hidden == std::vector<std::string>{"LMX"});
  CHECK(prefs.leagues == std::vector<std::string>{"NBA", "EPL"});
  // Shown again: back in the picks.
  showPick(prefs, "LMX", {"US"});
  CHECK(prefs.hidden.empty());
  CHECK(prefs.leagues == std::vector<std::string>{"NBA", "EPL", "LMX"});
  // Pro wrestling turned on from the list.
  showPick(prefs, kProWrestlingPick, {"US"});
  CHECK(proWrestlingOn(prefs));
  CHECK(hiddenPicks(prefs).size() == 1);
  // Follow and unfollow a team.
  followTeam(prefs, "LMX|10", true, {"US"});
  followTeam(prefs, "LMX|10", true, {"US"});
  CHECK(prefs.teams == std::vector<std::string>{"LMX|10"});
  followTeam(prefs, "LMX|10", false, {"US"});
  CHECK(prefs.teams.empty());
  // Unconfigured: the defaults the board was showing become the picks first.
  MySportsPrefs unset;
  hidePick(unset, "EPL", {"US", "US-ES"});
  CHECK(unset.configured);
  CHECK(has(unset.leagues, "LMX"));
  CHECK_FALSE(has(unset.leagues, "EPL"));
  CHECK(unset.hidden == std::vector<std::string>{"EPL"});
  MySportsPrefs fresh;
  followTeam(fresh, "NBA|3", true, {"US"});
  CHECK(fresh.configured);
  CHECK(fresh.leagues == defaultPicks({"US"}).leagues);
  CHECK(fresh.teams == std::vector<std::string>{"NBA|3"});
  CHECK(isHidden(unset, "EPL"));
  CHECK_FALSE(isHidden(unset, ""));
}
