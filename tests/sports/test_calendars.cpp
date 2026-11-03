// The other event calendars (Build 125): reading their answers (trimmed from
// real ones: a hockey game, an endurance race weekend, a boxing card with no
// time, a darts day, snooker's day, nothing on), which competitions are
// looked up, the day's pass with its request budget (next event, the round
// when it changed, the next round when this one ends within the week; the
// cap, refusals, errors), the saved copy, and the schedule's leftover check.
#include <set>

#include "core/sports/calendars.hpp"
#include "core/sports/schedule.hpp"
#include "core/sports/schedule_index.hpp"
#include "core/util/json.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::calendars;
using sportsdata::utc;

namespace {

const std::vector<Calendar>& allCalendars() { return marigold::sports::calendars::calendars(); }

const Calendar& cal(const char* key) {
  const Calendar* c = byKey(key);
  REQUIRE(c);
  return *c;
}

// eventsnextleague.php?id=4930 (trimmed).
const char* kHockeyNext = R"JSON({"events":[{"idEvent":"2522118","idAPIfootball":"427862",
  "strTimestamp":"2026-11-02T18:00:00","strEvent":"Rødovre Mighty Bulls vs SønderjyskE Ishockey",
  "strSport":"Ice Hockey","idLeague":"4930","strLeague":"Danish Metal Ligaen","strSeason":"2026-2027",
  "strHomeTeam":"Rødovre Mighty Bulls","strAwayTeam":"SønderjyskE Ishockey","intHomeScore":null,
  "intRound":"11","dateEvent":"2026-11-02","strTime":"18:00:00","idHomeTeam":"140923","idAwayTeam":"140952",
  "strVenue":"Holger Danske Arena","strCountry":"Denmark","strCity":null,"strStatus":"NS","strPostponed":"no"}]})JSON";

// eventsround.php?id=4413&r=8&s=2026 (three of its sessions and the race).
const char* kWecRound = R"JSON({"events":[
  {"idEvent":"2490543","strTimestamp":"2026-11-06T09:30:00","strEvent":"6 Hours of Monza Free Practice 1",
   "strSport":"Motorsport","strLeague":"WEC","strSeason":"2026","strHomeTeam":null,"strAwayTeam":null,"intRound":"8",
   "dateEvent":"2026-11-06","strTime":"09:30:00","strVenue":"Monza Circuit","strCountry":"Italy","strStatus":"","strPostponed":"no"},
  {"idEvent":"2490548","strTimestamp":"2026-11-07T14:00:00","strEvent":"6 Hours of Monza Qualifying - Hypercar",
   "strSport":"Motorsport","strLeague":"WEC","strSeason":"2026","intRound":"8","dateEvent":"2026-11-07","strTime":"14:00:00",
   "strVenue":"Monza Circuit","strStatus":"","strPostponed":"no"},
  {"idEvent":"2490549","strTimestamp":"2026-11-07T14:20:00","strEvent":"6 Hours of Monza Hyperpole - Hypercar",
   "strSport":"Motorsport","strLeague":"WEC","strSeason":"2026","intRound":"8","dateEvent":"2026-11-07","strTime":"14:20:00",
   "strVenue":"Monza Circuit","strStatus":"","strPostponed":"no"},
  {"idEvent":"2421042","strTimestamp":"2026-11-08T10:00:00","strEvent":"6 Hours of Monza",
   "strSport":"Motorsport","strLeague":"WEC","strSeason":"2026","intRound":"8","dateEvent":"2026-11-08","strTime":"10:00:00",
   "strVenue":"Monza Circuit","strStatus":"","strPostponed":"no"}]})JSON";

// eventsround.php?id=4445&r=112&s=2026: a card with no time.
const char* kBoxingRound = R"JSON({"events":[{"idEvent":"2607375","strTimestamp":"2026-11-07T00:00:00",
  "strEvent":"Joshua Buatsi vs Willy Hutchinson 2","strSport":"Fighting","strLeague":"Boxing","strSeason":"2026",
  "strHomeTeam":null,"strAwayTeam":null,"intRound":"112","dateEvent":"2026-11-07","strTime":"00:00:00",
  "strVenue":"AO Arena","strCountry":"United Kingdom","strStatus":null,"strPostponed":"no"}]})JSON";

