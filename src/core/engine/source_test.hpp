// Build 166: Test all sources. Every copy of every My Lineup
// channel, on every provider, is probed once over HTTP (source_probe.hpp):
// live, and for a copy that keeps catch-up one stretch of its archive about
// two hours back and one near the edge it claims (its days less an hour and
// a half), to confirm the depth. Each provider (account) is read one
// connection at a time, the providers side by side, with a short spaced gap
// (5 to 10 s, jittered) between tests. A provider that refuses a connection
// (HTTP 403 / 429 / 451 / 458, connections used up, connection refused) is
// left alone for 15 minutes; refusing again straight after stops it for
// this run, the others carry on. The whole test pauses while anything plays
// (and while the app is away) and carries on after. Progress is saved
// (which copies are done), so a restart carries on; a run older than two
// days is dropped instead. Build 174: a run stopped part way (by you, or
// because the providers left turned it away) is kept a week and can be
// carried on from where it stopped (carryOn()); Start over is start().
//
// Results go into the copies' records (Store::recordProbe), which order a
// channel's copies, and into the screens: the providers ranked (rank()),
// one or two kept (choices(), lost()), what was found so far (foundSoFar()).
//
// Never a link, a user name or a password in the log or on screen, and
// never a count of channels on screen: percentages only.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/engine/feed_scores.hpp"
#include "core/engine/host.hpp"
#include "core/engine/source_probe.hpp"
#include "core/util/time.hpp"

namespace marigold::engine {
class Store;
}

