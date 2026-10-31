// Live Events made on a worker (Build 116): the board put in by the tick is
// the one made on the spot from the same inputs, boards asked for while one
// is being made become one more board (not one each), and the board's focus
// stays on its game when a new board comes in. A board kept from before
// shows only while new (15 minutes, the same day), made again from the
// clock; otherwise placeholders until fresh games are in.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/engine/events_build.hpp"
#include "core/engine/store.hpp"
#include "core/engine/views.hpp"
#include "core/sports/board.hpp"
#include "engine/loaded_store.hpp"
#include "sports/helpers.hpp"

using marigold::engine::EventGroup;
using marigold::engine::EventRow;
using marigold::engine::EventsBoard;
using marigold::engine::EventTile;
using marigold::engine::Settings;
using marigold::engine::Store;
namespace views = marigold::engine::views;
namespace events_build = marigold::engine::events_build;
namespace sb = marigold::sports::board;
namespace mtime = marigold::time;

namespace {

// Every field a screen draws, a line per row, group, tile and side.
std::string dump(const EventsBoard& board) {
  std::string out;
  auto add = [&out](const std::string& s) { out += s + "|"; };
  auto flag = [&out](bool b) { out += b ? "1|" : "0|"; };
  out += board.loading ? "loading\n" : "\n";
  out += board.more ? "more\n" : "\n";
  for (const EventRow& r : board.rows) {
    out += "ROW ";
    add(r.id);
    add(r.title);
    add(std::to_string(r.live) + "/" + std::to_string(r.count));
    flag(r.families);
    out += "\n";
    for (const EventGroup& g : r.groups) {
      out += " GROUP " + g.title + "\n";
      for (const EventTile& t : g.tiles) {
        out += "  ";
        for (const std::string* s :
             {&t.id, &t.pillText, &t.status, &t.statusExtra, &t.tag, &t.league, &t.sport, &t.competition, &t.title,
              &t.subtitle, &t.icon, &t.symbol, &t.channelName, &t.channelLogo, &t.pill2, &t.footer, &t.detail,
              &t.situation, &t.leagueChip, &t.heroTitle, &t.heroStatus, &t.whereLine, &t.topLine, &t.topLineMixed,
              &t.extraLine})
          add(*s);
        for (bool b : {t.statusBold, t.favorite, t.tagAlert, t.highSchool, t.soon, t.pill2Outlined, t.redZone,
                       t.watchable, t.finished, t.upcoming, t.textMade})
          flag(b);
        add(std::to_string(static_cast<int>(t.pill)) + "/" + std::to_string(t.progress) + "/" +
            std::to_string(t.start));
        for (const auto& s : t.sides) {
          for (const std::string* x : {&s.abbr, &s.name, &s.country, &s.logoUrl, &s.rank, &s.score, &s.pens})
            add(*x);
          add(std::to_string(s.color) + "/" + std::to_string(s.alt));
        }
        for (const auto& p : t.parts) add(p.label);
        for (const auto& p : t.podium) add(p.code);
        for (const auto& l : t.leaders) add(l.name + "/" + l.score);
        out += "\n";
      }
    }
  }
  return out;
}

size_t tiles(const EventsBoard& board) {
  size_t n = 0;
  for (const EventRow& r : board.rows)
    for (const EventGroup& g : r.groups) n += g.tiles.size();
  return n;
}

// A row of tiles by id, one group.
EventRow row(const std::string& id, const std::vector<std::string>& tileIds) {
  EventRow r;
  r.id = id;
  r.title = id;
  EventGroup g;
  for (const std::string& t : tileIds) {
    EventTile tile;
    tile.id = t;
    g.tiles.push_back(tile);
  }
  r.groups.push_back(g);
  return r;
}

// A playlist with a few networks and an event group of games with no time
// in their titles, so nothing on the board moves with the clock (every name
// made up).
std::string playlist() {
  const char* teams[] = {"Harbor City Owls", "Valley Hawks",    "Lakeside Pines", "Northport Gulls",
                         "Ridge Rams",       "Bayview Otters",  "Summit Elk",     "Canyon Foxes",
                         "Mill Town Bears",  "Riverbend Herons", "Old Port Seals", "Granite Wolves"};
  std::string m = "#EXTM3U\n";
  int n = 0;
  auto add = [&](const std::string& group, const std::string& name) {
    m += "#EXTINF:-1 group-title=\"" + group + "\"," + name + "\n";
    m += "http://mock.example.invalid:8080/" + std::to_string(++n) + ".ts\n";
  };
  for (const char* net : {"US: ESPN", "US: ESPN2", "US: FOX", "US: CBS", "US: NBC", "US: TNT"}) add("US| SPORTS", net);
  for (int i = 0; i < 48; ++i) {
    std::string slot = (i < 9 ? "0" : "") + std::to_string(i + 1);
    std::string a = teams[i % 12], b = teams[(i * 5 + 1 + i / 12) % 12];
    if (a == b) b = teams[(i + 6) % 12];
    add("US| ESPN+ PPV", "ESPN+ " + slot + ": " + a + " vs " + b);
  }
  return m;
}

void tick(Store& store, fake::Tv& tv) {
  tv.ms += 100;
  store.tick(tv.ms);
}

}  // namespace

