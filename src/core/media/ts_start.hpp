// Build 172: the video clock of an MPEG-TS stream from its bytes, and a
// stream cut to start at a moment inside it.
//
// TsClock reads the presentation time (PTS, 33 bits at 90 kHz) of every
// video PES start (stream_id 0xE0..0xEF): the first one, the span read so
// far (wraps handled) and the bytes that took, for the archive timing
// measurement (archive_timing.hpp): where a replay's stream starts in the
// programme, and its bytes per second.
//
// TsStartAt drops a stream's bytes until its video reaches a moment (a
// number of seconds after its first picture), then passes everything from
// a packet boundary on: the last PAT and PMT packets first, then the video
// PES that reaches the moment (a key picture when the stream marks them),
// so a player starts there as it would at the stream's own start. The
// replay relay (platform/replay_relay) runs it.
//
// Any chunking gives the same result; no allocation per packet.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace marigold::media::tsclock {

constexpr int64_t kWrap = int64_t{1} << 33;  // the PTS clock wraps here (26.5 h)
constexpr int64_t kHz = 90000;

// b - a on the 33-bit clock, the short way round (|result| < 13.25 h).
int64_t delta(int64_t b, int64_t a);
// b - a going forward only, in [0, kWrap): how far b is after a.
int64_t ahead(int64_t b, int64_t a);
// The PTS in a PES header (`pes` at its 00 00 01, `n` bytes there);
// nullopt when it carries none. `video`: set when its stream_id is video.
std::optional<int64_t> pesPts(const uint8_t* pes, size_t n, bool* video = nullptr);

// Packets one at a time from any chunking (sync found and kept on 0x47
// every 188 bytes). The callback gets each whole packet.
class Packets {
 public:
  template <typename F>
  void feed(const uint8_t* data, size_t n, F&& onPacket) {
    while (n > 0) {
      if (len_ < 188) {
        size_t take = std::min(n, size_t{188} - len_);
        for (size_t i = 0; i < take; ++i) buf_[len_ + i] = data[i];
        len_ += take;
        data += take;
        n -= take;
        if (len_ < 188) return;
      }
      if (buf_[0] != 0x47) {
        // Out of step: drop up to the next 0x47 and read on from there.
        size_t i = 1;
        while (i < len_ && buf_[i] != 0x47) ++i;
        for (size_t j = i; j < len_; ++j) buf_[j - i] = buf_[j];
        len_ -= i;
        skipped_ += i;
        continue;
      }
      onPacket(buf_);
      len_ = 0;
    }
  }
  uint64_t skipped() const { return skipped_; }

 private:
  uint8_t buf_[188] = {};
  size_t len_ = 0;
  uint64_t skipped_ = 0;
};

class TsClock {
 public:
  void feed(const uint8_t* data, size_t n);
  // The first video PTS seen (nullopt: none yet).
  std::optional<int64_t> first() const { return haveFirst_ ? std::optional<int64_t>(first_) : std::nullopt; }
  // The latest video PTS (raw, 33-bit).
  std::optional<int64_t> last() const { return haveFirst_ ? std::optional<int64_t>(last_) : std::nullopt; }
  // Seconds of video read, from the first PTS to the furthest (0 none).
  double seconds() const { return static_cast<double>(span_) / kHz; }
  uint64_t bytes() const { return bytes_; }
  // The bytes per second of video read so far, from the first picture to
  // the furthest one (0 when under a second is read).
  uint32_t byteRate() const;

 private:
  void packet(const uint8_t* p);
  Packets packets_;
  int videoPid_ = -1;
  bool haveFirst_ = false;
  int64_t first_ = 0, last_ = 0, span_ = 0;
  uint64_t bytes_ = 0, firstAt_ = 0, spanAt_ = 0;
};

class TsStartAt {
 public:
  // `skipSeconds` after the stream's first video PTS. `maxBytes`: dropped
  // at most this much, then it passes on from wherever it is (0: no cap).
  TsStartAt(int skipSeconds, uint64_t maxBytes);
  // Or from a PTS already known (the stream's start read elsewhere).
  void target(int64_t pts90);
  // The bytes to pass on for this piece (appended to `out`).
  void feed(const uint8_t* data, size_t n, std::string& out);
  bool passing() const { return passing_; }
  // Once passing: the seconds actually skipped (the PES it started on
  // against the stream's first PTS, or the target's base); -1 not yet.
  int skipped() const { return skipped_; }
  // The stream's first video PTS (nullopt: none seen yet).
  std::optional<int64_t> first() const { return haveFirst_ ? std::optional<int64_t>(first_) : std::nullopt; }
  uint64_t dropped() const { return dropped_; }
  // Reading through took too long: passes on from the next packet (and
  // says where it began).
  void stopDropping() { maxBytes_ = dropped_ + 1; }

 private:
  void packet(const uint8_t* p, std::string& out);
  void start(const uint8_t* p, std::string& out, int64_t pts);
  int64_t base() const;  // the PTS the skip counts from
  Packets packets_;
  int skip_ = 0;
  uint64_t maxBytes_ = 0;
  bool passing_ = false;
  bool haveFirst_ = false, haveTarget_ = false, given_ = false, sawKey_ = false;
  int64_t first_ = 0, target_ = 0, latest_ = 0;
  int videoPid_ = -1, pmtPid_ = -1;
  uint8_t pat_[188] = {}, pmt_[188] = {};
  bool havePat_ = false, havePmt_ = false;
  uint64_t dropped_ = 0;
  int skipped_ = -1;
};

}  // namespace marigold::media::tsclock
