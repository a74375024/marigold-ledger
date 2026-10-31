// The picture check: whether a stream that plays shows a real picture.
//
// A verdict comes from the stream itself (the LG relay reads every byte:
// ts_video_probe) or from the player (frames drawn, video tracks listed, the
// frame counter). Recovery (core/playback/recovery) decides what to do
// about one; nothing here acts.
#pragma once

#include <cstdint>

namespace marigold::media {

enum class Verdict : uint8_t {
  Unknown,      // not enough seen yet, or nothing checks this stream
  Picture,      // real pictures
  Unsupported,  // a codec / profile / bit depth this device can't decode
  Scrambled,    // encrypted (the TS scrambling bits are set)
  NoVideo,      // the program has sound but no video
  TablesOnly,   // only tables and padding: no sound, no video
  NoFrame,      // the sound plays and the player hasn't drawn a frame
  Black,        // pictures, all flat black (tiny I and P frames)
  Frozen,       // the player stopped drawing frames while its clock runs
  Static,       // pictures that don't change: a slate, a frozen source
};

// For the playback log: "black", "no video track"...
const char* verdictName(Verdict v);

}  // namespace marigold::media
