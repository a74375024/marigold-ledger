// Build 166 (1.0.10): what each copy (a raw channel, "feed") has shown when
// it really played: how long its picture took (the median of its recent
// starts), the starts that failed, the stalls per hour played, the picture
// measured on it and when it was last seen. Learned from the plays the
// viewer makes (no extra connections); Test all sources (Build 166) adds its
// probes to the same records (recordProbe).
//
// Older results count less: every count halves each week (kHalfLife), so a
// copy that was bad a month ago and fine since reads as fine. The book keeps
// the newest kCap copies (the least recently seen dropped first) and is
// saved as JSON: ids and numbers only, never a link.
//
// Uses: the order a channel's copies are tried in (orderMeasured; Build
// 169: the best working picture first); the provider roll-up (rollup) for
// the playback log, so the providers can be compared; the channel report's
// measured_* columns.
//
// Build 180 (1.0.23): replays have figures of their own in the same record
// (a replay is never charged to the live ones): the stretches opened and
// those that showed no picture, their time to picture, the stalls (and
// their seconds) and the time watched. The roll-up's "replay:" part and a
// replay's choice of copy (Book::replayBadness) use them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/util/time.hpp"

namespace marigold::engine::scores {

// Every count halves in this long.
constexpr time::Seconds kHalfLife = 7 * 86400;
// A probe (Test all sources) weighs this much of a real play.
constexpr double kProbeWeight = 0.5;
// Starts (weighted) before a copy's record is used for its order.
constexpr double kEnoughStarts = 2.0;
// Test all sources: a copy Test all sources probed is ordered by its record at once
// (the test probes every copy once), while that probe still weighs this
// much (about two weeks).
constexpr double kEnoughProbes = 0.25;
// A measured copy goes ahead of another only when its score is higher by
// more than this (scores run 0..1; like the source reliability's 0.25, a
// margin, so near-equal copies keep the catalog's order).
constexpr double kClearlyBetter = 0.12;
// Build 169 (1.0.12): a measured copy "works" when its starts show a
// picture and show it reasonably soon:
//   works = (ok + 0.9) / (ok + bad + 1) >= kWorksAt
// (ok and bad: plays and probes, a probe weighing kProbeWeight), and its
// median start is at most kWorkingStartMs. The prior is lighter than the
// score's (ok + 1) / (ok + bad + 2) so the one probe Test all sources makes
// is enough (0.93) while one failure in four starts isn't (0.78).
constexpr double kWorksAt = 0.8;
constexpr uint32_t kWorkingStartMs = 4000;
// Build 180: replays (weighted, starts and start failures) before a copy's
// replay figures choose between copies; a stall an hour weighs as this
// much of the start-failure rate; and how much worse a copy's replays must
// be (failure rate + weighted stalls) before another is preferred.
constexpr double kEnoughReplays = 3.0;
constexpr double kReplayStallWeight = 0.1;
constexpr double kReplayClearlyWorse = 0.1;

struct Record {
  time::Seconds seen = 0;  // the newest result; the weights are as of then
  double plays = 0;        // starts that showed a picture
  double failures = 0;     // starts that didn't
  double stalls = 0;       // stalls long enough to show "Buffering…"
  double playedS = 0;      // seconds of picture played
  std::vector<uint32_t> startMs;  // time to picture, oldest first (kSamples)
  // The picture measured last (0: not known).
  int width = 0, height = 0, fps = 0;
  // Test all sources (Build 166): probes that showed / didn't show a picture,
  // and what a probe found of the archive.
  double probes = 0, probeFailures = 0;
  int archiveOk = -1;        // -1 not probed, 0 didn't play, 1 played
  uint16_t archiveDays = 0;  // the depth a probe found (0 not known)
  time::Seconds archiveAt = 0;
  time::Seconds probedAt = 0;  // the newest probe (0 never)
  // Build 180, replays: stretches opened that showed a picture (a next
  // stretch following one that ended isn't a start), those that showed
  // none, their time to picture (oldest first, kSamples), the stalls of 3 s
  // or more and their seconds, and the seconds watched.
  double replayStarts = 0, replayFailures = 0, replayStalls = 0, replayStallS = 0, replayPlayedS = 0;
  std::vector<uint32_t> replayStartMs;

