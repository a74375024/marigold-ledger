// Build 136. The channel list is read again every half hour, Live Events
// open or not, and a provider renames its event slots in place ("NO EVENT
// STREAMING NOW" becomes "NEXT | Team A - Team B | ... | 8K EXCLUSIVE");
// Live Events matches again only when it's open. A report made meanwhile
// says a slot is new or renamed since the last match instead of "no tile
// took it", and opening Live Events matches it. And with Live Events open,
// the guide's listings read again every 10 minutes are matched again only
// when they changed (or anything else did).
#include <doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/engine/store.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/store.hpp"
#include "core/util/strings.hpp"
#include "core/util/time.hpp"
#include "engine/loaded_store.hpp"

using marigold::engine::Store;
namespace mtime = marigold::time;
namespace ss = marigold::sports::store;
namespace str = marigold::str;

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

// A few networks and a provider's PPV group of event slots, some of them
// empty for now (every name made up).
std::string playlist(const std::vector<std::string>& slots) {
  std::string m = "#EXTM3U\n";
  int n = 0;
  auto add = [&](const std::string& group, const std::string& name) {
    m += "#EXTINF:-1 group-title=\"" + group + "\"," + name + "\n";
    m += "http://mock.example.invalid:8080/" + std::to_string(++n) + ".ts\n";
  };
  for (const char* net : {"US: ESPN", "US: FOX", "US: CBS"}) add("US| SPORTS", net);
  for (const std::string& slot : slots) add("US| VIAPLAY PPV", slot);
  return m;
}

std::string slot(int n, const std::string& what) {
  return what + " | 8K EXCLUSIVE | US: VIAPLAY PPV " + std::to_string(n);
}

std::string report(Store& store, fake::Tv& tv) {
  std::shared_ptr<std::string> csv;
  store.buildLiveEventsReport([&](std::shared_ptr<std::string> made) { csv = std::move(made); });
  tv.pool.drain();
  return csv ? *csv : std::string();
}

std::string lineWith(const std::string& csv, const std::string& needle) {
  for (std::string_view line : str::split(csv, '\n'))
    if (line.find(needle) != std::string_view::npos) return std::string(line);
  return {};
}

bool logSays(const std::vector<std::string>& lines, const std::string& text) {
  for (const std::string& line : lines)
    if (line.find(text) != std::string::npos) return true;
  return false;
}

}  // namespace

TEST_CASE("engine: slots renamed while Live Events is closed: the report says so, opening matches them") {
  fake::Tv tv;
  std::vector<std::string> slots = {slot(1, "LIVE | Harbor City Owls - Valley Hawks"),
                                    slot(2, "NO EVENT STREAMING NOW -"), slot(3, "NO EVENT STREAMING NOW -")};
  tv.pool.answer(fake::kPlaylist, playlist(slots));
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  store.eventsOpen(true);
  for (int i = 0; i < 4; ++i) fake::settle(store, tv);
  store.eventsOpen(false);
  fake::settle(store, tv);
  std::string csv = report(store, tv);
  REQUIRE(!lineWith(csv, "Harbor City Owls").empty());
  CHECK(lineWith(csv, "Harbor City Owls").find("since Live Events last matched") == std::string::npos);

  // The half-hourly read of the list, Live Events closed: two slots now
  // carry games, one the game another slot had.
  slots[1] = slot(2, "NEXT | Lakeside Pines - Northport Gulls");
  slots[2] = slot(3, "NEXT | Ridge Rams - Bayview Otters");
  slots[0] = slot(1, "NEXT | Summit Elk - Canyon Foxes");
  tv.pool.answer(fake::kPlaylist, playlist(slots));
  store.refreshAll();
  fake::settle(store, tv);
  csv = report(store, tv);
  for (const char* game : {"Lakeside Pines", "Ridge Rams", "Summit Elk"}) {
    std::string line = lineWith(csv, game);
    INFO(line);
    REQUIRE(!line.empty());
    CHECK(line.find("\"left out\",\"new or renamed since Live Events last matched (it matches again when opened)\"") !=
          std::string::npos);
    CHECK(line.find("no tile took it") == std::string::npos);
    CHECK(str::endsWith(line, ",\"not matched yet\""));
  }

  // Opened: matched, and the report says what became of them.
  store.eventsOpen(true);
  for (int i = 0; i < 4; ++i) fake::settle(store, tv);
  csv = report(store, tv);
  for (const char* game : {"Lakeside Pines", "Ridge Rams", "Summit Elk"}) {
    std::string line = lineWith(csv, game);
    INFO(line);
    CHECK(line.find("since Live Events last matched") == std::string::npos);
    CHECK(line.find("no tile took it") == std::string::npos);
    CHECK(line.find("given no reason by matching") == std::string::npos);
  }
  store.eventsOpen(false);
}

