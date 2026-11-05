// Build 142: the line under Live Events' title while its scores catch up,
// as the guide's "Filling in missing listings…": "Updating scores…" with a
// spinner while this open's round still has a stale league of a game around
// now to ask, a short note when scores can't come, and nothing otherwise.
#include <doctest.h>

#include <string>

#include "core/engine/events_build.hpp"

namespace eb = marigold::engine::events_build;
using Mark = marigold::engine::ScoresLine::Mark;

namespace {

eb::ScoresLineInput updating() {
  eb::ScoresLineInput in;
  in.shown = true;
  in.scoresOn = true;
  in.checked = false;
  in.anyAround = true;
  in.staleAround = true;
  in.sinceOpenMs = 1000;
  return in;
}

std::string text(const marigold::engine::ScoresLine& l) { return l.text ? l.text : ""; }

}  // namespace

TEST_CASE("scores line: updating while this open's round has a stale league to ask") {
  marigold::engine::ScoresLine l = eb::scoresLine(updating());
  CHECK(l.mark == Mark::Busy);
  CHECK(text(l) == "Updating scores\xe2\x80\xa6");
  // However long it takes (it goes when the round is over).
  eb::ScoresLineInput late = updating();
  late.sinceOpenMs = 60000;
  CHECK(eb::scoresLine(late).mark == Mark::Busy);
}

TEST_CASE("scores line: nothing when the round is over or the scores are fresh") {
  eb::ScoresLineInput done = updating();
  done.checked = true;
  CHECK(eb::scoresLine(done).text == nullptr);
  // Every league of the games around now answered lately: nothing to ask.
  eb::ScoresLineInput fresh = updating();
  fresh.staleAround = false;
  CHECK(eb::scoresLine(fresh).text == nullptr);
}

TEST_CASE("scores line: nothing without games around now, scores off, or the board not shown") {
  eb::ScoresLineInput none = updating();
  none.anyAround = false;
  CHECK(eb::scoresLine(none).text == nullptr);
  eb::ScoresLineInput off = updating();
  off.scoresOn = false;
  CHECK(eb::scoresLine(off).text == nullptr);
  // Placeholders keep their own loading line ("Getting live scores…").
  eb::ScoresLineInput hidden = updating();
  hidden.shown = false;
  CHECK(eb::scoresLine(hidden).text == nullptr);
}

TEST_CASE("scores line: the day's requests used up says so for a few seconds") {
  eb::ScoresLineInput capped = updating();
  capped.capped = true;
  marigold::engine::ScoresLine l = eb::scoresLine(capped);
  CHECK(l.mark == Mark::Paused);
  CHECK(text(l) == "Live scores paused until tomorrow");
  capped.sinceOpenMs = eb::kScoresNoteMs;
  CHECK(eb::scoresLine(capped).text == nullptr);
}

TEST_CASE("scores line: a source failing or refusing says so briefly, unless others are updating") {
  eb::ScoresLineInput down = updating();
  down.staleAround = false;
  down.unavailableAround = true;
  marigold::engine::ScoresLine l = eb::scoresLine(down);
  CHECK(l.mark == Mark::Paused);
  CHECK(text(l) == "Live scores unavailable right now");
  down.sinceOpenMs = eb::kScoresNoteMs + 1;
  CHECK(eb::scoresLine(down).text == nullptr);
  // One league down, another being asked: the spinner, not the note.
  eb::ScoresLineInput mixed = updating();
  mixed.unavailableAround = true;
  CHECK(eb::scoresLine(mixed).mark == Mark::Busy);
}
