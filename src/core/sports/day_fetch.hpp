// Build 176 (1.0.19), Live Events by day: one past day's games, fetched
// when the viewer picks that day on the board (never in the background).
// The schedule keeps finished games only a day and a half
// (schedule::kFinishedShown); a whole past day needs that day's own answers,
// which carry the finals.
//
// Kept as gentle as the schedule's own fetch:
// - one request per league that can be asked for a date (the scoreboard's
//   "dates=YYYYMMDD", the NCAA's day pages); cricket's one listing has no
//   date, and the other calendars (calendars.hpp) aren't asked again: their
//   saved games of that day are used as they are;
// - one at a time, schedule::kRequestSpacingMs apart, in the form the
//   schedule learned works for the league (the plain form after a 400);
// - a refusal (429, 403) leaves that source alone for the rest of the day
//   (refusals::until), its other leagues skipped, and the schedule told;
// - a day that came is kept for the session (DayCache, the last kKept days)
//   and not fetched again for kFreshFor; a day that failed is fetched again
//   on a re-pick only after a backoff once it failed twice in a row (Try
//   again always asks, refusals still apply).
// Nothing about the viewer is sent; the source is never named on screen.
//
// No I/O: requests are values, bodies are parsed from string views.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/sources/common.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/schedule.hpp"
#include "core/sports/scores.hpp"
#include "core/sports/types.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::days {

// A league whose games can be asked for by date (everything the
// scoreboard and the NCAA's pages list; not cricket's one listing).
bool byDate(const League& league);
// "20261116": the viewer's local day starting at `day`.
std::string dateOf(Seconds day, const time::Zone& zone);
// The request for one league's day: the schedule's scoreboard request with
// "dates=" (the plain form when that's the one that works for it).
sources::Request request(const League& league, std::string_view date, bool plainForm);

// One league's answer: its games starting in [from, to) and their statuses
// (finals carry the score). nullopt games: the body isn't a listing.
struct Answer {
  std::optional<schedule::LeagueResult> games;
  scores::Statuses statuses;
};
Answer parse(const League& league, std::string_view body, Seconds from, Seconds to);

// One day's games as fetched.
struct Day {
  Seconds day = 0;         // the local day's start
  Seconds fetchedAt = 0;
  std::vector<SportsGame> games;
  scores::Statuses statuses;  // each game's, by game id (finals with their score)
  int leagues = 0;    // asked (or skipped, refused)
  int answered = 0;   // answered with a listing (an empty day counts)
  int failed = 0;
  // Every league asked failed (or was refused): nothing to show.
  bool failedAll() const { return leagues > 0 && answered == 0; }
};

// One day's fetch, driven like the schedule's: nextRequest() / onAnswer()
// until takeFinished() gives the day. One at a time.
class DayFetch {
 public:
  struct Pending {
    League league;
    std::string date;
    sources::Request request;
    Seconds from = 0, to = 0;
    int waitMs = 0;  // wait this long before sending
  };

  // Begins the day starting at `day` (local) for `active` (activeLeagues):
  // a request for each that can be asked by date and whose source isn't
  // refusing us (`refusedUntil`, the schedule's), in its known form
  // (`forms`: league -> "plain" or "limit"). False when there is nothing to
  // ask (it finishes at once with what it has: no league).
  bool begin(Seconds day, const std::vector<League>& active, const std::map<std::string, Seconds>& refusedUntil,
             const std::map<std::string, std::string>& forms, Seconds now, const time::Zone& zone);
  bool busy() const { return state_.has_value(); }
  Seconds day() const { return state_ ? state_->day.day : 0; }
  // Drops the fetch under way (another day picked, Live Events closed): an
  // answer still out is ignored.
  void cancel() { state_.reset(); }

  std::optional<Pending> nextRequest();
  // The answer to the last request: status (0: none), its body parsed (only
  // a 200's) and Retry-After.
  void onAnswer(int status, Answer answer, int retryAfterSeconds = 0);
  // Both steps here.
  void onResponse(int status, std::string_view body, int retryAfterSeconds = 0);

  // Set once when the day is done.
  std::optional<Day> takeFinished();
  // Sources that refused us during it ("espn.com" -> until): the schedule
  // leaves them alone too.
  std::map<std::string, Seconds> takeRefusals();
  // Leagues that answered 200 only in the plain form (the schedule learns it).
  std::set<std::string> takePlain();

  // For the log: "Monday's games, 12 leagues, fine" (never a link).
  static std::string note(const Day& day, const time::Zone& zone);

 private:
  struct State {
    Day day;
    Seconds now = 0;
    time::Zone zone = time::Zone::utc();
    std::map<std::string, Seconds> refused;
    std::vector<League> leagues;
    std::set<std::string> plainKnown;
    size_t index = 0;
    bool awaiting = false;
    bool retrying = false;  // the plain form after a 400
    int sent = 0;
    std::optional<Pending> current;
  };
  void finish();
  std::optional<State> state_;
  std::optional<Day> finished_;
  std::map<std::string, Seconds> refusals_;
  std::set<std::string> plain_;
};

// The days fetched this session: the last kKept, each fresh for kFreshFor.
class DayCache {
 public:
  static constexpr size_t kKept = 3;
  static constexpr Seconds kFreshFor = 3600;
  const Day* find(Seconds day) const;
  // A re-pick shows it without asking again.
  bool fresh(Seconds day, Seconds now) const;
  // A re-pick (not Try again) may ask again: never while failing twice in
  // a row within the schedule's backoff (1 h, 1 h, 6 h, 24 h, then weekly).
  bool mayFetch(Seconds now) const;
  // Keeps it (a failed day is noted, never kept over one that came).
  void put(Day day);
  void clear() { days_.clear(); }
  size_t size() const { return days_.size(); }
  int failures() const { return failures_; }

 private:
  std::vector<Day> days_;  // newest last
  int failures_ = 0;
  Seconds failedAt_ = 0;
};

}  // namespace marigold::sports::days
