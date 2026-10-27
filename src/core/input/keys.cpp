#include "core/input/keys.hpp"

namespace marigold {
namespace {

// Standard SDL_Scancode values (SDL_scancode.h, stable since SDL 2.0.0).
// Copied as numbers so core/ does not include SDL.
namespace sdl {
constexpr int kReturn = 40, kEscape = 41, kBackspace = 42, kSpace = 44;
constexpr int k1 = 30, k0 = 39;
constexpr int kRight = 79, kLeft = 80, kDown = 81, kUp = 82;
constexpr int kKp1 = 89, kKp0 = 98, kKpEnter = 88;
constexpr int kPageUp = 75, kPageDown = 78;
constexpr int kMenu = 118, kAcBack = 270;
constexpr int kAudioNext = 258, kAudioPrev = 259, kAudioStop = 260, kAudioPlay = 261;
constexpr int kAudioRewind = 285, kAudioFastForward = 286;  // SDL 2.0.6+, harmless if unused
constexpr int kF1 = 58;
}  // namespace sdl

// android.view.KeyEvent key codes (stable since API 1-21).
namespace android {
constexpr int kBack = 4, k0 = 7, k9 = 16, kUp = 19, kDown = 20, kLeft = 21, kRight = 22, kCenter = 23;
constexpr int kSpace = 62, kEnter = 66, kDel = 67, kMenu = 82, kPlayPause = 85, kStop = 86, kNext = 87;
constexpr int kPrevious = 88, kRewind = 89, kFastForward = 90, kPageUp = 92, kPageDown = 93;
constexpr int kButtonA = 96, kButtonB = 97, kEscape = 111, kPlay = 126, kPause = 127;
constexpr int kNumpad0 = 144, kNumpad9 = 153, kNumpadEnter = 160, kInfo = 165, kChannelUp = 166;
constexpr int kChannelDown = 167, kGuide = 172, kRed = 183, kGreen = 184, kYellow = 185, kBlue = 186;
}  // namespace android

// SDL_Keycode values that only desktop keyboards send.
constexpr int32_t kKeycodeEscape = 27, kKeycodeBackspace = 8;

}  // namespace

Key keyFromSdl(int scancode, int32_t keycode) {
  using namespace webos_scancode;
  switch (scancode) {
    case kChannelUp: return Key::ChannelUp;
    case kChannelDown: return Key::ChannelDown;
    case kBack: return Key::Back;
    case kExit: return Key::Exit;
    case kCursorShow: return Key::CursorShown;
    case kCursorHide: return Key::CursorHidden;
    case kRed: return Key::Red;
    case kGreen: return Key::Green;
    case kYellow: return Key::Yellow;
    case kBlue: return Key::Blue;
    case sdl::kUp: return Key::Up;
    case sdl::kDown: return Key::Down;
    case sdl::kLeft: return Key::Left;
    case sdl::kRight: return Key::Right;
    case sdl::kReturn:
    case sdl::kKpEnter: return Key::Ok;
    case sdl::kEscape:
    case sdl::kAcBack:
    case sdl::kBackspace: return Key::Back;
    case sdl::kPageUp: return Key::ChannelUp;
    case sdl::kPageDown: return Key::ChannelDown;
    case sdl::kAudioPlay: return Key::PlayPause;
    case sdl::kSpace: return Key::PlayPause;
    case sdl::kAudioStop: return Key::Stop;
    case sdl::kAudioRewind:
    case sdl::kAudioPrev: return Key::Rewind;
    case sdl::kAudioFastForward:
    case sdl::kAudioNext: return Key::FastForward;
    case sdl::kMenu: return Key::Menu;
    case sdl::kF1: return Key::Info;
    default: break;
  }
  if (scancode >= sdl::k1 && scancode <= sdl::k0) {
    int d = scancode == sdl::k0 ? 0 : scancode - sdl::k1 + 1;
    return static_cast<Key>(static_cast<int>(Key::Digit0) + d);
  }
  if (scancode >= sdl::kKp1 && scancode <= sdl::kKp0) {
    int d = scancode == sdl::kKp0 ? 0 : scancode - sdl::kKp1 + 1;
    return static_cast<Key>(static_cast<int>(Key::Digit0) + d);
  }
  if (keycode == kKeycodeEscape || keycode == kKeycodeBackspace) return Key::Back;
  return Key::Unknown;
}

Key keyFromAndroid(int keycode) {
  using namespace android;
  switch (keycode) {
    case kUp: return Key::Up;
    case kDown: return Key::Down;
    case kLeft: return Key::Left;
    case kRight: return Key::Right;
    case kCenter:
    case kEnter:
    case kNumpadEnter:
    case kButtonA: return Key::Ok;
    case kBack:
    case kEscape:
    case kDel:
    case kButtonB: return Key::Back;
    case kChannelUp:
    case kPageUp: return Key::ChannelUp;
    case kChannelDown:
    case kPageDown: return Key::ChannelDown;
    case kPlayPause:
    case kSpace: return Key::PlayPause;
    case kPlay: return Key::Play;
    case kPause: return Key::Pause;
    case kStop: return Key::Stop;
    case kRewind:
    case kPrevious: return Key::Rewind;
    case kFastForward:
    case kNext: return Key::FastForward;
    case kMenu: return Key::Menu;
    case kInfo:
    case kGuide: return Key::Info;
    case kRed: return Key::Red;
    case kGreen: return Key::Green;
    case kYellow: return Key::Yellow;
    case kBlue: return Key::Blue;
    default: break;
  }
  if (keycode >= k0 && keycode <= k9) return static_cast<Key>(static_cast<int>(Key::Digit0) + keycode - k0);
  if (keycode >= kNumpad0 && keycode <= kNumpad9)
    return static_cast<Key>(static_cast<int>(Key::Digit0) + keycode - kNumpad0);
  return Key::Unknown;
}

Key keyFromTvos(int pressType) {
  switch (pressType) {
    case 0: return Key::Up;           // UIPressTypeUpArrow
    case 1: return Key::Down;
    case 2: return Key::Left;
    case 3: return Key::Right;
    case 4: return Key::Ok;           // select: a click
    case 5: return Key::Back;         // menu: the Back button
    case 6: return Key::PlayPause;
    case 30: return Key::ChannelUp;   // pageUp (a TV remote's CH +)
    case 31: return Key::ChannelDown;
    default: return Key::Unknown;
  }
}

bool RemoteKeys::saw(Key key) {
  bool* has = nullptr;
  switch (key) {
    case Key::Red:
    case Key::Green:
    case Key::Yellow:
    case Key::Blue: has = &colour; break;
    case Key::Menu: has = &menu; break;
    case Key::PlayPause:
    case Key::Play:
    case Key::Pause: has = &playPause; break;
    case Key::Rewind:
    case Key::FastForward: has = &seek; break;
    case Key::ChannelUp:
    case Key::ChannelDown: has = &channel; break;
    default:
      if (digitOf(key) >= 0) has = &digits;
      break;
  }
  if (!has || *has) return false;
  *has = true;
  return true;
}

RemoteKeys RemoteKeys::lg() {
  RemoteKeys k;
  k.colour = k.menu = k.playPause = k.seek = k.digits = k.channel = true;
  return k;
}

RemoteKeys RemoteKeys::fireTv() {
  RemoteKeys k;
  k.menu = k.playPause = k.seek = true;
  return k;
}

// Its Menu button is Back; it has no media keys but play/pause.
RemoteKeys RemoteKeys::appleTv() {
  RemoteKeys k;
  k.playPause = true;
  return k;
}

int digitOf(Key key) {
  int k = static_cast<int>(key);
  int d0 = static_cast<int>(Key::Digit0);
  return k >= d0 && k <= d0 + 9 ? k - d0 : -1;
}

const char* keyName(Key key) {
  switch (key) {
    case Key::Unknown: return "Unknown";
    case Key::Up: return "Up";
    case Key::Down: return "Down";
    case Key::Left: return "Left";
    case Key::Right: return "Right";
    case Key::Ok: return "OK";
    case Key::Back: return "Back";
    case Key::Exit: return "Exit";
    case Key::ChannelUp: return "CH+";
    case Key::ChannelDown: return "CH-";
    case Key::Red: return "Red";
    case Key::Green: return "Green";
    case Key::Yellow: return "Yellow";
    case Key::Blue: return "Blue";
    case Key::Play: return "Play";
    case Key::Pause: return "Pause";
    case Key::PlayPause: return "Play/Pause";
    case Key::Stop: return "Stop";
    case Key::Rewind: return "Rewind";
    case Key::FastForward: return "Fast forward";
    case Key::Digit0: return "0";
    case Key::Digit1: return "1";
    case Key::Digit2: return "2";
    case Key::Digit3: return "3";
    case Key::Digit4: return "4";
    case Key::Digit5: return "5";
    case Key::Digit6: return "6";
    case Key::Digit7: return "7";
    case Key::Digit8: return "8";
    case Key::Digit9: return "9";
    case Key::Info: return "Info";
    case Key::Menu: return "Menu";
    case Key::CursorShown: return "Cursor shown";
    case Key::CursorHidden: return "Cursor hidden";
  }
  return "Unknown";
}

}  // namespace marigold
