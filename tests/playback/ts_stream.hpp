// MPEG-TS with captions, built as an encoder writes it, for the caption
// tests: PAT and PMT, H.264 / HEVC / MPEG-2 pictures carrying CEA-608 cc_data,
// PES with PTS (and DTS).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace tsgen {

using Bytes = std::vector<uint8_t>;

inline constexpr int kPmtPid = 0x1000;
inline constexpr int kVideoPid = 0x100;
inline constexpr int64_t kWrap = int64_t{1} << 33;

inline void add(Bytes& to, const Bytes& more) { to.insert(to.end(), more.begin(), more.end()); }

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

// Adds 00 00 03 wherever an encoder must.
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

struct Triplet {
  uint8_t head, b1, b2;
};

// A 608 pair on field 1 (or 2), as an encoder writes it.
inline Triplet cc(uint8_t b1, uint8_t b2, int field = 0) {
  return Triplet{static_cast<uint8_t>(0xFC | field), b1, b2};
}

inline Bytes ccData(const std::vector<Triplet>& triplets) {
  Bytes d = {static_cast<uint8_t>(0x40 | triplets.size()), 0xFF};
  for (const Triplet& t : triplets) add(d, {t.head, t.b1, t.b2});
  d.push_back(0xFF);  // marker_bits
  return d;
}

// SEI messages: an optional unrelated one that needs escaping, then GA94.
inline Bytes seiRbsp(const std::vector<Triplet>& triplets, bool tricky) {
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
inline Bytes sliceData(size_t n) {
  Bytes b(n);
  for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(0x40 + (i * 37) % 180);
  return b;
}

// H.264 access unit: AUD, an optional filler (to move the SEI across packet
// boundaries), SEI, a slice (IDR unless `head` says otherwise).
inline Bytes h264(const std::vector<Triplet>& triplets, bool tricky = false, size_t filler = 0, size_t slice = 1500,
                  Bytes head = {0x65, 0x88, 0x84}) {
  Bytes au = {0, 0, 0, 1, 0x09, 0xF0};
  if (filler) {
    add(au, {0, 0, 1, 0x0C});
    au.insert(au.end(), filler, 0xFF);
    au.push_back(0x80);
  }
  add(au, {0, 0, 1, 0x06});
  add(au, escape(seiRbsp(triplets, tricky)));
  add(au, {0, 0, 1});
  add(au, head);
  add(au, sliceData(slice));
  return au;
}

// HEVC access unit: AUD, prefix SEI, a slice (IDR_W_RADL unless `nalType`
// says otherwise).
inline Bytes hevc(const std::vector<Triplet>& triplets, int nalType = 19) {
  Bytes au = {0, 0, 0, 1, 0x46, 0x01, 0x50};
  add(au, {0, 0, 1, 0x4E, 0x01});
  add(au, escape(seiRbsp(triplets, true)));
  add(au, {0, 0, 1, static_cast<uint8_t>(nalType << 1), 0x01, 0xAF});
  add(au, sliceData(900));
  return au;
}

// MPEG-2 picture: picture header, coding extension, GA94 user data, a slice.
inline Bytes mpeg2(const std::vector<Triplet>& triplets) {
  Bytes pic = {0, 0, 1, 0x00, 0x00, 0x0F, 0xFF, 0xF8};
  add(pic, {0, 0, 1, 0xB5, 0x8F, 0xFF, 0xF3, 0x41, 0x80});
  add(pic, {0, 0, 1, 0xB2, 'G', 'A', '9', '4', 0x03});
  add(pic, ccData(triplets));
  add(pic, {0, 0, 1, 0x01});
  add(pic, sliceData(700));
  return pic;
}

// A 33-bit timestamp as the PES header carries it, after a 4-bit prefix.
inline Bytes stamp(int prefix, int64_t t90) {
  uint64_t p = static_cast<uint64_t>(t90) & (static_cast<uint64_t>(kWrap) - 1);
  return {static_cast<uint8_t>((prefix << 4) | ((p >> 29) & 0x0E) | 1), static_cast<uint8_t>(p >> 22),
          static_cast<uint8_t>(((p >> 14) & 0xFE) | 1), static_cast<uint8_t>(p >> 7),
          static_cast<uint8_t>(((p << 1) & 0xFE) | 1)};
}

// A video PES: PTS, and DTS when given (>= 0).
inline Bytes pes(int64_t pts90, const Bytes& es, int64_t dts90 = -1) {
  Bytes b = {0, 0, 1, 0xE0, 0, 0, 0x80, static_cast<uint8_t>(dts90 >= 0 ? 0xC0 : 0x80),
             static_cast<uint8_t>(dts90 >= 0 ? 10 : 5)};
  add(b, stamp(dts90 >= 0 ? 3 : 2, pts90));
  if (dts90 >= 0) add(b, stamp(1, dts90));
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

  void video(int64_t pts90, const Bytes& es, int64_t dts90 = -1) { packets(kVideoPid, pes(pts90, es, dts90), false); }
};

}  // namespace tsgen
