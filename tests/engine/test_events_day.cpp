// Build 176 (1.0.19), Live Events by day: the day panel's list (past days
// as deep as the longest archive reaches, Today always, days ahead only
// with games), one past day's fetch (one request per league that can be
// asked by date, kept for the session, refusals and failures), a past
// day's board (no live rows, games with replay first, the rest dimmed with
// why), "replay ends in" and "replay until", OK's replay on a past game,
// where games were at their start kept for a week, and on the pretend TV a
// day picked, picked again and tried again. Every team, channel and link
// is made up.
#include <doctest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/engine/events_build.hpp"
#include "core/engine/events_day.hpp"
#include "core/engine/game_replay.hpp"
#include "core/engine/replay.hpp"
#include "core/engine/store.hpp"
#include "core/sports/day_fetch.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/schedule.hpp"
#include "core/util/time.hpp"
#include "engine/loaded_store.hpp"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::engine;
namespace ed = marigold::engine::events_day;
namespace gr = marigold::engine::game_replay;
namespace days = marigold::sports::days;
using marigold::sports::EventSource;
using marigold::sports::LivePhase;
using marigold::sports::LiveStatus;
using marigold::sports::SportsEvent;
using sportsdata::eventOf;
using sportsdata::game;
using sportsdata::team;
using sportsdata::utc;

namespace {

// Tuesday 2026-11-17, 7:42 PM UTC (the mock's evening), on the minute.
const time::Seconds kNow = utc(2026, 11, 17, 19, 42);
const time::Zone kUtc = time::Zone::utc();
const time::Seconds kToday = utc(2026, 11, 17, 0, 0);
const time::Seconds kMonday = utc(2026, 11, 16, 0, 0);
const time::Seconds kSunday = utc(2026, 11, 15, 0, 0);

std::vector<std::string> words(const std::vector<DayRow>& rows) {
  std::vector<std::string> out;
  for (const DayRow& r : rows) out.push_back(r.word);
  return out;
}

const sports::League& L(const char* s) { return *sports::leagueByShort(s); }

const sports::League& ncaaPageLeague() {
  for (const sports::League& l : sports::allLeagues())
    if (l.ncaaPage) return l;
  FAIL("no NCAA page league");
  return sports::allLeagues().front();
}

// One final on a scoreboard, as its day's answer has it.
std::string finalGame(const std::string& id, const std::string& date, const std::string& home, const std::string& away,
                      const std::string& homeScore, const std::string& awayScore) {
  return R"({"id":")" + id + R"(","date":")" + date + R"(","competitions":[{"competitors":[)" +
         R"({"homeAway":"home","score":")" + homeScore + R"(","team":{"id":"h)" + id + R"(","displayName":")" + home +
         R"(","abbreviation":"HCO"}},)" + R"({"homeAway":"away","score":")" + awayScore + R"(","team":{"id":"a)" + id +
         R"(","displayName":")" + away + R"(","abbreviation":"VAH"}}],)" +
         R"("venue":{"fullName":"Owl Arena"},"broadcasts":[{"market":"national","names":["ESPN"]}],)" +
         R"("status":{"type":{"name":"STATUS_FINAL","state":"post","completed":true}}}]})";
}

std::string scoreboard(const std::vector<std::string>& events) {
  std::string body = R"({"events":[)";
  for (size_t i = 0; i < events.size(); ++i) body += (i ? "," : "") + events[i];
  return body + "]}";
}

EventSource listed(const std::string& id, EventSource::Kind kind, std::optional<time::Seconds> from,
                   std::optional<time::Seconds> until = std::nullopt) {
  EventSource s;
  s.channelID = id;
  s.kind = kind;
  s.lang = "en";
  s.from = from;
  s.until = until;
  return s;
}

LiveStatus finalScore(const std::string& home, const std::string& away) {
  LiveStatus s;
  s.phase = LivePhase::final;
  s.detail = "Final";
  s.homeScore = home;
  s.awayScore = away;
  return s;
}

gr::Channels archives() {
  gr::Channels c;
  c["espn"] = {2, false, "ESPN"};
  c["nbatv"] = {1, false, "NBA TV"};
  c["tnt"] = {2, true, "TNT"};
  return c;
}

}  // namespace

// ------------------------------------------------------------------ the days

