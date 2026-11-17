// Build 172: replay lands on the minute picked. The archive's clock read
// from its packets, what two reads of it say (exact, chunked, programme,
// rewritten), byte ranges, the replay's plan for each, where the timeline
// can rest, and what is kept.
#include <doctest.h>

#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "core/engine/archive_timing.hpp"
#include "core/engine/replay.hpp"
#include "core/engine/store_impl.hpp"
#include "core/media/ts_start.hpp"
#include "engine/fake_host.hpp"
#include "playback/ts_builder.hpp"

using namespace marigold;
namespace archive = marigold::engine::archive;
namespace replay = marigold::engine::replay;
namespace tsc = marigold::media::tsclock;
using archive::Kind;
using archive::Timing;

namespace {

constexpr int64_t kWrap = int64_t{1} << 33;

// `seconds` of 25 fps video from `pts0` (PAT and PMT every ten pictures, a
// key picture every two seconds), `frame` bytes a picture.
std::string clip(int64_t pts0, int seconds, size_t frame = 3000) {
  tsgen::Muxer m;
  tsgen::VideoFormat f;
  for (int i = 0; i < seconds * 25; ++i) {
    if (i % 10 == 0) {
      m.section(0, tsgen::pat());
      m.section(tsgen::kPmtPid, tsgen::pmt(0x1B, 0));
    }
    int64_t t = (pts0 + static_cast<int64_t>(i) * 3600) % kWrap;
    m.pes(tsgen::kVideoPid, tsgen::videoPes(t, t, tsgen::picture(f, i % 50 == 0, frame, 0, static_cast<uint8_t>(i))));
  }
  return std::string(m.out.begin(), m.out.end());
}

// Broadcast PTS at wall time `t` (the channel's encoder clock).
int64_t broadcast(time::Seconds t) { return ((t - 1700000000) * 90000 % kWrap + kWrap) % kWrap; }

archive::Readings readings(time::Seconds now, std::function<time::Seconds(time::Seconds)> startOf, int clock = 0) {
  archive::Readings r;
  r.clock = clock;
  r.live = archive::Read{now, broadcast(now - 4)};  // live a few seconds behind
  time::Seconds first = archive::firstMoment(now);
  for (time::Seconds at : {first, first + archive::kApart}) r.archive.push_back({at, broadcast(startOf(at))});
  return r;
}

time::Seconds chunkOf(time::Seconds t, int minutes, int clock = 0) {
  time::Seconds c = minutes * 60;
  return (t + clock) / c * c - clock;
}

}  // namespace

TEST_CASE("archive timing: the video clock from the packets, wraps handled") {
  CHECK(tsc::delta(10, kWrap - 10) == 20);
  CHECK(tsc::delta(kWrap - 10, 10) == -20);
  CHECK(tsc::ahead(5, kWrap - 5) == 10);
  CHECK(tsc::ahead(kWrap - 5, 5) == kWrap - 10);
  uint8_t pes[] = {0, 0, 1, 0xE0, 0, 0, 0x80, 0x80, 5, 0, 0, 0, 0, 0};
  tsgen::Bytes t = tsgen::ts90(0x20, 123456789);
  std::copy(t.begin(), t.end(), pes + 9);
  bool video = false;
  REQUIRE(tsc::pesPts(pes, sizeof pes, &video));
  CHECK(*tsc::pesPts(pes, sizeof pes, &video) == 123456789);
  CHECK(video);
  pes[7] = 0;  // no PTS
  CHECK_FALSE(tsc::pesPts(pes, sizeof pes));

  // Six seconds across the wrap, in odd pieces: the first PTS, the span
  // and the bytes a second.
  int64_t start = kWrap - 2 * 90000;
  std::string s = clip(start, 6);
  tsc::TsClock c;
  for (size_t at = 0; at < s.size(); at += 1000)
    c.feed(reinterpret_cast<const uint8_t*>(s.data()) + at, std::min<size_t>(1000, s.size() - at));
  REQUIRE(c.first());
  CHECK(*c.first() == start);
  CHECK(c.seconds() == doctest::Approx(5.96).epsilon(0.01));
  // 25 pictures of ~3.1 KB with their packets: about 80 KB a second.
  CHECK(c.byteRate() > 70000);
  CHECK(c.byteRate() < 100000);
  // A stream joined mid-packet finds its step.
  tsc::TsClock late;
  late.feed(reinterpret_cast<const uint8_t*>(s.data()) + 77, s.size() - 77);
  CHECK(late.first());
}

