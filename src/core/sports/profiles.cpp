#include "core/sports/profiles.hpp"

#include <vector>

#include "core/util/rx.hpp"

#include "core/sports/types.hpp"
#include "core/util/strings.hpp"

namespace marigold::sports {
namespace {

using Clock = Profile::Clock;
using Tile = Profile::Tile;
using Parts = Profile::Parts;

Profile base(const char* league, time::Seconds length, const char* symbol, Tile tile = Tile::Title) {
  Profile p;
  p.league = league;
  p.length = length;
  p.symbol = symbol;
  p.tile = tile;
  return p;
}

Profile football(const char* league) {
  Profile p = base(league, 12'960, "football.fill", Tile::Teams);  // 3.6 h
  p.quarterSeconds = 900;
  return p;
}

Profile basketball(const char* league) { return base(league, 9'360, "basketball.fill", Tile::Teams); }  // 2.6 h

Profile baseball(const char* league, time::Seconds length = 11'160) {  // 3.1 h
  Profile p = base(league, length, "baseball.fill", Tile::Teams);
  p.clock = Clock::Innings;
  p.suspendResumes = true;
  p.doubleheaders = true;
  return p;
}

Profile soccer(const char* league) {
  Profile p = base(league, 7'560, "soccerball", Tile::Teams);  // 2.1 h
  p.homeFirst = true;
  p.clock = Clock::Minutes;
  p.knockoutOverrun = 60 * 60;  // extra time and penalties
  return p;
}

const std::vector<Profile>& table() {
  static const std::vector<Profile> kTable = [] {
    std::vector<Profile> v;

    Profile nfl = football("NFL");
    nfl.completeSchedule = true;
    v.push_back(nfl);
    Profile cfb = football("College Football");
    cfb.chip = "NCAAF";
    v.push_back(cfb);
    Profile plainFootball = football("Football");  // the level isn't known
    plainFootball.tile = Tile::Title;
    v.push_back(plainFootball);

    Profile nba = basketball("NBA");
    nba.completeSchedule = true;
    nba.quarterSeconds = 720;
    v.push_back(nba);
    Profile wnba = basketball("WNBA");
    wnba.completeSchedule = true;
    v.push_back(wnba);
    Profile cbb = basketball("College Basketball");
    cbb.chip = "NCAAB";
    cbb.periods = 2;  // halves
    v.push_back(cbb);
    Profile plainBasketball = basketball("Basketball");
    plainBasketball.tile = Tile::Title;
    v.push_back(plainBasketball);

    Profile mlb = baseball("MLB");
    mlb.completeSchedule = true;
    v.push_back(mlb);
    Profile plainBaseball = baseball("Baseball");
    plainBaseball.tile = Tile::Title;
    v.push_back(plainBaseball);
    Profile milb = baseball("MiLB");
    milb.tile = Tile::Title;
    v.push_back(milb);
    Profile softball = base("Softball", 9'000, "baseball.fill");
    softball.suspendResumes = true;
    softball.doubleheaders = true;
    v.push_back(softball);

    Profile nhl = base("NHL", 9'360, "hockey.puck.fill", Tile::Teams);
    nhl.completeSchedule = true;
    v.push_back(nhl);
    v.push_back(base("Hockey", 9'360, "hockey.puck.fill"));
    Profile fieldHockey = base("Field Hockey", 9'000, "figure.field.hockey");
    fieldHockey.chip = "FIELD H.";
    v.push_back(fieldHockey);

    v.push_back(soccer("Soccer"));
    v.push_back(soccer("MLS"));
    // European cups: knockout ties (and their legs) run into extra time.
    for (const char* cup : {"UCL", "UEL", "UECL"}) {
      Profile p = soccer("Soccer");
      p.leagueShort = cup;
      p.cup = true;
      v.push_back(p);
    }

    // A card's prelims run about 2.5 h, its main card about 4; a whole
    // card 5. Fighters are named "A vs B".
    Profile fighting = base("Fighting", 5 * 3600, "figure.boxing", Tile::Card);
    fighting.chip = "FIGHT";
    fighting.parts = Parts::FightCard;
    fighting.homeFirst = true;
    fighting.clock = Clock::None;  // fight statuses carry no running clock
    v.push_back(fighting);
    Profile wrestling = base("Wrestling", 9'000, "sportscourt.fill");
    wrestling.chip = "WREST.";
    v.push_back(wrestling);

    // A race weekend's sessions have their own lengths; a NASCAR Cup race
    // runs 3-4 hours.
    Profile motorsport = base("Motorsport", 4 * 3600, "flag.checkered", Tile::RaceWeekend);
    motorsport.chip = "MOTOR";
    motorsport.parts = Parts::RaceSessions;
    v.push_back(motorsport);
    Profile f1 = motorsport;
    f1.leagueShort = "F1";
    f1.podium = true;
    v.push_back(f1);
    Profile nascar = motorsport;
    nascar.leagueShort = "NASCAR";
    nascar.length = 13'500;  // 3.75 h
    nascar.parts = Parts::None;
    v.push_back(nascar);

    // A later match on a court can start hours after its listed time; a
    // long match runs well past its usual length.
    Profile tennis = base("Tennis", 9'000, "tennisball.fill", Tile::Players);
    tennis.matchWindow = 8 * 3600;
    tennis.overrun = 60 * 60;
    tennis.parts = Parts::TennisMatches;
    tennis.longLength = 4 * 3600;
    tennis.suspendResumes = true;
    v.push_back(tennis);
    // All-day coverage: live for as long as the guide listing says.
    Profile golf = base("Golf", 4 * 3600, "figure.golf");
    golf.listingCap = 12 * 3600;
    golf.suspendResumes = true;
    v.push_back(golf);
    Profile cricket = base("Cricket", 12'600, "figure.cricket");  // 3.5 h, a T20
    cricket.listingCap = 12 * 3600;
    cricket.suspendResumes = true;
    v.push_back(cricket);

    Profile volleyball = base("Volleyball", 9'000, "volleyball.fill");
    volleyball.chip = "VOLLEY";
    v.push_back(volleyball);
    Profile lacrosse = base("Lacrosse", 9'000, "figure.lacrosse");
    lacrosse.chip = "LAX";
    v.push_back(lacrosse);
    return v;
  }();
  return kTable;
}

const Profile& fallback() {
  static const Profile kOther = base("Other", 9'000, "sportscourt.fill");
  return kOther;
}

}  // namespace

const std::vector<Profile>& profiles() { return table(); }

const Profile& profile(std::string_view league, std::string_view leagueShort) {
  const Profile* group = nullptr;
  for (const Profile& p : table()) {
    if (p.league != league) continue;
    if (!leagueShort.empty() && p.leagueShort == leagueShort) return p;
    if (p.leagueShort.empty() && !group) group = &p;
  }
  return group ? *group : fallback();
}

bool isGrandSlam(std::string_view tournament) {
  return rx::test("\\b(australian open|roland garros|french open|wimbledon|us open)\\b", tournament, true);
}

bool isKnockout(const Profile& p, const SportsGame& game) {
  if (p.cup) return true;
  return game.note &&
         rx::test("\\b(leg|final|semifinals?|quarterfinals?|round of \\d+|playoffs?|knockout|cup)\\b", *game.note, true);
}

time::Seconds overrunOf(std::string_view league, const SportsGame* game) {
  const Profile& p = profile(league, game ? std::string_view(game->leagueShort) : std::string_view());
  if (p.knockoutOverrun && game && isKnockout(p, *game)) return p.knockoutOverrun;
  return p.overrun;
}

time::Seconds lengthOf(std::string_view league, const SportsGame* game) {
  const Profile& p = profile(league, game ? std::string_view(game->leagueShort) : std::string_view());
  // Men's singles at a Grand Slam: best of five sets.
  if (p.parts == Parts::TennisMatches && game && game->leagueShort == "ATP" && game->tournament &&
      isGrandSlam(*game->tournament) && game->home.name.find(" / ") == std::string::npos &&
      game->away.name.find(" / ") == std::string::npos) {
    return p.longLength;
  }
  if (game && game->cardKey) {
    const std::string& card = *game->cardKey;
    if (p.parts == Parts::FightCard) {
      if (str::endsWith(card, "|early") || str::endsWith(card, "|prelims")) return 9'000;
      if (str::endsWith(card, "|main")) return 4 * 3600;
    } else if (p.parts == Parts::RaceSessions && game->isRacing()) {
      size_t cut = card.rfind('|');
      if (cut != std::string::npos) return race_session::length(card.substr(cut + 1));
    }
  }
  return p.length;
}

}  // namespace marigold::sports
