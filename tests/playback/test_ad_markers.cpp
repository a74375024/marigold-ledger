// Build 177: SCTE-35 ad markers read from MPEG-TS as it passes: declared in
// the PMT (stream type 0x86, a "CUEI" registration), cues (splice_insert,
// time_signal, splice_null) on the streams it lists or on any PID when it
// lists none, sections across packets and in any chunking, broken ones
// skipped, and the words for the log (never a link).
#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/media/ts_ad_markers.hpp"
#include "core/media/ts_video_probe.hpp"
#include "playback/ts_builder.hpp"

namespace ad = marigold::media::scte35;
using tsgen::Bytes;

namespace {

// A second of 25 fps video from `pts0` (tables first), with `extra` sent
// after the tables.
void video(tsgen::Muxer& m, const Bytes& pmt, int64_t pts0, int frames = 25) {
  tsgen::VideoFormat f;
  for (int i = 0; i < frames; ++i) {
    if (i % 10 == 0) {
      m.section(0, tsgen::pat());
      m.section(tsgen::kPmtPid, pmt);
    }
    int64_t t = pts0 + static_cast<int64_t>(i) * 3600;
    m.pes(tsgen::kVideoPid, tsgen::videoPes(t, t, tsgen::picture(f, i == 0, 2000, 0, static_cast<uint8_t>(i))));
  }
}

ad::Tally scan(const Bytes& b, size_t chunk = 0) {
  ad::Scanner s;
  if (!chunk) chunk = b.size();
  for (size_t at = 0; at < b.size(); at += chunk) s.feed(b.data() + at, std::min(chunk, b.size() - at));
  return s.tally();
}

Bytes declaredPmt() { return tsgen::pmt(0x1B, 0x0F, false, ad::kStreamType); }

void noLink(const std::string& s) {
  CHECK(s.find("://") == std::string::npos);
  CHECK(s.find("example") == std::string::npos);
}

}  // namespace

TEST_CASE("ad markers: a PMT listing stream type 0x86 declares them; no cue in a few seconds says so") {
  tsgen::Muxer m;
  video(m, declaredPmt(), 900000, 75);
  ad::Tally t = scan(m.out);
  CHECK(t.read());
  CHECK(t.tables);
  CHECK(t.declared);
  CHECK(t.cues == 0);
  CHECK(ad::found(t) == ad::Found::Declared);
  CHECK(ad::readText(t) == "declared, no cue in the first seconds");
  // The video's clock: three seconds, less a picture.
  CHECK(t.videoMs >= 2900);
  CHECK(t.videoMs <= 3000);
  CHECK(ad::replayText(t) == "ad markers declared, none seen in 3 s");
}

TEST_CASE("ad markers: a CUEI registration declares them, on the program or on a stream") {
  // On the program, no stream for them: declared, cues looked for on any PID.
  {
    tsgen::Muxer m;
    Bytes entries = tsgen::esEntry(0x1B, tsgen::kVideoPid);
    video(m, tsgen::pmtOf(entries, tsgen::cuei()), 900000);
    m.section(0x1FF0, tsgen::timeSignal(900000));
    ad::Tally t = scan(m.out);
    CHECK(t.declared);
    CHECK(t.cues == 1);
    CHECK(t.timeSignal == 1);
  }
  // On a private stream (type 0x06): declared, and its PID is where cues are read.
  {
    tsgen::Muxer m;
    Bytes entries = tsgen::esEntry(0x1B, tsgen::kVideoPid);
    tsgen::add(entries, tsgen::esEntry(0x06, tsgen::kCuePid, tsgen::cuei()));
    video(m, tsgen::pmtOf(entries), 900000);
    m.section(tsgen::kCuePid, tsgen::spliceInsert(7, true, 60));
    m.section(0x1FF0, tsgen::spliceInsert(8, true, 60));  // not a stream it lists: not read
    ad::Tally t = scan(m.out);
    CHECK(t.declared);
    CHECK(t.cues == 1);
    CHECK(t.spliceInsert == 1);
  }
  // Another registration (AC-3) declares nothing.
  {
    tsgen::Muxer m;
    Bytes entries = tsgen::esEntry(0x1B, tsgen::kVideoPid);
    tsgen::add(entries, tsgen::esEntry(0x06, tsgen::kAudioPid, {0x05, 0x04, 'A', 'C', '-', '3'}));
    video(m, tsgen::pmtOf(entries), 900000);
    ad::Tally t = scan(m.out);
    CHECK(t.tables);
    CHECK_FALSE(t.declared);
  }
}

