// Build 169 (1.0.12): the player's OK controls and the programme bar over
// them, decided once for every platform. The screen measures text and
// draws; which buttons, in which group, what the focused one says, where
// the row's buttons sit, and where the bar's moment, ticks and live go are
// worked out here.
//
// The row: round icon buttons in three groups with a thin divider between
// them. Watching (behind live: Live first, then Pause on LG, Rewind; live:
// Pause, Rewind; nothing on a channel without replay), this channel
// (Subtitles, Sources, East/West, Cameras), elsewhere (Back, Guide or Games,
// Favorite, Settings). Only the focused button (or the one under the
// pointer) opens into a pill with its label and state; the row always fits.
//
// The bar: live, the show on now from its start to its end with the moment
// at now and "AC360 · 20 min in · 40 min left" under it. Behind live, from
// the start of the programme at the replay's moment (half an hour before it
// when none is known) to live, always the right end; a tick at every
// programme start between the moment and live, only the first one named.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/model.hpp"
#include "core/util/time.hpp"

namespace marigold::engine::controls {

enum class Button : uint8_t { Live, Pause, Rewind, Subtitles, Sources, Swap, Cameras, Back, Guide, Favorite, Settings };
enum class Group : uint8_t { Watching, Channel, Elsewhere };
Group groupOf(Button b);

// What the row is for.
struct RowIn {
  uint16_t replayDays = 0;    // the channel's archive (0: no replay buttons)
  bool behind = false;        // a replay plays (or holds) behind live
  bool lgPause = false;       // LG's remote: Pause stays behind live, after Live
  bool bothVersions = false;  // the channel has East and West
  bool cameras = false;       // the race on screen has onboard cameras
  bool back = false;          // Right has somewhere to go
  bool favorite = false;      // a guide channel (a game has no Favorite)
};
std::vector<Button> row(const RowIn& in);
// The button focused when OK opens the row: Live behind live, else
// Subtitles (-1: neither, as on an empty row).
int opening(const std::vector<Button>& row, bool behind);

// What each button says when focused.
struct LabelIn {
  std::string finalChannel;  // a Final's replay: Live is "<channel> live"
  bool subtitlesOn = false;
  std::string subtitles;     // the language on ("English"; "" or "On": none named)
  std::string source;        // the playing copy's provider ("Dream")
  std::string quality;       // its quality ("1080p")
  bool west = false;         // the West version plays (Swap: "Watch East")
  std::string backTo;        // where Back goes ("TNT")
  bool game = false;         // a game: Guide is "Games"
  bool favorite = false;     // the channel is a favourite
};
struct Label {
  std::string title;   // "Subtitles", "Back to TNT"
  std::string state;   // "English", "Dream · 1080p" ("" none)
  bool dot = false;    // Subtitles on: a small dot on the icon
};
Label label(Button b, const LabelIn& in);
// "Subtitles · English": the title and its state.
std::string text(const Label& l);

// Where the row's buttons go, from its left edge (x 0), `room` wide. Every
// button is `kSize` round; the focused one (-1: none) is `focusedWidth`
// wide, never more than leaves the rest room. Groups are `kDivider` apart
// with a divider in the middle, buttons `kGap`. Only the focused button
// grows, to the right: the ones before it never move (the pointer stays on
// what it opened).
constexpr float kSize = 80, kGap = 14, kDivider = 46;
struct Slot {
  float x = 0, w = 0;
};
struct RowLayout {
  std::vector<Slot> slots;       // one per button
  std::vector<float> dividers;   // the dividers' centres
  float width = 0;               // the whole row
};
RowLayout layout(const std::vector<Button>& row, int focused, float focusedWidth, float room);

// The programme bar.
struct Bar {
  bool replay = false;               // behind live: live at the right end
  time::Seconds from = 0, to = 0;    // the left and right ends (`to` is live behind live)
  double at = 0;                     // the moment (now live, the replay's behind), 0..1
  const Listing* show = nullptr;     // the programme at the moment (into the list given; null none known)
  std::string line;                  // live: "AC360 · 20 min in · 40 min left" ("" none known)
  struct Tick {
    double at = 0;                   // 0..1
    std::string title;               // the programme starting there
  };
  std::vector<Tick> ticks;           // behind live: programme starts after the moment, before live
};
// `shows`: the channel's programmes, sorted (behind live, the replay
// banner's: views::mergedListings of the guide's, the past kept for replay
// and the copy's own). `live`: the guide's show on now (null none known).
// `replayAt`: the replay's moment (none: live).
Bar bar(const std::vector<Listing>& shows, const Listing* live, std::optional<time::Seconds> replayAt,
        time::Seconds now);

// The first tick's name, under the bar: where it starts (px from the bar's
// left) and what fits before the next tick or live. `width`: the bar's;
// `clockRight`: where the moment's clock under its dot ends (the name
// never starts before it). Empty text when there's no tick, or too little
// room for a few letters.
using Measure = std::function<float(const std::string&)>;
constexpr float kNameAfterTick = 8, kNameRoom = 12;
struct TickName {
  float x = 0;
  std::string text;
};
TickName tickName(const Bar& b, float width, float clockRight, const Measure& measure);
// `s` whole when it fits `room`, else cut with "…" to fit; "" when not even
// its first three letters and "…" do.
std::string fit(const std::string& s, float room, const Measure& measure);

}  // namespace marigold::engine::controls
