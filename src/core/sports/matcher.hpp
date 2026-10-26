// Matches the schedule's games with your channels, provider event feeds and
// guide listings (SportsMatcher in SportsEvents.swift). Pure; runs on a
// worker.
//
// Cost is kept linear: provider events that don't look like a game skip
// matching, and team names are looked up through an index (first word ->
// names) instead of comparing every title with every game.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/sports/types.hpp"
#include "core/util/json.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::matcher {

struct GuideListing {
  std::string entryID;
  bool inLineup = false;
  bool spanish = false;
  std::string title;
  Seconds start = 0;
  Seconds end = 0;
  // For a plain sports title ("NBA Basketball"): the start of its
  // description, when it may name the teams.
  std::optional<std::string> detail;
  // Where it's played, from the description, normalized: identifies the
  // game for titles that never name the teams.
  std::optional<std::string> venue;
  std::optional<std::string> city;
  // The station's rank (station_rank).
  std::optional<int> rank;

  bool operator==(const GuideListing& o) const {
    return entryID == o.entryID && inLineup == o.inLineup && spanish == o.spanish &&
           title == o.title && start == o.start && end == o.end && detail == o.detail &&
           venue == o.venue && city == o.city && rank == o.rank;
  }
  bool operator!=(const GuideListing& o) const { return !(*this == o); }
};
json::Value toJson(const GuideListing& listing);
std::optional<GuideListing> listingFromJson(const json::Value& v);

// The venue and city in a guide description: "From War Memorial Stadium in
// Laramie, Wyo." -> ("war memorial stadium", "laramie").
std::optional<std::pair<std::string, std::string>> venueOf(std::string_view description);

// "NBA Basketball", "Live: College Football": a sport with no teams.
bool isGenericSportsTitle(std::string_view title);

struct ProviderEvent {
  std::string id;
  std::string title;
  std::string league;
  std::optional<Seconds> start;
  std::vector<std::string> feeds;
  std::string family;
  bool spanish = false;
  // Only a clock time was given; the day is a guess.
  bool guessed = false;
};

// Teams of one league (from the schedule), for naming teams in titles no
// game matched.
struct RosterLeague {
  std::string shortName;
  std::string group;
  bool college = false;
  std::vector<SportsTeam> teams;
};

// Leagues whose every game is in the schedule: two of their teams in a
// title with no scheduled game is a replay or an old slot name.
const std::set<std::string>& completeLeagues();
const std::set<std::string>& supportedLeagues();

struct Stats {
  int events = 0;
  int filtered = 0;
  int matchedEvents = 0;
  int listings = 0;
  int matchedListings = 0;
  int games = 0;
  int schoolPairs = 0;
  int namePairs = 0;
  // Airings of a scheduled matchup away from its game (replays).
  int replays = 0;
  // Two teams of a complete league with no game between them.
  int stale = 0;
  int guessedMatched = 0;
  // Plain feeds merged into another tile with the same title and time.
  int mergedPlain = 0;
  int venueListings = 0;
  int describedListings = 0;
  int trimmedStations = 0;
  // Fight feeds joined to a card part (from the schedule or each other).
  int fightFeeds = 0;
  std::vector<std::string> offsets;
  std::map<std::string, int> milliseconds;
  std::optional<std::string> aborted;

  // One line for the log (no titles, no links).
  std::string summary() const;
};

struct BuildInput {
  std::vector<SportsGame> games;
  // Game id -> channels airing its network (network_map::networkSources).
  std::map<std::string, std::vector<EventSource>> channelSources;
  std::vector<ProviderEvent> events;
  std::vector<GuideListing> listings;
  bool preferSpanish = false;
  Seconds now = 0;
  // The viewer's zone (Calendar.current): "before today" for stale feeds.
  time::Zone zone = time::Zone::device();
  // Game id -> the tile id it was shown under last time, so tiles keep their
  // identity (and focus) across rebuilds.
  std::map<std::string, std::string> previousIDs;
  std::vector<SportsTeam> schools;
  std::vector<RosterLeague> rosters;
  // Checked between steps (memory guard); a reason stops the build.
  std::function<std::optional<std::string>()> shouldStop;
  // SpecialFeeds.isCameraName: a race camera feed (goes to the race's
  // cameras panel, not its tile). Cameras are a later milestone: unset
  // means no feed is a camera.
  std::function<bool(std::string_view)> isCameraName;
};

struct BuildResult {
  std::vector<SportsEvent> events;
  Stats stats;
};

// Matches everything, then orders each event's sources best first (My
// Lineup, other channels, provider feeds, team channels; Spanish after
// English unless preferSpanish; stations beyond the best three counted in
// moreStations) and the events by start, title and id. nullopt when
// shouldStop stopped it.
std::optional<BuildResult> build(const BuildInput& input);