TEST_CASE("archive timing: a stream cut to start at its moment, tables first") {
  int64_t start = 900000;
  std::string s = clip(start, 30);
  tsc::TsStartAt cut(12, 0);
  std::string out;
  for (size_t at = 0; at < s.size(); at += 4096)
    cut.feed(reinterpret_cast<const uint8_t*>(s.data()) + at, std::min<size_t>(4096, s.size() - at), out);
  REQUIRE(cut.passing());
  CHECK(cut.skipped() == 12);
  REQUIRE(out.size() > 3 * 188);
  // PAT, then the PMT, then the picture at the moment.
  CHECK((((static_cast<uint8_t>(out[1]) & 0x1F) << 8) | static_cast<uint8_t>(out[2])) == 0);
  CHECK((((static_cast<uint8_t>(out[189]) & 0x1F) << 8) | static_cast<uint8_t>(out[190])) == tsgen::kPmtPid);
  tsc::TsClock c;
  c.feed(reinterpret_cast<const uint8_t*>(out.data()), out.size());
  REQUIRE(c.first());
  CHECK(tsc::delta(*c.first(), start) == 12 * 90000);
  CHECK(cut.dropped() < s.size());

  // A target known from elsewhere (the range read): counted from it.
  tsc::TsStartAt ranged(12, 0);
  ranged.target(start + 12 * 90000);
  std::string part = s.substr(s.size() / 4 / 188 * 188), out2;
  ranged.feed(reinterpret_cast<const uint8_t*>(part.data()), part.size(), out2);
  REQUIRE(ranged.passing());
  CHECK(ranged.skipped() == 12);

  // Capped: passed on from where the cap came, and said where.
  tsc::TsStartAt capped(25, 200 * 1024);
  std::string out3;
  capped.feed(reinterpret_cast<const uint8_t*>(s.data()), s.size(), out3);
  REQUIRE(capped.passing());
  CHECK(capped.skipped() >= 0);
  CHECK(capped.skipped() < 25);
}

TEST_CASE("archive timing: exact, with the recording's offset, and chunked with its size") {
  time::Seconds now = 1800000000;
  // Exact: every stretch starts at the minute asked.
  Timing e = archive::classify(readings(now, [](time::Seconds t) { return t; }));
  CHECK(e.kind == Kind::Exact);
  CHECK(e.offset == 0);
  // A minute behind: asked for 20:21, it shows 20:20.
  Timing behind = archive::classify(readings(now, [](time::Seconds t) { return t - 60; }));
  CHECK(behind.kind == Kind::Exact);
  CHECK(behind.offset == 60);
  CHECK(archive::describe(behind, true) == "archive: recording 1 min behind");
  // The live stream's own few seconds are no offset.
  CHECK(archive::classify(readings(now, [](time::Seconds t) { return t - 20; })).offset == 0);

  // 30- and 60-minute chunks: the size every start fits.
  for (int size : {15, 20, 30, 60}) {
    Timing c = archive::classify(readings(now, [size](time::Seconds t) { return chunkOf(t, size); }));
    CHECK(c.kind == Kind::Chunked);
    CHECK(c.chunkMinutes == size);
  }
  // In an archive clock an hour east: its boundaries.
  Timing east = archive::classify(readings(now, [](time::Seconds t) { return chunkOf(t, 30, 3600); }, 3600), nullptr);
  CHECK(east.kind == Kind::Chunked);
  CHECK(east.chunkMinutes == 30);

  // Ten-minute chunks look like five from :23 and :34: both said, and the
  // third moment tells them apart.
  std::vector<int> sizes;
  archive::Readings ten = readings(now, [](time::Seconds t) { return chunkOf(t, 10); });
  CHECK(archive::classify(ten, &sizes).chunkMinutes == 5);
  REQUIRE(sizes == std::vector<int>{5, 10});
  time::Seconds third = archive::decidingMoment(chunkOf(ten.archive[0].asked, 10), sizes, 0);
  CHECK(chunkOf(third, 5) != chunkOf(third, 10));
  ten.archive.push_back({third, broadcast(chunkOf(third, 10))});
  Timing decided = archive::classify(ten);
  CHECK(decided.kind == Kind::Chunked);
  CHECK(decided.chunkMinutes == 10);

  // The programme's start: shows at :13 and :43, both moments in the one
  // from :13.
  time::Seconds f = archive::firstMoment(now);
  std::vector<time::Seconds> shows = {f - 70 * 60, f - 10 * 60, f + 20 * 60};
  archive::Readings prog = readings(now, [&shows](time::Seconds t) {
    time::Seconds best = 0;
    for (time::Seconds s : shows)
      if (s <= t) best = s;
    return best;
  });
  prog.programmeStarts = shows;
  Timing p = archive::classify(prog);
  CHECK(p.kind == Kind::Programme);
}

TEST_CASE("archive timing: timestamps that can't be the broadcast's are not measured") {
  time::Seconds now = 1800000000;
  // Every stretch starting near zero.
  archive::Readings zero = readings(now, [](time::Seconds t) { return t; });
  for (archive::Read& a : zero.archive) a.pts = 90000 * 2;
  Timing z = archive::classify(zero);
  CHECK(z.kind == Kind::Rewritten);
  CHECK(archive::describe(z, true) == "archive: timestamps rewritten, not measured");
  // Starts far from the moments asked (another clock entirely).
  archive::Readings off = readings(now, [](time::Seconds t) { return t; });
  off.archive[0].pts = broadcast(now - 9 * 3600);
  off.archive[1].pts = broadcast(now - 1800);
  CHECK(archive::classify(off).kind == Kind::Rewritten);
  // No live: the gap between the two alone; eleven minutes is exact.
  archive::Readings alone = readings(now, [](time::Seconds t) { return t; });
  alone.live.reset();
  CHECK(archive::classify(alone).kind == Kind::Exact);
  archive::Readings chunkAlone = readings(now, [](time::Seconds t) { return chunkOf(t, 60); });
  chunkAlone.live.reset();
  CHECK(archive::classify(chunkAlone).kind == Kind::Unmeasured);
  // One read only.
  archive::Readings one = readings(now, [](time::Seconds t) { return t; });
  one.archive.pop_back();
  CHECK(archive::classify(one).kind == Kind::Unmeasured);
  // Rewritten and unmeasured plan as Build 170.
  for (Kind k : {Kind::Rewritten, Kind::Unmeasured}) {
    Timing t;
    t.kind = k;
    archive::Plan pl = archive::plan(t, now - 3600, 0, true, false, {});
    CHECK(pl.ask == now - 3600);
    CHECK(pl.shows == now - 3600);
    CHECK(pl.skip == 0);
  }
}

