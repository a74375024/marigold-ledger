// Search's keyboard grid, a swipe stopping at its edge, the corner
// preview's pace and Search's key hints.
#include <doctest.h>

#include <string>

#include "core/input/hints.hpp"
#include "core/input/preview_pace.hpp"
#include "core/input/search_keys.hpp"

using namespace marigold;
using search_keys::Grid;
using Area = Grid::Spot::Area;

TEST_CASE("search keys: the 6x6 grid, the wide keys and Speak") {
  Grid g(true);
  CHECK(g.typed() == "a");
  CHECK(Grid::keyAt(4, 2) == '1');
  CHECK(Grid::keyAt(5, 5) == '0');
  CHECK(Grid::keyFor('z').row == 4);
  CHECK(Grid::keyFor('z').col == 1);
  // Up from the top row: Speak; Right from Speak: the results.
  CHECK(g.move(Key::Up).moved);
  CHECK(g.at().area == Area::Speak);
  CHECK(g.move(Key::Right).edge == Grid::Edge::Right);
  CHECK(g.move(Key::Down).moved);
  CHECK(g.typed() == "f");
  // Right from the last column, Left from the first: out of the grid.
  CHECK(g.move(Key::Right).edge == Grid::Edge::Right);
  g.home();
  CHECK(g.move(Key::Left).edge == Grid::Edge::Left);
  // Down to the wide keys: Delete, Space, Clear (Clear far from Delete).
  g.set(Grid::keyFor('7'));
  CHECK(g.move(Key::Down).moved);
  CHECK(g.at().area == Area::Bottom);
  CHECK(g.at().col == static_cast<int>(search_keys::Wide::Space));
  CHECK(g.typed().empty());
  // Up returns to the column it came from.
  CHECK(g.move(Key::Up).moved);
  CHECK(g.typed() == "7");
  // No Speak (Fire TV): Up from the top row goes nowhere.
  Grid fire(false);
  CHECK(fire.move(Key::Up).edge == Grid::Edge::Up);
}

TEST_CASE("search keys: a swipe stops at the grid's edge, the next gesture crosses") {
  Grid g(false);
  // One long swipe (glide 7) right from "a": five moves to "f".
  for (int i = 0; i < 5; ++i) CHECK(g.move(Key::Right, 7).moved);
  CHECK(g.typed() == "f");
  // Its next move would cross to the results: dropped, and so is the rest
  // of that swipe, whatever the direction.
  Grid::Move m = g.move(Key::Right, 7);
  CHECK_FALSE(m.moved);
  CHECK(m.edge == Grid::Edge::None);
  CHECK_FALSE(g.move(Key::Left, 7).moved);
  CHECK(g.typed() == "f");
  // A new swipe crosses.
  CHECK(g.move(Key::Right, 8).edge == Grid::Edge::Right);
  // A ring press (glide 0) always crosses.
  CHECK(g.move(Key::Right, 0).edge == Grid::Edge::Right);
  // The same on the left, into the menu.
  g.set(Grid::keyFor('c'));
  CHECK(g.move(Key::Left, 9).moved);
  CHECK(g.move(Key::Left, 9).moved);
  CHECK(g.typed() == "a");
  CHECK(g.move(Key::Left, 9).edge == Grid::Edge::None);
  CHECK(g.move(Key::Left, 10).edge == Grid::Edge::Left);
  // The results list uses the same stop.
  search_keys::GlideStop stop;
  CHECK_FALSE(stop.stop(3));  // a swipe that starts at the edge crosses
  stop.moved(3);
  CHECK_FALSE(stop.blocked(3));
  CHECK(stop.stop(3));
  CHECK(stop.blocked(3));
  CHECK_FALSE(stop.blocked(4));
  CHECK_FALSE(stop.stop(0));
}

