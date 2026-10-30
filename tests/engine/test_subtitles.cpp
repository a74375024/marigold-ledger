// What the player shows of a stream's captions (engine::Subtitles).
#include <doctest.h>

#include "core/engine/subtitles.hpp"

using namespace marigold::engine;

TEST_CASE("subtitles: language and caption service names") {
  CHECK(languageName("en") == "English");
  CHECK(languageName("eng") == "English");
  CHECK(languageName("es-419") == "Espa\xc3\xb1ol");
  CHECK(languageName("fra") == "Fran\xc3\xa7" "ais");
  CHECK(languageName("") == "");
  CHECK(languageName("xx") == "");
  CHECK(captionServiceLabel(1, "English") == "English");
  CHECK(captionServiceLabel(3, "English") == "Espa\xc3\xb1ol");
  CHECK(captionServiceLabel(1, "Espa\xc3\xb1ol") == "Espa\xc3\xb1ol");
  CHECK(captionServiceLabel(3, "Espa\xc3\xb1ol") == "English");
}

TEST_CASE("subtitles: a player's tracks count once text arrives, unless the player only lists real ones") {
  Subtitles s;
  s.begin(0, "English");
  s.playerTracks({{"t0.0", ""}}, false);
  CHECK_FALSE(s.present());  // ExoPlayer lists CC1 for any TS stream
  CHECK(s.pick("") == "t0.0");
  s.select("t0.0");
  s.playerLines({"HELLO THERE"});
  CHECK(s.tick(0, -1, true));
  CHECK(s.present());
  REQUIRE(s.lines().size() == 1);
  CHECK(s.lines()[0].text == "HELLO THERE");
  CHECK(s.selectedLabel() == "English");
  // Nothing drawn without the picture, or with none selected.
  CHECK(s.tick(10, -1, false));
  CHECK(s.lines().empty());
  s.select("");
  s.tick(20, -1, true);
  CHECK(s.lines().empty());

  Subtitles v;
  v.begin(0, "English");
  v.playerTracks({{"2", "Espa\xc3\xb1ol"}, {"1", ""}}, true);
  CHECK(v.present());  // VLC lists closed captions once it sees them
  CHECK(v.pick("") == "1");
  CHECK(v.pick("Espa\xc3\xb1ol") == "2");
  CHECK(v.pick("Deutsch") == "1");
}

TEST_CASE("subtitles: a new stream forgets the last one") {
  Subtitles s;
  s.begin(0, "English");
  s.playerTracks({{"t0.0", ""}}, true);
  s.select("t0.0");
  s.playerLines({"ONE", "", "TWO"});
  s.tick(0, -1, true);
  CHECK(s.lines().size() == 2);  // empty lines dropped
  s.begin(0, "Espa\xc3\xb1ol");
  CHECK_FALSE(s.present());
  CHECK(s.selected().empty());
  CHECK(s.lines().empty());
  CHECK(s.tracks().empty());
  CHECK_FALSE(s.fromStream());
}
