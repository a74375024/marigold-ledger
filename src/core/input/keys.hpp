// Remote-control keys, independent of SDL.
//
// The LG remote sends standard SDL scancodes for arrows, OK, digits and media
// keys, and webOS-only scancodes (SDL_webOS.h) for Back, CH+/CH-, the colour
// keys and the Magic Remote cursor. Platform code passes the raw numbers here
// so the mapping can be unit tested on the host.
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

int digitOf(Key key);  // 0-9, or -1
const char* keyName(Key key);

}  // namespace marigold