TEST_CASE("archive timing: byte ranges from the answer's headers") {
  CHECK(archive::answersRanges(206, "bytes 0-999/5000", "", 1000));
  CHECK(archive::answersRanges(206, "Bytes 0-999/*", "", 0));
  CHECK(archive::answersRanges(200, "", "bytes", 5000));
  CHECK_FALSE(archive::answersRanges(200, "", "bytes", 0));  // no length: a live-like stream
  CHECK_FALSE(archive::answersRanges(200, "", "none", 5000));
  CHECK_FALSE(archive::answersRanges(200, "", "", 5000));
  CHECK_FALSE(archive::answersRanges(206, "", "", 5000));
  CHECK_FALSE(archive::answersRanges(416, "bytes */5000", "bytes", 0));
}

TEST_CASE("archive timing: the replay's plan for each kind of archive") {
  time::Seconds hour = 1800000000 / 3600 * 3600;  // 20:00 say
  time::Seconds picked = hour + 21 * 60;           // 20:21
  // Never measured: as Build 170.
  archive::Plan none = archive::plan(std::nullopt, picked, 0, true, false, {});
  CHECK(none.ask == picked);
  CHECK(none.shows == picked);
  // Exact, a minute behind: asked a minute later, shows the minute picked.
  Timing exact;
  exact.kind = Kind::Exact;
  exact.offset = 60;
  archive::Plan e = archive::plan(exact, picked, 0, true, false, {});
  CHECK(e.ask == picked + 60);
  CHECK(e.shows == picked);
  CHECK(archive::planText(e, exact, nullptr) == "(recording 1 min behind)");

  auto hm = [hour](time::Seconds t) { return "20:" + std::string(t - hour < 600 ? "0" : "") + std::to_string((t - hour) / 60); };
  // Chunked and seekable: the chunk asked, the player started 21 min in by
  // a byte range near there.
  Timing seek;
  seek.kind = Kind::Chunked;
  seek.chunkMinutes = 30;
  seek.seekable = 1;
  seek.byteRate = 750000;
  archive::Plan s = archive::plan(seek, picked, 0, true, false, {});
  CHECK(s.how == archive::Plan::How::Seek);
  CHECK(s.ask == hour);
  CHECK(s.base == hour);
  CHECK(s.skip == 21 * 60);
  CHECK(s.shows == picked);
  CHECK(s.startByte == uint64_t{21 * 60} * 750000 / 188 * 188);
  CHECK(archive::planText(s, seek, hm) == "(chunk 20:00, skipped 21 min)");
  CHECK(archive::describe(seek, true) == "archive: 30-min chunks, skips to the minute");
  // Without the relay (an https link): from the chunk, said so.
  archive::Plan noRelay = archive::plan(seek, picked, 0, false, false, {});
  CHECK(noRelay.how == archive::Plan::How::FromChunk);
  CHECK(noRelay.shows == hour);

  // Chunked, no ranges: read through up to 4 minutes, else from the chunk.
  Timing chunk;
  chunk.kind = Kind::Chunked;
  chunk.chunkMinutes = 60;
  chunk.seekable = 0;
  archive::Plan near = archive::plan(chunk, hour + 4 * 60, 0, true, false, {});
  CHECK(near.how == archive::Plan::How::ReadThrough);
  CHECK(near.skip == 240);
  CHECK(near.shows == hour + 4 * 60);
  archive::Plan far = archive::plan(chunk, hour + 5 * 60, 0, true, false, {});
  CHECK(far.how == archive::Plan::How::FromChunk);
  CHECK(far.skip == 0);
  CHECK(far.ask == hour);
  CHECK(far.shows == hour);  // what plays: the replay says 20:00
  CHECK(archive::planText(far, chunk, hm) == "(chunk 20:00, no skip)");
  CHECK(archive::describe(chunk, false) == "archive: 60-min chunks, no skip");
  CHECK(archive::describe(chunk, true) == "archive: 60-min chunks, reads through up to 4 min");
  // A stretch following one that ended: on from the next boundary.
  archive::Plan next = archive::plan(chunk, hour + 37 * 60, 0, false, true, {});
  CHECK(next.ask == hour + 3600);
  CHECK(next.shows == hour + 3600);
  // On a boundary: nothing to skip.
  archive::Plan on = archive::plan(chunk, hour, 0, false, false, {});
  CHECK(on.shows == hour);
  CHECK(on.skip == 0);
  // In an archive clock an hour east, a recording a minute behind.
  Timing east = chunk;
  east.chunkMinutes = 30;
  east.offset = 60;
  archive::Plan pe = archive::plan(east, hour + 10 * 60, 3600, false, false, {});
  CHECK(pe.ask == hour);
  CHECK(pe.base == hour - 60);
  CHECK(pe.shows == hour - 60);

  // Programme-aligned: its start from the listings.
  Timing prog;
  prog.kind = Kind::Programme;
  archive::Plan pp = archive::plan(prog, hour + 50 * 60, 0, true, false, {hour - 3600, hour + 47 * 60, hour + 3600});
  CHECK(pp.ask == hour + 47 * 60);
  CHECK(pp.how == archive::Plan::How::ReadThrough);
  CHECK(pp.skip == 180);
  CHECK(archive::planText(pp, prog, hm) == "(programme start 20:47, skipped 3 min)");
}

