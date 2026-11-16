// Build 166: each copy's record of its real plays (and, from Test all sources, its
// probes): what is counted, how older results fade, the saved form and its
// size, how a channel's copies are ordered by it, and the providers'
// roll-up for the playback log.
#include <doctest.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/engine/feed_scores.hpp"
#include "core/sports/store.hpp"
#include "core/util/json.hpp"

namespace scores = marigold::engine::scores;
using scores::Book;

namespace {

constexpr marigold::time::Seconds kNow = 1'800'000'000;
constexpr marigold::time::Seconds kDay = 86400;

// The order's view of each copy, as the store gives it: the book's
// standing, the tier a name claims where no picture was measured.
std::function<scores::Standing(const std::string&)> standingIn(const Book& b,
                                                              std::map<std::string, std::string> claims = {}) {
  return [&b, claims](const std::string& id) {
    scores::Standing s = b.standing(id, kNow);
    auto c = claims.find(id);
    if (s.tier == 0 && c != claims.end()) s.tier = scores::claimedTier(c->second);
    return s;
  };
}

// `n` probes (Test all sources) that showed a picture `ms` after the request.
void probed(Book& b, const std::string& id, uint32_t ms, int height, int fps, int n = 1) {
  scores::Probe p;
  p.ok = true;
  p.startMs = ms;
  p.width = height * 16 / 9;
  p.height = height;
  p.fps = fps;
  for (int i = 0; i < n; ++i) b.recordProbe(id, p, kNow);
}

}  // namespace

TEST_CASE("feed scores: starts, failures, stalls, time played and the picture go into the copy's record") {
  Book b;
  CHECK(b.find("xt:a|1") == nullptr);
  CHECK_FALSE(b.score("xt:a|1", kNow));
  b.started("xt:a|1", 1800, kNow);
  b.started("xt:a|1", 2600, kNow);
  b.started("xt:a|1", 1500, kNow);
  b.failed("xt:a|1", kNow);
  b.stalled("xt:a|1", kNow);
  b.played("xt:a|1", 3600 * 1000, kNow);
  b.picture("xt:a|1", 1920, 1080, 60, kNow);
  const scores::Record* r = b.find("xt:a|1");
  REQUIRE(r);
  CHECK(r->plays == doctest::Approx(3));
  CHECK(r->failures == doctest::Approx(1));
  CHECK(r->stalls == doctest::Approx(1));
  CHECK(r->playedS == doctest::Approx(3600));
  CHECK(r->medianStartMs() == 1800u);
  REQUIRE(r->stallsPerHour());
  CHECK(*r->stallsPerHour() == doctest::Approx(1.0));
  CHECK(r->height == 1080);
  CHECK(r->fps == 60);
  CHECK(r->seen == kNow);
  REQUIRE(b.score("xt:a|1", kNow));
  // The median of the recent starts only: the oldest drop out.
  for (int i = 0; i < 20; ++i) b.started("xt:a|1", 900, kNow);
  CHECK(b.find("xt:a|1")->startMs.size() == Book::kSamples);
  CHECK(b.find("xt:a|1")->medianStartMs() == 900u);
  // A short play says little about stalls: under a minute, nothing.
  b.stalled("xt:a|2", kNow);
  b.played("xt:a|2", 30 * 1000, kNow);
  CHECK_FALSE(b.find("xt:a|2")->stallsPerHour());
  // Nothing for an empty id.
  b.started("", 100, kNow);
  CHECK(b.size() == 2);
}