namespace marigold::engine::sourcetest {

// Pacing.
constexpr uint32_t kGapMinMs = 5000, kGapMaxMs = 10000;
constexpr uint32_t kRefusalPauseMs = 15 * 60 * 1000;
constexpr uint32_t kResumeAfterPlayMs = 10000;   // after watching, before the next test
constexpr uint32_t kLostAfterMs = probe::kTimeoutMs + 40000;  // a probe that never answered
// A test's cost before its own pace is known: a few seconds of reading
// and the gap.
constexpr uint32_t kItemMs = 4000 + (kGapMinMs + kGapMaxMs) / 2;
// The archive: one stretch this far back, one this far from the edge.
constexpr time::Seconds kArchiveBack = 2 * 3600;
constexpr time::Seconds kEdgeMargin = 90 * 60;
// A run not finished in this long is dropped (not carried on).
constexpr time::Seconds kRunKeptFor = 2 * 86400;
// Build 174: a run stopped part way (by you, or providers that turned it
// away) can be carried on this long after it stopped.
constexpr time::Seconds kStoppedKeptFor = 7 * 86400;
// The saved progress.
constexpr const char* kFile = "source-test.json";

// --------------------------------------------------------------- the plan

// What one test asks of a copy: a stretch of its archive about two hours
// back, one near the edge of the archive, or live. A copy's archive is
// tested before its live picture, so the live probe's record carries both.
//
// Build 170: before a copy's archive is probed, its own guide
// (get_simple_data_table, one light API request, in the same turn and under
// the same refusal rules) is read for the clock its archive is indexed on
// (Store::archiveTable keeps it); the archive probes ask in that clock. Its
// outcome is saved under the Clock key ("o<seconds east>" or "f"); no plan
// item has that kind.
//
// Build 172: once a copy's archive played, its timing (archive_timing.hpp:
// its live picture and two moments of its archive, read for their clocks,
// a few seconds each) is measured in a turn of its own before its next
// item, under the same pacing and refusal rules, and kept with its archive
// clock. Saved under the Timing key ("t<encoded>", "f" nothing read, "x"
// nothing to measure); no plan item has that kind either.
enum class Kind : uint8_t { Archive, Depth, Live, Clock, Timing };

struct Item {
  size_t row = 0, copy = 0;  // in the rows the plan was made from
  Kind kind = Kind::Live;
  std::string channelId;  // the copy
};
// "<copy id>#A" / "#D" / "#L" ("#C" its archive clock): an item's key in
// the saved progress.
std::string key(const std::string& channelId, Kind kind);

struct Plan {
  std::vector<std::string> providers;    // in the accounts' order
  std::vector<std::vector<Item>> queues;  // one per provider, in lineup order
  size_t total() const;
};
// Every copy of every row once (a copy in two rows, once), on a provider in
// `providers`; `testable` leaves out copies that can't be read directly
// (a server opens Emby / Jellyfin streams). Copies with an archive get
// Archive and Depth first.
Plan plan(const std::vector<scores::LineupRow>& rows, const std::vector<std::string>& providers,
          const std::function<bool(const std::string&)>& testable = nullptr);
// The longest queue at the default pace, in seconds (the start panel's
// "About 3 to 4 hours").
uint32_t estimateSeconds(const Plan& p);

// ------------------------------------------------- what the results say

// A copy's name for comparing two providers' lists: lower case, the
// country prefix's separator ("US|", "US:") as a space, spaces collapsed.
std::string normalName(std::string_view name);

// Two providers that look like one service underneath: at least half of
// the smaller one's lineup copy names are the same (normalName), and, when
// enough of their copies on shared rows failed to tell, their failures go
// together.
struct SameService {
  std::string a, b;
  double nameShare = 0;
  std::optional<double> failureOverlap;  // shared failures over failures, when measurable
};
constexpr double kSameNames = 0.5;
constexpr double kSameFailures = 0.25;
constexpr size_t kFailuresToTell = 4;
std::vector<SameService> sameServices(const std::vector<scores::LineupRow>& rows,
                                      const std::vector<std::string>& providers, const scores::Book& book,
                                      time::Seconds now);
bool isSame(const std::vector<SameService>& same, std::string_view a, std::string_view b);

// After the first copy, the backup comes from a separate service: when the
// second copy's provider is another one flagged the same service as the
// first's, the first copy from a provider that isn't moves up to second.
void preferSeparateBackup(std::vector<std::string>& ids,
                          const std::function<bool(std::string_view, std::string_view)>& same);

// The providers ranked (Results). Plays reliably first, then start speed
// and real picture, then lineup coverage and replay:
//   0.50 works + 0.15 speed + 0.15 picture + 0.12 lineup + 0.08 replay
// works = 1 - 5 x the share of starts that showed no picture (plays and
// probes, a probe half a play; 0 from one start in five; 0.5 unknown);
// speed = 1 at 1.5 s to picture or less, 0 at 10 s (0.5 unknown); picture
// = the share of its measured copies at 1080p or more (0.5 unknown);
// lineup = the share of My Lineup it has; replay = the share with replay
// from it that a probe didn't find broken.
constexpr double kFailureWeight = 5.0;
double rankScore(const scores::ProviderFigures& f);
std::vector<size_t> rank(const scores::Rollup& r);

// Keeping one or two: every pair and every single provider, the best pair
// first (two separate services before the same one; then the lineup in
// five-point steps, then replay, then measured reliability), the other
// pairs by lineup then replay, then the singles in rank order.
struct Choice {
  std::vector<std::string> providers;  // one or two
  double lineup = 0, replay = 0;
  enum class Kind { Separate, Same, Alone } kind = Kind::Alone;
  bool best = false;
};
std::vector<Choice> choices(const scores::Rollup& r, const std::vector<SameService>& same);

// The rows a choice keeps none of, by name (lineup order, each once), up to
// `limit`; `more` when there are others.
struct Lost {
  std::vector<std::string> names;
  bool more = false;
};
Lost lost(const std::vector<scores::LineupRow>& rows, const std::vector<std::string>& kept, size_t limit = 7);

// ------------------------------------------------------------- the words

// "About 3 to 4 hours", "About 40 minutes".
std::string estimateText(uint32_t seconds);
// "about 2 h 10 min left", "about 25 min left", "almost done".
std::string timeLeftText(uint32_t ms);
// "today 1:12 AM", "yesterday 9:40 PM", "Nov 14" in `zone`.
std::string testedText(time::Seconds t, time::Seconds now, const time::Zone& zone, bool clock24);
// The day alone: "today", "yesterday", "Nov 14" (the Sources panel's
// "tested today").
std::string testedDay(time::Seconds t, time::Seconds now, const time::Zone& zone);
// A copy's measured line for the Sources panel: "1080p 60 · starts in 1.4
// s · replay 1 day", "720p 60 (says HD) · starts in 1.6 s", "Didn't start ·
// tried last"; "" when nothing is measured. `claimed` is what its name says
// (quality::claimed), `days` the archive it claims.
std::string copyLine(const scores::Record* r, const std::string& claimed, uint16_t days, time::Seconds now);
// "1080p 60", "4K", "720p" ("" unknown).
std::string pictureText(int height, int fps);
// "1.9 s".
std::string secondsText(uint32_t ms);
// "54% · 1 day", "None".
std::string replayCell(const scores::ProviderFigures& f);
// Build 172: how long a timing measurement may take, all reads together.
// Build 174: room for an archive that is slow to start (its server builds
// the stretch first): about 12 s a read, as a probe has.
constexpr uint32_t kTimingBudgetMs = 36000;
// Build 174: an archive probe's time to its first picture (a live one has
// probe::kTimeoutMs): the server may build the stretch before its first
// byte.
constexpr uint32_t kArchiveProbeMs = 20000;
// Build 170: a provider's archive clocks in words, from each replay copy's
// clock (seconds east of UTC) and its server's: "UTC+1 on all replay
// channels", "UTC+1 (most), UTC (some)", with "; server says UTC" after
// when the most common one differs from it. Shares in words, never a
// count; "" for none.
std::string clocksText(const std::vector<int>& clocks, std::optional<int> server);

// ------------------------------------------------------------- the runner

class Runner {
 public:
  Runner(Store& store, Host host);
  ~Runner();
  Runner(const Runner&) = delete;
  Runner& operator=(const Runner&) = delete;