TEST_CASE("archive timing: where the timeline rests on a chunked archive") {
  Timing chunk;
  chunk.kind = Kind::Chunked;
  chunk.chunkMinutes = 30;
  chunk.seekable = 0;
  // Any minute where it seeks, or where nothing is measured.
  Timing seek = chunk;
  seek.seekable = 1;
  seek.byteRate = 500000;
  CHECK(archive::reach(seek, 0, true).anyMinute());
  CHECK(archive::reach(std::nullopt, 0, true).anyMinute());
  CHECK_FALSE(archive::reach(seek, 0, false).anyMinute());
  replay::Reach r = archive::reach(chunk, 0, false);
  CHECK(r.chunk == 1800);
  CHECK(r.readThrough == 0);
  replay::Reach rt = archive::reach(chunk, 0, true);
  CHECK(rt.readThrough == 240);
  // An archive clock half an hour off and a minute behind: boundaries move.
  replay::Reach odd = archive::reach(chunk, 1800 + 900, false);
  CHECK(replay::boundaryOf(odd, 900 * 10) % 1800 == (1800 - 900) % 1800);

  time::Seconds h = 1800000000 / 3600 * 3600;  // 20:00
  time::Seconds earliest = h - 86400, live = h + 3 * 3600 + 7 * 60;
  // Steps: boundary to boundary; held, two, then four.
  CHECK(replay::stepReach(r, h + 21 * 60, -1, 0, earliest, live) == h);
  CHECK(replay::stepReach(r, h, -1, 0, earliest, live) == h - 1800);
  CHECK(replay::stepReach(r, h + 21 * 60, 1, 0, earliest, live) == h + 1800);
  CHECK(replay::stepReach(r, h, -1, 6, earliest, live) == h - 3600);
  CHECK(replay::stepReach(r, h, -1, 16, earliest, live) == h - 7200);
  CHECK(replay::stepReach(r, live, -1, 0, earliest, live) == h + 3 * 3600);
  CHECK(replay::stepReach(r, h + 3 * 3600, 1, 0, earliest, live) == live);
  // Not before the archive's edge.
  CHECK(replay::stepReach(r, earliest + 1800, -1, 16, earliest, live) == earliest);
  CHECK(replay::stepReach(r, earliest, -1, 0, earliest, live) == earliest);
  // Exact channels: minute steps as before.
  CHECK(replay::stepReach(replay::Reach{}, h + 21 * 60, -1, 0, earliest, live) == h + 20 * 60);

  // Reachable starts: boundaries, plus the read-through after each.
  CHECK(replay::reachable(r, h));
  CHECK_FALSE(replay::reachable(r, h + 60));
  CHECK(replay::reachable(rt, h + 4 * 60));
  CHECK_FALSE(replay::reachable(rt, h + 5 * 60));
  // A pointer's click snaps to the nearest.
  CHECK(replay::snap(r, h + 10 * 60, earliest, live) == h);
  CHECK(replay::snap(r, h + 20 * 60, earliest, live) == h + 1800);
  CHECK(replay::snap(rt, h + 3 * 60 + 20, earliest, live) == h + 3 * 60);
  CHECK(replay::snap(rt, h + 12 * 60, earliest, live) == h + 4 * 60);
  CHECK(replay::snap(rt, h + 25 * 60, earliest, live) == h + 1800);
  CHECK(replay::snap(r, live - 30, earliest, live) == live);
  CHECK(replay::snap(replay::Reach{}, h + 12 * 60 + 40, earliest, live) == h + 12 * 60);

  // Start over: the programme's start where reachable, else just before.
  CHECK(replay::startOverTarget(r, h + 1800, earliest, live) == h + 1800);
  CHECK(replay::startOverTarget(r, h + 35 * 60, earliest, live) == h + 1800);
  CHECK(replay::startOverTarget(rt, h + 33 * 60, earliest, live) == h + 33 * 60);  // within the read-through
  CHECK(replay::startOverTarget(rt, h + 40 * 60, earliest, live) == h + 34 * 60);
  CHECK(replay::startOverTarget(replay::Reach{}, h + 40 * 60 + 5, earliest, live) == h + 40 * 60);

  // Up / Down: the programme starts, made reachable.
  std::vector<replay::Programme> list = {{h - 3600, h + 10 * 60, "A", true},
                                         {h + 10 * 60, h + 70 * 60, "B", true},
                                         {h + 70 * 60, h + 3 * 3600, "C", true}};
  CHECK(replay::jumpReach(r, list, h + 50 * 60, -1, earliest, live) == h);
  CHECK(replay::jumpReach(r, list, h + 50 * 60, 1, earliest, live) == h + 90 * 60);

  // The bar's ticks.
  std::vector<time::Seconds> ticks = replay::boundaries(r, replay::Span{h - 10 * 60, h + 95 * 60});
  CHECK(ticks == std::vector<time::Seconds>{h, h + 1800, h + 3600, h + 5400});
  CHECK(replay::boundaries(replay::Reach{}, replay::Span{h, h + 3600}).empty());
}