TEST_CASE("by day: past days as deep as the longest archive, Today always, days ahead only with games") {
  // No archive anywhere, no games ahead: Today alone.
  std::vector<DayRow> rows = ed::dayRows(kNow, kUtc, 0, {}, 0);
  CHECK(words(rows) == std::vector<std::string>{"Today"});
  CHECK(rows[0].today);
  CHECK(rows[0].shown);
  CHECK(rows[0].date == "Tue, Nov 17");

  // Two days (less an hour, as replay uses it) at 7:42 PM: Sunday from 8:42
  // PM on is still in it ("Replay on some"), Monday whole ("Replay").
  // Saturday isn't: nothing of it is left.
  rows = ed::dayRows(kNow, kUtc, 2, {}, 0);
  REQUIRE(words(rows) == std::vector<std::string>{"Sunday", "Yesterday", "Today"});
  CHECK(rows[0].day == kSunday);
  CHECK(rows[0].date == "Nov 15");
  CHECK(rows[0].replay == DayRow::Replay::Some);
  CHECK(rows[0].past);
  CHECK(rows[1].day == kMonday);
  CHECK(rows[1].date == "Mon, Nov 16");
  CHECK(rows[1].replay == DayRow::Replay::All);
  CHECK(rows[2].replay == DayRow::Replay::None);
  CHECK_FALSE(rows[2].past);

  // One day: yesterday only in part.
  rows = ed::dayRows(kNow, kUtc, 1, {}, 0);
  REQUIRE(words(rows) == std::vector<std::string>{"Yesterday", "Today"});
  CHECK(rows[0].replay == DayRow::Replay::Some);

  // A week: seven days back, the oldest only in part.
  rows = ed::dayRows(kNow, kUtc, 7, {}, 0);
  REQUIRE(rows.size() == 8);
  CHECK(rows[0].day == utc(2026, 11, 10, 0, 0));
  CHECK(rows[0].replay == DayRow::Replay::Some);
  for (size_t i = 1; i < 7; ++i) CHECK(rows[i].replay == DayRow::Replay::All);

  // Days ahead: only those with a game, within the week; never empty ones.
  std::vector<time::Seconds> starts = {utc(2026, 11, 18, 20, 0), utc(2026, 11, 19, 1, 0), utc(2026, 11, 21, 18, 0),
                                       utc(2026, 12, 1, 18, 0), kNow - 3600};
  rows = ed::dayRows(kNow, kUtc, 2, starts, kMonday);
  REQUIRE(words(rows) == std::vector<std::string>{"Sunday", "Yesterday", "Today", "Tomorrow", "Thursday", "Saturday"});
  CHECK(rows[3].date == "Wed, Nov 18");
  CHECK(rows[4].date == "Nov 19");
  CHECK(rows[5].date == "Nov 21");
  // The day shown carries the check.
  CHECK(rows[1].shown);
  CHECK_FALSE(rows[2].shown);
  // Nothing in them names a source or counts anything.
  for (const DayRow& r : rows) CHECK(r.word.find_first_of("0123456789") == std::string::npos);
}

TEST_CASE("by day: the day's words") {
  CHECK(ed::dayWord(kMonday, kNow, kUtc) == "Yesterday");
  CHECK(ed::dayWord(kSunday, kNow, kUtc) == "Sunday");
  CHECK(ed::dayWord(kToday, kNow, kUtc) == "Today");
  CHECK(ed::dayWord(utc(2026, 11, 18, 0, 0), kNow, kUtc) == "Tomorrow");
  CHECK(ed::dayWord(utc(2026, 11, 19, 0, 0), kNow, kUtc) == "Thursday");
  CHECK(ed::dayTitle(kMonday, kUtc) == "Monday, Nov 16");
  CHECK(ed::loadingLine(kMonday, kUtc) == "Getting Monday's games\xe2\x80\xa6");
  CHECK(ed::failedLine(kMonday, kUtc) == "Monday's games couldn't be loaded.");
  CHECK(ed::emptyLine(kMonday, kUtc) == "No games on Monday in your sports.");
  // A local day, not UTC's: late Monday evening in New York is Monday.
  time::Zone ny = time::Zone::usEastern();
  time::Seconds lateMonday = utc(2026, 11, 17, 3, 30);  // 10:30 PM Monday there
  CHECK(ed::dayTitle(ed::dayOf(lateMonday, ny), ny) == "Monday, Nov 16");
  CHECK(ed::nextDay(ed::dayOf(lateMonday, ny), ny) == utc(2026, 11, 17, 5, 0));
  CHECK(days::dateOf(ed::dayOf(lateMonday, ny), ny) == "20261116");
}

// ------------------------------------------------- replay ends / replay until

TEST_CASE("by day: \"replay ends in\" within three hours, \"replay until\" in words") {
  time::Seconds from = utc(2026, 11, 16, 17, 14);
  time::Seconds leaves = ed::replayLeaves(from, 2);
  // The first minute the archive no longer holds it, as replay::covers says.
  CHECK(replay::covers(2, from, leaves - 60));
  CHECK_FALSE(replay::covers(2, from, leaves));
  CHECK(leaves - from <= 2 * 86400 - 3600 + 60);

  CHECK(ed::replayEnds(kNow + 2 * 3600, kNow) == "replay ends in 2 h");
  CHECK(ed::replayEnds(kNow + 2 * 3600 + 40 * 60, kNow) == "replay ends in 2 h");
  CHECK(ed::replayEnds(kNow + 3 * 3600, kNow) == "replay ends in 3 h");
  CHECK(ed::replayEnds(kNow + 40 * 60, kNow) == "replay ends in 40 min");
  CHECK(ed::replayEnds(kNow + 30, kNow) == "replay ends in 1 min");
  CHECK(ed::replayEnds(kNow + 3 * 3600 + 60, kNow).empty());
  CHECK(ed::replayEnds(kNow, kNow).empty());
  CHECK(ed::replayEnds(kNow - 60, kNow).empty());

  CHECK(ed::replayUntil(utc(2026, 11, 17, 23, 14), kNow, kUtc, true) == "replay until 11:14 PM");
  CHECK(ed::replayUntil(utc(2026, 11, 18, 17, 15), kNow, kUtc, true) == "replay until tomorrow 5:15 PM");
  CHECK(ed::replayUntil(utc(2026, 11, 19, 17, 15), kNow, kUtc, true) == "replay until Thursday 5:15 PM");
  CHECK(ed::replayUntil(utc(2026, 11, 18, 17, 15), kNow, kUtc, false) == "replay until tomorrow 17:15");
}

