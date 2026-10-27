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
  auto readList = [&](const char* key, std::vector<std::string>& into) {
    if (!v.has(key)) return true;
    if (!v[key].isArray()) return false;
    into.clear();
    for (const json::Value& item : v[key].items()) {
      if (!item.isString()) return false;
      into.push_back(item.asString());
    }
    return true;
  };
  if (!readBool("configured", prefs.configured) || !readList("leagues", prefs.leagues) ||
      !readList("teams", prefs.teams) || !readBool("reminders", prefs.reminders) ||
      !readBool("prompted", prefs.prompted)) {
    return std::nullopt;
  }
  return prefs;
}

MySportsChange compare(const MySportsPrefs& before, const MySportsPrefs& after) {
  MySportsChange change;
  change.changed = before != after;
  change.leaguesChanged = after.leagues != before.leagues || after.configured != before.configured;
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
      {"S:Fighting", "Fighting", "UFC, boxing & wrestling"},
      {"S:Motorsport", "Motorsport", "Racing"},
      {"S:Tennis", "Tennis", "Tennis"},
      {"S:Golf", "Golf", "Golf"},
      {"S:Rugby", "Rugby", "Rugby"},
      {"S:Cricket", "Cricket", "Cricket"},
      {"S:Darts", "Darts", "Darts"},
  };
  return kSports;
}

std::optional<std::string> nameOf(std::string_view id) {
  for (const League& l : leagues()) {
    if (l.shortName == id) return l.name;
  }
  for (const OtherSport& s : otherSports()) {
    if (s.id == id) return s.name;
  }
  return std::nullopt;
}

std::vector<std::string> pickedGroups(const MySportsPrefs& prefs) {
  std::vector<std::string> groups;
  for (const std::string& id : prefs.leagues) {
    std::optional<std::string> group;
    for (const League& l : leagues()) {
      if (l.shortName == id) {
        group = l.group;
        break;
      }
    }
    if (!group) {
      for (const OtherSport& s : otherSports()) {
        if (s.id == id) {
          group = s.group;
          break;
        }
      }
    }
    if (!group || std::find(groups.begin(), groups.end(), *group) != groups.end()) continue;
    groups.push_back(*group);
  }
  return groups;
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