TEST_CASE("archive timing: kept with the archive clocks, ids and numbers only, bounded") {
  Timing t;
  t.kind = Kind::Chunked;
  t.chunkMinutes = 30;
  t.offset = -60;
  t.seekable = 1;
  t.byteRate = 625000;
  t.at = 1800000000;
  CHECK(archive::encode(t) == "c30,-60,1,625000,1800000000");
  std::optional<Timing> back = archive::decode(archive::encode(t));
  REQUIRE(back);
  CHECK(back->kind == Kind::Chunked);
  CHECK(back->chunkMinutes == 30);
  CHECK(back->offset == -60);
  CHECK(back->seekable == 1);
  CHECK(back->byteRate == 625000);
  CHECK(back->at == 1800000000);
  for (const char* junk : {"", "c7,0,0,0,0", "x,0,0,0,0", "e,30,0,0,0", "e,0,5,0,0", "e", "c30,http://x"})
    CHECK_FALSE(archive::decode(junk));

  engine::ArchiveClocks clocks;
  CHECK(clocks.note("acc1", "501", 3600));
  CHECK(clocks.noteTiming("acc1", "501", t));
  Timing e;
  e.kind = Kind::Exact;
  CHECK(clocks.noteTiming("acc1", "502", e));
  CHECK_FALSE(clocks.noteTiming("acc1", "503", Timing{}));  // nothing told
  std::string json = clocks.toJson();
  CHECK(json.find("http") == std::string::npos);
  engine::ArchiveClocks read = engine::ArchiveClocks::fromJson(json);
  REQUIRE(read.timing("acc1", "501"));
  CHECK(read.timing("acc1", "501")->chunkMinutes == 30);
  CHECK(read.timing("acc1", "502")->kind == Kind::Exact);
  CHECK_FALSE(read.timing("acc1", "503"));
  CHECK(read.stream("acc1", "501") == 3600);
  // Bounded per account.
  engine::ArchiveClocks many;
  for (size_t i = 0; i < engine::ArchiveClocks::kStreamsKept + 10; ++i) many.noteTiming("a", std::to_string(i), e);
  CHECK(many.accounts["a"].timings.size() == engine::ArchiveClocks::kStreamsKept);
  // Junk in the file is left out.
  engine::ArchiveClocks junk = engine::ArchiveClocks::fromJson(
      R"({"v":1,"accounts":{"acc":{"streams":{},"timing":{"1":"c30,0,1,5,6","2":"zz","../x":"e,0,0,0,0"}}}})");
  CHECK(junk.timing("acc", "1"));
  CHECK_FALSE(junk.timing("acc", "2"));
  CHECK(junk.accounts["acc"].timings.size() == 1);
}

TEST_CASE("archive timing: measured over the network, a range asked on the second read") {
  fake::Tv tv;
  time::Seconds now = time::now();
  archive::Request req;
  req.live = "http://m.example.invalid/live/u/p/9.ts";
  req.headers = {"User-Agent: Test"};
  req.linkFor = [](time::Seconds at) { return "http://m.example.invalid/timeshift/x/" + std::to_string(at) + "/9.ts"; };
  time::Seconds first = archive::firstMoment(now), second = first + archive::kApart;
  // 30-minute chunks, the second answering a range.
  tv.pool.answer(req.live, clip(broadcast(now - 3), 4));
  tv.pool.answer(*req.linkFor(first), clip(broadcast(chunkOf(first, 30)), 5));
  tv.pool.answer(*req.linkFor(second), clip(broadcast(chunkOf(second, 30)), 5), 206);
  tv.pool.ranges(*req.linkFor(second), "bytes 0-99999/100000", "bytes", 100000);
  uint32_t clock = 0;
  archive::Clock ticking = [&clock] { return clock += 5; };
  archive::Outcome o = archive::measure(tv.pool, req, ticking, nullptr, 20000, now);
  REQUIRE(o.timing);
  CHECK(o.timing->kind == Kind::Chunked);
  CHECK(o.timing->chunkMinutes == 30);
  CHECK(o.timing->seekable == 1);
  CHECK(o.timing->byteRate > 70000);
  CHECK_FALSE(o.refused);
  // The range on the second read only.
  auto has = [](const std::vector<std::string>& h, const std::string& line) {
    return std::find(h.begin(), h.end(), line) != h.end();
  };
  CHECK(has(tv.pool.sent[*req.linkFor(second)], "Range: bytes=0-"));
  CHECK_FALSE(has(tv.pool.sent[*req.linkFor(first)], "Range: bytes=0-"));
  CHECK(has(tv.pool.sent[req.live], "User-Agent: Test"));

  // A refusal stops it, said as a refusal; never a link in why.
  fake::Tv refusing;
  refusing.pool.answer(req.live, clip(broadcast(now - 3), 4));
  refusing.pool.answer(*req.linkFor(first), "", 429);
  archive::Outcome r = archive::measure(refusing.pool, req, ticking, nullptr, 20000, now);
  CHECK(r.refused);
  CHECK_FALSE(r.timing);
  CHECK(r.why == "http 429");
  // Nothing answers: nothing told.
  fake::Tv off;
  archive::Outcome n = archive::measure(off.pool, req, ticking, nullptr, 20000, now);
  CHECK_FALSE(n.timing);
  CHECK_FALSE(n.refused);
  CHECK(n.why.find("://") == std::string::npos);

  // The words for a provider: shares, never counts.
  Timing ex;
  ex.kind = Kind::Exact;
  Timing ch;
  ch.kind = Kind::Chunked;
  ch.chunkMinutes = 30;
  ch.seekable = 1;
  ch.byteRate = 1;
  CHECK(archive::providerText({ex, ex}) == "exact on all replay channels");
  CHECK(archive::providerText({ex, ex, ex, ch}) == "exact 75%, 30-min chunks (skips to the minute) 25%");
  CHECK(archive::providerText({}).empty());
}

