// Build 177, Skip break in a sports game's replay: what the player shows of
// the game's breaks (sports::plays, by the wall clock), decided once for
// every platform. The replay's moment is a wall-clock time (Session::
// replayAt, with each copy's measured archive clock, Builds 173-174), so a
// break's times are the replay's.
//
// Nothing new on the remote: during a break the top-right tag (BehindTag's
// seconds, from the moment the break begins) says "BREAK · about 2 min · OK
// to skip"; OK opens the controls on "Skip break" (after Start over); OK on
// it plays from just before the next play, and the notice card says
// "Skipped a 2 min break · OK again to go back" (OK while it shows: back).
// A break that runs to live ("live is in it too") has no Skip break: the
// controls open on Live, as behind live. The Rewind timeline draws the
// breaks on its bar, and its Up / Down stop at each break's end too.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/engine/replay.hpp"
#include "core/sports/plays.hpp"
#include "core/util/time.hpp"

namespace marigold::engine::breaks {

using sports::plays::Break;

// A skip that would leave out less than this isn't offered.
constexpr time::Seconds kLeastSaved = 30;
// A break whose next play is this close to live runs to live (as
// Session::kLiveWithin: a replay that close is live).
constexpr time::Seconds kNearLive = 60;

// Where a break's game resumes, as the replay can play it: the next play
// less the trim (Break::to), to the minute the replay asks in, at or
// before it, where this channel's archive can start (replay::Reach).
time::Seconds playFrom(const Break& b, const replay::Reach& r, time::Seconds earliest, time::Seconds now);
// Where "OK again to go back" plays from: the moment the skip left, as the
// replay can play it (at or before it).
time::Seconds backTo(time::Seconds at, const replay::Reach& r, time::Seconds earliest, time::Seconds now);

// The replay's moment and the breaks.
struct Moment {
  enum class Kind { None, Skip, Live };
  Kind kind = Kind::None;
  const Break* in = nullptr;  // into the list given
  time::Seconds target = 0;   // Skip: where it plays from
  time::Seconds saves = 0;    // Skip: how much of the break it leaves out
};
// `at`: the replay's moment; `skipped`: the start of a break just skipped
// (the minute it played from can still be in it): not offered again.
Moment moment(const std::vector<Break>& list, time::Seconds at, time::Seconds now, const replay::Reach& r,
              time::Seconds earliest, time::Seconds skipped = 0);

// "about 2 min" (at least 1).
std::string about(time::Seconds s);
// The tag's words after its BREAK chip: "about 2 min · OK to skip", "live
// is in it too"; "" none.
std::string tagText(const Moment& m);
// The notice after a skip: "Skipped a 2 min break" and its line.
std::string skippedTitle(time::Seconds saved);
constexpr const char* kBackLine = "OK again to go back";
// "2 min 10 s", "45 s": the log's.
std::string lengthText(time::Seconds s);

// The Rewind timeline's Up / Down stops: each closed break's end, where
// the replay can play from (not within a minute of live).
std::vector<time::Seconds> stops(const std::vector<Break>& list, const replay::Reach& r, time::Seconds earliest,
                                 time::Seconds now);

// The blocks a bar from `from` to `to` draws (0..1 of it; an open break
// runs to `now`).
struct Block {
  double from = 0, to = 0;
  bool halftime = false;
};
std::vector<Block> blocks(const std::vector<Break>& list, time::Seconds from, time::Seconds to, time::Seconds now);

}  // namespace marigold::engine::breaks
