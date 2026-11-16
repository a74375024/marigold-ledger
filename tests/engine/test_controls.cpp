// Build 169 (1.0.12), the player's controls with icons and the programme
// bar: which buttons the row has and in which group, what the focused one
// says, where the buttons sit (the row always fits), and the bar's
// geometry live and behind live (live at the right end, ticks at programme
// starts, only the first one named, cut or left out). Pure functions: no
// store, no renderer.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/engine/controls.hpp"

using namespace marigold;
using namespace marigold::engine::controls;
using B = Button;

namespace {

constexpr time::Seconds kMin = 60;
// 8:20 PM on some day, and the programmes around it.
constexpr time::Seconds kNow = 1'700'000'000 - 1'700'000'000 % 3600 + 20 * kMin;
constexpr time::Seconds kHour = kNow - 20 * kMin;  // 8:00 PM

engine::Listing show(const std::string& title, time::Seconds start, time::Seconds end) {
  engine::Listing l;
  l.title = title;
  l.start = start;
  l.end = end;
  return l;
}

// Ten pixels a letter (a code point).
float tenEach(const std::string& s) {
  float n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) n += 10;
  return n;
}

std::vector<Group> groups(const std::vector<Button>& row) {
  std::vector<Group> out;
  for (Button b : row) out.push_back(groupOf(b));
  return out;
}

}  // namespace

TEST_CASE("controls 1.0.12: a channel without replay has no watching buttons") {
  RowIn in;
  in.back = true;
  in.favorite = true;
  CHECK(row(in) == std::vector<Button>{B::Subtitles, B::Sources, B::Back, B::Guide, B::Favorite, B::Settings});
  // Nothing to go back to: no Back.
  in.back = false;
  CHECK(row(in) == std::vector<Button>{B::Subtitles, B::Sources, B::Guide, B::Favorite, B::Settings});
  CHECK(opening(row(in), false) == 0);
}

TEST_CASE("controls 1.0.12: live on a channel with replay: Pause, Rewind, then the channel's, then elsewhere") {
  RowIn in;
  in.replayDays = 2;
  in.back = true;
  in.favorite = true;
  std::vector<Button> r = row(in);
  CHECK(r == std::vector<Button>{B::Pause, B::Rewind, B::Subtitles, B::Sources, B::Back, B::Guide, B::Favorite,
                                 B::Settings});
  CHECK(groups(r) == std::vector<Group>{Group::Watching, Group::Watching, Group::Channel, Group::Channel,
                                        Group::Elsewhere, Group::Elsewhere, Group::Elsewhere, Group::Elsewhere});
  // Subtitles focused when the row opens live.
  CHECK(opening(r, false) == 2);
  // LG's remote: the same live.
  in.lgPause = true;
  CHECK(row(in) == r);
}

TEST_CASE("controls 1.0.12: behind live (or paused): Live first; Pause after it on LG only") {
  RowIn in;
  in.replayDays = 1;
  in.behind = true;
  in.back = true;
  in.favorite = true;
  std::vector<Button> r = row(in);
  CHECK(r == std::vector<Button>{B::Live, B::Rewind, B::Subtitles, B::Sources, B::Back, B::Guide, B::Favorite,
                                 B::Settings});
  CHECK(opening(r, true) == 0);
  in.lgPause = true;
  CHECK(row(in) == std::vector<Button>{B::Live, B::Pause, B::Rewind, B::Subtitles, B::Sources, B::Back, B::Guide,
                                       B::Favorite, B::Settings});
}

TEST_CASE("controls 1.0.12: East/West and cameras sit with the channel's buttons; a game has no Favorite") {
  RowIn in;
  in.bothVersions = true;
  in.favorite = true;
  CHECK(row(in) == std::vector<Button>{B::Subtitles, B::Sources, B::Swap, B::Guide, B::Favorite, B::Settings});
  // A race with onboard cameras (a game: no Favorite), behind live on its
  // Final's replay.
  RowIn game;
  game.replayDays = 3;
  game.behind = true;
  game.cameras = true;
  game.back = true;
  std::vector<Button> r = row(game);
  CHECK(r == std::vector<Button>{B::Live, B::Rewind, B::Subtitles, B::Sources, B::Cameras, B::Back, B::Guide,
                                 B::Settings});
  CHECK(groupOf(B::Swap) == Group::Channel);
  CHECK(groupOf(B::Cameras) == Group::Channel);
  CHECK(groupOf(B::Back) == Group::Elsewhere);
}