// ------------------------------------------------------ one past day's fetch

TEST_CASE("by day: a past day's fetch asks each league that has a dated query once, a moment apart") {
  days::DayFetch f;
  std::vector<sports::League> active = {L("NBA"), L("NHL"), sports::cricketLeague(), ncaaPageLeague()};
  REQUIRE(f.begin(kMonday + 5 * 3600, active, {}, {}, kNow, kUtc));
  CHECK(f.day() == kMonday);
  std::vector<days::DayFetch::Pending> asked;
  // NBA: a final that day, and one starting the next day (left out).
  auto p = f.nextRequest();
  REQUIRE(p);
  CHECK(p->league.shortName == "NBA");
  CHECK(p->waitMs == 0);
  CHECK(p->request.url.find("dates=20261116") != std::string::npos);
  CHECK(p->request.url == days::request(L("NBA"), "20261116", false).url);
  CHECK_FALSE(f.nextRequest());  // one at a time
  f.onResponse(200, scoreboard({finalGame("9001", "2026-11-16T22:00Z", "Harbor City Owls", "Valley Hawks", "24", "20"),
                                finalGame("9002", "2026-11-17T00:30Z", "River Town Otters", "Lake Gulls", "1", "2")}));
  // NHL: the source is down.
  p = f.nextRequest();
  REQUIRE(p);
  CHECK(p->league.shortName == "NHL");
  CHECK(p->waitMs == sports::schedule::kRequestSpacingMs);
  f.onResponse(500, "");
  // Cricket's listing has no date: never asked. The NCAA's page for a day
  // without games is "not found": an answer.
  p = f.nextRequest();
  REQUIRE(p);
  CHECK(p->league.ncaaPage);
  CHECK(p->request.url.find("/2026/11/16/") != std::string::npos);
  f.onResponse(404, "");
  CHECK_FALSE(f.nextRequest());
  auto day = f.takeFinished();
  REQUIRE(day);
  CHECK_FALSE(f.busy());
  CHECK(day->day == kMonday);
  CHECK(day->leagues == 3);
  CHECK(day->answered == 2);
  CHECK(day->failed == 1);
  CHECK_FALSE(day->failedAll());
  REQUIRE(day->games.size() == 1);
  CHECK(day->games[0].id == "NBA|9001");
  // The final and its score come from that day's answer.
  REQUIRE(day->statuses.count("NBA|9001"));
  CHECK(day->statuses.at("NBA|9001").phase == LivePhase::final);
  CHECK(day->statuses.at("NBA|9001").homeScore == std::optional<std::string>("24"));
  CHECK_FALSE(day->statuses.count("NBA|9002"));
  // The log line: the day, the leagues, how it went; never a link.
  std::string note = days::DayFetch::note(*day, kUtc);
  CHECK(note == "Monday's games, 3 leagues, fine, 1 failed");
  CHECK(note.find("http") == std::string::npos);
  CHECK(note.find("espn") == std::string::npos);
}