// ------------------------------------------- Build 173: the hour from the video

TEST_CASE("archive clock 1.0.16: a whole number of quarter hours is the clock, a smaller lag the recording's") {
  // The lag split.
  CHECK(archive::splitLag(3600).clock == 3600);
  CHECK(archive::splitLag(3600).offset == 0);
  CHECK(archive::splitLag(3600 + 59).clock == 3600);  // within a minute
  CHECK(archive::splitLag(3600 + 59).offset == 60);
  CHECK(archive::splitLag(3600 - 70).offset == -60);
  CHECK(archive::splitLag(-3600).clock == -3600);
  CHECK(archive::splitLag(900).clock == 900);
  CHECK(archive::splitLag(-2700).clock == -2700);
  CHECK(archive::splitLag(14 * 3600).clock == 14 * 3600);
  CHECK(archive::splitLag(14 * 3600 + 900).clock == 0);  // beyond any clock
  CHECK_FALSE(archive::splitLag(14 * 3600 + 900).usable);
  for (int lag : {0, 30, 60, 120, 20 * 60, 3600 + 150, 7 * 60})
    CHECK(archive::splitLag(lag).clock == 0);
  CHECK(archive::splitLag(20 * 60).offset == 20 * 60);
  CHECK(archive::splitLag(30).offset == 0);

  time::Seconds now = 1800000000;
  // The user's case: asked in UTC, the archive on UTC+1 starts an hour early.
  Timing hour = archive::classify(readings(now, [](time::Seconds t) { return t - 3600; }));
  CHECK(hour.kind == Kind::Exact);
  CHECK(hour.offset == 0);
  REQUIRE(hour.clock);
  CHECK(*hour.clock == 3600);
  // Asked in UTC−5 (the server's): the archive's clock, not a correction.
  Timing east = archive::classify(readings(now, [](time::Seconds t) { return t - 3600; }, -5 * 3600));
  CHECK(east.clock == -4 * 3600);
  // A quarter hour, with the recording a minute behind as well.
  Timing quarter = archive::classify(readings(now, [](time::Seconds t) { return t - 900 - 60; }));
  CHECK(quarter.kind == Kind::Exact);
  CHECK(quarter.clock == 900);
  CHECK(quarter.offset == 60);
  // Fourteen hours: still a clock.
  CHECK(archive::classify(readings(now, [](time::Seconds t) { return t - 14 * 3600; })).clock == 14 * 3600);
  // The archive later than asked (1.0.15: "timestamps rewritten").
  Timing later = archive::classify(readings(now, [](time::Seconds t) { return t + 3600; }));
  CHECK(later.kind == Kind::Exact);
  CHECK(later.clock == -3600);
  Timing later2 = archive::classify(readings(now, [](time::Seconds t) { return t + 3600 + 1800; }));
  CHECK(later2.clock == -3600 - 1800);
  // Smaller lags stay the recording's, in the clock asked.
  Timing small = archive::classify(readings(now, [](time::Seconds t) { return t - 120; }));
  CHECK(small.kind == Kind::Exact);
  CHECK(small.offset == 120);
  CHECK(small.clock == 0);
  Timing twenty = archive::classify(readings(now, [](time::Seconds t) { return t - 20 * 60; }));
  CHECK(twenty.offset == 20 * 60);
  CHECK(twenty.clock == 0);
  // Two hours and seven minutes: neither.
  Timing odd = archive::classify(readings(now, [](time::Seconds t) { return t - 2 * 3600 - 7 * 60; }));
  CHECK(odd.kind == Kind::Unmeasured);
  CHECK_FALSE(odd.clock);
  // Past fourteen hours: nothing the broadcast's clock explains.
  CHECK_FALSE(archive::classify(readings(now, [](time::Seconds t) { return t - 15 * 3600; })).clock);

  // Chunks on another clock: 30-minute files on UTC+1, asked in UTC.
  Timing chunks = archive::classify(readings(now, [](time::Seconds t) { return chunkOf(t, 30, 3600) - 3600; }));
  CHECK(chunks.kind == Kind::Chunked);
  CHECK(chunks.chunkMinutes == 30);
  CHECK(chunks.clock == 3600);
  // Chunks on the clock asked: that clock.
  CHECK(archive::classify(readings(now, [](time::Seconds t) { return chunkOf(t, 30); })).clock == 0);
  // Planned in the measured clock: asked for its chunk, on its boundaries.
  archive::Plan p = archive::plan(chunks, now - 3 * 3600 + 40 * 60, 3600, false, false, {});
  CHECK(p.ask == now - 3 * 3600 + 30 * 60);
}

