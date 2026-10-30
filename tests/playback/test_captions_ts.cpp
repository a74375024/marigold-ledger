// Caption bytes out of MPEG-TS: tables, PES reassembly, SEI, reordering.
#include <doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "core/captions/ts_captions.hpp"

using marigold::captions::CcPair;
using marigold::captions::TsCaptions;

namespace {

using Bytes = std::vector<uint8_t>;

constexpr int kPmtPid = 0x1000;
constexpr int kVideoPid = 0x100;
constexpr int64_t kWrap = int64_t{1} << 33;

void add(Bytes& to, const Bytes& more) { to.insert(to.end(), more.begin(), more.end()); }

uint32_t crc32(const Bytes& b) {
  uint32_t crc = 0xFFFFFFFFu;
  for (uint8_t c : b) {
    crc ^= static_cast<uint32_t>(c) << 24;
    for (int i = 0; i < 8; ++i) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
  }
  return crc;
}

void withCrc(Bytes& section) {
  uint32_t crc = crc32(section);
  for (int s = 24; s >= 0; s -= 8) section.push_back(static_cast<uint8_t>(crc >> s));
}

// Adds 00 00 03 wherever an encoder must.
Bytes escape(const Bytes& rbsp) {
  Bytes out;
  int zeros = 0;
  for (uint8_t c : rbsp) {
    if (zeros >= 2 && c <= 3) {
      out.push_back(3);
      zeros = 0;
    }
    out.push_back(c);
    zeros = c == 0 ? zeros + 1 : 0;
  }
  return out;
}

struct Triplet {
  uint8_t head, b1, b2;
};

// A 608 pair on field 1 (or 2), as an encoder writes it.
Triplet cc(uint8_t b1, uint8_t b2, int field = 0) {
  return Triplet{static_cast<uint8_t>(0xFC | field), b1, b2};
}

Bytes ccData(const std::vector<Triplet>& triplets) {
  Bytes d = {static_cast<uint8_t>(0x40 | triplets.size()), 0xFF};
  for (const Triplet& t : triplets) add(d, {t.head, t.b1, t.b2});
  d.push_back(0xFF);  // marker_bits
  return d;
}

// SEI messages: an optional unrelated one that needs escaping, then GA94.
Bytes seiRbsp(const std::vector<Triplet>& triplets, bool tricky) {
  Bytes rbsp;
  if (tricky) {
    // user_data_unregistered with zero runs (UUID then 00 00 01 00 00 00 02)
    Bytes m = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    add(m, {0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02});
    rbsp.push_back(5);
    rbsp.push_back(static_cast<uint8_t>(m.size()));
    add(rbsp, m);
  }
  Bytes t35 = {0xB5, 0x00, 0x31, 'G', 'A', '9', '4', 0x03};
  add(t35, ccData(triplets));
  rbsp.push_back(4);
  rbsp.push_back(static_cast<uint8_t>(t35.size()));
  add(rbsp, t35);
  rbsp.push_back(0x80);
  return rbsp;
}

// Slice data with no start codes in it.
Bytes sliceData(size_t n) {
  Bytes b(n);
  for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(0x40 + (i * 37) % 180);
  return b;
}

// H.264 access unit: AUD, an optional filler (to move the SEI across packet
// boundaries), SEI, IDR slice.
Bytes h264(const std::vector<Triplet>& triplets, bool tricky = false, size_t filler = 0, size_t slice = 1500) {
  Bytes au = {0, 0, 0, 1, 0x09, 0xF0};
  if (filler) {
    add(au, {0, 0, 1, 0x0C});
    au.insert(au.end(), filler, 0xFF);
    au.push_back(0x80);
  }
  add(au, {0, 0, 1, 0x06});
  add(au, escape(seiRbsp(triplets, tricky)));
  add(au, {0, 0, 1, 0x65, 0x88, 0x84});
  add(au, sliceData(slice));
  return au;
}

// HEVC access unit: AUD, prefix SEI, IDR slice.
Bytes hevc(const std::vector<Triplet>& triplets) {
  Bytes au = {0, 0, 0, 1, 0x46, 0x01, 0x50};
  add(au, {0, 0, 1, 0x4E, 0x01});
  add(au, escape(seiRbsp(triplets, true)));
  add(au, {0, 0, 1, 0x26, 0x01, 0xAF});
  add(au, sliceData(900));
  return au;
}

// MPEG-2 picture: picture header, coding extension, GA94 user data, a slice.
Bytes mpeg2(const std::vector<Triplet>& triplets) {
  Bytes pic = {0, 0, 1, 0x00, 0x00, 0x0F, 0xFF, 0xF8};
  add(pic, {0, 0, 1, 0xB5, 0x8F, 0xFF, 0xF3, 0x41, 0x80});
  add(pic, {0, 0, 1, 0xB2, 'G', 'A', '9', '4', 0x03});
  add(pic, ccData(triplets));
  add(pic, {0, 0, 1, 0x01});
  add(pic, sliceData(700));
  return pic;
}

Bytes pes(int64_t pts90, const Bytes& es) {
  uint64_t p = static_cast<uint64_t>(pts90) & (static_cast<uint64_t>(kWrap) - 1);
  Bytes b = {0, 0, 1, 0xE0, 0, 0, 0x80, 0x80, 0x05,
             static_cast<uint8_t>(0x21 | ((p >> 29) & 0x0E)), static_cast<uint8_t>(p >> 22),
             static_cast<uint8_t>(((p >> 14) & 0xFE) | 1), static_cast<uint8_t>(p >> 7),
             static_cast<uint8_t>(((p << 1) & 0xFE) | 1)};
  add(b, es);
  return b;
}

struct Ts {
  Bytes out;
  std::map<int, int> counters;

