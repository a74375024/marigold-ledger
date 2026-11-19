// Build 177, Skip break: what the player shows of a sports replay's breaks
// (engine::breaks), the controls row with and without one (and near live),
// where the row opens, what the tag and the notice say, where Skip break and
// "OK again to go back" play from, and the Rewind timeline's Up / Down
// stopping at break ends. Pure functions: no store, no renderer.
#include <doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/engine/breaks.hpp"
#include "core/engine/controls.hpp"
#include "core/engine/replay.hpp"

using namespace marigold;
using namespace marigold::engine;
using B = controls::Button;
using Kind = breaks::Moment::Kind;

namespace {

constexpr time::Seconds kMin = 60;
// A game's replay: now is 9:00 PM; the game began at 6:00 PM.
constexpr time::Seconds kNow = 1'794'000'000 - 1'794'000'000 % 3600 + 21 * 3600 % 86400;
constexpr time::Seconds kKick = kNow - 3 * 3600;
constexpr time::Seconds kEarliest = kNow - 86'400 + 3600;
const replay::Reach kAny{};

// A break from `from` to `to`, seconds after kickoff.
breaks::Break brk(time::Seconds from, time::Seconds to, bool halftime = false) {
  return {kKick + from, kKick + to, halftime, false};
}

controls::RowIn replayRow(bool skip) {
  controls::RowIn in;
  in.replayDays = 1;
  in.behind = true;
  in.startOver = true;
  in.skipBreak = skip;
  in.back = true;
  in.favorite = false;  // a game
  return in;
}

}  // namespace

TEST_CASE("breaks 177: the replay's moment in a break maps by the wall clock") {
  // 18:10:00 to 18:12:10, the next play at 18:12:20.
  std::vector<breaks::Break> list = {brk(600, 730), brk(1500, 1620)};
  // Before, between and after: nothing.
  CHECK(breaks::moment(list, kKick + 599, kNow, kAny, kEarliest).kind == Kind::None);
  CHECK(breaks::moment(list, kKick + 730, kNow, kAny, kEarliest).kind == Kind::None);
  CHECK(breaks::moment(list, kKick + 1000, kNow, kAny, kEarliest).kind == Kind::None);
  // At its start: Skip, to the minute the replay asks in at or before the
  // next play less 10 s (18:12:00), saving 2 min.
  breaks::Moment m = breaks::moment(list, kKick + 600, kNow, kAny, kEarliest);
  REQUIRE(m.kind == Kind::Skip);
  REQUIRE(m.in == &list[0]);
  CHECK(m.target == kKick + 720);
  CHECK(m.saves == 120);
  // Late in it: less than half a minute to save is no skip.
  CHECK(breaks::moment(list, kKick + 691, kNow, kAny, kEarliest).kind == Kind::None);
  CHECK(breaks::moment(list, kKick + 690, kNow, kAny, kEarliest).kind == Kind::Skip);
  // The break just skipped (the minute played from is still in it): not
  // again.
  CHECK(breaks::moment(list, kKick + 720, kNow, kAny, kEarliest, kKick + 600).kind == Kind::None);
  CHECK(breaks::moment(list, kKick + 1500, kNow, kAny, kEarliest, kKick + 600).kind == Kind::Skip);
}

TEST_CASE("breaks 177: Skip break respects an archive in chunks") {
  // Ten-minute chunks from the hour, read through for two minutes after
  // each boundary: 18:12:00 can't be started at; 18:02 can, but it's
  // before the break.
  replay::Reach chunks{600, 0, 120};
  std::vector<breaks::Break> list = {brk(600, 730)};
  CHECK(breaks::moment(list, kKick + 600, kNow, chunks, kEarliest).kind == Kind::Skip);
  CHECK(breaks::playFrom(list[0], chunks, kEarliest, kNow) == kKick + 720);
  std::vector<breaks::Break> later = {brk(900, 1030)};  // 18:15:00 to 18:17:10
  CHECK(breaks::playFrom(later[0], chunks, kEarliest, kNow) == kKick + 720);  // 18:12, before it
  CHECK(breaks::moment(later, kKick + 900, kNow, chunks, kEarliest).kind == Kind::None);
}

