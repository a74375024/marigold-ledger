// Build 177, Skip break in the store: a game on the board whose replay
// plays (Store::wantBreaks) has its play-by-play fetched in the background
// and its breaks kept (Store::breaks); nothing is asked while no replay
// asks; a game with no play-by-play to be had (an error, a body without
// times) has none, said once in the log. Made-up teams and a pretend
// network.
#include <doctest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/engine/store.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/plays.hpp"
#include "core/sports/schedule.hpp"
#include "core/sports/scores.hpp"
#include "core/sports/text.hpp"
#include "engine/loaded_store.hpp"
#include "sports/helpers.hpp"

using marigold::engine::Store;
namespace sports = marigold::sports;
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

std::string iso(mtime::Seconds t) {
  mtime::Civil c = mtime::civilFromSeconds(t);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", c.year, c.month, c.day, c.hour, c.minute, c.second);
  return buf;
}

// A football game's plays by drive: a timeout, then 2 min 10 s to the next
// play; a touchdown, then 2 min 40 s to the kickoff.
std::string footballPlays(mtime::Seconds kick) {
  struct P {
    int at;
    const char* type;
    bool scoring;
  };
  std::vector<P> plays = {{0, "Kickoff", false},         {40, "Rush", false},
                          {80, "Timeout", false},        {210, "Pass Reception", false},
                          {250, "Passing Touchdown", true}, {410, "Kickoff", false},
                          {450, "Rush", false}};
  std::string out = R"({"drives":{"previous":[{"id":"1","plays":[)";
  for (size_t i = 0; i < plays.size(); ++i) {
    if (i) out += ",";
    out += R"({"id":")" + std::to_string(i + 1) + R"(","type":{"text":")" + plays[i].type +
           R"("},"text":"play","period":{"number":1},"scoringPlay":)" + (plays[i].scoring ? "true" : "false") +
           R"(,"wallclock":")" + iso(kick + plays[i].at) + R"("})";
  }
  out += "]}]}}";
  return out;
}

std::string playlist() {
  return "#EXTM3U\n#EXTINF:-1 group-title=\"US| SPORTS\",US: FOX\nhttp://mock.example.invalid:8080/1.ts\n";
}

struct Sunday {
  fake::Tv tv;
  ClockAhead clock;
  mtime::Seconds kick = 0;
  std::string summaryUrl;

  Sunday() {
    mtime::Seconds now = mtime::now();
    kick = now - 1200;
    sports::schedule::Saved saved;
    saved.fetchedAt = now;
    saved.schema = sports::schedule::kSchema;
    const sports::League* nfl = sports::leagueByShort("NFL");
    REQUIRE(nfl);
    saved.games.push_back(sportsdata::game("NFL", nfl->group, "401", sportsdata::team("h401", "Harbor Gulls"),
                                           sportsdata::team("a401", "Valley Owls"), kick, {"FOX"}));
    tv.files.write("sports-schedule.json", sports::schedule::toJson(saved), false);
    tv.pool.answer(fake::kPlaylist, playlist());
    summaryUrl = sports::plays::summaryRequest(*nfl, "401").url;
  }
  int asked() const {
    auto it = tv.pool.asked.find(summaryUrl);
    return it == tv.pool.asked.end() ? 0 : it->second;
  }
  // `s` seconds on, the replay of `id` asking every second (none: "").
  void run(Store& store, int s, const std::string& id) {
    for (int i = 0; i < s; ++i) {
      clock.move(1);
      if (!id.empty()) store.wantBreaks(id);
      for (int k = 0; k < 10; ++k) {
        tv.ms += 100;
        store.tick(tv.ms);
        tv.pool.drain();
      }
    }
  }
  std::string board(Store& store) {
    REQUIRE(fake::addPlaylist(store, tv).empty());
    store.eventsOpen(true);
    run(store, 20, "");
    store.eventsOpen(false);
    std::string id = store.eventOfGame("NFL|401");
    REQUIRE_FALSE(id.empty());
    return id;
  }
};

bool logged(Store& store, const std::string& line) {
  for (const std::string& l : store.takeEventsLog())
    if (l == line) return true;
  return false;
}

}  // namespace

TEST_CASE("engine 177: a game's replay fetches its play-by-play and keeps its breaks") {
  Sunday s;
  s.tv.pool.answer(s.summaryUrl, footballPlays(s.kick));
  Store store(s.tv.host());
  std::string id = s.board(store);
  // Nothing asked while no replay plays.
  CHECK(s.asked() == 0);
  CHECK_FALSE(static_cast<bool>(store.breaks(id)));
  // The replay asks: one request, the breaks in.
  s.run(store, 3, id);
  CHECK(s.asked() == 1);
  auto b = store.breaks(id);
  REQUIRE(static_cast<bool>(b));
  CHECK(b->league == "NFL");
  REQUIRE(b->list.size() >= 2);
  CHECK(b->list[0].from == s.kick + 90);
  CHECK(b->list[0].to == s.kick + 200);
  CHECK(b->list[1].from == s.kick + 260);
  CHECK(b->list[1].to == s.kick + 400);
  CHECK(logged(store, "breaks: " + std::to_string(b->list.size()) + " found in NFL play-by-play"));
  // A game still on: again after 2.5 min of its replay, not before.
  s.run(store, 140, id);
  CHECK(s.asked() == 1);
  s.run(store, 15, id);
  CHECK(s.asked() == 2);
  // Said once a session.
  CHECK_FALSE(logged(store, "breaks: " + std::to_string(b->list.size()) + " found in NFL play-by-play"));
  // The replay ends: nothing more.
  s.run(store, 600, "");
  CHECK(s.asked() == 2);
}

TEST_CASE("engine 177: no play-by-play, no breaks") {
  Sunday s;
  s.tv.pool.answer(s.summaryUrl, "{\"error\":\"not found\"}", 404);
  Store store(s.tv.host());
  std::string id = s.board(store);
  s.run(store, 5, id);
  CHECK(s.asked() == 1);
  CHECK_FALSE(static_cast<bool>(store.breaks(id)));
  CHECK(logged(store, "breaks: no NFL play-by-play for this game (HTTP 404)"));
  // Backed off: not again within two minutes.
  s.run(store, 100, id);
  CHECK(s.asked() == 1);
  // A body without times: an empty list, said once.
  s.tv.pool.answer(s.summaryUrl, R"({"plays":[{"id":"1","type":{"text":"Timeout"}},{"id":"2","type":{"text":"Rush"}}]})");
  s.run(store, 30, id);
  CHECK(s.asked() == 2);
  REQUIRE(static_cast<bool>(store.breaks(id)));
  CHECK(store.breaks(id)->list.empty());
  CHECK(logged(store, "breaks: NFL play-by-play has no times"));
  // An unknown game: nothing asked.
  store.wantBreaks("no such game");
  s.run(store, 200, "");
  CHECK(s.asked() == 2);
}
