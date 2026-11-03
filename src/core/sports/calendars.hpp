// Event calendars for competitions the scoreboard (schedule.hpp) doesn't
// list: smaller soccer leagues (Albania, Poland, Sweden's second tier, Spain's
// third, Mauritania, Burundi, Lithuania), Danish and Swedish second-tier
// hockey, endurance racing, MotoGP, Supercars, PDC darts, boxing, Oktagon,
// the CFL, snooker, and UFC when the scoreboard's own fight cards aren't
// looked up. Read from TheSportsDB's free tier (the public test key "123").
//
// A gap source, kept small:
// - a competition is asked for only when it fits the viewer's picked sports
//   (the same rule as the scoreboard's leagues) and one of your provider's
//   event feeds named it in the last two weeks;
// - once a day per competition, its next event (which says the round and
//   season); its round only when that changed or the saved copy is two days
//   old, and the next round when this one ends within the week; snooker's
//   day once;
// - at most kDailyCap requests a day, kRequestSpacingMs apart, one at a time,
//   after the scoreboard's games for today and tomorrow;
// - a refusal (429, 403) leaves it alone for the rest of the day (or the
//   Retry-After); errors back off like the scoreboard (1 h ... weekly).
// No live scores come from here: these games are live by the clock. Nothing
// about the viewer is sent; the source is never named on screen.
//
// No I/O: requests are values, bodies are parsed from string views with the
// streaming reader, and the cache is part of the saved schedule.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/sources/common.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/types.hpp"
#include "core/util/json.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::calendars {

constexpr const char* kApiBase = "https://www.thesportsdb.com/api/v1/json/123/";
constexpr int kTimeoutSeconds = 20;
// Between two requests to this source (it allows about 30 a minute).
constexpr int kRequestSpacingMs = 2000;
constexpr int kDailyCap = 40;
// A round fetched this long ago is fetched again when it's the current one.
constexpr time::Seconds kRoundFreshFor = 2 * 86'400;
// Games this far ahead are kept and published (the scoreboard's window).
constexpr time::Seconds kAhead = 8 * 86'400;
// Saved games, at most this many per competition and in all.
constexpr size_t kMaxPerLeague = 80;
constexpr size_t kMaxGames = 600;
// Which fields the saved calendars have: older ones start empty.
constexpr int kSchema = 1;

struct Calendar {
  // How a competition's events read.
  enum class Kind {
    teams,     // two teams a game (soccer, hockey, the CFL)
    sessions,  // a race weekend's sessions, each an event of the round
    cards,     // a fight card named after its main event; one a round
    titles,    // a tournament day with no two sides (darts); one a round
    day,       // a sport's events of the day, no league (snooker)
  };
  std::string key;        // "4403" (the league id), or "snooker"
  std::string shortName;  // the game's leagueShort ("SWE2"); never a scoreboard league's
  std::string name;       // the tile's tag ("Superettan")
  std::string group;      // row group / profile: "Soccer", "Hockey", "Motorsport"...
  Kind kind = Kind::teams;
  // The My Sports pick that makes it relevant: "soccer" (any soccer
  // league), a league ("NHL"), a sport ("S:Darts"); several with '|';
  // empty: seen is enough (snooker has no pick).
  std::string pick;
  // Lower-case words of feed titles that name it.
  std::vector<std::string> keywords;
  // Events listed with no time (fight cards, darts): this hour, UTC.
  int dateOnlyHourUtc = 18;
  // "MotoGP" weekends read "Qatar" in the data: shown as "Qatar GP".
  std::string weekendSuffix;
  // The scoreboard league that has the same events: then this one isn't
  // asked for ("UFC").
  std::string sameAs;
};

const std::vector<Calendar>& calendars();
const Calendar* byKey(std::string_view key);
const Calendar* byShort(std::string_view shortName);
// A game of one of these calendars ("listed only when you can watch it",
// never polled for scores).
bool isCalendarGame(const SportsGame& game);

// The calendars looked up: relevant to the picks (or any, before My Sports
// is set up), seen in feeds lately, and not `covered` (keys). Build 127:
// never one whose games are hidden (its own id, or a soccer calendar's
// "S:SoccerAll").
std::vector<const Calendar*> active(const MySportsPrefs& prefs, const SportFeedsSeen& seen, time::Seconds now,
                                    const std::set<std::string>& covered);
// Keys of the calendars the scoreboard already has: one standing in for a
// scoreboard league looked up (`scoreboard`: short names, "UFC"), or a
// competition of the all-soccer listing (`competitions`, lower case:
// "polish ekstraklasa").
std::set<std::string> coveredBy(const std::set<std::string>& scoreboard, const std::set<std::string>& competitions);

// Keys of the calendars the event feeds name (leagues.cpp's noteSportFeeds).
std::set<std::string> namedIn(const std::vector<catalog::LiveEvent>& events);