  // The weights as they stand at `now`.
  Record at(time::Seconds now) const;
  // Starts, probes weighed in.
  double starts() const;
  // More of its starts (probes weighed in) showed no picture than did.
  bool mostlyFails() const;
  // Build 169: (ok + 0.9) / (ok + bad + 1), probes weighed in.
  double works() const;
  // works() >= kWorksAt and the median start within kWorkingStartMs.
  bool working() const;
  std::optional<uint32_t> medianStartMs() const;
  // Stalls per hour played (null: under a minute played).
  std::optional<double> stallsPerHour() const;
  // Build 180: the replay figures (null: none yet): the median time to
  // picture, the share of starts that showed none, stalls and stall
  // seconds per hour watched (under a minute watched: null; under half an
  // hour, as half an hour, like stallsPerHour), minutes watched.
  bool replayed() const { return replayStarts + replayFailures > 0 || replayPlayedS > 0; }
  std::optional<uint32_t> replayMedianStartMs() const;
  std::optional<double> replayFailureRate() const;
  std::optional<double> replayStallsPerHour() const;
  std::optional<double> replayStallSecondsPerHour() const;
  double replayMinutes() const { return replayPlayedS / 60.0; }
};

// Build 169: what the order knows of one copy (Book::standing; the store
// fills in a tier from the pictures memory or the name when the record has
// none).
struct Standing {
  bool measured = false;  // enough history to order by (Book::score's rule)
  bool works = false;     // measured and Record::working()
  // Its picture tier: measured when a picture was, else what its name
  // claims; 0 not known.
  int tier = 0;
  double score = 0;  // scoreOf: start speed and smoothness, the tie-break
};

// What a probe found (Test all sources, Build 166).
struct Probe {
  bool ok = false;        // a picture came
  uint32_t startMs = 0;   // ... this long after the request
  int width = 0, height = 0, fps = 0;
  // The archive, when it was tried: played or not, and its depth (days).
  std::optional<bool> archiveOk;
  uint16_t archiveDays = 0;
};

class Book {
 public:
  static constexpr size_t kCap = 3000;
  static constexpr size_t kSamples = 9;

  // A start showed its picture `ms` after the request.
  void started(const std::string& feedId, uint32_t ms, time::Seconds now);
  // A start that showed no picture.
  void failed(const std::string& feedId, time::Seconds now);
  // A stall (or a picture that broke) while it played.
  void stalled(const std::string& feedId, time::Seconds now);
  // `ms` more of its picture played.
  void played(const std::string& feedId, uint32_t ms, time::Seconds now);
  // The picture measured on it.
  void picture(const std::string& feedId, int width, int height, int fps, time::Seconds now);
  // Test all sources: one probe's result, into the same record.
  void recordProbe(const std::string& feedId, const Probe& probe, time::Seconds now);
  // Build 180, replays: a stretch's picture `ms` after it was handed to the
  // player; a stretch that showed none; a stall of `ms`; `ms` watched.
  void replayStarted(const std::string& feedId, uint32_t ms, time::Seconds now);
  void replayFailed(const std::string& feedId, time::Seconds now);
  void replayStalled(const std::string& feedId, uint32_t ms, time::Seconds now);
  void replayPlayed(const std::string& feedId, uint32_t ms, time::Seconds now);
  // Build 180: how badly a copy's replays go, for choosing between copies:
  // its start-failure rate plus kReplayStallWeight per stall an hour; null
  // under kEnoughReplays replays. No copy of the record (cheap enough for
  // every frame).
  std::optional<double> replayBadness(const std::string& feedId, time::Seconds now) const;

  const Record* find(const std::string& feedId) const;
  size_t size() const { return byId_.size(); }
  // Every record, least recently seen first.
  void each(const std::function<void(const std::string&, const Record&)>& fn) const;
  // 0..1, higher better; null while the copy has too little history.
  std::optional<double> score(const std::string& feedId, time::Seconds now) const;
  // Build 169: what the order knows of a copy from its record (see
  // Standing); not measured while it has too little history (as score).
  Standing standing(const std::string& feedId, time::Seconds now) const;

  // When the provider roll-up last went into the playback log (0 never).
  time::Seconds rolledUpAt = 0;

  // {"v":1,"rolledUp":t,"feeds":[[id,seen,plays,failures,stalls,playedS,
  // [ms...],width,height,fps,probes,probeFailures,archiveOk,archiveDays,
  // archiveAt,probedAt],...]}, oldest first; weights in hundredths.
  // Build 180: a copy with replay figures has six more at the end:
  // replayStarts, replayFailures, [replay ms...], replayStalls,
  // replayStallS, replayPlayedS. Still "v":1: a row without them reads as
  // no replays, and older builds read the first sixteen.
  std::string toJson() const;
  // A saved book (the newest kCap of it); empty when it doesn't read.
  static Book fromJson(std::string_view text);