TEST_CASE("controls 1.0.12: the focused button's label and state") {
  LabelIn in;
  CHECK(text(label(B::Live, in)) == "Live");
  in.finalChannel = "NBA TV";
  CHECK(text(label(B::Live, in)) == "NBA TV live");
  CHECK(text(label(B::Pause, in)) == "Pause");
  CHECK(text(label(B::Rewind, in)) == "Rewind");
  // Subtitles: off, on with a language, on with none named.
  Label off = label(B::Subtitles, in);
  CHECK(text(off) == "Subtitles \xc2\xb7 Off");
  CHECK_FALSE(off.dot);
  in.subtitlesOn = true;
  in.subtitles = "English";
  Label on = label(B::Subtitles, in);
  CHECK(on.title == "Subtitles");
  CHECK(on.state == "English");
  CHECK(text(on) == "Subtitles \xc2\xb7 English");
  CHECK(on.dot);
  in.subtitles = "On";
  CHECK(text(label(B::Subtitles, in)) == "Subtitles \xc2\xb7 On");
  CHECK(label(B::Subtitles, in).dot);
  // Sources: the copy playing and its quality.
  in.source = "Dream";
  in.quality = "1080p";
  CHECK(text(label(B::Sources, in)) == "Sources \xc2\xb7 Dream \xc2\xb7 1080p");
  in.source.clear();
  CHECK(text(label(B::Sources, in)) == "Sources \xc2\xb7 1080p");
  in.quality.clear();
  CHECK(text(label(B::Sources, in)) == "Sources");
  // East/West: the other version.
  CHECK(text(label(B::Swap, in)) == "Watch West");
  in.west = true;
  CHECK(text(label(B::Swap, in)) == "Watch East");
  // Back says where, whole (never shortened to "Back" for room).
  in.backTo = "Turner Classic Movies";
  CHECK(text(label(B::Back, in)) == "Back to Turner Classic Movies");
  CHECK(text(label(B::Guide, in)) == "Guide");
  in.game = true;
  CHECK(text(label(B::Guide, in)) == "Games");
  CHECK(text(label(B::Favorite, in)) == "Favorite");
  in.favorite = true;
  CHECK(text(label(B::Favorite, in)) == "Unfavorite");
  CHECK(text(label(B::Settings, in)) == "Settings");
  CHECK(text(label(B::Cameras, in)) == "Cameras");
  // Only Subtitles ever has the dot.
  for (Button b : {B::Live, B::Pause, B::Rewind, B::Sources, B::Swap, B::Cameras, B::Back, B::Guide, B::Favorite,
                   B::Settings})
    CHECK_FALSE(label(b, in).dot);
}

TEST_CASE("controls 1.0.12: the row's layout: round buttons, dividers between groups, the focused one grows") {
  std::vector<Button> r = {B::Pause, B::Rewind, B::Subtitles, B::Sources, B::Back, B::Guide, B::Settings};
  RowLayout none = layout(r, -1, 400, 1728);
  REQUIRE(none.slots.size() == r.size());
  for (const Slot& s : none.slots) CHECK(s.w == kSize);
  REQUIRE(none.dividers.size() == 2);
  CHECK(none.slots[1].x == kSize + kGap);
  CHECK(none.slots[2].x == 2 * kSize + kGap + kDivider);
  CHECK(none.dividers[0] == doctest::Approx(2 * kSize + kGap + kDivider / 2));
  CHECK(none.width == doctest::Approx(7 * kSize + 4 * kGap + 2 * kDivider));

  // Focus on Subtitles: it grows; the buttons before it stay put, the ones
  // after move right by what it grew.
  RowLayout focused = layout(r, 2, 300, 1728);
  CHECK(focused.slots[2].w == 300);
  for (size_t i = 0; i <= 2; ++i) CHECK(focused.slots[i].x == none.slots[i].x);
  for (size_t i = 3; i < r.size(); ++i) CHECK(focused.slots[i].x == doctest::Approx(none.slots[i].x + 300 - kSize));
  CHECK(focused.width == doctest::Approx(none.width + 300 - kSize));

  // A label too long for the room: the pill stops where the row fits.
  RowLayout tight = layout(r, 4, 5000, 1000);
  CHECK(tight.width == doctest::Approx(1000));
  CHECK(tight.slots[4].w == doctest::Approx(1000 - none.width + kSize));
  // Never narrower than round.
  CHECK(layout(r, 0, 10, 1728).slots[0].w == kSize);
  // The longest row there is still fits round on the 1920 layout.
  RowIn all;
  all.replayDays = 2;
  all.behind = true;
  all.lgPause = true;
  all.bothVersions = all.cameras = all.back = all.favorite = true;
  std::vector<Button> longest = row(all);
  CHECK(longest.size() == 11);
  CHECK(layout(longest, 0, 600, 1728).width <= 1728);
}

