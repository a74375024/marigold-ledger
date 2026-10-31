// The left rail's key path (Build 112): content, the neighborhood column
// and the menu, the return rule, and Back in each.
#include <doctest.h>

#include <string>

#include "core/input/left_rail.hpp"

using marigold::Key;
using namespace marigold::rail;
using Do = Step::Do;

TEST_CASE("left rail: the strip's items") {
  std::vector<Item> plain = items(false);
  REQUIRE(plain.size() == 7);
  CHECK(plain.front() == Item::Search);
  CHECK(plain[4] == Item::Events);
  CHECK(plain.back() == Item::Options);
  std::vector<Item> all = items(true);
  REQUIRE(all.size() == 8);
  CHECK(all[5] == Item::All);
  CHECK(std::string(title(Item::All)) == "All Channels");
  CHECK(hasColumn(Item::Lineup));
  CHECK(hasColumn(Item::Favorites));
  CHECK(hasColumn(Item::Events));
  CHECK_FALSE(hasColumn(Item::Recent));
  CHECK_FALSE(hasColumn(Item::Search));
  CHECK_FALSE(isSection(Item::Options));
  CHECK(atBottom(Item::MySports));
}

TEST_CASE("left rail: Left at the first show opens the column, then the menu") {
  Nav n;
  n.leftEdge(2, 5);
  CHECK(n.mode() == Mode::Column);
  CHECK(n.origin() == 2);
  CHECK(n.index() == 2);
  Step s = n.key(Key::Left);
  CHECK(s.what == Do::Restore);  // the grid slides back as it was
  CHECK(n.mode() == Mode::Menu);
  CHECK(n.items()[static_cast<size_t>(n.menuFocus())] == Item::Lineup);
}

TEST_CASE("left rail: Recent, or a list without neighborhoods, has no column") {
  Nav n(Item::Recent);
  n.leftEdge(0, 0);
  CHECK(n.mode() == Mode::Menu);
  CHECK(n.items()[static_cast<size_t>(n.menuFocus())] == Item::Recent);
  Nav lineup;
  lineup.leftEdge(0, 0);
  CHECK(lineup.mode() == Mode::Menu);
}

TEST_CASE("left rail: Up and Down walk the neighborhoods, and the content scrolls") {
  Nav n;
  n.leftEdge(0, 3);
  CHECK(n.key(Key::Up).what == Do::None);  // already the first
  Step s = n.key(Key::Down);
  CHECK(s.what == Do::Scroll);
  CHECK(s.index == 1);
  n.key(Key::Down);
  CHECK(n.key(Key::Down).what == Do::None);
  CHECK(n.index() == 2);
  CHECK(n.mode() == Mode::Column);
}

TEST_CASE("left rail: the return rule") {
  SUBCASE("confirming the neighborhood it opened on, after moving away and back") {
    Nav n;
    n.leftEdge(2, 6);
    n.key(Key::Up);
    n.key(Key::Up);
    n.key(Key::Down);
    n.key(Key::Down);
    Step s = n.key(Key::Ok);
    CHECK(s.what == Do::Restore);
    CHECK(n.mode() == Mode::Content);
  }
  SUBCASE("Right confirms as OK does") {
    Nav n;
    n.leftEdge(2, 6);
    CHECK(n.key(Key::Right).what == Do::Restore);
  }
  SUBCASE("another neighborhood: its top") {
    Nav n;
    n.leftEdge(2, 6);
    n.key(Key::Up);
    Step s = n.key(Key::Right);
    CHECK(s.what == Do::Top);
    CHECK(s.index == 1);
    CHECK(n.mode() == Mode::Content);
  }
  SUBCASE("a click follows the same rule") {
    Nav n;
    n.leftEdge(2, 6);
    CHECK(n.clickColumn(2).what == Do::Restore);
    n.leftEdge(2, 6);
    Step s = n.clickColumn(4);
    CHECK(s.what == Do::Top);
    CHECK(s.index == 4);
    Nav closed;
    CHECK(closed.clickColumn(1).what == Do::None);
  }
}

TEST_CASE("left rail: Back in the column cancels") {
  Nav n;
  n.leftEdge(1, 4);
  n.key(Key::Down);
  n.key(Key::Down);
  Step s = n.key(Key::Back);
  CHECK(s.what == Do::Restore);
  CHECK(n.mode() == Mode::Content);

  // A column the menu opened goes back to the menu.
  Nav m(Item::Favorites);
  m.openMenu();
  Step pick = m.key(Key::Ok);
  CHECK(pick.what == Do::Open);
  CHECK(pick.item == Item::Favorites);
  m.openColumn(0, 3, true);
  CHECK(m.fromMenu());
  CHECK(m.key(Key::Back).what == Do::Restore);
  CHECK(m.mode() == Mode::Menu);
}

TEST_CASE("left rail: the menu") {
  Nav n;
  n.openMenu();
  CHECK(n.items()[static_cast<size_t>(n.menuFocus())] == Item::Lineup);
  n.key(Key::Down);
  CHECK(n.items()[static_cast<size_t>(n.menuFocus())] == Item::Favorites);
  CHECK(n.key(Key::Left).what == Do::None);
  CHECK(n.mode() == Mode::Menu);
  Step s = n.key(Key::Right);
  CHECK(s.what == Do::Open);
  CHECK(s.item == Item::Favorites);
  CHECK(n.mode() == Mode::Content);

  // Up stops at Search; Down stops at Options.
  n.openMenu();
  for (int i = 0; i < 10; ++i) n.key(Key::Up);
  CHECK(n.items()[static_cast<size_t>(n.menuFocus())] == Item::Search);
  for (int i = 0; i < 10; ++i) n.key(Key::Down);
  CHECK(n.items()[static_cast<size_t>(n.menuFocus())] == Item::Options);

  // The pointer highlights, then picks.
  n.hoverMenu(3);
  CHECK(n.menuFocus() == 3);
  CHECK(n.pick(Item::Recent).item == Item::Recent);
  CHECK(n.mode() == Mode::Content);
  n.hoverMenu(1);  // closed: nothing moves
  CHECK(n.menuFocus() == 3);
}

TEST_CASE("left rail: Back in the menu goes to My Lineup, then out") {
  Nav n(Item::Recent);
  n.openMenu();
  Step s = n.key(Key::Back);
  CHECK(s.what == Do::Home);
  CHECK(n.section() == Item::Lineup);
  CHECK(n.mode() == Mode::Content);
  n.openMenu();
  CHECK(n.key(Key::Back).what == Do::Exit);
  CHECK(n.mode() == Mode::Menu);  // stays, for "Press Back again to exit"

  Nav events(Item::Events);
  events.openMenu();
  CHECK(events.key(Key::Back).what == Do::Home);
}

TEST_CASE("left rail: the strip with All Channels keeps the menu's focus in range") {
  Nav n(Item::All);
  n.setItems(items(true));
  n.openMenu();
  CHECK(n.items()[static_cast<size_t>(n.menuFocus())] == Item::All);
  n.setItems(items(false));
  CHECK(n.menuFocus() < static_cast<int>(n.items().size()));
  n.close();
  CHECK(n.mode() == Mode::Content);
}
