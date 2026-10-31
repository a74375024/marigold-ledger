// Key hints per remote: the LG remote's colour keys exactly as before, the
// Fire TV remote's keys as designed, and what every remote has otherwise.
#include <doctest.h>

#include <string>

#include "core/input/hints.hpp"

using marigold::RemoteKeys;
using namespace marigold::hints;

namespace {
std::string labels(const Bar& b) {
  std::string out;
  for (const Hint& h : b.keys) out += (out.empty() ? "" : " | ") + h.label;
  return out;
}
}  // namespace

TEST_CASE("hints: the LG remote keeps its colour keys") {
  Bar g = guide(RemoteKeys::lg(), false);
  CHECK(labels(g) == "Sources | Add to My Lineup | Live Events | Settings");
  CHECK(g.keys[0].colour == Colour::Red);
  CHECK(g.keys[3].colour == Colour::Blue);
  // Only the line at the right changed with the left rail (Build 112).
  CHECK(g.right == "\xe2\x97\x80 Neighborhoods \xc2\xb7 Back: Menu \xc2\xb7 Hold OK: more");
  CHECK(guide(RemoteKeys::lg(), false, Rail::NoColumn).right == "\xe2\x97\x80 Menu \xc2\xb7 Back: Menu \xc2\xb7 Hold OK: more");
  CHECK(guide(RemoteKeys::lg(), false, Rail::Column).right == "OK or \xe2\x96\xb6 Go \xc2\xb7 Back: Stay \xc2\xb7 \xe2\x97\x80 Menu");
  CHECK(labels(guide(RemoteKeys::lg(), false, Rail::Column)) == labels(g));
  CHECK(guide(RemoteKeys::lg(), true).keys[1].label == "Remove from My Lineup");
  Bar e = eventsBoard(RemoteKeys::lg());
  CHECK(labels(e) == "Where to Watch | Guide | Settings");
  CHECK(e.right == "\xe2\x97\x80 Sports \xc2\xb7 Back: Menu \xc2\xb7 Hold OK: more");
  CHECK(eventsBoard(RemoteKeys::lg(), Rail::Column).right == "OK or \xe2\x96\xb6 Go \xc2\xb7 Back: Stay \xc2\xb7 \xe2\x97\x80 Menu");
  CHECK(playerSources(RemoteKeys::lg()).colour == Colour::Red);
  REQUIRE(playerOptions(RemoteKeys::lg()));
  CHECK(playerOptions(RemoteKeys::lg())->label == "Settings");
  auto tips = gettingAround(RemoteKeys::lg());
  REQUIRE(tips.size() == 6);
  CHECK(tips[3].title == "Subtitles & more");
  CHECK(tips[4].key.colour == Colour::Green);
  CHECK(tips[5].title == "Number keys");
}

TEST_CASE("hints: a Fire TV remote gets Menu, play/pause and rewind/fast forward") {
  RemoteKeys k = RemoteKeys::fireTv();
  Bar g = guide(k, false);
  CHECK(labels(g) == "Channel options | Live Events | Page");
  CHECK(g.keys[0].icons == std::vector<std::string>{"key-menu"});
  CHECK(g.keys[1].icons == std::vector<std::string>{"key-playpause"});
  CHECK(g.keys[2].icons == std::vector<std::string>{"key-rewind", "key-forward"});
  CHECK(g.right == "\xe2\x97\x80 Neighborhoods \xc2\xb7 Back: Menu");
  for (const Hint& h : g.keys) CHECK(h.colour == Colour::None);
  Bar e = eventsBoard(k);
  CHECK(labels(e) == "Where to Watch | Guide");
  CHECK(e.keys[1].icons == std::vector<std::string>{"key-playpause"});
  CHECK(playerSources(k).cap == "OK");  // a button in the controls
  CHECK_FALSE(playerOptions(k));
  REQUIRE(pickAnotherSource(k));
  CHECK(pickAnotherSource(k)->icons == std::vector<std::string>{"key-menu"});
  auto tips = gettingAround(k);
  REQUIRE(tips.size() == 5);
  CHECK(tips[2].detail == "Up or Down, always");
  CHECK(tips[4].title == "Back");
}