TEST_CASE("controls 1.0.12: the bar live: the show from start to end, the moment at now, the line under it") {
  engine::Listing ac = show("AC360", kHour, kHour + 60 * kMin);
  Bar b = bar({}, &ac, std::nullopt, kNow);
  CHECK_FALSE(b.replay);
  CHECK(b.from == kHour);
  CHECK(b.to == kHour + 60 * kMin);
  CHECK(b.at == doctest::Approx(20.0 / 60));
  CHECK(b.line == "AC360 \xc2\xb7 20 min in \xc2\xb7 40 min left");
  CHECK(b.ticks.empty());
  // Nothing known on now: an hour back, half an hour on, no line.
  Bar none = bar({}, nullptr, std::nullopt, kNow);
  CHECK(none.from == kNow - 3600);
  CHECK(none.to == kNow + 1800);
  CHECK(none.line.empty());
}

TEST_CASE("controls 1.0.12: the bar behind live: from the programme watched to live, live always the right end") {
  // Erin Burnett OutFront 7:00-8:00 from 7:21, then AC360; live 8:20.
  std::vector<engine::Listing> shows = {show("Erin Burnett OutFront", kHour - 60 * kMin, kHour),
                                        show("AC360", kHour, kHour + 60 * kMin),
                                        show("CNN NewsNight", kHour + 60 * kMin, kHour + 120 * kMin)};
  time::Seconds at = kHour - 39 * kMin;  // 7:21
  Bar b = bar(shows, &shows[1], at, kNow);
  CHECK(b.replay);
  CHECK(b.from == kHour - 60 * kMin);
  CHECK(b.to == kNow);
  REQUIRE(b.show);
  CHECK(b.show->title == "Erin Burnett OutFront");
  CHECK(b.at == doctest::Approx(21.0 / 80));
  // One tick, at AC360's start (NewsNight starts after live: none).
  REQUIRE(b.ticks.size() == 1);
  CHECK(b.ticks[0].at == doctest::Approx(60.0 / 80));
  CHECK(b.ticks[0].title == "AC360");
  CHECK(b.line.empty());

  // No programme known at the moment: half an hour before it.
  Bar unknown = bar({}, nullptr, at, kNow);
  CHECK(unknown.from == at - 30 * kMin);
  CHECK(unknown.to == kNow);
  CHECK(unknown.ticks.empty());
  CHECK(unknown.at == doctest::Approx(30.0 / 89));
}