TEST_CASE("preview: rest, gap and once per item") {
  preview::Pace pace(true);
  using S = preview::Pace::Step;
  CHECK(pace.step("", false, 10000) == S::Idle);
  CHECK(pace.step("a", false, 10000) == S::Wait);
  CHECK(pace.step("a", false, 11999) == S::Wait);
  CHECK(pace.step("a", false, 12000) == S::Tune);
  // Tried once: not again while focus stays (a failed feed isn't retried).
  CHECK(pace.step("a", false, 20000) == S::Idle);
  // Another item: its own rest, and at least the gap after the last start.
  CHECK(pace.step("b", false, 12500) == S::Wait);
  CHECK(pace.step("b", false, 14600) == S::Wait);  // rested, but not 3 s since "a" started
  CHECK(pace.step("b", false, 15000) == S::Tune);
  // Playing already: nothing.
  CHECK(pace.step("c", true, 30000) == S::Idle);
  // The guide retries an item (not once per item).
  preview::Pace guide(false);
  CHECK(guide.step("a", false, 10000) == S::Wait);
  CHECK(guide.step("a", false, 12000) == S::Tune);
  CHECK(guide.step("a", false, 16000) == S::Tune);
}

TEST_CASE("preview: nothing on the first landing until focus moves; the pointer restarts the rest") {
  preview::Pace pace(true);
  using S = preview::Pace::Step;
  pace.hold("top");
  CHECK(pace.step("top", false, 10000) == S::Idle);
  CHECK(pace.step("top", false, 20000) == S::Idle);
  // Moved away and back: the normal rule.
  CHECK(pace.step("next", false, 21000) == S::Wait);
  CHECK(pace.step("top", false, 22000) == S::Wait);
  CHECK(pace.step("top", false, 24000) == S::Tune);
  // A pointer move restarts the rest.
  CHECK(pace.step("x", false, 30000) == S::Wait);
  pace.moved(31500);
  CHECK(pace.step("x", false, 32000) == S::Wait);
  CHECK(pace.step("x", false, 33500) == S::Tune);
}

TEST_CASE("hints: Search on LG, Fire TV and Apple TV") {
  using namespace marigold::hints;
  Bar lg = search(RemoteKeys::lg(), SearchPlace::Keyboard, true);
  REQUIRE(lg.keys.size() == 2);
  CHECK(lg.keys[0].colour == Colour::Red);
  CHECK(lg.keys[0].label == "Clear text");
  CHECK(lg.keys[1].label == "Channel");
  CHECK(lg.right == "\xe2\x96\xb6 Choose a result \xc2\xb7 Back: Close");
  CHECK(search(RemoteKeys::lg(), SearchPlace::Keyboard, false).right == "Back: Close");
  CHECK(search(RemoteKeys::lg(), SearchPlace::Results, true).right ==
        "\xe2\x97\x80 Keyboard \xc2\xb7 Hold OK: more \xc2\xb7 Back: Keyboard");
  // Fire TV: nothing on the keyboard; ☰ is More in the results.
  CHECK(search(RemoteKeys::fireTv(), SearchPlace::Keyboard, true).keys.empty());
  Bar fire = search(RemoteKeys::fireTv(), SearchPlace::Results, true);
  REQUIRE(fire.keys.size() == 1);
  CHECK(fire.keys[0].icons == std::vector<std::string>{"key-menu"});
  CHECK(fire.right == "\xe2\x97\x80 Keyboard \xc2\xb7 Back: Keyboard");
  // Apple TV: how to move and type; click and hold for More.
  Bar apple = search(RemoteKeys::appleTv(), SearchPlace::Keyboard, true);
  REQUIRE(apple.keys.size() == 2);
  CHECK(apple.keys[0].cap == "Swipe or ring");
  CHECK(apple.keys[1].label == "Type");
  CHECK(search(RemoteKeys::appleTv(), SearchPlace::Results, true).keys[0].cap == "Click and hold");
  // LG's number box: plays when complete, or waits when the digits start a name.
  Bar box = search(RemoteKeys::lg(), SearchPlace::Digits, false, {"206", ""});
  CHECK(box.keys[0].label == "Watch now");
  CHECK(box.right == "Plays when the number is complete \xc2\xb7 Back: Cancel");
  Bar wait = search(RemoteKeys::lg(), SearchPlace::Digits, false, {"49", "49s"});
  CHECK(wait.keys[0].label == "Watch channel 49");
  CHECK(wait.keys[1].label == "The 49s");
  CHECK(wait.right == "Doesn't tune by itself \xc2\xb7 Back: Cancel");
}