// eventsnextleague.php?id=4554: a darts day.
const char* kDartsNext = R"JSON({"events":[{"idEvent":"2623053","strTimestamp":"2026-11-04T00:00:00",
  "strEvent":"Players Championship 33","strSport":"Darts","strLeague":"PDC Darts","strSeason":"2026",
  "strHomeTeam":null,"strAwayTeam":null,"intRound":"76","dateEvent":"2026-11-04","strTime":"00:00:00",
  "strVenue":"Mattioli Arena","strCountry":"England","strCity":"Leicester","strStatus":null,"strPostponed":"no"}]})JSON";

// eventsday.php?d=2026-11-02&s=Snooker.
const char* kSnookerDay = R"JSON({"events":[{"idEvent":"2625597","strTimestamp":"2026-11-02T00:00:00",
  "strEvent":"International Championship Round 3","strSport":"Snooker","strLeague":"World Snooker",
  "strSeason":"2026-2027","intRound":"9","dateEvent":"2026-11-02","strTime":"00:00:00","strVenue":"",
  "strCountry":"China","strStatus":"","strPostponed":"no"}]})JSON";

// A synthetic event of a soccer competition, for the pass.
std::string soccerEvent(const std::string& id, int round, Seconds start, const std::string& status = "NS") {
  time::Civil c = time::civilFromSeconds(start);
  char stamp[32];
  std::snprintf(stamp, sizeof stamp, "%04d-%02d-%02dT%02d:%02d:00", c.year, c.month, c.day, c.hour, c.minute);
  return std::string(R"({"idEvent":")") + id + R"(","strEvent":"Harbor vs Valley","strTimestamp":")" + stamp +
         R"(","strSeason":"2026","intRound":")" + std::to_string(round) +
         R"(","strHomeTeam":"Harbor IF","strAwayTeam":"Valley FF","idHomeTeam":"1","idAwayTeam":"2","strStatus":")" +
         status + R"("})";
}

std::string events(const std::vector<std::string>& list) {
  std::string body = R"({"events":[)";
  for (size_t i = 0; i < list.size(); ++i) body += (i ? "," : "") + list[i];
  return body + "]}";
}

SportFeedsSeen seenAll(Seconds now) {
  SportFeedsSeen seen;
  for (const Calendar& c : allCalendars()) seen.calendars[c.key] = now - 3600;
  return seen;
}

catalog::LiveEvent feed(const std::string& title, const std::string& league = "Other") {
  catalog::LiveEvent e;
  e.id = title;
  e.title = title;
  e.league = league;
  return e;
}

}  // namespace

TEST_CASE("calendars: a hockey game, both teams, home first in the data, by its UTC time") {
  auto parsed = parse(kHockeyNext, cal("4930"));
  REQUIRE(parsed);
  CHECK(parsed->season == std::optional<std::string>("2026-2027"));
  CHECK(parsed->round == std::optional<int>(11));
  REQUIRE(parsed->games.size() == 1);
  const SportsGame& g = parsed->games[0];
  CHECK(g.id == "DEN|2522118");
  CHECK(g.league == "Hockey");
  CHECK(g.leagueShort == "DEN");
  CHECK(g.home.name == "R\xC3\xB8" "dovre Mighty Bulls");
  CHECK(g.away.name == "S\xC3\xB8nderjyskE Ishockey");
  CHECK(g.home.id == "140923");
  CHECK(g.start == utc(2026, 11, 2, 18, 0));
  CHECK(g.note == std::optional<std::string>("Metal Ligaen"));
  CHECK(g.venue == std::optional<std::string>("Holger Danske Arena"));
  CHECK_FALSE(g.completed);
  CHECK(isCalendarGame(g));
}