TEST_CASE("engine: Live Events left open: listings read again and the same are not matched again") {
  ClockAhead clock;
  // Just after a half hour: the 10 minutes below stay within it (the
  // fingerprint's own clock), so only the listings' read could rebuild.
  clock.move(1800 - mtime::now() % 1800 + 5);
  fake::Tv tv;
  std::vector<std::string> slots;
  for (int i = 1; i <= 6; ++i) slots.push_back(slot(i, "LIVE | Team " + std::to_string(i) + "A - Team " +
                                                              std::to_string(i) + "B"));
  tv.pool.answer(fake::kPlaylist, playlist(slots));
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  store.eventsOpen(true);
  for (int i = 0; i < 4; ++i) fake::settle(store, tv);
  store.takeEventsLog();
  // Eleven minutes with Live Events on screen, ticked through (not a sleep).
  for (int s = 0; s < 11 * 60; s += 5) {
    clock.move(5);
    tv.pool.drain();
    tv.ms += 5000;
    store.tick(tv.ms);
  }
  fake::settle(store, tv);
  std::vector<std::string> log = store.takeEventsLog();
  INFO(str::join(log, "\n"));
  CHECK(logSays(log, "guide listings read again: the same, nothing new to match"));
  CHECK_FALSE(logSays(log, "rebuilt in"));
  store.eventsOpen(false);
}

TEST_CASE("sports: the listings' digest: what every read in the half hour has") {
  using marigold::sports::matcher::GuideListing;
  const mtime::Seconds half = 1793397600;  // a half hour's start
  const mtime::Seconds now = half + 600;
  auto listing = [](const std::string& id, const std::string& title, mtime::Seconds start, mtime::Seconds end) {
    GuideListing l;
    l.entryID = id;
    l.title = title;
    l.start = start;
    l.end = end;
    return l;
  };
  std::vector<GuideListing> a = {listing("c1", "Team A vs Team B", now - 3600, now + 3600),
                                 listing("c2", "Team C vs Team D", now + 86400, now + 86400 + 7200)};
  const uint64_t base = ss::listingsDigest(a, now);
  CHECK(base == ss::listingsDigest(a, now + 900));  // later in the same half hour
  // A listing in the middle changed, or another one: a new digest.
  std::vector<GuideListing> b = a;
  b[1].title = "Team C vs Team E";
  CHECK(ss::listingsDigest(b, now) != base);
  b = a;
  b.push_back(listing("c3", "Team F vs Team G", now + 7200, now + 9000));
  CHECK(ss::listingsDigest(b, now) != base);
  b = a;
  b[0].venue = std::string("harbor arena");
  CHECK(ss::listingsDigest(b, now) != base);
  // At the window's ends (gone or come by the half hour's end): the same.
  b = a;
  b.push_back(listing("c4", "Team H vs Team I", half - 7 * 3600, half + 1800 - ss::kListingsBefore));
  b.push_back(listing("c5", "Team J vs Team K", half + ss::kListingsAhead, half + ss::kListingsAhead + 3600));
  CHECK(ss::listingsDigest(b, now) == base);
  // Another half hour: another window.
  CHECK(ss::listingsDigest(b, now + 1800) != ss::listingsDigest(a, now + 1800));
}
