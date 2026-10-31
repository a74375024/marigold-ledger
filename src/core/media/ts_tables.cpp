#include "core/media/ts_tables.hpp"

#include <algorithm>
#include <cstring>

namespace marigold::media::ts {

uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    crc ^= static_cast<uint32_t>(p[i]) << 24;
    for (int b = 0; b < 8; ++b) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
  }
  return crc;
}

const uint8_t* SectionReader::feed(const uint8_t* p, size_t n, bool start, size_t* size) {
  if (start) {
    size_t pointer = p[0];
    if (1 + pointer > n) {
      open_ = false;
      return nullptr;
    }
    buf_.clear();
    open_ = true;
    p += 1 + pointer;
    n -= 1 + pointer;
  } else if (!open_) {
    return nullptr;
  }
  size_t room = kMaxSection - buf_.size();
  buf_.insert(buf_.end(), p, p + std::min(n, room));
  if (buf_.size() < 3) return nullptr;
  size_t total = 3 + (((buf_[1] & 0x0F) << 8) | buf_[2]);
  if (total > kMaxSection || total < 12) {
    open_ = false;
    return nullptr;
  }
  if (buf_.size() < total) return nullptr;
  open_ = false;
  if (crc32(buf_.data(), total) != 0) return nullptr;
  *size = total;
  return buf_.data();
}

int patFirstProgram(const uint8_t* p, size_t n) {
  if (n < 12 || p[0] != 0x00) return -1;
  for (size_t i = 8; i + 4 <= n - 4; i += 4) {
    int program = (p[i] << 8) | p[i + 1];
    if (program == 0) continue;  // the network PID
    return ((p[i + 2] & 0x1F) << 8) | p[i + 3];
  }
  return -1;
}

namespace {

// What the descriptors of one stream say: a CA descriptor, a private audio codec.
void descriptors(const uint8_t* d, size_t n, bool& ca, const char*& audio) {
  size_t i = 0;
  while (i + 2 <= n) {
    uint8_t tag = d[i];
    size_t len = d[i + 1];
    if (i + 2 + len > n) return;
    const uint8_t* v = d + i + 2;
    switch (tag) {
      case 0x09: ca = true; break;
      case 0x6A: audio = "ac3"; break;
      case 0x7A: audio = "eac3"; break;
      case 0x7B: audio = "dts"; break;
      case 0x7C: audio = "aac"; break;
      case 0x05:  // registration: format_identifier
        if (len >= 4) {
          if (std::memcmp(v, "AC-3", 4) == 0) audio = "ac3";
          else if (std::memcmp(v, "EAC3", 4) == 0) audio = "eac3";
          else if (std::memcmp(v, "DTS", 3) == 0) audio = "dts";
        }
        break;
      default: break;
    }
    i += 2 + len;
  }
}

}  // namespace

bool parsePmt(const uint8_t* p, size_t n, ProgramMap& out) {
  if (n < 16 || p[0] != 0x02) return false;
  out.streams.clear();
  out.conditionalAccess = false;
  size_t end = n - 4;
  size_t info = ((p[10] & 0x0F) << 8) | p[11];
  const char* unused = "";
  if (12 + info <= end) descriptors(p + 12, info, out.conditionalAccess, unused);
  size_t i = 12 + info;
  while (i + 5 <= end) {
    Stream s;
    s.type = p[i];
    s.pid = ((p[i + 1] & 0x1F) << 8) | p[i + 2];
    size_t esInfo = ((p[i + 3] & 0x0F) << 8) | p[i + 4];
    if (i + 5 + esInfo <= end) descriptors(p + i + 5, esInfo, out.conditionalAccess, s.privateAudio);
    out.streams.push_back(s);
    i += 5 + esInfo;
  }
  return true;
}

Codec videoCodec(int type) {
  switch (type) {
    case 0x01:
    case 0x02: return Codec::Mpeg2;
    case 0x1B: return Codec::H264;
    case 0x24: return Codec::Hevc;
    case 0x10:  // MPEG-4 part 2
    case 0x1F:  // SVC
    case 0x20:  // MVC
    case 0x42:  // AVS
    case 0xD1:  // Dirac
    case 0xEA:  // VC-1
      return Codec::OtherVideo;
    default: return Codec::None;
  }
}

const char* audioCodec(const Stream& s) {
  switch (s.type) {
    case 0x03:
    case 0x04: return "mp2";
    case 0x0F:
    case 0x11: return "aac";
    case 0x81: return "ac3";
    case 0x87: return "eac3";
    case 0x82:
    case 0x85:
    case 0x86: return "dts";
    case 0x06: return s.privateAudio[0] ? s.privateAudio : nullptr;
    default: return nullptr;
  }
}

}  // namespace marigold::media::ts