TEST_CASE("by day: a refusal leaves that source alone; a bad request is asked again in the plain form") {
  SUBCASE("refused") {
    days::DayFetch f;
    REQUIRE(f.begin(kMonday, {L("NBA"), L("NHL"), ncaaPageLeague()}, {}, {}, kNow, kUtc));
    auto p = f.nextRequest();
    REQUIRE(p);
    f.onResponse(429, "", 0);
    // NHL is the same source: skipped. The NCAA's isn't.
    p = f.nextRequest();
    REQUIRE(p);
    CHECK(p->league.ncaaPage);
    f.onResponse(500, "");
    CHECK_FALSE(f.nextRequest());
    auto day = f.takeFinished();
    REQUIRE(day);
    CHECK(day->failedAll());
    CHECK(days::DayFetch::note(*day, kUtc) == "Monday's games, 3 leagues, failed");
    std::map<std::string, time::Seconds> refused = f.takeRefusals();
    REQUIRE(refused.count("espn.com"));
    CHECK(refused["espn.com"] > kNow);
    CHECK(f.takeRefusals().empty());  // once
  }
  SUBCASE("still refused from before: nothing asked") {
    days::DayFetch f;
    std::map<std::string, time::Seconds> before = {{"espn.com", kNow + 3600}};
    f.begin(kMonday, {L("NBA"), L("NHL")}, before, {}, kNow, kUtc);
    CHECK_FALSE(f.nextRequest());
    auto day = f.takeFinished();
    REQUIRE(day);
    CHECK(day->failedAll());
  }
  SUBCASE("400: the plain form, learned") {
    days::DayFetch f;
    REQUIRE(f.begin(kMonday, {L("NBA")}, {}, {}, kNow, kUtc));
    auto p = f.nextRequest();
    REQUIRE(p);
    CHECK(p->request.url.find("limit=500") != std::string::npos);
    f.onResponse(400, "");
    p = f.nextRequest();
    REQUIRE(p);
    CHECK(p->request.url == days::request(L("NBA"), "20261116", true).url);
    CHECK(p->request.url.find("limit=") == std::string::npos);
    f.onResponse(200, scoreboard({}));
    CHECK_FALSE(f.nextRequest());
    auto day = f.takeFinished();
    REQUIRE(day);
    CHECK(day->answered == 1);
    CHECK(f.takePlain() == std::set<std::string>{"NBA"});
    // Known plain: asked that way at once.
    days::DayFetch g;
    g.begin(kMonday, {L("NBA")}, {}, {{"NBA", "plain"}}, kNow, kUtc);
    p = g.nextRequest();
    REQUIRE(p);
    CHECK(p->request.url == days::request(L("NBA"), "20261116", true).url);
  }
  SUBCASE("nothing to ask: done at once") {
    days::DayFetch f;
    CHECK_FALSE(f.begin(kMonday, {sports::cricketLeague()}, {}, {}, kNow, kUtc));
    auto day = f.takeFinished();
    REQUIRE(day);
    CHECK(day->leagues == 0);
    CHECK_FALSE(day->failedAll());
  }
}

TEST_CASE("by day: the days fetched are kept for the session, a few, fresh for an hour; failing backs off") {
  days::DayCache cache;
  auto made = [](time::Seconds day, time::Seconds at, bool ok) {
    days::Day d;
    d.day = day;
    d.fetchedAt = at;
    d.leagues = 2;
    d.answered = ok ? 2 : 0;
    d.failed = ok ? 0 : 2;
    return d;
  };
  cache.put(made(kMonday, kNow, true));
  CHECK(cache.fresh(kMonday, kNow + 59 * 60));
  CHECK_FALSE(cache.fresh(kMonday, kNow + 61 * 60));  // a re-pick an hour on asks again
  CHECK_FALSE(cache.fresh(kSunday, kNow));
  // At most kKept days, the oldest dropped.
  cache.put(made(kSunday, kNow, true));
  cache.put(made(utc(2026, 11, 14, 0, 0), kNow, true));
  cache.put(made(utc(2026, 11, 13, 0, 0), kNow, true));
  CHECK(cache.size() == days::DayCache::kKept);
  CHECK_FALSE(cache.find(kMonday));
  CHECK(cache.find(kSunday));
  // A failure never replaces a day that came.
  cache.put(made(kSunday, kNow + 60, false));
  REQUIRE(cache.find(kSunday));
  CHECK_FALSE(cache.find(kSunday)->failedAll());
  // That was a failure in a row; a second one: a re-pick waits out the
  // schedule's backoff.
  CHECK(cache.failures() == 1);
  CHECK(cache.mayFetch(kNow));
  cache.put(made(kMonday, kNow, false));
  CHECK_FALSE(cache.fresh(kMonday, kNow));
  CHECK(cache.failures() == 2);
  CHECK_FALSE(cache.mayFetch(kNow + 60));
  CHECK(cache.mayFetch(kNow + sports::schedule::kBackoff[0]));
  // One that comes resets it.
  cache.put(made(kMonday, kNow + 2 * 3600, true));
  CHECK(cache.failures() == 0);
  CHECK(cache.mayFetch(kNow + 2 * 3600));
}

// --------------------------------------------------------- a past day's board

namespace {

const EventRow* rowOf(const EventsBoard& b, const std::string& id) {
  for (const EventRow& r : b.rows)
    if (r.id == id) return &r;
  return nullptr;
}

std::vector<std::string> idsIn(const EventRow& r) {
  std::vector<std::string> out;
  for (const EventGroup& g : r.groups)
    for (const EventTile& t : g.tiles) out.push_back(t.id);
  return out;
}

struct DayRig {
  events_build::BoardInput in;
  std::vector<SportsEvent> events;
  explicit DayRig(bool past = true) {
    in.catalog = std::make_shared<catalog::CatalogResult>();
    in.guide = std::make_shared<engine::Channels>();
    in.channels = std::make_shared<std::vector<Channel>>();
    in.now = kNow;
    in.zone = kUtc;
    in.settings.showsUnwatchable = true;
    in.replayChannels = std::make_shared<gr::Channels>(archives());
    in.prefs.configured = true;
    in.prefs.leagues = {"NBA", "NHL"};
    in.prefs.teams = {sports::teamKey("NBA", "h1")};
    sports::board::Input::Day day;
    day.from = past ? kMonday : utc(2026, 11, 19, 0, 0);
    day.to = past ? kToday : utc(2026, 11, 20, 0, 0);
    day.past = past;
    in.day = day;
  }
  void add(const std::string& id, time::Seconds start, std::vector<EventSource> sources,
           const std::string& league = "NBA", bool final = true) {
    sports::SportsGame g = game(league, league, id, team("h" + id, "Harbor City Owls", "HCO"),
                                team("a" + id, "Valley Hawks", "VAH"), start, {}, "Owl Arena");
    events.push_back(eventOf(g, std::move(sources)));
    if (final) in.statuses[g.id] = finalScore("24", "20");
  }
  std::shared_ptr<EventsBoard> make() {
    in.events = std::make_shared<std::vector<SportsEvent>>(events);
    return events_build::board(in).events;
  }
};

}  // namespace