TEST_CASE("breaks 177: a break that runs to live has no Skip break") {
  // Still open: the next play isn't in yet.
  std::vector<breaks::Break> open = {{kNow - 200, kNow - 30, false, true}};
  breaks::Moment m = breaks::moment(open, kNow - 60, kNow, kAny, kEarliest);
  CHECK(m.kind == Kind::Live);
  CHECK(breaks::tagText(m) == "live is in it too");
  // Closed, but its next play within a minute of live: the same.
  std::vector<breaks::Break> near = {{kNow - 200, kNow - 40, false, false}};
  CHECK(breaks::moment(near, kNow - 150, kNow, kAny, kEarliest).kind == Kind::Live);
  // A minute and more before live: Skip.
  std::vector<breaks::Break> earlier = {{kNow - 400, kNow - 130, false, false}};
  CHECK(breaks::moment(earlier, kNow - 350, kNow, kAny, kEarliest).kind == Kind::Skip);
}

TEST_CASE("breaks 177: no breaks, no button") {
  breaks::Moment m = breaks::moment({}, kKick + 600, kNow, kAny, kEarliest);
  CHECK(m.kind == Kind::None);
  CHECK(breaks::tagText(m).empty());
  controls::RowIn in = replayRow(m.kind == Kind::Skip);
  std::vector<B> row = controls::row(in);
  CHECK(std::find(row.begin(), row.end(), B::SkipBreak) == row.end());
  CHECK(row[static_cast<size_t>(controls::opening(row, true))] == B::Live);
}

TEST_CASE("breaks 177: the tag's words") {
  std::vector<breaks::Break> list = {brk(600, 730)};
  breaks::Moment m = breaks::moment(list, kKick + 600, kNow, kAny, kEarliest);
  CHECK(breaks::tagText(m) == "about 2 min \xc2\xb7 OK to skip");
  CHECK(breaks::about(29) == "about 1 min");
  CHECK(breaks::about(89) == "about 1 min");
  CHECK(breaks::about(90) == "about 2 min");
  CHECK(breaks::about(13 * kMin + 10) == "about 13 min");
}

TEST_CASE("breaks 177: the notice after a skip, and where going back plays from") {
  CHECK(breaks::skippedTitle(130) == "Skipped a 2 min break");
  CHECK(breaks::skippedTitle(40) == "Skipped a 1 min break");
  CHECK(std::string(breaks::kBackLine) == "OK again to go back");
  CHECK(breaks::lengthText(130) == "2 min 10 s");
  CHECK(breaks::lengthText(45) == "45 s");
  CHECK(breaks::lengthText(120) == "2 min");
  // Skipped from 18:10:25: back to 18:10 (the minute the replay asks in).
  std::vector<breaks::Break> list = {brk(600, 730)};
  breaks::Moment m = breaks::moment(list, kKick + 625, kNow, kAny, kEarliest);
  REQUIRE(m.kind == Kind::Skip);
  CHECK(m.target == kKick + 720);
  CHECK(breaks::backTo(kKick + 625, kAny, kEarliest, kNow) == kKick + 600);
  // In chunks: the reachable start at or before it.
  replay::Reach chunks{600, 0, 120};
  CHECK(breaks::backTo(kKick + 625, chunks, kEarliest, kNow) == kKick + 600);
  CHECK(breaks::backTo(kKick + 905, chunks, kEarliest, kNow) == kKick + 720);
}

TEST_CASE("breaks 177: the controls row with a break to skip") {
  // Behind live: Live, Pause (1.0.27: every remote), Rewind, Start over,
  // Skip break, then the rest.
  std::vector<B> row = controls::row(replayRow(true));
  CHECK(row == std::vector<B>{B::Live, B::Pause, B::Rewind, B::StartOver, B::SkipBreak, B::Subtitles, B::Sources,
                              B::Back, B::Guide, B::Settings});
  CHECK(controls::groupOf(B::SkipBreak) == controls::Group::Watching);
  // The row opens on Skip break, not Live (nor Subtitles).
  CHECK(controls::opening(row, true) == 4);
  // No Start over: Skip break after Rewind.
  controls::RowIn in = replayRow(true);
  in.startOver = false;
  row = controls::row(in);
  CHECK(row[3] == B::SkipBreak);
  // Without: as before, Live first.
  row = controls::row(replayRow(false));
  CHECK(row == std::vector<B>{B::Live, B::Pause, B::Rewind, B::StartOver, B::Subtitles, B::Sources, B::Back,
                              B::Guide, B::Settings});
  CHECK(controls::opening(row, true) == 0);
  // Never on a channel without replay.
  in = controls::RowIn{};
  in.skipBreak = true;
  row = controls::row(in);
  CHECK(std::find(row.begin(), row.end(), B::SkipBreak) == row.end());
  CHECK(engine::replay::buttons(0, true, true, true).empty());
}

