// What the player shows of a stream's captions (engine::Subtitles).
#include <doctest.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "core/engine/store.hpp"
#include "core/engine/subtitles.hpp"
#include "playback/ts_stream.hpp"

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

TEST_CASE("subtitles: English-language and Spanish-language channels keep their own choice") {
  Settings s;
  CHECK_FALSE(subtitleChoice(s, false).on);
  CHECK_FALSE(anySubtitles(s));
  setSubtitleChoice(s, true, {true, "Espa\xc3\xb1ol"});
  CHECK(subtitleChoice(s, true).on);
  CHECK(subtitleChoice(s, true).language == "Espa\xc3\xb1ol");
  CHECK_FALSE(subtitleChoice(s, false).on);  // ESPN stays without
  CHECK(anySubtitles(s));
  setSubtitleChoice(s, false, {true, ""});
  setSubtitleChoice(s, true, {false, "Espa\xc3\xb1ol"});
  CHECK(subtitleChoice(s, false).on);
  CHECK_FALSE(subtitleChoice(s, true).on);
  CHECK(subtitleChoice(s, true).language == "Espa\xc3\xb1ol");  // kept for next time
}

// ---------------------------------------------------------------- timing
//
// A live stream through the relay to a player, a millisecond at a time.
// Picture i reaches the relay at 33i ms (later by `stallMs` from `stallAt`
// on) and shows `buffer + unseen` later: `buffer` is what the player's
// buffer figure says, `unseen` its decoder, renderer and socket. It shows
// from the first picture a decoder can start on (after `leading` others).
// From two seconds after that picture to 32 s, a pop-on caption is
// completed (EOC) every 60 pictures; what's measured is when it appeared
// against when its picture showed (ms, + late).

namespace {

uint8_t odd(uint8_t b) {
  int bits = 0;
  for (int i = 0; i < 7; ++i) bits += (b >> i) & 1;
  return static_cast<uint8_t>((b & 0x7F) | (bits % 2 ? 0 : 0x80));
}

struct Scenario {
  int frames = 1100;
  int leading = 0;           // pictures before the first one a decoder can start on
  int jumpAt = -1;           // the PTS jumps by jumpBy at this picture
  int64_t jumpBy = 0;
  int64_t buffer = 3000;
  int64_t unseen = 300;
  int way = 0;               // its position counts from: 0 the first picture, 1 the first timestamp; -1 says none
  bool restarts = false;     // its position starts again from 0 at the PTS jump
  bool lead = true;          // it says its buffer's lead
  int stallAt = -1;          // the source stops for stallMs before this picture
  int64_t stallMs = 0;
  int delay = 0;             // Caption timing
};

struct Outcome {
  int completed = 0;
  std::vector<int64_t> offsets;  // per caption shown, in order
  std::vector<char> letters;     // the captions as they appeared
  std::string anchor;
  CaptionTiming timing;
};

Outcome simulate(const Scenario& s) {
  using namespace tsgen;
  Subtitles sub;
  sub.begin(1, "English");
  sub.setDelay(s.delay);
  auto arrival = [&](int i) { return 33 * static_cast<int64_t>(i) + (s.stallAt >= 0 && i >= s.stallAt ? s.stallMs : 0); };
  auto show = [&](int i) { return arrival(i) + s.buffer + s.unseen; };
  int k = s.leading;
  std::vector<int64_t> eocShow;  // when each caption's picture showed
  std::map<char, int64_t> appeared;
  Outcome out;
  Ts ts;
  int next = 0;          // the next picture to reach the relay
  int onScreen = -1;     // the picture showing
  int64_t reported = -1, reportedAt = 0;
  std::string lastText;
  int64_t end = show(s.frames - 1) + 2000;
  for (int64_t w = 0; w <= end; ++w) {
    while (next < s.frames && arrival(next) <= w) {
      int i = next++;
      int64_t pts = 1000000 + 33 * static_cast<int64_t>(i) + (s.jumpAt >= 0 && i >= s.jumpAt ? s.jumpBy : 0);
      std::vector<Triplet> t;
      int n = i - k - 60;
      char letter = static_cast<char>('A' + (n / 60) % 26);
      switch (n < 0 || i >= 960 ? -1 : n % 60) {
        case 0: t.push_back(cc(odd(0x14), odd(0x20))); break;  // RCL
        case 2: t.push_back(cc(odd(0x14), odd(0x2E))); break;  // ENM
        case 4: t.push_back(cc(odd(0x14), odd(0x70))); break;  // PAC, row 15
        case 6: t.push_back(cc(odd(static_cast<uint8_t>(letter)), odd(static_cast<uint8_t>(letter)))); break;
        case 10:
          t.push_back(cc(odd(0x14), odd(0x2F)));  // EOC
          eocShow.push_back(show(i));
          break;
        default: break;
      }
      Ts one;
      one.counters = ts.counters;
      if (i == 0) one.tables();
      Bytes head = i >= k ? Bytes{0x65, 0x88, 0x84} : Bytes{0x41, 0x9A, 0x00};
      one.video(90 * pts, h264(t, false, 0, 300, head));
      ts.counters = one.counters;
      sub.bytes(one.out.data(), one.out.size(), 1);
      if (i == 0) sub.select("cc1");
    }
    if (onScreen < 0 && show(k) <= w) onScreen = k;
    while (onScreen >= 0 && onScreen + 1 < s.frames && show(onScreen + 1) <= w) ++onScreen;
    // The player's position: the picture showing, continuous within it.
    int64_t media = -1;
    bool running = false;
    if (onScreen >= k) {
      int64_t since = w - show(onScreen);
      running = since < 33 || onScreen == s.frames - 1;
      media = 33 * static_cast<int64_t>(onScreen) + std::min<int64_t>(since, 33);
    }
    if (media >= 0 && (w - show(k)) % 200 == 0 && s.way >= 0) {
      reported = media - (s.way == 0 ? 33 * static_cast<int64_t>(k) : 0);
      if (s.restarts && s.jumpAt >= 0 && onScreen >= s.jumpAt) reported = media - 33 * static_cast<int64_t>(s.jumpAt);
      reportedAt = w;
    }
    if (w % 16 != 0) continue;
    PlayerClock c;
    c.showing = media >= 0;
    c.running = running;
    c.reportsPosition = s.way >= 0;
    if (reported > 0) {
      c.positionMs = reported;
      c.positionAgeMs = w - reportedAt;
    }
    if (s.lead && media > 0) {
      int64_t newest = 33 * static_cast<int64_t>(next - 1);
      // Waiting for data, its buffer figure says nothing is buffered.
      c.leadMs = running ? std::max<int64_t>(0, newest - media - s.unseen) : 0;
    }
    sub.tick(w, c);
    std::string text = sub.lines().empty() ? std::string() : sub.lines()[0].text;
    if (text != lastText && !text.empty() && !appeared.count(text[0])) {
      appeared[text[0]] = w;
      out.letters.push_back(text[0]);
    }
    lastText = text;
  }
  out.completed = static_cast<int>(eocShow.size());
  for (size_t n = 0; n < out.letters.size() && n < eocShow.size(); ++n)
    out.offsets.push_back(appeared[out.letters[n]] - eocShow[n]);
  out.anchor = sub.takeAnchorNote();
  sub.takeTiming(out.timing);
  return out;
}

void inOrder(const Outcome& o) {
  REQUIRE(static_cast<int>(o.letters.size()) == o.completed);
  for (size_t n = 0; n < o.letters.size(); ++n) CHECK(o.letters[n] == static_cast<char>('A' + n % 26));
}

void within(const Outcome& o, int64_t lo, int64_t hi) {
  for (size_t n = 0; n < o.offsets.size(); ++n) {
    CAPTURE(n);
    CHECK(o.offsets[n] >= lo);
    CHECK(o.offsets[n] <= hi);
  }
}

}  // namespace

