// My Sports (followed leagues and teams, JSON, picked rows, suggestions,
// reminders for favorite teams) and reminders (due logic, dedup, JSON).
#include "core/sports/my_sports.hpp"
#include "core/sports/reminders.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using sportsdata::game;
using sportsdata::team;
using sportsdata::utc;

TEST_CASE("My Sports JSON round trip; missing fields keep defaults; wrong types fail") {
  MySportsPrefs prefs;
  prefs.configured = true;
  prefs.leagues = {"NFL", "S:Golf", "EPL"};
  prefs.teams = {"NFL|1", "EPL|20"};
  prefs.reminders = true;
  auto back = mySportsFromJson(toJson(prefs));
  REQUIRE(back);
  CHECK(*back == prefs);
  auto partial = mySportsFromJson(R"({"configured": true, "leagues": ["NBA"]})");
  REQUIRE(partial);
  CHECK(partial->configured);
  CHECK(partial->leagues == std::vector<std::string>{"NBA"});
  CHECK(partial->teams.empty());
  CHECK_FALSE(partial->prompted);
  CHECK_FALSE(mySportsFromJson(R"({"configured": "yes"})"));
  CHECK_FALSE(mySportsFromJson(R"({"leagues": [1, 2]})"));
  CHECK_FALSE(mySportsFromJson("nonsense"));
}

TEST_CASE("changes: leagues or setup changed means the schedule looks again") {
  MySportsPrefs a;
  MySportsPrefs b = a;
  CHECK_FALSE(compare(a, b).changed);
  b.teams = {"NFL|1"};
  CHECK(compare(a, b).changed);
  CHECK_FALSE(compare(a, b).leaguesChanged);
  b.leagues = {"NFL"};
  CHECK(compare(a, b).leaguesChanged);
  MySportsPrefs c = a;
  c.configured = true;
  CHECK(compare(a, c).leaguesChanged);
}

TEST_CASE("favorites, names and picked rows in the order picked") {
  MySportsPrefs prefs;
  prefs.leagues = {"EPL", "NCAAM", "S:Golf", "UCL", "NCAAW", "NOPE"};
  prefs.teams = {teamKey("NFL", "2")};
  SportsGame g = game("NFL", "NFL", "1", team("1", "Harbor City Gulls"), team("2", "Team Alpha"), 0);
  CHECK(isFavorite(prefs, g));
  g.away.id = "3";
  CHECK_FALSE(isFavorite(prefs, g));
  // Soccer leagues share a row; men's and women's college basketball too.
  CHECK(pickedGroups(prefs) == std::vector<std::string>{"Soccer", "College Basketball", "Golf"});
  CHECK(nameOf("EPL") == "Premier League");
  CHECK(nameOf("S:Fighting") == "UFC, boxing & wrestling");
  CHECK(nameOf("S:Motorsport") == "Racing");
  CHECK(nameOf("NOPE") == std::nullopt);
}

TEST_CASE("suggestions count the provider's events per league") {
  Seconds now = sportsdata::saturday();
  std::vector<catalog::LiveEvent> events = {
      sportsdata::liveEvent("1", "NFL 01: Team Alpha vs Harbor City Gulls", "NFL", now, {"a"}),
      sportsdata::liveEvent("2", "Premier League: Harbor United vs Riverside Rovers", "Soccer", now, {"b"}),
      sportsdata::liveEvent("3", "Soccer: Harbor FC vs Coast FC", "Soccer", now, {"c"}),
      sportsdata::liveEvent("4", "Women's Basketball: Lakeside vs Harbor State", "College Basketball", now, {"d"}),
      sportsdata::liveEvent("5", "Golf: Final Round", "Golf", now, {"e"}),
      sportsdata::liveEvent("6", "Basketball: River Town vs Valley", "Basketball", now, {"f"}),
  };
  auto counts = suggestionCounts(events);
  CHECK(counts["NFL"] == 1);
  CHECK(counts["EPL"] == 1);
  CHECK(counts.count("UCL") == 0);
  CHECK(counts.count("MLS") == 0);  // "Soccer" with no MLS keyword
  CHECK(counts["NCAAW"] == 1);
  CHECK(counts["NCAAM"] == 1);  // "Basketball: ..." covers the college leagues
  CHECK(counts["NBA"] == 1);
  CHECK(counts["S:Golf"] == 1);
}

