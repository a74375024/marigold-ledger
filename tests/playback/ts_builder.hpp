// Synthetic MPEG-TS for the picture check tests: the tables, a video stream
// whose pictures have the sizes and types a real encoder gives a scene (a
// normal one, black, a slate...), sound, padding, scrambling. The streams of
// the black-screen analysis are rebuilt from their measured numbers here, so
// no media file is kept in the repository.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tsgen {

using Bytes = std::vector<uint8_t>;

constexpr int kPmtPid = 0x1000;
constexpr int kVideoPid = 0x100;
constexpr int kAudioPid = 0x101;

inline void add(Bytes& to, const Bytes& more) { to.insert(to.end(), more.begin(), more.end()); }

// Exp-Golomb bits, as an encoder writes a sequence header.
class Bits {
 public:
  void u(int n, uint32_t v) {
    for (int i = n - 1; i >= 0; --i) bit((v >> i) & 1u);
  }
  void ue(uint32_t v) {
    uint32_t x = v + 1;
    int len = 0;
    while ((x >> len) > 1) ++len;
    u(len, 0);
    u(len + 1, x);
  }
  // rbsp_trailing_bits, then the bytes.
  Bytes done() {
    bit(1);
    while (n_ % 8) bit(0);
    return out_;
  }

 private:
  void bit(uint32_t b) {
    if (n_ % 8 == 0) out_.push_back(0);
    if (b) out_.back() |= static_cast<uint8_t>(0x80 >> (n_ % 8));
    ++n_;
  }
  Bytes out_;
  size_t n_ = 0;
};

