// Build 167 (1.0.11), replay in Live Events: when a game can be watched
// from its start on a guide channel's own archive (the replay of Builds
// 165-166), and on which channel. Worked out once when the board is made
// (events_build::board), never per frame: the tile's mark, the focused
// game's "Replay" chip, "Start over" in the hold-OK menu of a live game,
// "Watch replay" on a Final's card and the "Full games" row all read it.
//
// A game gets replay only when all of these hold:
//   (a) one of its sources is a guide channel (My Lineup or your channels;
//       not a provider event feed, not a team channel's "likely") whose
//       guide listing shows the game at its start (the listing begins no
//       later than kListingSlack after the scheduled start and hasn't ended
//       by it);
//   (b) one of that channel's own copies keeps an archive that Test all
//       sources hasn't found broken (Channel::days > 0);
//   (c) the scheduled start less a minute is inside that archive as replay
//       uses it (replay::covers: the days less an hour): on one day, a game
//       that started up to 23 h ago (less the minute), on two, 47 h;
//   (d) the channel is in the language of the game's best live source (the
//       one OK plays, languages::lead): "" (not known, mostly English
//       channels with no mark) counts as English;
//   (e) the game is live, likely over or final (not upcoming, postponed or
//       of no time).
// Several channels qualify: the longest archive, then My Lineup, then the
// one OK plays, then the order of the game's sources.
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

#include "core/sports/types.hpp"
#include "core/util/time.hpp"

namespace marigold::engine::game_replay {

// A guide listing that starts this long after the game's scheduled start
// still shows it at its start (listings are often on the half hour).
constexpr time::Seconds kListingSlack = 5 * 60;
// A game's replay starts this long before its scheduled start.
constexpr time::Seconds kBeforeStart = 60;

// What the board knows of a guide channel that could serve a replay (made
// on the UI thread by the store, read on the board's worker).
struct Channel {
  // The longest archive among the channel's own copies (Xtream catch-up
  // days), leaving out a copy Test all sources found broken; 0: none.
  uint16_t days = 0;
  bool lineup = false;  // in My Lineup
  std::string name;     // as the guide shows it ("ESPN")
};
// By guide entry id (EventSource::channelID of a lineup or guide source).
using Channels = std::unordered_map<std::string, Channel>;

// The channel a game's replay plays on.
struct Pick {
  std::string channelId;    // the guide entry
  std::string channelName;  // "ESPN"
  time::Seconds start = 0;  // the game's scheduled start
  time::Seconds from = 0;   // where the replay starts: a minute before it, to the minute
  uint16_t days = 0;        // the channel's archive (Channel::days)
  // Not the channel OK plays (the menu then says " · on ESPN").
  bool otherChannel = false;
};

// Two source languages are the same: equal, with "" taken for English.
bool sameLanguage(std::string_view a, std::string_view b);

// The rule above for `event` in `state` at `now`; `viewer`: the viewer's
// languages (languages::ofViewer), for the source OK plays. nullopt: no
// replay.
std::optional<Pick> pick(const sports::SportsEvent& event, sports::SportsEvent::State state, time::Seconds now,
                         const Channels& channels, const std::set<std::string>& viewer);

// "Started 7:10 PM", plus " · on ESPN" when the replay comes from another
// channel than the one OK plays: the hold-OK menu's Start over line.
std::string startedLine(const Pick& p, const time::Zone& zone, bool twelveHour);

}  // namespace marigold::engine::game_replay
