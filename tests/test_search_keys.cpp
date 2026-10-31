// Search's keyboard (its letters and 123 pages), a swipe stopping at its
// edge, the corner preview's pace and Search's key hints.
#include <doctest.h>

#include <set>
#include <string>

#include "core/input/hints.hpp"
#include "core/input/preview_pace.hpp"
#include "core/input/search_keys.hpp"

using namespace marigold;
using search_keys::Grid;
using search_keys::Page;
using Area = Grid::Spot::Area;

namespace {

Grid::Spot key(int row, int col) { return {Area::Key, row, col}; }
Grid::Spot wide(search_keys::Wide w) { return {Area::Bottom, 0, static_cast<int>(w)}; }

// A page's keys row by row, Space as "_" once.
std::string table(Page page) {
  std::string out;
  for (int r = 0; r < search_keys::kRows; ++r) {
    if (r) out += '/';
    for (int c = 0; c < search_keys::colsOf(page); ++c) {
      char ch = Grid::keyAt(page, r, c);
      if (ch == ' ' && c > 0 && Grid::keyAt(page, r, c - 1) == ' ') continue;
      out += ch == ' ' ? '_' : ch;
    }
  }
  return out;
}

}  // namespace

TEST_CASE("search keys: the letters page, 6x5 with Space after z, and the wide keys") {
  CHECK(table(Page::Letters) == "abcdef/ghijkl/mnopqr/stuvwx/yz_");
  CHECK(Grid::keyAt(Page::Letters, 4, 5) == ' ');
  CHECK(Grid::keyAt(Page::Letters, 5, 0) == 0);  // one row shorter than Build 113's
  CHECK(Grid::keyAt(Page::Letters, 0, 6) == 0);
  CHECK(Grid::keyFor(Page::Letters, 'z') == key(4, 1));
  CHECK(Grid::keyFor(Page::Letters, ' ') == key(4, 2));
  Grid g(true);
  CHECK(g.page() == Page::Letters);
  CHECK(g.typed() == "a");
  // Up from the top row: Speak; Right from Speak: the results; Down: f.
  CHECK(g.move(Key::Up).moved);
  CHECK(g.at().area == Area::Speak);
  CHECK(g.move(Key::Right).edge == Grid::Edge::Right);
  CHECK(g.move(Key::Down).moved);
  CHECK(g.typed() == "f");
  // Right from the last column, Left from the first: out of the keys.
  CHECK(g.move(Key::Right).edge == Grid::Edge::Right);
  g.home();
  CHECK(g.move(Key::Left).edge == Grid::Edge::Left);
  // Space: right of z, one key over four cells; Right from it leaves.
  g.set(Grid::keyFor(Page::Letters, 'z'));
  CHECK(g.move(Key::Right).moved);
  CHECK(g.at() == key(4, 2));
  CHECK(g.typed() == " ");
  CHECK(g.move(Key::Right).edge == Grid::Edge::Right);
  CHECK(g.move(Key::Left).moved);
  CHECK(g.typed() == "z");
  // Up from Space goes back to the column it came from.
  g.set(Grid::keyFor(Page::Letters, 'x'));
  CHECK(g.move(Key::Down).moved);
  CHECK(g.typed() == " ");
  CHECK(g.move(Key::Up).moved);
  CHECK(g.typed() == "x");
  // Down from the middle columns lands on the mode key; Clear on the right
  // and Delete on the left, away from each other.
  g.set(Grid::keyFor(Page::Letters, 'v'));
  CHECK(g.move(Key::Down).moved);
  CHECK(g.move(Key::Down).moved);
  CHECK(g.at() == wide(search_keys::Wide::Mode));
  CHECK(g.onMode());
  CHECK(g.typed().empty());
  CHECK(g.move(Key::Up).moved);
  CHECK(g.move(Key::Up).moved);
  CHECK(g.typed() == "v");
  g.set(Grid::keyFor(Page::Letters, 'x'));
  g.move(Key::Down);
  g.move(Key::Down);
  CHECK(g.at() == wide(search_keys::Wide::Clear));
  g.set(Grid::keyFor(Page::Letters, 'y'));
  CHECK(g.move(Key::Down).moved);
  CHECK(g.at() == wide(search_keys::Wide::Delete));
  CHECK(g.move(Key::Up).moved);
  CHECK(g.typed() == "y");
  // Up from the mode key: Space.
  g.set(wide(search_keys::Wide::Mode));
  CHECK(g.move(Key::Up).moved);
  CHECK(g.typed() == " ");
  CHECK(g.move(Key::Down).edge == Grid::Edge::None);
  CHECK(g.move(Key::Down).edge == Grid::Edge::Down);
  // No Speak (Fire TV): Up from the top row goes nowhere.
  Grid fire(false);
  CHECK(fire.move(Key::Up).edge == Grid::Edge::Up);
}

