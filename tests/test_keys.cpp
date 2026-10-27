#include "core/input/keys.hpp"

#include <string>

#include "doctest.h"

using marigold::Key;
using marigold::keyFromSdl;

TEST_CASE("webOS remote scancodes") {
  CHECK(keyFromSdl(482, 0) == Key::Back);
  CHECK(keyFromSdl(480, 0) == Key::ChannelUp);
  CHECK(keyFromSdl(481, 0) == Key::ChannelDown);
  CHECK(keyFromSdl(486, 0) == Key::Red);
  CHECK(keyFromSdl(487, 0) == Key::Green);
  CHECK(keyFromSdl(488, 0) == Key::Yellow);
  CHECK(keyFromSdl(489, 0) == Key::Blue);
  CHECK(keyFromSdl(484, 0) == Key::CursorShown);
  CHECK(keyFromSdl(485, 0) == Key::CursorHidden);
}

TEST_CASE("arrows, OK and digits") {
  CHECK(keyFromSdl(82, 0) == Key::Up);
  CHECK(keyFromSdl(81, 0) == Key::Down);
  CHECK(keyFromSdl(80, 0) == Key::Left);
  CHECK(keyFromSdl(79, 0) == Key::Right);
  CHECK(keyFromSdl(40, 0) == Key::Ok);
  CHECK(keyFromSdl(30, 0) == Key::Digit1);
  CHECK(keyFromSdl(39, 0) == Key::Digit0);
  CHECK(marigold::digitOf(keyFromSdl(38, 0)) == 9);
  CHECK(marigold::digitOf(Key::Ok) == -1);
}

TEST_CASE("unknown keys stay unknown") {
  CHECK(keyFromSdl(9999, 0) == Key::Unknown);
  CHECK(std::string(marigold::keyName(Key::Unknown)) == "Unknown");
}

TEST_CASE("Fire TV and Android TV remotes") {
  using marigold::keyFromAndroid;
  CHECK(keyFromAndroid(19) == Key::Up);
  CHECK(keyFromAndroid(20) == Key::Down);
  CHECK(keyFromAndroid(21) == Key::Left);
  CHECK(keyFromAndroid(22) == Key::Right);
  CHECK(keyFromAndroid(23) == Key::Ok);  // the D-pad's centre
  CHECK(keyFromAndroid(66) == Key::Ok);
  CHECK(keyFromAndroid(4) == Key::Back);
  CHECK(keyFromAndroid(82) == Key::Menu);
  CHECK(keyFromAndroid(85) == Key::PlayPause);
  CHECK(keyFromAndroid(89) == Key::Rewind);
  CHECK(keyFromAndroid(90) == Key::FastForward);
  CHECK(keyFromAndroid(166) == Key::ChannelUp);
  CHECK(keyFromAndroid(167) == Key::ChannelDown);
  CHECK(keyFromAndroid(183) == Key::Red);
  CHECK(keyFromAndroid(186) == Key::Blue);
  CHECK(keyFromAndroid(7) == Key::Digit0);
  CHECK(keyFromAndroid(16) == Key::Digit9);
  CHECK(keyFromAndroid(145) == Key::Digit1);
  CHECK(keyFromAndroid(96) == Key::Ok);    // a game controller's A
  CHECK(keyFromAndroid(97) == Key::Back);  // and B
  CHECK(keyFromAndroid(3) == Key::Unknown);  // Home stays the system's
}
