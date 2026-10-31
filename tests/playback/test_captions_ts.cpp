// Caption bytes out of MPEG-TS: tables, PES reassembly, SEI, reordering.
#include <doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "core/captions/ts_captions.hpp"
#include "playback/ts_stream.hpp"

using marigold::captions::CcPair;
using marigold::captions::TsCaptions;

using namespace tsgen;

namespace {

int64_t ms(int i) { return 90 * (1000 + 33 * static_cast<int64_t>(i)); }

std::vector<CcPair> run(const Bytes& stream, size_t chunk, TsCaptions* keep = nullptr) {
  TsCaptions local;
  TsCaptions& t = keep ? *keep : local;
  std::vector<CcPair> got;
  for (size_t at = 0; at < stream.size(); at += chunk) {
    t.feed(stream.data() + at, std::min(chunk, stream.size() - at));
    for (const CcPair& p : t.take()) got.push_back(p);
  }
  t.flush();
  for (const CcPair& p : t.take()) got.push_back(p);
  return got;
}

// A stream of n access units, each carrying the pair ('A' + i, '0' + i).
Bytes numbered(int n, int streamType = 0x1B, bool tricky = false) {
  Ts ts;
  ts.tables(streamType);
  for (int i = 0; i < n; ++i) {
    std::vector<Triplet> t = {cc(static_cast<uint8_t>('A' + i), static_cast<uint8_t>('0' + i))};
    Bytes es = streamType == 0x24 ? hevc(t) : streamType == 0x02 ? mpeg2(t) : h264(t, tricky);
    ts.video(ms(i), es);
  }
  return ts.out;
}

void checkNumbered(const std::vector<CcPair>& got, int n) {
  REQUIRE(got.size() == static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    CHECK(got[static_cast<size_t>(i)].b1 == 'A' + i);
    CHECK(got[static_cast<size_t>(i)].b2 == '0' + i);
    CHECK(got[static_cast<size_t>(i)].field == 0);
    CHECK(got[static_cast<size_t>(i)].ptsMs == ms(i) / 90);
  }
}

}  // namespace

TEST_CASE("captions ts: H.264 SEI pairs in any chunking") {
  Bytes stream = numbered(8);
  for (size_t chunk : {size_t{1}, size_t{7}, size_t{188}, size_t{1000}, stream.size()}) {
    CAPTURE(chunk);
    TsCaptions t;
    checkNumbered(run(stream, chunk, &t), 8);
    CHECK(t.sawCaptionData());
    CHECK(t.newestPtsMs() == ms(7) / 90);
  }
}

TEST_CASE("captions ts: emulation prevention bytes are removed before parsing") {
  Bytes au = h264({cc('H', 'I')}, true);
  bool escaped = false;
  for (size_t i = 0; i + 2 < au.size(); ++i) escaped = escaped || (au[i] == 0 && au[i + 1] == 0 && au[i + 2] == 3);
  CHECK(escaped);
  checkNumbered(run(numbered(5, 0x1B, true), 7), 5);
}

TEST_CASE("captions ts: start codes split across TS packets") {
  // Fillers of every size near a packet's end put the SEI's start code across it.
  for (size_t filler = 150; filler < 185; ++filler) {
    CAPTURE(filler);
    Ts ts;
    ts.tables();
    ts.video(ms(0), h264({cc(0x94, 0x2C), cc('O', 'K')}, false, filler));
    ts.video(ms(1), h264({cc(0x94, 0x20, 1)}, false, filler + 1));
    std::vector<CcPair> got = run(ts.out, 188);
    REQUIRE(got.size() == 3);
    CHECK(got[0].b1 == 0x94);
    CHECK(got[1].b1 == 'O');
    CHECK(got[1].b2 == 'K');
    CHECK(got[2].field == 1);
  }
}

TEST_CASE("captions ts: B-frames come out in presentation order") {
  Ts ts;
  ts.tables();
  // Decode order 3, 1, 2 (then 6, 4, 5).
  int order[] = {3, 1, 2, 6, 4, 5};
  for (int i : order) ts.video(ms(i), h264({cc(static_cast<uint8_t>('A' + i), '0')}));
  TsCaptions t;
  t.feed(ts.out.data(), ts.out.size());
  // A picture goes out once 4 later ones have arrived: only PTS 1 so far.
  std::vector<CcPair> early = t.take();
  REQUIRE(early.size() == 1);
  CHECK(early[0].b1 == 'A' + 1);
  t.flush();
  std::vector<CcPair> rest = t.take();
  REQUIRE(rest.size() == 5);
  for (int i = 0; i < 5; ++i) CHECK(rest[static_cast<size_t>(i)].b1 == 'A' + 2 + i);
  CHECK(t.newestPtsMs() == ms(6) / 90);
}