TEST_CASE("archive clock 1.0.16: a first replay's two reads, the moment read, and 1.0.15's kept offsets") {
  time::Seconds now = 1800000000;
  // Read 7 minutes past a quarter hour, near the moment picked, ten
  // minutes back at least.
  for (time::Seconds picked : {now - 2 * 3600, now - 2 * 3600 + 3 * 60, now - 3600 + 14 * 60, now - 40 * 60 + 37}) {
    time::Seconds at = archive::quickMoment(picked, now);
    CHECK((at / 60) % 15 == 7);
    CHECK(at % 60 == 0);
    CHECK(std::abs(at - picked) <= 8 * 60);
  }
  CHECK(archive::quickMoment(now - 2 * 3600, now) == now - 2 * 3600 + 7 * 60);
  time::Seconds close = archive::quickMoment(now - 5 * 60, now);
  CHECK(close <= now - 10 * 60);
  CHECK((close / 60) % 15 == 7);

  auto quick = [now](std::function<time::Seconds(time::Seconds)> startOf, int clock = 0,
                     std::vector<time::Seconds> programmes = {}) {
    archive::Readings r;
    r.clock = clock;
    r.live = archive::Read{now, broadcast(now - 4)};
    time::Seconds at = archive::quickMoment(now - 2 * 3600, now);
    r.archive.push_back({at, broadcast(startOf(at))});
    r.programmeStarts = std::move(programmes);
    return archive::classifyQuick(r);
  };
  std::optional<Timing> hour = quick([](time::Seconds t) { return t - 3600; });
  REQUIRE(hour);
  CHECK(hour->kind == Kind::Exact);
  CHECK(hour->clock == 3600);
  CHECK(quick([](time::Seconds t) { return t + 3600; })->clock == -3600);
  CHECK(quick([](time::Seconds t) { return t; })->clock == 0);
  CHECK(quick([](time::Seconds t) { return t - 60; })->offset == 60);
  CHECK(quick([](time::Seconds t) { return t - 3600; }, 3600)->clock == 7200);
  // A chunk's or a programme's start: the full reads tell.
  for (int size : {5, 10, 15, 20, 30, 60, 120})
    CHECK_FALSE(quick([size](time::Seconds t) { return chunkOf(t, size); }));
  CHECK_FALSE(quick([](time::Seconds t) { return chunkOf(t, 60) - 3600; }));
  time::Seconds show = archive::quickMoment(now - 2 * 3600, now) - 3600;
  CHECK_FALSE(quick([show](time::Seconds) { return show; }, 0, {show}));
  CHECK_FALSE(quick([](time::Seconds t) { return t - 2 * 3600 - 7 * 60; }));
  // Nothing without live.
  archive::Readings alone;
  alone.archive.push_back({now - 7200, broadcast(now - 7200)});
  CHECK_FALSE(archive::classifyQuick(alone));

  // 1.0.15 kept a clock error as the recording's offset: not read back.
  CHECK_FALSE(archive::decode("e,3600,-1,0,1800000000"));
  CHECK_FALSE(archive::decode("e,-3600,-1,0,1800000000"));
  CHECK_FALSE(archive::decode("c30,900,1,5,6"));
  CHECK(archive::decode("e,60,-1,0,1800000000"));
  CHECK(archive::decode("e,1200,-1,0,1800000000"));
}