TEST_CASE("ad markers: splice_insert out of the network with its break's duration") {
  tsgen::Muxer m;
  video(m, declaredPmt(), 900000);
  m.section(tsgen::kCuePid, tsgen::spliceInsert(42, true, 120));
  m.section(tsgen::kCuePid, tsgen::spliceInsert(42, true, 120, false, 900000 + 90000));  // the same, at a time
  m.section(tsgen::kCuePid, tsgen::spliceInsert(43, false));                             // back in
  ad::Tally t = scan(m.out);
  CHECK(t.cues == 3);
  CHECK(t.spliceInsert == 3);
  CHECK(t.outOfNetwork == 2);
  CHECK(t.longestBreak90 == 120 * 90000);
  CHECK(t.skipped == 0);
  CHECK(ad::found(t) == ad::Found::Cues);
  CHECK(ad::readText(t) == "3 cues (splice_insert, out of network, break 120 s)");
  noLink(ad::readText(t));
}

TEST_CASE("ad markers: time_signal and splice_null, each counted by its command") {
  tsgen::Muxer m;
  video(m, declaredPmt(), 900000);
  m.section(tsgen::kCuePid, tsgen::timeSignal(1000000));
  m.section(tsgen::kCuePid, tsgen::spliceNull());
  m.section(tsgen::kCuePid, tsgen::spliceNull());
  ad::Tally t = scan(m.out);
  CHECK(t.cues == 3);
  CHECK(t.timeSignal == 1);
  CHECK(t.spliceNull == 2);
  CHECK(t.spliceInsert == 0);
  CHECK(t.outOfNetwork == 0);
  CHECK(ad::commandsText(t) == "1 time_signal, 2 splice_null");
  CHECK(ad::readText(t) == "3 cues (1 time_signal, 2 splice_null)");
}

TEST_CASE("ad markers: a section across packets, and any chunking, gives the same") {
  tsgen::Muxer m;
  video(m, declaredPmt(), 900000);
  // 300 bytes of descriptors: the section takes two packets.
  Bytes descriptors;
  for (int i = 0; i < 6; ++i) {
    Bytes d = {0x00, 48, 'C', 'U', 'E', 'I'};
    d.resize(50, 0xAB);
    tsgen::add(descriptors, d);
  }
  Bytes cmd = {0, 0, 0, 9, 0x7F, 0xFF, 0xFE, 0x00, 0x29, 0x32, 0xE0, 0x00, 0x01, 0x00, 0x00};  // out, 30 s
  Bytes section = tsgen::spliceSection(0x05, cmd, descriptors);
  REQUIRE(section.size() > 184);
  // Its packets interleaved with the video's.
  tsgen::Muxer cue;
  cue.section(tsgen::kCuePid, section);
  REQUIRE(cue.out.size() == 2 * 188);
  m.out.insert(m.out.end(), cue.out.begin(), cue.out.begin() + 188);
  video(m, declaredPmt(), 900000 + 90000, 3);
  m.out.insert(m.out.end(), cue.out.begin() + 188, cue.out.end());
  for (size_t chunk : {size_t{0}, size_t{1}, size_t{7}, size_t{188}, size_t{1000}}) {
    CAPTURE(chunk);
    ad::Tally t = scan(m.out, chunk);
    CHECK(t.cues == 1);
    CHECK(t.spliceInsert == 1);
    CHECK(t.outOfNetwork == 1);
    CHECK(t.longestBreak90 == 30 * 90000);
  }
  // Bytes out of step before it: found again on the next sync.
  Bytes junk = {0x12, 0x34, 0x00};
  junk.insert(junk.end(), m.out.begin(), m.out.end());
  CHECK(scan(junk, 100).cues == 1);
}

