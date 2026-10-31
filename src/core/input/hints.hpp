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

// Where the focus is on the guide or Live Events, for the line at the right
// of their bars (the left rail, Build 112): the content of a section with a
// neighborhood column (SPORTS on Live Events), the content of one without
// (Recent), or the column itself.
enum class Rail { Content, NoColumn, Column };

// The guide's bottom bar.
Bar guide(const RemoteKeys& k, bool inLineup, Rail rail = Rail::Content);
// Live Events' bottom bar.
Bar eventsBoard(const RemoteKeys& k, Rail rail = Rail::Content);

// Search (Build 113, 115): on its keyboard's letters or 123 page
// (`results`: something is on the right to choose), in the results, or
// LG's number box from an empty field (`digits.team`: a name the digits
// also start; `digits.waits`: only OK tunes, as for a channel in a hidden
// group).
enum class SearchPlace { Keyboard, Keypad, Results, Digits };
struct SearchDigits {
  std::string number, team;
  bool waits = false;
};
Bar search(const RemoteKeys& k, SearchPlace place, bool results, const SearchDigits& digits = {});

// The setup screens (first run, My Lineup, My Sports): how to pick and go
// back, and on A-to-Z lists the keys that jump a letter, if the remote has
// any (CH + / CH - on LG, rewind / fast forward on Fire TV; the Siri Remote
// swipes instead).
Bar setup(const RemoteKeys& k, bool letters);

// All settings: moving through the categories, or inside one (with
// "◀▶ Change" on a row of choices). LG's pointer is mentioned on the
// categories.
Bar settings(const RemoteKeys& k, bool inRows, bool choiceRow);

// The player: the Sources key (Red on LG; elsewhere a button in the
// controls OK brings up), Quick settings (only a colour key; its label is
// "Settings"), and "pick another source" while a feed opens (nothing without
// either: the controls aren't there yet).
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