TEST_CASE("captions ts: pairs are held until 300 ms behind the newest picture") {
  Ts ts;
  ts.tables();
  ts.video(90 * 1000, h264({cc('A', 'A')}));
  ts.video(90 * 1200, h264({}));
  TsCaptions t;
  t.feed(ts.out.data(), ts.out.size());
  CHECK(t.take().empty());
  Ts more;
  more.counters = ts.counters;
  more.video(90 * 1301, h264({}));
  t.feed(more.out.data(), more.out.size());
  std::vector<CcPair> got = t.take();
  REQUIRE(got.size() == 1);
  CHECK(got[0].ptsMs == 1000);
}

TEST_CASE("captions ts: HEVC prefix SEI") {
  TsCaptions t;
  checkNumbered(run(numbered(6, 0x24), 7, &t), 6);
  CHECK(t.sawCaptionData());
}

TEST_CASE("captions ts: MPEG-2 picture user data") {
  checkNumbered(run(numbered(6, 0x02), 1000), 6);
}

TEST_CASE("captions ts: PTS keeps counting across the 33-bit wrap") {
  Ts ts;
  ts.tables();
  int64_t start = kWrap - 90 * 100;  // 100 ms before the wrap
  for (int i = 0; i < 8; ++i) ts.video(start + 90 * 33 * i, h264({cc('W', static_cast<uint8_t>('0' + i))}));
  TsCaptions t;
  std::vector<CcPair> got = run(ts.out, 188, &t);
  REQUIRE(got.size() == 8);
  for (int i = 0; i < 8; ++i) {
    CHECK(got[static_cast<size_t>(i)].b2 == '0' + i);
    CHECK(got[static_cast<size_t>(i)].ptsMs == (start + 90 * 33 * static_cast<int64_t>(i)) / 90);
  }
  CHECK(t.newestPtsMs() > kWrap / 90);
}

TEST_CASE("captions ts: a PTS far back starts a new timeline") {
  Ts ts;
  ts.tables();
  ts.video(90 * 500000, h264({cc('O', 'L')}));
  ts.video(90 * 1000, h264({cc('N', 'W')}));
  TsCaptions t;
  std::vector<CcPair> got = run(ts.out, 188, &t);
  REQUIRE(got.size() == 2);
  CHECK(got[0].b1 == 'O');
  CHECK(got[1].b1 == 'N');
  CHECK(t.newestPtsMs() == 1000);
}

TEST_CASE("captions ts: garbage before sync and between packets") {
  Bytes garbage(5000);
  uint32_t x = 12345;
  for (uint8_t& b : garbage) {
    x = x * 1103515245u + 12345u;
    b = static_cast<uint8_t>(x >> 16);
    if ((x >> 8) % 40 == 0) b = 0x47;  // more false sync bytes than chance
  }
  Bytes stream = garbage;
  Bytes good = numbered(6);
  add(stream, good);
  checkNumbered(run(stream, 7), 6);
  checkNumbered(run(stream, 1000), 6);

  // Once in sync, a burst of junk between two packets loses nothing.
  for (size_t len : {size_t{1}, size_t{77}, size_t{400}}) {
    CAPTURE(len);
    Bytes cut = good;
    cut.insert(cut.begin() + 188 * 12, garbage.begin(), garbage.begin() + static_cast<std::ptrdiff_t>(len));
    checkNumbered(run(cut, 188), 6);
    checkNumbered(run(cut, 7), 6);
  }

  // Only garbage: nothing, and no trouble.
  TsCaptions t;
  CHECK(run(garbage, 1000, &t).empty());
  CHECK_FALSE(t.sawCaptionData());
  CHECK(t.newestPtsMs() == -1);
}

TEST_CASE("captions ts: padding, invalid and 708 triplets are dropped") {
  Ts ts;
  ts.tables();
  ts.video(ms(0), h264({cc(0x80, 0x80), Triplet{0xF8, 0x94, 0x2C}, Triplet{0xFE, 0x41, 0x42},
                        Triplet{0xFF, 0x43, 0x44}, cc(0x00, 0x00, 1)}));
  TsCaptions t;
  CHECK(run(ts.out, 188, &t).empty());
  CHECK_FALSE(t.sawCaptionData());
  CHECK(t.newestPtsMs() == ms(0) / 90);

  Ts mixed;
  mixed.tables();
  mixed.video(ms(0), h264({cc(0x80, 0x80), cc(0x94, 0x2C), Triplet{0xFA, 0x00, 0x00}, cc(0x20, 0x80, 1)}));
  TsCaptions t2;
  std::vector<CcPair> got = run(mixed.out, 188, &t2);
  REQUIRE(got.size() == 2);
  CHECK(got[0].b1 == 0x94);
  CHECK(got[1].field == 1);
  CHECK(t2.sawCaptionData());
}

