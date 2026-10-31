#include "core/media/ts_video_probe.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace marigold::media {
namespace {

using ts::Codec;
using ts::kPacket;
using ts::kSync;

constexpr size_t kLockBytes = 2 * kPacket + 1;  // to see three sync bytes in a row
constexpr size_t kHeaderBytes = 64;             // of each NAL: slice header or SPS
constexpr int64_t kWrap = int64_t{1} << 33;
constexpr int64_t kSecond90 = 90000;

// Bits of an RBSP (emulation prevention bytes removed), past the end reads 0.
class Bits {
 public:
  Bits(const uint8_t* p, size_t n) {
    int zeros = 0;
    for (size_t i = 0; i < n && n_ < sizeof buf_; ++i) {
      if (zeros >= 2 && p[i] == 3) {
        zeros = 0;
        continue;
      }
      buf_[n_++] = p[i];
      zeros = p[i] == 0 ? zeros + 1 : 0;
    }
  }
  uint32_t u(int k) {
    uint32_t v = 0;
    while (k-- > 0) {
      size_t byte = pos_ >> 3;
      v = (v << 1) | (byte < n_ ? (buf_[byte] >> (7 - (pos_ & 7))) & 1u : 0u);
      ++pos_;
    }
    return v;
  }
  uint32_t ue() {
    int zeros = 0;
    while (u(1) == 0) {
      if (++zeros >= 31 || over()) return 0;
    }
    return zeros ? (1u << zeros) - 1 + u(zeros) : 0;
  }
  int32_t se() {
    uint32_t k = ue();
    return (k & 1) ? static_cast<int32_t>((k + 1) / 2) : -static_cast<int32_t>(k / 2);
  }
  bool over() const { return pos_ > n_ * 8; }

 private:
  uint8_t buf_[kHeaderBytes];
  size_t n_ = 0, pos_ = 0;
};

int64_t pts90(const uint8_t* p) {
  return (static_cast<int64_t>(p[0] & 0x0E) << 29) | (static_cast<int64_t>(p[1]) << 22) |
         (static_cast<int64_t>(p[2] & 0xFE) << 14) | (static_cast<int64_t>(p[3]) << 7) | (p[4] >> 1);
}

// b - a on the 33-bit clock, the short way round.
int64_t diff90(int64_t b, int64_t a) {
  int64_t d = ((b - a) % kWrap + kWrap) % kWrap;
  return d > kWrap / 2 ? d - kWrap : d;
}

std::string bytesText(uint64_t n) {
  if (n < 10 * 1024) return std::to_string(n) + " B";
  return std::to_string(n / 1024) + " KB";
}

std::string rateText(double fps) {
  // Measured rates land on the broadcast ones.
  static const double kRates[] = {23.976, 24, 25, 29.97, 30, 47.952, 48, 50, 59.94, 60};
  double nearest = 0;
  for (double r : kRates)
    if (std::fabs(fps - r) < r * 0.004 && (nearest == 0 || std::fabs(fps - r) < std::fabs(fps - nearest))) nearest = r;
  if (nearest > 0) fps = nearest;
  char buf[16];
  if (std::fabs(fps - std::round(fps)) < 0.005) std::snprintf(buf, sizeof buf, "%d", static_cast<int>(std::round(fps)));
  else if (std::fabs(fps - 23.976) < 0.001) std::snprintf(buf, sizeof buf, "23.976");
  else std::snprintf(buf, sizeof buf, "%.2f", fps);
  return buf;
}

const char* h264Profile(int p) {
  switch (p) {
    case 66: return "Baseline";
    case 77: return "Main";
    case 88: return "Extended";
    case 100: return "High";
    case 110: return "High 10";
    case 122: return "High 4:2:2";
    case 244: return "High 4:4:4";
    case 44: return "CAVLC 4:4:4";
    default: return nullptr;
  }
}

const char* hevcProfile(int p) {
  switch (p) {
    case 1: return "Main";
    case 2: return "Main10";
    case 3: return "Main Still";
    case 4: return "RExt";
    default: return nullptr;
  }
}

const char* mpeg2Profile(int pl) {
  if (pl < 0) return nullptr;
  if (pl & 0x80) return (pl & 0x0F) == 0x02 || (pl & 0x0F) == 0x05 ? "4:2:2" : nullptr;
  switch ((pl >> 4) & 7) {
    case 1: return "High";
    case 4: return "Main";
    case 5: return "Simple";
    default: return nullptr;
  }
}

const char* chromaText(int c) {
  switch (c) {
    case 0: return "4:0:0";
    case 2: return "4:2:2";
    case 3: return "4:4:4";
    default: return nullptr;
  }
}

}  // namespace