TEST_CASE("engine: the board made on a worker is the one made on the spot from the same inputs") {
  fake::Tv tv;
  tv.pool.answer(fake::kPlaylist, playlist());
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  store.eventsOpen(true);
  fake::settle(store, tv);
  auto board = store.events();
  REQUIRE(tiles(*board) > 10);
  CHECK(dump(*board) == dump(*store.boardNow()));
  // The tiles' text is made with them.
  for (const EventRow& r : board->rows)
    for (const EventGroup& g : r.groups)
      for (const EventTile& t : g.tiles) CHECK(t.textMade);

  // Changes while a board is being made: the next one has them all.
  Settings s = store.settings();
  s.showScores = !s.showScores;
  store.setSettings(s);
  tick(store, tv);  // a board starts on a worker
  s.clock24 = !s.clock24;
  store.setSettings(s);
  tick(store, tv);
  s.showUnwatchable = !s.showUnwatchable;
  store.setSettings(s);
  tick(store, tv);
  fake::settle(store, tv);
  CHECK(store.events()->version > board->version);
  CHECK(store.events()->scoresOn == s.showScores);
  CHECK(dump(*store.events()) == dump(*store.boardNow()));
  store.eventsOpen(false);
}

TEST_CASE("engine: boards asked for while one is being made become one more board, not one each") {
  fake::Tv tv;
  tv.pool.answer(fake::kPlaylist, playlist());
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  store.eventsOpen(true);
  fake::settle(store, tv);
  uint64_t before = store.events()->version;

  // Three changes, a tick after each: the first starts a board, the other
  // two wait for it.
  Settings s = store.settings();
  for (int i = 0; i < 3; ++i) {
    s.showUnwatchable = !s.showUnwatchable;
    store.setSettings(s);
    tick(store, tv);
  }
  CHECK(store.events()->version == before);  // nothing put in until the worker is done
  tv.pool.drain();
  tick(store, tv);  // the first goes in; one more starts for the other two
  CHECK(store.events()->version == before + 1);
  tick(store, tv);
  CHECK(store.events()->version == before + 1);  // still being made: not another
  tv.pool.drain();
  tick(store, tv);
  CHECK(store.events()->version == before + 2);
  // Nothing more asked for: nothing more made.
  for (int i = 0; i < 20; ++i) {
    tv.pool.drain();
    tick(store, tv);
  }
  CHECK(store.events()->version == before + 2);
  CHECK(dump(*store.events()) == dump(*store.boardNow()));
  store.eventsOpen(false);
}

TEST_CASE("views: the board's focus stays on its game when a new board comes in") {
  std::vector<EventRow> before = {row("teams", {"a", "b"}), row("sport|NFL", {"c", "d", "e"})};
  views::BoardFocus f = views::boardFocus(before, "sport|NFL", "d", 1, 1);
  CHECK(f.row == 1);
  CHECK(f.tile == 1);

  // A row comes in above, a game before it: the same game in the same row.
  std::vector<EventRow> after = {row("live", {"x"}), row("teams", {"a", "b"}), row("sport|NFL", {"c", "z", "d", "e"})};
  f = views::boardFocus(after, "sport|NFL", "d", 1, 1);
  CHECK(f.row == 2);
  CHECK(f.tile == 2);

  // The game left its row: that row, at the same place.
  std::vector<EventRow> left = {row("live", {"x"}), row("teams", {"a", "b"}), row("sport|NFL", {"c", "e"})};
  f = views::boardFocus(left, "sport|NFL", "d", 1, 1);
  CHECK(f.row == 2);
  CHECK(f.tile == 1);
  f = views::boardFocus(left, "sport|NFL", "d", 1, 7);
  CHECK(f.tile == 1);  // kept inside the row

  // Its row is gone: the game where it is now.
  std::vector<EventRow> moved = {row("live", {"x", "d"}), row("teams", {"a", "b"})};
  f = views::boardFocus(moved, "sport|NFL", "d", 1, 1);
  CHECK(f.row == 0);
  CHECK(f.tile == 1);

  // Neither: the same place, kept inside the rows.
  std::vector<EventRow> other = {row("live", {"x"})};
  f = views::boardFocus(other, "sport|NFL", "d", 3, 4);
  CHECK(f.row == 0);
  CHECK(f.tile == 0);
  f = views::boardFocus({}, "sport|NFL", "d", 3, 4);
  CHECK(f.row == 0);
  CHECK(f.tile == 0);
}

