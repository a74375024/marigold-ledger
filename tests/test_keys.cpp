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
  CHECK(keyFromAndroid(165) == Key::Info);      // the info banner
  CHECK(keyFromAndroid(172) == Key::Guide);     // the in-player guide
  CHECK(keyFromAndroid(175) == Key::Captions);  // CC: subtitles on or off
  CHECK(keyFromAndroid(183) == Key::Red);
  CHECK(keyFromAndroid(186) == Key::Blue);
  CHECK(keyFromAndroid(7) == Key::Digit0);
  CHECK(keyFromAndroid(16) == Key::Digit9);
  CHECK(keyFromAndroid(145) == Key::Digit1);
  CHECK(keyFromAndroid(96) == Key::Ok);    // a game controller's A
  CHECK(keyFromAndroid(97) == Key::Back);  // and B
  CHECK(keyFromAndroid(3) == Key::Unknown);  // Home stays the system's
}

TEST_CASE("a remote's keys: what the hints may offer, learned as they are pressed") {
  using marigold::RemoteKeys;
  RemoteKeys fire = RemoteKeys::fireTv();
  CHECK(fire.menu);
  CHECK(fire.playPause);
  CHECK(fire.seek);
  CHECK_FALSE(fire.colour);
  CHECK_FALSE(fire.digits);
  CHECK_FALSE(fire.channel);
  RemoteKeys lg = RemoteKeys::lg();
  CHECK((lg.colour && lg.digits && lg.channel && lg.menu));

  // Any other stick starts with none; a key pressed once is offered after.
  RemoteKeys stick;
  CHECK_FALSE(stick.saw(Key::Up));
  CHECK_FALSE(stick.saw(Key::Ok));
  CHECK(stick.saw(Key::Menu));
  CHECK(stick.menu);
  CHECK_FALSE(stick.saw(Key::Menu));  // not news the second time
  CHECK(stick.saw(Key::FastForward));
  CHECK(stick.seek);
  CHECK(stick.saw(Key::Digit7));
  CHECK(stick.digits);
  CHECK(stick.saw(Key::Blue));
  CHECK(stick.colour);
  CHECK(stick.saw(Key::Pause));
  CHECK(stick.playPause);
  CHECK(stick.saw(Key::ChannelDown));
  CHECK(stick.channel);
  CHECK(stick == RemoteKeys::lg());
}

TEST_CASE("keys: Apple TV press types") {
  using marigold::keyFromTvos;
  CHECK(keyFromTvos(0) == Key::Up);
  CHECK(keyFromTvos(1) == Key::Down);
  CHECK(keyFromTvos(2) == Key::Left);
  CHECK(keyFromTvos(3) == Key::Right);
  CHECK(keyFromTvos(4) == Key::Ok);
  CHECK(keyFromTvos(5) == Key::Back);
  CHECK(keyFromTvos(6) == Key::PlayPause);
  CHECK(keyFromTvos(30) == Key::ChannelUp);
  CHECK(keyFromTvos(31) == Key::ChannelDown);
  CHECK(keyFromTvos(32) == Key::Unknown);  // the 123 overlay button: the system's
  marigold::RemoteKeys k = marigold::RemoteKeys::appleTv();
  CHECK(k.playPause);
  CHECK_FALSE(k.menu);
  CHECK_FALSE(k.colour);
}

TEST_CASE("keys: a Mac's keyboard") {
  using marigold::keyFromMac;
  using marigold::keyFromMacChar;
  CHECK(keyFromMac(0x7E) == Key::Up);
  CHECK(keyFromMac(0x7D) == Key::Down);
  CHECK(keyFromMac(0x7B) == Key::Left);
  CHECK(keyFromMac(0x7C) == Key::Right);
  CHECK(keyFromMac(0x24) == Key::Ok);  // return
  CHECK(keyFromMac(0x4C) == Key::Ok);  // enter
  CHECK(keyFromMac(0x35) == Key::Back);
  CHECK(keyFromMac(0x74) == Key::ChannelUp);
  CHECK(keyFromMac(0x79) == Key::ChannelDown);
  CHECK(keyFromMac(0x00) == Key::Unknown);  // A types instead
  CHECK(keyFromMac(0x31) == Key::Unknown);  // space types instead
  // Typed characters nothing took as text.
  CHECK(keyFromMacChar("7") == Key::Digit7);
  CHECK(keyFromMacChar("0") == Key::Digit0);
  CHECK(keyFromMacChar(" ") == Key::PlayPause);
  CHECK(keyFromMacChar("\b") == Key::Back);
  CHECK(keyFromMacChar("i") == Key::Info);
  CHECK(keyFromMacChar("C") == Key::Captions);
  CHECK(keyFromMacChar("x") == Key::Unknown);
  CHECK(keyFromMacChar("\xc3\xa9") == Key::Unknown);  // é
  CHECK(keyFromMacChar("") == Key::Unknown);
  marigold::RemoteKeys k = marigold::RemoteKeys::mac();
  CHECK(k.playPause);
  CHECK(k.digits);
  CHECK(k.typing);
  CHECK_FALSE(k.clickpad);
  CHECK_FALSE(k.colour);
  CHECK_FALSE(k == marigold::RemoteKeys::appleTv());
}