TEST_CASE("by day: a past day's board: Your teams, then each sport; replay first, the rest dimmed with why") {
  DayRig r;
  const time::Seconds owls = utc(2026, 11, 16, 17, 15);   // your team, on ESPN (two days)
  const time::Seconds late = utc(2026, 11, 16, 22, 0);    // on NBA TV (one day): leaves at 9 PM tonight
  const time::Seconds kcbs = utc(2026, 11, 16, 16, 0);    // on a channel without an archive
  const time::Seconds none = utc(2026, 11, 16, 15, 0);    // on none of your channels
  const time::Seconds early = utc(2026, 11, 16, 12, 0);   // on ESPN, in the morning
  r.add("1", owls, {listed("espn", EventSource::Kind::guide, owls - 900, owls + 3 * 3600 + 16 * 60)});
  r.add("2", late, {listed("nbatv", EventSource::Kind::guide, late - 600, late + 3 * 3600)});
  r.add("3", kcbs, {listed("kcbs", EventSource::Kind::lineup, kcbs - 600, kcbs + 3 * 3600)});
  r.add("4", none, {});
  r.add("5", early, {listed("espn", EventSource::Kind::guide, early, early + 3 * 3600)});
  // Another day's game, and another sport's game that day.
  r.add("6", utc(2026, 11, 15, 20, 0), {listed("espn", EventSource::Kind::guide, utc(2026, 11, 15, 20, 0))});
  r.add("7", utc(2026, 11, 16, 23, 0), {listed("tnt", EventSource::Kind::lineup, utc(2026, 11, 16, 23, 0))}, "NHL");
  std::shared_ptr<EventsBoard> b = r.make();

  // No Live now, Starting soon, Later today or Full games: Your teams, then
  // the sports in the picks' order.
  std::vector<std::string> rows;
  for (const EventRow& row : b->rows) rows.push_back(row.id);
  CHECK(rows == std::vector<std::string>{"teams", "sport|NBA", "sport|NHL"});
  // No group headers.
  for (const EventRow& row : b->rows)
    for (const EventGroup& g : row.groups) CHECK(g.title.empty());
  const EventRow* nba = rowOf(*b, "sport|NBA");
  REQUIRE(nba);
  // With replay first (by start), then on your channels without, then not
  // on them. Sunday's game isn't on Monday's board.
  CHECK(idsIn(*nba) == std::vector<std::string>{"sev|NBA|5", "sev|NBA|1", "sev|NBA|2", "sev|NBA|3", "sev|NBA|4"});
  CHECK(idsIn(*rowOf(*b, "teams")) == std::vector<std::string>{"sev|NBA|1"});

  const EventTile* t1 = b->tile("sev|NBA|1");
  REQUIRE(t1);
  // FINAL and the score, as finals show them.
  CHECK(t1->finished);
  CHECK(t1->pill == EventTile::Pill::Final);
  REQUIRE(t1->sides.size() == 2);
  CHECK_FALSE(t1->sides[0].score.empty());
  // The footer: the start time, the channel and the mark.
  CHECK(t1->footLeft == "5:15 PM");
  CHECK_FALSE(t1->footWarn);
  CHECK(t1->footRight == "ESPN");
  CHECK(t1->footReplay);
  CHECK_FALSE(t1->dimmed);
  CHECK(t1->playsReplay);
  // The hero: the matchup, the score and the venue, where and how long.
  CHECK(t1->heroTitle.find(" at ") != std::string::npos);
  CHECK(t1->heroStatus.find("VAH 20 \xe2\x80\x93 HCO 24") != std::string::npos);
  CHECK(t1->heroStatus.find("Owl Arena") != std::string::npos);
  CHECK(t1->whereLine == "On ESPN, 5:15 \xe2\x80\x93 8:31 PM \xc2\xb7 replay until tomorrow 4:15 PM");

  // Leaving within three hours: "replay ends in", amber.
  const EventTile* t2 = b->tile("sev|NBA|2");
  REQUIRE(t2);
  CHECK(t2->footWarn);
  CHECK(t2->footLeft == "replay ends in 1 h");
  CHECK(t2->footRight == "NBA TV");

  // No replay: dimmed, and why.
  const EventTile* t3 = b->tile("sev|NBA|3");
  REQUIRE(t3);
  CHECK(t3->dimmed);
  CHECK_FALSE(t3->playsReplay);
  CHECK_FALSE(t3->hasReplay());
  CHECK(t3->footLeft == "4:00 PM");
  CHECK(t3->footRight == "No replay");
  const EventTile* t4 = b->tile("sev|NBA|4");
  REQUIRE(t4);
  CHECK(t4->dimmed);
  CHECK(t4->footRight == "Not on your channels");
  CHECK(t4->whereLine == "Not on your channels");

  // Scores off: none anywhere, as the settings say.
  r.in.settings.showsScores = false;
  b = r.make();
  const EventTile* quiet = b->tile("sev|NBA|1");
  REQUIRE(quiet);
  for (const EventSide& s : quiet->sides) CHECK(s.score.empty());
  CHECK(quiet->heroStatus == "Owl Arena");

  // "Games not on my channels" off: those aren't on the board.
  r.in.settings.showsScores = true;
  r.in.settings.showsUnwatchable = false;
  b = r.make();
  CHECK_FALSE(b->tile("sev|NBA|4"));
  CHECK(b->tile("sev|NBA|3"));
}

