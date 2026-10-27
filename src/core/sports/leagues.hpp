// The leagues the schedule covers (SportsData.League and its tables in
// SportsData.swift), and which of them are looked up: My Sports' picks only,
// or the standard set until My Sports is set up.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/util/json.hpp"
#include "core/util/time.hpp"

namespace marigold::sports {

struct MySportsPrefs;

struct League {
  // How the week ahead is asked for: a month (one or two requests), this
  // week and next (college football: a month comes back cut short), day by
  // day (college basketball has too many games), or the current listing.
  enum class Mode { month, week, days, current };

  std::string path;        // "football/nfl"
  std::string group;       // row group: "NFL", "College Football", "Soccer"
  std::string shortName;   // id and chip text: "NFL", "EPL" ("short" in Swift)
  std::string name;        // name in My Sports: "Premier League"
  std::string sport;       // section in My Sports: "Football", "Basketball"
  bool soccer = false;
  std::string extra;       // extra query ("&groups=80")
  Mode mode = Mode::month;
  int days = 0;            // Mode::days
  bool standard = false;   // fetched before My Sports is set up
  // Words in provider event titles that mean this league (for suggestions);
  // empty means the row group decides.
  std::vector<std::string> keywords;
  bool fights = false;     // a promotion: events are cards of many bouts
  bool racing = false;     // each event is a race weekend of sessions
  bool tennis = false;     // each event is a tournament of matches
  bool golf = false;       // each event is a tournament, its competitors a leaderboard

  bool college() const { return group.rfind("College", 0) == 0; }
};

const std::vector<League>& leagues();
// Fight schedules, looked up when "UFC, boxing & wrestling" is picked.
const std::vector<League>& fightLeagues();
// Racing: the current race weekend (one request lists every session).
const std::vector<League>& racingLeagues();
// Tennis: this week's tournaments (one request per tour lists every match).
const std::vector<League>& tennisLeagues();
// Sports picked as a whole in My Sports, each with its own leagues (picked
// with "S:Rugby", "S:Volleyball"...): rugby's competitions, college
// volleyball, lacrosse (college and PLL), field hockey and softball. Team
// games, read like the leagues above.
const std::vector<League>& sportLeagues(std::string_view sportID);
// Every league of those sports.
const std::vector<League>& teamSportLeagues();
// Golf: this week's tournament on each tour (PGA Tour, LPGA, LIV, DP World).
const std::vector<League>& golfLeagues();
// leagues + fights + racing + tennis + golf + teamSportLeagues.
const std::vector<League>& allLeagues();
const League* leagueByShort(std::string_view shortName);

// Racing and tennis feeds seen in your channels (UserDefaults dates in
// Swift; the caller stores this). A sport seen in the last two weeks is
// looked up even when My Sports doesn't pick it.
struct SportFeedsSeen {
  std::optional<time::Seconds> racing;  // F1
  std::optional<time::Seconds> tennis;
  std::optional<time::Seconds> nascar;
  std::optional<time::Seconds> golf;

  static constexpr time::Seconds kWindow = 14 * 86'400;
  bool racingSeen(time::Seconds now) const { return racing && now - *racing < kWindow; }
  bool tennisSeen(time::Seconds now) const { return tennis && now - *tennis < kWindow; }
  bool nascarSeen(time::Seconds now) const { return nascar && now - *nascar < kWindow; }
  bool golfSeen(time::Seconds now) const { return golf && now - *golf < kWindow; }

  bool operator==(const SportFeedsSeen& o) const {
    return racing == o.racing && tennis == o.tennis && nascar == o.nascar && golf == o.golf;
  }
};
json::Value toJson(const SportFeedsSeen& seen);
SportFeedsSeen sportFeedsSeenFromJson(const json::Value& v);

// The leagues looked up: My Sports' picks, or the standard set (plus
// racing, and tennis when seen) until it's set up.
std::vector<League> activeLeagues(const MySportsPrefs& prefs, const SportFeedsSeen& seen,
                                  time::Seconds now);

// Live Events opened: look for F1, NASCAR and tennis feeds among the event
// titles, at most every 6 hours (`checkedAt` is that throttle, kept in
// memory by the caller). True when a sport is newly seen, so the caller
// calls Schedule::leaguesChanged.
bool noteSportFeeds(SportFeedsSeen& seen, std::optional<time::Seconds>& checkedAt,
                    const std::vector<catalog::LiveEvent>& events, time::Seconds now);

}  // namespace marigold::sports