TEST_CASE("feed scores: older results count less (a week's half-life)") {
  Book b;
  for (int i = 0; i < 4; ++i) b.failed("xt:a|1", kNow);
  // A week on: the four failures weigh two, and the plays since count fully.
  b.started("xt:a|1", 1500, kNow + 7 * kDay);
  const scores::Record* r = b.find("xt:a|1");
  CHECK(r->failures == doctest::Approx(2.0));
  CHECK(r->plays == doctest::Approx(1.0));
  // Read later without a new result: the weights as they stand then.
  scores::Record later = r->at(kNow + 21 * kDay);
  CHECK(later.failures == doctest::Approx(0.5));
  CHECK(later.plays == doctest::Approx(0.25));
  // Too little left to order by: no score.
  CHECK(b.score("xt:a|1", kNow + 7 * kDay));
  CHECK_FALSE(b.score("xt:a|1", kNow + 30 * kDay));
  // A copy that failed a month ago and has played well since reads well.
  Book c;
  for (int i = 0; i < 6; ++i) c.failed("xt:b|1", kNow);
  for (int i = 0; i < 6; ++i) c.started("xt:b|1", 1600, kNow + 28 * kDay);
  Book d;
  for (int i = 0; i < 6; ++i) d.failed("xt:b|1", kNow + 28 * kDay);
  for (int i = 0; i < 6; ++i) d.started("xt:b|1", 1600, kNow + 28 * kDay);
  CHECK(*c.score("xt:b|1", kNow + 28 * kDay) > *d.score("xt:b|1", kNow + 28 * kDay) + 0.1);
}

TEST_CASE("feed scores: saved and read back; the newest copies kept") {
  Book b;
  b.started("xt:a|1", 1700, kNow);
  b.failed("xt:a|1", kNow);
  b.stalled("xt:a|1", kNow);
  b.played("xt:a|1", 1234567, kNow);
  b.picture("xt:a|1", 1280, 720, 30, kNow);
  scores::Probe p;
  p.ok = true;
  p.startMs = 2100;
  p.width = 1920;
  p.height = 1080;
  p.fps = 50;
  p.archiveOk = true;
  p.archiveDays = 1;
  b.recordProbe("m3u:c|9", p, kNow + 60);
  b.rolledUpAt = kNow - 5;
  std::string text = b.toJson();
  CHECK(text.find("://") == std::string::npos);
  Book back = Book::fromJson(text);
  CHECK(back.size() == 2);
  CHECK(back.rolledUpAt == kNow - 5);
  const scores::Record* r = back.find("xt:a|1");
  REQUIRE(r);
  CHECK(r->plays == doctest::Approx(1));
  CHECK(r->failures == doctest::Approx(1));
  CHECK(r->stalls == doctest::Approx(1));
  CHECK(r->playedS == doctest::Approx(1235));
  CHECK(r->startMs == std::vector<uint32_t>{1700});
  CHECK(r->width == 1280);
  CHECK(r->height == 720);
  CHECK(r->fps == 30);
  CHECK(r->seen == kNow);
  const scores::Record* q = back.find("m3u:c|9");
  REQUIRE(q);
  CHECK(q->probes == doctest::Approx(1));
  CHECK(q->archiveOk == 1);
  CHECK(q->archiveDays == 1);
  CHECK(q->archiveAt == kNow + 60);
  CHECK(q->height == 1080);
  CHECK(back.toJson() == text);
  // Garbage or another version: empty.
  CHECK(Book::fromJson("nope").size() == 0);
  CHECK(Book::fromJson(R"({"v":2,"feeds":[["x",1,100]]})").size() == 0);

  // Bounded: the least recently seen go first; a copy seen again stays.
  Book big;
  for (size_t i = 0; i < Book::kCap + 50; ++i) {
    big.started("xt:a|" + std::to_string(i), 1500, kNow + static_cast<marigold::time::Seconds>(i));
    if (i + 1 == Book::kCap) big.played("xt:a|0", 60000, kNow + static_cast<marigold::time::Seconds>(i));
  }
  CHECK(big.size() == Book::kCap);
  CHECK(big.find("xt:a|0"));
  CHECK_FALSE(big.find("xt:a|1"));
  CHECK(big.find("xt:a|" + std::to_string(Book::kCap + 49)));
  // A file over the cap (an older build's) reads its newest kCap.
  std::string bigText = big.toJson();
  CHECK(bigText.size() < 300 * 1024);  // under a hundred bytes a copy
  CHECK(Book::fromJson(bigText).size() == Book::kCap);
}

