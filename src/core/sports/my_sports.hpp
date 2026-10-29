// The leagues and teams the viewer follows (MySports.swift). Live Events
// leads with their teams, gives each picked league a row, and folds every
// other sport into "More events". The schedule is only looked up for
// picked leagues. Saving the JSON is the caller's.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/sports/reminders.hpp"
#include "core/sports/types.hpp"

namespace marigold::sports {

struct MySportsPrefs {
  bool configured = false;
  // League ids ("NFL", "EPL") and other sports ("S:Golf"), in the order picked.
  std::vector<std::string> leagues;
  // "LEAGUE|teamID".
  std::vector<std::string> teams;
  // A reminder before each favorite team's game (off unless turned on).
  bool reminders = false;
  // The setup has been offered once.
  bool prompted = false;

  bool operator==(const MySportsPrefs& o) const {
    return configured == o.configured && leagues == o.leagues && teams == o.teams &&
           reminders == o.reminders && prompted == o.prompted;
  }
  bool operator!=(const MySportsPrefs& o) const { return !(*this == o); }
};

std::string toJson(const MySportsPrefs& prefs);
// Missing fields keep their defaults; a field of the wrong type fails the
// decode (nullopt: start from MySportsPrefs()).
std::optional<MySportsPrefs> mySportsFromJson(std::string_view text);

// After MySports.update: which follow-ups the change needs.
struct MySportsChange {
  bool changed = false;
  // Leagues or the configured flag changed: Schedule::leaguesChanged.
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

// Display name of a picked id (league or other sport).
std::optional<std::string> nameOf(std::string_view id);

// Row groups of the picked leagues and sports, in the order picked.
std::vector<std::string> pickedGroups(const MySportsPrefs& prefs);

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
