// The picture check's stream reader on synthetic MPEG-TS: the scenes of the
// black-screen analysis rebuilt from their measured picture sizes.
#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/media/ts_video_probe.hpp"
#include "playback/ts_builder.hpp"

using marigold::media::Device;
using marigold::media::TsVideoProbe;
using marigold::media::Verdict;
using marigold::media::VideoFacts;
using tsgen::Bytes;
using tsgen::Codec;
using tsgen::Scene;

namespace {

struct Result {
  Verdict verdict = Verdict::Unknown;
  std::string detail, timeline, facts;
  // Every verdict the stream went through, in order (repeats dropped).
  std::vector<Verdict> seen;
};

Result read(const Bytes& ts, size_t chunk = 1316, Device d = Device::LgWebOs) {
  TsVideoProbe p(d);
  Result r;
  for (size_t off = 0; off < ts.size(); off += chunk) {
    p.feed(ts.data() + off, std::min(chunk, ts.size() - off));
    Verdict v = p.verdict();
    if (r.seen.empty() || r.seen.back() != v) r.seen.push_back(v);
  }
  r.verdict = p.verdict();
  r.detail = p.detail();
  r.timeline = p.timeline();
  r.facts = marigold::media::describe(p.facts());
  return r;
}

// The longest run of windows that aren't a picture.
size_t longestGap(const std::string& timeline) {
  size_t best = 0, run = 0;
  for (char c : timeline) {
    run = c == '.' ? 0 : run + 1;
    best = std::max(best, run);
  }
  return best;
}

bool all(const std::string& timeline, char c) {
  return !timeline.empty() && std::all_of(timeline.begin(), timeline.end(), [c](char x) { return x == c; });
}

// The analysis streams (1080p, 25 fps, 2 s GOP unless said).
Scene normal() { return Scene{}; }

Scene darkNoisy() {
  Scene s;
  s.size = [](int, bool intra) { return intra ? 136000u : 18000u; };
  return s;
}

Scene blackVbr() {
  Scene s;
  s.size = [](int, bool intra) { return intra ? 432u : 72u; };
  return s;
}

Scene blackCbr() {
  Scene s = blackVbr();
  s.cbrKbps = 4000;  // filler NAL units and null packets up to the rate
  return s;
}

Scene slate() {
  Scene s;
  s.size = [](int, bool intra) { return intra ? 140000u : 318u; };
  return s;
}

Scene slateText() {
  Scene s;
  s.size = [](int, bool intra) { return intra ? 15400u : 135u; };
  return s;
}

// A normal scene that fades through black for a few seconds (8 to 11).
Scene fade() {
  Scene s;
  s.seconds = 16;
  s.size = [](int second, bool intra) -> size_t {
    if (second == 9) return intra ? 47000u : 300u;  // the fade down: nothing moves
    if (second == 10 || second == 11) return intra ? 432u : 72u;
    return intra ? 47000u : 20000u;
  };
  return s;
}

}  // namespace

TEST_CASE("picture probe: a normal scene is a picture, with its facts") {
  Result r = read(tsgen::build(normal()));
  CHECK(r.verdict == Verdict::Picture);
  CHECK(all(r.timeline, '.'));
  CHECK(r.facts == "1920x1080 25p \xc2\xb7 h264 High 8-bit \xc2\xb7 aac");
  CHECK(r.detail == r.facts);
}

TEST_CASE("picture probe: a dark, noisy scene is still a picture") {
  Result r = read(tsgen::build(darkNoisy()));
  CHECK(r.verdict == Verdict::Picture);
  CHECK(all(r.timeline, '.'));
}

TEST_CASE("picture probe: black, variable rate") {
  Result r = read(tsgen::build(blackVbr()));
  CHECK(r.verdict == Verdict::Black);
  CHECK(all(r.timeline, 'B'));
  CHECK(r.detail.find("I 4") == 0);
  CHECK(r.detail.find("kbps") != std::string::npos);
}

TEST_CASE("picture probe: black padded to a constant rate is black too") {
  Bytes ts = tsgen::build(blackCbr());
  CHECK(ts.size() > tsgen::build(blackVbr()).size() * 8);  // mostly padding
  Result r = read(ts);
  CHECK(r.verdict == Verdict::Black);
  CHECK(all(r.timeline, 'B'));
}

