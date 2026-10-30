// Closed caption bytes out of a live MPEG-TS stream.
//
// ATSC A/53 carries CEA-608 (and 708) captions in the video itself: cc_data
// in a user_data_registered_itu_t_t35 SEI ("GA94", user_data_type_code 3)
// for H.264 and HEVC, or in picture user data for MPEG-2 video. This finds
// the first video stream (PAT, then PMT), reassembles its PES packets up to
// the first picture slice (the caption data always comes before it, so the
// picture itself is never copied), and pulls the 608 pairs out.
//
// The video arrives in decode order, but captions must be shown in
// presentation order: with B-frames they differ. Each access unit's pairs
// are held until 4 later access units have arrived or the newest picture is
// more than 300 ms ahead, then released earliest PTS first.
//
// Built to run on every byte of a live stream on a TV: no allocation per
// byte or per packet, sync search with memchr, buffers reused and capped.
// Any chunking works, and garbage or lost packets only lose what they hit.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace marigold::captions {

struct CcPair {
  int64_t ptsMs;  // presentation time of the access unit carrying it
  int field;      // 0 = field 1 (CC1/CC2), 1 = field 2 (CC3/CC4)
  uint8_t b1, b2; // as carried, with parity bits
};

// Limits.
inline constexpr size_t kMaxPesBytes = 2u << 20;  // a PES with more than this before its first slice is dropped
inline constexpr int kReorderUnits = 4;           // access units held for B-frame reordering
inline constexpr int64_t kReorderMs = 300;        // ... or this far behind the newest PTS
inline constexpr int64_t kPtsJumpMs = 10000;      // a PTS this far back is a new timeline
inline constexpr size_t kMaxReadyPairs = 8192;    // pairs kept when nobody takes them

class TsCaptions {
 public:
  TsCaptions();

  void feed(const uint8_t* data, size_t n);  // any chunking, resyncs on 0x47, survives garbage
  std::vector<CcPair> take();                // pairs ready, oldest first
  // The same, into a buffer the caller keeps (cleared first): no allocation
  // once both buffers have grown.
  void takeInto(std::vector<CcPair>& out);
  // End of stream: releases the pairs still held for reordering.
  void flush();
  bool sawCaptionData() const { return sawCaptionData_; }  // any valid 608 cc pair seen
  void reset();                                            // new stream (channel change)
  // PTS of the newest video access unit seen (the highest so far on this
  // timeline), ms, for syncing to the player clock; -1 if none.
  int64_t newestPtsMs() const { return haveNewest_ ? newestMs_ : -1; }

 private:
  enum class Codec { None, H264, Hevc, Mpeg2 };
  struct Section {
    std::vector<uint8_t> buf;
    bool open = false;
  };
  struct Held {
    uint64_t unit;  // which access unit, in arrival order
    CcPair pair;
  };

  size_t consume(const uint8_t* p, size_t n);
  void packet(const uint8_t* p);
  void section(Section& s, const uint8_t* p, size_t n, bool start, bool isPat);
  void sectionDone(const uint8_t* p, size_t n, bool isPat);
  void readPat(const uint8_t* p, size_t n);
  void readPmt(const uint8_t* p, size_t n);
  void video(const uint8_t* p, size_t n, bool start, int cc, bool discontinuity);
  void startPes();
  void endPes();
  void dropPes();
  void parsePes();
  void scanUnits();
  void unit(size_t from, size_t to);
  void sei(const uint8_t* p, size_t n);
  void ccData(const uint8_t* p, size_t n);
  int64_t unwrapMs(int64_t pts90);
  void accessUnitDone();
  void release(bool all);

  // Sync.
  uint8_t carry_[2 * 188 + 1];  // bytes held over between calls
  size_t carryLen_ = 0;
  bool locked_ = false;

  // Tables.
  Section patSection_, pmtSection_;
  int pmtPid_ = -1;
  int videoPid_ = -1;
  Codec codec_ = Codec::None;

  // The PES being collected.
  std::vector<uint8_t> pes_;
  std::vector<uint8_t> rbsp_;
  bool pesOpen_ = false;
  bool pesHeader_ = false;
  bool unitDone_ = false;   // the access unit's caption data is in; the rest is picture
  size_t pesLength_ = 0;    // whole PES size from its header, 0 = unbounded
  size_t pesReceived_ = 0;
  size_t scan_ = 0;         // where the start code search resumes
  size_t nal_ = 0;          // start of the current NAL (its header byte), if haveNal_
  bool haveNal_ = false;
  int lastCc_ = -1;
  bool havePts_ = false;
  int64_t ptsMs_ = 0;
  std::vector<CcPair> unitPairs_;

  // PTS and reordering.
  bool havePrev90_ = false;
  int64_t prev90_ = 0;
  bool haveNewest_ = false;
  int64_t newestMs_ = 0;
  int64_t lastOutMs_ = 0;
  bool haveOut_ = false;
  uint64_t units_ = 0;
  std::vector<Held> held_;
  std::vector<CcPair> ready_;
  bool sawCaptionData_ = false;
};

}  // namespace marigold::captions