// The 2 s budget for reading guide listings.
constexpr int kListingsBudgetMs = 2000;

// MARK: Fight cards

struct FightKey {
  std::string org;     // "ufc", "pfl", "bkfc"
  std::string number;  // "333", or "" for a Fight Night or no number
  std::optional<std::string> part;  // "early", "prelims", "main"
  bool fightPass = false;           // "Fight Pass Prelims": UFC's early prelims
  bool operator==(const FightKey& o) const {
    return org == o.org && number == o.number && part == o.part && fightPass == o.fightPass;
  }
};

// The card a fight feed is for: "UFC 333: FIGHT PASS PRELIMS", "(ESPANOL)
// UFC 333: ... PRELIMINARES", "Prelims UFC 333". nullopt for shows around
// the fights (weigh-ins, press conferences, 24/7 channels).
std::optional<FightKey> fightKey(std::string_view title);

// The scheduled card parts (game indices) a fight feed is for. `cards` is
// "org|number" -> game indices.
std::vector<int> fightTargets(const FightKey& key, std::optional<Seconds> start,
                              const std::map<std::string, std::vector<int>>& cards,
                              const std::vector<SportsGame>& games);

// MARK: Names

// "rams|seahawks", or the tidied title when it has no two sides.
std::string matchupKey(std::string_view title);
// Two sides: "A vs B", "A v B", "A @ B", "A at B", "A - B" (after a
// "Network:" style prefix).
bool looksLikeGame(std::string_view title);
// Lower-case words separated by single spaces, padded with spaces so
// whole-word checks are a plain find(" word ").
std::string normalized(std::string_view text);
// Ways a team is written: full name, short names, the nickname (last word
// or two); the place name only for college teams ("Utah").
std::vector<std::string> teamVariants(const SportsTeam& team, bool college);
// A sport named without its level ("Football: ...") covers its pro and
// college leagues.
const std::map<std::string, std::set<std::string>>& sportFamilies();
// A studio show (pregame, countdown, kickoff show, weigh-ins...) rather
// than a game: kept out of Live now, at the end of its sport's row.
bool isStudioShow(std::string_view title);
// Studio and highlight shows around a game, not the game itself (`text`
// is normalized).
bool excluded(std::string_view text);
// "rams|seahawks" for "Rams vs Seahawks" or "Seahawks @ Rams".
std::optional<std::string> pairKey(std::string_view title);
// Chip text for a row group with no schedule ("NCAAF", "FIGHT", "EVENT").
std::string shortLeague(std::string_view league);

// Team names, looked up by their first word. Built from games (two sides
// each) or from a plain list of teams (school logos, team channels).
class TeamIndex {
 public:
  struct Name {
    int slot = 0;  // game index (or team index for a plain list)
    int side = 0;  // 0 home, 1 away
    std::string team;  // team id, to keep one team's names apart
    std::vector<std::string> words;
    // The abbreviation as written ("LAR"), for names that are one.
    std::optional<std::string> abbreviation;
    // A city name ("Atlanta"): only counts when the league is known.
    bool loose = false;
  };

  explicit TeamIndex(const std::vector<SportsGame>& games);
  // A plain list (logos by name): full names, nicknames and, for schools,
  // places. No abbreviations.
  static TeamIndex fromTeams(const std::vector<SportsTeam>& list, bool college = true);

  using Filter = std::function<bool(int)>;
  // Games whose two teams both appear in `title`, on separate words.
  // `allowLoose`: the event's league is the game's, so city names and
  // two-letter abbreviations ("NJ") count too.
  std::vector<int> matches(std::string_view title, const Filter& allowed,
                           const Filter& allowLoose = nullptr) const;
  // The one team of a plain list a channel name is about ("NBA Miami Heat",
  // "Atlanta Falcons"): by its full name, or by its nickname next to the
  // league's name ("NBA Lakers").
  std::optional<SportsTeam> single(std::string_view text, std::string_view leagueWord) const;
  // Two different teams of a plain list named in `title` (left side first),
  // each by a full name or place of 4+ letters.
  std::optional<std::pair<SportsTeam, SportsTeam>> pair(std::string_view title) const;

  const std::vector<SportsTeam>& teams() const { return teams_; }

 private:
  TeamIndex() = default;
  struct Hit {
    const Name* name;
    int start;
    int end;
    int length() const { return end - start; }
  };
  void add(const SportsTeam& team, int slot, int side, bool college, bool abbreviations);
  std::vector<Hit> hits(std::string_view title, const Filter& allowed, const Filter& allowLoose,
                        std::set<std::string>* written) const;

  std::unordered_map<std::string, std::vector<Name>> byFirstWord_;
  std::vector<SportsTeam> teams_;
};

}  // namespace marigold::sports::matcher