TEST_CASE("breaks 177: near live the row is today's behind-live row") {
  std::vector<breaks::Break> open = {{kNow - 200, kNow - 30, false, true}};
  breaks::Moment m = breaks::moment(open, kNow - 60, kNow, kAny, kEarliest);
  std::vector<B> row = controls::row(replayRow(m.kind == Kind::Skip));
  CHECK(std::find(row.begin(), row.end(), B::SkipBreak) == row.end());
  CHECK(controls::opening(row, true) == 0);
  CHECK(row[0] == B::Live);
}

TEST_CASE("breaks 177: Skip break's label") {
  controls::LabelIn in;
  in.skipAbout = breaks::about(130);
  controls::Label l = controls::label(B::SkipBreak, in);
  CHECK(l.title == "Skip break");
  CHECK(l.state == "about 2 min");
  CHECK(controls::text(l) == "Skip break \xc2\xb7 about 2 min");
}

TEST_CASE("breaks 177: the timeline's Up / Down stop at each break's end") {
  // A three-hour game on one programme from 6:00; breaks ending (next play
  // less 10 s) at 18:12:10, 18:31:50 and halftime's 19:25:30.
  std::vector<replay::Programme> list = {{kKick - 1800, kKick, "Pregame", true},
                                         {kKick, kKick + 4 * 3600, "Harbor at Valley", true}};
  std::vector<breaks::Break> b = {brk(600, 730), brk(1800, 1910), brk(4200, 5130, true)};
  std::vector<time::Seconds> stops = breaks::stops(b, kAny, kEarliest, kNow);
  REQUIRE(stops == std::vector<time::Seconds>{kKick + 720, kKick + 1860, kKick + 5100});
  time::Seconds head = kKick + 7200;  // 8:00 PM
  // Up: halftime's end, the next one, the first, then the programme's start
  // and the one before.
  head = replay::jumpReach(kAny, list, head, -1, kEarliest, kNow, stops);
  CHECK(head == kKick + 5100);
  head = replay::jumpReach(kAny, list, head, -1, kEarliest, kNow, stops);
  CHECK(head == kKick + 1860);
  head = replay::jumpReach(kAny, list, head, -1, kEarliest, kNow, stops);
  CHECK(head == kKick + 720);
  head = replay::jumpReach(kAny, list, head, -1, kEarliest, kNow, stops);
  CHECK(head == kKick);
  head = replay::jumpReach(kAny, list, head, -1, kEarliest, kNow, stops);
  CHECK(head == kKick - 1800);
  // Down: back through them; past the last, live (no programme after).
  head = replay::jumpReach(kAny, list, head, 1, kEarliest, kNow, stops);
  CHECK(head == kKick);
  head = replay::jumpReach(kAny, list, head, 1, kEarliest, kNow, stops);
  CHECK(head == kKick + 720);
  head = replay::jumpReach(kAny, list, head, 1, kEarliest, kNow, stops);
  CHECK(head == kKick + 1860);
  head = replay::jumpReach(kAny, list, head, 1, kEarliest, kNow, stops);
  CHECK(head == kKick + 5100);
  head = replay::jumpReach(kAny, list, head, 1, kEarliest, kNow, stops);
  CHECK(head == kNow);
  // Within half a minute after a stop is at it: Up goes to the one before.
  CHECK(replay::jumpReach(kAny, list, kKick + 1880, -1, kEarliest, kNow, stops) == kKick + 720);
  // No stops: as before.
  CHECK(replay::jumpReach(kAny, list, kKick + 7200, -1, kEarliest, kNow, {}) ==
        replay::jumpReach(kAny, list, kKick + 7200, -1, kEarliest, kNow));
}

TEST_CASE("breaks 177: the bar's blocks") {
  std::vector<breaks::Break> b = {brk(600, 730), brk(4200, 5130, true), {kNow - 100, kNow - 50, false, true}};
  // A bar from kickoff to live (three hours).
  std::vector<breaks::Block> k = breaks::blocks(b, kKick, kNow, kNow);
  REQUIRE(k.size() == 3);
  CHECK(k[0].from == doctest::Approx(600.0 / 10800));
  CHECK(k[0].to == doctest::Approx(730.0 / 10800));
  CHECK(k[1].halftime);
  // The open one runs to live.
  CHECK(k[2].to == doctest::Approx(1.0));
  // Cut to the bar; none outside it.
  k = breaks::blocks(b, kKick + 650, kKick + 3600, kNow);
  REQUIRE(k.size() == 1);
  CHECK(k[0].from == doctest::Approx(0.0));
  CHECK(breaks::blocks(b, kKick + 800, kKick + 3000, kNow).empty());
}
