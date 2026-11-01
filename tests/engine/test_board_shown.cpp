// The board's rows as the screens show them without copying the board
// (Build 119): the rows shown as indices, a row's tiles (or a filter's) as
// pointers into it, the focus over them, and the event channel groups'
// lineup and favourite marks made on Search's worker.
#include <doctest.h>

#include <functional>
#include <string>
#include <vector>

#include "core/engine/search_build.hpp"
#include "core/engine/views.hpp"

using marigold::Channel;
using marigold::catalog::CatalogResult;
using marigold::catalog::EventFamily;
using marigold::engine::BoardShows;
using marigold::engine::Channels;
using marigold::engine::EventGroup;
using marigold::engine::EventRow;
using marigold::engine::EventsBoard;
using marigold::engine::EventTile;
using marigold::engine::GuideChannel;
namespace views = marigold::engine::views;
namespace search_build = marigold::engine::search_build;

namespace {

EventTile tile(const std::string& id, bool live = false, bool favorite = false, bool watchable = true) {
  EventTile t;
  t.id = id;
  t.title = id;
  t.pill = live ? EventTile::Pill::Live : EventTile::Pill::None;
  t.upcoming = !live;
  t.favorite = favorite;
  t.watchable = watchable;
  t.sport = id.substr(0, 1);
  return t;
}

EventRow row(const std::string& id, std::vector<EventGroup> groups, bool families = false) {
  EventRow r;
  r.id = id;
  r.title = "Row " + id;
  r.families = families;
  r.groups = std::move(groups);
  for (const EventGroup& g : r.groups) {
    r.count += static_cast<int>(g.tiles.size());
    for (const EventTile& t : g.tiles)
      if (t.pill == EventTile::Pill::Live && t.watchable) ++r.live;
  }
  return r;
}

// A board as the store makes it: groups (one of them empty), the three rows
// left out by default, a game in several rows.
std::vector<EventRow> fixture() {
  return {row("live", {{"Live", {tile("a1", true), tile("b1", true, true)}}}),
          row("teams", {{"", {tile("b1", true, true), tile("b2", false, true)}}}),
          row("sport|A", {{"Live", {tile("a1", true), tile("a2", true, false, false)}},
                          {"", {}},
                          {"Later Today", {tile("a3"), tile("a4", false, true)}}}),
          row("sport|B", {{"Live", {tile("b1", true, true)}}, {"Sat", {tile("b2", false, true), tile("b3")}}}),
          row("more", {{"", {tile("m1", true), tile("m2")}}}),
          row("untimed", {{"", {tile("u1", true)}}}),
          row("families", {{"", {tile("fam|x"), tile("fam|y")}}}, true)};
}

std::vector<std::string> ids(const views::ShownRow& r) {
  std::vector<std::string> out;
  for (const EventTile* t : r.tiles) out.push_back(t->id);
  return out;
}

std::vector<std::string> ids(const EventRow& r) {
  std::vector<std::string> out;
  for (const EventGroup& g : r.groups)
    for (const EventTile& t : g.tiles) out.push_back(t.id);
  return out;
}

// What the board screen's filter made before Build 119: copies of the rows
// with only the tiles kept (Live now, My teams).
std::vector<EventRow> copiedFilter(const std::vector<EventRow>& rows, bool live) {
  auto isLive = [](const EventTile& t) { return !t.upcoming && !t.finished && t.pill == EventTile::Pill::Live; };
  std::vector<EventRow> out;
  for (const EventRow& r : rows) {
    if (r.families || r.id == "live" || r.id == "teams" || r.id == "untimed") continue;
    EventRow kept = r;
    kept.groups.clear();
    kept.count = kept.live = 0;
    for (const EventGroup& g : r.groups) {
      EventGroup group{g.title, {}};
      for (const EventTile& t : g.tiles)
        if (live ? isLive(t) : t.favorite) group.tiles.push_back(t);
      if (group.tiles.empty()) continue;
      kept.count += static_cast<int>(group.tiles.size());
      for (const EventTile& t : group.tiles) kept.live += isLive(t) && t.watchable;
      kept.groups.push_back(std::move(group));
    }
    if (!kept.groups.empty()) out.push_back(std::move(kept));
  }
  return out;
}

}  // namespace

