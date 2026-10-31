// The guide in the player (Build 116): Left at the first show at Now opens
// the neighborhoods inside the card, Left again the lists; the return rule,
// Back in each, the lists themselves, the pointer and a Siri Remote swipe.
#include <doctest.h>

#include <vector>

#include "core/input/player_rail.hpp"

using marigold::Key;
using namespace marigold::rail;
using Do = PlayerStep::Do;

namespace {

// The grid at Now on the first show of a row in neighborhood `origin`, of
// `count`.
Grid edge(int origin = 1, int count = 6) { return {true, false, false, origin, count}; }

}  // namespace

TEST_CASE("player rail: the lists, with All Channels only when on and Live Events only with the board") {
  CHECK(playerLists(false, true) == std::vector<Item>{Item::Lineup, Item::Favorites, Item::Recent, Item::Events});
  CHECK(playerLists(true, true) ==
        std::vector<Item>{Item::Lineup, Item::Favorites, Item::Recent, Item::Events, Item::All});
  // The game schedule off in Settings: no Live Events.
  CHECK(playerLists(false, false) == std::vector<Item>{Item::Lineup, Item::Favorites, Item::Recent});
  CHECK(playerLists(true, false) == std::vector<Item>{Item::Lineup, Item::Favorites, Item::Recent, Item::All});
  // Search, My Sports and Settings are the main guide's, never here.
  for (Item i : playerLists(true, true)) CHECK(isSection(i));
}

TEST_CASE("player rail: Left opens the column only at the first show at Now") {
  PlayerNav n;
  // A later show in the row: the grid goes to the one before.
  CHECK(n.key(Key::Left, {false, false, false, 1, 6}).what == Do::Grid);
  CHECK(n.mode() == Mode::Content);
  // A later time, even on the first show: earlier first.
  CHECK(n.key(Key::Left, {true, true, false, 1, 6}).what == Do::Grid);
  CHECK(n.mode() == Mode::Content);
  // The first show at Now: the column, on the focused row's neighborhood.
  PlayerStep s = n.key(Key::Left, edge(1, 6));
  CHECK(s.what == Do::Column);
  CHECK(n.mode() == Mode::Column);
  CHECK(n.origin() == 1);
  CHECK(n.index() == 1);
  CHECK(n.count() == 6);
  // The grid's own keys are the screen's.
  PlayerNav m;
  for (Key k : {Key::Up, Key::Down, Key::Right, Key::Ok, Key::ChannelUp, Key::FastForward})
    CHECK(m.key(k, edge()).what == Do::Grid);
}

TEST_CASE("player rail: walking the column scrolls the grid behind and never tunes") {
  PlayerNav n;
  n.key(Key::Left, edge(0, 3));
  PlayerStep s = n.key(Key::Down, edge());
  CHECK(s.what == Do::Scroll);  // the grid behind, to its first channel
  CHECK(s.index == 1);
  s = n.key(Key::Down, edge());
  CHECK(s.what == Do::Scroll);
  CHECK(s.index == 2);
  CHECK(n.key(Key::Down, edge()).what == Do::None);  // the last
  CHECK(n.key(Key::Up, edge()).index == 1);
  // Walking never lands: nothing is picked, nothing plays.
  CHECK(n.mode() == Mode::Column);
  for (Key k : {Key::Up, Key::Down, Key::Up}) {
    Do d = n.key(k, edge()).what;
    CHECK(d != Do::Open);
    CHECK(d != Do::Grid);
  }
}

TEST_CASE("player rail: the return rule, as in the main guide") {
  SUBCASE("the neighborhood it opened from: exactly where you were") {
    PlayerNav n;
    n.key(Key::Left, edge(2, 6));
    n.key(Key::Down, edge());
    n.key(Key::Up, edge());
    PlayerStep s = n.key(Key::Ok, edge());
    CHECK(s.what == Do::Restore);
    CHECK(n.mode() == Mode::Content);
  }
  SUBCASE("another: its top at Now, by OK or Right") {
    PlayerNav n;
    n.key(Key::Left, edge(2, 6));
    n.key(Key::Down, edge());
    PlayerStep s = n.key(Key::Right, edge());
    CHECK(s.what == Do::Top);
    CHECK(s.index == 3);
    CHECK(n.mode() == Mode::Content);
  }
  SUBCASE("a click on a neighborhood follows it too") {
    PlayerNav n;
    n.edge(2, 6);  // the list name clicked
    CHECK(n.mode() == Mode::Column);
    CHECK(n.clickColumn(2).what == Do::Restore);
    n.edge(2, 6);
    PlayerStep s = n.clickColumn(5);
    CHECK(s.what == Do::Top);
    CHECK(s.index == 5);
  }
  SUBCASE("Back cancels: exactly where you were") {
    PlayerNav n;
    n.key(Key::Left, edge(2, 6));
    n.key(Key::Down, edge());
    CHECK(n.key(Key::Back, edge()).what == Do::Restore);
    CHECK(n.mode() == Mode::Content);
  }
}

TEST_CASE("player rail: Back goes lists, column, digits, later time, then closes") {
  PlayerNav n;
  n.key(Key::Left, edge(1, 4));
  PlayerStep s = n.key(Key::Left, edge());
  CHECK(s.what == Do::Restore);  // the grid put back under the lists
  CHECK(n.mode() == Mode::Menu);
  CHECK(n.fromColumn());
  CHECK(n.lists()[static_cast<size_t>(n.listFocus())] == Item::Lineup);  // the list on screen
  CHECK(n.key(Key::Left, edge()).what == Do::None);  // as far left as it goes
  // Lists: back to the column, on the neighborhood it opened on.
  s = n.key(Key::Back, edge());
  CHECK(s.what == Do::Column);
  CHECK(n.mode() == Mode::Column);
  CHECK(n.index() == 1);
  // Column: cancels.
  CHECK(n.key(Key::Back, edge()).what == Do::Restore);
  CHECK(n.mode() == Mode::Content);
  // The grid: a typed number, then a later time, then the guide closes.
  CHECK(n.key(Key::Back, {false, true, true, 0, 4}).what == Do::Digits);
  CHECK(n.key(Key::Back, {false, true, false, 0, 4}).what == Do::Now);
  CHECK(n.key(Key::Back, {false, false, false, 0, 4}).what == Do::Close);
}