TEST_CASE("subtitles: on the player's clock a caption shows with its picture") {
  Scenario s;
  s.unseen = 600;  // decoder, renderer and socket: no buffer figure counts them
  Outcome o = simulate(s);
  CHECK(o.completed == 15);
  inOrder(o);
  within(o, -50, 50);
  CHECK(o.anchor == "the player's clock counts from the first picture");
  CHECK(o.timing.clock == "currentTime");
  CHECK(o.timing.aheadMedian >= 3500);  // the stream read 3.6 s ahead of the picture
  CHECK(o.timing.aheadMedian <= 4000);
  CHECK(o.timing.errorMs >= 0);
  CHECK(o.timing.errorMs <= 50);
}

TEST_CASE("subtitles: the player's position counted from the first picture or from the first timestamp") {
  for (int way : {0, 1}) {
    CAPTURE(way);
    Scenario s;
    s.leading = 15;  // half a second of pictures before the first IDR
    s.way = way;
    Outcome o = simulate(s);
    CHECK(o.completed == 15);
    inOrder(o);
    within(o, -50, 50);
    CHECK(o.anchor == (way == 0 ? "the player's clock counts from the first picture"
                                : "the player's clock counts from the stream's first timestamp"));
  }
}

TEST_CASE("subtitles: without a position, the buffer's lead, and a default without either") {
  Scenario s;
  s.way = -1;
  s.unseen = 0;
  Outcome o = simulate(s);
  inOrder(o);
  within(o, -50, 50);
  CHECK(o.timing.clock == "buffer");
  // What the buffer figure leaves out shows as early.
  s.unseen = 600;
  o = simulate(s);
  inOrder(o);
  within(o, -650, -550);
  // Saying neither: a few seconds' lead, not one.
  s.lead = false;
  s.unseen = 0;
  o = simulate(s);
  inOrder(o);
  within(o, -50, 50);
  CHECK(o.timing.clock == "default");
}

