// Live Events data shared by the sports modules: teams and games from the
// schedule (SportsTeam, SportsGame in SportsData.swift), live status from
// the scoreboard (LivePhase, LiveStatus in LiveScores.swift), and a game or
// event with every way to watch it (EventSource, SportsEvent in
// SportsEvents.swift). Plain values: copyable, no pointers.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/util/json.hpp"
#include "core/util/time.hpp"

namespace marigold::sports {

using time::Seconds;

struct SportsTeam {
  std::string id;
  std::string name;
  // "short" in Swift: the schedule's abbreviation ("LAR").
  std::optional<std::string> shortName;
  std::vector<std::string> alternates;
  // Logo URL (a fighter's headshot or flag).
  std::optional<std::string> badge;
  // "Utah", "Los Angeles": only used to match college teams.
  std::optional<std::string> location;
  // Team colors as hex ("00274C"), for the badge when there's no logo.
  std::optional<std::string> color;
  std::optional<std::string> altColor;
  // Poll rank in this game (college, 1-25), or a tennis seed.
  std::optional<int> rank;
  // A fighter's or player's country code ("AUS").
  std::optional<std::string> country;

  // "LAR", or initials when there's no abbreviation.
  std::string abbreviation() const;
  bool operator==(const SportsTeam& o) const;
  bool operator!=(const SportsTeam& o) const { return !(*this == o); }
};

struct SportsGame {
  std::string id;           // "NFL|401..." , "UFC|600...|main", "F1|<event>|<session>"
  std::string league;       // row group: "NFL", "College Football", "Soccer"
  std::string leagueShort;  // chip text: "NFL", "EPL", "UCL"
  // "Home vs Away" with the home side first (soccer, fight cards); saved
  // as "isSoccer".
  bool homeFirst = false;
  std::optional<std::string> weekLabel;  // "Week 8"
  SportsTeam home;
  SportsTeam away;
  Seconds start = 0;
  // TV networks, national first ("FOX", "ESPN", "NBA League Pass").
  std::vector<std::string> networks;
  std::optional<std::string> venue;
  bool postponed = false;
  // Already over when the schedule was fetched.
  bool completed = false;
  // A fight card part's own title ("UFC 333 Prelims").
  std::optional<std::string> customTitle;
  // Fight cards "ufc|333|main"; race weekends "f1|<event>|<session kind>".
  std::optional<std::string> cardKey;
  // A short tag for the tile ("World Series · Game 1", "Sunday Night").
  // Never a score or a result.
  std::optional<std::string> note;
  // A race weekend session: both sides are the Grand Prix.
  std::optional<bool> racing;
  // A tennis match: both sides are players (or doubles pairs).
  std::optional<bool> tennis;

  bool isRacing() const { return racing.value_or(false); }
  bool isTennis() const { return tennis.value_or(false); }
  std::string title() const;
  bool operator==(const SportsGame& o) const;
  bool operator!=(const SportsGame& o) const { return !(*this == o); }
};

// Where a game stands, from the live scoreboard.
enum class LivePhase : uint8_t { pre, live, delayed, final, postponed };
const char* rawValue(LivePhase p);
std::optional<LivePhase> livePhaseFromRaw(std::string_view raw);

struct LiveStatus {
  LivePhase phase = LivePhase::pre;
  // "Q3 4:12", "Halftime", "Bot 8th", "67'", "2nd 11:02", "Final/OT".
  std::string detail;
  std::optional<std::string> homeScore;
  std::optional<std::string> awayScore;
  // The team with the ball (football), by team id.
  std::optional<std::string> possession;
  // "3rd & 7 at MICH 35", "2 outs · 3-1 count".
  std::optional<std::string> situation;
  bool redZone = false;
  // Runners on first, second and third (baseball).
  std::optional<std::vector<bool>> bases;
  std::optional<std::string> lastPlay;
  // A fight card part: fights done and in all, the fight on now (or next).
  // Never who won.
  std::optional<int> boutsDone;
  std::optional<int> boutsTotal;
  std::optional<std::string> boutA;
  std::optional<std::string> boutB;
  std::optional<std::string> boutACountry;
  std::optional<std::string> boutBCountry;
  // Tennis: games won in each set so far, per side.
  std::optional<std::vector<std::string>> homeSets;
  std::optional<std::vector<std::string>> awaySets;

