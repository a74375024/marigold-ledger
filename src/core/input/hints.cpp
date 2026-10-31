#include "core/input/hints.hpp"

namespace marigold::hints {
namespace {

const char* const kUp = "\xe2\x96\xb2";     // ▲
const char* const kDown = "\xe2\x96\xbc";   // ▼
const char* const kLeft = "\xe2\x97\x80";   // ◀
const char* const kRight = "\xe2\x96\xb6";  // ▶

Hint colour(Colour c, std::string label) {
  Hint h;
  h.colour = c;
  h.label = std::move(label);
  return h;
}

Hint icons(std::vector<std::string> names, std::string label) {
  Hint h;
  h.icons = std::move(names);
  h.label = std::move(label);
  return h;
}

Hint cap(std::string text, std::string label) {
  Hint h;
  h.cap = std::move(text);
  h.label = std::move(label);
  return h;
}

// The rail's line: the way to the left and what Back does (the colour keys,
// OK and Hold OK do what they did before the rail).
std::string railLine(const RemoteKeys& k, Rail rail, const char* column) {
  const char* dot = " \xc2\xb7 ";
  if (rail == Rail::Column)
    return std::string(k.clickpad ? "Click" : "OK") + " or " + kRight + " Go" + dot + "Back: Stay" + dot + kLeft + " Menu";
  return std::string(kLeft) + " " + (rail == Rail::NoColumn ? "Menu" : column) + dot + "Back: Menu";
}

}  // namespace

Bar guide(const RemoteKeys& k, bool inLineup, Rail rail) {
  Bar b;
  if (k.colour) {
    b.keys = {colour(Colour::Red, "Sources"),
              colour(Colour::Green, inLineup ? "Remove from My Lineup" : "Add to My Lineup"),
              colour(Colour::Yellow, "Live Events"), colour(Colour::Blue, "Settings")};
  } else {
    // Menu does what holding OK does: the channel's panel (Watch, Remind Me,
    // My Lineup, Sources).
    b.keys.push_back(k.menu ? icons({"key-menu"}, "Channel options") : cap("Hold OK", "Channel options"));
    if (k.playPause) b.keys.push_back(icons({"key-playpause"}, "Live Events"));
    if (k.seek) b.keys.push_back(icons({"key-rewind", "key-forward"}, "Page"));
  }
  b.right = railLine(k, rail, "Neighborhoods");
  // Without colour keys, Hold OK is one of the keys already.
  if (k.colour && rail != Rail::Column) b.right += " \xc2\xb7 Hold OK: more";
  return b;
}

Bar eventsBoard(const RemoteKeys& k, Rail rail) {
  Bar b;
  if (k.colour) {
    b.keys = {colour(Colour::Red, "Where to Watch"), colour(Colour::Yellow, "Guide"), colour(Colour::Blue, "Settings")};
  } else {
    if (k.menu) b.keys.push_back(icons({"key-menu"}, "Where to Watch"));
    // Back opens the menu now; My Lineup is in it.
    if (k.playPause) b.keys.push_back(icons({"key-playpause"}, "Guide"));
  }
  b.right = railLine(k, rail, "Sports");
  if (rail != Rail::Column) b.right += " \xc2\xb7 Hold OK: more";
  return b;
}

Bar search(const RemoteKeys& k, SearchPlace place, bool results, const SearchDigits& digits) {
  Bar b;
  const char* dot = " \xc2\xb7 ";
  if (place == SearchPlace::Digits) {
    if (!digits.team.empty()) {
      // The digits also start a name ("49"): nothing tunes by itself.
      b.keys = {cap("OK", "Watch channel " + digits.number), cap(kDown, "The " + digits.team),
                cap("Letters", "Keep typing \xe2\x80\x9c" + digits.number + "\xe2\x80\xa6\xe2\x80\x9d")};
      b.right = std::string("Doesn't tune by itself") + dot + "Back: Cancel";
    } else {
      b.keys = {cap("OK", "Watch now"), cap("0\xe2\x80\x93" "9", "More digits")};
      b.right = std::string("Plays when the number is complete") + dot + "Back: Cancel";
    }
    return b;
  }
  if (k.colour) b.keys.push_back(colour(Colour::Red, "Clear text"));
  if (k.digits) b.keys.push_back(cap("0\xe2\x80\x93" "9", "Channel"));
  if (place == SearchPlace::Keyboard) {
    // The Siri Remote: the ring or a swipe moves, only the centre click types.
    if (k.clickpad) b.keys = {cap("Swipe or ring", "Move"), cap("Centre click", "Type")};
    b.right = results ? std::string(kRight) + " Choose a result" + dot + "Back: Close" : std::string("Back: Close");
    return b;
  }
  // In the results: ☰ is More on Fire TV; click and hold on Apple TV.
  bool fire = k.menu && !k.colour;
  if (k.clickpad) b.keys.push_back(cap("Click and hold", "More"));
  else if (fire) b.keys.push_back(icons({"key-menu"}, "More"));
  b.right = std::string(kLeft) + " Keyboard";
  if (!k.clickpad && !fire) b.right += std::string(dot) + "Hold OK: more";
  b.right += std::string(dot) + "Back: Keyboard";
  return b;
}

Bar setup(const RemoteKeys& k, bool letters) {
  Bar b;
  b.keys.push_back(cap(k.clickpad ? "Click" : "OK", "Pick"));
  b.keys.push_back(cap("Back", "Previous step"));
  if (letters) {
    if (k.channel) b.keys.push_back(cap(std::string("CH ") + kUp + kDown, "Next letter"));
    else if (k.seek) b.keys.push_back(icons({"key-rewind", "key-forward"}, "Next letter"));
  }
  if (letters && k.clickpad) b.right = "Swipe to move faster";
  return b;
}

Bar settings(const RemoteKeys& k, bool inRows, bool choiceRow) {
  Bar b;
  std::string ok = k.clickpad ? "Click" : "OK";
  if (!inRows) {
    b.keys = {cap(std::string(kUp) + kDown, "Move"), cap(kRight, "Open"), cap("Back", "Close Settings")};
    if (k.colour) b.right = "Point and click works too";
    return b;
  }
  if (choiceRow) b.keys.push_back(cap(std::string(kLeft) + kRight, "Change"));
  b.keys.push_back(cap(ok, "Select"));
  b.keys.push_back(cap("Back", "Categories"));
  b.right = "Changes save as you go";
  return b;
}

Hint playerSources(const RemoteKeys& k) {
  if (k.colour) return colour(Colour::Red, "Sources");
  // Elsewhere Sources is a button in the controls (OK, or Menu).
  return cap(k.clickpad ? "Click" : "OK", "Sources");
}

std::optional<Hint> playerOptions(const RemoteKeys& k) {
  if (k.colour) return colour(Colour::Blue, "Settings");
  return std::nullopt;  // on the guide
}

std::optional<Hint> pickAnotherSource(const RemoteKeys& k) {
  if (k.colour) return colour(Colour::Red, "Pick another source");
  if (k.menu) return icons({"key-menu"}, "Pick another source");
  return std::nullopt;
}

std::vector<Tip> gettingAround(const RemoteKeys& k) {
  std::string ok = k.clickpad ? "Click" : "OK";
  std::vector<Tip> tips = {
      {cap(kLeft, ""), "Guide", "What's on now and later, without leaving"},
      {cap(kRight, ""), "Last channel", "Back to the one you watched before"},
      {cap(std::string(kUp) + kDown, ""), "Channels",
       k.channel ? "Up/Down or CH + / CH \xe2\x88\x92, always" : "Up or Down, always"},
      {cap(ok, ""), "Subtitles & more", "Subtitles, Sources, Favorite and more"},
  };
  if (k.colour) tips.push_back({colour(Colour::Green, ""), "Subtitles", "On or off in one press"});
  if (k.digits) tips.push_back({cap("123", ""), "Number keys", "Go straight to a channel"});
  else tips.push_back({cap("Back", ""), "Back", "Closes what's on screen; again to leave"});
  return tips;
}

}  // namespace marigold::hints