TEST_CASE("search keys: the 123 page is a phone keypad, and the mode key keeps the focus") {
  CHECK(table(Page::Digits) == "123/456/789/&0'/_");
  CHECK(Grid::keyAt(Page::Digits, 0, 3) == 0);
  // No - or . anywhere, and no digits on the letters.
  for (Page p : {Page::Letters, Page::Digits})
    for (char c : {'-', '.'}) CHECK(table(p).find(c) == std::string::npos);
  for (char d = '0'; d <= '9'; ++d) CHECK(table(Page::Letters).find(d) == std::string::npos);
  Grid g(true);
  g.set(wide(search_keys::Wide::Mode));
  // OK on 123 flips the page and stays on the key: a second OK flips back.
  g.flip();
  CHECK(g.page() == Page::Digits);
  CHECK(g.onMode());
  g.flip();
  CHECK(g.page() == Page::Letters);
  CHECK(g.onMode());
  g.flip();
  // Up from abc: Space across the row, then 0, then up the keypad to Speak.
  CHECK(g.move(Key::Up).moved);
  CHECK(g.at() == key(4, 0));
  CHECK(g.typed() == " ");
  CHECK(g.move(Key::Up).moved);
  CHECK(g.typed() == "0");
  for (const char* d : {"8", "5", "2"}) {
    CHECK(g.move(Key::Up).moved);
    CHECK(g.typed() == d);
  }
  CHECK(g.move(Key::Up).moved);
  CHECK(g.at().area == Area::Speak);
  CHECK(g.move(Key::Down).moved);
  CHECK(g.typed() == "3");
  // The keypad's edges: 3 6 9 ' on the right, 1 4 7 & on the left.
  CHECK(g.move(Key::Right).edge == Grid::Edge::Right);
  g.set(Grid::keyFor(Page::Digits, '7'));
  CHECK(g.move(Key::Left).edge == Grid::Edge::Left);
  // Space spans its row: both sides leave.
  g.set(Grid::keyFor(Page::Digits, ' '));
  CHECK(g.move(Key::Left).edge == Grid::Edge::Left);
  CHECK(g.move(Key::Right).edge == Grid::Edge::Right);
  // Down each column to its wide key; Up comes back over Space.
  g.set(Grid::keyFor(Page::Digits, '9'));
  for (int i = 0; i < 3; ++i) CHECK(g.move(Key::Down).moved);
  CHECK(g.at() == wide(search_keys::Wide::Clear));
  g.set(Grid::keyFor(Page::Digits, '&'));
  g.move(Key::Down);
  g.move(Key::Down);
  CHECK(g.at() == wide(search_keys::Wide::Delete));
  g.move(Key::Up);
  CHECK(g.move(Key::Up).moved);
  CHECK(g.typed() == "&");
  // Typing keeps the page (numbers are several digits).
  g.set(Grid::keyFor(Page::Digits, '2'));
  CHECK(g.typed() == "2");
  CHECK(g.page() == Page::Digits);
  // Flipping from a key goes to the one in its place; letters() goes back
  // (Clear, an empty field); home() is "a" on the letters.
  g.flip();
  CHECK(g.page() == Page::Letters);
  CHECK(g.typed() == "c");
  g.set(Grid::keyFor(Page::Letters, 'q'));
  g.flip();
  CHECK(g.typed() == "9");
  g.letters();
  CHECK(g.page() == Page::Letters);
  CHECK(g.typed() == "q");
  g.letters();
  CHECK(g.typed() == "q");
  g.flip();
  g.home();
  CHECK(g.page() == Page::Letters);
  CHECK(g.typed() == "a");
}

TEST_CASE("search keys: every key is one pointer target on its page") {
  using search_keys::spotOfTarget;
  using search_keys::targetOf;
  for (Page page : {Page::Letters, Page::Digits}) {
    std::set<int> seen;
    int keys = 0;
    for (int r = 0; r < search_keys::kRows; ++r)
      for (int c = 0; c < search_keys::colsOf(page); ++c) {
        char ch = Grid::keyAt(page, r, c);
        if (ch == ' ' && c > 0 && Grid::keyAt(page, r, c - 1) == ' ') continue;
        int t = targetOf(page, key(r, c));
        CHECK(seen.insert(t).second);
        Grid::Spot back;
        REQUIRE(spotOfTarget(page, t, back));
        CHECK(back == key(r, c));
        ++keys;
      }
    for (int w = 0; w < 3; ++w) {
      int t = targetOf(page, {Area::Bottom, 0, w});
      CHECK(seen.insert(t).second);
      Grid::Spot back;
      REQUIRE(spotOfTarget(page, t, back));
      CHECK(back == Grid::Spot{Area::Bottom, 0, w});
    }
    CHECK(keys == (page == Page::Letters ? 27 : 13));
    CHECK(*seen.rbegin() < search_keys::kTargets);
  }
  CHECK(targetOf(Page::Letters, Grid::keyFor(Page::Letters, 'a')) == 0);
  CHECK(targetOf(Page::Letters, Grid::keyFor(Page::Letters, ' ')) == 26);
  CHECK(targetOf(Page::Digits, Grid::keyFor(Page::Digits, '1')) == search_keys::kTargetPad);
  CHECK(targetOf(Page::Digits, Grid::keyFor(Page::Digits, ' ')) == search_keys::kTargetPad + 12);
  CHECK(targetOf(Page::Letters, wide(search_keys::Wide::Mode)) == search_keys::kTargetWide + 1);
  CHECK(targetOf(Page::Letters, {Area::Speak, 0, 0}) == -1);
  // Space's other cells, and the other page's keys, are no target.
  Grid::Spot s;
  CHECK_FALSE(spotOfTarget(Page::Letters, 27, s));
  CHECK_FALSE(spotOfTarget(Page::Letters, search_keys::kTargetPad, s));
  CHECK_FALSE(spotOfTarget(Page::Digits, 5, s));
  CHECK_FALSE(spotOfTarget(Page::Digits, search_keys::kTargetPad + 13, s));
}

