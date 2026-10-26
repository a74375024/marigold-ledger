// Race weekend extras: the data feed's bodies (sessions, drivers, results,
// the NASCAR driver list), driver lookups by a camera's name, a session's
// top 3 for the podium line, and the request budget: what is due when, one
// request at a time, the daily cap, the pause after a failure, results 10
// minutes after a session (retried, given up after 6 tries), the weekly
// NASCAR list, and the saved state. Drivers and teams are invented.
#include "core/sports/race_data.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::race_data;
using sportsdata::utc;

namespace {

// Saturday 2026-10-24 18:00 UTC.
const Seconds kNow = utc(2026, 10, 24, 18, 0);
const time::Zone kZone = time::Zone::utc();

const char* kSessions = R"([
  {"meeting_key": 1301, "session_key": 9101, "session_name": "Practice 1",
   "date_start": "2026-10-23T17:30:00+00:00", "date_end": "2026-10-23T18:30:00+00:00"},
  {"meeting_key": 1301, "session_key": 9103, "session_name": "Qualifying",
   "date_start": "2026-10-24T21:00:00.000+00:00"},
  {"meeting_key": 1301, "session_key": 9102, "session_type": "Practice 2",
   "date_start": "2026-10-24T14:00:00+00:00", "date_end": "2026-10-24T15:00:00+00:00"},
  {"meeting_key": 1301, "session_name": "No key", "date_start": "2026-10-24T14:00:00+00:00"},
  {"meeting_key": 1301, "session_key": 9104, "session_name": "Race", "date_start": "soon"}
])";

const char* kDrivers = R"([
  {"meeting_key": 1301, "driver_number": 4, "first_name": "Driver", "last_name": "Alpha",
   "name_acronym": "DRA", "team_name": "Team Alpha", "team_colour": "f47600",
   "headshot_url": "https://example.invalid/alpha.png"},
  {"meeting_key": 1301, "driver_number": "16", "first_name": "Driver", "last_name": "B\u00e9ta",
   "team_name": "Team Beta", "team_colour": "DC00"},
  {"meeting_key": 1301, "driver_number": 81, "full_name": "DRIVER CHARLIE"},
  {"meeting_key": 1301, "driver_number": 7},
  {"meeting_key": 1301, "first_name": "No", "last_name": "Number"}
])";

const char* kResult = R"([
  {"position": 3, "driver_number": 81},
  {"position": 1, "driver_number": 16},
  {"position": 2, "driver_number": 4},
  {"position": 4, "driver_number": 7},
  {"position": null, "driver_number": 22}
])";

// Runs a pass to the end, answering every request with `answer`.
template <class Answer>
std::vector<RaceData::Request> drive(RaceData& race, Seconds now, Answer answer) {
  std::vector<RaceData::Request> sent;
  while (auto r = race.nextRequest(now, kZone)) {
    sent.push_back(*r);
    auto [status, body] = answer(*r);
    race.onResponse(status, body, now);
  }
  return sent;
}

std::pair<int, std::string> feed(const RaceData::Request& r) {
  switch (r.ask) {
    case RaceData::Ask::sessions: return {200, kSessions};
    case RaceData::Ask::drivers: return {200, kDrivers};
    case RaceData::Ask::result: return {200, kResult};
  }
  return {0, ""};
}

}  // namespace

