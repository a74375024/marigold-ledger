// Build 177, Skip break: a game's breaks (commercials, timeouts, the ends
// of periods, halftime) from its play-by-play, by the wall clock.
//
// The scoreboard source's event summary ("summary?event=<id>", the same
// site as the scores) lists a game's plays, each with the wall-clock time
// it was logged ("wallclock", ISO 8601): at the top level ("plays":
// basketball, hockey) or by drive ("drives.previous[].plays", and the drive
// under way, "drives.current.plays": football). Only the plays' times,
// periods and kinds are kept; the body (up to a megabyte for football) is
// read a play (or a drive) at a time on a worker and dropped.
//
// A break: two plays in a row with a gap of at least Rules::gap where the
// first is a timeout, the end of a period or half, a score, the two-minute
// warning, an injury or an official review (or a stoppage), or any gap of
// at least Rules::anyGap. It runs from the first play's time plus
// Rules::trim to the next play's less Rules::trim, and is kept only when
// that leaves at least Rules::shortest. Breaks with only one play between
// them are one break. The end of period Rules::halftimeAfter (or a play
// that says halftime or the end of a half) starts halftime. A game still
// on whose last play started a break (or was long enough ago) ends in an
// open break: it runs to live.
//
// Fetched only while a game's replay plays (Store::wantBreaks), one request
// at a time, in the background's lane, spaced, capped per day, backed off
// after a failure and left alone after a refusal (429, 403) as the score
// requests are (refusals.hpp). A live game's are asked again every
// Fetch::kRefresh while its replay is behind live. Nothing about the viewer
// is sent.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/sources/common.hpp"
#include "core/sports/leagues.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::plays {

using time::Seconds;

// What a play says about what comes after it.
enum class Kind : uint8_t {
  Plain,
  Timeout,     // a team's or a TV/official timeout
  PeriodEnd,   // the end of a period or quarter
  HalfEnd,     // the end of a half, halftime
  Score,       // a scoring play
  Warning,     // football's two-minute warning
  Stoppage,    // an injury, an official review or challenge, a stoppage
  GameEnd,
};

struct Play {
  Seconds at = 0;  // its wall clock
  int period = 0;
  Kind kind = Kind::Plain;
};

// The thresholds of a sport (seconds).
struct Rules {
  Seconds gap = 75;        // a gap after a timeout, a period end, a score...
  Seconds anyGap = 150;    // ... or any gap this long
  Seconds trim = 10;       // off each end: the play before, the next one coming
  Seconds shortest = 45;   // what's left must be this long
  int halftimeAfter = 2;   // the end of this period is halftime (0: none)
};
// Per sport. Football: four quarters, halftime after the second.
constexpr Rules kFootball{75, 150, 10, 45, 2};
// Basketball in quarters (NBA, WNBA, women's college).
constexpr Rules kBasketball{75, 150, 10, 45, 2};
// Men's college basketball: two halves, halftime after the first.
constexpr Rules kCollegeHalves{75, 150, 10, 45, 1};
// Hockey: three periods, intermissions (no halftime).
constexpr Rules kHockey{75, 150, 10, 45, 0};
// The rules for a league's play-by-play; nullopt: none read for it
// (soccer, baseball, racing, tennis, golf, fights, cricket, the NCAA's own
// pages).
std::optional<Rules> rulesFor(const League& league);
// Its plays come by drive (football).
bool byDrive(const League& league);

struct Break {
  Seconds from = 0, to = 0;  // wall clock; open: `to` is not known (live)
  bool halftime = false;
  bool open = false;         // runs to live (the next play isn't in yet)
  Seconds length() const { return to - from; }
  bool operator==(const Break& o) const {
    return from == o.from && to == o.to && halftime == o.halftime && open == o.open;
  }
};

// `plays` in any order. `live`: the game is still on (a trailing open
// break is possible); `asOf`: when the plays were read (the last play this
// long ago with no break kind still opens one past anyGap).
std::vector<Break> detect(std::vector<Play> plays, const Rules& rules, bool live = false, Seconds asOf = 0);