  void packets(int pid, const Bytes& payload, bool psi) {
    Bytes data;
    if (psi) data.push_back(0);  // pointer_field
    add(data, payload);
    size_t at = 0;
    bool first = true;
    while (at < data.size() || first) {
      size_t left = data.size() - at;
      uint8_t pkt[188];
      pkt[0] = 0x47;
      pkt[1] = static_cast<uint8_t>((first ? 0x40 : 0) | (pid >> 8));
      pkt[2] = static_cast<uint8_t>(pid);
      int& c = counters[pid];
      size_t off = 4;
      if (left >= 184 || psi) {
        pkt[3] = static_cast<uint8_t>(0x10 | c);
        size_t n = std::min<size_t>(left, 184);
        std::copy(data.begin() + static_cast<std::ptrdiff_t>(at), data.begin() + static_cast<std::ptrdiff_t>(at + n),
                  pkt + off);
        std::fill(pkt + off + n, pkt + 188, 0xFF);
        at += n;
      } else {
        // Adaptation field stuffing fills the last packet.
        pkt[3] = static_cast<uint8_t>(0x30 | c);
        size_t stuff = 183 - left;
        pkt[4] = static_cast<uint8_t>(stuff);
        if (stuff > 0) {
          pkt[5] = 0x00;
          std::fill(pkt + 6, pkt + 5 + stuff, 0xFF);
        }
        off = 5 + stuff;
        std::copy(data.begin() + static_cast<std::ptrdiff_t>(at), data.end(), pkt + off);
        at = data.size();
      }
      c = (c + 1) & 0x0F;
      out.insert(out.end(), pkt, pkt + 188);
      first = false;
    }
  }

  void tables(int streamType = 0x1B) {
    Bytes pat = {0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x00, 0x01,
                 static_cast<uint8_t>(0xE0 | (kPmtPid >> 8)), static_cast<uint8_t>(kPmtPid & 0xFF)};
    withCrc(pat);
    packets(0, pat, true);
    // An audio stream first: the video one must be picked.
    Bytes pmt = {0x02, 0xB0, 0x00, 0x00, 0x01, 0xC1, 0x00, 0x00, static_cast<uint8_t>(0xE0 | (kVideoPid >> 8)),
                 static_cast<uint8_t>(kVideoPid & 0xFF), 0xF0, 0x00};
    add(pmt, {0x0F, 0xE1, 0x01, 0xF0, 0x00});
    add(pmt, {static_cast<uint8_t>(streamType), static_cast<uint8_t>(0xE0 | (kVideoPid >> 8)),
              static_cast<uint8_t>(kVideoPid & 0xFF), 0xF0, 0x00});
    pmt[2] = static_cast<uint8_t>(pmt.size() - 3 + 4);
    withCrc(pmt);
    packets(kPmtPid, pmt, true);
  }

  void video(int64_t pts90, const Bytes& es) { packets(kVideoPid, pes(pts90, es), false); }
};

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
