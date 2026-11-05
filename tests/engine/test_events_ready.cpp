// Build 141: Live Events ready when opened. From launch, in the background
// and while a channel plays full screen too, the day's schedule comes in,
// the games are matched and a board is made from them (and kept up while
// Live Events is closed), so opening it shows the board at once and then
// refreshes it in place. Opening never starts a separate look-up that
// leaves the day's schedule for a later open. Never with the schedule off,
// or on a TV that never used Live Events.
#include <doctest.h>

#include <string>

#include "core/engine/store.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/schedule.hpp"
#include "engine/loaded_store.hpp"

using marigold::engine::EventGroup;
using marigold::engine::EventRow;
using marigold::engine::EventsBoard;
using marigold::engine::Settings;
using marigold::engine::Store;
namespace schedule = marigold::sports::schedule;
namespace sports = marigold::sports;
namespace mtime = marigold::time;

namespace {

// now() moved on for one test, and back after it.
struct ClockAhead {
  mtime::Seconds by = 0;
  void move(mtime::Seconds s) {
    mtime::advanceForTests(s);
    by += s;
  }
  ~ClockAhead() { mtime::advanceForTests(-by); }
};

// A few networks, game slots with no time in their titles, and a tennis
// match (a sport newly seen when the feeds are looked at).
std::string playlist() {
  const char* teams[] = {"Harbor City Owls", "Valley Hawks",   "Lakeside Pines", "Northport Gulls",
                         "Ridge Rams",       "Bayview Otters", "Summit Elk",     "Canyon Foxes"};
  std::string m = "#EXTM3U\n";
  int n = 0;
  auto add = [&](const std::string& group, const std::string& name) {
    m += "#EXTINF:-1 group-title=\"" + group + "\"," + name + "\n";
    m += "http://mock.example.invalid:8080/" + std::to_string(++n) + ".ts\n";
  };
  for (const char* net : {"US: ESPN", "US: ESPN2", "US: FOX", "US: CBS", "US: NBC"}) add("US| SPORTS", net);
  for (int i = 0; i < 24; ++i) {
    std::string slot = (i < 9 ? "0" : "") + std::to_string(i + 1);
    add("US| ESPN+ PPV", std::string("ESPN+ ") + slot + ": " + teams[i % 8] + " vs " + teams[(i + 1 + i / 8) % 8]);
  }
  add("US| TENNIS", "Tennis 01: A. Moreau vs B. Lindqvist");
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

int espnAsked(const fake::Tv& tv) {
  int n = 0;
  for (const auto& [url, count] : tv.pool.asked)
    if (url.find("site.api.espn.com") != std::string::npos) n += count;
  return n;
}

std::optional<mtime::Seconds> savedFetchedAt(const fake::Tv& tv) {
  auto text = tv.files.read("sports-schedule.json");
  if (!text) return std::nullopt;
  auto saved = schedule::savedFromJson(*text);
  if (!saved) return std::nullopt;
  return saved->fetchedAt;
}

void seconds(Store& store, fake::Tv& tv, int s) {
  for (int i = 0; i < s * 10; ++i) {
    tv.pool.drain();
    tv.ms += 100;
    store.tick(tv.ms);
  }
  tv.pool.drain();
}

// The guide's outside listings wait (for real, up to 5 minutes) while a
// channel plays full screen; not part of these tests.
void noOutsideListings(Store& store) {
  Settings s = store.settings();
  s.fillMissingListings = false;
  store.setSettings(s);
}

size_t tiles(const EventsBoard& board) {
  size_t n = 0;
  for (const EventRow& r : board.rows)
    for (const EventGroup& g : r.groups) n += g.tiles.size();
  return n;
}

}  // namespace

TEST_CASE("events ready: opened while the day's schedule is due, the day's fetch is the one that runs") {
  // Nothing in the background (a key pressed the whole time): the open
  // fetches. A sport newly seen among the feeds (tennis) is looked up too,
  // after the day's fetch, not in its place.
  fake::Tv tv;
  tv.pool.answer(fake::kPlaylist, playlist());
  savedYesterday(tv);
  answerScoreboards(tv);
  marigold::engine::Host host = tv.host();
  host.idle = [] { return false; };
  Store store(host);
  REQUIRE(fake::addPlaylist(store, tv).empty());
  REQUIRE(espnAsked(tv) == 0);
  store.eventsOpen(true);
  seconds(store, tv, 120);
  store.eventsOpen(false);
  // The day's schedule is in: fetched today, so the next open doesn't wait
  // for it.
  auto at = savedFetchedAt(tv);
  REQUIRE(at);
  CHECK(mtime::now() - *at < 600);
}

TEST_CASE("events ready: launched straight into a channel, the board is ready when Live Events opens") {
  fake::Tv tv;
  tv.pool.answer(fake::kPlaylist, playlist());
  savedYesterday(tv);
  answerScoreboards(tv);
  // A channel full screen from launch on (the last channel plays at launch).
  marigold::engine::Host host = tv.host();
  host.playingFull = [] { return true; };
  Store store(host);
  noOutsideListings(store);
  REQUIRE(fake::addPlaylist(store, tv).empty());
  // The day's fetch begins within seconds of the channels, while watching.
  seconds(store, tv, 10);
  CHECK(espnAsked(tv) > 0);
  seconds(store, tv, 80);
  auto at = savedFetchedAt(tv);
  REQUIRE(at);
  CHECK(mtime::now() - *at < 600);
  // Opened: the board at once (no placeholders, nothing fetched for it).
  int before = espnAsked(tv);
  store.takeEventsLog();
  store.eventsOpen(true);
  auto opened = store.events();
  CHECK_FALSE(opened->loading);
  CHECK(tiles(*opened) > 10);
  bool kept = false;
  for (const std::string& line : store.takeEventsLog())
    if (line.rfind("opened; the board is from", 0) == 0 && line.find("placeholders") == std::string::npos) kept = true;
  CHECK(kept);
  for (int i = 0; i < 100; ++i) {
    tv.pool.drain();
    tv.ms += 100;
    store.tick(tv.ms);
    CHECK_FALSE(store.events()->loading);
    CHECK(store.events()->stage != EventsBoard::Stage::Schedule);
  }
  CHECK(tiles(*store.events()) == tiles(*opened));
  CHECK(espnAsked(tv) == before);
  store.eventsOpen(false);
}

TEST_CASE("events ready: closed for a while, the board kept up shows at once on opening") {
  ClockAhead clock;
  fake::Tv tv;
  tv.pool.answer(fake::kPlaylist, playlist());
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  store.eventsOpen(true);
  for (int i = 0; i < 60 && (store.events()->loading || store.events()->more); ++i) fake::settle(store, tv);
  size_t whole = tiles(*store.events());
  REQUIRE(whole > 10);
  store.eventsOpen(false);
  // Twenty minutes watching something else (past the 15 minutes a board
  // was kept for), ticked through.
  for (int s = 0; s < 20 * 60; s += 5) {
    clock.move(5);
    tv.pool.drain();
    tv.ms += 5000;
    store.tick(tv.ms);
  }
  tv.pool.drain();
  store.eventsOpen(true);
  CHECK_FALSE(store.events()->loading);
  CHECK(tiles(*store.events()) == whole);
  fake::settle(store, tv);
  CHECK_FALSE(store.events()->loading);
  CHECK(tiles(*store.events()) == whole);
  store.eventsOpen(false);
}

TEST_CASE("events ready: nothing in the background with the schedule off or on a TV that never used Live Events") {
  SUBCASE("never used") {
    fake::Tv tv;
    tv.pool.answer(fake::kPlaylist, playlist());
    marigold::engine::Host host = tv.host();
    host.playingFull = [] { return true; };
    Store store(host);
    noOutsideListings(store);
    REQUIRE(fake::addPlaylist(store, tv).empty());
    seconds(store, tv, 90);
    CHECK(espnAsked(tv) == 0);
  }
  SUBCASE("schedule off") {
    fake::Tv tv;
    tv.pool.answer(fake::kPlaylist, playlist());
    savedYesterday(tv);
    Store store(tv.host());
    Settings s = store.settings();
    s.scheduleEnabled = false;
    store.setSettings(s);
    REQUIRE(fake::addPlaylist(store, tv).empty());
    seconds(store, tv, 90);
    CHECK(espnAsked(tv) == 0);
  }
}