  // Something plays (or the app is away): the test waits.
  std::function<bool()> playing;
  // One playback log line: ("test", detail). Never a link.
  std::function<void(std::string_view, std::string_view)> log;
  // The probes' clock (any thread); the tests' own.
  probe::Clock clock = probe::steadyClock();

  // A new run from the start (the last one's results stay in the records).
  void start();
  // Stops; what was found so far is kept, and (Build 174) where it got to:
  // carryOn() picks up from there.
  void stop();
  bool running() const { return run_.active; }

  // Build 174: a run that stopped part way (by you, or because every
  // provider left had turned it away), within kStoppedKeptFor.
  bool canCarryOn() const;
  // Carries it on: what was tested stays, the rest is tested; providers
  // that turned it away get another try. Nothing when !canCarryOn().
  void carryOn();
  struct Carry {
    time::Seconds stoppedAt = 0;
    double fraction = 0;   // of the whole run, done
    uint32_t leftSeconds = 0;  // the rest at the default pace
    std::map<std::string, double> providers;  // provider id -> its share tested
  };
  // Where a stopped run got to (against today's lineup); nullopt when
  // there's nothing to carry on.
  std::optional<Carry> carry() const;
  // When a run last ended (finished or stopped), 0 never.
  time::Seconds lastTested() const { return lastTested_; }
  // Moves when a run ends, for the screen showing it.
  int endedSerial() const { return ended_; }
  // The plan for a new run, for the start panel's estimate.
  uint32_t estimateNow() const;

  // UI thread, once per loop: starts what is due. True when what the
  // screen shows changed.
  bool tick(uint32_t ms);
  // Writes the progress now (the app leaving).
  void saveNow();

