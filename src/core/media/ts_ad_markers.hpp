// Build 177: do the providers' streams carry SCTE-35 ad markers? A quiet
// diagnostic: what it finds goes in the playback log, nothing on screen,
// and nothing is skipped by it.
//
// SCTE-35 tells where a broadcast's commercial breaks are: a PMT lists a
// stream for it (stream_type 0x86, or a registration descriptor "CUEI" on
// the program or a stream), whose packets carry splice_info_sections
// (table_id 0xFC): splice_insert (0x05: out of the network to a break, with
// its duration where it says one, or back), time_signal (0x06: a moment,
// its meaning in descriptors), splice_null (0x00: a heartbeat). With them a
// replay could skip a break exactly; without, it can't.
//
// The Scanner reads a stream's bytes as they pass (any chunking) for:
//   - declared: whether a PMT lists SCTE-35;
//   - cues: the splice_info_sections on the streams it lists, or on any
//     PID when it lists none (a PES never starts with table_id 0xFC), with
//     their command, a splice_insert's out_of_network_indicator and its
//     break_duration. Sections come whole and CRC-checked (ts::
//     SectionReader); one that doesn't parse within its own lengths is
//     skipped and counted;
//   - the video read, by its clock (its first stream's PTS, jumps left
//     out), for "in 41 min".
// Constant memory: three section buffers of ts::kMaxSection reserved once,
// the program map's few streams, fixed PID slots. O(1) a packet: a header,
// a few compares; tables and cues only on their PIDs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/media/ts_start.hpp"
#include "core/media/ts_tables.hpp"

namespace marigold::media::scte35 {

inline constexpr int kStreamType = 0x86;
inline constexpr uint8_t kTableId = 0xFC;
inline constexpr int kMaxCuePids = 8;           // SCTE-35 streams a PMT's listing is kept for
inline constexpr int64_t kJump90 = 10 * 90000;  // a PTS this far off is a new timeline

enum Command : uint8_t { kSpliceNull = 0x00, kSpliceInsert = 0x05, kTimeSignal = 0x06 };

// What a stream said so far (or several, merged).
struct Tally {
  uint64_t packets = 0;    // TS packets read
  bool tables = false;     // a PMT was read
  bool declared = false;   // a PMT listed SCTE-35
  uint32_t cues = 0;       // splice_info_sections read (whole, CRC intact, parsed)
  uint32_t spliceNull = 0, spliceInsert = 0, timeSignal = 0, other = 0;  // ... by command
  uint32_t outOfNetwork = 0;   // splice_inserts going out to a break
  int64_t longestBreak90 = 0;  // the longest break_duration said (90 kHz), 0 none
  uint32_t skipped = 0;        // sections with an intact CRC that didn't parse
  uint64_t videoMs = 0;        // video read, by its clock

  bool read() const { return packets > 0; }
  void merge(const Tally& o);
};

// What a read found, in order: nothing read, no marker, declared without a
// cue, cues.
enum class Found : uint8_t { NotRead, None, Declared, Cues };
Found found(const Tally& t);

class Scanner {
 public:
  Scanner();
  // Any chunking; resyncs on 0x47.
  void feed(const uint8_t* data, size_t n);
  // One whole packet (188 bytes from its 0x47).
  void packet(const uint8_t* p);
  const Tally& tally() const { return t_; }
  // The counts so far, handed over and cleared; what the tables said
  // (tables, declared) stays.
  Tally take();
  // A new stream.
  void reset();

 private:
  void readPmt(const uint8_t* s, size_t n);
  void readCue(const uint8_t* s, size_t n);
  void clock(const uint8_t* payload, size_t n);
  bool listed(int pid) const;

  tsclock::Packets packets_;
  ts::SectionReader pat_, pmt_, cue_;
  ts::ProgramMap map_;
  int pmtPid_ = -1, videoPid_ = -1;
  int cuePid_ = -1;  // the PID whose section is being put together
  int cuePids_[kMaxCuePids] = {};
  int cuePidCount_ = 0;
  uint32_t pmtCrc_ = 0;
  bool havePmt_ = false;
  bool havePts_ = false;
  int64_t lastPts_ = 0;
  Tally t_;
};

// ---------------------------------------------------------------- words

// The commands seen: "splice_insert", "37 splice_null, 3 splice_insert"
// (counts once there is more than one kind); "" none.
std::string commandsText(const Tally& t);
// What one short read found, for a clock check's line: "none", "declared,
// no cue in the first seconds", "2 cues (splice_insert, out of network,
// break 120 s)", "1 cue (time_signal), not declared"; "no tables read"
// when no PMT came and no cue; "" for nothing read.
std::string readText(const Tally& t);
// A whole replay's: "ad markers seen 3 times in 41 min (splice_insert,
// out of network)", "ad markers declared, none seen in 41 min", "no ad
// markers in 41 min"; "" for nothing read.
std::string replayText(const Tally& t);
// "41 min", "35 s" ("" for none).
std::string durationText(uint64_t ms);

}  // namespace marigold::media::scte35
