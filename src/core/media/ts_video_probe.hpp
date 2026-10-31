// "Is there a real picture in this MPEG-TS?", from the bytes alone.
//
// Runs on every byte the LG relay hands the TV's player (a second reader
// beside the captions). No decoding: the tables (PAT, PMT), the TS header
// bits, and a start code scan of the video's elementary stream that sizes
// each picture (filler NAL units and null packets left out, so a constant
// bit rate stream padded to its rate looks the same as a variable one) and
// reads its type (I or not) and the sequence header (codec, profile, bit
// depth, chroma, size).
//
// The video clock (DTS, else PTS) cuts it into 1 s windows; each closed
// window is called:
//   - picture: ordinary pictures;
//   - black: the last I frame and the other frames are tiny (a flat frame
//     compresses to almost nothing: 432 B for a 1080p H.264 I frame);
//   - static: ordinary I frames but tiny P / B frames (nothing moves: a
//     slate, a frozen source). MPEG-2 can't make a tiny I frame, so a black
//     MPEG-2 picture counts as static.
// Besides the windows: scrambled (most packets carry scrambling bits), no
// video (the PMT lists none, or only sound arrives), tables only (nothing
// but tables and null packets), unsupported (a sequence header this device
// can't decode).
//
// Built like the captions reader: no allocation per byte or packet, sync
// search with memchr, any chunking gives the same result.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "core/media/picture.hpp"
#include "core/media/ts_tables.hpp"

namespace marigold::media {

// Whose decoder the stream must suit (cannotDecode()).
enum class Device : uint8_t {
  Any,       // no table: the player says for itself
  LgWebOs,   // LG webOS TVs (2020-21 sets, C1 and the like)
};

struct VideoFacts {
  ts::Codec codec = ts::Codec::None;
  bool header = false;  // the sequence header / SPS was read
  int width = 0, height = 0;
  // H.264 profile_idc, HEVC general_profile_idc, MPEG-2 profile_and_level.
  int profile = -1;
  int bitDepth = 8;
  int chroma = 1;  // 0 monochrome, 1 4:2:0, 2 4:2:2, 3 4:4:4
  bool interlaced = false;
  double frameRate = 0;  // access units a second, measured; 0 unknown
  std::string audio;     // the first audio stream's codec ("aac"); "" none
  int audioStreams = 0;
};

// "1920x1080 59.94p · h264 High 8-bit · aac" (only what is known).
std::string describe(const VideoFacts& f);
// "h264 High 4:2:2 10-bit".
std::string codecName(const VideoFacts& f);
// Why `d` can't decode this ("h264 High 4:2:2 10-bit"); "" when it can, or
// when that isn't known.
std::string cannotDecode(Device d, const VideoFacts& f);

// Limits and thresholds.
inline constexpr double kBlackIntraPerMb = 0.25;   // bytes per macroblock: a flat I frame
inline constexpr double kBlackInterPerMb = 0.02;   // ... a flat P / B frame
inline constexpr double kStaticInterPerMb = 0.05;  // a P / B frame where nothing moves
// MPEG-2 can't skip the first and last macroblock of a slice: ~700 B a frame.
inline constexpr double kStaticInterPerMbMpeg2 = 0.15;
inline constexpr int kIntraMemory = 10;            // windows an I frame's size is used for
inline constexpr int kQuietPackets = 2000;         // packets with no sound or video: tables only
inline constexpr int kAudioWithoutVideo = 400;     // sound packets with no video packet among them
inline constexpr int kScrambleSample = 256;        // sound / video packets per scrambling count
inline constexpr size_t kTimelineMax = 600;        // windows kept for timeline()

class TsVideoProbe {
 public:
  explicit TsVideoProbe(Device device = Device::Any);

  void feed(const uint8_t* data, size_t n);  // any chunking; resyncs on 0x47
  void reset();                              // a new stream

