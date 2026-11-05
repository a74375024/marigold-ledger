// The day's schedule in the background (Build 125): on a TV that used Live
// Events lately, the schedule saved yesterday is fetched again shortly after
// launch (once the channels are in), with Live Events closed, so opening it
// finds today's games already in. Never with the schedule off, never before
// Live Events was used, never while the TV is busy (a channel starting, a
// key just pressed). Build 141: within seconds of the channels, and while
// a channel plays full screen too (tests/engine/test_events_ready.cpp).
#include <doctest.h>

#include <string>

#include "core/engine/store.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/schedule.hpp"
#include "engine/loaded_store.hpp"

using marigold::engine::EventsBoard;
using marigold::engine::Settings;
using marigold::engine::Store;
namespace schedule = marigold::sports::schedule;
namespace sports = marigold::sports;
namespace mtime = marigold::time;

namespace {

std::string playlist() {
  std::string m = "#EXTM3U\n";
  int n = 0;
  auto add = [&](const std::string& group, const std::string& name) {
    m += "#EXTINF:-1 group-title=\"" + group + "\"," + name + "\n";
    m += "http://mock.example.invalid:8080/" + std::to_string(++n) + ".ts\n";
  };
  for (const char* net : {"US: ESPN", "US: FOX", "US: CBS", "US: NBC"}) add("US| SPORTS", net);
  add("US| ESPN+ PPV", "ESPN+ 01: Harbor City Owls vs Valley Hawks");
  return m;
}

// Yesterday's schedule (Live Events used then).
void savedYesterday(fake::Tv& tv) {
  schedule::Saved s;
  s.fetchedAt = mtime::now() - 86'400;
  s.schema = schedule::kSchema;
  tv.files.write("sports-schedule.json", schedule::toJson(s), false);
}

// Every scoreboard request of the standard leagues today answers, empty.
void answerScoreboards(fake::Tv& tv) {
  mtime::Seconds now = mtime::now();
  for (const sports::League& l : sports::activeLeagues(sports::MySportsPrefs(), sports::SportFeedsSeen(), now, {})) {
    for (const std::string& q : schedule::queriesFor(l, now, tv.zone)) {
      tv.pool.answer(schedule::scoreboardRequest(l, q, false).url, R"({"events":[]})");
    }
  }
}

int scoreboardAsked(const fake::Tv& tv) {
  int n = 0;
  for (const auto& [url, count] : tv.pool.asked)
    if (url.find("site.api.espn.com") != std::string::npos) n += count;
  return n;
}

void seconds(Store& store, fake::Tv& tv, int s) {
  for (int i = 0; i < s * 10; ++i) {
    tv.pool.drain();
    tv.ms += 100;
    store.tick(tv.ms);
  }
  tv.pool.drain();
}

}  // namespace

TEST_CASE("engine: the day's schedule comes in the background after launch; Live Events then doesn't wait") {
  fake::Tv tv;
  tv.pool.answer(fake::kPlaylist, playlist());
  savedYesterday(tv);
  answerScoreboards(tv);
  Store store(tv.host());
  // Not before the channels are in.
  CHECK(scoreboardAsked(tv) == 0);
  REQUIRE(fake::addPlaylist(store, tv).empty());
  // Build 141: then within seconds (was 20 s).
  CHECK(scoreboardAsked(tv) > 0);
  seconds(store, tv, 90);
  CHECK(scoreboardAsked(tv) > 10);
  // Saved: fetched today.
  auto text = tv.files.read("sports-schedule.json");
  REQUIRE(text);
  auto saved = schedule::savedFromJson(*text);
  REQUIRE(saved);
  REQUIRE(saved->fetchedAt);
  CHECK(mtime::now() - *saved->fetchedAt < 600);
  CHECK_FALSE(saved->restPending);
  // Opened now: nothing to fetch, never "Getting this week's games".
  int before = scoreboardAsked(tv);
  store.eventsOpen(true);
  for (int i = 0; i < 300 && (store.events()->loading || store.events()->more); ++i) {
    CHECK(store.events()->stage != EventsBoard::Stage::Schedule);
    tv.pool.drain();
    tv.ms += 100;
    store.tick(tv.ms);
  }
  CHECK(scoreboardAsked(tv) == before);
  store.eventsOpen(false);
}

TEST_CASE("engine: no background schedule when it's off, before Live Events was used, or while the TV is busy; "
          "watching full screen doesn't stop it") {
  SUBCASE("off") {
    fake::Tv tv;
    tv.pool.answer(fake::kPlaylist, playlist());
    savedYesterday(tv);
    Store store(tv.host());
    Settings s = store.settings();
    s.scheduleEnabled = false;
    store.setSettings(s);
    REQUIRE(fake::addPlaylist(store, tv).empty());
    seconds(store, tv, 90);
    CHECK(scoreboardAsked(tv) == 0);
  }
  SUBCASE("never used") {
    fake::Tv tv;
    tv.pool.answer(fake::kPlaylist, playlist());
    Store store(tv.host());
    REQUIRE(fake::addPlaylist(store, tv).empty());
    seconds(store, tv, 90);
    CHECK(scoreboardAsked(tv) == 0);
  }
  SUBCASE("busy") {
    fake::Tv tv;
    tv.pool.answer(fake::kPlaylist, playlist());
    savedYesterday(tv);
    bool busy = true;
    marigold::engine::Host host = tv.host();
    host.idle = [&busy] { return !busy; };
    Store store(host);
    REQUIRE(fake::addPlaylist(store, tv).empty());
    seconds(store, tv, 90);
    CHECK(scoreboardAsked(tv) == 0);
    // The channel started: it goes on.
    busy = false;
    seconds(store, tv, 10);
    CHECK(scoreboardAsked(tv) > 0);
  }
  SUBCASE("watching full screen") {
    fake::Tv tv;
    tv.pool.answer(fake::kPlaylist, playlist());
    savedYesterday(tv);
    bool full = true;
    marigold::engine::Host host = tv.host();
    host.playingFull = [&full] { return full; };
    Store store(host);
    // The outside listings wait for real while watching; not this test's.
    Settings s = store.settings();
    s.fillMissingListings = false;
    store.setSettings(s);
    REQUIRE(fake::addPlaylist(store, tv).empty());
    // Build 141: fetched while watching (was: only once back in the guide).
    seconds(store, tv, 90);
    CHECK(scoreboardAsked(tv) > 0);
  }
}