TEST_CASE("picture probe: a slate (ordinary I frames, nothing moves) is static") {
  for (Scene s : {slate(), slateText()}) {
    Result r = read(tsgen::build(s));
    CHECK(r.verdict == Verdict::Static);
    // Its first window can still count as a picture (the I frame came first).
    CHECK(r.timeline.find('B') == std::string::npos);
    CHECK(r.timeline.substr(2).find_first_not_of('s') == std::string::npos);
    CHECK(r.detail.find("P ") == 0);
  }
}

TEST_CASE("picture probe: a fade through black lasts no more than its 3 s") {
  Result r = read(tsgen::build(fade()));
  CHECK(r.verdict == Verdict::Picture);
  CHECK(longestGap(r.timeline) <= 3);
  CHECK(r.timeline.find('B') != std::string::npos);
}

TEST_CASE("picture probe: sound without video, tables only, scrambled") {
  Scene audioOnly;
  audioOnly.video = false;
  Result a = read(tsgen::build(audioOnly));
  CHECK(a.verdict == Verdict::NoVideo);
  CHECK(a.detail == "PMT audio 1");

  Scene tables;
  tables.tablesOnly = true;
  Result t = read(tsgen::build(tables));
  CHECK(t.verdict == Verdict::TablesOnly);
  CHECK(t.detail.find("null") != std::string::npos);

  Scene scrambled;
  scrambled.scrambled = true;
  scrambled.ca = true;
  Result s = read(tsgen::build(scrambled));
  CHECK(s.verdict == Verdict::Scrambled);
  CHECK(s.detail == "100% of packets scrambled, CA descriptor");
  CHECK(s.timeline.empty());
}

TEST_CASE("picture probe: a PMT that lists video that never comes is no video") {
  Scene s;
  s.size = [](int, bool) { return 0u; };
  Bytes ts = tsgen::build(s);
  // Drop every video packet: the sound and tables stay.
  Bytes kept;
  for (size_t i = 0; i + 188 <= ts.size(); i += 188) {
    int pid = ((ts[i + 1] & 0x1F) << 8) | ts[i + 2];
    if (pid == tsgen::kVideoPid) continue;
    kept.insert(kept.end(), ts.begin() + static_cast<long>(i), ts.begin() + static_cast<long>(i + 188));
  }
  Result r = read(kept);
  CHECK(r.verdict == Verdict::NoVideo);
  CHECK(r.detail == "PMT video 1, audio 1; no video packets");
}

TEST_CASE("picture probe: HEVC Main10 plays on the TV; H.264 4:2:2 10-bit doesn't") {
  Scene hevc;
  hevc.format.codec = Codec::Hevc;
  hevc.format.profile = 2;
  hevc.format.bitDepth = 10;
  hevc.seconds = 6;
  Result h = read(tsgen::build(hevc));
  CHECK(h.verdict == Verdict::Picture);
  CHECK(h.facts == "1920x1080 25p \xc2\xb7 hevc Main10 10-bit \xc2\xb7 aac");

  Scene hi422;
  hi422.format.profile = 122;
  hi422.format.chroma = 2;
  hi422.format.bitDepth = 10;
  hi422.seconds = 4;
  Bytes ts = tsgen::build(hi422);
  Result u = read(ts);
  CHECK(u.verdict == Verdict::Unsupported);
  CHECK(u.detail == "h264 High 4:2:2 10-bit");
  // Said at the first sequence header, before any window closed.
  REQUIRE(u.seen.size() >= 1);
  CHECK(u.seen.front() == Verdict::Unsupported);
  // Another device's player decides for itself.
  CHECK(read(ts, 1316, Device::Any).verdict == Verdict::Picture);
}

TEST_CASE("picture probe: MPEG-2 1080i and its black (static: MPEG-2 has no tiny I frame)") {
  Scene m;
  m.format.codec = Codec::Mpeg2;
  m.format.interlaced = true;
  m.fps = 30000.0 / 1001.0;
  m.seconds = 6;
  m.audioType = 0x03;
  m.size = [](int, bool intra) { return intra ? 72000u : 30000u; };
  Result r = read(tsgen::build(m));
  CHECK(r.verdict == Verdict::Picture);
  CHECK(r.facts == "1920x1080 29.97i \xc2\xb7 mpeg2 Main 8-bit \xc2\xb7 mp2");

  m.size = [](int, bool intra) { return intra ? 31000u : 816u; };
  Result b = read(tsgen::build(m));
  CHECK(b.verdict == Verdict::Static);
}

