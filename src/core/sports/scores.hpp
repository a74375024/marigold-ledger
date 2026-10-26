// Live status and scores (LiveScores.swift, and the scoreboard parsers in
// SportsData.swift). While Live Events or the in-player event guide is
// open, each league with a game on now (or starting within 5 minutes) is
// asked for today's scoreboard: one request covers every game in it. That
// gives each game's state (not started, live, delayed, final, postponed),
// its clock, and the score and situation.
//
// Kept gentle on the source (the request budget is part of the design):
// - only while the board or the event guide is on screen, never in the
//   background or while watching (unless the event guide is open);
// - only leagues with a game in its window, one request at a time;
// - once a minute per league while a game is live (every 2 minutes with
//   scores off, or for a large scoreboard); every 2 minutes around a start;
// - a failed request backs that league off (2 -> 5 -> 15 -> 30 minutes);
// - at most kDailyCap requests a day, then nothing until tomorrow.
// Nothing about the viewer is sent.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/sources/common.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/types.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::scores {

// MARK: Parsing

// "pre" / "in" / "post" and the status name -> the phase.
LivePhase livePhase(const std::optional<std::string>& state, std::string_view name);
// The tile's clock: "Q3 4:12", "2nd 11:02", "67'", "Bot 8th", "Halftime".
std::string liveDetail(const League& league, LivePhase phase, std::string_view name, int period,
                       std::string_view clock, const std::optional<std::string>& shortDetail);

using Statuses = std::map<std::string, LiveStatus>;
// Today's scoreboard -> each game's live status, by game id (the same ids
// as the schedule's: "NFL|401...", "UFC|600...|main", "F1|<event>|<session>",
// "ATP|<event>|<match>"). Empty when the body can't be read.
Statuses statuses(std::string_view body, const League& league);
Statuses gameStatuses(std::string_view body, const League& league);
// A fight card's parts: how many fights are done, and the one on now (or
// next). Never who won.
Statuses fightStatuses(std::string_view body, const League& league);
// Each race session's status; the lap count when the short status gives one.
Statuses raceStatuses(std::string_view body, const League& league);
// Each tennis match's status ("Set 2", "Final") and games per set.
Statuses tennisStatuses(std::string_view body, const League& league);

// MARK: Requests

constexpr int kTimeoutSeconds = 15;
// Requests a day, at most.
constexpr int kDailyCap = 500;
// Requests an hour before intervals stretch to 5 minutes.
constexpr int kHourlyPace = 120;
// Between two requests of a round.
constexpr int kRequestSpacingMs = 700;
// A league is polled from this long before a game's start...
constexpr Seconds kLeadBeforeStart = 5 * 60;
// ...until this long after it (or while the scoreboard says it's live, up
// to half a day).
constexpr Seconds kWindowAfterStart = 6 * 3600;
constexpr Seconds kLiveAtMost = 12 * 3600;
// Polling intervals.
constexpr Seconds kLiveInterval = 60;
constexpr Seconds kQuietInterval = 120;
constexpr Seconds kPacedInterval = 300;
constexpr Seconds kIdleWait = 300;
constexpr Seconds kWatchingWait = 60;
constexpr Seconds kCappedWait = 1800;
constexpr Seconds kMinimumWait = 15;
constexpr size_t kLargeScoreboardBytes = 700'000;
// Failed requests back a league off.
constexpr Seconds kBackoff[] = {120, 300, 900, 1800};
// A game no scoreboard listed this many polls in a row stops keeping its
// league polled.
constexpr int kMissingLimit = 3;
constexpr bool kShowsScoresByDefault = true;

// A league's scoreboard for a US Eastern day ("20261025"), or the current
// listing when `day` is empty (race weekends, tennis).
sources::Request scoreboardRequest(const League& league, std::string_view day);

// MARK: Polling

// Who's looking: the board (Live Events on screen) or the in-player event
// guide. Polling runs while anyone is.
enum class Holder { board, guide };