const char* verdictName(Verdict v) {
  switch (v) {
    case Verdict::Unknown: return "not known";
    case Verdict::Picture: return "picture";
    case Verdict::Unsupported: return "unsupported";
    case Verdict::Scrambled: return "scrambled";
    case Verdict::NoVideo: return "no video track";
    case Verdict::TablesOnly: return "tables only";
    case Verdict::NoFrame: return "no frame drawn";
    case Verdict::Black: return "black";
    case Verdict::Frozen: return "frozen";
    case Verdict::Static: return "static";
  }
  return "";
}

std::string codecName(const VideoFacts& f) {
  std::string out;
  const char* profile = nullptr;
  switch (f.codec) {
    case Codec::H264: out = "h264"; profile = h264Profile(f.profile); break;
    case Codec::Hevc: out = "hevc"; profile = hevcProfile(f.profile); break;
    case Codec::Mpeg2: out = "mpeg2"; profile = mpeg2Profile(f.profile); break;
    case Codec::OtherVideo: return "other video";
    case Codec::None: return {};
  }
  if (!f.header) return out;
  if (profile) out += std::string(" ") + profile;
  const char* chroma = chromaText(f.chroma);
  if (chroma && (!profile || !std::strstr(profile, chroma))) out += std::string(" ") + chroma;
  out += " " + std::to_string(f.bitDepth) + "-bit";
  return out;
}

std::string describe(const VideoFacts& f) {
  std::string out;
  if (f.width > 0 && f.height > 0) {
    out = std::to_string(f.width) + "x" + std::to_string(f.height);
    if (f.frameRate > 0) out += " " + rateText(f.frameRate) + (f.interlaced ? "i" : "p");
  }
  std::string codec = codecName(f);
  if (!codec.empty()) out += (out.empty() ? "" : " \xc2\xb7 ") + codec;
  if (!f.audio.empty()) out += (out.empty() ? "" : " \xc2\xb7 ") + f.audio;
  return out;
}

std::string cannotDecode(Device d, const VideoFacts& f) {
  if (d != Device::LgWebOs || !f.header) return {};
  bool bad = false;
  switch (f.codec) {
    // 8-bit 4:2:0 only: High 10, High 4:2:2 and High 4:4:4 have no decoder.
    case Codec::H264:
      bad = f.bitDepth > 8 || f.chroma > 1 || f.profile == 110 || f.profile == 122 || f.profile == 244 ||
            f.profile == 44;
      break;
    // Main and Main10, 4:2:0.
    case Codec::Hevc: bad = f.bitDepth > 10 || f.chroma > 1; break;
    // Main profile, 4:2:0.
    case Codec::Mpeg2: bad = f.chroma > 1; break;
    default: break;
  }
  return bad ? codecName(f) : std::string();
}

// --- the probe -----------------------------------------------------------------

TsVideoProbe::TsVideoProbe(Device device) : device_(device) { timeline_.reserve(kTimelineMax); }

void TsVideoProbe::reset() { *this = TsVideoProbe(device_); }