TEST_CASE("by day: a day ahead's board is that day's games by sport, as upcoming tiles") {
  DayRig r(false);
  const time::Seconds thu = utc(2026, 11, 19, 1, 0);
  r.add("1", thu, {listed("espn", EventSource::Kind::guide, thu - 600, thu + 3 * 3600)}, "NBA", false);
  r.add("2", utc(2026, 11, 19, 23, 30), {listed("tnt", EventSource::Kind::lineup, std::nullopt)}, "NHL", false);
  r.add("3", utc(2026, 11, 18, 20, 0), {listed("espn", EventSource::Kind::guide, std::nullopt)}, "NBA", false);
  std::shared_ptr<EventsBoard> b = r.make();
  std::vector<std::string> rows;
  for (const EventRow& row : b->rows) rows.push_back(row.id);
  CHECK(rows == std::vector<std::string>{"teams", "sport|NBA", "sport|NHL"});
  CHECK_FALSE(b->tile("sev|NBA|3"));  // Wednesday's
  const EventTile* t = b->tile("sev|NBA|1");
  REQUIRE(t);
  CHECK(t->upcoming);
  CHECK(t->status == "1:00 AM");
  CHECK(t->footLeft.empty());
  CHECK(t->footRight.empty());
  CHECK_FALSE(t->dimmed);
  CHECK_FALSE(t->hasReplay());
}

// ---------------------------------------------------- OK's replay, past day

TEST_CASE("by day: OK on a past game plays its replay: the longest archive, from a minute before the start") {
  DayRig r;
  const time::Seconds start = utc(2026, 11, 16, 19, 0);
  // On NBA TV (one day) and ESPN (two days), both listed at the start.
  r.add("1", start, {listed("nbatv", EventSource::Kind::lineup, start - 600, start + 3 * 3600),
                     listed("espn", EventSource::Kind::guide, start, start + 3 * 3600)});
  // Live at the scoreboard's last word (a late game): still a final here.
  r.add("2", start + 3600, {listed("espn", EventSource::Kind::guide, start + 3600)});
  LiveStatus live;
  live.phase = LivePhase::live;
  r.in.statuses["NBA|2"] = live;
  // Postponed: no replay.
  r.add("3", start + 1800, {listed("espn", EventSource::Kind::guide, start + 1800)});
  LiveStatus postponed;
  postponed.phase = LivePhase::postponed;
  r.in.statuses["NBA|3"] = postponed;
  std::shared_ptr<EventsBoard> b = r.make();
  const EventTile* t = b->tile("sev|NBA|1");
  REQUIRE(t);
  CHECK(t->playsReplay);
  CHECK(t->replayChannelId == "espn");
  CHECK(t->replayChannelName == "ESPN");
  CHECK(t->replayFrom == start - gr::kBeforeStart);
  CHECK(t->replayIsOtherChannel);  // OK on the game itself would play NBA TV (My Lineup)
  CHECK(t->replayLine.rfind("Started 7:00 PM", 0) == 0);
  // What Watch replay is given is the same pick as game_replay's.
  SportsEvent e = r.events[0];
  auto pick = gr::pick(e, SportsEvent::State::finished, kNow, archives(), {"en"});
  REQUIRE(pick);
  CHECK(pick->channelId == t->replayChannelId);
  CHECK(pick->from == t->replayFrom);
  const EventTile* late = b->tile("sev|NBA|2");
  REQUIRE(late);
  CHECK(late->playsReplay);
  const EventTile* off = b->tile("sev|NBA|3");
  REQUIRE(off);
  CHECK_FALSE(off->playsReplay);
  CHECK(off->dimmed);
  // Its start out of every archive by now: none.
  r.in.now = kNow + 2 * 86400;
  b = r.make();
  REQUIRE(b->tile("sev|NBA|1"));
  CHECK_FALSE(b->tile("sev|NBA|1")->playsReplay);
}

// ---------------------------------------------- where games were at their start