TEST_CASE("feed scores: a probe (Test all sources) counts as half a play, into the same record") {
  Book b;
  scores::Probe ok;
  ok.ok = true;
  ok.startMs = 1600;
  ok.height = 1080;
  ok.fps = 60;
  scores::Probe bad;
  b.recordProbe("xt:a|1", ok, kNow);
  CHECK(b.find("xt:a|1")->starts() == doctest::Approx(0.5));
  CHECK(b.find("xt:a|1")->probedAt == kNow);
  // Test all sources: the test probes each copy once, which is enough to order by
  // while that probe is recent (about two weeks)...
  CHECK(b.score("xt:a|1", kNow));
  CHECK(b.score("xt:a|1", kNow + 13 * 86400));
  CHECK_FALSE(b.score("xt:a|1", kNow + 15 * 86400));
  // ... and played copies still need two starts.
  b.started("xt:a|3", 1500, kNow);
  CHECK_FALSE(b.score("xt:a|3", kNow));
  b.recordProbe("xt:a|2", bad, kNow);
  CHECK(b.find("xt:a|2")->probeFailures == doctest::Approx(1));
  CHECK(b.find("xt:a|2")->archiveOk == -1);  // the archive wasn't tried
  CHECK(b.find("xt:a|1")->plays == 0);       // plays are real plays only
}

TEST_CASE("feed scores: a clearly better measured copy goes ahead; no history keeps its place; the first kept") {
  Book b;
  // Copy 3 starts every time, fast, at 1080p60; copy 1 mostly fails.
  for (int i = 0; i < 4; ++i) b.started("p|3", 1500, kNow);
  b.picture("p|3", 1920, 1080, 60, kNow);
  b.started("p|1", 9000, kNow);
  for (int i = 0; i < 3; ++i) b.failed("p|1", kNow);
  auto standing = standingIn(b);

  std::vector<std::string> ids = {"p|1", "p|2", "p|3"};
  scores::orderMeasured(ids, 0, standing);
  // p|2 has no history (and claims nothing): it keeps its place; the
  // measured two swap.
  CHECK(ids == std::vector<std::string>{"p|3", "p|2", "p|1"});

  // The pinned (or kept last played) copy stays first.
  ids = {"p|1", "p|2", "p|3"};
  scores::orderMeasured(ids, 1, standing);
  CHECK(ids == std::vector<std::string>{"p|1", "p|2", "p|3"});

  // No history at all: today's order.
  Book none;
  ids = {"p|1", "p|2", "p|3"};
  scores::orderMeasured(ids, 0, standingIn(none));
  CHECK(ids == std::vector<std::string>{"p|1", "p|2", "p|3"});

  // Near-equal copies keep the catalog's order (a little faster isn't
  // clearly better).
  Book close;
  for (int i = 0; i < 4; ++i) close.started("q|1", 2000, kNow);
  for (int i = 0; i < 4; ++i) close.started("q|2", 1700, kNow);
  ids = {"q|1", "q|2"};
  scores::orderMeasured(ids, 0, standingIn(close));
  CHECK(ids == std::vector<std::string>{"q|1", "q|2"});
}

