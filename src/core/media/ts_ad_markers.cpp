#include "core/media/ts_ad_markers.hpp"

#include <algorithm>
#include <optional>

namespace marigold::media::scte35 {
namespace {

uint32_t be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

// splice_time() at `i` (moved past it); false when it runs over `end`.
bool spliceTime(const uint8_t* c, size_t end, size_t& i) {
  if (i >= end) return false;
  size_t len = (c[i] & 0x80) ? 5 : 1;  // time_specified_flag: 33 bits of PTS
  if (i + len > end) return false;
  i += len;
  return true;
}

struct Insert {
  bool cancel = false, out = false;
  int64_t duration90 = 0;  // 0: none said
};

// splice_insert() in `c[0..end)`; nullopt when it doesn't fit its own lengths.
std::optional<Insert> spliceInsert(const uint8_t* c, size_t end) {
  Insert in;
  if (end < 5) return std::nullopt;
  in.cancel = (c[4] & 0x80) != 0;  // after the 32-bit splice_event_id
  size_t i = 5;
  if (in.cancel) return in;
  if (i >= end) return std::nullopt;
  uint8_t flags = c[i++];
  in.out = (flags & 0x80) != 0;
  bool program = (flags & 0x40) != 0, duration = (flags & 0x20) != 0, immediate = (flags & 0x10) != 0;
  if (program) {
    if (!immediate && !spliceTime(c, end, i)) return std::nullopt;
  } else {
    if (i >= end) return std::nullopt;
    int components = c[i++];
    for (int k = 0; k < components; ++k) {
      if (i >= end) return std::nullopt;
      ++i;  // component_tag
      if (!immediate && !spliceTime(c, end, i)) return std::nullopt;
    }
  }
  if (duration) {
    if (i + 5 > end) return std::nullopt;
    in.duration90 = (static_cast<int64_t>(c[i] & 0x01) << 32) | be32(c + i + 1);
    i += 5;
  }
  if (i + 4 > end) return std::nullopt;  // unique_program_id, avail_num, avails_expected
  return in;
}

std::string plural(uint64_t n, const char* one, const char* many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

// "splice_insert, out of network, break 120 s" (what the cues said).
std::string detailText(const Tally& t) {
  std::string s = commandsText(t);
  if (t.outOfNetwork) s += ", out of network";
  if (t.longestBreak90 > 0) s += ", break " + std::to_string((t.longestBreak90 + 45000) / 90000) + " s";
  return s;
}

}  // namespace

void Tally::merge(const Tally& o) {
  packets += o.packets;
  tables = tables || o.tables;
  declared = declared || o.declared;
  cues += o.cues;
  spliceNull += o.spliceNull;
  spliceInsert += o.spliceInsert;
  timeSignal += o.timeSignal;
  other += o.other;
  outOfNetwork += o.outOfNetwork;
  longestBreak90 = std::max(longestBreak90, o.longestBreak90);
  skipped += o.skipped;
  videoMs += o.videoMs;
}

Found found(const Tally& t) {
  if (!t.read()) return Found::NotRead;
  if (t.cues) return Found::Cues;
  return t.declared ? Found::Declared : Found::None;
}

// --------------------------------------------------------------- Scanner

Scanner::Scanner() = default;

void Scanner::reset() {
  packets_ = tsclock::Packets();
  pat_.reset();
  pmt_.reset();
  cue_.reset();
  map_.streams.clear();
  pmtPid_ = videoPid_ = cuePid_ = -1;
  cuePidCount_ = 0;
  pmtCrc_ = 0;
  havePmt_ = havePts_ = false;
  lastPts_ = 0;
  t_ = Tally{};
}

Tally Scanner::take() {
  Tally out = t_;
  t_ = Tally{};
  t_.tables = out.tables;
  t_.declared = out.declared;
  return out;
}

void Scanner::feed(const uint8_t* data, size_t n) {
  packets_.feed(data, n, [this](const uint8_t* p) { packet(p); });
}

bool Scanner::listed(int pid) const {
  for (int i = 0; i < cuePidCount_; ++i)
    if (cuePids_[i] == pid) return true;
  return false;
}

void Scanner::packet(const uint8_t* p) {
  ++t_.packets;
  if (p[0] != ts::kSync || (p[1] & 0x80)) return;  // out of step, or a transport error
  int pid = ((p[1] & 0x1F) << 8) | p[2];
  if (pid == ts::kNullPid || (p[3] >> 6) != 0) return;  // padding, or scrambled
  bool start = (p[1] & 0x40) != 0;
  int control = (p[3] >> 4) & 0x03;
  size_t off = 4;
  if (control & 0x02) off = 5 + static_cast<size_t>(p[4]);
  if (!(control & 0x01) || off >= ts::kPacket) return;  // no payload
  const uint8_t* payload = p + off;
  size_t n = ts::kPacket - off;
  size_t size = 0;
  if (pid == 0) {
    if (const uint8_t* s = pat_.feed(payload, n, start, &size)) {
      int pmt = ts::patFirstProgram(s, size);
      if (pmt > 0 && pmt != pmtPid_) {
        pmtPid_ = pmt;
        pmt_.reset();
        havePmt_ = false;
      }
    }
    return;
  }
  if (pid == pmtPid_) {
    if (const uint8_t* s = pmt_.feed(payload, n, start, &size)) readPmt(s, size);
    return;
  }
  if (pid == videoPid_) {
    if (start) clock(payload, n);
    return;
  }
  // Cues: on the streams the PMT lists for them, or on any PID when it lists none.
  if (cuePidCount_ > 0 && !listed(pid)) return;
  if (start) {
    size_t pointer = payload[0];
    if (1 + pointer >= n || payload[1 + pointer] != kTableId) {
      if (pid == cuePid_) {
        cue_.reset();
        cuePid_ = -1;
      }
      return;
    }
    if (pid != cuePid_) cue_.reset();
    cuePid_ = pid;
  } else if (pid != cuePid_) {
    return;
  }
  if (const uint8_t* s = cue_.feed(payload, n, start, &size)) readCue(s, size);
}

void Scanner::readPmt(const uint8_t* s, size_t n) {
  // The same PMT again (it comes several times a second): nothing new.
  uint32_t crc = be32(s + n - 4);
  if (havePmt_ && crc == pmtCrc_) return;
  if (!ts::parsePmt(s, n, map_)) return;
  havePmt_ = true;
  pmtCrc_ = crc;
  t_.tables = true;
  bool declared = map_.cuei;
  int video = -1;
  cuePidCount_ = 0;
  for (const ts::Stream& st : map_.streams) {
    if (st.type == kStreamType || st.cuei) {
      declared = true;
      if (cuePidCount_ < kMaxCuePids) cuePids_[cuePidCount_++] = st.pid;
    } else if (video < 0 && ts::videoCodec(st.type) != ts::Codec::None) {
      video = st.pid;
    }
  }
  if (declared) t_.declared = true;
  if (video != videoPid_) {
    videoPid_ = video;
    havePts_ = false;
  }
  if (cuePid_ >= 0 && cuePidCount_ > 0 && !listed(cuePid_)) {
    cue_.reset();
    cuePid_ = -1;
  }
}

void Scanner::clock(const uint8_t* payload, size_t n) {
  bool video = false;
  std::optional<int64_t> pts = tsclock::pesPts(payload, n, &video);
  if (!pts || !video) return;
  if (!havePts_) {
    havePts_ = true;
    lastPts_ = *pts;
    return;
  }
  int64_t d = tsclock::delta(*pts, lastPts_);
  if (d > kJump90 || d < -kJump90) {
    lastPts_ = *pts;  // a new timeline: nothing counted across the jump
  } else if (d > 0) {
    // The furthest picture so far (B frames come out of order).
    t_.videoMs += static_cast<uint64_t>(d) / 90;
    lastPts_ = *pts;
  }
}

void Scanner::readCue(const uint8_t* s, size_t n) {
  // table_id, lengths, protocol_version, encrypted_packet / pts_adjustment,
  // cw_index, tier / splice_command_length, splice_command_type: 14 bytes;
  // then the command, the descriptor loop's length and the CRC.
  if (n < 14 + 2 + 4 || s[0] != kTableId || s[3] != 0) {
    ++t_.skipped;
    return;
  }
  const size_t body = n - 4 - 14;  // after the header, before the CRC
  const uint8_t* c = s + 14;
  uint8_t type = s[13];
  if (s[4] & 0x80) {
    // Encrypted: its command can't be read.
    ++t_.cues;
    ++t_.other;
    return;
  }
  size_t len = (static_cast<size_t>(s[11] & 0x0F) << 8) | s[12];
  if (len == 0xFFF) {
    len = body;  // legacy: not said
  } else {
    // The command and the descriptor loop, each within the section.
    if (len + 2 > body) {
      ++t_.skipped;
      return;
    }
    size_t loop = (static_cast<size_t>(c[len]) << 8) | c[len + 1];
    if (len + 2 + loop > body) {
      ++t_.skipped;
      return;
    }
  }
  switch (type) {
    case kSpliceNull: ++t_.spliceNull; break;
    case kSpliceInsert: {
      std::optional<Insert> in = spliceInsert(c, len);
      if (!in) {
        ++t_.skipped;
        return;
      }
      ++t_.spliceInsert;
      if (!in->cancel && in->out) ++t_.outOfNetwork;
      t_.longestBreak90 = std::max(t_.longestBreak90, in->duration90);
      break;
    }
    case kTimeSignal: {
      size_t i = 0;
      if (!spliceTime(c, len, i)) {
        ++t_.skipped;
        return;
      }
      ++t_.timeSignal;
      break;
    }
    default: ++t_.other; break;
  }
  ++t_.cues;
}

// ---------------------------------------------------------------- words

std::string commandsText(const Tally& t) {
  struct Kind {
    uint32_t n;
    const char* name;
  };
  const Kind kinds[] = {{t.spliceInsert, "splice_insert"},
                        {t.timeSignal, "time_signal"},
                        {t.spliceNull, "splice_null"},
                        {t.other, "other commands"}};
  int seen = 0;
  for (const Kind& k : kinds) seen += k.n > 0;
  std::string out;
  for (const Kind& k : kinds) {
    if (!k.n) continue;
    if (!out.empty()) out += ", ";
    if (seen > 1) out += std::to_string(k.n) + " ";
    out += k.name;
  }
  return out;
}

std::string durationText(uint64_t ms) {
  if (ms == 0) return {};
  if (ms < 60000) return std::to_string(std::max<uint64_t>(1, (ms + 500) / 1000)) + " s";
  return std::to_string((ms + 30000) / 60000) + " min";
}

std::string readText(const Tally& t) {
  switch (found(t)) {
    case Found::NotRead: return {};
    case Found::None: return t.tables ? "none" : "no tables read";
    case Found::Declared: return "declared, no cue in the first seconds";
    case Found::Cues: break;
  }
  return plural(t.cues, "cue", "cues") + " (" + detailText(t) + ")" + (t.declared ? "" : ", not declared");
}

std::string replayText(const Tally& t) {
  std::string in = durationText(t.videoMs);
  in = in.empty() ? std::string() : " in " + in;
  switch (found(t)) {
    case Found::NotRead: return {};
    case Found::None: return "no ad markers" + in + (t.tables ? "" : " (no tables read)");
    case Found::Declared: return "ad markers declared, none seen" + in;
    case Found::Cues: break;
  }
  return "ad markers seen " + (t.cues == 1 ? std::string("once") : std::to_string(t.cues) + " times") + in + " (" + detailText(t) +
         ")" + (t.declared ? "" : ", not declared");
}

}  // namespace marigold::media::scte35