TEST_CASE("by day: where each game was at its start is kept a week, small") {
  ed::ReplaySources kept;
  const time::Seconds start = kNow - 3 * 3600;
  std::vector<SportsEvent> events;
  events.push_back(eventOf(game("NBA", "NBA", "1", team("h1", "Harbor City Owls"), team("a1", "Valley Hawks"), start),
                           {listed("espn", EventSource::Kind::guide, start - 600, start + 3 * 3600),
                            listed("feed", EventSource::Kind::provider, start),
                            listed("fs1", EventSource::Kind::guide, std::nullopt),
                            listed("late", EventSource::Kind::guide, start + 3600)}));
  // Not started yet: nothing to keep.
  events.push_back(eventOf(game("NBA", "NBA", "2", team("h2", "River Town Otters"), team("a2", "Lake Gulls"), kNow + 3600),
                           {listed("espn", EventSource::Kind::guide, kNow + 3000)}));
  CHECK(kept.note(events, kNow));
  CHECK_FALSE(kept.note(events, kNow));  // the same again: nothing new
  REQUIRE(kept.find("NBA|1"));
  REQUIRE(kept.find("NBA|1")->size() == 1);
  CHECK(kept.find("NBA|1")->front().channelID == "espn");
  CHECK_FALSE(kept.find("NBA|2"));
  // Saved and read back; a week on it's gone.
  std::string text = kept.toJson();
  CHECK(text.find("http") == std::string::npos);
  ed::ReplaySources back = ed::ReplaySources::fromJson(text, kNow);
  REQUIRE(back.find("NBA|1"));
  CHECK(*back.find("NBA|1") == *kept.find("NBA|1"));
  CHECK(ed::ReplaySources::fromJson(text, kNow + ed::ReplaySources::kKeptFor + 86400).size() == 0);
  CHECK(ed::ReplaySources::fromJson("not json", kNow).size() == 0);
  // A past day's events use them first, then the channels of its networks.
  sports::SportsGame g = *events[0].game;
  std::map<std::string, std::vector<EventSource>> network;
  network["NBA|1"] = {listed("espn", EventSource::Kind::guide, std::nullopt),
                      listed("abc", EventSource::Kind::lineup, std::nullopt)};
  std::vector<SportsEvent> past = ed::pastEvents(
      {g}, [&kept](const sports::SportsGame& x) { return *kept.find(x.id); }, network);
  REQUIRE(past.size() == 1);
  CHECK(past[0].id == "sev|NBA|1");
  REQUIRE(past[0].sources.size() == 2);
  CHECK(past[0].sources[0].channelID == "espn");
  CHECK(past[0].sources[0].from);
  CHECK(past[0].sources[1].channelID == "abc");
}

// ------------------------------------------------------- on the pretend TV

namespace {

std::string playlist() {
  std::string m = "#EXTM3U\n";
  int n = 0;
  for (const char* net : {"US: ESPN", "US: FOX", "US: TNT"}) {
    m += "#EXTINF:-1 group-title=\"US| SPORTS\"," + std::string(net) + "\n";
    m += "http://mock.example.invalid:8080/" + std::to_string(++n) + ".ts\n";
  }
  return m;
}

void seconds(Store& store, fake::Tv& tv, int s) {
  for (int i = 0; i < s * 10; ++i) {
    tv.pool.drain();
    tv.ms += 100;
    store.tick(tv.ms);
  }
  tv.pool.drain();
}

// The schedule fetched just now, for every league looked up: no day's
// fetch of its own to tell apart.
void savedToday(fake::Tv& tv) {
  sports::schedule::Saved s;
  s.fetchedAt = time::now();
  s.schema = sports::schedule::kSchema;
  std::vector<std::string> set;
  for (const sports::League& l : sports::allLeagues()) set.push_back(l.shortName);
  s.leagueSet = set;
  tv.files.write("sports-schedule.json", sports::schedule::toJson(s), false);
}

struct DayUrls {
  std::vector<std::string> urls;
  std::string nba;
};
// Every league's day request (whichever the store looks up).
DayUrls dayUrls(const fake::Tv& tv, time::Seconds day) {
  DayUrls out;
  std::string date = days::dateOf(day, tv.zone);
  std::set<std::string> seen;
  for (const sports::League& l : sports::allLeagues()) {
    if (!days::byDate(l) || !seen.insert(l.shortName).second) continue;
    std::string url = days::request(l, date, false).url;
    out.urls.push_back(url);
    if (l.shortName == "NBA") out.nba = url;
  }
  return out;
}

int asked(const fake::Tv& tv, const std::vector<std::string>& urls) {
  int n = 0;
  for (const std::string& u : urls) {
    auto it = tv.pool.asked.find(u);
    if (it != tv.pool.asked.end()) n += it->second;
  }
  return n;
}

}  // namespace