TEST_CASE("race data bodies") {
  ParsedSessions s = parseSessions(kSessions);
  CHECK(s.meeting == 1301);
  REQUIRE(s.sessions.size() == 3);
  CHECK(s.sessions[0].kind == "fp1");
  CHECK(s.sessions[0].end == utc(2026, 10, 23, 18, 30));
  CHECK(s.sessions[1].kind == "qual");
  CHECK(s.sessions[1].start == utc(2026, 10, 24, 21, 0));  // fractional seconds
  CHECK_FALSE(s.sessions[1].end);
  CHECK(s.sessions[2].kind == "fp2");  // from the session type
  CHECK(parseSessions("{}").sessions.empty());
  CHECK_FALSE(parseSessions("not json").meeting);

  ParsedDrivers d = parseDrivers(kDrivers);
  CHECK(d.meeting == 1301);
  REQUIRE(d.drivers.size() == 4);
  CHECK(d.drivers[0] == RaceDriver{4, "DRA", "Driver Alpha", "Team Alpha", std::string("F47600"),
                                   std::string("https://example.invalid/alpha.png")});
  CHECK(d.drivers[1].number == 16);          // a number as text
  CHECK(d.drivers[1].code == "B\xC3\x89T");  // the last name's first three letters
  CHECK_FALSE(d.drivers[1].color);           // not six digits
  CHECK(d.drivers[2].name == "DRIVER CHARLIE");
  CHECK(d.drivers[2].code.empty());
  CHECK(d.drivers[3].name == "#7");

  CHECK(parseResult(kResult) == std::vector<int>{16, 4, 81});
  CHECK(parseResult("[]").empty());
}

TEST_CASE("drivers by a camera's name") {
  ParsedDrivers d = parseDrivers(kDrivers);
  Saved saved;
  saved.drivers = d.drivers;
  RaceData race(saved);
  CHECK(race.driver("Driver Alpha")->number == 4);
  CHECK(race.driver("DRIVER ALPHA")->number == 4);
  CHECK(race.driver("Alpha")->number == 4);
  CHECK(race.driver("Someone Alpha")->number == 4);  // by surname
  CHECK(race.driver("Driver Beta Jr.")->number == 16);  // accents and " Jr." folded
  CHECK(race.driver("DRIVER B\xC3\x89TA")->number == 16);
  CHECK_FALSE(race.driver("Driver Zulu"));
  CHECK_FALSE(race.driver(""));
}

TEST_CASE("what is due, and a pass of requests") {
  RaceData race;
  std::vector<WantedSession> weekend = {{"qual", utc(2026, 10, 24, 21, 0)}};
  RaceData::Plan plan = race.due(weekend, true, kNow);
  CHECK(plan.sessions);
  CHECK(plan.drivers);
  CHECK(plan.results.empty());
  REQUIRE(race.refreshIfNeeded(weekend, true, kNow));
  CHECK(race.running());
  CHECK_FALSE(race.refreshIfNeeded(weekend, true, kNow));  // one at a time

  std::vector<RaceData::Request> sent = drive(race, kNow, feed);
  REQUIRE(sent.size() == 2);
  CHECK(sent[0].ask == RaceData::Ask::sessions);
  CHECK(sent[0].request.url == "https://api.openf1.org/v1/sessions?meeting_key=latest");
  CHECK(sent[0].request.timeoutSeconds == 15);
  CHECK(sent[0].waitMs == 0);
  CHECK(sent[1].ask == RaceData::Ask::drivers);
  CHECK(sent[1].request.url == "https://api.openf1.org/v1/drivers?session_key=latest");
  CHECK(sent[1].waitMs == kRequestSpacingMs);
  CHECK_FALSE(race.running());
  CHECK(race.revision() == 1);
  CHECK(race.requestsToday(kNow, kZone) == 2);
  const Saved& s = race.saved();
  CHECK(s.meetingKey == 1301);
  REQUIRE(s.sessions.size() == 3);
  CHECK(s.sessions[1].kind == "fp2");  // sorted by start
  CHECK(s.driversMeeting == 1301);
  CHECK(s.sessionsAt == kNow);

  // Known now: nothing due until the data is old.
  CHECK(race.due(weekend, false, kNow + 3600).empty());
  CHECK_FALSE(race.refreshIfNeeded(weekend, false, kNow + 3600));
  CHECK(race.due(weekend, false, kNow + kSessionsMaxAge + 1).sessions);
  // A session the list doesn't have: asked again after 3 hours, not before.
  std::vector<WantedSession> other = {{"sprint", utc(2026, 10, 25, 12, 0)}};
  CHECK_FALSE(race.due(other, false, kNow + 2 * 3600).sessions);
  CHECK(race.due(other, false, kNow + 3 * 3600 + 1).sessions);
  CHECK(race.due(other, false, kNow + kDriversMaxAge + 1).drivers);

  // Practice 1 and 2 are over (more than 10 minutes ago): their results.
  plan = race.due(weekend, true, kNow);
  CHECK(plan.results == std::vector<int>{9101, 9102});
  CHECK(race.due(weekend, false, kNow).results.empty());  // scores off
  REQUIRE(race.refreshIfNeeded(weekend, true, kNow));
  sent = drive(race, kNow, feed);
  REQUIRE(sent.size() == 2);
  CHECK(sent[0].request.url == "https://api.openf1.org/v1/session_result?session_key=9101");
  CHECK(sent[1].sessionKey == 9102);
  CHECK(sent[0].waitMs == kRequestSpacingMs);
  CHECK(race.saved().results.at("9101") == std::vector<int>{16, 4, 81});
  CHECK(race.saved().results.at("9102") == std::vector<int>{16, 4, 81});
  CHECK(race.saved().resultTries.at("9102") == 1);
  CHECK(race.revision() == 2);

  // The podium line.
  auto top = race.top3("fp2", utc(2026, 10, 24, 14, 30));
  REQUIRE(top);
  CHECK((*top)[0].name == "Driver B\xC3\xA9ta");
  CHECK_FALSE(race.top3("fp2", utc(2026, 10, 24, 20, 0)));  // over 3 hours off
  CHECK_FALSE(race.top3("qual", utc(2026, 10, 24, 21, 0)));  // no result yet
  auto hook = race.podiumHook();
  std::vector<board::PodiumDriver> podium = hook("fp2", utc(2026, 10, 24, 14, 0));
  REQUIRE(podium.size() == 3);
  CHECK(podium[1].code == "DRA");
  CHECK(podium[1].color == std::string("F47600"));
  CHECK(podium[2].code.empty());
  CHECK(hook("race", kNow).empty());

  // Saved and read back.
  auto back = savedFromJson(toJson(race.saved()));
  REQUIRE(back);
  RaceData again(*back);
  CHECK(toJson(again.saved()) == toJson(race.saved()));
  CHECK(again.top3("fp2", utc(2026, 10, 24, 14, 0))->size() == 3);
  CHECK(again.driver("Alpha")->number == 4);
  CHECK_FALSE(savedFromJson("[]"));
}

