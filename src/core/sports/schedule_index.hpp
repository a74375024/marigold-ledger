// "Is there a scheduled game between these sides (or an event of this
// title) within a few days, and when?" over the merged schedule: the
// scoreboard's games, the NCAA pages' and the other calendars' (whatever
// Schedule::publish() gives). For a provider slot no tile took: a game that
// is on the schedule on another day is a leftover title or a replay, not a
// game of its own.
//
// Built once from a list of games (on a worker; a few thousand games take a
// few milliseconds) and read from any thread. Sides are compared by their
// normalised words (case, accents and punctuation folded; "FC", "IF" and the
// like left out), the shorter name's words all in the longer's: "Mjallby"
// finds "Mjällby AIF", "Man City" doesn't find "Manchester City".
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/sports/types.hpp"

namespace marigold::sports::schedule {

struct Scheduled {
  std::string id;           // the game's id ("SWE2|2524001")
  std::string leagueShort;  // "SWE2"
  Seconds start = 0;
  bool completed = false;
};

class GameIndex {
 public:
  GameIndex() = default;
  explicit GameIndex(const std::vector<SportsGame>& games);

  // The game between `a` and `b` (either way round) starting within
  // `within` seconds of `around`, the nearest.
  std::optional<Scheduled> find(std::string_view a, std::string_view b, Seconds around, Seconds within) const;
  // An event with no two sides ("Players Championship 33", "6 Hours of
  // Monza · Race"): the title's words (two at least) all in the event's,
  // or the event's in the title's.
  std::optional<Scheduled> findTitle(std::string_view title, Seconds around, Seconds within) const;
  size_t size() const { return entries_.size(); }

  // The words a name is compared by.
  static std::vector<std::string> words(std::string_view name);

 private:
  struct Entry {
    Scheduled game;
    // Each side's names (name, short name, alternates), as words.
    std::vector<std::vector<std::string>> home, away;
    std::vector<std::string> title;  // a title event's words; empty for two sides
  };
  std::vector<uint32_t> candidates(const std::vector<std::string>& words) const;

  std::vector<Entry> entries_;
  std::unordered_map<std::string, std::vector<uint32_t>> byWord_;
};

}  // namespace marigold::sports::schedule
