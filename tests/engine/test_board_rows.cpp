// Show on the board (Build 119): More events, Time not listed and Event
// channels are left off the board screen (and its SPORTS column and the
// games rail) until switched on, installs from before too; the board itself
// keeps them. Search finds the event channel groups as channels.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/engine/search.hpp"
#include "core/engine/search_build.hpp"
#include "core/engine/store.hpp"
#include "core/engine/views.hpp"
#include "engine/fake_host.hpp"

using marigold::engine::BoardShows;
using marigold::engine::EventGroup;
using marigold::engine::EventRow;
using marigold::engine::EventsBoard;
using marigold::engine::EventTile;
using marigold::engine::Settings;
using marigold::engine::Store;
namespace views = marigold::engine::views;
namespace se = marigold::engine::search;
namespace search_build = marigold::engine::search_build;

namespace {

EventRow row(const std::string& id, const std::string& title, std::vector<std::string> tiles, bool families = false) {
  EventRow r;
  r.id = id;
  r.title = title;
  r.families = families;
  EventGroup g;
  for (const std::string& t : tiles) {
    EventTile tile;
    tile.id = t;
    tile.title = t;
    g.tiles.push_back(tile);
  }
  r.groups.push_back(g);
  r.count = static_cast<int>(tiles.size());
  return r;
}

// A board's rows as the store makes them: the sports, then the three.
std::vector<EventRow> boardRows() {
  return {row("live", "Live now", {"a"}), row("sport|Golf", "Golf", {"a", "b"}),
          row("more", "More events", {"darts", "marathon"}), row("untimed", "Time not listed", {"regatta"}),
          row("families", "Event channels", {"fam|matchday", "fam|fights"}, true)};
}

std::vector<std::string> ids(const std::vector<EventRow>& rows) {
  std::vector<std::string> out;
  for (const EventRow& r : rows) out.push_back(r.id);
  return out;
}

}  // namespace

TEST_CASE("board rows: the three switches are off by default, for installs from before too") {
  fake::Tv tv;
  {
    Store store(tv.host());
    const Settings& s = store.settings();
    CHECK_FALSE(s.boardMoreEvents);
    CHECK_FALSE(s.boardUntimed);
    CHECK_FALSE(s.boardEventChannels);
    BoardShows shows = marigold::engine::boardShows(s);
    CHECK_FALSE(shows.more);
    CHECK_FALSE(shows.untimed);
    CHECK_FALSE(shows.families);
  }
  // Settings saved by Build 118 (no such keys): hidden.
  tv.files.write("settings.json", R"({"previewChannels":true,"scheduleEnabled":true,"showUnwatchable":true,)"
                                  R"("showScores":false,"clock24":true})", true);
  {
    Store store(tv.host());
    CHECK_FALSE(store.settings().boardMoreEvents);
    CHECK_FALSE(store.settings().boardUntimed);
    CHECK_FALSE(store.settings().boardEventChannels);
    CHECK_FALSE(store.settings().boardUnscheduled);  // Build 125: Games without a schedule
    CHECK_FALSE(store.settings().showScores);  // the rest as saved
    CHECK(store.settings().clock24);
    // Switched on: saved, and on after a restart.
    Settings s = store.settings();
    s.boardMoreEvents = s.boardEventChannels = s.boardUnscheduled = true;
    store.setSettings(s);
  }
  Store again(tv.host());
  CHECK(again.settings().boardMoreEvents);
  CHECK_FALSE(again.settings().boardUntimed);
  CHECK(again.settings().boardEventChannels);
  CHECK(again.settings().boardUnscheduled);
  // Game schedule off: Event channels always show.
  Settings off;
  off.scheduleEnabled = false;
  CHECK(marigold::engine::boardShows(off).families);
  CHECK_FALSE(marigold::engine::boardShows(off).more);
}