TEST_CASE("subtitles: PTS going back keeps captions coming, in order") {
  for (int way : {0, -1}) {
    for (bool restarts : {false, true}) {
      CAPTURE(way);
      CAPTURE(restarts);
      Scenario s;
      s.way = way;
      s.unseen = way < 0 ? 0 : 300;
      s.jumpAt = 300;
      s.jumpBy = -60000;  // the relay reopened the source
      s.restarts = restarts;
      Outcome o = simulate(s);
      CHECK(o.completed == 15);
      inOrder(o);
      within(o, -50, 50);
      CHECK(o.timing.jumps >= 1);
    }
  }
}

TEST_CASE("subtitles: PTS jumping ahead lets no waiting captions out at once") {
  for (int way : {0, -1}) {
    CAPTURE(way);
    Scenario s;
    s.way = way;
    s.unseen = way < 0 ? 0 : 300;
    s.jumpAt = 300;
    s.jumpBy = 30000;
    Outcome o = simulate(s);
    CHECK(o.completed == 15);
    inOrder(o);
    within(o, -50, 50);
  }
}

TEST_CASE("subtitles: a stall holds captions with the picture") {
  for (int way : {0, -1}) {
    CAPTURE(way);
    Scenario s;
    s.way = way;
    s.unseen = way < 0 ? 0 : 300;
    s.stallAt = 300;
    s.stallMs = 5000;  // the buffer drains; its lead says 0 meanwhile
    Outcome o = simulate(s);
    CHECK(o.completed == 15);
    inOrder(o);
    within(o, -50, 50);
  }
}

TEST_CASE("subtitles: Caption timing moves captions by exactly its value, earlier only as far as the stream read") {
  Scenario s;
  Outcome on = simulate(s);
  for (int delay : {1500, -1000, 3000, -500}) {
    CAPTURE(delay);
    s.delay = delay;
    Outcome o = simulate(s);
    inOrder(o);
    REQUIRE(o.offsets.size() == on.offsets.size());
    for (size_t n = 0; n < o.offsets.size(); ++n) {
      CAPTURE(n);
      CHECK(std::llabs(o.offsets[n] - on.offsets[n] - delay) <= 16);  // a frame of the UI
    }
    CHECK(o.timing.delayMs == delay);
  }
  // 3 s earlier with 1.3 s read ahead: as early as the stream read allows,
  // less the pictures held for B-frame order.
  s.buffer = 1000;
  s.delay = -3000;
  Outcome o = simulate(s);
  inOrder(o);
  within(o, -1300 - 16, -1300 + 4 * 33 + 32);
}

TEST_CASE("subtitles: a player's own lines wait for a later Caption timing; earlier is on time") {
  for (int delay : {0, 1000, -1500}) {
    CAPTURE(delay);
    Subtitles s;
    s.begin(0, "English");
    s.setDelay(delay);
    s.playerTracks({{"t0.0", ""}}, false);
    s.select("t0.0");
    s.tick(1000, -1, true);
    s.playerLines({"HELLO"});
    int64_t shownAt = -1;
    for (int64_t t = 1016; t < 4000 && shownAt < 0; t += 16) {
      s.tick(t, -1, true);
      if (!s.lines().empty()) shownAt = t;
    }
    CHECK(shownAt - 1000 >= std::max(delay, 0));
    CHECK(shownAt - 1000 <= std::max(delay, 0) + 16);
  }
}

TEST_CASE("subtitles: Caption timing's words and the playback log's line") {
  CHECK(captionTimingLabel(0) == "On time");
  CHECK(captionTimingLabel(-500) == "0.5 s earlier");
  CHECK(captionTimingLabel(1500) == "1.5 s later");
  CHECK(captionTimingLabel(-3000) == "3 s earlier");
  CaptionTiming t;
  t.clock = "currentTime";
  t.samples = 120;
  t.aheadMedian = 2400;
  t.aheadMin = 1800;
  t.aheadMax = 3100;
  t.errorMs = 120;
  t.jumps = 1;
  t.delayMs = -1000;
  CHECK(captionTimingLog(t) ==
        "clock currentTime; ahead median 2400 ms (min 1800, max 3100); error \xc2\xb1"
        "120 ms; jumps 1; setting \xe2\x88\x92"
        "1.0 s");
  t.clock = "player";
  t.delayMs = 500;
  CHECK(captionTimingLog(t) == "clock player; setting +0.5 s");
}

TEST_CASE("subtitles: Caption timing for every channel, and a channel's own") {
  Settings s;
  CHECK(captionTiming(s, "ch:a") == 0);
  setCaptionTiming(s, "", -1200);  // to the nearest half second
  CHECK(s.captionTiming == -1000);
  setCaptionTiming(s, "ch:a", 9000);  // at most 3 s
  CHECK(captionTiming(s, "ch:a") == 3000);
  CHECK(captionTiming(s, "ch:b") == -1000);
  CHECK(hasOwnCaptionTiming(s, "ch:a"));
  CHECK_FALSE(hasOwnCaptionTiming(s, "ch:b"));
  CHECK_FALSE(hasOwnCaptionTiming(s, ""));
  clearOwnCaptionTiming(s, "ch:a");
  CHECK(captionTiming(s, "ch:a") == -1000);
  CHECK(clampCaptionTiming(250) == 500);
  CHECK(clampCaptionTiming(-249) == 0);
}
