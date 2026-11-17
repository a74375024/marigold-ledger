#include "core/media/ts_start.hpp"

#include <cstring>

namespace marigold::media::tsclock {
namespace {

int64_t pts90(const uint8_t* p) {
  return (static_cast<int64_t>(p[0] & 0x0E) << 29) | (static_cast<int64_t>(p[1]) << 22) |
         (static_cast<int64_t>(p[2] & 0xFE) << 14) | (static_cast<int64_t>(p[3]) << 7) | (p[4] >> 1);
}

struct Header {
  int pid = -1;
  bool start = false;    // payload_unit_start_indicator
  bool key = false;      // random_access_indicator
  size_t payload = 188;  // offset of the payload (188: none)
};

Header header(const uint8_t* p) {
  Header h;
  h.pid = ((p[1] & 0x1F) << 8) | p[2];
  h.start = (p[1] & 0x40) != 0;
  int afc = (p[3] >> 4) & 3;
  size_t at = 4;
  if (afc & 2) {
    size_t len = p[4];
    if (len > 0 && 5 + len <= 188) h.key = (p[5] & 0x40) != 0;
    at = 5 + len;
  }
  if ((afc & 1) && at < 188) h.payload = at;
  return h;
}

// The PMT's PID from a PAT packet (-1: none read).
int pmtPid(const uint8_t* p, const Header& h) {
  if (!h.start || h.payload >= 188) return -1;
  size_t at = h.payload + 1 + p[h.payload];
  if (at + 8 > 188 || p[at] != 0x00) return -1;
  size_t len = static_cast<size_t>(((p[at + 1] & 0x0F) << 8) | p[at + 2]);
  size_t end = std::min<size_t>(188, at + 3 + len);
  if (end < at + 12) return -1;
  end -= 4;  // the CRC
  for (size_t i = at + 8; i + 4 <= end; i += 4) {
    int program = (p[i] << 8) | p[i + 1];
    if (program != 0) return ((p[i + 2] & 0x1F) << 8) | p[i + 3];
  }
  return -1;
}

}  // namespace

int64_t delta(int64_t b, int64_t a) {
  int64_t d = ((b - a) % kWrap + kWrap) % kWrap;
  return d > kWrap / 2 ? d - kWrap : d;
}

int64_t ahead(int64_t b, int64_t a) { return ((b - a) % kWrap + kWrap) % kWrap; }

std::optional<int64_t> pesPts(const uint8_t* pes, size_t n, bool* video) {
  if (n < 9 || pes[0] != 0 || pes[1] != 0 || pes[2] != 1) return std::nullopt;
  if (video) *video = (pes[3] & 0xF0) == 0xE0;
  if (!(pes[7] & 0x80) || n < 14) return std::nullopt;
  return pts90(pes + 9);
}

// ---------------------------------------------------------------- TsClock

void TsClock::feed(const uint8_t* data, size_t n) {
  packets_.feed(data, n, [this](const uint8_t* p) { packet(p); });
}

void TsClock::packet(const uint8_t* p) {
  bytes_ += 188;
  Header h = header(p);
  if (!h.start || h.payload >= 188) return;
  if (videoPid_ >= 0 && h.pid != videoPid_) return;
  bool video = false;
  std::optional<int64_t> pts = pesPts(p + h.payload, 188 - h.payload, &video);
  if (!video) return;
  videoPid_ = h.pid;
  if (!pts) return;
  if (!haveFirst_) {
    haveFirst_ = true;
    first_ = last_ = *pts;
    firstAt_ = bytes_;
    return;
  }
  last_ = *pts;
  // Pictures come out of order (B frames): the furthest one counts.
  int64_t span = delta(*pts, first_);
  if (span > span_) {
    span_ = span;
    spanAt_ = bytes_;
  }
}

uint32_t TsClock::byteRate() const {
  if (span_ < kHz || spanAt_ <= firstAt_) return 0;
  double rate = static_cast<double>(spanAt_ - firstAt_) * kHz / static_cast<double>(span_);
  return rate > 4e9 ? 0 : static_cast<uint32_t>(rate);
}

// -------------------------------------------------------------- TsStartAt

TsStartAt::TsStartAt(int skipSeconds, uint64_t maxBytes) : skip_(std::max(0, skipSeconds)), maxBytes_(maxBytes) {}

void TsStartAt::target(int64_t pts90) {
  target_ = ((pts90 % kWrap) + kWrap) % kWrap;
  haveTarget_ = true;
  given_ = true;
}

int64_t TsStartAt::base() const {
  return given_ ? target_ - static_cast<int64_t>(skip_) * kHz : first_;
}

void TsStartAt::feed(const uint8_t* data, size_t n, std::string& out) {
  packets_.feed(data, n, [this, &out](const uint8_t* p) { packet(p, out); });
}

void TsStartAt::start(const uint8_t* p, std::string& out, int64_t pts) {
  passing_ = true;
  // The tables first, so the player knows the streams at once.
  if (havePat_) out.append(reinterpret_cast<const char*>(pat_), 188);
  if (havePmt_) out.append(reinterpret_cast<const char*>(pmt_), 188);
  out.append(reinterpret_cast<const char*>(p), 188);
  skipped_ = static_cast<int>(std::max<int64_t>(0, delta(pts, base())) / kHz);
}

void TsStartAt::packet(const uint8_t* p, std::string& out) {
  if (passing_) {
    out.append(reinterpret_cast<const char*>(p), 188);
    return;
  }
  Header h = header(p);
  if (h.pid == 0) {
    std::memcpy(pat_, p, 188);
    havePat_ = true;
    if (int pid = pmtPid(p, h); pid > 0) pmtPid_ = pid;
  } else if (h.pid == pmtPid_ && h.start) {
    std::memcpy(pmt_, p, 188);
    havePmt_ = true;
  }
  if (h.start && h.payload < 188 && (videoPid_ < 0 || h.pid == videoPid_)) {
    bool video = false;
    std::optional<int64_t> pts = pesPts(p + h.payload, 188 - h.payload, &video);
    if (video) {
      videoPid_ = h.pid;
      if (h.key) sawKey_ = true;
      if (pts) {
        if (!haveFirst_) {
          haveFirst_ = true;
          first_ = *pts;
          if (!haveTarget_) {
            target_ = ((first_ + static_cast<int64_t>(skip_) * kHz) % kWrap + kWrap) % kWrap;
            haveTarget_ = true;
          }
        }
        latest_ = *pts;
        int64_t past = delta(*pts, target_);
        // At the moment: a key picture where the stream marks them (or
        // five seconds on without one), else the first picture there.
        if (past >= 0 && (h.key || !sawKey_ || past >= 5 * kHz)) {
          start(p, out, *pts);
          return;
        }
      }
    }
  }
  dropped_ += 188;
  if (maxBytes_ && dropped_ >= maxBytes_) {
    // Too far to read through: passed on from here, and said where.
    passing_ = true;
    if (havePat_) out.append(reinterpret_cast<const char*>(pat_), 188);
    if (havePmt_) out.append(reinterpret_cast<const char*>(pmt_), 188);
    out.append(reinterpret_cast<const char*>(p), 188);
    skipped_ = haveFirst_ ? static_cast<int>(std::max<int64_t>(0, delta(latest_, base())) / kHz) : 0;
  }
}

}  // namespace marigold::media::tsclock
