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
    } else if (digits.waits) {
      // A channel in a hidden group: only OK tunes it.
      b.keys = {cap("OK", "Watch channel " + digits.number), cap("0\xe2\x80\x93" "9", "More digits")};
      b.right = std::string("Doesn't tune by itself") + dot + "Back: Cancel";
    } else {
      b.keys = {cap("OK", "Watch now"), cap("0\xe2\x80\x93" "9", "More digits")};
      b.right = std::string("Plays when the number is complete") + dot + "Back: Cancel";
    }
    return b;
  }
  if (k.colour) b.keys.push_back(colour(Colour::Red, "Clear text"));
  if (k.digits) b.keys.push_back(cap("0\xe2\x80\x93" "9", "Channel"));
  if (place == SearchPlace::Keyboard || place == SearchPlace::Keypad) {
    // A computer's keyboard types straight into the field.
    if (k.typing) b.keys.insert(b.keys.begin(), cap("A\xe2\x80\x93Z", "Type to search"));
    // The Siri Remote: the ring or a swipe moves, only the centre click
    // types; Play/Pause changes the page, as on Apple's keyboard.
    if (k.clickpad)
      b.keys = {cap("Swipe or ring", "Move"), cap("Centre click", "Type"),
                icons({"key-playpause"}, place == SearchPlace::Keypad ? "abc" : "123")};
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

Bar settings(const RemoteKeys& k, bool inRows, bool choiceRow, const std::string& ok) {
  Bar b;
  if (!inRows) {
    b.keys = {cap(std::string(kUp) + kDown, "Move"), cap(kRight, "Open"), cap("Back", "Close Settings")};
    if (k.colour) b.right = "Point and click works too";
    return b;
  }
  if (choiceRow) b.keys.push_back(cap(std::string(kLeft) + kRight, "Change"));
  b.keys.push_back(cap(k.clickpad ? "Click" : "OK", ok.empty() ? std::string("Select") : ok));
  b.keys.push_back(cap("Back", "Categories"));
  b.right = "Changes save as you go";
  return b;
}

Bar settingsPage(const RemoteKeys& k, const std::string& ok, const std::string& back, bool choiceRow,
                 const std::string& down) {
  Bar b;
  if (choiceRow) b.keys.push_back(cap(std::string(kLeft) + kRight, "Change"));
  if (!ok.empty()) b.keys.push_back(cap(k.clickpad ? "Click" : "OK", ok));
  if (!down.empty()) b.keys.push_back(cap(kDown, down));
  b.keys.push_back(cap("Back", back));
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

std::string playerGuide(const RemoteKeys& k, PlayerGuide place, bool column, bool pointer) {
  const char* dot = " \xc2\xb7 ";
  std::string ok = k.clickpad ? "Click" : "OK";
  std::string digits = k.digits ? std::string(dot) + "123 jumps to a channel" : std::string();
  switch (place) {
    case PlayerGuide::Now:
      return std::string(kLeft) + (column ? " Neighborhoods" : " Lists") + dot + kUp + kDown + " channels" + dot + kRight +
             " later" + dot + ok + " watch" + dot + "Back closes" + digits;
    case PlayerGuide::Later:
      return std::string(kUp) + kDown + " channels" + dot + kLeft + kRight + " earlier and later" + dot + ok + " details" +
             dot + (k.clickpad ? "Click and hold" : "Hold OK") + " remind" + dot + "Back to now" + digits;
    case PlayerGuide::Column:
      if (pointer)
        return std::string("Click: go") + dot + "Wheel: walk" + dot + "Click the grid: close" + dot +
               "Click beside the guide: close it";
      return ok + " or " + kRight + " Go" + dot + "Back: Stay" + dot + kLeft + " Lists";
    case PlayerGuide::Lists:
    case PlayerGuide::GameLists:
      return std::string(kUp) + kDown + " lists" + dot + ok + " or " + kRight + " open" + dot +
             (place == PlayerGuide::GameLists ? (column ? "Back: sports" : "Back: the games")
              : column                        ? "Back: neighborhoods"
                                              : "Back: the grid");
    case PlayerGuide::Times: return std::string(kUp) + kDown + " times" + dot + ok + " go" + dot + "Back: the grid";
    case PlayerGuide::Games:
      return std::string(kLeft) + " Sports" + dot + kUp + kDown + " games" + dot + ok + " switch" + dot + "Back closes";
    case PlayerGuide::Sports:
      if (pointer)
        return std::string("Click: its games") + dot + "Wheel: walk" + dot + "Click a game: close" + dot +
               "Click beside the guide: close it";
      return ok + " or " + kRight + " its games" + dot + "Back: Stay" + dot + kLeft + " Lists";
  }
  return {};
}

std::string playerSearch(const RemoteKeys& k, bool inResults, bool any, bool watch) {
  const char* dot = " \xc2\xb7 ";
  std::string ok = k.clickpad ? "Click" : "OK";
  if (!inResults)
    return ok + " type" + (any ? std::string(dot) + kRight + " results" : std::string()) + dot + "Back: lists";
  return std::string(kUp) + kDown + " results" + dot + ok + (watch ? " watch here" : " details") + dot + kLeft +
         " keyboard" + dot + "Back: keyboard";
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