TEST_CASE("results: 10 minutes after the end, retried every 20 minutes, 6 tries, 2 a pass, 4 days") {
  Saved saved;
  saved.sessionsAt = kNow;
  saved.driversAt = kNow;
  saved.meetingKey = 1;
  saved.driversMeeting = 1;
  saved.drivers = {RaceDriver{4, "DRA", "Driver Alpha", "Team Alpha", std::nullopt, std::nullopt}};
  saved.sessions = {Session{1, "fp1", kNow - 2 * 3600, kNow - 3600}, Session{2, "fp2", kNow - 3600, kNow - 5 * 60},
                    Session{3, "fp3", kNow - 5 * 86'400, std::nullopt}, Session{4, "qual", kNow - 4 * 3600, std::nullopt},
                    Session{5, "sq", kNow - 6 * 3600, std::nullopt}};
  RaceData race(saved);
  // fp2 ended 5 minutes ago, fp3 5 days ago: not due. Two a pass.
  CHECK(race.due({}, true, kNow).results == std::vector<int>{1, 4});
  REQUIRE(race.refreshIfNeeded({}, true, kNow));
  std::vector<RaceData::Request> sent = drive(race, kNow, [](const RaceData::Request&) {
    return std::pair<int, std::string>{200, "[]"};  // no result yet
  });
  CHECK(sent.size() == 2);
  CHECK(race.revision() == 0);  // nothing changed
  // 1 and 4 wait 20 minutes; fp2 is 15 minutes over by then.
  CHECK(race.due({}, true, kNow + 10 * 60).results == std::vector<int>{2, 5});
  CHECK(race.due({}, true, kNow + 21 * 60).results == std::vector<int>{1, 2});

  saved.resultTries["1"] = kResultMaxTries;
  saved.resultTries["99"] = 2;  // not this weekend: dropped after a pass
  RaceData tired(saved);
  CHECK(tired.due({}, true, kNow).results == std::vector<int>{4, 5});
  REQUIRE(tired.refreshIfNeeded({}, true, kNow));
  drive(tired, kNow, [](const RaceData::Request&) { return std::pair<int, std::string>{200, "[]"}; });
  CHECK_FALSE(tired.saved().resultTries.count("99"));
  CHECK(tired.saved().resultTries.at("1") == kResultMaxTries);
}

TEST_CASE("a failure pauses 30 minutes; the daily cap ends a pass") {
  RaceData race;
  REQUIRE(race.refreshIfNeeded({}, false, kNow));
  std::vector<RaceData::Request> sent =
      drive(race, kNow, [](const RaceData::Request&) { return std::pair<int, std::string>{503, ""}; });
  CHECK(sent.size() == 1);  // the drivers aren't asked after a failure
  CHECK_FALSE(race.saved().sessionsAt);
  CHECK_FALSE(race.refreshIfNeeded({}, false, kNow + 29 * 60));
  CHECK(race.refreshIfNeeded({}, false, kNow + kFailurePause));

  Saved full;
  full.requestDay = "2026-10-24";
  full.requestCount = kDailyCap;
  RaceData capped(full);
  REQUIRE(capped.refreshIfNeeded({}, false, kNow));
  CHECK_FALSE(capped.nextRequest(kNow, kZone));
  CHECK_FALSE(capped.running());
  CHECK_FALSE(capped.refreshIfNeeded({}, false, kNow + 60));  // paused like a failure
  // A new day starts a new count.
  RaceData tomorrow(full);
  REQUIRE(tomorrow.refreshIfNeeded({}, false, kNow + 86'400));
  auto r = tomorrow.nextRequest(kNow + 86'400, kZone);
  REQUIRE(r);
  CHECK(tomorrow.saved().requestCount == 1);
  CHECK(tomorrow.saved().requestDay == std::string("2026-10-25"));
  CHECK(tomorrow.requestsToday(kNow + 86'400, kZone) == 1);
}

TEST_CASE("a new meeting's session list asks for its drivers") {
  Saved saved;
  saved.meetingKey = 1200;
  saved.driversMeeting = 1200;
  saved.sessionsAt = kNow - 2 * 86'400;
  saved.driversAt = kNow - 3600;
  saved.drivers = {RaceDriver{4, "DRA", "Driver Alpha", "Team Alpha", std::nullopt, std::nullopt}};
  RaceData race(saved);
  RaceData::Plan plan = race.due({}, false, kNow);
  CHECK(plan.sessions);
  CHECK_FALSE(plan.drivers);
  REQUIRE(race.refreshIfNeeded({}, false, kNow));
  std::vector<RaceData::Request> sent = drive(race, kNow, feed);
  REQUIRE(sent.size() == 2);
  CHECK(sent[1].ask == RaceData::Ask::drivers);
  CHECK(race.saved().driversMeeting == 1301);
  CHECK(race.saved().drivers.size() == 4);
}

TEST_CASE("NASCAR drivers: photos and car numbers, once a week") {
  const char* body = R"json({"response": [
    {"Full_Name": "Driver Alpha Jr.", "Badge": "5", "Team": "Team Alpha", "Driver_Series": "nascar-xfinity-series",
     "Image_Small": "https://example.invalid/default.png", "Image": "https://example.invalid/alpha-x.png"},
    {"Full_Name": "Driver Alpha", "Badge": 5, "Team": "Team Alpha", "Driver_Series": "nascar-cup-series",
     "Image_Small": "https://example.invalid/alpha-small.png"},
    {"Full_Name": "Driver Bravo (i)", "Badge": "24", "Driver_Series": "nascar-cup-series",
     "Image": "https://example.invalid/Silhouette.png"},
    {"Full_Name": "Charlie", "Badge": "9", "Image": "https://example.invalid/c.png"},
    {"Full_Name": "Driver Delta", "Badge": "9", "Image": "http://example.invalid/d.png"},
    {"Full_Name": "Driver Echo", "Badge": "11", "Image": "https://example.invalid/e.png"}
  ]})json";
  std::vector<RaceDriver> drivers = parseNascar(body);
  REQUIRE(drivers.size() == 3);
  CHECK(drivers[0].name == "Driver Alpha");  // Cup Series first
  CHECK(drivers[0].number == 5);
  CHECK(drivers[0].code == "5");
  CHECK(drivers[0].headshot == std::string("https://example.invalid/alpha-small.png"));
  CHECK(drivers[1].name == "Driver Alpha");  // "Jr." and the default photo dropped
  CHECK(drivers[1].headshot == std::string("https://example.invalid/alpha-x.png"));
  CHECK(drivers[2].name == "Driver Echo");
  CHECK(parseNascar("[]").empty());
  CHECK(bareName("Driver Foxtrot III (C)") == "Driver Foxtrot");
  CHECK(bareName("D. Golf Sr.") == "D Golf");

  RaceData race;
  auto request = race.refreshNascarIfNeeded(kNow);
  REQUIRE(request);
  CHECK(request->url == "https://cf.nascar.com/cacher/drivers.json");
  CHECK(request->timeoutSeconds == 20);
  CHECK_FALSE(race.refreshNascarIfNeeded(kNow));  // one out
  race.onNascarResponse(500, "", kNow);
  CHECK_FALSE(race.refreshNascarIfNeeded(kNow + 3600));  // 6 hours after a failure
  REQUIRE(race.refreshNascarIfNeeded(kNow + kNascarFailurePause));
  race.onNascarResponse(200, body, kNow + kNascarFailurePause);
  CHECK(race.revision() == 1);
  CHECK(race.nascarDriver("Driver Alpha")->headshot == std::string("https://example.invalid/alpha-small.png"));
  CHECK(race.nascarDriver("Driver Alpha Jr. (C)")->number == 5);
  CHECK(race.nascarDriver("driver echo")->number == 11);
  CHECK_FALSE(race.nascarDriver("Driver Bravo"));
  CHECK_FALSE(race.refreshNascarIfNeeded(kNow + 86'400));
  CHECK(race.refreshNascarIfNeeded(kNow + kNascarFailurePause + kNascarMaxAge));

  auto back = nascarFromJson(toJson(race.nascarSaved()));
  REQUIRE(back);
  RaceData again(Saved{}, *back);
  CHECK(again.nascarDriver("Driver Echo")->number == 11);
  CHECK(back->fetchedAt == kNow + kNascarFailurePause);
}

TEST_CASE("race weekends on the board ask for their data") {
  auto part = [](const std::string& kind, Seconds start) {
    SportsGame g = sportsdata::game("F1", "Motorsport", "ev1|" + kind, sportsdata::team("gp", "Grand Prix Alpha"),
                                    sportsdata::team("gp", "Grand Prix Alpha"), start);
    g.cardKey = "f1|ev1|" + kind;
    g.racing = true;
    return sportsdata::eventOf(g);
  };
  std::vector<SportsEvent> parts = {part("fp1", kNow - 86'400), part("qual", kNow + 3600)};
  SportsEvent noKey = part("race", kNow + 86'400);
  noKey.game->cardKey.reset();
  parts.push_back(noKey);
  std::vector<WantedSession> sessions = weekendSessions(parts);
  REQUIRE(sessions.size() == 2);
  CHECK(sessions[0].kind == "fp1");
  CHECK(sessions[1].kind == "qual");
  CHECK(sessions[1].start == kNow + 3600);

  board::Board b;
  RaceData race;
  CHECK_FALSE(refreshForBoard(race, b, true, kNow));  // no weekend you can watch
  b.watchableRaceWeekends = {"card|F1|ev1"};
  b.cards["card|F1|ev1"] = parts;
  CHECK(refreshForBoard(race, b, true, kNow));
  CHECK(race.running());
}
