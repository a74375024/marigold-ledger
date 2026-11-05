// The leagues and teams the viewer follows (MySports.swift). Live Events
// leads with their teams, gives each picked league a row, and folds every
// other sport into "More events". The schedule is only looked up for
// picked leagues. Saving the JSON is the caller's.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/sports/reminders.hpp"
#include "core/sports/types.hpp"

namespace marigold::sports {

// Build 127: a college sport's settings, per college league id ("NCAAF",
// "NCAAM", "NCAAW", "NCAAWV", "NCAAMV", hockey "NCAAMH"/"NCAAWH",
// lacrosse...; collegeKey() says which one a league follows). Missing from
// MySportsPrefs::college = the defaults: the Top 25 and your schools.
struct CollegePick {
  bool top25 = true;        // games with a ranked team (this week's poll)
  bool everyGame = false;   // "Every Division I game" (Build 126's behaviour)
  std::vector<std::string> conferences;  // conference keys (Conference::key)

  bool operator==(const CollegePick& o) const {
    return top25 == o.top25 && everyGame == o.everyGame && conferences == o.conferences;
  }
  bool operator!=(const CollegePick& o) const { return !(*this == o); }
};

constexpr int kMySportsVersion = 2;

struct MySportsPrefs {
  bool configured = false;
  // League ids ("NFL", "EPL"), soccer competitions ("USA", "S:SoccerAll")
  // and other sports ("S:Golf"), in the order picked.
  std::vector<std::string> leagues;
  // "LEAGUE|teamID".
  std::vector<std::string> teams;
  // A reminder before each favorite team's game (off unless turned on).
  bool reminders = false;
  // The setup has been offered once.
  bool prompted = false;
  // Build 127. Saved in the same JSON; older files have none.
  // College sports' settings by college league id (collegePick()).
  std::map<std::string, CollegePick> college;
  // Competition, league or sport ids hidden from Live Events ("LMX",
  // "S:Golf"): their games are on no row, and they aren't looked up.
  std::vector<std::string> hidden;
  // Files without it are 1 and are migrated when read (migrate()).
  int version = kMySportsVersion;
  // False: the board shows the one-time note about the update.
  bool updateNoted = true;

