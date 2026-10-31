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
// are held until the newest picture's decode time (DTS, from the PES
// header) has reached their presentation time: every picture shown before
// them has arrived by then, however deep the B-frame hierarchy (HEVC sends
// up to 7 between two references). A safety cap lets them go after 32 later
// pictures or 1.5 s. A stream that never sends a DTS keeps the old window:
// 4 later access units, or the newest picture more than 300 ms ahead.
//
// Each pair also gets a media time: its PTS on one continuous timeline that
// carries on across PTS jumps (the relay reopening a source, an HLS
// discontinuity), as a player's own clock does. It counts from the first
// picture seen; keyMediaMs() is the first one a decoder can start on (an
// IDR or I picture), where a player's picture starts.
//
// Built to run on every byte of a live stream on a TV: no allocation per
// byte or per packet, sync search with memchr, buffers reused and capped.
// Any chunking works, and garbage or lost packets only lose what they hit.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/media/ts_tables.hpp"

namespace marigold::captions {

struct CcPair {
  int64_t ptsMs;  // presentation time of the access unit carrying it
  int field;      // 0 = field 1 (CC1/CC2), 1 = field 2 (CC3/CC4)
  uint8_t b1, b2; // as carried, with parity bits
  int64_t mediaMs = 0;  // the same on the continuous media timeline
};

// Limits.
inline constexpr size_t kMaxPesBytes = 2u << 20;  // a PES with more than this before its first slice is dropped
inline constexpr int kReorderUnits = 4;           // access units held for B-frame reordering (no DTS)
inline constexpr int64_t kReorderMs = 300;        // ... or this far behind the newest PTS
inline constexpr int kReorderCapUnits = 32;       // with DTS: never held longer than this many pictures
inline constexpr int64_t kReorderCapMs = 1500;    // ... or this far behind the newest PTS
inline constexpr int64_t kPtsJumpMs = 10000;      // a PTS this far back or ahead is a new timeline
inline constexpr int64_t kDiscontinuityJumpMs = 1000;  // ... or this far, at a TS discontinuity
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
  // The same on the media timeline; false until a picture arrives.
  bool haveMedia() const { return haveNewest_; }
  int64_t newestMediaMs() const { return newestMedia_; }
  // The first picture a decoder can start on (media ms); false until one.
  bool haveKey() const { return haveKey_; }
  int64_t keyMediaMs() const { return keyMedia_; }
  // The first PTS seen (ms, as sent; media time 0).
  int64_t firstPtsMs() const { return firstPtsMs_; }
  // PTS jumps the media timeline carried on across.
  int jumps() const { return jumps_; }

 private:
  enum class Codec { None, H264, Hevc, Mpeg2 };
  struct Held {
    uint64_t unit;  // which access unit, in arrival order
    CcPair pair;
  };

  size_t consume(const uint8_t* p, size_t n);
  void packet(const uint8_t* p);
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
  bool keyPicture(const uint8_t* p, size_t n) const;
  void accessUnitDone();
  void release(bool all);

  // Sync.
  uint8_t carry_[2 * 188 + 1];  // bytes held over between calls
  size_t carryLen_ = 0;
  bool locked_ = false;

  // Tables (shared with the picture check: core/media/ts_tables).
  media::ts::SectionReader patSection_, pmtSection_;
  media::ts::ProgramMap pmt_;
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
  bool haveDts_ = false;
  int64_t dtsMs_ = 0;
  bool unitKey_ = false;          // the access unit is one a decoder can start on
  bool discontinuity_ = false;    // the TS flagged a discontinuity on the video
  std::vector<CcPair> unitPairs_;

  // PTS and reordering.
  bool havePrev90_ = false;
  int64_t prev90_ = 0;
  bool haveNewest_ = false;
  int64_t newestMs_ = 0;
  // Media time = PTS + base_ (moved at each jump so the timeline carries on).
  int64_t base_ = 0;
  int64_t newestMedia_ = 0;
  int64_t newestDts_ = 0;        // media ms
  int64_t lastDts_ = 0;          // media ms, the last access unit's
  int64_t frameMs_ = 33;         // the picture interval, from the DTS steps
  bool sawDts_ = false;
  bool haveKey_ = false;
  int64_t keyMedia_ = 0;
  int64_t firstPtsMs_ = 0;
  int jumps_ = 0;
  int64_t lastOutMs_ = 0;
  bool haveOut_ = false;
  uint64_t units_ = 0;
  std::vector<Held> held_;
  std::vector<CcPair> ready_;
  bool sawCaptionData_ = false;
};

}  // namespace marigold::captions