TEST_CASE("engine: a board kept from before shows only while new and from the same day") {
  mtime::Zone zone = mtime::Zone::usEastern();
  mtime::Seconds now = sportsdata::utc(2026, 10, 31, 18, 0);  // 2:00 PM in New York
  CHECK(events_build::keptBoardShows(now - 60, now, zone));
  CHECK(events_build::keptBoardShows(now - 14 * 60 - 59, now, zone));
  CHECK_FALSE(events_build::keptBoardShows(now - 15 * 60, now, zone));
  CHECK_FALSE(events_build::keptBoardShows(now - 6 * 3600, now, zone));
  CHECK_FALSE(events_build::keptBoardShows(now - 3 * 86400, now, zone));
  CHECK_FALSE(events_build::keptBoardShows(0, now, zone));          // none
  CHECK_FALSE(events_build::keptBoardShows(now + 120, now, zone));  // the clock went back
  // Ten minutes old, but made before midnight here: another day.
  mtime::Seconds midnight = sportsdata::utc(2026, 11, 1, 4, 0);  // 12:00 AM in New York (EDT)
  CHECK_FALSE(events_build::keptBoardShows(midnight - 5 * 60, midnight + 5 * 60, zone));
  CHECK(events_build::keptBoardShows(midnight + 60, midnight + 5 * 60, zone));
}

TEST_CASE("sports: a board made again from the clock drops games that have ended and a LIVE it contradicts") {
  using marigold::sports::LivePhase;
  mtime::Seconds start = sportsdata::utc(2026, 10, 31, 17, 0);
  auto event = sportsdata::plainEvent("ev|harbor", "Harbor City Owls vs Valley Hawks", "Soccer", start,
                                      {sportsdata::feed("f1")});
  auto make = [&](mtime::Seconds now, std::optional<LivePhase> live) {
    sb::Input in;
    auto e = event;
    e.live = live;
    in.events = {e};
    in.now = now;
    in.zone = mtime::Zone::usEastern();
    in.channelFor = [](const marigold::sports::EventSource& src) -> std::optional<sb::ChannelLabel> {
      return sb::ChannelLabel{src.channelID, "Feed", "Feed", {}};
    };
    return sb::build(in);
  };
  // Half an hour in: live by the clock.
  sb::Board during = make(start + 1800, std::nullopt);
  REQUIRE(during.tiles.count("ev|harbor"));
  CHECK(during.tiles.at("ev|harbor").pill == sb::Pill::live);
  // The same game made again ten hours on: gone.
  sb::Board later = make(start + 10 * 3600, std::nullopt);
  CHECK_FALSE(later.tiles.count("ev|harbor"));
  // A "live" kept from earlier that the clock contradicts (half a day on): not LIVE.
  sb::Board stale = make(start + 14 * 3600, LivePhase::live);
  CHECK((!stale.tiles.count("ev|harbor") || stale.tiles.at("ev|harbor").pill != sb::Pill::live));
}

TEST_CASE("engine: Live Events shows placeholders until its board is made from the games there are now") {
  fake::Tv tv;
  tv.pool.answer(fake::kPlaylist, playlist());
  Store store(tv.host());
  REQUIRE(fake::addPlaylist(store, tv).empty());
  CHECK_FALSE(store.events()->loading);  // closed: nothing loading
  // The board made while closed is older than the games worked out since:
  // placeholders, no tiles, until a board is made from them.
  store.eventsOpen(true);
  auto opened = store.events();
  CHECK(opened->loading);
  CHECK(tiles(*opened) == 0);
  fake::settle(store, tv);
  auto board = store.events();
  REQUIRE(tiles(*board) > 10);
  CHECK_FALSE(board->loading);
  // The schedule still coming (it doesn't answer here): placeholder rows
  // after the rows in, until it gives up.
  for (int i = 0; i < 60 && store.events()->more; ++i) fake::settle(store, tv);
  CHECK_FALSE(store.events()->more);
  CHECK(tiles(*store.events()) == tiles(*board));
  // Closed and opened again at once: the board just made shows as it is.
  store.eventsOpen(false);
  fake::settle(store, tv);
  uint64_t before = store.events()->version;
  store.eventsOpen(true);
  CHECK(store.events()->version == before);
  CHECK(tiles(*store.events()) > 10);
  store.eventsOpen(false);
}
