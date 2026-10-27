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

}  // namespace

Bar guide(const RemoteKeys& k, bool inLineup) {
  Bar b;
  if (k.colour) {
    b.keys = {colour(Colour::Red, "Sources"),
              colour(Colour::Green, inLineup ? "Remove from My Lineup" : "Add to My Lineup"),
              colour(Colour::Yellow, "Live Events"), colour(Colour::Blue, "Options")};
  } else {
    // Menu does what holding OK does: the channel's panel (Watch, Remind Me,
    // My Lineup, Sources).
    b.keys.push_back(k.menu ? icons({"key-menu"}, "Channel options") : cap("Hold OK", "Channel options"));
    if (k.playPause) b.keys.push_back(icons({"key-playpause"}, "Live Events"));
    if (k.seek) b.keys.push_back(icons({"key-rewind", "key-forward"}, "Page"));
  }
  std::string right;
  auto add = [&right](const std::string& s) { right += (right.empty() ? "" : " \xc2\xb7 ") + s; };
  if (k.channel) add(std::string("CH ") + kUp + kDown + " page");
  if (k.digits) add("0\xe2\x80\x93" "9 go to channel");
  if (k.colour) add("Hold OK: more");
  else add(std::string(kUp) + (k.playPause ? " Search & Options" : " Live Events, Search & Options"));
  b.right = right;
  return b;
}

Bar eventsBoard(const RemoteKeys& k) {
  Bar b;
  if (k.colour) {
    b.keys = {colour(Colour::Red, "Where to Watch"), colour(Colour::Yellow, "Guide"), colour(Colour::Blue, "Options")};
  } else {
    if (k.menu) b.keys.push_back(icons({"key-menu"}, "Where to Watch"));
    b.keys.push_back(k.playPause ? icons({"key-playpause"}, "Guide") : cap("Back", "Guide"));
  }
  b.right = std::string("Hold OK: more \xc2\xb7 ") + kLeft + " sports";
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

Hint playerSources(const RemoteKeys& k) {
  if (k.colour) return colour(Colour::Red, "Sources");
  if (k.menu) return icons({"key-menu"}, "Sources");
  return cap(kDown, "Sources");
}

std::optional<Hint> playerOptions(const RemoteKeys& k) {
  if (k.colour) return colour(Colour::Blue, "Options");
  return std::nullopt;  // on the guide
}

std::optional<Hint> pickAnotherSource(const RemoteKeys& k) {
  if (k.colour) return colour(Colour::Red, "Pick another source");
  if (k.menu) return icons({"key-menu"}, "Pick another source");
  return std::nullopt;
}

std::vector<Tip> gettingAround(const RemoteKeys& k) {
  std::vector<Tip> tips = {
      {cap(kLeft, ""), "Guide", "What's on, without leaving"},
      {cap(kRight, ""), "Last channel", "Jump back and forth"},
      {cap(std::string(kUp) + kDown, ""), "Channels",
       k.channel ? "Up, or CH + / CH \xe2\x88\x92" : "Up or Down"},
      {cap("OK", ""), "Info", std::string("What's on now and next; ") + kDown + " for more"},
  };
  Hint sources = playerSources(k);
  sources.label.clear();
  tips.push_back({sources, "Sources",
                  k.colour || k.menu ? "Pick another feed of this channel"
                                     : "Down, then OK: another feed of this channel"});
  if (k.digits) tips.push_back({cap("123", ""), "Number keys", "Go straight to a channel"});
  else tips.push_back({cap("Back", ""), "Back", "To the guide"});
  return tips;
}

}  // namespace marigold::hints