TEST_CASE("archive clock 1.0.16: tables' and measured clocks kept apart; measured wins; 1.0.15's file read as tables") {
  // A 1.0.15 file: every clock from a table, a timing with an hour's offset.
  engine::ArchiveClocks old = engine::ArchiveClocks::fromJson(
      R"({"v":1,"accounts":{"acc":{"last":0,"streams":{"501":0,"502":0,"509":7200},)"
      R"("timing":{"501":"e,3600,-1,0,1800000000","502":"e,0,-1,0,1800000000"}}}})");
  CHECK(old.stream("acc", "501") == 0);
  CHECK_FALSE(old.measuredStream("acc", "501"));
  CHECK_FALSE(old.timing("acc", "501"));  // measured again
  CHECK(old.timing("acc", "502"));
  CHECK_FALSE(old.measuredFor("acc", 0, 0, true));  // nothing measured yet

  // A measurement on 501: UTC+1 where its table said UTC.
  CHECK(old.noteMeasured("acc", "501", 3600));
  CHECK_FALSE(old.noteMeasured("acc", "501", 3600));  // nothing changed
  CHECK(old.measuredStream("acc", "501") == 3600);
  CHECK(old.stream("acc", "501") == 0);  // the table's, still kept as the table's
  // Another copy whose table says UTC too: UTC+1.
  CHECK(old.measuredFor("acc", 0, 0, false) == 3600);
  // One whose own table says something else keeps it; one without a table
  // takes what most measured copies showed.
  CHECK_FALSE(old.measuredFor("acc", 7200, 0, false));
  CHECK(old.measuredFor("acc", 0, std::nullopt, true) == 3600);
  // The account's copies disagree: the most common measured value.
  old.noteMeasured("acc", "502", 3600);
  old.noteMeasured("acc", "503", 0);
  CHECK(old.measuredFor("acc", 0, 0, false) == 3600);
  CHECK(old.measuredFor("acc", 99, 0, true) == 3600);

  // Saved apart, ids and numbers only, and read back.
  std::string json = old.toJson();
  CHECK(json.find("\"measured\":{\"501\":3600,\"502\":3600,\"503\":0}") != std::string::npos);
  CHECK(json.find("\"streams\":{\"501\":0,\"502\":0,\"509\":7200}") != std::string::npos);
  CHECK(json.find("http") == std::string::npos);
  engine::ArchiveClocks back = engine::ArchiveClocks::fromJson(json);
  CHECK(back.measuredStream("acc", "501") == 3600);
  CHECK(back.stream("acc", "501") == 0);
  CHECK(back.measuredFor("acc", 0, 0, false) == 3600);
  // Junk measured entries left out.
  engine::ArchiveClocks junk = engine::ArchiveClocks::fromJson(
      R"({"v":2,"accounts":{"acc":{"measured":{"1":3600,"2":"x","../3":0,"4":99999999}}}})");
  CHECK(junk.measuredStream("acc", "1") == 3600);
  CHECK(junk.accounts["acc"].measured.size() == 1);

  // Exact everywhere measured: a copy not measured yet is taken as exact.
  engine::ArchiveClocks ex;
  Timing e;
  e.kind = Kind::Exact;
  CHECK_FALSE(ex.allExact("a"));
  ex.noteTiming("a", "1", e);
  CHECK(ex.allExact("a"));
  Timing c;
  c.kind = Kind::Chunked;
  c.chunkMinutes = 30;
  ex.noteTiming("a", "2", c);
  CHECK_FALSE(ex.allExact("a"));
}

TEST_CASE("archive clock 1.0.16: over the network for a replay: two reads when they tell, the full ones when not") {
  fake::Tv tv;
  time::Seconds now = time::now();
  archive::Request req;
  req.live = "http://m.example.invalid/live/u/p/9.ts";
  req.linkFor = [](time::Seconds at) { return "http://m.example.invalid/timeshift/x/" + std::to_string(at) + "/9.ts"; };
  time::Seconds picked = now - 2 * 3600 - (now % 3600);
  req.picked = picked;
  req.quickMs = 4000;
  time::Seconds at = archive::quickMoment(picked, now);
  time::Seconds first = archive::firstMoment(now), second = first + archive::kApart;
  tv.pool.answer(req.live, clip(broadcast(now - 3), 4));
  tv.pool.answer(*req.linkFor(at), clip(broadcast(at - 3600), 5));
  uint32_t ms = 0;
  archive::Clock ticking = [&ms] { return ms += 5; };
  archive::Outcome o = archive::measure(tv.pool, req, ticking, nullptr, 4000, now);
  REQUIRE(o.timing);
  CHECK(o.timing->kind == Kind::Exact);
  CHECK(o.timing->clock == 3600);
  CHECK(tv.pool.asked[*req.linkFor(first)] == 0);  // nothing more read
  CHECK(tv.pool.asked[*req.linkFor(second)] == 0);

  // 30-minute chunks: the read near the moment can't tell; the full reads do.
  fake::Tv chunked;
  chunked.pool.answer(req.live, clip(broadcast(now - 3), 4));
  chunked.pool.answer(*req.linkFor(at), clip(broadcast(chunkOf(at, 30)), 5));
  chunked.pool.answer(*req.linkFor(first), clip(broadcast(chunkOf(first, 30)), 5));
  chunked.pool.answer(*req.linkFor(second), clip(broadcast(chunkOf(second, 30)), 5));
  archive::Outcome c = archive::measure(chunked.pool, req, ticking, nullptr, 20000, now);
  REQUIRE(c.timing);
  CHECK(c.timing->kind == Kind::Chunked);
  CHECK(c.timing->chunkMinutes == 30);
  CHECK(c.timing->clock == 0);

  // Out of time before the full reads tell: nothing (measured again), not
  // "not measured".
  fake::Tv slow;
  slow.pool.answer(req.live, clip(broadcast(now - 3), 4));
  slow.pool.answer(*req.linkFor(at), clip(broadcast(chunkOf(at, 30)), 5));
  uint32_t slowMs = 0;
  archive::Clock crawling = [&slowMs] { return slowMs += 400; };
  archive::Outcome s = archive::measure(slow.pool, req, crawling, nullptr, 4000, now);
  CHECK_FALSE(s.timing);
  CHECK(s.why.find("://") == std::string::npos);
}
