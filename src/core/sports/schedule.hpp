// The game schedule (SportsData.swift): the week ahead for the major
// leagues (teams, logos, colors, start times, venue, week, notes, TV
// networks) from a public scoreboard. No scores are used here.
//
// Kept to a minimum: one request per league for the next week, only when
// Live Events is opened and the saved schedule is from an earlier day (or
// on Refresh). Only picked leagues are asked for. Leagues none of your
// channels showed are skipped and looked at again after a week. Nothing
// about the viewer is sent.
//
// No I/O: requests are values (sources::Request), bodies are parsed from
// string views with the streaming reader, and the cache is a JSON string
// the caller stores.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/sources/common.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/types.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::schedule {

// MARK: Requests

constexpr const char* kApiBase = "https://site.api.espn.com/apis/site/v2/sports/";
constexpr int kTimeoutSeconds = 20;
// Between two requests of one fetch.
constexpr int kRequestSpacingMs = 700;
// Which fields the saved games have: an older schedule is fetched again
// once, even on the same day. 2: team colors, ranks, game tags, countries.
constexpr int kSchema = 2;
// A schedule fetched today and younger than this isn't fetched again.
constexpr Seconds kFreshFor = 20 * 3600;
// Finished games stay published a day and a half: a leftover feed naming
// one matches it and is hidden instead of looking like today's game.
constexpr Seconds kFinishedShown = 36 * 3600;
// Saved games older than this are dropped.
constexpr Seconds kSavedFor = 2 * 86'400;
// School logos (college events in any sport) are fetched once a month.
constexpr Seconds kSchoolsMaxAge = 30 * 86'400;
// Before My Sports is set up, a league none of your channels showed in a
// week is looked at weekly.
constexpr Seconds kSkipFor = 7 * 86'400;
// noteWatchable writes a league at most this often.
constexpr Seconds kWatchableNoteGap = 6 * 3600;
// After a failed fetch: 1 h, 1 h, 6 h, 24 h, then weekly.
constexpr Seconds kBackoff[] = {3600, 3600, 6 * 3600, 24 * 3600, 7 * 86'400};
// Settings defaults ("Game schedule" and "Show games not in my channels").
constexpr bool kEnabledByDefault = true;
constexpr bool kShowsUnwatchableByDefault = true;

// A league's scoreboard: `query` ("dates=202610", "week=9&seasontype=2" or
// "") plus the league's extra; "&limit=1000" unless the plain form is the
// one that works for it.
sources::Request scoreboardRequest(const League& league, std::string_view query, bool plainForm);
// Division I schools (about 360) with their logos.
sources::Request schoolsRequest();

// The queries a league is asked with for the window around `now`: a month
// (one or two), this week (next week is added from the answer), or day by
// day; fight cards also the default listing.
std::vector<std::string> queriesFor(const League& league, Seconds now, const time::Zone& zone);

// MARK: Parsing

struct Document {
  std::vector<SportsGame> games;
  std::optional<int> week;
  std::optional<int> seasonType;
};
// Games of a scoreboard (decoding only the fields used; odd values are
// skipped, never fatal). nullopt when the body has no events list.
std::optional<Document> parseScoreboard(std::string_view body, const League& league);
// One scoreboard event, or nullopt when it lacks an id, a date or two sides.
std::optional<SportsGame> parseGame(const json::Value& event, const League& league);
// Schools from the teams list.
std::optional<std::vector<SportsTeam>> parseSchools(std::string_view body);

struct FightCards {
  std::vector<SportsGame> games;
  std::string summary;  // for the log: no links
};
// One game per part of each card (early prelims, prelims, main card), at
// its first bout's time, with the networks of its bouts. A card with no
// parts in the data is one game.
std::optional<FightCards> parseFightCards(std::string_view body, const League& league);
// One game per session of each race weekend in the current listing.
std::optional<std::vector<SportsGame>> parseRaceWeekends(std::string_view body, const League& league);
// One game per match of the tournaments in the listing.
std::optional<std::vector<SportsGame>> parseTennisMatches(std::string_view body, const League& league);

// The tile's tag: the schedule's note ("World Series - Game 1" -> "World
// Series · Game 1", "NFL Paris Game" -> "Paris"), an NFL night game
// ("Sunday Night", by US Eastern time), or the stage ("League Phase"). A
// note that could say how a game or series is going is never used.
std::optional<std::string> note(const std::vector<std::string>& headlines,
                                const std::optional<std::string>& stage, const League& league,
                                Seconds start);
// "MSC Cruises United States Grand Prix" -> "United States GP".
std::string raceName(std::string_view name);
// "Quarterfinal" -> "QF", "Qualifying 1st Round" -> "Q1", "2nd Round" -> "R2".
std::optional<std::string> tennisRound(const std::optional<std::string>& text);
// The surname a tile shows ("Rybakina"; doubles as is).
std::string tennisShort(const SportsTeam& team);

// Games starting after `cutoff`; a race weekend's sessions stay together
// until its last one is past it.
std::vector<SportsGame> keeping(const std::vector<SportsGame>& games, Seconds cutoff);
// "F1|600057446|401839122" -> "F1|600057446".
std::string weekendKey(const SportsGame& game);

// What one answer of a league gives for the window [from, to].
struct LeagueResult {
  std::vector<SportsGame> games;
  std::vector<SportsTeam> teams;
  std::optional<int> week;
  std::optional<int> seasonType;
  size_t total = 0;
};
std::optional<LeagueResult> parseLeague(std::string_view body, const League& league, Seconds from,
                                        Seconds to);

// MARK: Saved schedule

struct Saved {
  std::vector<SportsGame> games;
  std::optional<Seconds> fetchedAt;
  // League (chip text) -> when it was last fetched.
  std::map<std::string, Seconds> leagueFetched;
  // League -> last time one of its games was on your channels.
  std::map<std::string, Seconds> leagueWatchable;
  // Last attempt that got nothing, so a failing source isn't asked again
  // every time Live Events opens.
  std::optional<Seconds> failedAt;
  // Failed days in a row (backoff).
  std::optional<int> failures;
  // League -> "limit" or "plain": the request form that works.
  std::map<std::string, std::string> leagueForm;
  // Requests made today, for the log.
  std::optional<std::string> requestDay;
  std::optional<int> requestCount;
  // App build the failure count belongs to (reset on update).
  std::optional<std::string> failureBuild;
  // League -> its teams (from the schedule), for picking favorites.
  std::map<std::string, std::vector<SportsTeam>> rosters;
  std::optional<std::vector<SportsTeam>> schools;
  std::optional<Seconds> schoolsFetchedAt;
  // The leagues the saved schedule was fetched for.
  std::optional<std::vector<std::string>> leagueSet;
  std::optional<int> schema;
};
std::string toJson(const Saved& saved);
// nullopt when the text isn't a saved schedule (start empty).
std::optional<Saved> savedFromJson(std::string_view text);

// What Swift reads from globals: My Sports, the sport feeds seen, the
// "Game schedule" switch, the time and the viewer's zone.
struct Context {
  MySportsPrefs prefs;
  SportFeedsSeen seen;
  bool enabled = kEnabledByDefault;
  Seconds now = 0;
  time::Zone zone = time::Zone::device();
};

struct Published {
  // Games of the active leagues, finished ones for a day and a half, by start.
  std::vector<SportsGame> games;
  std::optional<Seconds> updatedAt;
  std::map<std::string, std::vector<SportsTeam>> rosters;
  // Division I schools while a college league is active.
  std::vector<SportsTeam> schools;
};

// The schedule's state and its fetches, without the I/O. Owned by the
// caller (UI thread); one fetch at a time. A fetch is driven by
// nextRequest() / onResponse() until finished() reports it.
class Schedule {
 public:
  explicit Schedule(Saved saved = Saved(), std::string appBuild = "?");

  // Live Events opened: begins a fetch when the saved schedule is from an
  // earlier day, older than 20 h or of an older schema (or a league added
  // since is missing), and not while backing off after failures. True when
  // a fetch began.
  bool refreshIfNeeded(const Context& ctx);
  // Options -> Refresh schedule: every league, now.
  bool refreshNow(const Context& ctx);
  // My Sports changed (or a sport newly seen): look up newly picked
  // leagues (and the schools for college). During a fetch it waits for it.
  bool leaguesChanged(const Context& ctx);
  // Setup: teams for leagues picked there but not saved yet.
  bool requestRosters(const std::set<std::string>& shorts, const Context& ctx);
  // After matching: which leagues had a game on your channels.
  void noteWatchable(const std::set<std::string>& leagues, Seconds now);

  Published publish(const Context& ctx) const;
  // Teams to pick from: the schedule's, plus every school for college leagues.
  std::vector<SportsTeam> teams(const League& league) const;

  // MARK: The fetch in progress

  struct Pending {
    enum class Kind { league, schools };
    Kind kind = Kind::league;
    League league;
    std::string query;
    Seconds from = 0, to = 0;
    sources::Request request;
    // Wait this long before sending (kRequestSpacingMs after the first).
    int waitMs = 0;
  };
  // Parsed on any thread: only `pending` and the body are read.
  struct Answer {
    std::optional<LeagueResult> league;
    std::optional<std::vector<SportsTeam>> schools;
  };

  bool fetching() const { return fetch_.has_value(); }
  // The next request to send; nullopt when none is due (waiting for an
  // answer, or the fetch is over).
  std::optional<Pending> nextRequest();
  static Answer parse(const Pending& pending, std::string_view body);
  // The answer to the last request: HTTP status (0 = no response, which
  // doesn't count toward the backoff) and its parsed body (parse only a 200).
  void onAnswer(int status, Answer answer);
  // Both steps on this thread.
  void onResponse(int status, std::string_view body);

  struct Finished {
    bool succeeded = false;
    // Picks changed during the fetch: call leaguesChanged again.
    bool recheck = false;
    std::string note;  // for the log
  };
  // Set once when a fetch ends (then cleared).
  std::optional<Finished> takeFinished();

  const Saved& saved() const { return saved_; }
  // The saved schedule changed since the last call: write toJson(saved()).
  bool takeDirty();

 private:
  bool start(const Context& ctx, bool force, std::optional<std::set<std::string>> only);
  bool skip(const std::string& league, Seconds now) const;
  void finishLeague();
  void finish();

  Saved saved_;
  std::string appBuild_;
  bool dirty_ = false;
  bool recheckPending_ = false;
  std::set<std::string> wantedRosters_;
  std::optional<Finished> finished_;
  struct FetchState {
    bool force = false;
    std::optional<std::set<std::string>> only;
    Seconds now = 0, from = 0, to = 0;
    time::Zone zone = time::Zone::utc();
    std::vector<League> active;
    bool needsSchools = false;
    bool configured = false;
    std::map<std::string, std::vector<SportsGame>> byLeague;
    std::map<std::string, std::vector<SportsTeam>> rosters;
    std::vector<std::string> succeeded;
    bool fetchedAny = false;
    int leaguesAsked = 0;
    int sent = 0;
    bool networkTrouble = false;
    size_t leagueIndex = 0;
    bool inLeague = false;
    std::vector<std::string> queries;
    size_t queryIndex = 0;
    bool retrying = false;
    bool plainForm = false;
    std::vector<SportsGame> list;
    std::map<std::string, SportsTeam> roster;
    bool ok = false;
    bool schoolsAsked = false;
    bool awaiting = false;
    bool awaitingSchools = false;
  };
  std::optional<FetchState> fetch_;
};

}  // namespace marigold::sports::schedule