  // The stream as it is now (the latest window, or what the tables and
  // packets say).
  Verdict verdict() const;
  // The numbers behind it, for the playback log: "I 432 B, 16 kbps",
  // "PMT audio 1", "h264 High 4:2:2 10-bit"...
  std::string detail() const;
  const VideoFacts& facts() const { return facts_; }
  // Windows closed so far, and one letter each (oldest first, at most
  // kTimelineMax): '.' picture, 'B' black, 's' static, '?' no pictures.
  int windows() const { return windows_; }
  const std::string& timeline() const { return timeline_; }
  bool sawPicture() const { return sawPicture_; }
  uint64_t packets() const { return packets_; }

 private:
  struct Window {
    int pictures = 0, intra = 0;
    uint64_t interBytes = 0, vclBytes = 0, fillerBytes = 0;
  };
  enum class Class : uint8_t { None, Picture, Black, Static };

  size_t consume(const uint8_t* p, size_t n);
  void packet(const uint8_t* p);
  void readPmt(const uint8_t* p, size_t n);
  void video(const uint8_t* p, size_t n, bool start);
  void es(const uint8_t* p, size_t n);
  void startCode(uint64_t pos, const uint8_t* after, size_t avail);
  void endNal(uint64_t pos);
  void header();
  void newPicture(bool intra);
  void endPicture();
  void endAccessUnit();
  void clock(int64_t t90);
  void closeWindow();
  void sps264(const uint8_t* p, size_t n);
  void sps265(const uint8_t* p, size_t n);
  double macroblocks() const;

  Device device_;

  // Sync.
  uint8_t carry_[2 * ts::kPacket + 1];
  size_t carryLen_ = 0;
  bool locked_ = false;

  // Tables.
  ts::SectionReader pat_, pmt_;
  ts::ProgramMap map_;
  int pmtPid_ = -1;
  bool havePmt_ = false;
  int videoPid_ = -1;
  int videoStreams_ = 0;
  uint32_t audioPids_[8] = {};  // PIDs of the program's sound, 0 = none
  bool conditionalAccess_ = false;

  // Packets.
  uint64_t packets_ = 0, nulls_ = 0;
  int quiet_ = 0;          // packets since the last sound or video one
  int audioSinceVideo_ = 0;
  int esSample_ = 0, esScrambled_ = 0;
  bool scrambled_ = false;
  int scrambledPercent_ = 0;
  int lastCc_ = -1;

  // The video's elementary stream.
  bool pesOpen_ = false;
  size_t pesSkip_ = 0;     // PES header bytes still to skip in the next packet
  uint64_t esPos_ = 0;     // video ES bytes seen
  uint8_t prev_[2] = {0xFF, 0xFF};  // the two bytes before this payload
  bool nalOpen_ = false;
  uint64_t nalStart_ = 0;
  uint8_t hdr_[64];
  size_t hdrLen_ = 0, hdrNeed_ = 0;
  bool hdrRead_ = false;
  bool picOpen_ = false, picIntra_ = false;
  uint64_t picBytes_ = 0;

  // Windows.
  bool haveStart_ = false;
  int64_t start90_ = 0;
  Window cur_;
  int windows_ = 0;
  Class last_ = Class::None;
  std::string timeline_;
  bool sawPicture_ = false;
  uint64_t lastIntra_ = 0;
  int lastIntraWindow_ = -1000;
  // The latest window's numbers, for detail().
  uint64_t lastInterMean_ = 0;
  int lastKbps_ = 0;
  // Frame rate: the clock of the last access units.
  int64_t rateTimes_[64] = {};
  int rateCount_ = 0;

  VideoFacts facts_;
};

// The probe behind a lock, for the relay: its thread hands the bytes of the
// current session, the UI thread reads a snapshot every frame.
class StreamCheck {
 public:
  explicit StreamCheck(Device device) : probe_(device) {}

  // A new stream; `session` is the relay session its bytes come with (0:
  // it doesn't come through the relay, and nothing is checked).
  void begin(unsigned session);
  // The relay's threads.
  void bytes(const uint8_t* data, size_t n, unsigned session);

  struct Snapshot {
    bool active = false;  // this stream is being read
    Verdict verdict = Verdict::Unknown;
    std::string detail;
    VideoFacts facts;
    uint64_t packets = 0;
  };
  Snapshot snapshot() const;

 private:
  mutable std::mutex mu_;
  unsigned session_ = 0;
  TsVideoProbe probe_;
};

}  // namespace marigold::media