TEST_CASE("captions ts: lost packets drop only the damaged picture") {
  Bytes good = numbered(6);
  // Lose the packet that starts the second picture.
  int seen = 0;
  size_t cutAt = 0;
  for (size_t at = 0; at + 188 <= good.size(); at += 188) {
    int pid = ((good[at + 1] & 0x1F) << 8) | good[at + 2];
    if (pid == kVideoPid && (good[at + 1] & 0x40) && ++seen == 2) {
      cutAt = at;
      break;
    }
  }
  REQUIRE(cutAt > 0);
  Bytes lossy = good;
  lossy.erase(lossy.begin() + static_cast<std::ptrdiff_t>(cutAt), lossy.begin() + static_cast<std::ptrdiff_t>(cutAt + 188));
  std::vector<CcPair> got = run(lossy, 188);
  REQUIRE(got.size() == 5);
  CHECK(got[0].b1 == 'A');
  CHECK(got[1].b1 == 'C');
}

TEST_CASE("captions ts: reset starts over") {
  TsCaptions t;
  Bytes stream = numbered(3);
  t.feed(stream.data(), stream.size() - 100);  // stop mid-packet
  t.reset();
  CHECK_FALSE(t.sawCaptionData());
  CHECK(t.newestPtsMs() == -1);
  CHECK(t.take().empty());
  checkNumbered(run(stream, 188, &t), 3);
  // takeInto reuses the caller's buffer.
  std::vector<CcPair> buf = {CcPair{0, 0, 1, 2}};
  t.takeInto(buf);
  CHECK(buf.empty());
}

TEST_CASE("captions ts: HEVC hierarchical B-frames come out in presentation order, by decode time") {
  // Two 16-picture GOPs at 30 fps, 7 B pictures between references, in
  // decode order, with DTS 4 pictures before the first PTS. A fixed window
  // lets the deep ones out too soon (B4 is 400 ms behind P16).
  const int gop[] = {0,  16, 8,  4,  2,  1,  3,  6,  5,  7,  12, 10, 9,  11, 14, 13, 15,
                     32, 24, 20, 18, 17, 19, 22, 21, 23, 28, 26, 25, 27, 30, 29, 31};
  const int64_t frame = 3003;  // 90 kHz
  Ts ts;
  ts.tables(0x24);
  int d = 0;
  for (int p : gop) {
    std::vector<Triplet> t = {cc(static_cast<uint8_t>('A' + p % 26), static_cast<uint8_t>('0' + p / 26))};
    ts.video(90 * 1000 + frame * p, hevc(t, p == 0 ? 19 : 1), 90 * 1000 + frame * (d - 4));
    ++d;
  }
  TsCaptions t;
  std::vector<CcPair> got;
  for (size_t at = 0; at < ts.out.size(); at += 188) {
    t.feed(ts.out.data() + at, std::min<size_t>(188, ts.out.size() - at));
    for (const CcPair& p : t.take()) got.push_back(p);
  }
  // Pictures went out as the stream came, not all at the end.
  CHECK(got.size() >= 20);
  t.flush();
  for (const CcPair& p : t.take()) got.push_back(p);
  REQUIRE(got.size() == 33);
  for (size_t i = 0; i < got.size(); ++i) {
    CAPTURE(i);
    CHECK(got[i].b1 == 'A' + static_cast<int>(i % 26));
    CHECK(got[i].b2 == '0' + static_cast<int>(i / 26));
    CHECK(got[i].mediaMs == (frame * static_cast<int64_t>(i)) / 90);
  }
}

TEST_CASE("captions ts: a decode time lets a picture go once every earlier one is in") {
  // I0 P3 B1: P3 waits for the DTS to reach it; B1 (PTS = DTS) goes at once.
  Ts ts;
  ts.tables();
  int64_t f = 90 * 33;
  ts.video(90 * 1000, h264({cc('I', '0')}), 90 * 1000 - f);
  ts.video(90 * 1000 + 3 * f, h264({cc('P', '3')}), 90 * 1000);
  ts.video(90 * 1000 + f, h264({cc('B', '1')}), 90 * 1000 + f);
  TsCaptions t;
  t.feed(ts.out.data(), ts.out.size());
  std::vector<CcPair> got = t.take();
  REQUIRE(got.size() == 2);
  CHECK(got[0].b1 == 'I');
  CHECK(got[1].b1 == 'B');
  Ts more;
  more.counters = ts.counters;
  more.video(90 * 1000 + 2 * f, h264({cc('B', '2')}), 90 * 1000 + 2 * f);
  more.video(90 * 1000 + 6 * f, h264({cc('P', '6')}), 90 * 1000 + 3 * f);
  t.feed(more.out.data(), more.out.size());
  got = t.take();
  REQUIRE(got.size() == 2);
  CHECK(got[0].b2 == '2');
  CHECK(got[1].b2 == '3');
}

