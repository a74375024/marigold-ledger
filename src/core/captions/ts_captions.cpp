#include "core/captions/ts_captions.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace marigold::captions {
namespace {

constexpr size_t kPacket = 188;
constexpr uint8_t kSync = 0x47;
constexpr size_t kLockBytes = 2 * kPacket + 1;  // to see three sync bytes in a row
constexpr size_t kMaxSection = 1024;     // PAT and PMT sections are at most 1024 bytes
constexpr size_t kMaxSei = 64u << 10;    // caption SEI is tens of bytes
constexpr size_t kMaxUnitPairs = 256;    // cc_count is at most 31 per message
constexpr size_t kMaxHeld = 1024;
constexpr size_t kPesReserve = 16u << 10;
constexpr size_t kPesShrink = 256u << 10;  // capacity given back after a huge PES
constexpr int64_t kWrap = int64_t{1} << 33;
constexpr size_t kNone = static_cast<size_t>(-1);

// MPEG-2 CRC-32 over a whole section, CRC included: 0 when intact.
uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    crc ^= static_cast<uint32_t>(p[i]) << 24;
    for (int b = 0; b < 8; ++b) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
  }
  return crc;
}

// The next 00 00 01 at or after `from`, or kNone.
size_t findStartCode(const uint8_t* b, size_t from, size_t end) {
  size_t i = from + 2;
  while (i < end) {
    const void* q = std::memchr(b + i, 1, end - i);
    if (!q) return kNone;
    size_t k = static_cast<size_t>(static_cast<const uint8_t*>(q) - b);
    if (b[k - 1] == 0 && b[k - 2] == 0) return k - 2;
    i = k + 1;
  }
  return kNone;
}

int64_t floorDiv(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

}  // namespace

TsCaptions::TsCaptions() {
  patSection_.buf.reserve(kMaxSection);
  pmtSection_.buf.reserve(kMaxSection);
  rbsp_.reserve(256);
  unitPairs_.reserve(32);
  held_.reserve(64);
  ready_.reserve(64);
}

void TsCaptions::reset() {
  carryLen_ = 0;
  locked_ = false;
  patSection_.buf.clear();
  patSection_.open = false;
  pmtSection_.buf.clear();
  pmtSection_.open = false;
  pmtPid_ = -1;
  videoPid_ = -1;
  codec_ = Codec::None;
  dropPes();
  lastCc_ = -1;
  havePrev90_ = false;
  haveNewest_ = false;
  haveOut_ = false;
  units_ = 0;
  held_.clear();
  ready_.clear();
  sawCaptionData_ = false;
}

// --- sync ----------------------------------------------------------------------

void TsCaptions::feed(const uint8_t* data, size_t n) {
  while (n > 0) {
    if (carryLen_ > 0) {
      // Finish the packet held over from the last call, then go back to
      // reading straight from the caller's buffer.
      size_t want = locked_ ? kPacket : kLockBytes;
      size_t old = carryLen_;
      size_t k = carryLen_ < want ? std::min(n, want - carryLen_) : 0;
      std::memcpy(carry_ + carryLen_, data, k);
      carryLen_ += k;
      data += k;
      n -= k;
      if (carryLen_ < want) return;
      size_t used = consume(carry_, carryLen_);
      if (used >= old) {
        size_t back = carryLen_ - used;  // only new bytes are left: hand them back
        data -= back;
        n += back;
        carryLen_ = 0;
      } else {
        std::memmove(carry_, carry_ + used, carryLen_ - used);
        carryLen_ -= used;
      }
      continue;
    }
    size_t used = consume(data, n);
    data += used;
    n -= used;
    if (n > 0) {
      std::memcpy(carry_, data, n);  // what can't be used yet, from a sync byte
      carryLen_ = n;
      return;
    }
  }
}

size_t TsCaptions::consume(const uint8_t* p, size_t n) {
  size_t i = 0;
  while (i < n) {
    if (p[i] != kSync) {
      locked_ = false;
      const void* q = std::memchr(p + i, kSync, n - i);
      if (!q) return n;
      i = static_cast<size_t>(static_cast<const uint8_t*>(q) - p);
    }
    if (n - i < kPacket) break;
    if (!locked_) {
      // Out of sync, a sync byte counts only when the next two packets' are
      // there too.
      if (n - i < kLockBytes) break;
      if (p[i + kPacket] != kSync || p[i + 2 * kPacket] != kSync) {
        ++i;
        continue;
      }
      locked_ = true;
    }
    packet(p + i);
    i += kPacket;
  }
  return i;
}