TEST_CASE("calendars: a race weekend's sessions under one weekend, the race last") {
  auto parsed = parse(kWecRound, cal("4413"));
  REQUIRE(parsed);
  REQUIRE(parsed->games.size() == 4);
  for (const SportsGame& g : parsed->games) {
    CHECK(g.isRacing());
    CHECK(g.league == "Motorsport");
    CHECK(g.home.name == "6 Hours of Monza");
    CHECK(g.home == g.away);
    // One weekend: its sessions share the id's first two parts.
    CHECK(schedule::weekendKey(g) == "WEC|2026-8");
  }
  CHECK(parsed->games[0].title() == "6 Hours of Monza \xC2\xB7 Free Practice 1");
  CHECK(parsed->games[0].cardKey == std::optional<std::string>("wec|2026-8|fp1"));
  CHECK(parsed->games[1].title() == "6 Hours of Monza \xC2\xB7 Qualifying - Hypercar");
  CHECK(parsed->games[3].title() == "6 Hours of Monza \xC2\xB7 Race");
  CHECK(parsed->games[3].cardKey == std::optional<std::string>("wec|2026-8|race"));
  CHECK(parsed->games[0].venue == std::optional<std::string>("Monza Circuit"));
}

TEST_CASE("calendars: weekend and session names, fighters, timestamps") {
  CHECK(weekendAndSession("Qatar Free Practice 1") == std::make_pair(std::string("Qatar"), std::string("Free Practice 1")));
  CHECK(weekendAndSession("Qatar GP") == std::make_pair(std::string("Qatar"), std::string()));
  CHECK(weekendAndSession("Qatar Sprint Race") == std::make_pair(std::string("Qatar"), std::string("Sprint Race")));
  CHECK(weekendAndSession("Penrite Oil Sandown 500 Race 1") ==
        std::make_pair(std::string("Penrite Oil Sandown 500"), std::string("Race 1")));
  CHECK(weekendAndSession("6 Hours of Monza") == std::make_pair(std::string("6 Hours of Monza"), std::string()));
  // MotoGP weekends read as Grands Prix.
  std::string motogp = events({R"({"idEvent":"1","strEvent":"Qatar Practice","strTimestamp":"2026-11-06T16:00:00",
    "strSeason":"2026","intRound":"20"})"});
  auto parsed = parse(motogp, cal("4407"));
  REQUIRE(parsed);
  REQUIRE(parsed->games.size() == 1);
  CHECK(parsed->games[0].title() == "Qatar GP \xC2\xB7 Practice");

  CHECK(fighters("Oktagon 97 Severino vs Holzer") == std::make_pair(std::string("Severino"), std::string("Holzer")));
  CHECK(fighters("UFC Fight Night 293 Bonfim vs Brady") == std::make_pair(std::string("Bonfim"), std::string("Brady")));
  CHECK(fighters("Joshua Buatsi vs Willy Hutchinson 2") ==
        std::make_pair(std::string("Joshua Buatsi"), std::string("Willy Hutchinson")));
  CHECK_FALSE(fighters("Players Championship 33"));

  CHECK(timestamp("2026-11-06T09:30:00") == utc(2026, 11, 6, 9, 30));
  CHECK(timestamp("2026-11-06T09:30:00Z") == utc(2026, 11, 6, 9, 30));
  CHECK(timestamp("2026-11-06T09:30:00+00:00") == utc(2026, 11, 6, 9, 30));
  CHECK_FALSE(timestamp("2026-11-06T09:30:00+02:00"));
  CHECK_FALSE(timestamp("2026-13-06T09:30:00"));
  CHECK_FALSE(timestamp(""));
}