// 00 00 03 wherever an encoder must put it.
inline Bytes escape(const Bytes& rbsp) {
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

// Slice data: no start codes, no zero bytes.
inline Bytes payload(size_t n, uint8_t seed = 0) {
  Bytes b(n);
  for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(0x40 + (i * 37 + seed) % 180);
  return b;
}

enum class Codec { H264, Hevc, Mpeg2 };

struct VideoFormat {
  Codec codec = Codec::H264;
  int width = 1920, height = 1080;
  int profile = 100;  // H.264 profile_idc / HEVC general_profile_idc
  int chroma = 1;
  int bitDepth = 8;
  bool interlaced = false;
};

// H.264 SPS NAL (with its start code), 1080 cropped from 1088.
inline Bytes h264Sps(const VideoFormat& f) {
  Bits b;
  b.u(8, static_cast<uint32_t>(f.profile));
  b.u(8, 0);
  b.u(8, 40);  // level 4.0
  b.ue(0);
  if (f.profile >= 100) {
    b.ue(static_cast<uint32_t>(f.chroma));
    b.ue(static_cast<uint32_t>(f.bitDepth - 8));
    b.ue(static_cast<uint32_t>(f.bitDepth - 8));
    b.u(1, 0);
    b.u(1, 0);  // no scaling matrices
  }
  b.ue(0);  // log2_max_frame_num - 4
  b.ue(0);  // poc type 0
  b.ue(2);
  b.ue(1);  // max_num_ref_frames
  b.u(1, 0);
  int mbsW = (f.width + 15) / 16;
  int frameMbs = f.interlaced ? 0 : 1;
  int mapUnitsH = ((f.height + 15) / 16) / (f.interlaced ? 2 : 1);
  if (f.interlaced) mapUnitsH = (f.height + 31) / 32;
  b.ue(static_cast<uint32_t>(mbsW - 1));
  b.ue(static_cast<uint32_t>(mapUnitsH - 1));
  b.u(1, static_cast<uint32_t>(frameMbs));
  if (!frameMbs) b.u(1, 0);
  b.u(1, 1);  // direct_8x8_inference
  int codedH = mapUnitsH * 16 * (f.interlaced ? 2 : 1);
  int cropUnitY = (f.chroma == 1 ? 2 : 1) * (f.interlaced ? 2 : 1);
  int cropB = (codedH - f.height) / cropUnitY;
  int cropR = (mbsW * 16 - f.width) / 2;
  b.u(1, cropB || cropR ? 1 : 0);
  if (cropB || cropR) {
    b.ue(0);
    b.ue(static_cast<uint32_t>(cropR));
    b.ue(0);
    b.ue(static_cast<uint32_t>(cropB));
  }
  b.u(1, 0);  // no VUI
  Bytes nal = {0, 0, 0, 1, 0x67};
  add(nal, escape(b.done()));
  return nal;
}

// HEVC SPS NAL (one layer).
inline Bytes hevcSps(const VideoFormat& f) {
  Bits b;
  b.u(4, 0);
  b.u(3, 0);  // max_sub_layers - 1
  b.u(1, 1);
  b.u(2, 0);
  b.u(1, 0);
  b.u(5, static_cast<uint32_t>(f.profile));
  b.u(32, 1u << (31 - f.profile));
  b.u(1, 1);  // progressive
  b.u(1, f.interlaced ? 1 : 0);
  b.u(2, 0);
  b.u(32, 0);
  b.u(12, 0);
  b.u(8, 120);  // level 4
  b.ue(0);
  b.ue(static_cast<uint32_t>(f.chroma));
  int w = (f.width + 7) / 8 * 8, h = (f.height + 7) / 8 * 8;
  b.ue(static_cast<uint32_t>(w));
  b.ue(static_cast<uint32_t>(h));
  bool crop = w != f.width || h != f.height;
  b.u(1, crop ? 1 : 0);
  if (crop) {
    b.ue(0);
    b.ue(static_cast<uint32_t>((w - f.width) / 2));
    b.ue(0);
    b.ue(static_cast<uint32_t>((h - f.height) / 2));
  }
  b.ue(static_cast<uint32_t>(f.bitDepth - 8));
  b.ue(static_cast<uint32_t>(f.bitDepth - 8));
  Bytes nal = {0, 0, 0, 1, 0x42, 0x01};
  add(nal, escape(b.done()));
  return nal;
}

// MPEG-2 sequence header and sequence extension.
inline Bytes mpeg2Sequence(const VideoFormat& f) {
  Bytes s = {0, 0, 1, 0xB3, static_cast<uint8_t>(f.width >> 4),
             static_cast<uint8_t>(((f.width & 0x0F) << 4) | (f.height >> 8)), static_cast<uint8_t>(f.height & 0xFF),
             0x34, 0xFF, 0xFF, 0xE0, 0x18};
  // extension id 1, profile_and_level 0x44 (Main@High), progressive_sequence, chroma.
  uint8_t prog = f.interlaced ? 0 : 1;
  add(s, {0, 0, 1, 0xB5, static_cast<uint8_t>(0x10 | 0x04),
          static_cast<uint8_t>(0x40 | (prog << 3) | (f.chroma << 1)), 0x00, 0x01, 0x00, 0x00});
  return s;
}

// One access unit of `bytes` slice data (I or not), with an optional filler
// NAL of `filler` bytes (H.264 / HEVC: what a CBR encoder pads with).
inline Bytes picture(const VideoFormat& f, bool intra, size_t bytes, size_t filler = 0, uint8_t seed = 0) {
  Bytes au;
  switch (f.codec) {
    case Codec::H264: {
      add(au, {0, 0, 0, 1, 0x09, 0xF0});
      if (intra) {
        add(au, h264Sps(f));
        add(au, {0, 0, 0, 1, 0x68, 0xCE, 0x3C, 0x80});
      }
      // first_mb_in_slice 0 ('1'), slice_type 7 (I) or 5 (P): ue codes.
      Bits b;
      b.ue(0);
      b.ue(intra ? 7 : 5);
      b.ue(0);
      Bytes head = b.done();
      add(au, {0, 0, 1, static_cast<uint8_t>(intra ? 0x65 : 0x41)});
      add(au, head);
      add(au, payload(bytes > head.size() + 4 ? bytes - head.size() - 4 : 1, seed));
      if (filler) {
        add(au, {0, 0, 1, 0x0C});
        au.insert(au.end(), filler, 0xFF);
        au.push_back(0x80);
      }
      break;
    }
    case Codec::Hevc: {
      add(au, {0, 0, 0, 1, 0x46, 0x01, 0x50});
      if (intra) add(au, hevcSps(f));
      // IDR_W_RADL (19) or TRAIL_R (1); first_slice_segment_in_pic_flag.
      add(au, {0, 0, 1, static_cast<uint8_t>((intra ? 19 : 1) << 1), 0x01, 0xAF});
      add(au, payload(bytes > 6 ? bytes - 6 : 1, seed));
      if (filler) {
        add(au, {0, 0, 1, static_cast<uint8_t>(38 << 1), 0x01});
        au.insert(au.end(), filler, 0xFF);
        au.push_back(0x80);
      }
      break;
    }
    case Codec::Mpeg2: {
      if (intra) add(au, mpeg2Sequence(f));
      // picture header: temporal reference, picture_coding_type 1 (I) or 2 (P).
      uint8_t type = intra ? 1 : 2;
      add(au, {0, 0, 1, 0x00, 0x00, static_cast<uint8_t>(type << 3), 0xFF, 0xF8});
      add(au, {0, 0, 1, 0x01});
      add(au, payload(bytes > 12 ? bytes - 12 : 1, seed));
      break;
    }
  }
  return au;
}

inline uint32_t crc32(const Bytes& b) {
  uint32_t crc = 0xFFFFFFFFu;
  for (uint8_t c : b) {
    crc ^= static_cast<uint32_t>(c) << 24;
    for (int i = 0; i < 8; ++i) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
  }
  return crc;
}

inline void withCrc(Bytes& section) {
  uint32_t crc = crc32(section);
  for (int s = 24; s >= 0; s -= 8) section.push_back(static_cast<uint8_t>(crc >> s));
}

inline Bytes pat() {
  Bytes s = {0x00, 0xB0, 0x0D, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x00, 0x01,
             static_cast<uint8_t>(0xE0 | (kPmtPid >> 8)), static_cast<uint8_t>(kPmtPid & 0xFF)};
  withCrc(s);
  return s;
}

// `videoType` 0: no video stream; `audioType` 0: no sound.
inline Bytes pmt(int videoType, int audioType, bool ca = false) {
  Bytes body;
  if (videoType) add(body, {static_cast<uint8_t>(videoType), 0xE0 | (kVideoPid >> 8), kVideoPid & 0xFF, 0xF0, 0x00});
  if (audioType) add(body, {static_cast<uint8_t>(audioType), 0xE0 | (kAudioPid >> 8), kAudioPid & 0xFF, 0xF0, 0x00});
  Bytes programInfo;
  if (ca) programInfo = {0x09, 0x04, 0x0B, 0x00, 0xE7, 0xD0};
  size_t len = 9 + programInfo.size() + body.size() + 4;
  Bytes s = {0x02, static_cast<uint8_t>(0xB0 | (len >> 8)), static_cast<uint8_t>(len & 0xFF), 0x00, 0x01, 0xC1, 0x00,
             0x00, static_cast<uint8_t>(0xE0 | (kVideoPid >> 8)), kVideoPid & 0xFF,
             static_cast<uint8_t>(0xF0 | (programInfo.size() >> 8)), static_cast<uint8_t>(programInfo.size() & 0xFF)};
  add(s, programInfo);
  add(s, body);
  withCrc(s);
  return s;
}

inline Bytes ts90(uint8_t marker, int64_t t) {
  uint64_t p = static_cast<uint64_t>(t) & ((uint64_t{1} << 33) - 1);
  return {static_cast<uint8_t>(marker | ((p >> 29) & 0x0E) | 1), static_cast<uint8_t>(p >> 22),
          static_cast<uint8_t>(((p >> 14) & 0xFE) | 1), static_cast<uint8_t>(p >> 7),
          static_cast<uint8_t>(((p << 1) & 0xFE) | 1)};
}

// A video PES with PTS and DTS.
inline Bytes videoPes(int64_t pts, int64_t dts, const Bytes& es) {
  Bytes b = {0, 0, 1, 0xE0, 0, 0, 0x80, 0xC0, 0x0A};
  add(b, ts90(0x30, pts));
  add(b, ts90(0x10, dts));
  add(b, es);
  return b;
}

inline Bytes audioPes(int64_t pts, size_t n) {
  Bytes b = {0, 0, 1, 0xC0, 0, 0, 0x80, 0x80, 0x05};
  add(b, ts90(0x20, pts));
  add(b, payload(n, 7));
  return b;
}

// TS packets for one PID, continuity counters kept by the caller.
class Muxer {
 public:
  Bytes out;
  int scrambling = 0;  // transport_scrambling_control for sound and video

  void section(int pid, const Bytes& s) { packets(pid, Bytes{0x00}, s, false); }
  void pes(int pid, const Bytes& p) { packets(pid, {}, p, true); }
  void null(size_t count) {
    for (size_t i = 0; i < count; ++i) {
      Bytes k(188, 0xFF);
      k[0] = 0x47;
      k[1] = 0x1F;
      k[2] = 0xFF;
      k[3] = 0x10;
      add(out, k);
    }
  }

 private:
  void packets(int pid, const Bytes& prefix, const Bytes& data, bool es) {
    Bytes all = prefix;
    add(all, data);
    size_t off = 0;
    bool first = true;
    while (off < all.size()) {
      size_t left = all.size() - off;
      Bytes k = {0x47, static_cast<uint8_t>((first ? 0x40 : 0) | (pid >> 8)), static_cast<uint8_t>(pid & 0xFF)};
      int sc = es ? scrambling : 0;
      uint8_t cc = static_cast<uint8_t>(cc_[pid & 0x1FFF]++ & 0x0F);
      if (left >= 184) {
        k.push_back(static_cast<uint8_t>((sc << 6) | 0x10 | cc));
        k.insert(k.end(), all.begin() + static_cast<long>(off), all.begin() + static_cast<long>(off + 184));
        off += 184;
      } else {
        // Stuffing in the adaptation field (sections: 0xFF after the section).
        if (!es) {
          k.push_back(static_cast<uint8_t>(0x10 | cc));
          k.insert(k.end(), all.begin() + static_cast<long>(off), all.end());
          k.resize(188, 0xFF);
        } else {
          size_t af = 183 - left;
          k.push_back(static_cast<uint8_t>((sc << 6) | 0x30 | cc));
          k.push_back(static_cast<uint8_t>(af));
          if (af > 0) {
            k.push_back(0x00);
            k.insert(k.end(), af - 1, 0xFF);
          }
          k.insert(k.end(), all.begin() + static_cast<long>(off), all.end());
        }
        off = all.size();
      }
      first = false;
      add(out, k);
    }
  }
  uint8_t cc_[8192] = {};
};

// A scene: how big each picture is. `second` counts from 0; GOPs start on
// even seconds (2 s GOP, as in the analysis streams).
struct Scene {
  VideoFormat format;
  double fps = 25;
  int seconds = 12;
  bool video = true, audio = true;
  int videoType = 0x1B, audioType = 0x0F;
  bool scrambled = false;
  bool ca = false;
  bool tablesOnly = false;  // PAT, PMT and padding, nothing else
  int gopSeconds = 2;
  // Bytes of a picture: (second, intra) -> slice bytes.
  std::function<size_t(int, bool)> size = [](int, bool intra) { return intra ? 47000u : 20000u; };
  // Constant bit rate padding (bits a second): filler NAL units and null packets.
  int cbrKbps = 0;
};

inline Bytes build(const Scene& s) {
  Muxer m;
  m.scrambling = s.scrambled ? 2 : 0;
  int frames = static_cast<int>(s.fps * s.seconds + 0.5);
  double tick = 90000.0 / s.fps;
  int gop = static_cast<int>(s.fps * s.gopSeconds + 0.5);
  int audioPer = 0;
  int videoType = s.format.codec == Codec::H264 ? 0x1B : s.format.codec == Codec::Hevc ? 0x24 : 0x02;
  if (s.videoType != 0x1B) videoType = s.videoType;
  for (int i = 0; i < frames; ++i) {
    if (i % 10 == 0) {
      m.section(0, pat());
      m.section(kPmtPid, pmt(s.video ? videoType : 0, s.audio ? s.audioType : 0, s.ca));
    }
    int second = static_cast<int>(i / s.fps);
    int64_t dts = 900000 + static_cast<int64_t>(i * tick);
    if (s.tablesOnly) {
      m.null(40);
      continue;
    }
    size_t before = m.out.size();
    if (s.video) {
      bool intra = i % gop == 0;
      size_t bytes = s.size(second, intra);
      size_t filler = 0;
      if (s.cbrKbps) {
        size_t target = static_cast<size_t>(s.cbrKbps * 1000 / 8 / s.fps);
        filler = target > bytes + 64 ? target - bytes - 64 : 0;
      }
      m.pes(kVideoPid, videoPes(dts + 3600, dts, picture(s.format, intra, bytes, filler, static_cast<uint8_t>(i))));
    }
    // Sound: one PES of ~1.2 KB about every 40 ms.
    audioPer += 1;
    if (s.audio && audioPer * 25 >= s.fps) {
      audioPer = 0;
      m.pes(kAudioPid, audioPes(dts, 1200));
    }
    if (s.cbrKbps) {
      // Null packets bring every frame to its share of the rate.
      size_t share = static_cast<size_t>(s.cbrKbps * 1000 / 8 / s.fps) * 115 / 100;
      size_t used = m.out.size() - before;
      if (used < share) m.null((share - used) / 188);
    }
  }
  return m.out;
}

}  // namespace tsgen
