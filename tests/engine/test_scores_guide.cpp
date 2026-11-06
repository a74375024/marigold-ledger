// Build 145: the in-player event guide keeps the live scores at full pace
// over a playing channel (Store::eventsOpen with EventsScreen::guide holds
// sports::scores::Holder::guide); with no guide open a channel playing
// keeps the background's pace (one round at most every kBackgroundEvery),
// and closing the guide goes back to it. The board and Search open as
// before. Two games on now (made-up teams), every scoreboard answered live.
#include <doctest.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/engine/store.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/schedule.hpp"
#include "core/sports/scores.hpp"
#include "core/sports/text.hpp"
#include "engine/loaded_store.hpp"
#include "sports/helpers.hpp"

using marigold::engine::Store;
namespace sports = marigold::sports;
namespace scores = marigold::sports::scores;
namespace mtime = marigold::time;

namespace {

struct ClockAhead {
  mtime::Seconds by = 0;
  void move(mtime::Seconds s) {
    mtime::advanceForTests(s);
    by += s;
  }
  ~ClockAhead() { mtime::advanceForTests(-by); }
};

std::string playlist() {
  std::string m = "#EXTM3U\n";
  int n = 0;
  for (const char* net : {"US: ESPN", "US: FOX", "US: CBS", "US: NBC", "US: TNT"}) {
    m += "#EXTINF:-1 group-title=\"US| SPORTS\"," + std::string(net) + "\n";
    m += "http://mock.example.invalid:8080/" + std::to_string(++n) + ".ts\n";
  }
  return m;
}

std::string live(const std::string& id) {
  return R"({"events":[{"id":")" + id +
         R"(","competitions":[{"status":{"period":2,"displayClock":"9:00","type":{"name":"STATUS_IN_PROGRESS","state":"in"}},
          "competitors":[{"homeAway":"home","score":"0"},{"homeAway":"away","score":"7"}]}]}]})";
}

struct Afternoon {
  fake::Tv tv;
  bool playing = false;
  std::map<std::string, std::string> urls;  // league -> its scoreboard today
  std::string nflUrl, nhlUrl;
  ClockAhead clock;

  // A game on now in each league, on these networks.
  explicit Afternoon(const std::vector<std::pair<std::string, std::string>>& leagues = {{"NFL", "FOX"},
                                                                                          {"NHL", "TNT"}}) {
    mtime::Seconds now = mtime::now();
    sports::schedule::Saved saved;
    saved.fetchedAt = now;
    saved.schema = sports::schedule::kSchema;
    std::string day = sports::text::yearMonthDay(now - 600, mtime::Zone::usEastern());
    int n = 0;
    for (const auto& [league, network] : leagues) {
      const sports::League* l = sports::leagueByShort(league);
      REQUIRE(l);
      std::string id = std::to_string(401 + n);
      saved.games.push_back(sportsdata::game(league, l->group, id, sportsdata::team("h" + id, "Harbor " + league),
                                             sportsdata::team("a" + id, "Valley " + league), now - 600, {network}));
      urls[league] = scores::scoreboardRequest(*l, day).url;
      tv.pool.answer(urls[league], live(id));
      ++n;
    }
    tv.files.write("sports-schedule.json", sports::schedule::toJson(saved), false);
    tv.pool.answer(fake::kPlaylist, playlist());
    nflUrl = urls["NFL"];
    nhlUrl = urls["NHL"];
  }
  marigold::engine::Host host() {
    marigold::engine::Host h = tv.host();
    h.playingFull = [this] { return playing; };
    return h;
  }
  int asked(const std::string& url) const {
    auto it = tv.pool.asked.find(url);
    return it == tv.pool.asked.end() ? 0 : it->second;
  }
  // The clock and the frame loop `s` seconds on.
  void run(Store& store, int s) {
    for (int i = 0; i < s; ++i) {
      clock.move(1);
      for (int k = 0; k < 10; ++k) {
        tv.ms += 100;
        store.tick(tv.ms);
        tv.pool.drain();
      }
    }
  }
};

}  // namespace

TEST_CASE("engine: the in-player event guide keeps the scores at full pace over a playing channel") {
  Afternoon a;
  Store store(a.host());
  // The channels in (the guide's read waits while a channel plays), then a
  // channel plays.
  REQUIRE(fake::addPlaylist(store, a.tv).empty());
  a.playing = true;
  // Watching, no guide: the background's pace (one round in ten minutes).
  a.run(store, 30);
  int start = a.asked(a.nflUrl);
  CHECK(start >= 1);
  a.run(store, 9 * 60);
  CHECK(a.asked(a.nflUrl) - start <= 1);

  // The guide opens over the channel: it looks now, then once a minute.
  start = a.asked(a.nflUrl);
  store.eventsOpen(true, Store::EventsScreen::guide);
  a.run(store, 10);
  CHECK(a.asked(a.nflUrl) == start + 1);
  a.run(store, 5 * 60);
  CHECK(a.asked(a.nflUrl) - start >= 5);
  CHECK(a.asked(a.nflUrl) - start <= 7);

  // Closed: the background's pace again.
  store.eventsOpen(false, Store::EventsScreen::guide);
  start = a.asked(a.nflUrl);
  a.run(store, 9 * 60);
  CHECK(a.asked(a.nflUrl) - start <= 1);

  // The board (or Search) over a playing channel: as before, the
  // background's pace; with nothing playing, the board's own.
  store.eventsOpen(true);
  start = a.asked(a.nflUrl);
  a.run(store, 5 * 60);
  CHECK(a.asked(a.nflUrl) - start <= 1);
  a.playing = false;
  a.run(store, 11 * 60);  // the background's wait over
  start = a.asked(a.nflUrl);
  a.run(store, 5 * 60);
  CHECK(a.asked(a.nflUrl) - start >= 4);
  store.eventsOpen(false);
}

