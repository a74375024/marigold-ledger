// Build 127: the board follows the picks. The Soccer row holds the ticked
// competitions (national teams from the all-soccer listing, every other
// competition), college rows the Top 25, your conferences and your schools,
// hidden ids' games are on no row, "I don't watch sports" puts every game
// in More events (always shown), the unconfigured board is the defaults';
// rows say what they hold and what they let you choose, tiles what they
// can hide and follow, ranks on college teams' lines, the one-time note,
// and why each game left a row (the Live Events report).
#include <algorithm>

#include "core/catalog/catalog.hpp"
#include "core/report/live_events_report.hpp"
#include "core/sports/board.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/schedule.hpp"
#include "core/util/strings.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::board;
using sportsdata::eventOf;
using sportsdata::feed;
using sportsdata::game;
using sportsdata::plainEvent;
using sportsdata::source;
using sportsdata::team;
using sportsdata::utc;

namespace {

// Saturday 2026-10-31 16:00 UTC: noon in New York.
const Seconds kNow = utc(2026, 10, 31, 16, 0);

Input baseInput(std::vector<SportsEvent> events) {
  Input in;
  in.events = std::move(events);
  in.now = kNow;
  in.zone = time::Zone::usEastern();
  in.channelFor = [](const EventSource& s) -> std::optional<ChannelLabel> {
    return ChannelLabel{s.channelID, "Channel " + s.channelID, "Channel " + s.channelID, {}};
  };
  return in;
}

EventSource station(const std::string& id) {
  return source(id, EventSource::Kind::guide, false, station_rank::elsewhere, "In your channels");
}

std::vector<std::string> rowIDs(const Board& b) {
  std::vector<std::string> out;
  for (const Row& r : b.rows) out.push_back(r.id);
  return out;
}

const Row* findRow(const Board& b, std::string_view id) {
  for (const Row& r : b.rows) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

std::vector<std::string> ids(const Board& b, std::string_view rowID) {
  const Row* r = findRow(b, rowID);
  return r ? r->eventIDs : std::vector<std::string>{};
}

bool on(const Board& b, std::string_view rowID, const std::string& id) {
  std::vector<std::string> list = ids(b, rowID);
  return std::find(list.begin(), list.end(), id) != list.end();
}

SportsGame soccer(const std::string& league, const std::string& id, const std::string& home,
                  const std::string& away, Seconds start = kNow + 3600) {
  return game(league, "Soccer", id, team("h" + id, home), team("a" + id, away), start, {}, std::nullopt, true);
}

SportsTeam school(const std::string& id, const std::string& name, std::optional<int> rank = std::nullopt,
                  std::optional<std::string> conference = std::nullopt) {
  SportsTeam t = team(id, name + " Owls", std::nullopt, name);
  t.rank = rank;
  t.conference = std::move(conference);
  return t;
}

SportsGame cfb(const std::string& id, SportsTeam home, SportsTeam away, const std::string& league = "NCAAF") {
  const League* l = leagueByShort(league);
  return game(league, l->group, id, std::move(home), std::move(away), kNow + 2 * 3600);
}

MySportsPrefs picked(std::vector<std::string> leagues) {
  MySportsPrefs p;
  p.configured = true;
  p.leagues = std::move(leagues);
  return p;
}

}  // namespace

TEST_CASE("the Soccer row holds the ticked competitions; the rest of what you can watch is in More events") {
  SportsGame epl = soccer("EPL", "1", "Harbor United", "Riverside Rovers");
  SportsGame liga = soccer("LIGA", "2", "Ciudad FC", "Puerto CF");
  // From the all-soccer listing: a Liga MX game, a USA friendly, a Copa game.
  SportsGame lmx = soccer(kSoccerAll, "3", "Club Harbor", "Deportivo Valle");
  lmx.competition = std::string("LMX");
  SportsGame usa = soccer(kSoccerAll, "4", "United States", "Panama");
  usa.competition = std::string(kUSAPick);
  SportsGame usaWomen = soccer(kSoccerAll, "5", "Brazil", "United States Women");
  SportsGame copa = soccer(kSoccerAll, "6", "Portugalete", "Irun");
  copa.tournament = std::string("Copa del Rey");
  SportsGame mls = game("MLS", "MLS", "7", team("h7", "Harbor SC"), team("a7", "Valley FC"), kNow + 3600, {},
                        std::nullopt, true);
  Input in = baseInput({eventOf(epl, {station("1")}), eventOf(liga, {station("2")}), eventOf(lmx, {station("3")}),
                        eventOf(usa, {station("4")}), eventOf(usaWomen, {station("5")}),
                        eventOf(copa, {station("6")}), eventOf(mls, {station("7")})});
  in.prefs = picked({"EPL", kUSAPick, "MLS"});
  Board b = build(in);
  CHECK(ids(b, "sport|Soccer") ==
        std::vector<std::string>{"sev|EPL|1", "sev|SOC|4", "sev|SOC|5"});  // the US men and women
  CHECK(ids(b, "sport|MLS") == std::vector<std::string>{"sev|MLS|7"});     // MLS keeps its own row
  CHECK(on(b, "more", "sev|LIGA|2"));
  CHECK(on(b, "more", "sev|SOC|3"));
  CHECK(on(b, "more", "sev|SOC|6"));
  CHECK(b.moreWhy.at("sev|LIGA|2") == "not in your soccer competitions");
  CHECK(b.moreWhy.at("sev|SOC|6") == "not in your soccer competitions");
  CHECK(findRow(b, "sport|Soccer")->choose == "competitions");
  CHECK(findRow(b, "sport|Soccer")->subtitle.empty());
  CHECK(findRow(b, "sport|MLS")->choose.empty());
  // Liga MX ticked: its games from the all-soccer listing too.
  in.prefs = picked({"EPL", "LMX"});
  b = build(in);
  CHECK(on(b, "sport|Soccer", "sev|SOC|3"));
  CHECK_FALSE(on(b, "sport|Soccer", "sev|SOC|4"));
  // Mexico's team: USA against Mexico is either's.
  SportsGame clasico = soccer(kSoccerAll, "8", "Mexico", "United States");
  in.events.push_back(eventOf(clasico, {station("8")}));
  in.prefs = picked({kMexicoPick});
  b = build(in);
  CHECK(ids(b, "sport|Soccer") == std::vector<std::string>{"sev|SOC|8"});
  // Every other competition: every soccer game.
  in.prefs = picked({kSoccerAllPick});
  b = build(in);
  CHECK(ids(b, "sport|Soccer").size() == 7);
  // Live now only has what the rows take.
  SportsGame liveLiga = soccer("LIGA", "9", "Norte", "Sur", kNow - 1800);
  SportsGame liveEpl = soccer("EPL", "10", "North", "South", kNow - 1800);
  in.events = {eventOf(liveLiga, {station("9")}), eventOf(liveEpl, {station("10")})};
  in.prefs = picked({"EPL"});
  b = build(in);
  CHECK(ids(b, "live") == std::vector<std::string>{"sev|EPL|10"});
  CHECK(on(b, "more", "sev|LIGA|9"));
}

TEST_CASE("a national team's game from the all-soccer listing shows even when no channel has it") {
  SportsGame usa = soccer(kSoccerAll, "1", "United States", "Panama");
  usa.competition = std::string(kUSAPick);
  SportsGame other = soccer(kSoccerAll, "2", "Harbor", "Valley");
  Input in = baseInput({eventOf(usa), eventOf(other)});
  in.prefs = picked({kUSAPick, kSoccerAllPick});
  Board b = build(in);
  CHECK(on(b, "sport|Soccer", "sev|SOC|1"));  // "Not in your channels"
  CHECK_FALSE(b.tiles.count("sev|SOC|2"));    // the listing's every other game: only what you can watch
}

TEST_CASE("college rows: the Top 25, your conferences and your schools; the rest in More events") {
  SportsGame ranked = cfb("1", school("1", "Harbor State", 4, std::string("5")), school("2", "Valley Tech"));
  SportsGame conf = cfb("2", school("3", "Ridge", std::nullopt, std::string("8")), school("4", "Coast"));
  SportsGame mine = cfb("3", school("5", "Bay"), school("6", "Point"));
  SportsGame plain = cfb("4", school("7", "Glen"), school("8", "Dale"));
  SportsGame plainUnwatchable = cfb("5", school("9", "Lake"), school("10", "Brook"));
  Input in = baseInput({eventOf(ranked, {station("1")}), eventOf(conf, {station("2")}), eventOf(mine, {station("3")}),
                        eventOf(plain, {station("4")}), eventOf(plainUnwatchable)});
  in.prefs = picked({"NCAAF"});
  in.prefs.teams = {teamKey("NCAAF", "6")};
  Board b = build(in);
  // The default: the Top 25 and your schools (your team's game also in Your teams).
  CHECK(ids(b, "sport|College Football") == std::vector<std::string>{"sev|NCAAF|1", "sev|NCAAF|3"});
  CHECK(on(b, "teams", "sev|NCAAF|3"));
  CHECK(on(b, "more", "sev|NCAAF|2"));
  CHECK(on(b, "more", "sev|NCAAF|4"));
  CHECK(b.moreWhy.at("sev|NCAAF|4") == "college game without a ranked team or one you follow");
  CHECK(b.left.at("sev|NCAAF|5") == "college game without a ranked team or one you follow");
  const Row* row = findRow(b, "sport|College Football");
  REQUIRE(row);
  CHECK(row->subtitle == "Top 25 and your schools");
  CHECK(row->choose == "schools");
  CHECK(row->chooseLeague == "NCAAF");
  // Your conferences.
  in.prefs.college["NCAAF"].conferences = {"8"};
  b = build(in);
  CHECK(on(b, "sport|College Football", "sev|NCAAF|2"));
  CHECK(findRow(b, "sport|College Football")->subtitle == "Top 25, your conferences and schools");
  // Top 25 off.
  in.prefs.college["NCAAF"].top25 = false;
  b = build(in);
  CHECK_FALSE(on(b, "sport|College Football", "sev|NCAAF|1"));
  CHECK(findRow(b, "sport|College Football")->subtitle == "Your conferences and schools");
  // Every Division I game: today's behaviour (games no channel has too).
  in.prefs.college["NCAAF"].everyGame = true;
  b = build(in);
  CHECK(ids(b, "sport|College Football").size() == 5);
  CHECK(findRow(b, "sport|College Football")->subtitle == "Every Division I game");
  CHECK(findRow(b, "more") == nullptr);
}

TEST_CASE("college rows: men's and women's basketball share a row; FCS and the small-college pages filter too") {
  SportsGame men = cfb("1", school("1", "Harbor", 3), school("2", "Valley"), "NCAAM");
  SportsGame women = cfb("2", school("3", "Ridge"), school("4", "Coast"), "NCAAW");
  SportsGame d3 = cfb("3", school("ncaa:bay", "Bay", std::nullopt, std::string("ncaa:little-east")),
                      school("ncaa:point", "Point"), "D3MBB");
  SportsGame fcs = cfb("4", school("5", "Glen", 12), school("6", "Dale"), "FCS");
  SportsGame fcsPlain = cfb("5", school("7", "Lake"), school("8", "Brook"), "FCS");
  Input in = baseInput({eventOf(men, {station("1")}), eventOf(women, {station("2")}), eventOf(d3, {station("3")}),
                        eventOf(fcs, {station("4")}), eventOf(fcsPlain, {station("5")})});
  in.prefs = picked({"NCAAF", "NCAAM", "NCAAW"});
  Board b = build(in);
  CHECK(ids(b, "sport|College Basketball") == std::vector<std::string>{"sev|NCAAM|1"});
  CHECK(on(b, "more", "sev|NCAAW|2"));
  CHECK(on(b, "more", "sev|D3MBB|3"));
  CHECK(ids(b, "sport|College Football") == std::vector<std::string>{"sev|FCS|4"});
  CHECK(on(b, "more", "sev|FCS|5"));
  // The row's subtitle fits both basketball leagues, else the default.
  const Row* row = findRow(b, "sport|College Basketball");
  REQUIRE(row);
  CHECK(row->subtitle == "Top 25 and your schools");
  CHECK(row->chooseLeague == "NCAAM");
  in.prefs.college["NCAAM"].everyGame = true;
  b = build(in);
  CHECK(findRow(b, "sport|College Basketball")->subtitle == "Top 25 and your schools");
  in.prefs.college["NCAAW"].everyGame = true;
  b = build(in);
  CHECK(findRow(b, "sport|College Basketball")->subtitle == "Every Division I game");
  // A small-college conference ticked for men's basketball.
  in.prefs.college.clear();
  in.prefs.college["NCAAM"].conferences = {"ncaa:little-east"};
  b = build(in);
  CHECK(on(b, "sport|College Basketball", "sev|D3MBB|3"));
}

TEST_CASE("college volleyball: its own row filtered; the schools' conferences from the other schedules") {
  SportsGame vb = cfb("1", school("100", "Harbor"), school("200", "Valley"), "NCAAWV");
  SportsGame vbRanked = cfb("2", school("300", "Ridge", 7), school("400", "Coast"), "NCAAWV");
  Input in = baseInput({eventOf(vb, {station("1")}), eventOf(vbRanked, {station("2")})});
  in.prefs = picked({"S:Volleyball"});
  in.prefs.college["NCAAWV"].conferences = {"7"};  // the Big Ten, by basketball's id
  Board b = build(in);
  CHECK(ids(b, "sport|Volleyball") == std::vector<std::string>{"sev|NCAAWV|2"});
  const Row* row = findRow(b, "sport|Volleyball");
  REQUIRE(row);
  CHECK(row->choose == "schools");
  CHECK(row->chooseLeague == "NCAAWV");
  CHECK(row->subtitle == "Top 25, your conferences and schools");
  // The schedule gives volleyball's schools basketball's conferences (the
  // same team ids): then the conference takes the game.
  schedule::Saved saved;
  saved.games = {cfb("9", school("100", "Harbor", std::nullopt, std::string("7")),
                     school("500", "Lakeside", std::nullopt, std::string("47")), "NCAAW"),
                 cfb("8", school("600", "Bay", std::nullopt, std::string("8")),
                     school("700", "Point", std::nullopt, std::string("5")), "NCAAF")};
  schedule::learnConferences(saved);
  CHECK(saved.conferences.at("100") == "7");
  CHECK(saved.conferences.at("500") == "49");  // the Summit League, one key
  CHECK(saved.conferences.at("600") == "23");  // football's SEC as basketball's
  CHECK(saved.conferences.at("700") == "7");   // football's Big Ten as basketball's
  std::vector<SportsGame> games = {vb, vbRanked};
  schedule::fillConferences(games, saved.conferences);
  CHECK(games[0].home.conference == std::string("7"));
  CHECK_FALSE(games[0].away.conference);
  // Football keeps its own keys.
  std::vector<SportsGame> football = {cfb("7", school("100", "Harbor"), school("200", "Valley"))};
  schedule::fillConferences(football, saved.conferences);
  CHECK_FALSE(football[0].home.conference);
  in.events = {eventOf(games[0], {station("1")}), eventOf(games[1], {station("2")})};
  b = build(in);
  CHECK(ids(b, "sport|Volleyball") == std::vector<std::string>{"sev|NCAAWV|1", "sev|NCAAWV|2"});
  // Saved with the schedule.
  auto back = schedule::savedFromJson(schedule::toJson(saved));
  REQUIRE(back);
  CHECK(back->conferences == saved.conferences);
}

TEST_CASE("a row of pros and college (lacrosse): its college games filtered, with the college row's tile") {
  SportsGame pll = game("PLL", "Lacrosse", "1", team("p1", "Harbor Cannons"), team("p2", "Valley Archers"),
                        kNow + 3600);
  SportsGame college = cfb("2", school("10", "Harbor"), school("11", "Valley"), "NCAAML");
  SportsGame ranked = cfb("3", school("12", "Ridge", 3), school("13", "Coast"), "NCAAML");
  Input in = baseInput({eventOf(pll, {station("1")}), eventOf(college, {station("2")}),
                        eventOf(ranked, {station("3")})});
  in.prefs = picked({"S:Lacrosse"});
  Board b = build(in);
  CHECK(ids(b, "sport|Lacrosse") == std::vector<std::string>{"sev|PLL|1", "sev|NCAAML|3"});
  CHECK(on(b, "more", "sev|NCAAML|2"));
  const Row* row = findRow(b, "sport|Lacrosse");
  REQUIRE(row);
  CHECK(row->choose == "schools");
  CHECK(row->chooseLeague == "NCAAML");
  CHECK(row->subtitle == "Top 25 and your schools");
  // Men's college every game: the row says so while only men's games are on.
  in.prefs.college["NCAAML"].everyGame = true;
  b = build(in);
  CHECK(ids(b, "sport|Lacrosse").size() == 3);
  CHECK(findRow(b, "sport|Lacrosse")->subtitle == "Every Division I game");
  // No college league among the picks: no tile (the NFL's row).
  in.events.push_back(eventOf(game("NFL", "NFL", "4", team("1", "Gulls"), team("2", "Alphas"), kNow + 3600),
                              {station("4")}));
  in.prefs = picked({"NFL"});
  b = build(in);
  CHECK(findRow(b, "sport|NFL")->choose.empty());
  CHECK(findRow(b, "sport|NFL")->subtitle.empty());
}

TEST_CASE("hidden ids: their games are on no row, and the report says why") {
  SportsGame lmx = soccer("LMX", "1", "Club Harbor", "Deportivo Valle", kNow - 1800);
  SportsGame epl = soccer("EPL", "2", "Harbor United", "Riverside Rovers");
  SportsGame nba = game("NBA", "NBA", "3", team("5", "Otters"), team("6", "Hawks"), kNow + 3600);
  SportsEvent golf = eventOf(game("PGA", "Golf", "4", team("t", "Harbor Open"), team("t", "Harbor Open"), kNow - 3600),
                             {station("4")});
  golf.game->golf = true;
  Input in = baseInput({eventOf(lmx, {station("1")}), eventOf(epl, {station("2")}), eventOf(nba, {station("3")}), golf});
  in.prefs = picked({"EPL", "NBA"});
  in.prefs.hidden = {"LMX", "S:Golf"};
  Board b = build(in);
  CHECK_FALSE(b.tiles.count("sev|LMX|1"));
  CHECK_FALSE(b.tiles.count("sev|PGA|4"));
  CHECK(ids(b, "live").empty());
  CHECK(b.left.at("sev|LMX|1") == "hidden in Settings \xE2\x80\xBA Live Events \xE2\x80\xBA Hidden");
  CHECK(b.left.at("sev|PGA|4") == "hidden in Settings \xE2\x80\xBA Live Events \xE2\x80\xBA Hidden");
  CHECK(on(b, "sport|Soccer", "sev|EPL|2"));
  // Even your team's game.
  in.prefs.teams = {teamKey("LMX", "h1")};
  b = build(in);
  CHECK_FALSE(b.tiles.count("sev|LMX|1"));
}

TEST_CASE("\"I don't watch sports\": no sport rows, every game in More events, which is always shown") {
  SportsGame nfl = game("NFL", "NFL", "1", team("1", "Gulls"), team("2", "Alphas"), kNow - 1800);
  SportsGame epl = soccer("EPL", "2", "Harbor United", "Riverside Rovers");
  Input in = baseInput({eventOf(nfl, {station("1")}), eventOf(epl, {station("2")})});
  in.prefs = picked({});
  Board b = build(in);
  CHECK(rowIDs(b) == std::vector<std::string>{"more"});
  CHECK(findRow(b, "more")->alwaysShown);
  CHECK(ids(b, "more").size() == 2);
  // Everything hidden is the same.
  in.prefs = picked({"NFL"});
  in.prefs.hidden = {"NFL"};
  b = build(in);
  CHECK_FALSE(b.tiles.count("sev|NFL|1"));
  CHECK(findRow(b, "more")->alwaysShown);
  // With picks it follows Show on the board.
  in.prefs = picked({"NFL"});
  b = build(in);
  CHECK_FALSE(findRow(b, "more")->alwaysShown);
}

TEST_CASE("unconfigured: the board is the defaults' for the lineup's countries") {
  SportsGame lmx = soccer("LMX", "1", "Club Harbor", "Deportivo Valle");
  SportsGame epl = soccer("EPL", "2", "Harbor United", "Riverside Rovers");
  SportsGame ufc = game("UFC", "Fighting", "3", team("f1", "A. Fighter"), team("f2", "B. Fighter"), kNow + 3600);
  ufc.cardKey = std::string("ufc|333|main");
  SportsGame wnba = game("WNBA", "WNBA", "4", team("1", "Harbor Lights"), team("2", "Valley Sun"), kNow + 3600);
  Input in = baseInput({eventOf(lmx, {station("1")}), eventOf(epl, {station("2")}), eventOf(ufc, {station("3")}),
                        eventOf(wnba, {station("4")})});
  in.countries = {"US"};
  Board b = build(in);
  CHECK(rowIDs(b) == std::vector<std::string>{"sport|WNBA", "sport|Soccer", "more"});
  CHECK(on(b, "more", "sev|LMX|1"));
  CHECK(on(b, "more", "sev|UFC|3"));
  CHECK_FALSE(findRow(b, "more")->alwaysShown);
  // Español: Liga MX in the Soccer row.
  in.countries = {"US", "US-ES"};
  b = build(in);
  CHECK(on(b, "sport|Soccer", "sev|LMX|1"));
  // No note for a viewer who never had the old board.
  CHECK(b.noteTitle.empty());
  CHECK(b.noteLine.empty());  // College: the defaults' Top 25, a ranked game even where no channel
  // has it (as for a viewer set up with the defaults).
  SportsGame ranked = cfb("5", school("1", "Harbor", 9), school("2", "Valley"), "NCAAM");
  SportsGame plain = cfb("6", school("3", "Ridge"), school("4", "Coast"), "NCAAM");
  in.events = {eventOf(ranked), eventOf(plain, {station("6")})};
  b = build(in);
  CHECK(ids(b, "sport|College Basketball") == std::vector<std::string>{"sev|NCAAM|5"});
  CHECK(on(b, "more", "sev|NCAAM|6"));
  CHECK(findRow(b, "sport|College Basketball")->subtitle == "Top 25 and your schools");
}

TEST_CASE("tiles: what the card hides, the teams to follow, college ranks on the team lines") {
  SportsGame lmx = soccer("LMX", "1", "Club Harbor", "Deportivo Valle");
  SportsGame ranked = cfb("2", school("10", "Harbor State", 4), school("11", "Valley Tech", 19));
  SportsEvent golf = eventOf(game("PGA", "Golf", "3", team("t", "Harbor Open"), team("t", "Harbor Open"), kNow - 3600),
                             {station("3")});
  golf.game->golf = true;
  Input in = baseInput({eventOf(lmx, {station("1")}), eventOf(ranked, {station("2")}), golf});
  in.prefs = picked({"LMX", "NCAAF", "S:Golf"});
  in.prefs.teams = {teamKey("LMX", "a1")};
  Board b = build(in);
  const Tile& t = b.tiles.at("sev|LMX|1");
  CHECK(t.competitionId == "LMX");
  CHECK(t.competitionName == "Liga MX");
  REQUIRE(t.follow.size() == 2);
  CHECK(t.follow[0].key == "LMX|h1");  // soccer: home first, as the tile
  CHECK(t.follow[0].name == "Club Harbor");
  CHECK_FALSE(t.follow[0].followed);
  CHECK(t.follow[1].key == "LMX|a1");
  CHECK(t.follow[1].followed);
  const Tile& c = b.tiles.at("sev|NCAAF|2");
  CHECK(c.competitionId == "NCAAF");
  CHECK(c.competitionName == "College football");
  REQUIRE(c.sides.size() == 2);
  CHECK(c.sides[0].rank == std::optional<int>(19));  // away on top: "19 Valley Tech at 4 Harbor State"
  CHECK(c.sides[1].rank == std::optional<int>(4));
  REQUIRE(c.follow.size() == 2);
  CHECK(c.follow[0].key == "NCAAF|11");
  const Tile& g = b.tiles.at("sev|PGA|3");
  CHECK(g.competitionId == "S:Golf");
  CHECK(g.competitionName == "Golf");
  CHECK(g.follow.empty());  // a tournament: no teams to follow
}

TEST_CASE("the one-time note after the update") {
  SportsGame nfl = game("NFL", "NFL", "1", team("1", "Gulls"), team("2", "Alphas"), kNow + 3600);
  Input in = baseInput({eventOf(nfl, {station("1")})});
  in.prefs = picked({"NFL"});
  in.prefs.updateNoted = false;
  Board b = build(in);
  CHECK(b.noteTitle == "Your rows now show what you follow");
  CHECK(b.noteLine == "College shows the Top 25 and your schools. Other games are in More events and Search.");
  in.prefs.updateNoted = true;
  b = build(in);
  CHECK(b.noteTitle.empty());
}

TEST_CASE("the Live Events report says why a game is in More events or left out") {
  SportsGame liga = soccer("LIGA", "1", "Ciudad FC", "Puerto CF");
  SportsGame plain = cfb("2", school("1", "Glen"), school("2", "Dale"));
  SportsGame lmx = soccer("LMX", "3", "Club Harbor", "Deportivo Valle");
  std::vector<SportsEvent> events = {eventOf(liga, {feed("xt|1")}), eventOf(plain, {feed("xt|2")}),
                                     eventOf(lmx, {feed("xt|3")})};
  Input in = baseInput(events);
  in.prefs = picked({"EPL", "NCAAF"});
  in.prefs.hidden = {"LMX"};
  Board b = build(in);
  catalog::CatalogResult cat;
  for (int i = 1; i <= 3; ++i) {
    std::string id = "xt|" + std::to_string(i);
    catalog::ChannelTraits t;
    t.familyKey = std::string("PPV");
    t.familyNumber = i;
    t.eventText = std::string("Slot ") + std::to_string(i);
    cat.traits[id] = t;
    catalog::LiveEvent e;
    e.title = events[static_cast<size_t>(i - 1)].title;
    e.feeds = {id};
    cat.events.push_back(e);
  }
  report::LiveEventsReportInput rin;
  rin.catalog = &cat;
  rin.events = &events;
  rin.board = &b;
  rin.now = kNow;
  std::string csv = report::liveEventsCsv(rin);
  CHECK(csv.find("More events: not in your soccer competitions") != std::string::npos);
  CHECK(csv.find("More events: college game without a ranked team or one you follow") != std::string::npos);
  CHECK(csv.find("hidden in Settings \xE2\x80\xBA Live Events \xE2\x80\xBA Hidden") != std::string::npos);
}