// Build 169 (1.0.12): channels started on 720p ahead of 1080p after Test
// all sources: the picture made a tenth of the score, so it never moved a
// copy on its own. Now a working copy's picture tier comes first.
TEST_CASE("best working picture: what works, and the tiers") {
  // Works: (ok + 0.9) / (ok + bad + 1), probes at half a play; at least
  // 0.8, and a median start within 4 s.
  Book b;
  probed(b, "t|probe", 1500, 1080, 50);
  CHECK(b.find("t|probe")->works() == doctest::Approx(1.4 / 1.5));
  CHECK(b.standing("t|probe", kNow).measured);
  CHECK(b.standing("t|probe", kNow).works);  // one good probe is enough
  CHECK(b.standing("t|probe", kNow).tier == 5);
  b.recordProbe("t|bad", scores::Probe{}, kNow);
  CHECK(b.standing("t|bad", kNow).measured);
  CHECK_FALSE(b.standing("t|bad", kNow).works);
  for (int i = 0; i < 4; ++i) b.started("t|4of5", 2000, kNow);
  b.failed("t|4of5", kNow);
  CHECK(b.standing("t|4of5", kNow).works);
  for (int i = 0; i < 3; ++i) b.started("t|3of4", 2000, kNow);
  b.failed("t|3of4", kNow);
  CHECK_FALSE(b.standing("t|3of4", kNow).works);
  for (int i = 0; i < 4; ++i) b.started("t|slow", 5200, kNow);
  CHECK(b.standing("t|slow", kNow).measured);
  CHECK_FALSE(b.standing("t|slow", kNow).works);
  for (int i = 0; i < 4; ++i) b.started("t|4s", 4000, kNow);
  CHECK(b.standing("t|4s", kNow).works);
  // One start isn't enough to order by (two are, or a probe), but its
  // picture is known.
  b.started("t|once", 1500, kNow);
  b.picture("t|once", 1280, 720, 60, kNow);
  CHECK_FALSE(b.standing("t|once", kNow).measured);
  CHECK(b.standing("t|once", kNow).tier == 3);
  CHECK_FALSE(b.standing("t|none", kNow).measured);
  CHECK(b.standing("t|none", kNow).tier == 0);

  CHECK(scores::pictureTier(2160, 60) == 5);
  CHECK(scores::pictureTier(1080, 60) == 5);
  CHECK(scores::pictureTier(1088, 50) == 5);
  CHECK(scores::pictureTier(1080, 30) == 4);
  CHECK(scores::pictureTier(1080, 0) == 4);
  CHECK(scores::pictureTier(720, 60) == 3);
  CHECK(scores::pictureTier(720, 25) == 2);
  CHECK(scores::pictureTier(576, 50) == 1);
  CHECK(scores::pictureTier(0, 60) == 0);
  // A claim counts at its best: names seldom give the frame rate.
  CHECK(scores::claimedTier("4K") == 5);
  CHECK(scores::claimedTier("FHD \xc2\xb7 60") == 5);
  CHECK(scores::claimedTier("FHD") == 5);
  CHECK(scores::claimedTier("HD") == 3);
  CHECK(scores::claimedTier("SD \xc2\xb7 60") == 1);
  CHECK(scores::claimedTier("60 fps") == 0);
  CHECK(scores::claimedTier("") == 0);
}

