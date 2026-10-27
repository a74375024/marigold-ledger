// Remote-control keys, independent of the platform.
//
// The LG remote sends standard SDL scancodes for arrows, OK, digits and media
// keys, and webOS-only scancodes (SDL_webOS.h) for Back, CH+/CH-, the colour
// keys and the Magic Remote cursor. A Fire TV or Android TV remote sends
// Android key codes (KeyEvent.KEYCODE_*); an Apple TV, UIKit press types.
// Platform code passes the raw numbers here so the mapping can be unit
// tested on the host.
#pragma once

#include <cstdint>

namespace marigold {

enum class Key : uint8_t {
  Unknown,
  Up, Down, Left, Right, Ok, Back, Exit,
  ChannelUp, ChannelDown,
  Red, Green, Yellow, Blue,
  Play, Pause, PlayPause, Stop, Rewind, FastForward,
  Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
  Info, Menu,
  CursorShown, CursorHidden,
};

// webOS scancodes from SDL_webOS.h.
namespace webos_scancode {
constexpr int kChannelUp = 480;
constexpr int kChannelDown = 481;
constexpr int kBack = 482;
constexpr int kCursorShow = 484;
constexpr int kCursorHide = 485;
constexpr int kRed = 486;
constexpr int kGreen = 487;
constexpr int kYellow = 488;
constexpr int kBlue = 489;
constexpr int kExit = 505;
}  // namespace webos_scancode

// `scancode` is SDL_Scancode as an int; `keycode` is SDL_Keycode (used for
// desktop keyboards, where Escape and Backspace stand in for Back).
Key keyFromSdl(int scancode, int32_t keycode);
// `keycode` is android.view.KeyEvent's KEYCODE_* value.
Key keyFromAndroid(int keycode);
// `pressType` is UIPressType's raw value (the Siri Remote's clickpad
// directions and click, Back, play/pause; a TV remote's page keys over HDMI).
Key keyFromTvos(int pressType);

// The keys a remote has beyond the D-pad, OK and Back, which every remote
// has. Screens offer only these in their key hints, so a Fire TV remote
// (no colour, number or CH keys) is never told to press one it lacks.
struct RemoteKeys {
  bool colour = false;     // red, green, yellow, blue
  bool menu = false;       // ☰
  bool playPause = false;  // ⏯
  bool seek = false;       // ⏪ ⏩
  bool digits = false;     // 0-9
  bool channel = false;    // CH + / CH -
  // The Siri Remote: a clickpad that is clicked (not an OK key) and swiped.
  bool clickpad = false;
  // A key just pressed: this remote has it, so it is offered from now on.
  // True when that is news.
  bool saw(Key key);
  static RemoteKeys lg();      // the LG remote: all of them
  static RemoteKeys fireTv();  // menu, play/pause, rewind and fast forward
  static RemoteKeys appleTv();  // the Siri Remote: play/pause
  bool operator==(const RemoteKeys& o) const {
    return colour == o.colour && menu == o.menu && playPause == o.playPause && seek == o.seek && digits == o.digits &&
           channel == o.channel && clickpad == o.clickpad;
  }
};

int digitOf(Key key);  // 0-9, or -1
const char* keyName(Key key);

}  // namespace marigold