TEST_CASE("hints: any other remote is shown only what every remote has") {
  RemoteKeys k;  // a Chromecast-style remote: D-pad, OK, Back, Home
  Bar g = guide(k, false);
  REQUIRE(g.keys.size() == 1);
  CHECK(g.keys[0].cap == "Hold OK");
  CHECK(g.keys[0].label == "Channel options");
  CHECK(g.right == "\xe2\x97\x80 Neighborhoods \xc2\xb7 Back: Menu");
  Bar e = eventsBoard(k);
  CHECK(e.keys.empty());  // Back opens the menu, where My Lineup is
  CHECK(e.right == "\xe2\x97\x80 Sports \xc2\xb7 Back: Menu \xc2\xb7 Hold OK: more");
  CHECK(playerSources(k).cap == "OK");
  CHECK_FALSE(pickAnotherSource(k));
  CHECK(gettingAround(k)[4].detail == "Closes what's on screen; again to leave");

  // Pressing Menu once shows that it has one.
  k.saw(marigold::Key::Menu);
  CHECK(guide(k, false).keys[0].icons == std::vector<std::string>{"key-menu"});
  CHECK(eventsBoard(k).keys[0].label == "Where to Watch");
}

TEST_CASE("hints: the Siri Remote gets hold OK and play/pause") {
  RemoteKeys k = RemoteKeys::appleTv();
  Bar g = guide(k, false);
  CHECK(labels(g) == "Channel options | Live Events");
  CHECK(g.keys[0].cap == "Hold OK");
  CHECK(g.keys[1].icons == std::vector<std::string>{"key-playpause"});
  CHECK(g.right == "\xe2\x97\x80 Neighborhoods \xc2\xb7 Back: Menu");
  CHECK(guide(k, false, Rail::Column).right.rfind("Click or ", 0) == 0);
  Bar e = eventsBoard(k);
  CHECK(labels(e) == "Guide");
  CHECK(e.keys[0].icons == std::vector<std::string>{"key-playpause"});
  CHECK(playerSources(k).cap == "Click");
  CHECK_FALSE(pickAnotherSource(k));
  CHECK(gettingAround(k)[3].key.cap == "Click");
}

TEST_CASE("hints: All settings, on the categories and inside one") {
  Bar cats = marigold::hints::settings(RemoteKeys::lg(), false, false);
  CHECK(labels(cats) == "Move | Open | Close Settings");
  CHECK(cats.right == "Point and click works too");
  CHECK(marigold::hints::settings(RemoteKeys::fireTv(), false, false).right.empty());
  Bar rows = marigold::hints::settings(RemoteKeys::lg(), true, true);
  CHECK(labels(rows) == "Change | Select | Categories");
  CHECK(rows.right == "Changes save as you go");
  Bar apple = marigold::hints::settings(RemoteKeys::appleTv(), true, false);
  CHECK(labels(apple) == "Select | Categories");
  CHECK(apple.keys[0].cap == "Click");
}

TEST_CASE("hints: setup screens use each remote's own keys") {
  using marigold::RemoteKeys;
  auto lg = marigold::hints::setup(RemoteKeys::lg(), true);
  REQUIRE(lg.keys.size() == 3);
  CHECK(lg.keys[0].cap == "OK");
  CHECK(lg.keys[2].cap.rfind("CH", 0) == 0);
  auto fire = marigold::hints::setup(RemoteKeys::fireTv(), true);
  REQUIRE(fire.keys.size() == 3);
  CHECK(fire.keys[2].icons == std::vector<std::string>{"key-rewind", "key-forward"});
  auto apple = marigold::hints::setup(RemoteKeys::appleTv(), true);
  REQUIRE(apple.keys.size() == 2);
  CHECK(apple.keys[0].cap == "Click");
  CHECK(apple.right == "Swipe to move faster");
  CHECK(marigold::hints::setup(RemoteKeys::fireTv(), false).keys.size() == 2);
  CHECK(marigold::hints::setup(RemoteKeys(), true).keys.size() == 2);
}