TEST_CASE("by day on the TV: a picked day is fetched once, kept, tried again; back to today on closing") {
  fake::Tv tv;
  tv.pool.answer(fake::kPlaylist, playlist());
  savedToday(tv);
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  store.eventsOpen(true);
  seconds(store, tv, 5);
  const time::Seconds yesterday = ed::dayOf(time::now() - 86400, tv.zone);
  DayUrls urls = dayUrls(tv, yesterday);
  REQUIRE(urls.urls.size() > 2);
  REQUIRE_FALSE(urls.nba.empty());
  for (const std::string& u : urls.urls) tv.pool.answer(u, scoreboard({}));
  sports::Seconds noon = yesterday + 12 * 3600;
  time::Civil c = time::civilFromSeconds(noon);
  char date[32];
  std::snprintf(date, sizeof date, "%04d-%02d-%02dT12:00Z", c.year, c.month, c.day);
  tv.pool.answer(urls.nba, scoreboard({finalGame("9001", date, "Harbor City Owls", "Valley Hawks", "24", "20")}));

  CHECK(asked(tv, urls.urls) == 0);
  store.showEventsDay(yesterday);
  CHECK(store.eventsDay() == yesterday);
  REQUIRE(store.dayBoard());
  CHECK(store.dayBoard()->loading);
  CHECK(store.dayBoard()->loadingLine.rfind("Getting ", 0) == 0);
  CHECK(store.dayBoard()->dayTitle == ed::dayTitle(yesterday, tv.zone));
  // Today's board is untouched meanwhile.
  CHECK(store.events()->day == 0);
  seconds(store, tv, 60);
  // Each league looked up with a dated query, once.
  int leagues = 0;
  for (const std::string& u : urls.urls) {
    auto it = tv.pool.asked.find(u);
    if (it == tv.pool.asked.end()) continue;
    ++leagues;
    CHECK(it->second == 1);
  }
  CHECK(leagues > 2);
  CHECK(tv.pool.asked[urls.nba] == 1);
  std::shared_ptr<const EventsBoard> b = store.dayBoard();
  REQUIRE(b);
  CHECK_FALSE(b->loading);
  CHECK_FALSE(b->failed);
  CHECK(b->past);
  CHECK(b->day == yesterday);
  for (const EventRow& r : b->rows) {
    CHECK(r.id != "live");
    CHECK(r.id != "full");
  }
  // The game, from its own day's answer: a final with its score.
  const EventTile* t = store.eventTile("sev|NBA|9001");
  if (t) {
    CHECK(t->finished);
    CHECK(t->dimmed);  // on none of these channels' archives
  }
  // The log line, without a link.
  bool logged = false;
  for (const std::string& line : store.takeEventsLog()) {
    if (line.find("'s games, ") == std::string::npos) continue;
    logged = true;
    CHECK(line.find("leagues, fine") != std::string::npos);
    CHECK(line.find("http") == std::string::npos);
  }
  CHECK(logged);

  // Today, then the same day again: kept, not asked again.
  store.showEventsDay(0);
  CHECK(store.eventsDay() == 0);
  CHECK_FALSE(store.dayBoard());
  store.showEventsDay(yesterday);
  seconds(store, tv, 5);
  CHECK(asked(tv, urls.urls) == leagues);
  REQUIRE(store.dayBoard());
  CHECK_FALSE(store.dayBoard()->loading);
  // Try again asks again.
  store.showEventsDay(yesterday, true);
  seconds(store, tv, 60);
  CHECK(asked(tv, urls.urls) == 2 * leagues);
  CHECK(tv.pool.asked[urls.nba] == 2);

  // Closing Live Events: the next open shows today.
  store.eventsOpen(false);
  CHECK(store.eventsDay() == 0);
}

TEST_CASE("by day on the TV: a day that can't be had says so, with Try again") {
  fake::Tv tv;
  tv.pool.answer(fake::kPlaylist, playlist());
  savedToday(tv);
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  store.eventsOpen(true);
  const time::Seconds twoBack = ed::dayOf(time::now() - 2 * 86400, tv.zone);
  // Nothing answers (a network that's down).
  store.showEventsDay(twoBack);
  seconds(store, tv, 60);
  std::shared_ptr<const EventsBoard> b = store.dayBoard();
  REQUIRE(b);
  CHECK(b->failed);
  CHECK_FALSE(b->loading);
  CHECK(b->rows.empty());
  CHECK(b->failedLine == ed::failedLine(twoBack, tv.zone));
  // Try again: getting it again.
  store.showEventsDay(twoBack, true);
  REQUIRE(store.dayBoard());
  CHECK(store.dayBoard()->loading);
  store.eventsOpen(false);
}

TEST_CASE("by day on the TV: a day ahead is the week's games already in, nothing asked") {
  fake::Tv tv;
  tv.pool.answer(fake::kPlaylist, playlist());
  savedToday(tv);
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  store.eventsOpen(true);
  seconds(store, tv, 3);
  const time::Seconds tomorrow = ed::nextDay(ed::dayOf(time::now(), tv.zone), tv.zone);
  DayUrls urls = dayUrls(tv, tomorrow);
  store.showEventsDay(tomorrow);
  seconds(store, tv, 5);
  CHECK(asked(tv, urls.urls) == 0);
  REQUIRE(store.dayBoard());
  CHECK_FALSE(store.dayBoard()->past);
  CHECK_FALSE(store.dayBoard()->loading);
  // Today's own: back to today's board.
  store.showEventsDay(ed::dayOf(time::now(), tv.zone));
  CHECK(store.eventsDay() == 0);
  store.eventsOpen(false);
}