TEST_CASE("best working picture: 1080p before a faster 720p, 60 before 30, failing and slow copies after") {
  Book b;
  // 720p starting in 1.5 s; 1080p in 3 s (both always).
  for (int i = 0; i < 4; ++i) b.started("a|720", 1500, kNow);
  b.picture("a|720", 1280, 720, 30, kNow);
  for (int i = 0; i < 4; ++i) b.started("b|1080", 3000, kNow);
  b.picture("b|1080", 1920, 1080, 30, kNow);
  // The faster 720p scores higher (and stayed first before Build 169).
  CHECK(*b.score("a|720", kNow) > *b.score("b|1080", kNow));
  std::vector<std::string> ids = {"a|720", "b|1080"};
  scores::orderMeasured(ids, 0, standingIn(b));
  CHECK(ids == std::vector<std::string>{"b|1080", "a|720"});

  // 1080p60 ahead of 1080p30 (from Test all sources: one probe each).
  Book t;
  probed(t, "x|30", 1500, 1080, 30);
  probed(t, "y|60", 1900, 1080, 60);
  probed(t, "z|720", 1200, 720, 60);
  ids = {"z|720", "x|30", "y|60"};
  scores::orderMeasured(ids, 0, standingIn(t));
  CHECK(ids == std::vector<std::string>{"y|60", "x|30", "z|720"});

  // Within a tier: speed and smoothness where clearly better, else the
  // catalog's order.
  Book same;
  for (int i = 0; i < 4; ++i) same.started("s|good", 1500, kNow);
  same.played("s|good", 7200 * 1000, kNow);
  same.picture("s|good", 1920, 1080, 60, kNow);
  for (int i = 0; i < 4; ++i) same.started("s|bad", 3900, kNow);
  same.played("s|bad", 3600 * 1000, kNow);
  for (int i = 0; i < 8; ++i) same.stalled("s|bad", kNow);
  same.picture("s|bad", 1920, 1080, 60, kNow);
  probed(same, "s|fast", 1200, 1080, 60, 4);
  probed(same, "s|near", 1500, 1080, 60, 4);
  ids = {"s|bad", "s|good"};
  scores::orderMeasured(ids, 0, standingIn(same));
  CHECK(ids == std::vector<std::string>{"s|good", "s|bad"});
  ids = {"s|near", "s|fast"};
  scores::orderMeasured(ids, 0, standingIn(same));
  CHECK(ids == std::vector<std::string>{"s|near", "s|fast"});

  // A 1080p that fails goes after a working 720p; so does a slow one.
  Book f;
  for (int i = 0; i < 4; ++i) f.started("w|720", 2000, kNow);
  f.picture("w|720", 1280, 720, 50, kNow);
  f.started("f|1080", 2000, kNow);
  for (int i = 0; i < 3; ++i) f.failed("f|1080", kNow);
  f.picture("f|1080", 1920, 1080, 60, kNow);
  for (int i = 0; i < 4; ++i) f.started("s|1080", 6000, kNow);
  f.picture("s|1080", 1920, 1080, 60, kNow);
  ids = {"f|1080", "s|1080", "w|720"};
  scores::orderMeasured(ids, 0, standingIn(f));
  // The two that don't work by score, as before: the slow one starts.
  CHECK(ids == std::vector<std::string>{"w|720", "s|1080", "f|1080"});
}

TEST_CASE("best working picture: copies with no history keep their places unless they claim less") {
  Book b;
  probed(b, "m|1080", 2000, 1080, 50);
  std::map<std::string, std::string> claims = {{"u|hd", "HD"}, {"u|fhd", "FHD"}, {"u|sd", "SD"}};
  auto standing = standingIn(b, claims);
  // It claims HD: the measured 1080p50 goes ahead of it.
  std::vector<std::string> ids = {"u|hd", "m|1080"};
  scores::orderMeasured(ids, 0, standing);
  CHECK(ids == std::vector<std::string>{"m|1080", "u|hd"});
  // It claims as good (FHD, whatever its frame rate), or nothing: it keeps
  // its place.
  for (const char* u : {"u|fhd", "u|plain"}) {
    ids = {u, "m|1080"};
    scores::orderMeasured(ids, 0, standing);
    CHECK(ids == std::vector<std::string>{u, "m|1080"});
  }
  // Past each copy it outranks in turn, stopping at one it doesn't; the
  // unmeasured ones keep their order among themselves.
  ids = {"u|fhd", "u|sd", "u|hd", "m|1080"};
  scores::orderMeasured(ids, 0, standing);
  CHECK(ids == std::vector<std::string>{"u|fhd", "m|1080", "u|sd", "u|hd"});
  // A copy that doesn't work never passes one with no history.
  Book f;
  for (int i = 0; i < 4; ++i) f.failed("f|1080", kNow);
  f.picture("f|1080", 1920, 1080, 60, kNow);
  ids = {"u|sd", "f|1080"};
  scores::orderMeasured(ids, 0, standingIn(f, claims));
  CHECK(ids == std::vector<std::string>{"u|sd", "f|1080"});
  // Never past the kept first copy.
  ids = {"u|sd", "u|hd", "m|1080"};
  scores::orderMeasured(ids, 1, standing);
  CHECK(ids == std::vector<std::string>{"u|sd", "m|1080", "u|hd"});
}