// MARK: Requests

struct Ask {
  enum class What { next, round, day };
  What what = What::next;
  std::string key;     // the calendar
  std::string season;  // round: "2026-2027"
  int round = 0;
  bool following = false;  // round: the one after the current
  std::string day;         // day: "2026-11-02"
  sources::Request request;
};
sources::Request nextRequest(const Calendar& c);
sources::Request roundRequest(const Calendar& c, int round, std::string_view season);
// A day's events of a league (at most three on this tier), or of a sport.
sources::Request dayRequest(const Calendar& c, std::string_view day);

// MARK: Parsing

struct Parsed {
  std::vector<SportsGame> games;
  // The first event's season and round ("2026-2027", 11; 0 = no rounds).
  std::optional<std::string> season;
  std::optional<int> round;
};
// A body's events (an empty list for "events": null); nullopt when the
// body isn't one.
std::optional<Parsed> parse(std::string_view body, const Calendar& c);
// "2026-11-06T09:30:00" (UTC, optional "Z" or "+00:00").
std::optional<time::Seconds> timestamp(std::string_view text);
// A race weekend's name and a session's: "6 Hours of Monza Free Practice 1"
// -> ("6 Hours of Monza", "Free Practice 1"); "Qatar GP" -> ("Qatar", "").
std::pair<std::string, std::string> weekendAndSession(std::string_view event);
// Two fighters from a card's name: "Oktagon 97 Severino vs Holzer" ->
// ("Severino", "Holzer"); nullopt without " vs ".
std::optional<std::pair<std::string, std::string>> fighters(std::string_view card);

// MARK: Saved

struct LeagueState {
  std::string season;
  std::optional<int> round;  // of the last next event (0: no rounds)
  std::optional<time::Seconds> checkedAt;
  // "season|round" -> when it was fetched.
  std::map<std::string, time::Seconds> rounds;
  // Events by where they came from ("next", "round:2026|8", "day:2026-11-02"),
  // each with when it was fetched; newer ones win.
  struct Bucket {
    time::Seconds fetchedAt = 0;
    std::vector<SportsGame> games;
  };
  std::map<std::string, Bucket> buckets;
};

struct Saved {
  std::map<std::string, LeagueState> leagues;
  std::optional<std::string> requestDay;
  int requestCount = 0;
  std::optional<time::Seconds> failedAt;
  int failures = 0;
  std::optional<time::Seconds> refusedUntil;
};
json::Value toJson(const Saved& saved);
// Empty for another schema or a value that isn't one.
Saved savedFromJson(const json::Value& v);

// Every saved game of these calendars, newest copy of each, from
// kSavedFor ago to kAhead ahead, by start.
std::vector<SportsGame> games(const Saved& saved, const std::vector<const Calendar*>& active, time::Seconds now);

// MARK: A day's pass

// Asks one at a time (nextRequest, then onAnswer) until next() says none.
class Pass {
 public:
  // `force`: every calendar again (Refresh), still within the daily cap.
  Pass(const Saved& saved, std::vector<const Calendar*> leagues, time::Seconds now, const time::Zone& zone,
       bool force);
  // Something left to ask, before the cap and any refusal.
  bool pending(const Saved& saved) const;
  std::optional<Ask> next(Saved& saved);
  // The answer to the last ask: status (0: no response), its body parsed
  // (only a 200's), Retry-After.
  void onAnswer(Saved& saved, int status, std::optional<Parsed> parsed, int retryAfterSeconds);
  bool awaiting() const { return awaiting_.has_value(); }
  const Ask* current() const { return awaiting_ ? &*awaiting_ : nullptr; }
  int asked() const { return asked_; }
  // Ended early (network down, refused, failing, capped).
  bool stopped() const { return stopped_; }

  // True when the pass would ask anything for these calendars now (not
  // checked today, not refused, not backing off, under the cap).
  static bool due(const Saved& saved, const std::vector<const Calendar*>& leagues, time::Seconds now,
                  const time::Zone& zone, bool force);

 private:
  void queueFollowUps(const Saved& saved, const Ask& ask, const Parsed& parsed);
  bool fresh(const Saved& saved, const std::string& key, const std::string& roundKey) const;

  std::vector<const Calendar*> leagues_;
  time::Seconds now_ = 0;
  time::Zone zone_ = time::Zone::utc();
  std::vector<Ask> first_;   // next events and days
  std::vector<Ask> later_;   // rounds
  size_t firstIndex_ = 0, laterIndex_ = 0;
  std::set<std::string> queued_;
  std::optional<Ask> awaiting_;
  int asked_ = 0;
  int errors_ = 0;
  bool stopped_ = false;
};

}  // namespace marigold::sports::calendars
