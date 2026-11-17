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
//
// Build 173: the archive's hour from the video itself. A lag behind every
// moment asked that is a whole number of quarter hours (to within a
// minute), from 15 minutes to 14 hours either way, isn't the recording
// running late: the provider's archive is on another clock than the one
// the links asked in (its server and its own table can both say UTC while
// the archive runs on UTC+1). That lag becomes the channel's archive
// clock (Timing::clock, kept by the store as measured, over what the table
// or the server says, and taught to the account's other channels); only a
// smaller lag stays the recording's offset. A first replay on a channel
// whose clock was never measured reads its live picture and one stretch
// near the moment picked (classifyQuick) before it starts, a few seconds
// at most, so even that replay plays the hour picked.
//
// Build 174: the reads ask as the player asks. On an LG TV 1.0.16's reads
// never once told a clock: the app's own HTTP client (rede) refused any
// answer whose Content-Length was over the read's cap before its first
// byte (an archive stretch says its whole size; live says none), said
// "Nuvio/1.8" where the player's path says a browser's agent, asked for a
// compressed body, wouldn't follow an https link to http; the read took a
// first piece without a packet start for "not a stream" for good, and gave
// a slow archive two seconds. Each read now goes out as the player's would (probe::asPlayer,
// Request::media), tells a body from its first 4 KB together, follows an
// HLS answer to its media (an archive's first segment), and says why it
// failed in words for the log (never a link). A replay's own check stays
// within its few seconds; the store's background check (Store::
// clockCheckDue) gives the reads a minute.
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
  // Build 173: the clock the archive is really on (seconds east of UTC):
  // the clock the reads asked in, moved by a whole quarter-hour lag where
  // there was one. nullopt: the reads couldn't tell. Not saved with the
  // timing: the store keeps it as the copy's measured archive clock.
  std::optional<int> clock;
  bool told() const { return kind != Kind::Unknown; }
};

// Build 173: a lag (seconds the archive starts before the moment asked;
// negative: after) as a clock error and the recording's offset. `clock`: a
// whole number of quarter hours from 15 min to 14 h when the lag is that
// to within a minute (else 0); `offset`: the rest, whole minutes (under
// 45 s is none). `usable` false: no clock error and an offset over 90
// minutes (nothing a recording does).
struct LagParts {
  int clock = 0;
  int offset = 0;
  bool usable = true;
};
LagParts splitLag(time::Seconds lag);
inline constexpr int kClockStep = 15 * 60;
inline constexpr int kClockMost = 14 * 3600;

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
// possible when more than one is (a third read decides). Build 173: with
// the clock the archive is on (Timing::clock) wherever the reads tell it;
// a chunked archive on another clock is found too.
Timing classify(const Readings& r, std::vector<int>* ambiguous = nullptr);
// Build 173: what the live read and the first archive read alone say, for
// a first replay that can't wait for more: Exact, with its clock and
// offset, when the stretch starts a whole number of quarter hours (zero
// included) before or after the moment asked, to within a minute, and not
// on a programme's start; nullopt otherwise (a chunk, a programme, a
// stream that isn't the broadcast's: the full reads tell).
std::optional<Timing> classifyQuick(const Readings& r);
// The moment read for a replay of `picked`: the minute nearest it that is 7
// past a quarter hour (no chunk of any size in kChunkMinutes starts a
// whole number of quarter hours before it), at least ten minutes back.
time::Seconds quickMoment(time::Seconds picked, time::Seconds now);
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
  // The live read's (the copy's own). Build 174: the archive reads send none,
  // as the player gets none for a replay (Store::catchupFeed).
  std::vector<std::string> headers;
  std::function<std::optional<std::string>(time::Seconds)> linkFor;
  int clock = 0;
  std::vector<time::Seconds> programmeStarts;
  // Build 173: a replay's moment: the first archive read is near it
  // (quickMoment), each read stops at its first picture, and when those
  // two alone tell (classifyQuick) nothing more is read. `quickMs`: the
  // most the live read and that one may take together (0: the budget).
  std::optional<time::Seconds> picked;
  uint32_t quickMs = 0;
};
struct Outcome {
  std::optional<Timing> timing;  // nullopt: nothing could be read
  bool refused = false;
  bool aborted = false;
  // For the log ("http 404"), never a link. Build 174: why the reads
  // couldn't tell the clock, in words, with "(live)" or "(archive)" after
  // ("no answer in 20 s (archive)", "not a video stream (text/html)
  // (archive)", "out of time"); also set when `timing` is told without a
  // clock.
  std::string why;
};
// The first moment read: about two hours back, on minute 23 of its hour
// (no chunk boundary), the second eleven minutes later.
time::Seconds firstMoment(time::Seconds now);
inline constexpr time::Seconds kApart = 11 * 60;
// Runs the reads on the calling worker thread: live, then the two
// moments (the second asking a byte range from 0), a third when two chunk
// sizes fit. `budgetMs`: the whole of it (each read stops at its first
// picture and a few seconds of video, sooner near the end). Build 173:
// with `req.picked`, a read near it first (see Request); and reads cut
// short by the budget tell nothing (nullopt), so the copy is measured
// again rather than kept as not measurable.
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
// Build 173: an offset of a whole number of quarter hours (to within a
// minute) is a clock error 1.0.15 kept as an offset: not read back, so
// the copy is measured again.
std::string encode(const Timing& t);
std::optional<Timing> decode(const std::string& s);

}  // namespace marigold::engine::archive