void TsCaptions::packet(const uint8_t* p) {
  if (p[1] & 0x80) return;  // transport error
  bool start = (p[1] & 0x40) != 0;
  int pid = ((p[1] & 0x1F) << 8) | p[2];
  int control = (p[3] >> 4) & 0x03;
  int cc = p[3] & 0x0F;
  size_t off = 4;
  bool discontinuity = false;
  if (control & 0x02) {
    size_t len = p[4];
    off = 5 + len;
    if (off > kPacket) return;
    if (len > 0) discontinuity = (p[5] & 0x80) != 0;
  }
  if (!(control & 0x01) || off >= kPacket) return;  // no payload
  const uint8_t* payload = p + off;
  size_t n = kPacket - off;
  if (pid == 0) {
    section(patSection_, payload, n, start, true);
  } else if (pid == pmtPid_) {
    section(pmtSection_, payload, n, start, false);
  } else if (pid == videoPid_) {
    video(payload, n, start, cc, discontinuity);
  }
}

// --- PAT and PMT ---------------------------------------------------------------

void TsCaptions::section(Section& s, const uint8_t* p, size_t n, bool start, bool isPat) {
  if (start) {
    size_t pointer = p[0];
    if (1 + pointer > n) {
      s.open = false;
      return;
    }
    s.buf.clear();
    s.open = true;
    p += 1 + pointer;
    n -= 1 + pointer;
  } else if (!s.open) {
    return;
  }
  size_t room = kMaxSection - s.buf.size();
  s.buf.insert(s.buf.end(), p, p + std::min(n, room));
  if (s.buf.size() < 3) return;
  size_t total = 3 + (((s.buf[1] & 0x0F) << 8) | s.buf[2]);
  if (total > kMaxSection || total < 12) {
    s.open = false;
    return;
  }
  if (s.buf.size() < total) return;
  s.open = false;
  sectionDone(s.buf.data(), total, isPat);
}

void TsCaptions::sectionDone(const uint8_t* p, size_t n, bool isPat) {
  if (crc32(p, n) != 0) return;
  if (isPat) {
    readPat(p, n);
  } else {
    readPmt(p, n);
  }
}

void TsCaptions::readPat(const uint8_t* p, size_t n) {
  if (p[0] != 0x00) return;
  for (size_t i = 8; i + 4 <= n - 4; i += 4) {
    int program = (p[i] << 8) | p[i + 1];
    if (program == 0) continue;  // the network PID
    int pid = ((p[i + 2] & 0x1F) << 8) | p[i + 3];
    if (pid != pmtPid_) {
      pmtPid_ = pid;
      pmtSection_.open = false;
    }
    return;
  }
}

void TsCaptions::readPmt(const uint8_t* p, size_t n) {
  if (p[0] != 0x02) return;
  size_t end = n - 4;
  size_t i = 12 + (((p[10] & 0x0F) << 8) | p[11]);
  while (i + 5 <= end) {
    int type = p[i];
    int pid = ((p[i + 1] & 0x1F) << 8) | p[i + 2];
    size_t info = ((p[i + 3] & 0x0F) << 8) | p[i + 4];
    Codec codec = type == 0x1B ? Codec::H264 : type == 0x24 ? Codec::Hevc : type == 0x02 ? Codec::Mpeg2 : Codec::None;
    if (codec != Codec::None) {
      if (pid != videoPid_ || codec != codec_) {
        videoPid_ = pid;
        codec_ = codec;
        dropPes();
        lastCc_ = -1;
      }
      return;
    }
    i += 5 + info;
  }
}

// --- PES -----------------------------------------------------------------------

void TsCaptions::video(const uint8_t* p, size_t n, bool start, int cc, bool discontinuity) {
  if (lastCc_ >= 0 && !discontinuity) {
    if (cc == lastCc_) return;                   // a repeated packet
    if (cc != ((lastCc_ + 1) & 0x0F)) dropPes();  // lost packets: this PES is damaged
  }
  lastCc_ = cc;
  if (start) {
    endPes();
    startPes();
  }
  if (!pesOpen_) return;
  pesReceived_ += n;
  if (!unitDone_) {
    if (pes_.size() + n > kMaxPesBytes) {
      dropPes();
      return;
    }
    pes_.insert(pes_.end(), p, p + n);
    parsePes();
  }
  if (pesOpen_ && pesLength_ != 0 && pesReceived_ >= pesLength_) endPes();
}