TEST_CASE("engine: the sport open on the board gets its scores first") {
  Afternoon a({{"NFL", "FOX"}, {"NHL", "TNT"}, {"NBA", "ESPN"}, {"MLB", "FOX"}, {"MLS", "FOX"}, {"EPL", "NBC"}});
  Store store(a.host());
  REQUIRE(fake::addPlaylist(store, a.tv).empty());
  store.eventsOpen(true);
  a.run(store, 30);
  // The rows of the leagues' games.
  std::set<std::string> rows;
  for (const auto& r : store.events()->rows) rows.insert(r.id);
  REQUIRE(rows.count("sport|NHL"));
  // Nothing looked at: six leagues share the pace (60 s x 3).
  std::map<std::string, int> start;
  for (const auto& [league, url] : a.urls) start[league] = a.asked(url);
  a.run(store, 9 * 60);
  for (const auto& [league, url] : a.urls) {
    const std::string& name = league;  // Apple clang: no structured binding in INFO's lambda
    INFO(name);
    CHECK(a.asked(url) - start[league] >= 2);
    CHECK(a.asked(url) - start[league] <= 4);
  }
  // Hockey's row focused: NHL once a minute, the five others spread over
  // five (60 s x 2.5).
  store.eventsFocus(Store::EventsScreen::board, "sport|NHL");
  for (const auto& [league, url] : a.urls) start[league] = a.asked(url);
  a.run(store, 10 * 60);
  CHECK(a.asked(a.urls["NHL"]) - start["NHL"] >= 9);
  for (const auto& [league, url] : a.urls) {
    if (league == "NHL") continue;
    CHECK(a.asked(url) - start[league] >= 3);
    CHECK(a.asked(url) - start[league] <= 5);
  }
  // A row of many leagues (Live now) isn't one sport looked at.
  store.eventsFocus(Store::EventsScreen::board, "live");
  a.run(store, 5 * 60);
  for (const auto& [league, url] : a.urls) start[league] = a.asked(url);
  a.run(store, 9 * 60);
  for (const auto& [league, url] : a.urls) {
    const std::string& name = league;  // Apple clang: no structured binding in INFO's lambda
    INFO(name);
    CHECK(a.asked(url) - start[league] >= 2);
    CHECK(a.asked(url) - start[league] <= 4);
  }
  store.eventsOpen(false);
}

// 1.0.1: Live Events left on screen untouched (no key, pointer, wheel or
// swipe) for 5 minutes asks for scores every 20 minutes, silently; the first
// touch brings a round within a moment and the usual pace back.
TEST_CASE("engine: an untouched Live Events slows to a round every 20 minutes until touched") {
  Afternoon a;
  uint32_t touched = 0;
  marigold::engine::Host h = a.host();
  h.lastTouchMs = [&touched] { return touched; };
  Store store(h);
  REQUIRE(fake::addPlaylist(store, a.tv).empty());
  touched = a.tv.ms;
  store.eventsOpen(true);
  // Touched: the board's own pace (two leagues, once a minute).
  a.run(store, 30);
  int start = a.asked(a.nflUrl);
  a.run(store, 4 * 60);
  CHECK(a.asked(a.nflUrl) - start >= 3);
  // Left alone past 5 minutes: at most one round in the next 19 minutes,
  // and a round again within 20.
  a.run(store, 2 * 60);
  start = a.asked(a.nflUrl);
  a.run(store, 19 * 60);
  CHECK(a.asked(a.nflUrl) - start <= 1);
  a.run(store, 2 * 60);
  CHECK(a.asked(a.nflUrl) - start >= 1);
  CHECK(a.asked(a.nflUrl) - start <= 2);
  // A touch: asked within 3 s, then once a minute again.
  start = a.asked(a.nflUrl);
  touched = a.tv.ms;
  a.run(store, 3);
  CHECK(a.asked(a.nflUrl) == start + 1);
  a.run(store, 3 * 60);
  CHECK(a.asked(a.nflUrl) - start >= 3);
  // The in-player column over a channel, untouched: the same slow pace.
  a.playing = true;
  store.eventsOpen(false);
  store.eventsOpen(true, Store::EventsScreen::guide);
  touched = a.tv.ms;
  a.run(store, 6 * 60);
  start = a.asked(a.nflUrl);
  a.run(store, 19 * 60);
  CHECK(a.asked(a.nflUrl) - start <= 1);
  store.eventsOpen(false, Store::EventsScreen::guide);
}