TEST_CASE("reminders: one per show, due 30 s before, dropped once over") {
  Seconds now = sportsdata::saturday();
  Reminders reminders;
  LiveReminder r1{"e|fox13", "FOX 13", "Team Alpha at Harbor City Gulls", now + 600, now + 600 + 3 * 3600};
  LiveReminder r2{"e|espn", "ESPN", "River Town Otters vs Valley Hawks", now + 60, now + 3600};
  reminders.toggle(r1);
  reminders.toggle(r2);
  REQUIRE(reminders.items().size() == 2);
  CHECK(reminders.items()[0].channelID == "e|espn");  // by start
  CHECK(reminders.isSet("e|fox13", now + 600 + 30));  // within a minute is the same one
  CHECK_FALSE(reminders.isSet("e|fox13", now + 600 + 90));
  CHECK(r1.id() == "e|fox13|" + std::to_string(now + 600));
  // Not due yet.
  auto check = reminders.check(now);
  CHECK_FALSE(check.due);
  CHECK_FALSE(check.changed);
  // 30 s before its start: its card shows, and it leaves the list.
  check = reminders.check(now + 30);
  REQUIRE(check.due);
  CHECK(check.due->channelID == "e|espn");
  CHECK(check.changed);
  CHECK(reminders.items().size() == 1);
  // While a card shows, no other one replaces it.
  check = reminders.check(now + 600);
  CHECK_FALSE(check.due);
  reminders.dismiss();
  check = reminders.check(now + 600);
  REQUIRE(check.due);
  CHECK(check.due->channelID == "e|fox13");
  // Toggling again removes a reminder.
  Reminders other;
  other.toggle(r1);
  other.toggle(r1);
  CHECK(other.items().empty());
  // Over: dropped (and the list saved).
  Reminders old;
  old.toggle(r2);
  auto dropped = old.check(now + 7200);
  CHECK(dropped.changed);
  CHECK_FALSE(dropped.due);
  CHECK(old.items().empty());
}

TEST_CASE("reminders JSON; saved reminders already over are dropped") {
  Seconds now = sportsdata::saturday();
  Reminders reminders;
  reminders.toggle({"e|fox13", "FOX 13", "Game", now + 600, now + 4000});
  reminders.toggle({"e|old", "Old", "Over", now - 7200, now - 3600});
  auto saved = Reminders::fromJson(reminders.toJson());
  REQUIRE(saved);
  CHECK(saved->size() == 2);
  Reminders loaded(*saved, now);
  REQUIRE(loaded.items().size() == 1);
  CHECK(loaded.items()[0].title == "Game");
  CHECK_FALSE(Reminders::fromJson(R"([{"channelID": 1}])"));
  CHECK_FALSE(Reminders::fromJson("{}"));
}

TEST_CASE("favorite teams' games in the next day get a reminder, once each") {
  Seconds now = sportsdata::saturday();
  MySportsPrefs prefs;
  prefs.configured = true;
  prefs.reminders = true;
  prefs.teams = {teamKey("NFL", "1")};
  SportsGame soon = game("NFL", "NFL", "1", team("1", "Harbor City Gulls"), team("2", "Team Alpha"), now + 3600);
  SportsGame far = game("NFL", "NFL", "2", team("1", "Harbor City Gulls"), team("3", "Coast Crabs"), now + 3 * 86'400);
  SportsGame other = game("NFL", "NFL", "3", team("4", "Lake Pikes"), team("5", "Mountain Goats"), now + 3600);
  std::vector<SportsEvent> events = {
      sportsdata::eventOf(soon, {sportsdata::source("e|fox13", EventSource::Kind::lineup)}),
      sportsdata::eventOf(far, {sportsdata::source("e|fox13", EventSource::Kind::lineup)}),
      sportsdata::eventOf(other, {sportsdata::source("e|fox13", EventSource::Kind::lineup)}),
  };
  ReminderTargetFor target = [](const SportsEvent& e) -> std::optional<LiveReminder> {
    return LiveReminder{"e|fox13", "FOX 13", e.title, *e.start, *e.start + e.expectedLength()};
  };
  Reminders reminders;
  std::vector<std::string> reminded;
  CHECK(scheduleTeamReminders(prefs, events, now, target, reminders, reminded));
  REQUIRE(reminders.items().size() == 1);
  CHECK(reminders.items()[0].title == "Team Alpha at Harbor City Gulls");
  CHECK(reminded == std::vector<std::string>{"sev|NFL|1"});
  // Removed by hand: not added again.
  reminders.toggle(reminders.items()[0]);
  CHECK_FALSE(scheduleTeamReminders(prefs, events, now, target, reminders, reminded));
  CHECK(reminders.items().empty());
  // Off: nothing.
  prefs.reminders = false;
  std::vector<std::string> none;
  CHECK_FALSE(scheduleTeamReminders(prefs, events, now, target, reminders, none));
  // The handled list keeps the newest 200.
  prefs.reminders = true;
  std::vector<std::string> many(kRemindedCap, "x");
  Reminders fresh;
  CHECK(scheduleTeamReminders(prefs, events, now, target, fresh, many));
  CHECK(many.size() == kRemindedCap);
  CHECK(many.back() == "sev|NFL|1");
}