TEST_CASE("search keys: a swipe stops at the page's edge, the next gesture crosses") {
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
  g.set(Grid::keyFor(Page::Letters, 'c'));
  CHECK(g.move(Key::Left, 9).moved);
  CHECK(g.move(Key::Left, 9).moved);
  CHECK(g.typed() == "a");
  CHECK(g.move(Key::Left, 9).edge == Grid::Edge::None);
  CHECK(g.move(Key::Left, 10).edge == Grid::Edge::Left);
  // A swipe along the last row stops on Space.
  g.set(Grid::keyFor(Page::Letters, 'y'));
  CHECK(g.move(Key::Right, 11).moved);
  CHECK(g.move(Key::Right, 11).moved);
  CHECK(g.typed() == " ");
  CHECK(g.move(Key::Right, 11).edge == Grid::Edge::None);
  // On 123 the edge is the keypad's: a swipe from 1 stops on 3.
  g.flip();
  g.set(Grid::keyFor(Page::Digits, '1'));
  CHECK(g.move(Key::Right, 12).moved);
  CHECK(g.move(Key::Right, 12).moved);
  CHECK(g.typed() == "3");
  CHECK(g.move(Key::Right, 12).edge == Grid::Edge::None);
  CHECK(g.typed() == "3");
  CHECK(g.move(Key::Right, 13).edge == Grid::Edge::Right);
  // Down the keypad, a swipe stops on the wide keys.
  g.set(Grid::keyFor(Page::Digits, '2'));
  for (int i = 0; i < 5; ++i) CHECK(g.move(Key::Down, 14).moved);
  CHECK(g.onMode());
  CHECK(g.move(Key::Down, 14).edge == Grid::Edge::None);
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
  // Apple TV: how to move and type, and Play/Pause for the other page;
  // click and hold for More.
  Bar apple = search(RemoteKeys::appleTv(), SearchPlace::Keyboard, true);
  REQUIRE(apple.keys.size() == 3);
  CHECK(apple.keys[0].cap == "Swipe or ring");
  CHECK(apple.keys[1].label == "Type");
  CHECK(apple.keys[2].icons == std::vector<std::string>{"key-playpause"});
  CHECK(apple.keys[2].label == "123");
  Bar keypad = search(RemoteKeys::appleTv(), SearchPlace::Keypad, true);
  REQUIRE(keypad.keys.size() == 3);
  CHECK(keypad.keys[2].label == "abc");
  CHECK(search(RemoteKeys::appleTv(), SearchPlace::Results, true).keys[0].cap == "Click and hold");
  // LG on the 123 page: the same as on the letters (the remote's digits type too).
  Bar lgPad = search(RemoteKeys::lg(), SearchPlace::Keypad, true);
  REQUIRE(lgPad.keys.size() == 2);
  CHECK(lgPad.keys[1].label == "Channel");
  CHECK(lgPad.right == lg.right);
  // LG's number box: plays when complete, or waits when the digits start a name.
  Bar box = search(RemoteKeys::lg(), SearchPlace::Digits, false, {"206", ""});
  CHECK(box.keys[0].label == "Watch now");
  CHECK(box.right == "Plays when the number is complete \xc2\xb7 Back: Cancel");
  Bar wait = search(RemoteKeys::lg(), SearchPlace::Digits, false, {"49", "49s"});
  CHECK(wait.keys[0].label == "Watch channel 49");
  CHECK(wait.keys[1].label == "The 49s");
  CHECK(wait.right == "Doesn't tune by itself \xc2\xb7 Back: Cancel");
  // A channel in a hidden group outside your lineup: only OK tunes it.
  Bar hidden = search(RemoteKeys::lg(), SearchPlace::Digits, false, {"2061", "", true});
  CHECK(hidden.keys[0].label == "Watch channel 2061");
  CHECK(hidden.right == "Doesn't tune by itself \xc2\xb7 Back: Cancel");
}