void TsCaptions::startPes() {
  if (pes_.capacity() < kPesReserve) pes_.reserve(kPesReserve);
  pes_.clear();
  pesOpen_ = true;
  pesHeader_ = false;
  unitDone_ = false;
  pesLength_ = 0;
  pesReceived_ = 0;
  scan_ = 0;
  haveNal_ = false;
  havePts_ = false;
  unitPairs_.clear();
}

void TsCaptions::dropPes() {
  pesOpen_ = false;
  pes_.clear();
  if (pes_.capacity() > kPesShrink) std::vector<uint8_t>().swap(pes_);
  unitPairs_.clear();
}

void TsCaptions::endPes() {
  if (pesOpen_ && pesHeader_ && !unitDone_) {
    if (haveNal_) unit(nal_, pes_.size());
    accessUnitDone();
  }
  dropPes();
}

void TsCaptions::parsePes() {
  if (!pesHeader_) {
    if (pes_.size() < 9) return;
    const uint8_t* h = pes_.data();
    if (h[0] != 0 || h[1] != 0 || h[2] != 1 || (h[3] & 0xF0) != 0xE0 || (h[6] & 0xC0) != 0x80) {
      dropPes();
      return;
    }
    size_t start = 9 + static_cast<size_t>(h[8]);
    if (pes_.size() < start) return;
    size_t len = (static_cast<size_t>(h[4]) << 8) | h[5];
    pesLength_ = len ? len + 6 : 0;
    if ((h[7] & 0x80) && h[8] >= 5) {
      int64_t pts = (static_cast<int64_t>(h[9] & 0x0E) << 29) | (static_cast<int64_t>(h[10]) << 22) |
                    (static_cast<int64_t>(h[11] & 0xFE) << 14) | (static_cast<int64_t>(h[12]) << 7) |
                    (h[13] >> 1);
      ptsMs_ = unwrapMs(pts);
      havePts_ = true;
    }
    pesHeader_ = true;
    scan_ = start;
  }
  scanUnits();
}

void TsCaptions::scanUnits() {
  const uint8_t* b = pes_.data();
  size_t end = pes_.size();
  while (true) {
    size_t s = findStartCode(b, scan_, end);
    if (s == kNone) {
      // Keep the last two bytes: a start code may be split across packets.
      if (end >= scan_ + 2) scan_ = end - 2;
      return;
    }
    if (s + 3 >= end) {
      scan_ = s;  // its header byte isn't here yet
      return;
    }
    if (haveNal_) unit(nal_, s);
    nal_ = s + 3;
    haveNal_ = true;
    scan_ = s + 3;
    uint8_t h = b[nal_];
    bool picture = false;
    switch (codec_) {
      case Codec::H264: picture = (h & 0x1F) >= 1 && (h & 0x1F) <= 5; break;
      case Codec::Hevc: picture = ((h >> 1) & 0x3F) <= 31; break;
      case Codec::Mpeg2: picture = h >= 0x01 && h <= 0xAF; break;
      case Codec::None: break;
    }
    if (picture) {
      // Caption data comes before the first slice: the rest is never copied.
      unitDone_ = true;
      accessUnitDone();
      pes_.clear();
      return;
    }
  }
}

void TsCaptions::unit(size_t from, size_t to) {
  if (to <= from) return;
  const uint8_t* p = pes_.data() + from;
  size_t n = to - from;
  switch (codec_) {
    case Codec::H264:
      if ((p[0] & 0x1F) == 6) sei(p + 1, n - 1);
      break;
    case Codec::Hevc:
      if (n >= 2 && ((p[0] >> 1) & 0x3F) == 39) sei(p + 2, n - 2);
      break;
    case Codec::Mpeg2:
      // Picture user data: "GA94", user_data_type_code 3, cc_data.
      if (p[0] == 0xB2 && n >= 6 && std::memcmp(p + 1, "GA94", 4) == 0 && p[5] == 0x03) ccData(p + 6, n - 6);
      break;
    case Codec::None:
      break;
  }
}

void TsCaptions::sei(const uint8_t* p, size_t n) {
  if (n > kMaxSei) return;
  // Remove emulation prevention bytes (00 00 03).
  rbsp_.clear();
  int zeros = 0;
  for (size_t i = 0; i < n; ++i) {
    uint8_t c = p[i];
    if (zeros >= 2 && c == 0x03) {
      zeros = 0;
      continue;
    }
    rbsp_.push_back(c);
    zeros = c == 0 ? zeros + 1 : 0;
  }
  const uint8_t* r = rbsp_.data();
  size_t len = rbsp_.size();
  size_t i = 0;
  while (i + 2 <= len) {
    size_t type = 0, size = 0;
    while (i < len && r[i] == 0xFF) type += r[i++];
    if (i >= len) return;
    type += r[i++];
    while (i < len && r[i] == 0xFF) size += r[i++];
    if (i >= len) return;
    size += r[i++];
    if (size > len - i) return;
    // user_data_registered_itu_t_t35: USA, ATSC, "GA94", cc_data.
    const uint8_t* m = r + i;
    if (type == 4 && size >= 8 && m[0] == 0xB5 && m[1] == 0x00 && m[2] == 0x31 && std::memcmp(m + 3, "GA94", 4) == 0 &&
        m[7] == 0x03) {
      ccData(m + 8, size - 8);
    }
    i += size;
  }
}