TEST_CASE("best working picture: the last played copy stays first only while nothing working has a better picture") {
  Book b;
  probed(b, "last|720", 1500, 720, 50);
  probed(b, "w|720", 1200, 720, 50);
  for (int i = 0; i < 4; ++i) b.failed("f|1080", kNow);
  b.picture("f|1080", 1920, 1080, 60, kNow);
  auto standing = standingIn(b, {{"u|fhd", "FHD"}});
  // Others as good, failing, or only claiming better: it stays.
  std::vector<std::string> copies = {"last|720", "w|720", "f|1080", "u|fhd"};
  CHECK(scores::keepsFirst("last|720", copies, standing));
  // A working 1080p measured: it gives way.
  probed(b, "w|1080", 2500, 1080, 30);
  copies.push_back("w|1080");
  CHECK_FALSE(scores::keepsFirst("last|720", copies, standing));
  CHECK(scores::keepsFirst("w|1080", copies, standing));
  // A last played copy with no picture known (and no claim): kept.
  CHECK(scores::keepsFirst("u|plain", copies, standing));
}

TEST_CASE("best working picture: direct copies first only within a tier") {
  namespace ss = marigold::sports::store;
  // "jf:" copies open on their server; "xt:" copies are direct.
  auto server = [](const std::string& id) { return id.rfind("jf:", 0) == 0; };
  Book b;
  probed(b, "jf:1080", 3000, 1080, 50);
  probed(b, "jf:720", 3000, 720, 50);
  probed(b, "xt:720", 1500, 720, 50);
  for (int i = 0; i < 4; ++i) b.failed("xt:bad", kNow);
  b.picture("xt:bad", 1280, 720, 50, kNow);
  auto standing = standingIn(b, {{"xt:hd", "HD"}, {"xt:fhd", "FHD"}});
  ss::Holds holds = [&standing](const std::string& a, const std::string& z) {
    return scores::holdsAhead(standing(a), standing(z));
  };
  // A working server-opened 1080p stays ahead of a direct 720p.
  std::vector<std::string> list = {"jf:1080", "xt:720"};
  ss::directVersionsFirst(list, 0, server, holds);
  CHECK(list == std::vector<std::string>{"jf:1080", "xt:720"});
  // The same tier: the direct one first, as before.
  list = {"jf:720", "xt:720"};
  ss::directVersionsFirst(list, 0, server, holds);
  CHECK(list == std::vector<std::string>{"xt:720", "jf:720"});
  // Past the server-opened 720p, not past the 1080p.
  list = {"jf:1080", "jf:720", "xt:720"};
  ss::directVersionsFirst(list, 0, server, holds);
  CHECK(list == std::vector<std::string>{"jf:1080", "xt:720", "jf:720"});
  // A direct copy that doesn't work doesn't pass a working one.
  list = {"jf:720", "xt:bad"};
  ss::directVersionsFirst(list, 0, server, holds);
  CHECK(list == std::vector<std::string>{"jf:720", "xt:bad"});
  // Unmeasured direct copies pass a better picture only where they claim
  // as much, or claim nothing (today's rule).
  list = {"jf:1080", "xt:hd"};
  ss::directVersionsFirst(list, 0, server, holds);
  CHECK(list == std::vector<std::string>{"jf:1080", "xt:hd"});
  list = {"jf:1080", "xt:fhd"};
  ss::directVersionsFirst(list, 0, server, holds);
  CHECK(list == std::vector<std::string>{"xt:fhd", "jf:1080"});
  list = {"jf:1080", "xt:plain"};
  ss::directVersionsFirst(list, 0, server, holds);
  CHECK(list == std::vector<std::string>{"xt:plain", "jf:1080"});
  // The kept first copy stays first.
  list = {"jf:720", "jf:a", "xt:720"};
  ss::directVersionsFirst(list, 1, server, holds);
  CHECK(list == std::vector<std::string>{"jf:720", "xt:720", "jf:a"});
  // Nothing measured: every direct copy first, as before.
  Book none;
  auto blank = standingIn(none);
  ss::Holds noHolds = [&blank](const std::string& a, const std::string& z) {
    return scores::holdsAhead(blank(a), blank(z));
  };
  list = {"jf:a", "xt:1", "jf:b", "xt:2"};
  ss::directVersionsFirst(list, 0, server, noHolds);
  CHECK(list == std::vector<std::string>{"xt:1", "xt:2", "jf:a", "jf:b"});
  // The last played server-opened copy leads the server-opened ones that
  // don't hold their place, behind the direct ones.
  list = {"xt:720", "jf:1080", "jf:a", "jf:720"};
  ss::leadServerVersions(list, "jf:720", server, holds);
  CHECK(list == std::vector<std::string>{"xt:720", "jf:1080", "jf:720", "jf:a"});
  list = {"xt:720", "jf:a", "jf:1080"};
  ss::leadServerVersions(list, "jf:1080", server, holds);
  CHECK(list == std::vector<std::string>{"xt:720", "jf:1080", "jf:a"});
}

