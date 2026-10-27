// One profile per sport: how long it runs and how it may run over, how
// feeds are matched to it, how its games are named, how its clock reads,
// and how its tile looks. Every sports rule that differs by sport reads it
// here instead of keeping its own "if NFL..." branch; fixing or adding a
// sport is one entry in profiles.cpp.
//
// Profiles are looked up by the row group / event league ("NFL", "Soccer",
// "Motorsport"), with per-league entries where one league of a group differs
// (NASCAR and F1 within Motorsport). Unknown leagues get the default
// profile ("Other").
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/util/time.hpp"

namespace marigold::sports {

struct SportsGame;

struct Profile {
  // How the live clock text turns into progress along the tile.
  enum class Clock : uint8_t {
    Periods,  // "Q2 3:12", "2nd 14:05": count down through periods
    Innings,  // "Top 6th", "Bot 8th"
    Minutes,  // "67'", "90'+3'" out of clockMinutes
    None,     // no running clock (fight cards): progress goes by the time
  };
  // How the event's tile is laid out.
  enum class Tile : uint8_t {
    Teams,        // two teams: badge or logo, score
    Players,      // two players: country flags (the code until they load), sets
    Leaderboard,  // a tournament's top two: position, flag, score to par, holes played
    Card,         // a fight card: Prelims / Main timeline
    RaceWeekend,  // a race weekend: sessions, the last one's top three
    Title,        // a title and the sport's icon
  };
  // Parts of an event with lengths of their own.
  // Tennis matches: best of five sets (men's Grand Slam singles) run longer.
  // A golf tournament's round runs the day (longLength).
  // Cricket: by format (an ODI's day; a match over several days to its last).
  enum class Parts : uint8_t { None, FightCard, RaceSessions, TennisMatches, GolfRound, CricketMatch };

  std::string league;       // row group / event league this entry is for
  std::string leagueShort;  // one league within the group ("NASCAR"); empty = the whole group

  time::Seconds length = 9'000;          // a typical event, start to finish
  time::Seconds overrun = 30 * 60;       // past that: "may have ended" this long
  // A knockout match (extra time, penalties) may run over this long;
  // 0 = the usual overrun. `cup`: every match is treated as one.
  time::Seconds knockoutOverrun = 0;
  bool cup = false;
  time::Seconds longLength = 0;          // TennisMatches: a best-of-five match; GolfRound: a day's play
  // "Suspended" means play stops and resumes later (rain in baseball,
  // tennis, cricket, golf): the game stays live with a SUSPENDED pill.
  // Elsewhere a suspended game counts as postponed.
  bool suspendResumes = false;
  // Two games between the same teams on one day (baseball): a feed with no
  // time goes to the one on now.
  bool doubleheaders = false;
  time::Seconds listingCap = 8 * 3600;   // a guide listing may stretch it to this
  time::Seconds matchWindow = 3 * 3600;  // a feed's time this close to the game's
  Parts parts = Parts::None;

  // Two teams with no game on the schedule means the feed is a replay.
  bool completeSchedule = false;
  // "Home vs Away", the home side on top (soccer, fights); otherwise
  // "Away at Home".
  bool homeFirst = false;

  Clock clock = Clock::Periods;
  // Periods: length of a quarter ("Q" clocks), and the count and length of
  // other periods (hockey periods, college basketball halves).
  int quarterSeconds = 600;
  int periods = 3;
  int periodSeconds = 1200;
  int clockMinutes = 95;  // Minutes: a whole game with stoppage time
  int innings = 9;        // Innings: a regulation game

  Tile tile = Tile::Teams;
  bool podium = false;  // a finished session's top three on the tile (F1, from its race data)
  bool podiumFromScores = false;  // the same from the scoreboard's finishing order (NASCAR, IndyCar)
  std::string symbol = "other";  // icon on title-only tiles: an app/icons name
  std::string chip;  // short league chip ("NCAAF"); empty = the league upper-cased
};

// Every profile, group entries and per-league ones.
const std::vector<Profile>& profiles();

// The profile for an event's league, or one league within it.
const Profile& profile(std::string_view league, std::string_view leagueShort = {});

// A typical length for this event: the profile's, or its part's (a fight
// card's prelims or main card, a race weekend's session, a best-of-five
// tennis match).
time::Seconds lengthOf(std::string_view league, const SportsGame* game);
// How long past that it "may have ended": longer for a knockout match.
time::Seconds overrunOf(std::string_view league, const SportsGame* game);
// A knockout match: a cup competition's, or a round the schedule names
// ("Round of 16 · 2nd Leg", "Quarterfinal", "Playoffs").
bool isKnockout(const Profile& p, const SportsGame& game);
// A Grand Slam tournament ("Wimbledon", "US Open"...).
bool isGrandSlam(std::string_view tournament);

}  // namespace marigold::sports