struct RoundInput {
  // The published schedule.
  std::vector<SportsGame> games;
  // Tennis has matches all day on every court: only the ones you can watch
  // (a tile with a source) keep a tour polled.
  std::set<std::string> watchableTennis;
  // A channel is playing (Live Events may sit under the player).
  bool watching = false;
  bool showsScores = kShowsScoresByDefault;
  Seconds now = 0;
  // The viewer's zone, for the daily request count.
  time::Zone zone = time::Zone::device();
};

// The poller's state, owned by the caller (UI thread). A round asks each due
// league in turn:
//   scores.beginRound(input);
//   while (auto request = scores.nextRequest(now)) { send; scores.onResponse(...); }
//   wait scores.waitSeconds(), then the next round (while looking()).
class LiveScores {
 public:
  struct Request {
    League league;
    std::string day;
    sources::Request request;
    // Wait this long before sending (kRequestSpacingMs after the first).
    int waitMs = 0;
  };
  struct DailyCount {
    std::string day;  // "yyyy-MM-dd" in the viewer's zone
    int count = 0;
  };

  LiveScores() = default;
  explicit LiveScores(DailyCount daily) : daily_(std::move(daily)) {}

  // Live Events (or the guide) opened. True when the caller should start a
  // round now: polling wasn't running, or the guide opened while it was (it
  // looks now instead of after the current wait; due times still apply).
  bool start(Holder holder, bool scheduleEnabled);
  // Closed: true when nobody is looking any more (stop the loop and drop
  // any round in progress with cancelRound()).
  bool stop(Holder holder);
  bool looking() const { return !holders_.empty(); }
  // The scores setting changed: the next poll uses the new pace.
  void settingsChanged() { revision_ += 1; }

  void beginRound(const RoundInput& input);
  // The next request of the round, or nullopt when the round is over.
  std::optional<Request> nextRequest(Seconds now);
  // The answer: HTTP status (0 = none) and body; parsed here. Or parse on a
  // worker with statuses() and call onParsed.
  void onResponse(int status, std::string_view body, Seconds now);
  void onParsed(bool ok, size_t bytes, Statuses parsed, Seconds now);
  // Live Events closed mid-request: not a failure, nothing recorded.
  void cancelRound();
  // After the round: how long to wait before the next one (at least 15 s).
  Seconds waitSeconds() const;

  const Statuses& statuses() const { return statuses_; }
  const std::map<std::string, Seconds>& finalSeen() const { return finalSeen_; }
  std::optional<LiveStatus> status(const std::string& gameID) const;
  // Bumped when statuses change.
  int revision() const { return revision_; }
  // Persist across launches (UserDefaults in Swift).
  const DailyCount& daily() const { return daily_; }
  int requestsToday(Seconds now, const time::Zone& zone) const;

  // Merges fresh statuses (public for tests and for saved snapshots). A
  // game first seen final is stamped now if it was live, else 20 minutes
  // ago (already over when first looked at).
  void apply(const Statuses& fresh, Seconds now, const std::vector<SportsGame>& games);

 private:
  bool takeRequest(Seconds now);
  void endLeague(Seconds now);

  std::set<Holder> holders_;
  Statuses statuses_;
  std::map<std::string, Seconds> finalSeen_;
  int revision_ = 0;
  std::map<std::string, Seconds> nextDue_;
  std::map<std::string, int> failures_;
  std::map<std::string, size_t> lastBytes_;
  std::map<std::string, int> missing_;
  std::vector<Seconds> recent_;
  DailyCount daily_;
  bool cappedNoted_ = false;
  time::Zone zone_ = time::Zone::device();

  struct Round {
    std::vector<SportsGame> games;
    std::map<std::string, std::vector<SportsGame>> windows;
    std::vector<std::string> order;  // window keys, A-Z
    size_t nextIndex = 0;
    bool showsScores = true;
    double spread = 1;
    Seconds soonest = kIdleWait;
    int sent = 0;
    bool over = false;
    // The league being asked.
    bool inLeague = false;
    std::string shortName;
    League league;
    std::vector<std::string> days;
    size_t dayIndex = 0;
    Statuses merged;
    bool ok = false;
    size_t bytes = 0;
    bool awaiting = false;
  };
  std::optional<Round> round_;
  Seconds lastWait_ = kIdleWait;
};

}  // namespace marigold::sports::scores
