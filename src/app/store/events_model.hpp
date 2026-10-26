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

namespace marigold::app {

struct EventSide {
  std::string abbr;      // badge text ("KC"); empty with a country chip
  std::string name;      // short name on the tile ("Chiefs")
  std::string country;   // fighters / tennis: a country chip instead of a badge
  std::string logoUrl;   // team logo, when there is one
  uint32_t color = 0x3A3D44, alt = 0xFFFFFF;  // badge fill and ring
  std::string rank;      // "12" for ranked college teams
  std::string score;     // empty before the game
  bool home = false;
  bool leads = false, trails = false;
  bool ball = false;     // possession
  // Tennis: the player's photo (small first, then full size; logo cache
  // link list), with `initials` until it loads; doubles get a pair icon.
  std::string photoUrl;
  std::string initials;
  bool doubles = false;
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
  enum class Pill : uint8_t { None, Live, LiveOutline, OnNow, Delayed, LikelyOver, Postponed, Final, NoTime, NotStarted };
  std::string id;         // event id (what Watch / detail use)
  Pill pill = Pill::None;
  std::string pillText;   // Final pill: "Final", "Ended", "F/OT"
  std::string status;     // "Q2 3:12", "8:00 PM in 25 m", "ended 9:52"
  bool statusBold = false;  // the start time of an upcoming game
  std::string statusExtra;  // "in 25 m"
  bool favorite = false;
  std::string tag;        // "Monday Night", "RED ZONE", "WHIP-AROUND"
  bool tagAlert = false;
  std::string league;     // shown in mixed rows when there is no tag
  std::vector<EventSide> sides;  // two, or none
  std::string title, subtitle;   // tiles with no sides, fight cards
  std::vector<EventPart> parts;
  std::vector<EventPodium> podium;
  std::string symbol;     // sport icon key for no-sides tiles
  std::string channelName, channelLogo;  // footer channel
  std::string pill2;      // footer pill when no channel ("Event feed")
  bool pill2Outlined = false;
  std::string footer;     // "In your lineup", "+ 3 backup feeds"
  std::string situation;  // "2nd & 6 at LAC 41"
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

}  // namespace marigold::app
