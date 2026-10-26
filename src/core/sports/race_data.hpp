// Race weekend extras (RaceData.swift): F1 driver photos, numbers and team
// colours for the cameras panel, and each finished session's top 3 for the
// race tile (scores on), from a free F1 data feed; NASCAR driver photos and
// car numbers for the in-car feeds, from the series' driver list.
//
// Kept to a handful of requests a race weekend (the budget is part of the
// design):
// - the weekend's session list: once, then again only when a session the
//   schedule has isn't in it (at most every 3 hours) or after a day;
// - the drivers: once a weekend (or after 4 days);
// - a session's result: once, 10 minutes after it ends (retried every 20
//   minutes, given up after 6 tries, and only within 4 days), 2 a pass;
// - only while Live Events or the cameras panel shows an F1 weekend, one
//   request at a time (700 ms apart), at most kDailyCap a day; a failure
//   waits 30 minutes;
// - NASCAR drivers: at most once a week (6 hours after a failure), only when
//   a NASCAR race's cameras panel opens.
// Nothing about the viewer is sent.
//
// No I/O: the caller owns a RaceData (built from the saved JSON), sends the
// requests it hands out and gives back the answers:
//   if (race.refreshIfNeeded(sessions, showsScores, now))
//     while (auto r = race.nextRequest(now, zone)) { wait r->waitMs; send; race.onResponse(status, body, now); }
//     // the pass is over: write race.saved() (toJson) and redraw if revision() moved.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/sources/common.hpp"
#include "core/sports/board.hpp"
#include "core/sports/types.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::race_data {

struct RaceDriver {
  int number = 0;
  std::string code;  // "DRA"
  std::string name;  // "Driver Alpha"
  std::string team;
  std::optional<std::string> color;  // "F47600"
  std::optional<std::string> headshot;
  bool operator==(const RaceDriver& o) const {
    return number == o.number && code == o.code && name == o.name && team == o.team && color == o.color &&
           headshot == o.headshot;
  }
  bool operator!=(const RaceDriver& o) const { return !(*this == o); }
};

// A session of the weekend, from the data feed.
struct Session {
  int key = 0;
  std::string kind;  // "fp1", "qual", "race"... (race_session::kind)
  Seconds start = 0;
  std::optional<Seconds> end;
};

// A session the schedule has (kind, start): the refresh checks the data
// feed's list knows it.
struct WantedSession {
  std::string kind;
  Seconds start = 0;
};

// MARK: Budget

constexpr const char* kBase = "https://api.openf1.org/v1/";
constexpr int kTimeoutSeconds = 15;
constexpr int kDailyCap = 60;
constexpr int kRequestSpacingMs = 700;
// A schedule session and a data feed session are the same within 3 hours.
constexpr Seconds kSameSession = 3 * 3600;
constexpr Seconds kSessionsMaxAge = 24 * 3600;
constexpr Seconds kSessionsUnknownAge = 3 * 3600;
constexpr Seconds kDriversMaxAge = 4 * 86'400;
constexpr Seconds kDriversNewMeetingAge = 3600;
constexpr Seconds kResultDelay = 10 * 60;
constexpr Seconds kResultWindow = 4 * 86'400;
constexpr Seconds kResultRetry = 20 * 60;
constexpr int kResultMaxTries = 6;
constexpr size_t kResultsPerPass = 2;
constexpr Seconds kFailurePause = 30 * 60;

constexpr const char* kNascarUrl = "https://cf.nascar.com/cacher/drivers.json";
constexpr int kNascarTimeoutSeconds = 20;
constexpr Seconds kNascarMaxAge = 7 * 86'400;
constexpr Seconds kNascarFailurePause = 6 * 3600;

// MARK: Requests and parsing

sources::Request sessionsRequest();                // sessions?meeting_key=latest
sources::Request driversRequest();                 // drivers?session_key=latest
sources::Request resultRequest(int sessionKey);    // session_result?session_key=N
sources::Request nascarRequest();

struct ParsedSessions {
  std::optional<int> meeting;
  std::vector<Session> sessions;
};
ParsedSessions parseSessions(std::string_view body);
struct ParsedDrivers {
  std::optional<int> meeting;
  std::vector<RaceDriver> drivers;
};
ParsedDrivers parseDrivers(std::string_view body);
// Driver numbers of the first three places.
std::vector<int> parseResult(std::string_view body);
// Drivers with a real photo, Cup Series first (the same name can be in
// several series).
std::vector<RaceDriver> parseNascar(std::string_view body);

// "Driver Alpha (C)" -> "Driver Alpha"; dots and Jr / Sr / II / III dropped.
std::string bareName(std::string_view name);

// MARK: Saved state (race-data.json, nascar-drivers.json)

