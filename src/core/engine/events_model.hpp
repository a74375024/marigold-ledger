// What the Live Events screens draw: the board as rows of grouped tiles,
// with every string already worked out (EventWhere / LiveEventTile in tvOS).
// Built by the store from the sports module's board model; immutable once
// published. Never names where the schedule or scores come from.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/util/time.hpp"

namespace marigold::sports {
struct SportsEvent;
}

namespace marigold::engine {

struct EventSide {
  std::string abbr;      // badge text ("KC"); empty with a country chip
  std::string name;      // short name on the tile ("Chiefs")
  std::string country;   // fighters / tennis: a country chip instead of a badge
  std::string logoUrl;   // team logo, when there is one
  uint32_t color = 0x3A3D44, alt = 0xFFFFFF;  // badge fill and ring
  std::string rank;      // "12" for ranked college teams
  std::string score;     // empty before the game
  std::string pens;      // penalty shootout goals ("4"), shown "(4)" beside the score
  bool home = false;
  bool leads = false, trails = false;
  bool ball = false;     // possession
  // Tennis: each player's country flag (two for a doubles pair); the
  // country chip until they load.
  std::vector<std::string> flags;
  bool plain = false;    // a person named only in a title: initials in a plain circle
};

struct EventLeader {  // a golf leaderboard line
  std::string pos;    // "1", "T2"
  std::string name;
  std::string flagUrl;
  std::string score;  // to par
  std::string thru;   // "14", "F"; empty when not known
};

struct EventPodium {  // a finished race session's top three
  std::string code;   // "VER"
  uint32_t color = 0x8A8D93;
};

struct EventPart {  // fight card or race weekend timeline
  std::string label;    // "Prelims", "Main 10:00"
  double fill = 0;      // 0..1 of the part done
  bool current = false;
};

struct EventTile {
  enum class Pill : uint8_t { None, Live, LiveOutline, OnNow, Delayed, LikelyOver, Postponed, Final, NoTime, NotStarted, NotLive };
  std::string id;         // event id (what Watch / detail use)
  Pill pill = Pill::None;
  std::string pillText;   // Final pill: "Final", "Ended", "F/OT"
  std::string status;     // "Q2 3:12", "8:00 PM in 25 m", "ended 9:52"
  bool statusBold = false;  // the start time of an upcoming game
  std::string statusExtra;  // "in 25 m"
  bool favorite = false;
  std::string tag;        // "Monday Night", "RED ZONE", "WHIP-AROUND"
  bool tagAlert = false;
  std::string league;     // the league's short name ("NFL", "CRIC"): hero and search
  // The tile's top line: the sport spelled out ("CRICKET"; empty when it
  // isn't known), the competition ("3rd Test", "ATP · Swiss Indoors Basel
  // · R2"), and a high-school event's marker.
  std::string sport, competition;
  bool highSchool = false;
  bool soon = false;       // starts within the hour: its "in 6 m" stands out
  std::vector<EventSide> sides;  // two, or none
  std::string title, subtitle;   // tiles with no sides, fight cards
  std::vector<EventPart> parts;
  std::vector<EventPodium> podium;
  std::vector<EventLeader> leaders;  // golf: the top two, instead of sides
  std::string icon;       // sport icon for no-sides tiles: an app/icons name ("golf")
  std::string symbol;     // a glyph in its place until the icons load
  std::string channelName, channelLogo;  // footer channel
  std::string pill2;      // footer pill when no channel ("Event feed")
  bool pill2Outlined = false;
  std::string footer;     // "In your lineup", "+ 3 backup feeds" (the hero's where line)
  std::string detail;     // the tile's bottom line: "Centre Court", "5 boards", "Onboard cameras"
  std::string situation;  // "2nd & 6 at LAC 41"
  std::vector<bool> bases;  // baseball: runners on first, second, third; empty = none
  bool redZone = false;
  double progress = -1;   // live progress strip, -1 = none
  bool watchable = false;
  bool finished = false;
  bool upcoming = false;  // OK opens the detail card instead of playing
  // Hero and detail card.
  std::string leagueChip;  // "NFL · Week 8 · Monday Night"
  std::string heroTitle;   // "Chiefs 17 – 14 Chargers"
  std::string heroStatus;  // "Q2 3:12 · 2nd & 6 at LAC 41"
  std::string whereLine;   // "On ESPN, in your lineup · 3 backup feeds from your provider"
  std::string venue;
  time::Seconds start = 0;
};

// The tile's top line: a sport icon's name ("CRICKET"; empty for "other",
// a sport not known), and the competition after it: the league's name where
// it says more than the sport ("ATP", "Women's Division II"), then the tag
// ("3rd Test"), without repeating the sport.
std::string tileSport(const std::string& icon);
std::string tileCompetition(const sports::SportsEvent* event, const std::string& sport, const std::string& tag);

struct EventGroup {
  std::string title;  // "Live", "Later Today", "Sat"; empty for none
  std::vector<EventTile> tiles;
};

struct EventRow {
  std::string id;
  std::string title;  // "Your teams", "Live now", "NFL"
  int live = 0, count = 0;
  std::vector<EventGroup> groups;
  bool families = false;  // the "Event channels" row (feed families)
};

struct EventsBoard {
  uint64_t version = 0;
  std::vector<EventRow> rows;
  bool loading = false;   // the schedule is still coming
  // An old board (from last time) shown while the fresh one is made.
  bool updating = false;
  bool scoresOn = true;
};

// Where to Watch: every way to see one game.
struct WatchSource {
  std::string title;    // channel or feed name
  std::string detail;   // "In your lineup · FHD", "Spanish"
  std::string entryId;  // guide entry to tune, or
  std::string channelId;  // a raw event feed
  bool spanish = false;
};

struct WhereToWatch {
  std::string title;  // the game
  std::vector<std::pair<std::string, std::vector<WatchSource>>> sections;
  std::string note;   // "4 more stations are showing it"
};

// The player's banner while watching a game: who's playing and where, from
// what the board already holds (never a score: the sides carry none), and
// the times its status runs on by the clock (sports::banner::status).
struct GameBanner {
  std::string id;                // the board's event
  std::vector<EventSide> sides;  // two, or none (a title and the sport's icon)
  std::string title;             // "Rams at Seahawks", "Australian GP · Race"
  std::string line;              // "NFL · WEEK 8 · SOFI STADIUM"
  std::string icon, symbol;      // the sport's icon (app/icons name), a glyph until it loads
  bool favorite = false;         // your team
  std::optional<time::Seconds> start, end;  // end: the usual length, or the listing's end
  bool postponed = false;
  // Where it plays when it isn't a guide channel (an event feed).
  std::string channelName, channelLogo;
};

}  // namespace marigold::engine