TEST_CASE("captions ts: media time carries on across PTS jumps back and ahead") {
  Ts ts;
  ts.tables();
  int64_t pts = 1000000;
  for (int i = 0; i < 90; ++i) {
    if (i == 30) pts -= 60000;  // the source reopened: back a minute
    if (i == 60) pts += 30000;  // and ahead half a minute
    ts.video(90 * pts, h264({cc(static_cast<uint8_t>('A' + i % 26), static_cast<uint8_t>('0' + i / 26))}));
    pts += 33;
  }
  TsCaptions t;
  std::vector<CcPair> got = run(ts.out, 188, &t);
  REQUIRE(got.size() == 90);
  for (size_t i = 0; i < got.size(); ++i) {
    CAPTURE(i);
    CHECK(got[i].b1 == 'A' + static_cast<int>(i % 26));
    CHECK(got[i].b2 == '0' + static_cast<int>(i / 26));
    CHECK(got[i].mediaMs == 33 * static_cast<int64_t>(i));  // one picture apart, jumps or not
  }
  CHECK(got[30].ptsMs == got[29].ptsMs + 33 - 60000);
  CHECK(t.jumps() == 2);
  CHECK(t.firstPtsMs() == 1000000);
  CHECK(t.newestMediaMs() == 33 * 89);
}

TEST_CASE("captions ts: a TS discontinuity takes a smaller PTS jump as a new timeline") {
  Ts ts;
  ts.tables();
  ts.video(90 * 100000, h264({cc('A', 'A')}));
  ts.video(90 * 100033, h264({cc('B', 'B')}));
  // The next picture's first packet says discontinuity; its PTS is 3 s back.
  Ts jump;
  jump.counters = ts.counters;
  jump.video(90 * 97000, h264({cc('C', 'C')}));
  // Room for the adaptation field: the payload moves on 2 bytes (the PES has
  // no length, and the 2 bytes lost are slice data).
  Bytes& o = jump.out;
  o[3] = static_cast<uint8_t>((o[3] & 0x0F) | 0x30);
  std::copy_backward(o.begin() + 4, o.begin() + 186, o.begin() + 188);
  o[4] = 1;
  o[5] = 0x80;
  add(ts.out, o);
  TsCaptions t;
  std::vector<CcPair> got = run(ts.out, 188, &t);
  REQUIRE(got.size() == 3);
  CHECK(got[2].b1 == 'C');
  CHECK(got[2].mediaMs == 66);
  CHECK(t.jumps() == 1);
}

TEST_CASE("captions ts: the first picture a decoder can start on") {
  // H.264: P, P, then an I slice that isn't an IDR (open GOP), then P.
  const Bytes p = {0x41, 0x9A, 0x00};  // slice_type 5 (P)
  const Bytes i = {0x41, 0x88, 0x84};  // slice_type 7 (I)
  for (size_t filler = 120; filler < 150; ++filler) {
    CAPTURE(filler);
    Ts ts;
    ts.tables();
    ts.video(90 * 5000, h264({cc('A', 'A')}, false, 0, 1500, p));
    ts.video(90 * 5033, h264({cc('B', 'B')}, false, filler, 1500, p));
    ts.video(90 * 5066, h264({cc('C', 'C')}, false, filler, 1500, i));  // its slice header may span two packets
    ts.video(90 * 5099, h264({cc('D', 'D')}, false, 0, 1500, p));
    TsCaptions t;
    CHECK(run(ts.out, 188, &t).size() == 4);
    REQUIRE(t.haveKey());
    CHECK(t.keyMediaMs() == 66);
  }
  // HEVC: a trailing picture, then a CRA.
  Ts ts;
  ts.tables(0x24);
  ts.video(90 * 5000, hevc({cc('A', 'A')}, 1));
  ts.video(90 * 5033, hevc({cc('B', 'B')}, 21));
  TsCaptions t;
  CHECK(run(ts.out, 188, &t).size() == 2);
  REQUIRE(t.haveKey());
  CHECK(t.keyMediaMs() == 33);
  // MPEG-2: an I picture.
  TsCaptions m;
  CHECK(run(numbered(3, 0x02), 188, &m).size() == 3);
  REQUIRE(m.haveKey());
  CHECK(m.keyMediaMs() == 0);
}