  struct Provider {
    std::string id, name;
    size_t done = 0, total = 0;
    enum class State { Waiting, Testing, Paused, Stopped, Done } state = State::Waiting;
    std::string now;            // "CNN · live", "CNN · replay 2 h back"
    time::Seconds until = 0;    // Paused: tries again then
  };
  struct Status {
    bool active = false, waitingForPlay = false;
    double fraction = 0;
    std::optional<uint32_t> leftMs;
    std::vector<Provider> providers;
    std::vector<std::string> found;  // foundSoFar()
  };
  Status status() const;

  // What the run found so far (up to three plain sentences).
  std::vector<std::string> foundSoFar() const;

 private:
  struct Slot;
  struct Writer;
  void load();
  void save(bool now);
  bool ensurePlan();
  void next(Slot& s, uint32_t ms);
  // Build 170: the copy's archive clock, then its archive probe.
  void readClock(Slot& s, const Item& item, uint32_t ms);
  // Build 172: the copy's archive timing.
  void readTiming(Slot& s, const Item& item, uint32_t ms);
  void probeItem(Slot& s, const Item& item, uint32_t ms, uint32_t started);
  void finishItem(size_t provider, const Item& item, const probe::Outcome& o, uint32_t ms, uint32_t started);
  void endRun(bool stopped);
  void abortAll();
  void logLine(const std::string& detail);
  std::string providerName(const std::string& id) const;
  std::string summary() const;

  Store& store_;
  Host host_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  std::shared_ptr<Writer> writer_;
  std::minstd_rand random_;

  struct Run {
    bool active = false;
    time::Seconds started = 0;
    // item key -> outcome: live "o<height>,<fps>,<ms>" or "f"; archive and
    // depth "o", "f" or "s" (skipped: the archive two hours back failed);
    // "x" a copy that is gone.
    std::map<std::string, std::string> done;
    struct Account {
      int refusals = 0;          // in a row
      time::Seconds until = 0;   // paused until
      bool stopped = false;      // refused twice in a row
      bool logged = false;       // its start line written (this app run)
      bool finished = false;
    };
    std::map<std::string, Account> accounts;
    // Build 174: stopped part way and kept for carrying on (active false).
    bool stopped = false;
    time::Seconds stoppedAt = 0;
    time::Seconds resumed = 0;  // carried on last (the age rule counts from it)
  };
  Run run_;
  time::Seconds lastTested_ = 0;
  int ended_ = 0;

  Plan plan_;
  std::vector<scores::LineupRow> rows_;
  bool planned_ = false;
  struct Slot {
    size_t index = 0;            // in plan_.providers
    size_t at = 0;               // the next item to look at in its queue
    bool inFlight = false;
    uint32_t startedMs = 0, nextMs = 0;
    std::shared_ptr<std::atomic<bool>> abort;
    std::string nowText;
    uint32_t costMs = 0, costN = 0;  // the pace measured this app run
    std::map<std::string, int> lost;  // probes that never answered
    int serial = 0;                   // the timing measurement out (Build 172)
  };
  std::vector<Slot> slots_;
  bool waitingForPlay_ = false;
  bool droppedOld_ = false;  // a run too old to carry on was dropped (logged on the first tick)
  bool droppedStopped_ = false;  // Build 174: it was a stopped one
  mutable std::optional<Carry> carryCache_;  // carry(), for the stop it was worked out for
  // Build 174: the providers whose replay timing read failing was said (once a run).
  std::set<std::string> timingFailTold_;
  uint32_t resumeAt_ = 0;
  uint32_t ms_ = 0;
  bool dirty_ = false;
  uint32_t savedAt_ = 0;
};

// The testing screen's words: "38% · about 2 h 10 min left" (or "Paused
// while you watch"), and a provider card's line ("Now: CNN · live", "It
// refused a connection. Trying again in 15 min; the others carry on.").
std::string headerLine(const Runner::Status& s);
std::string providerLine(const Runner::Provider& p, time::Seconds now);
// The card's corner: "52%", "Paused", "Stopped", "Done".
std::string providerCorner(const Runner::Provider& p);

}  // namespace marigold::engine::sourcetest