struct Saved {
  std::optional<int> meetingKey;
  std::vector<Session> sessions;
  std::optional<Seconds> sessionsAt;
  std::vector<RaceDriver> drivers;
  std::optional<int> driversMeeting;
  std::optional<Seconds> driversAt;
  // Session key -> driver numbers, first three places.
  std::map<std::string, std::vector<int>> results;
  std::map<std::string, Seconds> resultTried;
  // Session key -> tries so far (a session with no result is given up after 6).
  std::map<std::string, int> resultTries;
  std::optional<std::string> requestDay;  // "yyyy-MM-dd" in the viewer's zone
  int requestCount = 0;
};
std::string toJson(const Saved& saved);
std::optional<Saved> savedFromJson(std::string_view text);

struct NascarSaved {
  std::vector<RaceDriver> drivers;
  std::optional<Seconds> fetchedAt;
};
std::string toJson(const NascarSaved& saved);
std::optional<NascarSaved> nascarFromJson(std::string_view text);

// MARK: The refresh

class RaceData {
 public:
  struct Plan {
    bool sessions = false;
    bool drivers = false;
    std::vector<int> results;  // session keys
    bool empty() const { return !sessions && !drivers && results.empty(); }
  };
  enum class Ask { sessions, drivers, result };
  struct Request {
    Ask ask = Ask::sessions;
    int sessionKey = 0;  // Ask::result
    sources::Request request;
    // Wait this long before sending (kRequestSpacingMs between requests).
    int waitMs = 0;
  };

  RaceData() = default;
  explicit RaceData(Saved saved, NascarSaved nascar = {});

  // MARK: Lookups (no requests)

  // A driver by a camera's name ("Driver Alpha", "Alpha"): accents, case
  // and " Jr." ignored, else by surname.
  std::optional<RaceDriver> driver(std::string_view name) const;
  // The top 3 of a finished session of the weekend, when known.
  std::optional<std::vector<RaceDriver>> top3(std::string_view kind, Seconds start) const;
  // top3 as the board's podium line (empty when unknown).
  std::vector<board::PodiumDriver> podium(const std::string& kind, Seconds start) const;
  // For board::Input::podium: a copy of the lookups (safe on a worker).
  std::function<std::vector<board::PodiumDriver>(const std::string&, Seconds)> podiumHook() const;
  // A NASCAR driver by an in-car feed's name ("Driver Alpha").
  std::optional<RaceDriver> nascarDriver(std::string_view name) const;

  // MARK: F1 refresh

  // What is due for these sessions (results only with scores on).
  Plan due(const std::vector<WantedSession>& sessions, bool results, Seconds now) const;
  // An F1 weekend is on screen: true when a pass began (drive nextRequest).
  // Cheap to call often: false right away when one is running, a failure
  // pause is on, or nothing is due.
  bool refreshIfNeeded(const std::vector<WantedSession>& sessions, bool results, Seconds now);
  // The pass's next request; nullopt when the pass is over (then save),
  // or while an answer is still awaited.
  // The daily cap ends the pass like a failure.
  std::optional<Request> nextRequest(Seconds now, const time::Zone& zone);
  // The answer to the last request: HTTP status (0 = none) and body.
  void onResponse(int status, std::string_view body, Seconds now);
  bool running() const { return pass_.has_value(); }

  // MARK: NASCAR refresh

  // The cameras panel opened on a NASCAR race: the request when the list is
  // missing or a week old (nullopt otherwise, or while one is out).
  std::optional<sources::Request> refreshNascarIfNeeded(Seconds now);
  // Its answer; parsed here (or parse on a worker and call onNascarParsed).
  void onNascarResponse(int status, std::string_view body, Seconds now);
  void onNascarParsed(std::vector<RaceDriver> drivers, Seconds now);

  // Bumped when drivers or results change.
  int revision() const { return revision_; }
  const Saved& saved() const { return saved_; }
  const NascarSaved& nascarSaved() const { return nascar_; }
  int requestsToday(Seconds now, const time::Zone& zone) const;

 private:
  void rebuildNames();
  void rebuildNascarNames();
  std::optional<sources::Request> take(sources::Request request, Seconds now, const time::Zone& zone);
  void finish(Seconds now);

  Saved saved_;
  NascarSaved nascar_;
  std::map<std::string, RaceDriver> byName_;
  std::map<std::string, RaceDriver> nascarByName_;
  Seconds nextAllowed_ = 0;
  Seconds nascarNextAllowed_ = 0;
  bool nascarRunning_ = false;
  int revision_ = 0;

  struct Pass {
    Plan plan;
    int step = 0;  // 0 sessions, 1 drivers, 2 results
    size_t resultIndex = 0;
    bool changed = false;
    bool failed = false;
    bool awaiting = false;
    Ask ask = Ask::sessions;
    int key = 0;
  };
  std::optional<Pass> pass_;
};

// The sessions of a race weekend card (its parts: kind from the card key's
// last part, start).
std::vector<WantedSession> weekendSessions(const std::vector<SportsEvent>& parts);
// Live Events was rebuilt: race weekends you can watch ask for their drivers
// and results (nothing for weekends not in your channels). True when a pass
// began.
bool refreshForBoard(RaceData& race, const board::Board& board, bool showsScores, Seconds now);

}  // namespace marigold::sports::race_data