TEST_CASE("feed scores: the providers' roll-up on a small lineup") {
  Book b;
  // Dream: 4 plays (2 at 1080p), 1 failure, 2 stalls in 2 h.
  for (int i = 0; i < 2; ++i) b.started("dream|1", 2000, kNow);
  for (int i = 0; i < 2; ++i) b.started("dream|2", 3000, kNow);
  b.picture("dream|1", 1920, 1080, 60, kNow);
  b.picture("dream|2", 1280, 720, 60, kNow);
  b.failed("dream|2", kNow);
  b.stalled("dream|1", kNow);
  b.stalled("dream|2", kNow);
  b.played("dream|1", 3600 * 1000, kNow);
  b.played("dream|2", 3600 * 1000, kNow);
  // Trex: 2 plays at 1080p, no failure.
  b.started("trex|1", 1800, kNow);
  b.started("trex|2", 2000, kNow);
  b.picture("trex|1", 1920, 1080, 50, kNow);
  b.picture("trex|2", 1920, 1080, 50, kNow);
  // Another account's copy (no longer listed) is left out.
  b.started("gone|1", 100, kNow);

  // Four lineup rows: A (Dream with replay, Trex), B (Trex with replay
  // only), C (Xtream only), D (Dream, Xtream).
  std::vector<scores::Row> rows = {
      {{"dream", true}, {"trex", false}},
      {{"trex", true}},
      {{"xtream", false}, {"xtream", false}},
      {{"dream", false}, {"xtream", false}},
  };
  scores::Rollup r = scores::rollup(b, {"dream", "xtream", "trex"}, rows, kNow);
  REQUIRE(r.providers.size() == 3);
  const scores::ProviderFigures& dream = r.providers[0];
  CHECK(dream.provider == "dream");
  CHECK(dream.plays == doctest::Approx(4));
  CHECK(dream.medianStartMs == 2500u);
  REQUIRE(dream.failureRate);
  CHECK(*dream.failureRate == doctest::Approx(0.2));
  REQUIRE(dream.stallsPerHour);
  CHECK(*dream.stallsPerHour == doctest::Approx(1.0));
  REQUIRE(dream.fullHd);
  CHECK(*dream.fullHd == doctest::Approx(0.5));
  CHECK(dream.lineup == doctest::Approx(0.5));
  CHECK(dream.only == doctest::Approx(0.0));
  CHECK(dream.replay == doctest::Approx(0.25));
  const scores::ProviderFigures& xtream = r.providers[1];
  CHECK(xtream.plays == 0);
  CHECK_FALSE(xtream.medianStartMs);
  CHECK_FALSE(xtream.failureRate);
  CHECK(xtream.lineup == doctest::Approx(0.5));
  CHECK(xtream.only == doctest::Approx(0.25));
  CHECK(xtream.replay == 0);
  const scores::ProviderFigures& trex = r.providers[2];
  CHECK(*trex.failureRate == 0);
  CHECK(*trex.fullHd == doctest::Approx(1.0));
  CHECK_FALSE(trex.stallsPerHour);  // nothing played long enough
  CHECK(trex.lineup == doctest::Approx(0.5));
  CHECK(trex.only == doctest::Approx(0.25));
  CHECK(trex.replay == doctest::Approx(0.25));
  // Pairs: every two providers, the rows either covers.
  REQUIRE(r.pairs.size() == 3);
  CHECK(r.pairs[0].a == "dream");
  CHECK(r.pairs[0].b == "xtream");
  CHECK(r.pairs[0].lineup == doctest::Approx(0.75));
  CHECK(r.pairs[0].replay == doctest::Approx(0.25));
  CHECK(r.pairs[1].b == "trex");
  CHECK(r.pairs[1].lineup == doctest::Approx(0.75));
  CHECK(r.pairs[1].replay == doctest::Approx(0.5));
  CHECK(r.pairs[2].lineup == doctest::Approx(1.0));

  // The log's lines: percentages and plays, the providers' names, no count
  // of channels and no link.
  auto names = [](const std::string& id) {
    return id == "dream" ? std::string("Dream") : id == "trex" ? std::string("Trex") : std::string("Xtream");
  };
  auto lines = scores::rollupLines(r, names);
  REQUIRE(lines.size() == 6);
  CHECK(lines[0].first == "Dream");
  CHECK(lines[0].second ==
        "plays 4.0, picture 2.5 s median, start failures 20%, stalls 1.0/h, 1080p+ 50%; "
        "lineup 50%, only here 0%, replay 25%");
  CHECK(lines[1].first == "Xtream");
  CHECK(lines[1].second == "no plays yet; lineup 50%, only here 25%, replay 0%");
  CHECK(lines[2].second ==
        "plays 2.0, picture 1.9 s median, start failures 0%, 1080p+ 100%; lineup 50%, only here 25%, replay 25%");
  CHECK(lines[4].first == "Dream + Trex");
  CHECK(lines[4].second == "together: lineup 75%, replay 50%");
  for (const auto& [who, what] : lines) {
    CHECK(what.find("://") == std::string::npos);
    CHECK(what.find("channel") == std::string::npos);
  }
  // No lineup: shares are 0, nothing divides by zero.
  scores::Rollup empty = scores::rollup(b, {"dream"}, {}, kNow);
  CHECK(empty.providers[0].lineup == 0);
  CHECK(empty.pairs.empty());
}

TEST_CASE("feed scores: the channel report's cells") {
  Book b;
  CHECK(scores::cellsOf(nullptr, kNow).plays.empty());
  b.started("xt:a|1", 1800, kNow);
  b.started("xt:a|1", 2200, kNow);
  b.failed("xt:a|1", kNow);
  b.stalled("xt:a|1", kNow);
  b.played("xt:a|1", 7200 * 1000, kNow);
  b.picture("xt:a|1", 1920, 1080, 60, kNow);
  scores::Cells c = scores::cellsOf(b.find("xt:a|1"), kNow);
  CHECK(c.startMs == "2000");
  CHECK(c.failures == "1.0");
  CHECK(c.stallsPerHour == "0.5");
  CHECK(c.picture == "1080p \xc2\xb7 60");
  CHECK(c.plays == "2.0");
  // A week on, the weights halve.
  CHECK(scores::cellsOf(b.find("xt:a|1"), kNow + 7 * kDay).plays == "1.0");
}