// What a summary body gives: the plays with a wall clock (in the order
// read; repeats by play id left out), and how many plays it had in all.
struct Parsed {
  std::vector<Play> plays;
  size_t seen = 0;   // plays read, with a wall clock or not
  bool ok = false;   // the body was JSON with plays (or drives) in it
};
Parsed parse(std::string_view body, bool drives);
// A play's kind from its type ("Timeout", "End Period"), its text and
// whether it scored.
Kind kindOf(std::string_view type, std::string_view text, bool scoring);

// The summary of event `eventId` ("401...", the schedule's id without its
// league) of a league with rules.
sources::Request summaryRequest(const League& league, std::string_view eventId);
// The largest body read (football runs to about a megabyte).
constexpr size_t kMaxBodyBytes = 3u << 20;
constexpr int kTimeoutSeconds = 20;

// What the store keeps of a game: its breaks and when they were read.
struct Breaks {
  std::string league;  // "NFL"
  std::vector<Break> list;
  bool live = false;   // read while the game was on
  Seconds readAt = 0;
};

// The game a replay shows, as the board knows it.
struct Game {
  std::string eventId;  // the board's
  std::string league;   // "NFL"
  std::string summaryId;  // "401..."
  bool live = false;    // still on (not final)
};

// The fetches (UI thread): one game wanted at a time (the replay playing),
// a few kept.
class Fetch {
 public:
  static constexpr Seconds kRefresh = 150;     // a live game's, while its replay is behind live
  static constexpr Seconds kWantedFor = 20;    // the player asks again within this
  static constexpr Seconds kSpacing = 15;      // between two requests
  static constexpr int kDailyCap = 120;        // requests in a day (rolling)
  static constexpr size_t kKept = 6;           // games kept
  static constexpr Seconds kBackoff[] = {120, 300, 900, 1800};

  // The replay of `g` plays (or is opening). Asked every few seconds.
  void want(const Game& g, Seconds now);
  struct Ask {
    std::string eventId;
    std::string league;
    bool live = false;  // the game is still on (an open break is possible)
    sources::Request request;
  };
  // A request out longer than this (its answer dropped with every other,
  // an account change) no longer holds the next one back.
  static constexpr Seconds kLostAfter = 120;
  // The next request (one out at a time), nullopt none due.
  std::optional<Ask> next(Seconds now);
  // Its answer: the HTTP status (0 none), the breaks read from a 200, the
  // Retry-After seconds. True when the game's breaks are new.
  bool done(const std::string& eventId, int status, std::optional<std::vector<Break>> breaks, Seconds now,
            int retryAfterSeconds, const time::Zone& zone);
  // Nothing more comes for the request out (dropped).
  void cancel() { busy_ = false; }
  // The breaks read for a game (null: none read yet).
  std::shared_ptr<const Breaks> breaks(const std::string& eventId) const;
  bool busy() const { return busy_; }
  // Sources refusing us ("espn.com") until when.
  const std::map<std::string, Seconds>& refusedUntil() const { return refusedUntil_; }
  void refuse(const std::string& source, Seconds until) { refusedUntil_[source] = until; }
  int sentToday(Seconds now) const;

 private:
  struct Entry {
    Game game;
    std::shared_ptr<const Breaks> breaks;
    Seconds readAt = 0;      // last answer that read (0 none)
    bool readLive = false;   // ... while the game was on
    Seconds nextAt = 0;      // not before (back-off)
    int failures = 0;
    Seconds used = 0;        // last wanted
  };
  std::map<std::string, Entry> entries_;
  std::string wanted_;
  Seconds wantedAt_ = 0;
  bool busy_ = false;
  Seconds lastSent_ = 0;
  std::vector<Seconds> sent_;
  std::map<std::string, Seconds> refusedUntil_;
};

}  // namespace marigold::sports::plays