  bool operator==(const LiveStatus& o) const;
  bool operator!=(const LiveStatus& o) const { return !(*this == o); }
};

// Where an event can be watched. A list of them is kept best first, which is
// also the automatic backup order.
struct EventSource {
  // Order of preference: My Lineup, your channels, provider event feeds,
  // then a team's own channel (likely, not certain, to carry the game).
  enum class Kind : uint8_t { lineup = 0, guide = 1, provider = 2, team = 3 };
  std::string channelID;
  Kind kind = Kind::guide;
  bool spanish = false;
  // "In My Lineup", "Provider event channel", "Team channel".
  std::string detail;
  // A station's rank (StationRank): 0 My Lineup and home, 1 My Lineup,
  // 2 home market, 3 your other markets, 4 anywhere else. nil = not ranked.
  std::optional<int> rank;

  bool operator==(const EventSource& o) const {
    return channelID == o.channelID && kind == o.kind && spanish == o.spanish &&
           detail == o.detail && rank == o.rank;
  }
  bool operator!=(const EventSource& o) const { return !(*this == o); }
};

// One game or event for Live Events: a schedule game with everything found
// showing it (guide channels and provider event feeds), or a provider event
// that matched no game.
struct SportsEvent {
  std::string id;
  std::string title;
  std::string league;       // row group
  std::string leagueShort;  // chip text
  std::optional<Seconds> start;
  std::optional<SportsGame> game;
  std::vector<EventSource> sources;
  std::optional<std::string> family;
  // The end of the guide listing showing it, when later than usual.
  std::optional<Seconds> listingEnd;
  // For an event with no game: the two teams named in it (logos only).
  std::optional<std::vector<SportsTeam>> teams;
  // When every provider feed that carried it moved on (the provider's sign
  // it's over).
  std::optional<Seconds> feedsEndedAt;
  // Stations also showing it that aren't listed (the list keeps the best few).
  std::optional<int> moreStations;
  // Where the game stands from the live scoreboard (set on board copies).
  // Overrides the guesswork in state().
  std::optional<LivePhase> live;

  enum class State : uint8_t { upcoming, live, mayHaveEnded, finished, postponed, unknownTime };

  // Swift's ==: id, sources, game, listing end, teams, feeds ended, more
  // stations and live (not title, league or family).
  bool operator==(const SportsEvent& o) const;
  bool operator!=(const SportsEvent& o) const { return !(*this == o); }

  // The same event under another tile id (live phase not carried, as in Swift).
  SportsEvent withId(const std::string& newID) const;
  // The same event under another league (an unknown one recognised by its teams).
  SportsEvent retitled(const std::string& newLeague, const std::string& shortName) const;

  std::vector<EventSource> channels() const;  // every source but provider feeds
  std::vector<EventSource> feeds() const;     // provider feeds only
  bool hasFeeds() const;

  // Rough length by sport; past it a game "may have ended", and a while
  // after that it's dropped from Live now.
  Seconds expectedLength() const;
  // Live for the sport's usual length (or until the guide listing ends, if
  // later), then "may have ended" for half an hour, then gone.
  State state(Seconds now) const;
  // Fraction of the expected length gone by (for the progress bar).
  double progress(Seconds now) const;
};

const char* rawValue(SportsEvent::State s);

// Race weekend sessions: kinds ("fp1", "sq", "qual", "race"...) and names
// (RaceSession in SportsData.swift).
namespace race_session {
// Session kind from the schedule's type ("FP1", "Qual", "Race", "SS"...).
std::string kind(std::string_view type);
// "Practice 1", "Sprint Qualifying", "Qualifying", "Race".
std::string name(std::string_view kind);
// Timeline label: "FP1", "SQ", "Sprint", "Quali", "Race".
std::string shortName(std::string_view kind);
// Usual length of a session.
Seconds length(std::string_view kind);
// The session a feed or listing title names, or nil.
std::optional<std::string> kindInTitle(std::string_view text);
}  // namespace race_session

// MARK: JSON (caches; dates are Unix seconds)

json::Value toJson(const SportsTeam& t);
std::optional<SportsTeam> teamFromJson(const json::Value& v);
json::Value toJson(const SportsGame& g);
std::optional<SportsGame> gameFromJson(const json::Value& v);
json::Value toJson(const EventSource& s);
std::optional<EventSource> sourceFromJson(const json::Value& v);
json::Value toJson(const SportsEvent& e);
std::optional<SportsEvent> eventFromJson(const json::Value& v);

}  // namespace marigold::sports