void TsVideoProbe::feed(const uint8_t* data, size_t n) {
  while (n > 0) {
    if (carryLen_ > 0) {
      // Finish the packet held over from the last call, then read straight
      // from the caller's buffer again.
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

size_t TsVideoProbe::consume(const uint8_t* p, size_t n) {
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
      // Out of sync, a sync byte counts only when the next two packets' are too.
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

void TsVideoProbe::packet(const uint8_t* p) {
  ++packets_;
  if (p[1] & 0x80) return;  // transport error
  int pid = ((p[1] & 0x1F) << 8) | p[2];
  if (pid == ts::kNullPid) {
    ++nulls_;
    ++quiet_;
    return;
  }
  bool start = (p[1] & 0x40) != 0;
  int scrambling = p[3] >> 6;
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
  bool payload = (control & 0x01) && off < kPacket;
  size_t size = 0;
  if (pid == 0 || pid == pmtPid_ || pid < 0x20) {
    ++quiet_;
    if (!payload || scrambling) return;
    if (pid == 0) {
      if (const uint8_t* s = pat_.feed(p + off, kPacket - off, start, &size)) {
        int pmt = ts::patFirstProgram(s, size);
        if (pmt >= 0 && pmt != pmtPid_) {
          pmtPid_ = pmt;
          pmt_.reset();
        }
      }
    } else if (pid == pmtPid_) {
      if (const uint8_t* s = pmt_.feed(p + off, kPacket - off, start, &size)) readPmt(s, size);
    }
    return;
  }
  if (!payload) {
    ++quiet_;
    return;
  }
  // Sound or video (or a stream the PMT hasn't named yet).
  quiet_ = 0;
  ++esSample_;
  if (scrambling) ++esScrambled_;
  if (esSample_ >= kScrambleSample) {
    scrambledPercent_ = esScrambled_ * 100 / esSample_;
    scrambled_ = scrambledPercent_ >= 90;
    esSample_ = esScrambled_ = 0;
  }
  if (pid == videoPid_) {
    audioSinceVideo_ = 0;
    if (scrambling) return;
    if (lastCc_ >= 0 && !discontinuity) {
      if (cc == lastCc_) return;  // a repeated packet
      if (cc != ((lastCc_ + 1) & 0x0F)) {
        // Lost packets: what was being sized is wrong, start over at the next picture.
        nalOpen_ = false;
        hdrNeed_ = 0;
        picOpen_ = false;
        picBytes_ = 0;
      }
    }
    lastCc_ = cc;
    video(p + off, kPacket - off, start);
    return;
  }
  for (uint32_t a : audioPids_) {
    if (a != 0 && static_cast<int>(a) == pid) {
      if (videoPid_ >= 0 && audioSinceVideo_ < kAudioWithoutVideo) ++audioSinceVideo_;
      break;
    }
  }
}

void TsVideoProbe::readPmt(const uint8_t* p, size_t n) {
  if (!ts::parsePmt(p, n, map_)) return;
  havePmt_ = true;
  conditionalAccess_ = map_.conditionalAccess;
  int video = -1;
  Codec codec = Codec::None;
  videoStreams_ = 0;
  int audio = 0;
  std::string firstAudio;
  std::fill(std::begin(audioPids_), std::end(audioPids_), 0u);
  for (const ts::Stream& s : map_.streams) {
    Codec c = ts::videoCodec(s.type);
    if (c != Codec::None) {
      ++videoStreams_;
      if (video < 0) {
        video = s.pid;
        codec = c;
      }
    } else if (const char* a = ts::audioCodec(s)) {
      if (audio < 8) audioPids_[audio] = static_cast<uint32_t>(s.pid);
      if (firstAudio.empty()) firstAudio = a;
      ++audio;
    }
  }
  facts_.audioStreams = audio;
  facts_.audio = firstAudio;
  if (video != videoPid_ || codec != facts_.codec) {
    // Another video stream: what was learned about the last one is gone.
    videoPid_ = video;
    VideoFacts f;
    f.codec = codec;
    f.audio = facts_.audio;
    f.audioStreams = facts_.audioStreams;
    facts_ = f;
    nalOpen_ = pesOpen_ = picOpen_ = false;
    hdrNeed_ = 0;
    lastCc_ = -1;
    audioSinceVideo_ = 0;
  }
}

// --- PES and the elementary stream ---------------------------------------------

void TsVideoProbe::video(const uint8_t* p, size_t n, bool start) {
  if (facts_.codec == Codec::OtherVideo || facts_.codec == Codec::None) return;
  if (start) {
    endAccessUnit();
    pesOpen_ = false;
    pesSkip_ = 0;
    if (n < 9 || p[0] != 0 || p[1] != 0 || p[2] != 1) return;
    size_t headerEnd = 9 + static_cast<size_t>(p[8]);
    if ((p[7] & 0x80) && n >= 14) {
      int64_t t = ((p[7] & 0xC0) == 0xC0 && n >= 19) ? pts90(p + 14) : pts90(p + 9);  // DTS, else PTS
      clock(t);
      rateTimes_[rateCount_ % 64] = t;
      ++rateCount_;
    }
    pesOpen_ = true;
    // A new PES starts a new run of the stream: no start code spans it.
    prev_[0] = prev_[1] = 0xFF;
    if (headerEnd >= n) {
      pesSkip_ = headerEnd - n;
      return;
    }
    es(p + headerEnd, n - headerEnd);
    return;
  }
  if (!pesOpen_) return;
  if (pesSkip_) {
    size_t k = std::min(pesSkip_, n);
    pesSkip_ -= k;
    p += k;
    n -= k;
    if (n == 0) return;
  }
  es(p, n);
}

void TsVideoProbe::es(const uint8_t* p, size_t n) {
  // The rest of the current NAL's first bytes.
  if (hdrNeed_) {
    size_t k = std::min(hdrNeed_, n);
    std::memcpy(hdr_ + hdrLen_, p, k);
    hdrLen_ += k;
    hdrNeed_ -= k;
    if (!hdrNeed_) header();
  }
  size_t i = 0;
  while (i < n) {
    const void* q = std::memchr(p + i, 1, n - i);
    if (!q) break;
    size_t k = static_cast<size_t>(static_cast<const uint8_t*>(q) - p);
    uint8_t b1 = k >= 1 ? p[k - 1] : prev_[1];
    uint8_t b2 = k >= 2 ? p[k - 2] : (k == 1 ? prev_[1] : prev_[0]);
    if (b1 == 0 && b2 == 0) startCode(esPos_ + k - 2, p + k + 1, n - k - 1);
    i = k + 1;
  }
  if (n >= 2) {
    prev_[0] = p[n - 2];
    prev_[1] = p[n - 1];
  } else {
    prev_[0] = prev_[1];
    prev_[1] = p[0];
  }
  esPos_ += n;
}

void TsVideoProbe::startCode(uint64_t pos, const uint8_t* after, size_t avail) {
  endNal(pos);
  nalOpen_ = true;
  nalStart_ = pos;
  hdrRead_ = false;
  hdrLen_ = std::min(avail, kHeaderBytes);
  std::memcpy(hdr_, after, hdrLen_);
  hdrNeed_ = kHeaderBytes - hdrLen_;
  if (!hdrNeed_) header();
}

void TsVideoProbe::endNal(uint64_t pos) {
  if (!nalOpen_) return;
  nalOpen_ = false;
  if (!hdrRead_) {
    hdrNeed_ = 0;
    header();
  }
  if (hdrLen_ == 0 || pos < nalStart_) return;
  uint64_t size = pos - nalStart_;
  uint8_t h = hdr_[0];
  bool vcl = false, filler = false;
  switch (facts_.codec) {
    case Codec::H264:
      vcl = (h & 0x1F) >= 1 && (h & 0x1F) <= 5;
      filler = (h & 0x1F) == 12;
      break;
    case Codec::Hevc:
      vcl = ((h >> 1) & 0x3F) <= 31;
      filler = ((h >> 1) & 0x3F) == 38;
      break;
    case Codec::Mpeg2: vcl = h <= 0xAF; break;
    default: break;
  }
  if (filler) {
    cur_.fillerBytes += size;
  } else if (vcl && picOpen_) {
    picBytes_ += size;
    cur_.vclBytes += size;
  }
}

// The first bytes of a NAL (or MPEG-2 start code) are in: a picture's start
// and type, or the sequence header.
void TsVideoProbe::header() {
  hdrRead_ = true;
  const uint8_t* h = hdr_;
  size_t n = hdrLen_;
  if (n == 0) return;
  switch (facts_.codec) {
    case Codec::H264: {
      int type = h[0] & 0x1F;
      if ((type == 1 || type == 5) && n > 1) {
        Bits r(h + 1, n - 1);
        uint32_t firstMb = r.ue();
        uint32_t sliceType = r.ue() % 5;
        if (firstMb == 0) newPicture(type == 5 || sliceType == 2 || sliceType == 4);
      } else if (type == 7) {
        sps264(h + 1, n - 1);
      }
      break;
    }
    case Codec::Hevc: {
      int type = (h[0] >> 1) & 0x3F;
      if (type <= 31 && n > 2) {
        if (h[2] & 0x80) newPicture(type >= 16 && type <= 23);  // first slice; IRAP
      } else if (type == 33 && n > 2) {
        sps265(h + 2, n - 2);
      }
      break;
    }
    case Codec::Mpeg2: {
      if (h[0] == 0x00 && n >= 3) {
        newPicture(((h[2] >> 3) & 7) == 1);
      } else if (h[0] == 0xB3 && n >= 4) {
        facts_.width = (h[1] << 4) | (h[2] >> 4);
        facts_.height = ((h[2] & 0x0F) << 8) | h[3];
        facts_.header = true;
      } else if (h[0] == 0xB5 && n >= 3 && (h[1] >> 4) == 1) {
        facts_.profile = ((h[1] & 0x0F) << 4) | (h[2] >> 4);
        facts_.interlaced = !(h[2] & 0x08);  // progressive_sequence
        facts_.chroma = (h[2] >> 1) & 3;
      }
      break;
    }
    default: break;
  }
}

void TsVideoProbe::sps264(const uint8_t* p, size_t n) {
  Bits r(p, n);
  int profile = static_cast<int>(r.u(8));
  r.u(16);  // constraint flags, level
  r.ue();   // seq_parameter_set_id
  int chroma = 1, depth = 8;
  bool separate = false;
  if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 ||
      profile == 86 || profile == 118 || profile == 128 || profile == 138 || profile == 139 || profile == 134 ||
      profile == 135) {
    chroma = static_cast<int>(r.ue());
    if (chroma == 3) separate = r.u(1);
    depth = 8 + static_cast<int>(r.ue());
    r.ue();  // bit_depth_chroma
    r.u(1);  // qpprime_y_zero_transform_bypass
    if (r.u(1)) {
      // Scaling lists: skipped value by value.
      for (int i = 0; i < (chroma != 3 ? 8 : 12); ++i) {
        if (!r.u(1)) continue;
        int size = i < 6 ? 16 : 64, last = 8, next = 8;
        for (int j = 0; j < size && !r.over(); ++j) {
          if (next != 0) next = (last + r.se() + 256) % 256;
          last = next == 0 ? last : next;
        }
      }
    }
  }
  facts_.profile = profile;
  facts_.chroma = chroma;
  facts_.bitDepth = depth;
  facts_.header = true;
  r.ue();  // log2_max_frame_num
  uint32_t poc = r.ue();
  if (poc == 0) {
    r.ue();
  } else if (poc == 1) {
    r.u(1);
    r.se();
    r.se();
    uint32_t cycle = r.ue();
    for (uint32_t i = 0; i < cycle && i < 256 && !r.over(); ++i) r.se();
  }
  r.ue();  // max_num_ref_frames
  r.u(1);
  int wMbs = static_cast<int>(r.ue()) + 1, hMaps = static_cast<int>(r.ue()) + 1;
  bool frameOnly = r.u(1);
  if (!frameOnly) r.u(1);  // mb_adaptive_frame_field
  r.u(1);                  // direct_8x8_inference
  int cropL = 0, cropR = 0, cropT = 0, cropB = 0;
  if (r.u(1)) {
    cropL = static_cast<int>(r.ue());
    cropR = static_cast<int>(r.ue());
    cropT = static_cast<int>(r.ue());
    cropB = static_cast<int>(r.ue());
  }
  if (r.over()) return;  // past what was kept of it: no size
  int unitX = (chroma == 0 || separate || chroma == 3) ? 1 : 2;
  int unitY = (chroma == 1 ? 2 : 1) * (frameOnly ? 1 : 2);
  int w = wMbs * 16 - unitX * (cropL + cropR);
  int h = hMaps * 16 * (frameOnly ? 1 : 2) - unitY * (cropT + cropB);
  if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return;
  facts_.width = w;
  facts_.height = h;
  facts_.interlaced = !frameOnly;
}

void TsVideoProbe::sps265(const uint8_t* p, size_t n) {
  Bits r(p, n);
  r.u(4);  // sps_video_parameter_set_id
  int subLayers = static_cast<int>(r.u(3));
  r.u(1);
  r.u(3);  // general_profile_space, tier
  int profile = static_cast<int>(r.u(5));
  r.u(32);  // compatibility flags
  r.u(1);   // progressive_source
  bool interlacedSource = r.u(1);
  r.u(2);
  r.u(32);
  r.u(12);
  r.u(8);  // general_level_idc
  bool profilePresent[8] = {}, levelPresent[8] = {};
  for (int i = 0; i < subLayers; ++i) {
    profilePresent[i] = r.u(1);
    levelPresent[i] = r.u(1);
  }
  if (subLayers > 0)
    for (int i = subLayers; i < 8; ++i) r.u(2);
  for (int i = 0; i < subLayers; ++i) {
    if (profilePresent[i]) {
      r.u(32);
      r.u(32);
      r.u(24);
    }
    if (levelPresent[i]) r.u(8);
  }
  r.ue();  // sps_seq_parameter_set_id
  int chroma = static_cast<int>(r.ue());
  if (chroma == 3) r.u(1);
  int w = static_cast<int>(r.ue()), h = static_cast<int>(r.ue());
  if (r.u(1)) {
    int subW = chroma == 1 || chroma == 2 ? 2 : 1, subH = chroma == 1 ? 2 : 1;
    int l = static_cast<int>(r.ue()), rr = static_cast<int>(r.ue());
    int t = static_cast<int>(r.ue()), b = static_cast<int>(r.ue());
    w -= subW * (l + rr);
    h -= subH * (t + b);
  }
  int depth = 8 + static_cast<int>(r.ue());
  if (r.over()) return;
  facts_.profile = profile;
  facts_.chroma = chroma;
  facts_.bitDepth = depth;
  facts_.interlaced = interlacedSource;
  facts_.header = true;
  if (w > 0 && h > 0 && w <= 8192 && h <= 8192) {
    facts_.width = w;
    facts_.height = h;
  }
}

void TsVideoProbe::newPicture(bool intra) {
  endPicture();
  picOpen_ = true;
  picIntra_ = intra;
  picBytes_ = 0;
}

void TsVideoProbe::endPicture() {
  if (!picOpen_) return;
  picOpen_ = false;
  ++cur_.pictures;
  if (picIntra_) {
    ++cur_.intra;
    lastIntra_ = picBytes_;
    lastIntraWindow_ = windows_;
  } else {
    cur_.interBytes += picBytes_;
  }
  picBytes_ = 0;
}

// A PES ends with its access unit: its last NAL and picture are complete.
void TsVideoProbe::endAccessUnit() {
  endNal(esPos_);
  hdrNeed_ = 0;
  endPicture();
}

// --- windows -------------------------------------------------------------------

void TsVideoProbe::clock(int64_t t) {
  if (!haveStart_) {
    haveStart_ = true;
    start90_ = t;
    return;
  }
  int64_t d = diff90(t, start90_);
  if (d < -5 * kSecond90 || d > 10 * kSecond90) {
    // A jump in the clock (a splice, a new stream): start counting again.
    start90_ = t;
    return;
  }
  if (d >= kSecond90) {
    closeWindow();
    start90_ = (start90_ + (d / kSecond90) * kSecond90) % kWrap;
  }
}

double TsVideoProbe::macroblocks() const {
  if (facts_.width > 0 && facts_.height > 0)
    return static_cast<double>(((facts_.width + 15) / 16) * ((facts_.height + 15) / 16));
  return 8160.0;  // 1080p
}

void TsVideoProbe::closeWindow() {
  Window w = cur_;
  cur_ = Window{};
  ++windows_;
  Class c = Class::None;
  int inter = w.pictures - w.intra;
  lastInterMean_ = inter > 0 ? w.interBytes / static_cast<uint64_t>(inter) : 0;
  lastKbps_ = static_cast<int>(w.vclBytes * 8 / 1000);
  if (w.pictures > 0) {
    double mbs = macroblocks();
    bool intraKnown = lastIntraWindow_ >= windows_ - 1 - kIntraMemory;
    double intraPerMb = intraKnown ? static_cast<double>(lastIntra_) / mbs : -1;
    double interPerMb = inter > 0 ? static_cast<double>(lastInterMean_) / mbs : -1;
    // MPEG-2 can't make a tiny I frame: its black is "static".
    bool flatIntra = facts_.codec != Codec::Mpeg2 && intraPerMb >= 0 && intraPerMb < kBlackIntraPerMb;
    double still = facts_.codec == Codec::Mpeg2 ? kStaticInterPerMbMpeg2 : kStaticInterPerMb;
    if (flatIntra && interPerMb < kBlackInterPerMb) c = Class::Black;
    else if (interPerMb >= 0 && interPerMb < still) c = Class::Static;
    else c = Class::Picture;
  }
  last_ = c;
  if (c == Class::Picture) sawPicture_ = true;
  if (timeline_.size() >= kTimelineMax) timeline_.erase(0, timeline_.size() - kTimelineMax + 1);
  timeline_ += c == Class::Picture ? '.' : c == Class::Black ? 'B' : c == Class::Static ? 's' : '?';
  // The frame rate from the clock of the last access units.
  int k = std::min(rateCount_, 64);
  if (k >= 8) {
    int64_t first = rateTimes_[(rateCount_ - k) % 64], last = rateTimes_[(rateCount_ - 1) % 64];
    int64_t span = diff90(last, first);
    if (span > 0) facts_.frameRate = (k - 1) * 90000.0 / static_cast<double>(span);
  }
}

Verdict TsVideoProbe::verdict() const {
  if (scrambled_) return Verdict::Scrambled;
  if (quiet_ >= kQuietPackets) return Verdict::TablesOnly;
  if (havePmt_ && videoPid_ < 0) return facts_.audioStreams > 0 ? Verdict::NoVideo : Verdict::TablesOnly;
  if (videoPid_ >= 0 && audioSinceVideo_ >= kAudioWithoutVideo) return Verdict::NoVideo;
  if (!cannotDecode(device_, facts_).empty()) return Verdict::Unsupported;
  switch (last_) {
    case Class::Picture: return Verdict::Picture;
    case Class::Black: return Verdict::Black;
    case Class::Static: return Verdict::Static;
    case Class::None: break;
  }
  return Verdict::Unknown;
}

std::string TsVideoProbe::detail() const {
  switch (verdict()) {
    case Verdict::Scrambled:
      return std::to_string(scrambledPercent_) + "% of packets scrambled" +
             (conditionalAccess_ ? std::string(", CA descriptor") : std::string());
    case Verdict::TablesOnly: {
      uint64_t share = packets_ ? nulls_ * 100 / packets_ : 0;
      return "tables and padding only (null " + std::to_string(share) + "%)";
    }
    case Verdict::NoVideo:
      if (videoPid_ < 0) return "PMT audio " + std::to_string(facts_.audioStreams);
      return "PMT video " + std::to_string(videoStreams_) + ", audio " + std::to_string(facts_.audioStreams) +
             "; no video packets";
    case Verdict::Unsupported: return codecName(facts_);
    case Verdict::Black: return "I " + bytesText(lastIntra_) + ", " + std::to_string(lastKbps_) + " kbps";
    case Verdict::Static:
      return "P " + bytesText(lastInterMean_) + ", I " + bytesText(lastIntra_) + ", " + std::to_string(lastKbps_) +
             " kbps";
    case Verdict::Picture: return describe(facts_);
    default: return {};
  }
}

// --- the shared one ------------------------------------------------------------

void StreamCheck::begin(unsigned session) {
  std::lock_guard<std::mutex> lock(mu_);
  session_ = session;
  probe_.reset();
}

void StreamCheck::bytes(const uint8_t* data, size_t n, unsigned session) {
  std::lock_guard<std::mutex> lock(mu_);
  if (session == 0 || session != session_) return;
  probe_.feed(data, n);
}

StreamCheck::Snapshot StreamCheck::snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  Snapshot s;
  s.active = session_ != 0;
  if (!s.active) return s;
  s.verdict = probe_.verdict();
  s.detail = probe_.detail();
  s.facts = probe_.facts();
  s.packets = probe_.packets();
  return s;
}

}  // namespace marigold::media