TEST_CASE("board rows: each switch shows its row; the board keeps them all") {
  EventsBoard board;
  board.rows = boardRows();
  board.index();
  const std::vector<std::string> sports = {"live", "sport|Golf"};
  BoardShows none;
  CHECK(views::boardHides(board.rows, none));
  CHECK(ids(views::boardShown(board.rows, none)) == sports);
  CHECK(views::boardHidden(board.rows, none) ==
        std::vector<std::string>{"More events", "Time not listed", "Event channels"});
  BoardShows more;
  more.more = true;
  CHECK(ids(views::boardShown(board.rows, more)) == std::vector<std::string>{"live", "sport|Golf", "more"});
  BoardShows untimed;
  untimed.untimed = true;
  CHECK(ids(views::boardShown(board.rows, untimed)) == std::vector<std::string>{"live", "sport|Golf", "untimed"});
  BoardShows families;
  families.families = true;
  CHECK(ids(views::boardShown(board.rows, families)) == std::vector<std::string>{"live", "sport|Golf", "families"});
  CHECK(views::boardHidden(board.rows, families) == std::vector<std::string>{"More events", "Time not listed"});
  BoardShows all{true, true, true};
  CHECK_FALSE(views::boardHides(board.rows, all));
  CHECK(ids(views::boardShown(board.rows, all)) == ids(board.rows));
  CHECK(views::boardHidden(board.rows, all).empty());
  // The board itself is untouched: every row and tile is still there (for
  // Search, Where to Watch and Up/Down while watching).
  CHECK(board.rows.size() == 5);
  CHECK(board.tile("darts"));
  CHECK(board.tile("regatta"));
  CHECK(board.tile("fam|fights"));
  // A board without the three rows hides nothing (no copy).
  std::vector<EventRow> plain = {row("live", "Live now", {"a"})};
  CHECK_FALSE(views::boardHides(plain, none));
}

TEST_CASE("board rows: the games rail keeps the row of the game on screen") {
  std::vector<EventRow> rows = boardRows();
  BoardShows none;
  // Watching a game from More events (picked in Search): its row stays.
  CHECK(ids(views::boardShown(rows, none, "marathon")) == std::vector<std::string>{"live", "sport|Golf", "more"});
  CHECK(ids(views::boardShown(rows, none, "regatta")) == std::vector<std::string>{"live", "sport|Golf", "untimed"});
  // A game in a shown row, or an event channel group's id: nothing comes back.
  CHECK(ids(views::boardShown(rows, none, "b")) == std::vector<std::string>{"live", "sport|Golf"});
  CHECK(ids(views::boardShown(rows, none, "fam|matchday")) == std::vector<std::string>{"live", "sport|Golf"});
}

TEST_CASE("board rows: the empty board names what is hidden") {
  CHECK(views::hiddenLine({}).empty());
  CHECK(views::hiddenLine({"Event channels"}) == "The Event channels row is hidden.");
  CHECK(views::hiddenLine({"Time not listed", "Event channels"}) == "Time not listed and Event channels are hidden.");
  CHECK(views::hiddenLine({"More events", "Time not listed", "Event channels"}) ==
        "More events, Time not listed and Event channels are hidden.");
}

TEST_CASE("board rows: Search finds event channel groups as channels") {
  search_build::BoardInput in;
  in.families = {{"fam|matchday", "Matchday Feeds", false, false}, {"fam|hoops", "Hoops Pass", true, false}};
  search_build::BoardPart part = search_build::board(in);
  REQUIRE(part.index);
  se::Index guide;
  guide.lineupSet = true;
  guide.finish();
  se::Index index = se::Index::join(guide, *part.index);
  REQUIRE(index.families.size() == 2);
  se::Options o;
  // By name, as a channel that plays the group.
  se::Results r = se::run(index, se::parse("matchday"), o);
  REQUIRE(r.top);
  CHECK(r.top->kind == se::Kind::Channel);
  CHECK(r.top->id == "fam|matchday");
  CHECK(r.top->family);
  CHECK(r.top->name == "Matchday Feeds");
  // Outside your lineup unless a channel of the group is in it.
  CHECK(r.top->outside);
  r = se::run(index, se::parse("hoops pass"), o);
  REQUIRE(r.top);
  CHECK(r.top->id == "fam|hoops");
  CHECK_FALSE(r.top->outside);
  // Scoped: a group in your lineup is in scope; one outside only comes up
  // as what's outside (nothing else matches), or named exactly.
  o.scoped = true;
  r = se::run(index, se::parse("hoops"), o);
  CHECK_FALSE(r.outsideOnly);
  REQUIRE(r.top);
  CHECK(r.top->id == "fam|hoops");
  r = se::run(index, se::parse("matchda"), o);
  CHECK(r.outsideOnly);
  REQUIRE(r.top);
  CHECK(r.top->id == "fam|matchday");
  r = se::run(index, se::parse("matchday feeds"), o);
  REQUIRE(r.top);
  CHECK(r.top->id == "fam|matchday");
  CHECK(r.top->outside);
  // Before a lineup is set up, nothing is outside.
  guide.lineupSet = false;
  se::Index unset = se::Index::join(guide, *part.index);
  r = se::run(unset, se::parse("matchday"), se::Options{});
  REQUIRE(r.top);
  CHECK_FALSE(r.top->outside);
}
