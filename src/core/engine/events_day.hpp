// Build 176 (1.0.19), Live Events by day: the board for another day than
// today, picked from the "Today ▼" button beside Filter (a leading panel,
// GO TO, "A day of games", as the guide's time menu).
//
//   - The days: past days as far back as the longest catch-up archive among
//     your channels reaches (replay::covers: its days less an hour), a day
//     listed while any part of it is still inside it; Today, always; then
//     the days ahead the schedule has games for (never an empty one).
//   - A past day's games are fetched when picked (sports/day_fetch.hpp):
//     its finals and scores from that day's own answers, never made up.
//     Where each was on: the sources the board matched while it was current
//     (kept by ReplaySources once the guide has moved on), else the
//     channels airing its networks, given a listing at its start when the
//     guide still holds one (Store::pastListing). Its replay is
//     game_replay::pick as on today's board, the game a final (rule (e)).
//   - A past day's board: Your teams then each sport (no Live now, Starting
//     soon or Later today), the games with replay first and the rest dimmed
//     at the end of each row, each tile's footer saying why (pastTile()).
//     OK on a game with replay plays it from the start (Watch replay's
//     path); on a dimmed one it opens the game's card.
//   - A day ahead's board: the schedule's games of that day, by sport.
//   - Back on another day goes to Today first; leaving Live Events and
//     coming back shows Today.
// Strings never name a data source; nothing here counts channels.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/engine/events_model.hpp"
#include "core/engine/game_replay.hpp"
#include "core/sports/types.hpp"
#include "core/util/time.hpp"

namespace marigold::engine::events_day {

// Days ahead looked at for games (the schedule's week).
constexpr int kDaysAhead = 7;
// A replay leaving the archive within this long says so on its tile.
constexpr time::Seconds kReplayEndsWithin = 3 * 3600;

// "Yesterday", "Today", "Tomorrow", else the weekday ("Sunday"): the day
// button and the panel's rows.
std::string dayWord(time::Seconds day, time::Seconds now, const time::Zone& zone);
// The panel's second line: "Mon, Nov 16" beside Yesterday, Today and
// Tomorrow; "Nov 19" beside a weekday's name.
std::string dayDate(time::Seconds day, time::Seconds now, const time::Zone& zone);
// The board's top right on another day: "Monday, Nov 16".
std::string dayTitle(time::Seconds day, const time::Zone& zone);
// Under the title while the day's games come: "Getting Monday's games…";
// when they couldn't: "Monday's games couldn't be loaded."; none in your
// sports: "No games on Monday in your sports."
std::string loadingLine(time::Seconds day, const time::Zone& zone);
std::string failedLine(time::Seconds day, const time::Zone& zone);
std::string emptyLine(time::Seconds day, const time::Zone& zone);
// The hero with no game focused: "Monday's games. The ones you can watch
// from the start come first." / "Thursday's games, and where you can
// watch them."
std::string introLine(time::Seconds day, bool past, const time::Zone& zone);

// The panel's rows, oldest first: past days back to what `longestArchive`
// (days; 0 none) still holds of them at `now`, Today, then the days ahead
// within kDaysAhead with a game starting in them (`starts`). `shown`: the
// day the board shows (0 or today's start: Today).
std::vector<DayRow> dayRows(time::Seconds now, const time::Zone& zone, uint16_t longestArchive,
                            const std::vector<time::Seconds>& starts, time::Seconds shown);
// The local start of the day `t` is in.
time::Seconds dayOf(time::Seconds t, const time::Zone& zone);
// The next day's start.
time::Seconds nextDay(time::Seconds day, const time::Zone& zone);

// When a replay from `from` on a channel keeping `days` leaves its archive
// (the first moment replay::covers no longer holds it).
time::Seconds replayLeaves(time::Seconds from, uint16_t days);
// "replay ends in 2 h", "replay ends in 40 min" within kReplayEndsWithin;
// "" otherwise.
std::string replayEnds(time::Seconds leaves, time::Seconds now);
// "replay until 11:14 PM", "replay until tomorrow 5:15 PM", "replay until
// Thursday 5:15 PM".
std::string replayUntil(time::Seconds leaves, time::Seconds now, const time::Zone& zone, bool twelveHour);

// A past day's games as Live Events' events ("sev|" + the game's id), each
// with where it was on: `known` (the sources the board matched, or
// remembered: ReplaySources), then the channels airing its networks
// (`network`, network_map::networkSources) not already among them.
std::vector<sports::SportsEvent> pastEvents(
    const std::vector<sports::SportsGame>& games,
    const std::function<std::vector<sports::EventSource>(const sports::SportsGame&)>& known,
    const std::map<std::string, std::vector<sports::EventSource>>& network);

// A past day's tile made with its board: with replay (`pick`), the footer's
// start time (or "replay ends in…", amber) and the channel with the mark,
// and the hero's lines ("On ESPN, 5:15 – 8:31 PM · replay until tomorrow
// 5:15 PM"); without, dimmed, "No replay" or "Not on your channels". The
// hero: the matchup as its title, the score (as the settings show it) and
// the venue after the Final pill.
void pastTile(EventTile& t, const sports::SportsEvent& event, const std::optional<game_replay::Pick>& pick,
              time::Seconds now, const time::Zone& zone, bool twelveHour);

// Where each game was at its start: the guide channels whose listing had
// it (game_replay's rule (a)), noted from every board's games while they
// are current, kept for kKeptFor so a past day still finds them once the
// guide has moved on. Saved small (kMost games, kPerGame sources each).
class ReplaySources {
 public:
  static constexpr time::Seconds kKeptFor = 8 * 86400;
  static constexpr size_t kMost = 800;
  static constexpr size_t kPerGame = 2;

  // Notes the started games' listed guide sources (and forgets the old).
  // True when anything changed.
  bool note(const std::vector<sports::SportsEvent>& events, time::Seconds now);
  // nullptr: none noted for this game.
  const std::vector<sports::EventSource>* find(const std::string& gameId) const;
  size_t size() const { return games_.size(); }

  std::string toJson() const;
  static ReplaySources fromJson(std::string_view text, time::Seconds now);

 private:
  struct Seen {
    time::Seconds start = 0;
    std::vector<sports::EventSource> sources;
  };
  void trim(time::Seconds now);
  std::map<std::string, Seen> games_;
};

}  // namespace marigold::engine::events_day