  bool operator==(const MySportsPrefs& o) const {
    return configured == o.configured && leagues == o.leagues && teams == o.teams &&
           reminders == o.reminders && prompted == o.prompted && college == o.college && hidden == o.hidden &&
           version == o.version && updateNoted == o.updateNoted;
  }
  bool operator!=(const MySportsPrefs& o) const { return !(*this == o); }
};

std::string toJson(const MySportsPrefs& prefs);
// Missing fields keep their defaults; a field of the wrong type fails the
// decode (nullopt: start from MySportsPrefs()). A file without "version"
// reads as version 1 (the caller then migrate()s it).
std::optional<MySportsPrefs> mySportsFromJson(std::string_view text);

// After MySports.update: which follow-ups the change needs.
struct MySportsChange {
  bool changed = false;
  // Leagues, hidden ids or the configured flag changed (what is looked up
  // follows them): Schedule::leaguesChanged. College settings only filter
  // the rows, so they don't.
  bool leaguesChanged = false;
};
MySportsChange compare(const MySportsPrefs& before, const MySportsPrefs& after);

std::string teamKey(std::string_view league, std::string_view teamID);
bool isFavorite(const MySportsPrefs& prefs, const SportsGame& game);

// A sport with no schedule behind it: picking it gives it a row of its own,
// built from your channels. No requests.
struct OtherSport {
  std::string id;     // "S:Golf"
  std::string group;  // the row ("Golf")
  std::string name;
};
const std::vector<OtherSport>& otherSports();
// Pro wrestling is a pick like the other sports, but off by default: Live
// Events leaves it out (WWE, AEW...) unless My Sports has it.
constexpr const char* kProWrestlingPick = "S:ProWrestling";
bool proWrestlingOn(const MySportsPrefs& prefs);
// High-school games have a row of their own; in Live now only when picked.
constexpr const char* kHighSchoolPick = "S:HighSchool";
constexpr const char* kHighSchoolRow = "High School";
bool highSchoolOn(const MySportsPrefs& prefs);

// Display name of a picked id (league, soccer competition or other sport).
std::optional<std::string> nameOf(std::string_view id);

// The row group a pick's games are in ("Soccer" for "EPL" and "USA",
// "Volleyball" for "S:Volleyball"); nullopt for an id that isn't a pick.
std::optional<std::string> pickGroup(std::string_view id);
// Row groups of the picked leagues and sports, in the order picked.
std::vector<std::string> pickedGroups(const MySportsPrefs& prefs);

// MARK: Build 127: picks v2

// The soccer picks that aren't a league of their own.
constexpr const char* kUSAPick = "USA";                // US men's and women's national teams
constexpr const char* kMexicoPick = "MEX";             // Mexico's national team
constexpr const char* kSoccerAllPick = "S:SoccerAll";  // every other soccer competition, any level

// The picks a new viewer starts with. `countries` = catalog::LineupPrefs::
// countries ("US", "US-ES", "MX", "IN", "PK", "BD", "LK"...): everyone gets
// NFL, NCAAF, NBA, WNBA, NCAAM, NCAAW, MLB, NHL, EPL, UCL, MLS, NWSL, USA and
// S:Volleyball; "US-ES" or "MX" adds LMX and MEX; "IN", "PK", "BD" or "LK"
// adds S:Cricket. Not `configured` (the setup saves it so).
MySportsPrefs defaultPicks(const std::set<std::string>& countries);
// What the board and the schedule go by: the picks once set up, else the
// defaults for these countries (with the saved teams, college settings and
// hidden ids). `configured` is kept as saved.
MySportsPrefs effectivePicks(const MySportsPrefs& prefs, const std::set<std::string>& countries);
// Why defaultPicks() ticked `id` for these countries: "For Español", "For
// Hindi", "For Urdu", "For Bengali", "For Tamil"; "" when it's ticked for
// everyone or not at all.
std::string pickReason(std::string_view id, const std::set<std::string>& countries);
bool isHidden(const MySportsPrefs& prefs, std::string_view id);
// The college league whose settings a league follows: FCS and Divisions II
// and III football follow "NCAAF", the small-college basketball pages
// "NCAAM" / "NCAAW", volleyball's "NCAAWV", hockey's "NCAAMH" / "NCAAWH",
// field hockey's "NCAAFH"; Build 136: the small colleges' soccer pages
// "NCAAMS" / "NCAAWS"; any other league itself.
std::string collegeKey(std::string_view leagueShort);
// A college league whose rows filter by CollegePick (every college league
// but soccer's: FBS and FCS football, basketball, volleyball, hockey,
// lacrosse, field hockey, softball, the small-college pages).
bool isCollegeLeague(std::string_view leagueShort);
// Build 136: a college soccer game: from the small colleges' soccer pages
// (D2MSO, D3WSO...) or a Division I game in the all-soccer listing (its
// competition "NCAAM Soccer" / "NCAAW Soccer"). Its row is Soccer, shared
// with the pro competitions, but it follows the college rule
// (inCollegeRow(), settings "NCAAMS" / "NCAAWS"), not the competitions.
bool isCollegeSoccer(const SportsGame& game);
// A game whose row filters by CollegePick: isCollegeLeague() or college soccer.
bool isCollegeGame(const SportsGame& game);
// The settings key a game follows: collegeKey() of its league; college
// soccer's "NCAAMS" / "NCAAWS".
std::string collegeKeyOf(const SportsGame& game);
// The settings for a league (collegeKey()); the defaults when absent.
CollegePick collegePick(const MySportsPrefs& prefs, std::string_view leagueShort);
// A college game goes in its row when the settings take every game, or
// Top 25 is on and a side is ranked 1-25, or a side's conference is
// ticked, or a side is one of your teams ("LEAGUE|id" under the game's
// league or its collegeKeyOf()).
bool inCollegeRow(const MySportsPrefs& prefs, const SportsGame& game);
// A college row's subtitle: "Every Division I game", "Top 25, your
// conferences and schools", "Your conferences and schools" (Top 25 off),
// else "Top 25 and your schools".
std::string collegeSubtitle(const CollegePick& pick);
// Version 1 -> 2: every pick kept; any soccer picked adds UCL and USA;
// college gets the defaults; updateNoted = false. Unconfigured files only
// get the version and updateNoted. Version 2 files are left as they are.
void migrate(MySportsPrefs& prefs);

// Soccer competitions for the picker, in Popular order.
struct SoccerCompetition {
  std::string id;      // "EPL", "USA", "S:SoccerAll"
  std::string name;    // "Premier League", "USA men and women", "Every other competition"
  std::string place;   // "England", "World Cup, Gold Cup, friendlies", "Any league, any level"
  std::vector<std::string> groups;  // rail groups: "Popular", "National teams", "England", "Spain",
                                    // "Italy and Germany", "Rest of Europe",
                                    // "Mexico and the Americas", "Women's", "Every other"
};
const std::vector<SoccerCompetition>& soccerCompetitions();
bool isSoccerPick(std::string_view id);
// "USA" for the US men's or women's national team, "MEX" for Mexico's
// (not youth sides); nullopt for any other team.
std::optional<std::string> nationalTeam(const SportsTeam& team);
// The soccer pick a competition of the all-soccer listing is: by the
// listing's own league id ("700" is the Premier League), else its name
// ("UEFA Champions League"); "" when it's none of the picks' leagues.
std::string soccerPickOfCompetition(std::string_view leagueID, std::string_view name);
// The soccer pick a game is under: a national team's ("USA", "MEX"), a
// league's own ("EPL", "MLS"), the all-soccer listing's competition
// (SportsGame::competition, else its tournament's name), else
// "S:SoccerAll" (any other league, college soccer).
std::string soccerCompetition(const SportsGame& game);

// What hiding an event's tile hides, and the name it goes by: its soccer
// competition ("LMX"), its league when it's a pick ("NBA") or a sport's
// ("S:Golf", "S:Volleyball"), else its league ("NCAAMH"); "" for none.
std::string competitionOf(const SportsEvent& event);
// "Liga MX", "NBA", "Golf", "Men's college hockey"; the id when unknown.
std::string competitionName(std::string_view id);
// Every id an event's games go by for hiding (competitionOf(), its league,
// its sport's pick): hidden when any is.
bool eventHidden(const MySportsPrefs& prefs, const SportsEvent& event);

// College conferences.
struct Conference {
  std::string key;   // the schedule's group id as text ("8"), or "ncaa:<seo>" for the small-college pages
  std::string name;  // "SEC", "Big Ten", "Little East"
  bool operator==(const Conference& o) const { return key == o.key && name == o.name; }
  bool operator!=(const Conference& o) const { return !(*this == o); }
};
// A conference's name for a league's key: football's group ids for the
// football leagues, basketball's for the others (their schools' are
// basketball's), the NCAA pages' slugs title-cased (ncaaConferenceName()).
// nullopt for an id not known (still matched, never listed).
std::optional<std::string> conferenceName(std::string_view leagueShort, std::string_view key);
// "little-east" -> "Little East", "sunyac" -> "SUNYAC", "mac-commonwealth"
// -> "MAC Commonwealth".
std::string ncaaConferenceName(std::string_view seo);
// Football's key for a conference as basketball's ("8", the SEC, is "23");
// nullopt when basketball has no conference of that name.
std::optional<std::string> basketballConferenceKey(std::string_view footballKey);

// Settings › Live Events › Hidden.
struct HiddenPick {
  std::string id, name;
  std::string why;  // "Hidden from a game card" / "Hidden unless you turn it on"
};
// Hidden ids, then pro wrestling and high school when not picked.
std::vector<HiddenPick> hiddenPicks(const MySportsPrefs& prefs);
// Hide from the board: out of the picks, into hidden. Show again: out of
// hidden, into the picks. Unconfigured picks first become the defaults for
// `countries`, configured (so the rest of the board stays as it was).
void hidePick(MySportsPrefs& prefs, const std::string& id, const std::set<std::string>& countries);
void showPick(MySportsPrefs& prefs, const std::string& id, const std::set<std::string>& countries);
// "LEAGUE|id" into or out of the teams (with the same first step).
void followTeam(MySportsPrefs& prefs, const std::string& key, bool follow, const std::set<std::string>& countries);

// How many of the provider's events look like each league (for the "in
// your channels" suggestions).
std::map<std::string, int> suggestionCounts(const std::vector<catalog::LiveEvent>& events);

// MARK: Reminders for favorite teams

// What a reminder for an event points at: its best channel (the store's
// reminderTarget). nullopt when nothing can play it.
using ReminderTargetFor = std::function<std::optional<LiveReminder>(const SportsEvent&)>;

constexpr size_t kRemindedCap = 200;

// Adds a reminder for favorite teams' games starting in the next day, once
// per game (so removing one by hand sticks). `reminded` is the saved list
// of event ids already handled, oldest first; it's trimmed to the last
// kRemindedCap. True when `reminded` changed (save it, and the reminders).
bool scheduleTeamReminders(const MySportsPrefs& prefs, const std::vector<SportsEvent>& events,
                           time::Seconds now, const ReminderTargetFor& targetFor,
                           Reminders& reminders, std::vector<std::string>& reminded);

}  // namespace marigold::sports