TEST_CASE("calendars: events with no time take the day at the sport's hour; title events have one side") {
  auto boxing = parse(kBoxingRound, cal("4445"));
  REQUIRE(boxing);
  REQUIRE(boxing->games.size() == 1);
  const SportsGame& card = boxing->games[0];
  CHECK(card.league == "Fighting");
  CHECK(card.start == utc(2026, 11, 7, 19, 0));
  CHECK(card.title() == "Joshua Buatsi vs Willy Hutchinson 2");
  CHECK(card.home.name == "Joshua Buatsi");
  CHECK(card.away.name == "Willy Hutchinson");
  CHECK(card.cardKey == std::optional<std::string>("box|2607375|main"));

  auto darts = parse(kDartsNext, cal("4554"));
  REQUIRE(darts);
  REQUIRE(darts->games.size() == 1);
  CHECK(darts->round == std::optional<int>(76));
  CHECK(darts->games[0].league == "Darts");
  CHECK(darts->games[0].title() == "Players Championship 33");
  CHECK(darts->games[0].home == darts->games[0].away);
  CHECK(darts->games[0].start == utc(2026, 11, 4, 12, 0));
  CHECK(darts->games[0].venue == std::optional<std::string>("Mattioli Arena, Leicester"));

  auto snooker = parse(kSnookerDay, cal("snooker"));
  REQUIRE(snooker);
  REQUIRE(snooker->games.size() == 1);
  CHECK(snooker->games[0].league == "Snooker");
  CHECK(snooker->games[0].title() == "International Championship Round 3");
  CHECK(snooker->games[0].note == std::optional<std::string>("World Snooker"));
  CHECK(snooker->games[0].start == utc(2026, 11, 2, 10, 0));
}

TEST_CASE("calendars: nothing on is an empty list; anything else isn't an answer") {
  auto none = parse(R"({"events":null})", cal("4403"));
  REQUIRE(none);
  CHECK(none->games.empty());
  CHECK_FALSE(none->round);
  CHECK_FALSE(parse("<html>busy</html>", cal("4403")));
  CHECK_FALSE(parse("", cal("4403")));
  // Odd events are skipped, never fatal: no id, no time, one team,
  // cancelled.
  std::string odd = events({R"({"strEvent":"A vs B","strTimestamp":"2026-11-02T18:00:00"})",
                            R"({"idEvent":"2","strEvent":"A vs B","strHomeTeam":"A","strAwayTeam":"B"})",
                            R"({"idEvent":"3","strEvent":"A vs B","strTimestamp":"2026-11-02T18:00:00","strHomeTeam":"A"})",
                            R"({"idEvent":"4","strEvent":"A vs B","strTimestamp":"2026-11-02T18:00:00","strHomeTeam":"A","strAwayTeam":"B","strStatus":"CANC"})",
                            R"({"idEvent":"5","strEvent":"A vs B","strTimestamp":"2026-11-01T18:00:00","strHomeTeam":"A","strAwayTeam":"B","strStatus":"FT"})",
                            R"({"idEvent":"6","strEvent":"A vs B","strTimestamp":"2026-11-03T18:00:00","strHomeTeam":"A","strAwayTeam":"B","strPostponed":"yes"})"});
  auto parsed = parse(odd, cal("4403"));
  REQUIRE(parsed);
  REQUIRE(parsed->games.size() == 2);
  CHECK(parsed->games[0].completed);
  CHECK(parsed->games[1].postponed);
}

TEST_CASE("calendars: requests name the competition and nothing else") {
  CHECK(nextRequest(cal("4403")).url == "https://www.thesportsdb.com/api/v1/json/123/eventsnextleague.php?id=4403");
  CHECK(roundRequest(cal("4930"), 11, "2026-2027").url ==
        "https://www.thesportsdb.com/api/v1/json/123/eventsround.php?id=4930&r=11&s=2026-2027");
  CHECK(dayRequest(cal("snooker"), "2026-11-02").url ==
        "https://www.thesportsdb.com/api/v1/json/123/eventsday.php?d=2026-11-02&s=Snooker");
  CHECK(dayRequest(cal("5162"), "2026-11-02").url ==
        "https://www.thesportsdb.com/api/v1/json/123/eventsday.php?d=2026-11-02&l=5162");
  CHECK(nextRequest(cal("4403")).headers.empty());
  // Short names never collide with a scoreboard league's (scores never
  // polled for them).
  for (const Calendar& c : allCalendars()) CHECK_FALSE(leagueByShort(c.shortName));
}

