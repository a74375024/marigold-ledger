// MPEG-TS program tables (PAT, PMT) as the stream readers on the relay need
// them: the closed captions reader (core/captions/ts_captions) and the
// picture check (ts_video_probe). Sections are put together from packet
// payloads and kept only when their CRC is intact.
//
// No allocation per packet: a section buffer is reserved once and reused.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace marigold::media::ts {

inline constexpr size_t kPacket = 188;
inline constexpr uint8_t kSync = 0x47;
inline constexpr int kNullPid = 0x1FFF;
inline constexpr size_t kMaxSection = 1024;  // PAT and PMT sections are at most 1024 bytes

// MPEG-2 CRC-32 over a whole section, CRC included: 0 when intact.
uint32_t crc32(const uint8_t* p, size_t n);

// One table's section, put together from the payloads of its PID.
class SectionReader {
 public:
  SectionReader() { buf_.reserve(kMaxSection); }
  // A packet's payload (`start`: payload_unit_start_indicator). Returns the
  // whole section once it is in and its CRC is intact (valid until the next
  // call), else nullptr.
  const uint8_t* feed(const uint8_t* p, size_t n, bool start, size_t* size);
  void reset() {
    buf_.clear();
    open_ = false;
  }

 private:
  std::vector<uint8_t> buf_;
  bool open_ = false;
};

// The PMT PID of the first program in a PAT section, or -1.
int patFirstProgram(const uint8_t* p, size_t n);

// A program's streams, in PMT order.
struct Stream {
  int type = 0;  // stream_type
  int pid = -1;
  // A registration or AC-3 / E-AC-3 / DTS descriptor said what a private
  // stream (type 0x06) carries: "ac3", "eac3", "dts"; else "".
  const char* privateAudio = "";
};
struct ProgramMap {
  bool conditionalAccess = false;  // a CA descriptor, for the program or a stream
  std::vector<Stream> streams;
};
// Reads a PMT section into `out` (its vector is reused). False when it isn't one.
bool parsePmt(const uint8_t* p, size_t n, ProgramMap& out);

// What a stream type carries.
enum class Codec : uint8_t { None, Mpeg2, H264, Hevc, OtherVideo };
// The video codec of a stream type (Codec::None when it isn't video).
Codec videoCodec(int streamType);
// The audio codec of a stream ("aac", "ac3", "mp2"...), or nullptr when it
// isn't audio.
const char* audioCodec(const Stream& s);

}  // namespace marigold::media::ts