TEST_CASE("player rail: a list picked in the lists opens its column") {
  PlayerNav n;
  n.setLists(playerLists(true, true));
  n.key(Key::Left, edge(0, 8));
  n.key(Key::Left, edge());
  n.key(Key::Down, edge());
  PlayerStep s = n.key(Key::Ok, edge());
  CHECK(s.what == Do::Open);
  CHECK(s.item == Item::Favorites);
  CHECK(n.mode() == Mode::Content);
  // The screen shows Favorites, then its neighborhoods on the playing
  // channel's; the return rule there puts it back on that channel.
  n.setList(Item::Favorites);
  CHECK(n.openColumn(1, 3).what == Do::Column);
  CHECK(n.key(Key::Ok, edge()).what == Do::Restore);
  // Back in that column goes to the grid, not back to the lists.
  n.openColumn(1, 3);
  CHECK(n.key(Key::Back, edge()).what == Do::Restore);
  CHECK(n.mode() == Mode::Content);
  // Right opens as OK does; Live Events is a list like the others.
  n.key(Key::Left, edge(0, 3));
  n.key(Key::Left, edge());
  CHECK(n.lists()[static_cast<size_t>(n.listFocus())] == Item::Favorites);
  n.key(Key::Down, edge());
  n.key(Key::Down, edge());
  s = n.key(Key::Right, edge());
  CHECK(s.what == Do::Open);
  CHECK(s.item == Item::Events);
  // A click on a list.
  n.openLists();
  CHECK(n.mode() == Mode::Menu);
  CHECK(n.pickList(4).item == Item::All);
}

TEST_CASE("player rail: Recent has no neighborhoods, so Left opens the lists") {
  PlayerNav n(Item::Recent);
  n.setLists(playerLists(false, true));
  PlayerStep s = n.key(Key::Left, {true, false, false, 0, 0});
  CHECK(s.what == Do::None);
  CHECK(n.mode() == Mode::Menu);
  CHECK_FALSE(n.fromColumn());
  CHECK(n.lists()[static_cast<size_t>(n.listFocus())] == Item::Recent);
  // Back: the grid.
  CHECK(n.key(Key::Back, edge()).what == Do::None);
  CHECK(n.mode() == Mode::Content);
  // A list without any neighborhood (an empty Favorites): the same.
  PlayerNav f(Item::Favorites);
  f.key(Key::Left, {true, false, false, 0, 0});
  CHECK(f.mode() == Mode::Menu);
  // Picked in the lists: no column, its grid.
  CHECK(f.openColumn(0, 0).what == Do::None);
  CHECK(f.mode() == Mode::Content);
}

TEST_CASE("player rail: the pointer's list name and the column's Lists button") {
  PlayerNav n;
  // The list name: as Left at the first show.
  CHECK(n.edge(3, 5).what == Do::Column);
  CHECK(n.origin() == 3);
  // "◀ Lists" in the column's header: the lists, over the grid put back.
  CHECK(n.openLists().what == Do::Restore);
  CHECK(n.mode() == Mode::Menu);
  CHECK(n.fromColumn());
  // A click on the dimmed card: as Back.
  CHECK(n.closeLists().what == Do::Column);
  CHECK(n.mode() == Mode::Column);
  // A click on the grid closes the column where it is.
  n.close();
  CHECK(n.mode() == Mode::Content);
  CHECK(n.closeLists().what == Do::None);
  CHECK(n.pickList(0).what == Do::None);  // the lists aren't open
}

TEST_CASE("player rail: one Siri Remote swipe moves one neighborhood, and stops at the grid's edge") {
  PlayerNav n;
  // A swipe walking the grid left reaches the first show: it stops there.
  CHECK(n.key(Key::Left, {false, false, false, 1, 6}, 7).what == Do::Grid);
  CHECK(n.key(Key::Left, edge(1, 6), 7).what == Do::None);
  CHECK(n.mode() == Mode::Content);
  // The next swipe opens the column, and only that.
  CHECK(n.key(Key::Left, edge(1, 6), 8).what == Do::Column);
  CHECK(n.key(Key::Left, edge(), 8).what == Do::None);
  CHECK(n.mode() == Mode::Column);
  // One long swipe down: one neighborhood.
  CHECK(n.key(Key::Down, edge(), 9).what == Do::Scroll);
  CHECK(n.key(Key::Down, edge(), 9).what == Do::None);
  CHECK(n.key(Key::Down, edge(), 9).what == Do::None);
  CHECK(n.index() == 2);
  // At the column's edge a swipe goes no further.
  n.key(Key::Up, edge(), 10);
  CHECK(n.key(Key::Up, edge(), 11).what == Do::Scroll);
  CHECK(n.index() == 0);
  CHECK(n.key(Key::Up, edge(), 12).what == Do::None);
  CHECK(n.index() == 0);
  // The D-pad and the wheel (no swipe) move every press.
  CHECK(n.key(Key::Down, edge()).what == Do::Scroll);
  CHECK(n.key(Key::Down, edge()).what == Do::Scroll);
  CHECK(n.index() == 2);
}