TEST_CASE("controls 1.0.12: the bar behind live: a tick at every programme start between the moment and live") {
  // Inside Politics 3:00-4:00 from 3:40, five shows on the hour before live
  // at 8:20.
  time::Seconds three = kHour - 5 * 60 * kMin;
  std::vector<engine::Listing> shows = {show("Inside Politics", three, three + 60 * kMin)};
  const char* titles[] = {"The Lead with Jake Tapper", "The Situation Room", "Erin Burnett OutFront",
                          "AC360", "CNN NewsNight"};
  for (int i = 0; i < 5; ++i)
    shows.push_back(show(titles[i], three + (i + 1) * 60 * kMin, three + (i + 2) * 60 * kMin));
  Bar b = bar(shows, nullptr, three + 40 * kMin, kNow);
  REQUIRE(b.ticks.size() == 5);
  double span = 320;  // 3:00 to 8:20, in minutes
  for (int i = 0; i < 5; ++i) {
    CHECK(b.ticks[static_cast<size_t>(i)].at == doctest::Approx((i + 1) * 60 / span));
    CHECK(b.ticks[static_cast<size_t>(i)].title == titles[i]);
  }
  CHECK(b.at == doctest::Approx(40 / span));
  // A programme starting exactly at the moment, or at live, is no tick.
  Bar onStart = bar(shows, nullptr, three + 60 * kMin, kHour);
  CHECK(onStart.ticks.size() == 3);
}

TEST_CASE("controls 1.0.12: only the first tick is named, cut to its room, left out with no room") {
  // 1: the name fits.
  std::vector<engine::Listing> shows = {show("Erin Burnett OutFront", kHour - 60 * kMin, kHour),
                                        show("AC360", kHour, kHour + 60 * kMin)};
  Bar b = bar(shows, nullptr, kHour - 39 * kMin, kNow);
  float width = 800;
  float clockRight = static_cast<float>(b.at) * width + 40;
  TickName n = tickName(b, width, clockRight, tenEach);
  CHECK(n.text == "AC360");
  CHECK(n.x == doctest::Approx(600 + kNameAfterTick));

  // 2: a long name, several ticks: cut with "…" before the second tick.
  time::Seconds three = kHour - 5 * 60 * kMin;
  std::vector<engine::Listing> many = {show("Inside Politics", three, three + 60 * kMin)};
  for (int i = 1; i <= 5; ++i)
    many.push_back(show(i == 1 ? "The Lead with Jake Tapper" : "Show", three + i * 60 * kMin,
                        three + (i + 1) * 60 * kMin));
  Bar m = bar(many, nullptr, three + 40 * kMin, kNow);
  float w2 = 1280;  // 4 px a minute: ticks every 240 px from 240
  TickName cut = tickName(m, w2, static_cast<float>(m.at) * w2 + 40, tenEach);
  // From 248 to 480 - 12: 220 px, 22 letters with the "…".
  CHECK(cut.x == doctest::Approx(240 + kNameAfterTick));
  CHECK(cut.text == "The Lead with Jake Ta\xe2\x80\xa6");
  CHECK(tenEach(cut.text) <= 480 - kNameRoom - cut.x);

  // 3: no room: the tick stays, the name goes. From 7:55; AC360 at 8:00,
  // live 8:03.
  Bar close = bar(shows, nullptr, kHour - 5 * kMin, kHour + 3 * kMin);
  REQUIRE(close.ticks.size() == 1);
  TickName none = tickName(close, 800, static_cast<float>(close.at) * 800 + 40, tenEach);
  CHECK(none.text.empty());

  // The name never starts under the moment's clock.
  TickName after = tickName(b, width, 640, tenEach);
  CHECK(after.x == doctest::Approx(640 + kNameRoom));
  // Live: no ticks, no name.
  engine::Listing ac = shows[1];
  CHECK(tickName(bar({}, &ac, std::nullopt, kNow), 800, 0, tenEach).text.empty());
}

TEST_CASE("controls 1.0.12: a name fits whole, cut on a letter with an ellipsis, or not at all") {
  CHECK(fit("AC360", 50, tenEach) == "AC360");
  CHECK(fit("AC360", 49, tenEach) == "AC3\xe2\x80\xa6");
  // Three letters and the ellipsis at least.
  CHECK(fit("AC360", 40, tenEach) == "AC3\xe2\x80\xa6");
  CHECK(fit("AC360", 39, tenEach).empty());
  CHECK(fit("CNN", 29, tenEach).empty());
  // No space before the ellipsis; letters, not bytes.
  CHECK(fit("The Lead", 50, tenEach) == "The\xe2\x80\xa6");
  CHECK(fit("Fútbol Mexicano", 60, tenEach) == "Fútbo\xe2\x80\xa6");
  CHECK(fit("", 100, tenEach).empty());
}
