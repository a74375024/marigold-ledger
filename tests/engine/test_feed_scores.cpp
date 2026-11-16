// Build 166: each copy's record of its real plays (and, from 1.0.11, its
// probes): what is counted, how older results fade, the saved form and its
// size, how a channel's copies are ordered by it, and the providers'
// roll-up for the playback log.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/engine/feed_scores.hpp"
#include "core/util/json.hpp"

namespace scores = marigold::engine::scores;
using scores::Book;

namespace {

constexpr marigold::time::Seconds kNow = 1'800'000'000;
constexpr marigold::time::Seconds kDay = 86400;

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
  b.recordProbe("xt:a|1", ok, kNow);
  b.recordProbe("xt:a|1", ok, kNow);
  // Three probes: one and a half starts, not enough to order by alone.
  CHECK_FALSE(b.score("xt:a|1", kNow));
  b.recordProbe("xt:a|1", ok, kNow);
  CHECK(b.score("xt:a|1", kNow));
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
  auto score = [&b](const std::string& id) { return b.score(id, kNow); };

  std::vector<std::string> ids = {"p|1", "p|2", "p|3"};
  scores::orderMeasured(ids, 0, score);
  // p|2 has no history: it keeps its place; the measured two swap.
  CHECK(ids == std::vector<std::string>{"p|3", "p|2", "p|1"});

  // The pinned (or last played) copy stays first.
  ids = {"p|1", "p|2", "p|3"};
  scores::orderMeasured(ids, 1, score);
  CHECK(ids == std::vector<std::string>{"p|1", "p|2", "p|3"});

  // No history at all: today's order.
  Book none;
  ids = {"p|1", "p|2", "p|3"};
  scores::orderMeasured(ids, 0, [&none](const std::string& id) { return none.score(id, kNow); });
  CHECK(ids == std::vector<std::string>{"p|1", "p|2", "p|3"});

  // Near-equal copies keep the catalog's order (a little faster isn't
  // clearly better).
  Book close;
  for (int i = 0; i < 4; ++i) close.started("q|1", 2000, kNow);
  for (int i = 0; i < 4; ++i) close.started("q|2", 1700, kNow);
  ids = {"q|1", "q|2"};
  scores::orderMeasured(ids, 0, [&close](const std::string& id) { return close.score(id, kNow); });
  CHECK(ids == std::vector<std::string>{"q|1", "q|2"});
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