void TsCaptions::ccData(const uint8_t* p, size_t n) {
  if (n < 2 || !(p[0] & 0x40)) return;  // process_cc_data_flag
  size_t count = p[0] & 0x1F;
  p += 2;  // flags, em_data
  n -= 2;
  for (size_t k = 0; k < count && 3 * k + 3 <= n; ++k) {
    const uint8_t* t = p + 3 * k;
    if (!(t[0] & 0x04)) continue;  // cc_valid
    int type = t[0] & 0x03;
    if (type > 1) continue;  // 708 (DTVCC)
    if ((t[1] & 0x7F) == 0 && (t[2] & 0x7F) == 0) continue;  // padding
    sawCaptionData_ = true;
    if (unitPairs_.size() < kMaxUnitPairs) unitPairs_.push_back(CcPair{0, type, t[1], t[2]});
  }
}

// --- PTS and presentation order ------------------------------------------------

int64_t TsCaptions::unwrapMs(int64_t pts90) {
  // 33 bits wrap every 26.5 hours: take the value nearest the previous one.
  if (havePrev90_) {
    int64_t c = prev90_ - (((prev90_ % kWrap) + kWrap) % kWrap) + pts90;
    if (c - prev90_ > kWrap / 2) {
      c -= kWrap;
    } else if (prev90_ - c > kWrap / 2) {
      c += kWrap;
    }
    pts90 = c;
  }
  havePrev90_ = true;
  prev90_ = pts90;
  return floorDiv(pts90, 90);
}

void TsCaptions::accessUnitDone() {
  int64_t pts = havePts_ ? ptsMs_ : (haveNewest_ ? newestMs_ : 0);
  ++units_;
  if (haveNewest_ && pts < newestMs_ - kPtsJumpMs) {
    // The clock went back (a splice or a new stream): finish the old timeline.
    release(true);
    newestMs_ = pts;
    haveOut_ = false;
  }
  if (!haveNewest_ || pts > newestMs_) newestMs_ = pts;
  haveNewest_ = true;
  for (const CcPair& c : unitPairs_) {
    CcPair pair{pts, c.field, c.b1, c.b2};
    if (haveOut_ && pts < lastOutMs_) {
      ready_.push_back(pair);  // later than the window allows: too late to reorder
    } else {
      held_.push_back(Held{units_, pair});
    }
  }
  unitPairs_.clear();
  release(held_.size() > kMaxHeld);
}

void TsCaptions::release(bool all) {
  while (!held_.empty()) {
    const Held* first = &held_[0];
    for (const Held& h : held_) {
      int64_t a = h.pair.ptsMs, b = first->pair.ptsMs;
      if (a < b || (a == b && h.unit < first->unit)) first = &h;
    }
    if (!all && units_ - first->unit < static_cast<uint64_t>(kReorderUnits) &&
        newestMs_ - first->pair.ptsMs <= kReorderMs) {
      break;
    }
    uint64_t unit = first->unit;
    lastOutMs_ = first->pair.ptsMs;
    haveOut_ = true;
    for (const Held& h : held_) {
      if (h.unit == unit) ready_.push_back(h.pair);
    }
    held_.erase(std::remove_if(held_.begin(), held_.end(), [unit](const Held& h) { return h.unit == unit; }),
                held_.end());
  }
  if (ready_.size() > kMaxReadyPairs) {
    // Nobody is taking them: keep the newest.
    ready_.erase(ready_.begin(), ready_.begin() + static_cast<std::ptrdiff_t>(ready_.size() - kMaxReadyPairs / 2));
  }
}

void TsCaptions::flush() { release(true); }

std::vector<CcPair> TsCaptions::take() {
  std::vector<CcPair> out;
  out.swap(ready_);
  ready_.reserve(64);
  return out;
}

void TsCaptions::takeInto(std::vector<CcPair>& out) {
  out.clear();
  out.swap(ready_);
}

}  // namespace marigold::captions
