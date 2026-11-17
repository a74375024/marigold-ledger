// Build 165, replay: the maths of the Rewind timeline, shared by every
// platform. Times are Unix seconds; the playhead moves by whole minutes
// and never leaves the archive (the copy's catch-up days) or passes live.
//
// Keys: Left / Right move one minute; held (repeat events), five, then
// fifteen. Up / Down jump to the start of the programme before / after.
// The bar shows from the start of the programme before the one on now (or
// two hours back, whichever is earlier) up to live; moving before it
// scrolls it back.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/util/time.hpp"

namespace marigold::engine::replay {

// A programme on the timeline (the guide's, or the copy's own past).
struct Programme {
  time::Seconds start = 0, end = 0;
  std::string title;
  bool archived = true;  // the provider says its archive holds it
};

// The earliest moment a copy keeping `days` of catch-up can replay at
// `now`: `days` back, an hour in hand (Build 166: a request for exactly a
// day back failed on a one-day archive), to the next whole minute. The
// timeline, Start over and Right's return all stop here.
time::Seconds archiveStart(time::Seconds now, uint16_t days);
// Whether a copy keeping `days` can replay `at` at `now`.
bool covers(uint16_t days, time::Seconds at, time::Seconds now);
// To the minute (down).
time::Seconds minuteOf(time::Seconds t);

// Build 167: what one copy's archive gives its channel row. `claimed`: the
// days its provider says it keeps; `archiveOk`: what Test all sources found
// (-1 not tested, 0 it didn't play, 1 it played) and `measuredDays` the
// depth it confirmed (0 not known). A broken archive gives nothing; a
// confirmed depth below the claim lowers it; otherwise the claim stands.
uint16_t usableDays(uint16_t claimed, int archiveOk, uint16_t measuredDays);

// Build 167: the guide's Start over (hold OK): offered on a programme on
// now that began more than kStartOverAfter ago and whose start a channel
// keeping `days` of usable archive still holds (covers()).
constexpr time::Seconds kStartOverAfter = 3 * 60;
bool startOverOffered(time::Seconds start, time::Seconds end, time::Seconds now, uint16_t days);

// Left / Right: the minutes one press moves, after `repeats` repeat
// events of the same key in a row (0: a single press).
int stepMinutes(int repeats);
// The playhead after one step `dir` (-1 back, +1 forward), on whole
// minutes, inside [earliest, live].
time::Seconds step(time::Seconds at, int dir, int repeats, time::Seconds earliest, time::Seconds live);
// Up / Down (-1 / +1): the start of the programme before / after `at`
// (a playhead within the first half minute of a programme is at its
// start, so Up goes to the one before). Past the last programme: live.
// With no programme there: the half hour before / after. Inside
// [earliest, live].
time::Seconds jump(const std::vector<Programme>& list, time::Seconds at, int dir, time::Seconds earliest,
                   time::Seconds live);
// The programme on at `t` (null: none known).
const Programme* on(const std::vector<Programme>& list, time::Seconds t);

// What the bar shows.
struct Span {
  time::Seconds from = 0, to = 0;
  time::Seconds length() const { return to - from; }
};
// When the timeline opens: from the start of the programme before the one
// on now (or two hours back, whichever is earlier), not before `earliest`,
// up to live.
Span opening(const std::vector<Programme>& list, time::Seconds live, time::Seconds earliest);
// The bar after the playhead moved to `at`: as it was while `at` is on it;
// else moved (its length kept) so `at` is a tenth in from the edge it
// passed; never before `earliest` nor after `live`.
Span follow(Span shown, time::Seconds at, time::Seconds earliest, time::Seconds live);
// A point on the bar (0..1) as a time, to the minute, inside
// [earliest, live] (a click with the pointer).
time::Seconds at(Span shown, double fraction, time::Seconds earliest, time::Seconds live);

// Merges the guide's listings with a copy's own past (from its panel): the
// guide's where it has them, the copy's to fill what it lacks (one that
// overlaps a guide programme by more than half of itself is left out).
// Sorted by start.
std::vector<Programme> merged(std::vector<Programme> guide, const std::vector<Programme>& own);

// "−17:05", "−1:02:05": how far behind live (never below zero).
std::string behindText(time::Seconds behind);
// "−17 min", "−1 h 02 min" for the playhead's bubble.
std::string behindMinutes(time::Seconds behind);

// The controls' replay buttons, at the front of the row on a channel
// whose copies keep an archive (`days` > 0; none otherwise): behind live,
// Go live (then Pause where `lgPause`: LG's remote), else Pause; then
// Rewind. Build 169 took Start over out of the row (and off the banner
// after a tune); Build 170 puts it back after Rewind where `startOver`
// (engine::controls::startOver: it can work). Never on the banner.
enum class Button { GoLive, Pause, Rewind, StartOver };
std::vector<Button> buttons(uint16_t days, bool behind, bool lgPause, bool startOver = false);

// Build 169: the "−17:05 behind live" tag at the top right. It shows for
// kShowMs once a replay starts or plays from another moment (a jump),
// counted from the first moment after it that neither the banner nor the
// controls are up (they say how far behind themselves), then fades over
// kFadeMs. Never while they're up. Times are the app's ms.
class BehindTag {
 public:
  static constexpr uint32_t kShowMs = 5000, kFadeMs = 400, kFrameMs = 16;
  // A jump (again while its stretch opens: its seconds start with the
  // picture).
  void jumped() {
    pending_ = true;
    from_ = 0;
  }
  // Every tick; `covered`: the banner or the controls are up.
  void tick(uint32_t ms, bool covered);
  // How much of it shows: 0 hidden, 1 fully, between while it fades.
  float opacity(uint32_t ms, bool covered) const;
  // When it next looks different by itself (0: it doesn't).
  uint32_t nextChange(uint32_t ms) const;

 private:
  bool pending_ = false;  // jumped; its seconds not begun
  uint32_t from_ = 0;     // ms they began (0: not)
};

}  // namespace marigold::engine::replay
