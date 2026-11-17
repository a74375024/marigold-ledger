// Build 172: replay lands on the minute picked, whatever the provider's
// archive does with a request.
//
// Providers record channels on different backends. Asked for a moment,
// some start at exactly that minute; others at the start of the chunk
// holding it (10, 30, 60 minutes...) or of the programme; and a channel's
// recording can sit a minute or two off the clock its programme table
// implies. Each copy's archive is measured once (Test all sources, or the
// first replay of a copy not measured yet) and kept with its archive clock
// (archive-clocks.json, ids and numbers only):
//
//   - precision: two stretches about ten minutes apart, two hours back,
//     each read for its first video PTS (33 bits at 90 kHz, wraps handled),
//     and the live stream read at about the same time. Where the provider
//     keeps the broadcast's own timestamps, live PTS minus archive PTS is
//     how long ago each stretch really begins: the same lag behind each
//     moment asked is an exact archive (the lag, to the minute, being the
//     recording's offset); starts on the same chunk boundaries are a
//     chunked one (its size the one every start fits; a third read where
//     two sizes fit); starts on the programmes' starts, a programme one.
//     Timestamps that can't be the broadcast's (near zero, or out of
//     reach of the moments asked): "timestamps rewritten", not measured.
//   - seekability: whether a stretch answers a byte range (206 with its
//     Content-Range, or Accept-Ranges), and its bytes per second of video.
//
// Every replay is then planned from it (plan()): an exact archive is asked
// for the moment (with its offset); a chunked one for its chunk, and the
// player starts inside it at the moment, by a byte range where the
// provider answers ranges, else by reading the stream through when the
// moment is at most four minutes into the chunk; otherwise it plays from
// the chunk, and everything shown says so. Not measured: as Build 170.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/replay.hpp"
#include "core/net/http.hpp"
#include "core/util/time.hpp"