 private:
  Record& touch(const std::string& feedId, time::Seconds now);
  struct Kept {
    Record record;
    uint64_t seq = 0;
  };
  std::unordered_map<std::string, Kept> byId_;
  std::map<uint64_t, std::string> bySeq_;  // least recently seen first
  uint64_t seq_ = 0;
};

// The score of a record as it stands (no evidence check).
double scoreOf(const Record& r);

// Build 169 (1.0.12): picture tiers, better higher: 0 not known, 1 under
// 720 lines, 2 720p, 3 720p at 50 / 60 fps, 4 1080 lines or more, 5 the
// same at 50 / 60 fps (49 fps and up; 4K counts as 1080).
int pictureTier(int height, int fps);
// The tier a name claims (quality::claimed's "FHD · 60", "HD", "SD", "4K"),
// at its best: names seldom give the frame rate, so a size counts as at
// 50 / 60 fps whether it says so or not (4K and FHD 5, HD 3, SD 1); 0 when
// it claims no size. An unmeasured copy is then passed only by a picture
// it can't have.
int claimedTier(std::string_view claimed);

// `a` is a working measured copy with a better picture than `b`: a higher
// tier, where `b`'s is known (an unmeasured copy that claims nothing isn't
// passed: it may well be as good).
bool outranks(const Standing& a, const Standing& b);

// A channel's copies, in the order they are tried (Build 169, 1.0.12):
//  1. the measured copies are reordered among their own places: the
//     working ones first, by picture tier, then (within a tier) by score
//     where clearly better (kClearlyBetter), then the catalog's order; the
//     ones that don't work after them, by score as before;
//  2. a copy with no history keeps its place, except that a working
//     measured copy behind it with a better picture than it claims
//     (outranks) moves just ahead of it;
//  3. the first `keep` (the pinned copy, or the last played one while
//     keepsFirst) stay first.
void orderMeasured(std::vector<std::string>& ids, size_t keep,
                   const std::function<Standing(const std::string&)>& standing);
// The last played copy stays first unless another of `copies` outranks it
// (a working measured copy with a better picture).
bool keepsFirst(const std::string& first, const std::vector<std::string>& copies,
                const std::function<Standing(const std::string&)>& standing);
// Whether a direct copy may not pass `ahead` (a server-opened copy before
// it) in directVersionsFirst / leadServerVersions: `ahead` outranks it, or
// works while it is measured and doesn't. Otherwise (the same tier,
// nothing measured) direct copies go first as before.
bool holdsAhead(const Standing& ahead, const Standing& behind);

// "xt:abc" of "xt:abc|123": the provider (account) of a copy.
std::string_view providerOf(std::string_view feedId);

// ---------------------------------------------------------------- roll-up

// A lineup row's copies: each one's provider and whether it keeps an archive.
struct RowCopy {
  std::string provider;
  bool replay = false;
};
using Row = std::vector<RowCopy>;

// Test all sources (Build 166): a lineup row with its name (the guide's) and
// its copies: each one's id, provider, name in the provider's list and the
// archive it claims (days; 0 none).
struct LineupCopy {
  std::string feedId, provider, name;
  uint16_t days = 0;
};
struct LineupRow {
  std::string entryId, name;
  std::vector<LineupCopy> copies;
};

struct ProviderFigures {
  std::string provider;  // its id ("xt:…")
  double plays = 0;      // weighted, recent counting most
  std::optional<uint32_t> medianStartMs;
  std::optional<double> failureRate;  // of starts, 0..1
  std::optional<double> stallsPerHour;
  std::optional<double> fullHd;  // share of plays at 1080 lines or more (measured)
  // Build 180, replays (real ones only): starts weighted (starts and start
  // failures), the median time to picture, the share of starts that showed
  // none, stalls per hour watched.
  double replays = 0;
  std::optional<uint32_t> replayMedianStartMs;
  std::optional<double> replayFailureRate;
  std::optional<double> replayStallsPerHour;
  // Shares of the lineup's rows (0..1): with a copy from it, with a copy
  // from it alone, with replay from it.
  double lineup = 0, only = 0, replay = 0;
  // Test all sources (Build 166)' results (the fields above stay the log's:
  // real plays only). Probes and plays together, a probe weighing
  // kProbeWeight: the starts, the share that showed no picture; the share
  // of its measured copies (each once) at 1080 lines or more; the share of
  // rows with replay from it that a probe didn't find broken, and the
  // typical depth of its copies' archives (days: what a probe found, else
  // what it claims; 0 none).
  double tested = 0;
  std::optional<double> startFailures;
  std::optional<double> fullHdCopies;
  double replayWorks = 0;
  uint16_t replayDays = 0;
};
struct PairFigures {
  std::string a, b;
  double lineup = 0, replay = 0;  // shares of rows either one covers
};
struct Rollup {
  std::vector<ProviderFigures> providers;
  std::vector<PairFigures> pairs;
};

// `providers` in the order to report them; `rows` the lineup's rows.
Rollup rollup(const Book& book, const std::vector<std::string>& providers, const std::vector<Row>& rows,
              time::Seconds now);
// The same from the lineup's rows with their copies, with the Test all sources fields
// (Test all sources' results) filled in too.
Rollup rollupLineup(const Book& book, const std::vector<std::string>& providers, const std::vector<LineupRow>& rows,
                    time::Seconds now);
// The playback log's "providers" lines: (provider column, detail). Shares
// as percentages; never a count of channels. `name` gives a provider's
// name.
std::vector<std::pair<std::string, std::string>> rollupLines(
    const Rollup& r, const std::function<std::string(const std::string&)>& name);

// The channel report's measured_* cells of one copy ("" unknown).
struct Cells {
  std::string startMs, failures, stallsPerHour, picture, plays;
};
Cells cellsOf(const Record* r, time::Seconds now);

}  // namespace marigold::engine::scores
