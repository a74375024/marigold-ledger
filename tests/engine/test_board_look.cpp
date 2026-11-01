// Live Events' look (Build 124): each sport's icon in a chip of its own
// colour, the same on every screen, none of them red (red means live
// only); a show that isn't a game of a sport known gets no chip, a game of
// a sport not known the dashed ring; and what a loading board waits for,
// named under its title.
#include <doctest.h>

#include <fstream>
#include <set>
#include <string>

#include "core/engine/events_build.hpp"
#include "core/engine/events_model.hpp"
#include "core/sports/profiles.hpp"

using marigold::engine::EventsBoard;
using marigold::engine::EventTile;
using marigold::engine::sportColor;
using marigold::engine::tileMark;
namespace events_build = marigold::engine::events_build;

namespace {

// Red, or close to it: the live colour's family.
bool reddish(uint32_t c) {
  int r = static_cast<int>(c >> 16 & 0xFF), g = static_cast<int>(c >> 8 & 0xFF), b = static_cast<int>(c & 0xFF);
  return r > 0x90 && r > g * 2 && r > b * 2;
}

}  // namespace

TEST_CASE("events: every sport's icon has a chip colour, and none is red") {
  std::set<std::string> icons = {"soccer",   "basketball", "football", "hockey",   "fieldHockey", "baseball",
                                 "softball", "tennis",     "cricket",  "rugby",    "golf",        "motorsport",
                                 "fighting", "wrestling",  "volleyball", "lacrosse", "cycling",   "darts",
                                 "athletics", "horseRacing", "multi",  "other"};
  // Every icon a league's profile names is one of them.
  for (const marigold::sports::Profile& p : marigold::sports::profiles()) {
    INFO(p.league, " ", p.symbol);
    CHECK(icons.count(p.symbol));
  }
  for (const std::string& icon : icons) {
    INFO(icon);
    uint32_t c = sportColor(icon);
    CHECK(c != 0);
    CHECK_FALSE(reddish(c));
    // Its icon ships with the app.
    CHECK(std::ifstream(std::string(MARIGOLD_SOURCE_DIR) + "/app/icons/" + icon + ".png").good());
  }
  CHECK(reddish(0xFF5252));  // the live red is red by this measure
  CHECK(sportColor("baseball") == 0x34489A);
  CHECK(sportColor("softball") == sportColor("baseball"));
  CHECK(sportColor("soccer") == 0x2E9D57);
  CHECK(sportColor("darts") == 0x5A6B2F);
  CHECK(sportColor("not a sport") == 0);
  CHECK(sportColor("") == 0);
}

TEST_CASE("events: a tile's mark is a chip for a sport known, a ring for a game of one not known, else none") {
  using Mark = EventTile::Mark;
  CHECK(tileMark("hockey", true, false) == Mark::Chip);
  CHECK(tileMark("cricket", false, false) == Mark::Chip);  // an event of a sport known: its chip
  CHECK(tileMark("multi", false, false) == Mark::Chip);
  // Not a sport known: a game (from the schedule, or two sides) gets the
  // ring; a show in "Time not listed" gets nothing.
  CHECK(tileMark("other", true, false) == Mark::Unknown);
  CHECK(tileMark("", true, false) == Mark::Unknown);
  CHECK(tileMark("other", false, false) == Mark::None);
  CHECK(tileMark("", false, false) == Mark::None);
  CHECK(tileMark("not a sport", false, false) == Mark::None);
  // A high-school event of a sport not known: the "other" chip, as before.
  CHECK(tileMark("other", false, true) == Mark::Chip);
}

TEST_CASE("events: a loading board says what it waits for, the channel list first and the scores last") {
  using Stage = EventsBoard::Stage;
  events_build::Loading l;
  CHECK(events_build::loadStage(l) == Stage::Matching);  // loading, nothing else known: the games being worked out
  l.holding = true;
  l.hold.scores = true;
  CHECK(events_build::loadStage(l) == Stage::Scores);
  // The hold still for a list or schedule just in: being matched.
  l.hold.list = true;
  CHECK(events_build::loadStage(l) == Stage::Matching);
  l.hold = {false, true, false};
  CHECK(events_build::loadStage(l) == Stage::Matching);
  l.hold = {false, false, false};
  CHECK(events_build::loadStage(l) == Stage::Scores);
  l.matching = true;
  CHECK(events_build::loadStage(l) == Stage::Matching);
  l.schedule = true;
  CHECK(events_build::loadStage(l) == Stage::Schedule);
  l.list = true;
  CHECK(events_build::loadStage(l) == Stage::Channels);
  // A new board says nothing until a load.
  CHECK(EventsBoard{}.stage == Stage::None);
}