TEST_CASE("calendars: named in feed titles, as whole words") {
  std::vector<catalog::LiveEvent> list = {
      feed("\xC3\x96STER - HELSINGBORG SUPERETTAN"),
      feed("R\xC3\x98" "DOVRE MIGHTY BULLS - S\xC3\x98NDERJYSKE ISHOCKEY (METAL LIGAEN)"),
      feed("Snooker: Wu Yize vs Stephen Maguire"),
      feed("1\xC2\xAA RFEF HUESCA VS GIMN\xC3\x80STIC TARRAGONA J10"),
      feed("Next | AS Pompier vs. Nouakchott Kings | Super D-1"),
      feed("Rapid Bucuresti vs Petrolul"),            // nothing named
      feed("SWECAN Ultra"),                            // "wec" inside a word
      feed("NBA: PDC Gulls vs Hawks", "NBA"),          // a sport none of them is
  };
  std::set<std::string> found = namedIn(list);
  CHECK(found == std::set<std::string>{"4403", "4930", "snooker", "5088", "5247"});

  // noteSportFeeds keeps them two weeks.
  SportFeedsSeen seen;
  std::optional<Seconds> checked;
  Seconds now = utc(2026, 11, 2, 16, 0);
  CHECK(noteSportFeeds(seen, checked, list, now));
  CHECK(seen.calendarSeen("4403", now + 13 * 86'400));
  CHECK_FALSE(seen.calendarSeen("4403", now + 15 * 86'400));
  auto back = sportFeedsSeenFromJson(toJson(seen));
  CHECK(back == seen);
}

TEST_CASE("calendars: looked up for the picked sports, when seen, unless the scoreboard has them") {
  Seconds now = utc(2026, 11, 2, 16, 0);
  SportFeedsSeen seen = seenAll(now);
  auto keys = [](const std::vector<const Calendar*>& list) {
    std::set<std::string> out;
    for (const Calendar* c : list) out.insert(c->key);
    return out;
  };
  // Before setup: every one seen.
  MySportsPrefs open;
  CHECK(keys(active(open, seen, now, {})).size() == allCalendars().size());
  // Picks: soccer brings the soccer ones, the NHL the hockey ones, the NFL
  // the CFL; snooker needs no pick.
  MySportsPrefs prefs;
  prefs.configured = true;
  prefs.leagues = {"EPL", "NHL"};
  CHECK(keys(active(prefs, seen, now, {})) ==
        std::set<std::string>{"4617", "5247", "4422", "4403", "5088", "5235", "5656", "4930", "5162", "snooker"});
  prefs.leagues = {"NFL", "S:Fighting", "S:Darts", "S:Motorsport"};
  CHECK(keys(active(prefs, seen, now, {})) ==
        std::set<std::string>{"4405", "4445", "5702", "4443", "4554", "4413", "4407", "4489", "snooker"});
  // Not seen in two weeks: not asked for.
  SportFeedsSeen old = seen;
  old.calendars["4405"] = now - 15 * 86'400;
  CHECK_FALSE(keys(active(prefs, old, now, {})).count("4405"));
  // UFC's card only when the scoreboard's UFC isn't looked up.
  std::set<std::string> covered = coveredBy({"UFC", "NFL"}, {});
  CHECK(covered == std::set<std::string>{"4443"});
  CHECK_FALSE(keys(active(prefs, seen, now, covered)).count("4443"));
  // A competition the all-soccer listing has.
  CHECK(coveredBy({}, {"polish ekstraklasa", "spanish laliga"}) == std::set<std::string>{"4422"});
  // Build 127: any soccer pick brings the soccer ones (every other
  // competition alone too); hidden, their games would be on no row, so
  // they aren't asked for.
  prefs.leagues = {kSoccerAllPick};
  CHECK(keys(active(prefs, seen, now, {})).count("4403"));
  prefs.leagues = {kUSAPick};
  CHECK(keys(active(prefs, seen, now, {})).count("4403"));
  prefs.leagues = {"EPL"};
  prefs.hidden = {kSoccerAllPick};
  CHECK_FALSE(keys(active(prefs, seen, now, {})).count("4403"));
  prefs.leagues = {"NFL"};
  prefs.hidden = {"CFL"};
  CHECK_FALSE(keys(active(prefs, seen, now, {})).count("4405"));
  prefs.hidden = {"NFL"};
  prefs.leagues = {"NFL"};  // a hidden pick isn't one
  CHECK_FALSE(keys(active(prefs, seen, now, {})).count("4405"));
}

TEST_CASE("calendars: the day's pass asks the next event, the round when it changed, and the next round") {
  Seconds now = utc(2026, 11, 2, 16, 0);
  time::Zone zone = time::Zone::utc();
  std::vector<const Calendar*> leagues = {&cal("4403"), &cal("4554"), &cal("snooker")};
  Saved saved;
  REQUIRE(Pass::due(saved, leagues, now, zone, false));
  Pass pass(saved, leagues, now, zone, false);
  std::vector<std::string> asked;
  auto answer = [&](const std::string& body, int status = 200) {
    auto ask = pass.next(saved);
    REQUIRE(ask);
    asked.push_back(ask->request.url.substr(std::string(kApiBase).size()));
    const Calendar* c = byKey(ask->key);
    REQUIRE(c);
    pass.onAnswer(saved, status, status == 200 ? parse(body, *c) : std::nullopt, 0);
  };
  // First every next event (and snooker's day), then rounds.
  answer(events({soccerEvent("10", 29, now + 2 * 3600)}));
  answer(kDartsNext);
  answer(kSnookerDay);
  // The current round, which ends within the week: the next one too.
  answer(events({soccerEvent("10", 29, now + 2 * 3600), soccerEvent("11", 29, now + 26 * 3600)}));
  // Darts: the event after this one.
  answer(R"({"events":null})");
  answer(events({soccerEvent("12", 30, now + 6 * 86'400)}));
  CHECK_FALSE(pass.next(saved));
  CHECK(asked == std::vector<std::string>{"eventsnextleague.php?id=4403", "eventsnextleague.php?id=4554",
                                          "eventsday.php?d=2026-11-02&s=Snooker",
                                          "eventsround.php?id=4403&r=29&s=2026",
                                          "eventsround.php?id=4554&r=77&s=2026",
                                          "eventsround.php?id=4403&r=30&s=2026"});
  CHECK(saved.requestCount == 6);
  CHECK(saved.requestDay == std::optional<std::string>("2026-11-02"));
  // Every game, newest copy of each.
  std::vector<SportsGame> list = games(saved, leagues, now);
  std::set<std::string> ids;
  for (const SportsGame& g : list) ids.insert(g.id);
  CHECK(ids == std::set<std::string>{"SWE2|10", "SWE2|11", "SWE2|12", "PDC|2623053", "SNOOKER|2625597"});
  // Checked today: nothing more today.
  CHECK_FALSE(Pass::due(saved, leagues, now + 3600, zone, false));

  // The next day: the round is the same and fresh, so the next event only
  // (the next round was fetched yesterday too).
  Seconds tomorrow = now + 86'400;
  REQUIRE(Pass::due(saved, leagues, tomorrow, zone, false));
  Pass again(saved, {&cal("4403")}, tomorrow, zone, false);
  auto ask = again.next(saved);
  REQUIRE(ask);
  again.onAnswer(saved, 200, parse(events({soccerEvent("11", 29, now + 26 * 3600)}), cal("4403")), 0);
  CHECK_FALSE(again.next(saved));
  CHECK(again.asked() == 1);
  // Three days on the round is fetched again.
  Pass later(saved, {&cal("4403")}, now + 3 * 86'400, zone, false);
  ask = later.next(saved);
  REQUIRE(ask);
  later.onAnswer(saved, 200, parse(events({soccerEvent("11", 29, now + 3 * 86'400 + 3600)}), cal("4403")), 0);
  ask = later.next(saved);
  REQUIRE(ask);
  CHECK(ask->what == Ask::What::round);
  CHECK(ask->round == 29);
}

TEST_CASE("calendars: a competition without rounds asks for today's and tomorrow's games") {
  Seconds now = utc(2026, 11, 2, 16, 0);
  time::Zone zone = time::Zone::utc();
  Saved saved;
  Pass pass(saved, {&cal("5162")}, now, zone, false);
  REQUIRE(pass.next(saved));
  pass.onAnswer(saved, 200, parse(events({soccerEvent("1", 0, now + 5 * 86'400)}), cal("5162")), 0);
  auto today = pass.next(saved);
  REQUIRE(today);
  CHECK(today->request.url.find("eventsday.php?d=2026-11-02&l=5162") != std::string::npos);
  pass.onAnswer(saved, 200, parse(R"({"events":null})", cal("5162")), 0);
  auto next = pass.next(saved);
  REQUIRE(next);
  CHECK(next->request.url.find("eventsday.php?d=2026-11-03&l=5162") != std::string::npos);
}

TEST_CASE("calendars: at most kDailyCap requests a day; a refusal stops the day; errors back off") {
  Seconds now = utc(2026, 11, 2, 16, 0);
  time::Zone zone = time::Zone::utc();
  std::vector<const Calendar*> all;
  for (const Calendar& c : allCalendars()) all.push_back(&c);
  // The cap, counted with what was asked earlier today.
  Saved saved;
  saved.requestDay = "2026-11-02";
  saved.requestCount = kDailyCap - 2;
  Pass capped(saved, all, now, zone, false);
  CHECK(capped.next(saved));
  capped.onAnswer(saved, 200, Parsed(), 0);
  CHECK(capped.next(saved));
  capped.onAnswer(saved, 200, Parsed(), 0);
  CHECK_FALSE(capped.next(saved));
  CHECK(capped.stopped());
  CHECK(saved.requestCount == kDailyCap);
  CHECK_FALSE(Pass::due(saved, all, now + 3600, zone, false));
  // Tomorrow the count starts again.
  CHECK(Pass::due(saved, all, now + 86'400, zone, false));

  // Refused: left alone until tomorrow, or the Retry-After when later.
  Saved refused;
  Pass pass(refused, all, now, zone, false);
  REQUIRE(pass.next(refused));
  pass.onAnswer(refused, 429, std::nullopt, 2 * 86'400);
  CHECK(pass.stopped());
  CHECK_FALSE(pass.next(refused));
  CHECK(refused.refusedUntil == now + 2 * 86'400);
  CHECK_FALSE(Pass::due(refused, all, now + 86'400, zone, false));
  CHECK(Pass::due(refused, all, now + 2 * 86'400 + 60, zone, false));

  // Three errors in a pass: backed off an hour, then longer.
  Saved failing;
  Pass bad(failing, all, now, zone, false);
  for (int i = 0; i < 3; ++i) {
    REQUIRE(bad.next(failing));
    bad.onAnswer(failing, 500, std::nullopt, 0);
  }
  CHECK(bad.stopped());
  CHECK(failing.failures == 1);
  CHECK_FALSE(Pass::due(failing, all, now + 1800, zone, false));
  CHECK(Pass::due(failing, all, now + 3601, zone, false));
  // No response (offline): the pass ends, nothing counted.
  Saved offline;
  Pass down(offline, all, now, zone, false);
  REQUIRE(down.next(offline));
  down.onAnswer(offline, 0, std::nullopt, 0);
  CHECK(down.stopped());
  CHECK(offline.failures == 0);
  CHECK(Pass::due(offline, all, now + 60, zone, false));
}

TEST_CASE("calendars: saved copy round trip, old buckets pruned, games kept to the window") {
  Seconds now = utc(2026, 11, 2, 16, 0);
  time::Zone zone = time::Zone::utc();
  Saved saved;
  Pass pass(saved, {&cal("4403")}, now, zone, false);
  REQUIRE(pass.next(saved));
  pass.onAnswer(saved, 200,
                parse(events({soccerEvent("1", 29, now + 3600), soccerEvent("2", 29, now + 10 * 86'400),
                              soccerEvent("3", 29, now - 6 * 86'400)}),
                      cal("4403")),
                0);
  json::Value v = toJson(saved);
  Saved back = savedFromJson(json::parse(json::write(v)).value_or(json::Value()));
  REQUIRE(back.leagues.count("4403"));
  CHECK(back.leagues.at("4403").round == std::optional<int>(29));
  CHECK(back.leagues.at("4403").season == "2026");
  CHECK(back.requestCount == 1);
  REQUIRE(back.leagues.at("4403").buckets.count("next"));
  CHECK(back.leagues.at("4403").buckets.at("next").games.size() == 3);
  // Published: last week's and this week's, not ten days ahead.
  std::vector<SportsGame> list = games(back, {&cal("4403")}, now);
  REQUIRE(list.size() == 2);
  CHECK(list[0].id == "SWE2|3");
  CHECK(list[1].id == "SWE2|1");
  CHECK(games(back, {}, now).empty());
  // Another schema: start empty.
  json::Value other = v;
  other.set("schema", kSchema + 1);
  CHECK(savedFromJson(other).leagues.empty());
  CHECK(savedFromJson(json::Value()).leagues.empty());
  // A bucket whose games are all over a week ago goes at the next answer.
  LeagueState::Bucket old;
  old.fetchedAt = now;
  old.games = {list[0]};
  back.leagues["4403"].buckets["day:2026-10-27"] = old;
  Pass week(back, {&cal("4403")}, now + 9 * 86'400, zone, false);
  REQUIRE(week.next(back));
  week.onAnswer(back, 200, parse(R"({"events":null})", cal("4403")), 0);
  CHECK_FALSE(back.leagues.at("4403").buckets.count("day:2026-10-27"));
  CHECK(back.leagues.at("4403").buckets.at("next").games.empty());  // the new, empty one
}

TEST_CASE("leftover check: a scheduled game for these sides within a few days, and when") {
  Seconds now = utc(2026, 11, 2, 16, 0);
  using sportsdata::team;
  std::vector<SportsGame> list = {
      sportsdata::game("SOC", "Soccer", "1", team("1", "Mj\xC3\xA4llby AIF"), team("2", "Kalmar FF"), now + 2 * 3600),
      sportsdata::game("SOC", "Soccer", "2", team("3", "Manchester City"), team("4", "Liverpool"), now + 3 * 86'400),
      sportsdata::game("NFL", "NFL", "3", team("5", "Harbor City Gulls", std::string("HCG")), team("6", "Valley Hawks"),
                       now - 2 * 86'400),
  };
  SportsGame darts;
  darts.id = "PDC|9";
  darts.leagueShort = "PDC";
  darts.league = "Darts";
  darts.home = team("t9", "Players Championship 33");
  darts.away = darts.home;
  darts.customTitle = "Players Championship 33";
  darts.start = now + 2 * 86'400;
  list.push_back(darts);
  schedule::GameIndex index(list);
  CHECK(index.size() == 4);
  // Accents, case, "AIF"/"FF" and order don't matter.
  auto hit = index.find("KALMAR", "Mjallby", now, 86'400);
  REQUIRE(hit);
  CHECK(hit->id == "SOC|1");
  CHECK(hit->start == now + 2 * 3600);
  // Within ±N days only.
  CHECK_FALSE(index.find("Man City", "Liverpool", now, 3 * 86'400));  // "man" isn't "manchester"
  CHECK(index.find("Manchester City", "Liverpool FC", now, 3 * 86'400));
  CHECK_FALSE(index.find("Manchester City", "Liverpool", now, 2 * 86'400));
  // Last week's game: a leftover slot names it.
  hit = index.find("Valley Hawks", "Harbor City Gulls", now, 3 * 86'400);
  REQUIRE(hit);
  CHECK(hit->leagueShort == "NFL");
  // The abbreviation names a side too; one side alone doesn't.
  CHECK(index.find("HCG", "Valley Hawks", now, 3 * 86'400));
  CHECK_FALSE(index.find("Harbor City Gulls", "Ridge Rams", now, 3 * 86'400));
  // A title event, by its words.
  auto title = index.findTitle("PDC Players Championship 33 - Board 2", now, 3 * 86'400);
  REQUIRE(title);
  CHECK(title->id == "PDC|9");
  CHECK_FALSE(index.findTitle("Players Championship 34", now, 3 * 86'400));
  CHECK_FALSE(index.findTitle("Darts", now, 3 * 86'400));
  CHECK_FALSE(schedule::GameIndex().find("A", "B", now, 86'400));
}