TEST_CASE("ad markers: broken sections are skipped, the good ones after them counted") {
  tsgen::Muxer m;
  video(m, declaredPmt(), 900000);
  // A CRC that doesn't match: never a cue.
  Bytes badCrc = tsgen::spliceInsert(1, true, 30);
  badCrc[20] ^= 0x55;
  m.section(tsgen::kCuePid, badCrc);
  // Intact CRC, a command longer than the section.
  m.section(tsgen::kCuePid, tsgen::spliceSection(0x05, {0, 0, 0, 1, 0x7F}, {}, 400));
  // Intact CRC, a splice_insert that says a duration it hasn't room for.
  m.section(tsgen::kCuePid, tsgen::spliceSection(0x05, {0, 0, 0, 2, 0x7F, 0xFF, 0xFE, 0x00}));
  // Intact CRC, a time_signal without its time.
  m.section(tsgen::kCuePid, tsgen::spliceSection(0x06, {0xFE, 0x00}));
  // An unknown protocol version.
  Bytes v1 = tsgen::spliceNull();
  v1.resize(v1.size() - 4);
  v1[3] = 1;
  tsgen::withCrc(v1);
  m.section(tsgen::kCuePid, v1);
  // A section cut short by a new one on the same PID.
  tsgen::Muxer cut;
  Bytes longOne = tsgen::spliceSection(0x00, {}, Bytes(300, 0xAB));
  cut.section(tsgen::kCuePid, longOne);
  m.out.insert(m.out.end(), cut.out.begin(), cut.out.begin() + 188);
  // Then a good one.
  m.section(tsgen::kCuePid, tsgen::spliceInsert(3, true, 45));
  ad::Tally t = scan(m.out);
  CHECK(t.cues == 1);
  CHECK(t.spliceInsert == 1);
  CHECK(t.longestBreak90 == 45 * 90000);
  CHECK(t.skipped == 4);
  // Garbage alone: nothing read wrong, nothing crashes.
  Bytes noise(188 * 50);
  for (size_t i = 0; i < noise.size(); ++i) noise[i] = static_cast<uint8_t>((i * 131 + 7) % 251);
  for (size_t i = 0; i < noise.size(); i += 188) {
    noise[i] = 0x47;
    noise[i + 1] = 0x40;  // a section start ...
    noise[i + 3] = 0x10;
    noise[i + 4] = 0x00;
    noise[i + 5] = 0xFC;  // ... that says it's a cue
  }
  ad::Tally n = scan(noise);
  CHECK(n.cues == 0);
  CHECK(n.packets == 50);
}

TEST_CASE("ad markers: none declared: none, and a cue on any PID still seen") {
  tsgen::Muxer m;
  video(m, tsgen::pmt(0x1B, 0x0F), 900000, 50);
  ad::Tally t = scan(m.out);
  CHECK(t.tables);
  CHECK_FALSE(t.declared);
  CHECK(t.cues == 0);
  CHECK(ad::found(t) == ad::Found::None);
  CHECK(ad::readText(t) == "none");
  CHECK(ad::replayText(t) == "no ad markers in 2 s");
  // Video and sound PES never read as cues; an undeclared one on its own PID is.
  m.section(0x1FF0, tsgen::spliceInsert(5, true));
  ad::Tally u = scan(m.out);
  CHECK(u.cues == 1);
  CHECK(ad::readText(u) == "1 cue (splice_insert, out of network), not declared");
  // No tables at all.
  tsgen::Muxer bare;
  bare.null(10);
  ad::Tally b = scan(bare.out);
  CHECK(b.read());
  CHECK(ad::readText(b) == "no tables read");
  // Nothing read: no words.
  CHECK(ad::readText(ad::Tally{}).empty());
  CHECK(ad::replayText(ad::Tally{}).empty());
}

