// The key hints on screen (the guide's and Live Events' bottom bars, the
// player's key guide and cards), worked out from the keys the remote in use
// has (RemoteKeys). The LG remote gets its colour keys, as before; a Fire
// TV remote gets Menu, play/pause and rewind/fast forward; any other remote
// gets only what every remote has (hold OK, the D-pad, Back) until it shows
// it has more. Every action keeps a way in with the D-pad alone.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/input/keys.hpp"

namespace marigold::hints {

enum class Colour { None, Red, Green, Yellow, Blue };

// One key: a colour key, or a key cap with symbols (icon names: "key-menu",
// "key-playpause", "key-rewind", "key-forward") or text ("OK", "Back",
// "Hold OK", "▲").
struct Hint {
  Colour colour = Colour::None;
  std::vector<std::string> icons;
  std::string cap;
  std::string label;
  bool operator==(const Hint& o) const {
    return colour == o.colour && icons == o.icons && cap == o.cap && label == o.label;
  }
};

struct Bar {
  std::vector<Hint> keys;
  std::string right;  // the line at the right end
};

// The guide's bottom bar.
Bar guide(const RemoteKeys& k, bool inLineup);
// Live Events' bottom bar.
Bar eventsBoard(const RemoteKeys& k);

// The setup screens (first run, My Lineup, My Sports): how to pick and go
// back, and on A-to-Z lists the keys that jump a letter, if the remote has
// any (CH + / CH - on LG, rewind / fast forward on Fire TV; the Siri Remote
// swipes instead).
Bar setup(const RemoteKeys& k, bool letters);

// The player: the Sources key (the action row, which opens on Sources, is
// the way in without a colour key or Menu), Options (only a colour key), and
// "pick another source" while a feed opens (nothing without either: the row
// isn't there yet).
Hint playerSources(const RemoteKeys& k);
std::optional<Hint> playerOptions(const RemoteKeys& k);
std::optional<Hint> pickAnotherSource(const RemoteKeys& k);

// The one-time "Getting around" card: six keys.
struct Tip {
  Hint key;
  std::string title, detail;
};
std::vector<Tip> gettingAround(const RemoteKeys& k);

}  // namespace marigold::hints
