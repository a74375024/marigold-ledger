// The caption size and look as each player shows them: VLC (Apple TV) gets
// the same share of the screen the app draws, and every caption setting
// changed in a row is kept for the next launch.
#include <doctest.h>

#include <string>

#include "core/captions/caption_style.hpp"
#include "core/engine/store.hpp"
#include "engine/fake_host.hpp"

using namespace marigold::captions;

TEST_CASE("caption style: the app's letter sizes, clamped") {
  CHECK(letterPx(0) == 36);
  CHECK(letterPx(1) == 44);
  CHECK(letterPx(2) == 56);
  CHECK(letterPx(3) == 72);
  CHECK(letterPx(-1) == 36);
  CHECK(letterPx(9) == 72);
}

TEST_CASE("caption style: VLC's letters take the screen share the app's do") {
  CHECK(vlcRelativeFontSize(0) == 30);
  CHECK(vlcRelativeFontSize(1) == 25);  // Medium, the default: 43 px of 1080, 4% of the height
  CHECK(vlcRelativeFontSize(2) == 19);
  CHECK(vlcRelativeFontSize(3) == 15);
  for (int size = 0; size <= 3; ++size) {
    // Within a pixel of the app's own letters on a 1080-line picture, and
    // each size bigger than the one before (a smaller number).
    int px = 1080 / vlcRelativeFontSize(size);
    CHECK(px >= letterPx(size) - 1);
    CHECK(px <= letterPx(size) + 1);
    if (size > 0) CHECK(vlcRelativeFontSize(size) < vlcRelativeFontSize(size - 1));
  }
  // Never VLC's unset size (0: a tenth of the picture, twice Medium).
  CHECK(vlcRelativeFontSize(-5) > 10);
  CHECK(vlcRelativeFontSize(7) > 10);
}

TEST_CASE("caption style: only Box puts VLC's box behind the captions") {
  CHECK(vlcCaptionBox(0));
  CHECK_FALSE(vlcCaptionBox(1));
  CHECK_FALSE(vlcCaptionBox(2));
  CHECK(vlcCaptionBox(-1));  // clamped like the setting
  CHECK_FALSE(vlcCaptionBox(5));
}

TEST_CASE("caption style: the log line says the values only") {
  CHECK(vlcCaptionNote(1, 0, 0) == "size Medium (1/25 of the picture), look Box (boxed), timing +0 ms");
  CHECK(vlcCaptionNote(3, 1, 1500) == "size Extra large (1/15 of the picture), look Outline (edged), timing +1500 ms");
  CHECK(vlcCaptionNote(0, 2, -500) == "size Small (1/30 of the picture), look Shadow (edged), timing +0 ms");
}

TEST_CASE("caption style: each change from the panel is saved, and the last one comes back") {
  fake::Tv tv;
  {
    marigold::engine::Store store(tv.host());
    CHECK(store.settings().subtitleSize == 1);  // Medium until changed
    CHECK(store.settings().subtitleLook == 0);
    // One change at a time, as Aa and Settings make them.
    for (int size : {2, 3, 0}) {
      marigold::engine::Settings s = store.settings();
      s.subtitleSize = size;
      store.setSettings(s);
      CHECK(store.settings().subtitleSize == size);
    }
    for (int look : {1, 2}) {
      marigold::engine::Settings s = store.settings();
      s.subtitleLook = look;
      store.setSettings(s);
      CHECK(store.settings().subtitleLook == look);
    }
    marigold::engine::Settings s = store.settings();
    s.subtitleTop = true;
    s.subtitleSentenceCase = true;
    marigold::engine::setCaptionTiming(s, "", 1500);
    store.setSettings(s);
  }
  marigold::engine::Store again(tv.host());
  const marigold::engine::Settings& s = again.settings();
  CHECK(s.subtitleSize == 0);
  CHECK(s.subtitleLook == 2);
  CHECK(s.subtitleTop);
  CHECK(s.subtitleSentenceCase);
  CHECK(s.captionTiming == 1500);
}
