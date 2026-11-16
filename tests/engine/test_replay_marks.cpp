// Build 167, replay marks and the replay banner: what a copy's archive gives
// its row, the mark's corner on the guide's logo cells, the guide's Start
// over, the "Can go back" filter, the banner during a replay and the
// About's depth line. Pure functions: no store, no renderer.
#include <doctest.h>

#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "core/engine/model.hpp"
#include "core/engine/replay.hpp"
#include "core/engine/views.hpp"

using namespace marigold;
using namespace marigold::engine;
namespace views = marigold::engine::views;

namespace {

constexpr time::Seconds kNow = 1'800'000'000;

Listing show(const std::string& title, time::Seconds start, time::Seconds end) {
  Listing l;
  l.title = title;
  l.start = start;
  l.end = end;
  return l;
}

// The guide's cell and its logo area (182 x 84; 154 x 60 at +14, +12).
constexpr views::Box kCell{100, 200, 182, 84};
constexpr views::Box kArea{114, 212, 154, 60};

bool same(views::Box a, views::Box b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }
bool clear(views::Box a, views::Box b) {
  return a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
}

// No count of anything: no digit followed by a word for channels or rows.
bool countsChannels(const std::string& s) {
  for (const char* word : {" channel", " row", " source", " copies", " copy"}) {
    for (size_t at = s.find(word); at != std::string::npos; at = s.find(word, at + 1))
      if (at > 0 && std::isdigit(static_cast<unsigned char>(s[at - 1]))) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("replay marks: what a copy's archive gives its row") {
  // As the provider says, untested.
  CHECK(replay::usableDays(2, -1, 0) == 2);
  // Tested and it played; depth not confirmed: the claim stands.
  CHECK(replay::usableDays(2, 1, 0) == 2);
  // Confirmed shallower: lowered.
  CHECK(replay::usableDays(2, 1, 1) == 1);
  // Never raised above the claim.
  CHECK(replay::usableDays(1, 1, 3) == 1);
  // Broken: nothing.
  CHECK(replay::usableDays(2, 0, 0) == 0);
  // No archive claimed: nothing, whatever was found.
  CHECK(replay::usableDays(0, 1, 2) == 0);
}

TEST_CASE("replay marks: the guide's Start over, on a programme on now begun over 3 min ago inside the archive") {
  // On now, begun 40 minutes ago, two days kept: offered.
  CHECK(replay::startOverOffered(kNow - 40 * 60, kNow + 20 * 60, kNow, 2));
  // Begun three minutes ago or less: not yet.
  CHECK_FALSE(replay::startOverOffered(kNow - 3 * 60, kNow + 57 * 60, kNow, 2));
  CHECK(replay::startOverOffered(kNow - 3 * 60 - 1, kNow + 57 * 60, kNow, 2));
  // A later show, or one that has ended: no.
  CHECK_FALSE(replay::startOverOffered(kNow + 30 * 60, kNow + 90 * 60, kNow, 2));
  CHECK_FALSE(replay::startOverOffered(kNow - 90 * 60, kNow - 30 * 60, kNow, 2));
  // A channel without replay: no.
  CHECK_FALSE(replay::startOverOffered(kNow - 40 * 60, kNow + 20 * 60, kNow, 0));
  // Its start past the usable archive (a day less an hour): no.
  CHECK_FALSE(replay::startOverOffered(kNow - 23 * 3600 - 30 * 60, kNow + 600, kNow, 1));
  CHECK(replay::startOverOffered(kNow - 23 * 3600 - 30 * 60, kNow + 600, kNow, 2));
}

TEST_CASE("replay marks: the mark's corner and which logos shrink to clear it") {
  views::Box mark = views::replayMark(kCell);
  CHECK(same(mark, {108, 262, 16, 16}));  // x + 8, y + h - 22, 16 square
  views::Box corner = views::replayCorner(kCell);
  // Fitted as ui::logo fits.
  CHECK(same(views::fitted(kArea, 1.0f), {161, 212, 60, 60}));
  CHECK(same(views::fitted(kArea, 154.0f / 38.0f), {114, 223, 154, 38}));

  // A wide logo (ESPN's shape): the corner is free, the logo as today.
  CHECK(same(views::logoArea(kArea, 154.0f / 38.0f, corner, true), kArea));
  // A square logo: plenty of room.
  CHECK(same(views::logoArea(kArea, 1.0f, corner, true), kArea));
  // A 16:9 logo sits clear of it too.
  CHECK(same(views::logoArea(kArea, 16.0f / 9.0f, corner, true), kArea));
  // One that fills the whole area: 15% smaller, centred across, at the top,
  // clear of the corner.
  views::Box full = views::logoArea(kArea, 154.0f / 60.0f, corner, true);
  CHECK(static_cast<double>(full.w) == doctest::Approx(154 * 0.85));
  CHECK(static_cast<double>(full.h) == doctest::Approx(60 * 0.85));
  CHECK(full.y == kArea.y);
  CHECK(static_cast<double>(full.x + full.w * 0.5f) == doctest::Approx(static_cast<double>(kArea.x + kArea.w * 0.5f)));
  CHECK(clear(views::fitted(full, 154.0f / 60.0f), corner));
  // The same logo without replay: exactly as today.
  CHECK(same(views::logoArea(kArea, 154.0f / 60.0f, corner, false), kArea));
  // Never smaller than needed for any shape, and always clear.
  for (float aspect : {0.5f, 1.0f, 1.5f, 2.0f, 2.3f, 2.6f, 3.0f, 3.5f, 4.0f, 5.0f, 8.0f}) {
    CAPTURE(aspect);
    views::Box a = views::logoArea(kArea, aspect, corner, true);
    CHECK(clear(views::fitted(a, aspect), corner));
    CHECK(a.w >= kArea.w * 0.5f);
  }
}

TEST_CASE("replay marks: Can go back shows the channels with replay, and only offered when there are some") {
  Channels ch;
  GuideChannel a, b, c;
  a.id = "a";
  a.section = "Sports";
  a.replayDays = 2;
  b.id = "b";
  b.section = "News";
  c.id = "c";
  c.section = "News";
  c.replayDays = 1;
  ch.all = {a, b, c};
  for (int i = 0; i < 3; ++i) {
    ch.byId[ch.all[static_cast<size_t>(i)].id] = i;
    ch.lineup.push_back(i);
    ch.browse.push_back(i);
  }
  ch.lineupConfigured = true;
  views::Filter back{views::Filter::Kind::CanGoBack, {}};
  views::List list = views::guide(ch, nullptr, views::Tab::Lineup, back, kNow);
  std::vector<std::string> shown;
  for (const views::Line& l : list.lines)
    if (l.kind == views::Line::Kind::Channel) shown.push_back(ch.all[static_cast<size_t>(l.channel)].id);
  CHECK(shown == std::vector<std::string>{"a", "c"});
  CHECK(views::anyReplay(ch, ch.lineup));
  CHECK_FALSE(views::anyReplay(ch, {1}));
  CHECK(std::string(views::kCanGoBack) == "Can go back");
}

TEST_CASE("replay banner: the programme at the moment, where it is in it, next in the replay and live") {
  // 7:00 Erin Burnett, 8:00 Anderson Cooper (on now), 9:00 NewsNight, 10:00.
  time::Seconds seven = kNow - kNow % 3600 - 3600;
  std::vector<Listing> shows = {show("Erin Burnett OutFront", seven, seven + 3600),
                                show("Anderson Cooper 360", seven + 3600, seven + 7200),
                                show("CNN NewsNight", seven + 7200, seven + 10800),
                                show("Laura Coates Live", seven + 10800, seven + 14400)};
  time::Seconds now = seven + 3600 + 20 * 60;  // 8:20
  time::Seconds at = seven + 21 * 60;          // 7:21, 59 min behind
  views::ReplayBanner b = views::replayBanner(shows, &shows[1], at, now);
  REQUIRE(b.show);
  CHECK(b.show->title == "Erin Burnett OutFront");
  CHECK(b.progress == doctest::Approx(21.0 / 60.0));
  CHECK(b.inLeft == "21 min in \xc2\xb7 39 min left");
  CHECK(b.liveLine == "Live now: Anderson Cooper 360 \xc2\xb7 59 min behind");
  // Next in the replay: from the end of the show on screen.
  REQUIRE(b.next.size() == 3);
  CHECK(b.next[0]->title == "Anderson Cooper 360");
  CHECK(b.next[1]->title == "CNN NewsNight");
  CHECK(b.next[2]->title == "Laura Coates Live");

  // More than an hour behind.
  views::ReplayBanner far = views::replayBanner(shows, &shows[1], seven + 5 * 60, now + 10 * 60);
  CHECK(far.liveLine == "Live now: Anderson Cooper 360 \xc2\xb7 1 h 25 min behind");

  // No listing for the moment: no show (the banner names the channel and
  // "Watching from"), live still said; next from the moment.
  views::ReplayBanner none = views::replayBanner(shows, nullptr, seven - 30 * 60, now);
  CHECK(none.show == nullptr);
  CHECK(none.inLeft.empty());
  CHECK(none.liveLine == "1 h 50 min behind live");
  REQUIRE(!none.next.empty());
  CHECK(none.next[0]->title == "Erin Burnett OutFront");

  // Nothing known at all.
  views::ReplayBanner empty = views::replayBanner({}, nullptr, at, now);
  CHECK(empty.show == nullptr);
  CHECK(empty.next.empty());
  CHECK(empty.liveLine == "59 min behind live");

  for (const std::string& s : {b.inLeft, b.liveLine, far.liveLine, none.liveLine}) CHECK_FALSE(countsChannels(s));
}

TEST_CASE("replay banner: the guide's listings first, the kept past and the copy's own past fill the gaps") {
  time::Seconds h = kNow - kNow % 3600;
  Listing guide = show("Guide 8:00", h, h + 3600);
  guide.episode = "Episode";
  std::vector<Listing> past = {show("Past 6:00", h - 7200, h - 3600), show("Past 7:00", h - 3600, h),
                               show("Past 8:00 (same slot)", h, h + 3600)};
  std::vector<Listing> merged = views::mergedListings({guide}, past);
  REQUIRE(merged.size() == 3);
  CHECK(merged[0].title == "Past 6:00");
  CHECK(merged[1].title == "Past 7:00");
  CHECK(merged[2].title == "Guide 8:00");
  CHECK(merged[2].episode == "Episode");
  // A copy's own programme overlapping by less than half fills in.
  std::vector<Listing> own = {show("Own 5:00", h - 3 * 3600, h - 7200 + 600)};
  std::vector<Listing> more = views::mergedListings(merged, own);
  REQUIRE(more.size() == 4);
  CHECK(more[0].title == "Own 5:00");
}

TEST_CASE("replay marks: the About's depth by provider, never a count") {
  using ByProvider = std::vector<std::pair<std::string, uint16_t>>;
  CHECK(views::replayAbout({}) == "");
  CHECK(views::replayAbout(ByProvider{{"Dream", 2}}) ==
        "2 days on Dream. Pause, rewind and start over work on it, and its games can be watched from the start.");
  std::string two = views::replayAbout(ByProvider{{"Dream", 2}, {"Trex", 1}});
  CHECK(two.rfind("2 days on Dream, 1 day on Trex. ", 0) == 0);
  CHECK_FALSE(countsChannels(two));
  CHECK(views::behindLine(59 * 60 + 30) == "59 min behind");
  CHECK(views::behindLine(65 * 60) == "1 h 5 min behind");
  CHECK_FALSE(countsChannels(views::kCanGoBack));
}