TEST_CASE("ad markers: take() hands the counts over and keeps what the tables said; merged tallies") {
  tsgen::Muxer m;
  video(m, declaredPmt(), 900000);
  m.section(tsgen::kCuePid, tsgen::spliceInsert(1, true, 30));
  ad::Scanner s;
  s.feed(m.out.data(), m.out.size());
  ad::Tally first = s.take();
  CHECK(first.cues == 1);
  CHECK(s.tally().cues == 0);
  CHECK(s.tally().declared);
  CHECK(s.tally().packets == 0);
  tsgen::Muxer more;
  more.section(tsgen::kCuePid, tsgen::spliceInsert(2, true, 90));
  s.feed(more.out.data(), more.out.size());
  ad::Tally second = s.take();
  CHECK(second.cues == 1);
  first.merge(second);
  CHECK(first.cues == 2);
  CHECK(first.longestBreak90 == 90 * 90000);
  // reset(): a new stream.
  s.reset();
  CHECK_FALSE(s.tally().declared);
}

TEST_CASE("ad markers: the LG relay's picture check says whether the PMT declares them") {
  tsgen::Scene s;
  s.seconds = 1;
  s.cueType = ad::kStreamType;
  tsgen::Bytes marked = tsgen::build(s);
  s.cueType = 0;
  tsgen::Bytes plain = tsgen::build(s);
  marigold::media::StreamCheck check(marigold::media::Device::LgWebOs);
  check.begin(1);
  CHECK(check.snapshot().adMarkers == -1);  // no PMT yet
  check.bytes(marked.data(), marked.size(), 1);
  CHECK(check.snapshot().adMarkers == 1);
  check.begin(2);
  check.bytes(plain.data(), plain.size(), 2);
  CHECK(check.snapshot().adMarkers == 0);
  // A "CUEI" registration on the program.
  tsgen::Muxer m;
  m.section(0, tsgen::pat());
  m.section(tsgen::kPmtPid, tsgen::pmtOf(tsgen::esEntry(0x1B, tsgen::kVideoPid), tsgen::cuei()));
  m.null(3);  // the probe locks on three packets in step
  marigold::media::TsVideoProbe probe;
  CHECK(probe.adMarkersDeclared() == -1);
  probe.feed(m.out.data(), m.out.size());
  CHECK(probe.adMarkersDeclared() == 1);
}

TEST_CASE("ad markers: the words for the log, counts and minutes, never a link") {
  ad::Tally t;
  t.packets = 1000;
  t.tables = t.declared = true;
  t.cues = t.spliceInsert = 3;
  t.outOfNetwork = 2;
  t.videoMs = 41 * 60 * 1000 + 10000;
  CHECK(ad::replayText(t) == "ad markers seen 3 times in 41 min (splice_insert, out of network)");
  t.longestBreak90 = 90 * 90000;
  CHECK(ad::replayText(t) == "ad markers seen 3 times in 41 min (splice_insert, out of network, break 90 s)");
  t.cues = t.spliceInsert = 1;
  t.outOfNetwork = 0;
  t.longestBreak90 = 0;
  CHECK(ad::replayText(t) == "ad markers seen once in 41 min (splice_insert)");
  t.cues = 0;
  t.spliceInsert = 0;
  CHECK(ad::replayText(t) == "ad markers declared, none seen in 41 min");
  t.declared = false;
  CHECK(ad::replayText(t) == "no ad markers in 41 min");
  t.videoMs = 0;
  CHECK(ad::replayText(t) == "no ad markers");
  CHECK(ad::durationText(35000) == "35 s");
  CHECK(ad::durationText(0).empty());
  for (const std::string& s : {ad::replayText(t), ad::readText(t)}) noLink(s);
}