TEST_CASE("board shown: the indices are the rows boardShown() copies, for every switch and kept game") {
  std::vector<EventRow> rows = fixture();
  for (int bits = 0; bits < 8; ++bits) {
    BoardShows shows{(bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0};
    for (const char* keep : {"", "m2", "u1", "a3", "fam|x", "nothing"}) {
      std::vector<size_t> at = views::boardShownRows(rows, shows, keep);
      std::vector<EventRow> copied = views::boardShown(rows, shows, keep);
      REQUIRE(at.size() == copied.size());
      for (size_t i = 0; i < at.size(); ++i) {
        CHECK(rows[at[i]].id == copied[i].id);
        // Every tile of the row, in order, by pointer into the board.
        views::ShownRow shown = views::shownRow(rows[at[i]]);
        CHECK(shown.row == &rows[at[i]]);
        CHECK(ids(shown) == ids(copied[i]));
        CHECK(shown.count == copied[i].count);
        CHECK(shown.live == copied[i].live);
        REQUIRE(shown.groups.size() == copied[i].groups.size());
        for (size_t g = 0; g < shown.groups.size(); ++g) {
          CHECK(shown.groups[g].group->title == copied[i].groups[g].title);
          CHECK(shown.groups[g].end - shown.groups[g].begin == copied[i].groups[g].tiles.size());
        }
      }
      CHECK(views::boardHides(rows, shows, keep) == (at.size() != rows.size()));
    }
  }
}

TEST_CASE("board shown: a filter's rows point into the board and match the copies made before") {
  std::vector<EventRow> rows = fixture();
  auto isLive = [](const EventTile& t) { return !t.upcoming && !t.finished && t.pill == EventTile::Pill::Live; };
  for (bool live : {true, false}) {
    std::vector<EventRow> copied = copiedFilter(rows, live);
    std::vector<views::ShownRow> shown;
    for (const EventRow& r : rows) {
      if (r.families || r.id == "live" || r.id == "teams" || r.id == "untimed") continue;
      views::ShownRow kept = views::shownRow(
          r, [&](const EventTile& t) { return live ? isLive(t) : t.favorite; },
          [&](const EventTile& t) { return isLive(t) && t.watchable; });
      if (!kept.groups.empty()) shown.push_back(std::move(kept));
    }
    REQUIRE(shown.size() == copied.size());
    for (size_t i = 0; i < shown.size(); ++i) {
      CHECK(shown[i].row->id == copied[i].id);
      CHECK(ids(shown[i]) == ids(copied[i]));
      CHECK(shown[i].count == copied[i].count);
      CHECK(shown[i].live == copied[i].live);
      REQUIRE(shown[i].groups.size() == copied[i].groups.size());
      for (size_t g = 0; g < shown[i].groups.size(); ++g)
        CHECK(shown[i].groups[g].group->title == copied[i].groups[g].title);
      // Pointers into the board's own row, not copies.
      for (const EventTile* t : shown[i].tiles) {
        bool inRow = false;
        for (const EventGroup& g : shown[i].row->groups)
          for (const EventTile& x : g.tiles) inRow = inRow || &x == t;
        CHECK(inRow);
      }
    }
    // The focus lands where it did on the copies.
    for (const char* id : {"a1", "b2", "a4", "zz"})
      for (int r = 0; r < 4; ++r) {
        views::BoardFocus a = views::boardFocusShown(shown, "sport|B", id, r, 3);
        views::BoardFocus b = views::boardFocus(copied, "sport|B", id, r, 3);
        CHECK(a.row == b.row);
        CHECK(a.tile == b.tile);
      }
  }
}

TEST_CASE("board shown: the games rail lists a row by its index as it did from a copy") {
  EventsBoard board;
  board.rows = fixture();
  board.index();
  BoardShows none;
  std::vector<size_t> at = views::boardShownRows(board.rows, none, "m2");
  EventsBoard copy;
  copy.rows = views::boardShown(board.rows, none, "m2");
  REQUIRE(at.size() == copy.rows.size());
  for (size_t i = 0; i < at.size(); ++i) {
    std::vector<views::EventLine> a = views::eventGuide(board, at[i]);
    std::vector<views::EventLine> b = views::eventGuide(copy, i);
    REQUIRE(a.size() == b.size());
    for (size_t k = 0; k < a.size(); ++k) {
      CHECK(a[k].header == b[k].header);
      CHECK((a[k].tile ? a[k].tile->id : "") == (b[k].tile ? b[k].tile->id : ""));
    }
  }
  CHECK(views::eventGuide(board, board.rows.size()).empty());
}

TEST_CASE("board shown: event channel groups are marked on the worker as the store marked them") {
  // The channel list, the catalog's groups and the guide's entries.
  std::vector<Channel> list(5);
  for (size_t i = 0; i < list.size(); ++i) list[i].id = "m3u:a|" + std::to_string(i);
  auto catalog = std::make_shared<CatalogResult>();
  catalog->families = {EventFamily{"fam|x", "X", {"m3u:a|0", "m3u:a|1"}, false, "", std::nullopt},
                       EventFamily{"fam|y", "Y", {"m3u:a|2", "m3u:a|gone"}, false, "", std::nullopt},
                       EventFamily{"fam|z", "Z", {"m3u:a|3", "m3u:a|4"}, false, "", std::nullopt},
                       EventFamily{"fam|w", "W", {"m3u:a|gone"}, false, "", std::nullopt},
                       EventFamily{"fam|x", "X again", {"m3u:a|4"}, false, "", std::nullopt}};
  catalog->entryOf = {{"m3u:a|1", "e|one"}, {"m3u:a|2", "e|two"}, {"m3u:a|3", "e|three"}, {"m3u:a|4", "e|four"},
                      {"m3u:a|gone", "e|four"}};
  auto channels = std::make_shared<Channels>();
  auto entry = [&](const std::string& id, bool lineup, bool favorite) {
    GuideChannel g;
    g.id = id;
    g.inLineup = lineup;
    g.favorite = favorite;
    channels->byId[id] = static_cast<int>(channels->all.size());
    channels->all.push_back(g);
  };
  entry("e|one", true, false);
  entry("e|two", false, false);
  entry("e|three", false, true);
  entry("e|four", true, true);

  // Before: per group, its feeds still in the list (Store::familyFeeds()),
  // each feed's entry (Store::entryOfChannel()).
  auto old = [&](const std::string& family, bool& lineup, bool& favorite) {
    lineup = favorite = false;
    for (const EventFamily& f : catalog->families) {
      if (f.id != family) continue;
      for (const std::string& feed : f.feeds) {
        bool present = false;
        for (const Channel& c : list) present = present || c.id == feed;
        if (!present) continue;
        auto e = catalog->entryOf.find(feed);
        const GuideChannel* g = channels->find(e == catalog->entryOf.end() ? std::string() : e->second);
        lineup = lineup || (g && g->inLineup);
        favorite = favorite || (g && g->favorite);
      }
      return;
    }
  };
  auto marks = search_build::familyMarks(*catalog, list, *channels);
  for (const char* id : {"fam|x", "fam|y", "fam|z", "fam|w"}) {
    bool lineup = false, favorite = false;
    old(id, lineup, favorite);
    REQUIRE(marks->count(id));
    CHECK(marks->at(id).inLineup == lineup);
    CHECK(marks->at(id).favorite == favorite);
  }
  CHECK(marks->at("fam|x").inLineup);  // its first group's feeds, not the later one's
  CHECK_FALSE(marks->at("fam|x").favorite);
  CHECK_FALSE(marks->at("fam|w").inLineup);  // its feed left the list

  // The board part marks them itself, hands the marks back, and takes
  // them as they are when given.
  search_build::BoardInput in;
  in.families = {{"fam|x", "Matchday", false, false}, {"fam|z", "Fight Night", false, false}};
  in.catalog = catalog;
  in.list = std::make_shared<std::vector<Channel>>(list);
  in.channels = channels;
  search_build::BoardPart part = search_build::board(in);
  REQUIRE((part.marks != nullptr));
  REQUIRE((part.index != nullptr));
  const auto& fams = *part.index->families;
  REQUIRE(fams.size() == 2);
  CHECK(fams[0].inLineup);
  CHECK_FALSE(fams[0].favorite);
  CHECK(fams[1].inLineup);
  CHECK(fams[1].favorite);
  auto given = std::make_shared<search_build::FamilyMarksById>();
  (*given)["fam|z"] = {false, true};
  in.marks = given;
  part = search_build::board(in);
  CHECK((part.marks == given));
  const auto& again = *part.index->families;
  CHECK_FALSE(again[0].inLineup);  // not in the marks given
  CHECK_FALSE(again[1].inLineup);
  CHECK(again[1].favorite);
}