TEST_CASE("picture probe: 59.94 frames a second, HEVC black") {
  Scene s;
  s.fps = 60000.0 / 1001.0;
  s.seconds = 4;
  s.size = [](int, bool intra) { return intra ? 47000u : 9000u; };
  CHECK(read(tsgen::build(s)).facts == "1920x1080 59.94p \xc2\xb7 h264 High 8-bit \xc2\xb7 aac");

  Scene h;
  h.format.codec = Codec::Hevc;
  h.format.profile = 1;
  h.seconds = 5;
  h.size = [](int, bool intra) { return intra ? 606u : 115u; };
  CHECK(read(tsgen::build(h)).verdict == Verdict::Black);
}

TEST_CASE("picture probe: any chunking, 7 B to 64 KB, reads the same") {
  for (Scene s : {fade(), blackCbr(), slateText()}) {
    s.seconds = std::min(s.seconds, 12);
    Bytes ts = tsgen::build(s);
    Result base = read(ts, 1316);
    for (size_t chunk : {size_t{7}, size_t{188}, size_t{189}, size_t{4096}, size_t{65536}}) {
      Result r = read(ts, chunk);
      CHECK(r.verdict == base.verdict);
      CHECK(r.timeline == base.timeline);
      CHECK(r.detail == base.detail);
      CHECK(r.facts == base.facts);
    }
  }
}

TEST_CASE("picture probe: garbage before the stream, and a new stream after reset") {
  Bytes ts = tsgen::build(blackVbr());
  Bytes junk(1000, 0x47);
  junk.insert(junk.end(), ts.begin(), ts.end());
  CHECK(read(junk).verdict == Verdict::Black);

  TsVideoProbe p(Device::LgWebOs);
  p.feed(ts.data(), ts.size());
  CHECK(p.verdict() == Verdict::Black);
  p.reset();
  CHECK(p.verdict() == Verdict::Unknown);
  CHECK(p.timeline().empty());
  Bytes n = tsgen::build(normal());
  p.feed(n.data(), n.size());
  CHECK(p.verdict() == Verdict::Picture);
  CHECK(p.sawPicture());
}

TEST_CASE("picture probe: the shared check reads only its own relay session") {
  marigold::media::StreamCheck c(Device::LgWebOs);
  Bytes ts = tsgen::build(blackVbr());
  CHECK_FALSE(c.snapshot().active);
  c.begin(7);
  c.bytes(ts.data(), ts.size(), 6);  // an old session: ignored
  CHECK(c.snapshot().verdict == Verdict::Unknown);
  c.bytes(ts.data(), ts.size(), 7);
  auto s = c.snapshot();
  CHECK(s.active);
  CHECK(s.verdict == Verdict::Black);
  CHECK(s.facts.codec == marigold::media::ts::Codec::H264);
  c.begin(0);
  CHECK_FALSE(c.snapshot().active);
}

TEST_CASE("picture probe: what this TV can't decode") {
  VideoFacts f;
  f.codec = marigold::media::ts::Codec::H264;
  f.header = true;
  f.profile = 100;
  CHECK(marigold::media::cannotDecode(Device::LgWebOs, f).empty());
  f.profile = 110;
  f.bitDepth = 10;
  CHECK(marigold::media::cannotDecode(Device::LgWebOs, f) == "h264 High 10 10-bit");
  CHECK(marigold::media::cannotDecode(Device::Any, f).empty());
  f.codec = marigold::media::ts::Codec::Hevc;
  f.profile = 2;
  CHECK(marigold::media::cannotDecode(Device::LgWebOs, f).empty());
  f.chroma = 2;
  CHECK_FALSE(marigold::media::cannotDecode(Device::LgWebOs, f).empty());
  f.codec = marigold::media::ts::Codec::Mpeg2;
  f.profile = 0x44;
  f.bitDepth = 8;
  CHECK(marigold::media::cannotDecode(Device::LgWebOs, f) == "mpeg2 Main 4:2:2 8-bit");
}