namespace marigold::engine::archive {

enum class Kind : uint8_t {
  Unknown,    // never measured
  Exact,      // starts at the minute asked
  Chunked,    // at the start of the chunk holding it
  Programme,  // at the start of the programme holding it
  Rewritten,  // timestamps not the broadcast's: not measured
  Unmeasured  // read, but nothing could be told (no live read, irregular starts)
};

struct Timing {
  Kind kind = Kind::Unknown;
  int chunkMinutes = 0;  // Chunked
  // Seconds later than the moment the archive is to be asked for it to
  // show the moment (the recording runs this much behind the clock;
  // negative: ahead). Whole minutes.
  int offset = 0;
  int seekable = -1;      // byte ranges: 1 answered, 0 not, -1 not known
  uint32_t byteRate = 0;  // bytes a second of video (0 not known)
  time::Seconds at = 0;   // when measured
  bool told() const { return kind != Kind::Unknown; }
};

// The chunk sizes looked for, in minutes.
inline constexpr int kChunkMinutes[] = {5, 10, 15, 20, 30, 60, 120};
// Read-through: at most this far into a chunk (and this many bytes a
// second of it, with a cap) when the archive answers no byte range.
inline constexpr time::Seconds kReadThrough = 4 * 60;
inline constexpr uint64_t kReadThroughCap = 400ull * 1024 * 1024;
// Starts within this of each other are the same start.
inline constexpr time::Seconds kSame = 30;
// Measured again after this long.
inline constexpr time::Seconds kKeptFor = 30 * 86400;

// ------------------------------------------------------- the measurement

// One read of a stream: when it was asked (archive: the moment asked;
// live: the wall clock when its first bytes came), its first video PTS.
struct Read {
  time::Seconds asked = 0;
  std::optional<int64_t> pts;
};
struct Readings {
  std::optional<Read> live;
  std::vector<Read> archive;   // two or three, in the order read
  int clock = 0;               // the archive clock the moments were asked in (seconds east)
  std::vector<time::Seconds> programmeStarts;  // the copy's programmes, for Programme
  int seekable = -1;
  uint32_t byteRate = 0;
  time::Seconds at = 0;
};
// What the reads say. `ambiguous` (when set) gets the chunk sizes still
// possible when more than one is (a third read decides).
Timing classify(const Readings& r, std::vector<int>* ambiguous = nullptr);
// The moment for a third read that tells `sizes` apart (after the first
// archive read's start `start`).
time::Seconds decidingMoment(time::Seconds start, const std::vector<int>& sizes, int clock);

// An answer's byte range support: a 206 with a Content-Range, or
// Accept-Ranges: bytes on an answer that says its length.
bool answersRanges(int status, const std::string& contentRange, const std::string& acceptRanges,
                   uint64_t contentLength);

// What the measurement needs, made on the UI thread (the links carry the
// login: never logged). `linkFor` makes the archive link for a moment.
struct Request {
  std::string live;  // the copy's live link, MPEG-TS
  std::vector<std::string> headers;
  std::function<std::optional<std::string>(time::Seconds)> linkFor;
  int clock = 0;
  std::vector<time::Seconds> programmeStarts;
};
struct Outcome {
  std::optional<Timing> timing;  // nullopt: nothing could be read
  bool refused = false;
  bool aborted = false;
  std::string why;  // for the log ("http 404"), never a link
};
// The first moment read: about two hours back, on minute 23 of its hour
// (no chunk boundary), the second eleven minutes later.
time::Seconds firstMoment(time::Seconds now);
inline constexpr time::Seconds kApart = 11 * 60;
// Runs the reads on the calling worker thread: live, then the two
// moments (the second asking a byte range from 0), a third when two chunk
// sizes fit. `budgetMs`: the whole of it (each read stops at its first
// picture and a few seconds of video, sooner near the end).
using Clock = std::function<uint32_t()>;
Outcome measure(http::Pool& pool, const Request& req, const Clock& clock, const std::atomic<bool>* abort,
                uint32_t budgetMs, time::Seconds now);

// ------------------------------------------------------- the replay plan

struct Plan {
  enum class How : uint8_t { AsAsked, Seek, ReadThrough, FromChunk };
  How how = How::AsAsked;
  time::Seconds ask = 0;    // the moment asked of the provider
  time::Seconds base = 0;   // the moment the stream asked for begins with
  time::Seconds shows = 0;  // where the replay begins (base + skip)
  int skip = 0;             // seconds into the stream
  uint64_t startByte = 0;   // Seek: about where the moment is
  uint32_t byteRate = 0;
  int chunkMinutes = 0;     // Chunked
  bool programme = false;   // the chunk is the programme
};
// `picked`: the moment wanted (to the minute); `clock` the archive clock
// the links ask in; `canSkip`: the player can start inside a stream (the
// replay relay takes it); `next`: a stretch following one that ended (a
// chunk it can't skip into begins at the next boundary instead, so nothing
// plays twice).
Plan plan(const std::optional<Timing>& t, time::Seconds picked, int clock, bool canSkip, bool next,
          const std::vector<time::Seconds>& programmeStarts);
// Where replays can start on the timeline for this archive (replay::Reach):
// chunk boundaries for a chunked archive the player can't seek into, any
// minute otherwise.
replay::Reach reach(const std::optional<Timing>& t, int clock, bool canSkip);

// ---------------------------------------------------------------- words

// The channel's line, once: "archive: exact", "archive: 30-min chunks,
// skips to the minute", "archive: 60-min chunks, no skip", "archive:
// recording 1 min behind", "archive: timestamps rewritten, not measured".
std::string describe(const Timing& t, bool canSkip);
// A replay's: "(chunk 20:00, skipped 21 min)", "(chunk 20:00, no skip)",
// "(recording 1 min behind)"; "" for one asked as picked. `hm` formats a
// moment as the log's clock.
std::string planText(const Plan& p, const std::optional<Timing>& t,
                     const std::function<std::string(time::Seconds)>& hm);
// A provider's archives in words for Test all sources (shares, never
// counts): "exact on all replay channels", "30-min chunks on most (skips
// to the minute), exact on some".
std::string providerText(const std::vector<Timing>& all);

// Saved: "e|c30|p|r|u", then ",<offset>,<seekable>,<rate>,<at>".
std::string encode(const Timing& t);
std::optional<Timing> decode(const std::string& s);

}  // namespace marigold::engine::archive
