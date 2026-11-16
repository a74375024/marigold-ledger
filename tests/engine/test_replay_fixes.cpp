// Build 169 (1.0.12), replay fixes: the behind-live tag's few seconds after
// a jump, the controls' replay buttons without Start over, and the guide's
// chip showing the replay window. Pure functions: no store, no renderer.
// (The guide's and Live Events' hold-OK Start over: test_replay.cpp,
// test_replay_marks.cpp and test_game_replay.cpp.)
#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/engine/replay.hpp"
#include "core/engine/views.hpp"

using namespace marigold::engine;
using Tag = replay::BehindTag;
using Button = replay::Button;

TEST_CASE("replay 1.0.12: the behind-live tag shows for 5 s after a jump, then fades") {
  Tag t;
  // Nothing jumped: never shown.
  t.tick(1000, false);
  CHECK(t.opacity(1000, false) == 0);
  CHECK(t.nextChange(1000) == 0);

  t.jumped();
  t.tick(10'000, false);
  CHECK(t.opacity(10'000, false) == 1);
  CHECK(t.opacity(14'999, false) == 1);
  CHECK(t.nextChange(12'000) == 15'000);
  // Then a short fade, and gone.
  float fading = t.opacity(15'200, false);
  CHECK(fading > 0);
  CHECK(fading < 1);
  CHECK(t.nextChange(15'200) == 15'200 + Tag::kFrameMs);
  CHECK(t.opacity(15'000 + Tag::kFadeMs, false) == 0);
  CHECK(t.opacity(60'000, false) == 0);
  CHECK(t.nextChange(60'000) == 0);

  // Another jump: its own 5 s.
  t.jumped();
  t.tick(70'000, false);
  CHECK(t.opacity(72'000, false) == 1);
  CHECK(t.opacity(75'000 + Tag::kFadeMs, false) == 0);
}

TEST_CASE("replay 1.0.12: never over the banner or the controls; its seconds start once they're down") {
  Tag t;
  t.jumped();
  // The banner is up after the jump (it says how far behind itself).
  t.tick(1000, true);
  CHECK(t.opacity(1000, true) == 0);
  t.tick(5900, true);
  CHECK(t.opacity(5900, true) == 0);
  // The banner hides: now the tag, for its 5 s.
  t.tick(6000, false);
  CHECK(t.opacity(6000, false) == 1);
  CHECK(t.opacity(10'999, false) == 1);
  // The controls come up meanwhile: hidden while they're up.
  CHECK(t.opacity(8000, true) == 0);
  // Back down within the 5 s: the rest of them; never again after.
  CHECK(t.opacity(9000, false) == 1);
  t.tick(30'000, false);
  CHECK(t.opacity(30'000, false) == 0);
}

TEST_CASE("replay 1.0.12: a replay's stretch still opening waits for its picture") {
  Tag t;
  t.jumped();
  t.tick(1000, false);
  // Jumped again each tick while it opens: its seconds start over.
  t.jumped();
  t.tick(3000, false);
  CHECK(t.opacity(7999, false) == 1);
  CHECK(t.opacity(8000 + Tag::kFadeMs, false) == 0);
}

TEST_CASE("replay 1.0.12: the controls' replay buttons have no Start over") {
  // No archive: no replay buttons at all.
  CHECK(replay::buttons(0, false, false).empty());
  CHECK(replay::buttons(0, true, true).empty());
  // Live: Pause, Rewind.
  CHECK(replay::buttons(2, false, false) == std::vector<Button>{Button::Pause, Button::Rewind});
  CHECK(replay::buttons(1, false, true) == std::vector<Button>{Button::Pause, Button::Rewind});
  // Behind live: Go live first, Pause after it on LG, then Rewind.
  CHECK(replay::buttons(2, true, false) == std::vector<Button>{Button::GoLive, Button::Rewind});
  CHECK(replay::buttons(2, true, true) == std::vector<Button>{Button::GoLive, Button::Pause, Button::Rewind});
  // Rewind (the timeline) is always there to start a programme over.
  for (bool behind : {false, true})
    for (bool lg : {false, true}) {
      std::vector<Button> b = replay::buttons(3, behind, lg);
      CHECK(std::count(b.begin(), b.end(), Button::Rewind) == 1);
      CHECK(b.size() <= 3);
    }
}

TEST_CASE("replay 1.0.12: the guide's chip shows the replay window") {
  CHECK(views::replayWindow(2) == "2 days");
  CHECK(views::replayWindow(1) == "1 day");
  CHECK(views::replayWindow(7) == "7 days");
  CHECK(views::replayWindow(0) == "");
}
