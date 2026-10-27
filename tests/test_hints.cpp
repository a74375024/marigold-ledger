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
  CHECK(labels(g) == "Sources | Add to My Lineup | Live Events | Options");
  CHECK(g.keys[0].colour == Colour::Red);
  CHECK(g.keys[3].colour == Colour::Blue);
  CHECK(g.right == "CH \xe2\x96\xb2\xe2\x96\xbc page \xc2\xb7 0\xe2\x80\x93" "9 go to channel \xc2\xb7 Hold OK: more");
  CHECK(guide(RemoteKeys::lg(), true).keys[1].label == "Remove from My Lineup");
  Bar e = eventsBoard(RemoteKeys::lg());
  CHECK(labels(e) == "Where to Watch | Guide | Options");
  CHECK(e.right == "Hold OK: more \xc2\xb7 \xe2\x97\x80 sports");
  CHECK(playerSources(RemoteKeys::lg()).colour == Colour::Red);
  CHECK(playerOptions(RemoteKeys::lg()));
  auto tips = gettingAround(RemoteKeys::lg());
  REQUIRE(tips.size() == 6);
  CHECK(tips[4].key.colour == Colour::Red);
  CHECK(tips[5].title == "Number keys");
}

TEST_CASE("hints: a Fire TV remote gets Menu, play/pause and rewind/fast forward") {
  RemoteKeys k = RemoteKeys::fireTv();
  Bar g = guide(k, false);
  CHECK(labels(g) == "Channel options | Live Events | Page");
  CHECK(g.keys[0].icons == std::vector<std::string>{"key-menu"});
  CHECK(g.keys[1].icons == std::vector<std::string>{"key-playpause"});
  CHECK(g.keys[2].icons == std::vector<std::string>{"key-rewind", "key-forward"});
  CHECK(g.right == "\xe2\x96\xb2 Search & Options");
  for (const Hint& h : g.keys) CHECK(h.colour == Colour::None);
  Bar e = eventsBoard(k);
  CHECK(labels(e) == "Where to Watch | Guide");
  CHECK(e.keys[1].icons == std::vector<std::string>{"key-playpause"});
  CHECK(playerSources(k).icons == std::vector<std::string>{"key-menu"});
  CHECK_FALSE(playerOptions(k));
  REQUIRE(pickAnotherSource(k));
  CHECK(pickAnotherSource(k)->icons == std::vector<std::string>{"key-menu"});
  auto tips = gettingAround(k);
  REQUIRE(tips.size() == 6);
  CHECK(tips[2].detail == "Up or Down");
  CHECK(tips[4].key.icons == std::vector<std::string>{"key-menu"});
  CHECK(tips[5].title == "Back");
}

TEST_CASE("hints: any other remote is shown only what every remote has") {
  RemoteKeys k;  // a Chromecast-style remote: D-pad, OK, Back, Home
  Bar g = guide(k, false);
  REQUIRE(g.keys.size() == 1);
  CHECK(g.keys[0].cap == "Hold OK");
  CHECK(g.keys[0].label == "Channel options");
  CHECK(g.right == "\xe2\x96\xb2 Live Events, Search & Options");
  Bar e = eventsBoard(k);
  REQUIRE(e.keys.size() == 1);
  CHECK(e.keys[0].cap == "Back");
  CHECK(playerSources(k).cap == "\xe2\x96\xbc");  // the action row opens on Sources
  CHECK_FALSE(pickAnotherSource(k));
  CHECK(gettingAround(k)[4].detail == "Down, then OK: another feed of this channel");

  // Pressing Menu once shows that it has one.
  k.saw(marigold::Key::Menu);
  CHECK(guide(k, false).keys[0].icons == std::vector<std::string>{"key-menu"});
  CHECK(eventsBoard(k).keys[0].label == "Where to Watch");
}
