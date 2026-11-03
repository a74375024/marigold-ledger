// Build 127: My Sports v2 through the store: a version 1 file migrated on
// load, the defaults and their reasons by the lineup's countries, college
// conferences and schools from the schedules already fetched (no
// requests), hide / show again / follow / the board's note, and More
// events always shown when nothing is picked.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/engine/store.hpp"
#include "core/engine/views.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/schedule.hpp"
#include "engine/loaded_store.hpp"
#include "sports/helpers.hpp"

using marigold::engine::BoardShows;
using marigold::engine::EventRow;
using marigold::engine::Store;
namespace sports = marigold::sports;
namespace views = marigold::engine::views;

namespace {

sports::SportsTeam school(const std::string& id, const std::string& name, const std::string& location,
                          std::optional<std::string> conference) {
  sports::SportsTeam t = sportsdata::team(id, name, std::nullopt, location);
  t.conference = std::move(conference);
  return t;
}

// A saved schedule with college games: football, men's basketball, and
// volleyball (whose schools have no conference of their own).
std::string savedSchedule() {
  using sportsdata::game;
  sports::Seconds start = marigold::time::now() + 3600;
  sports::schedule::Saved saved;
  saved.games = {
      game("NCAAF", "College Football", "1", school("130", "Harbor Owls", "Harbor", std::string("5")),
           school("194", "Valley Hawks", "Valley", std::string("5")), start),
      game("NCAAF", "College Football", "2", school("333", "Ridge Rams", "Ridge", std::string("8")),
           school("57", "Coast Gulls", "Coast", std::string("8")), start),
      game("FCS", "College Football", "3", school("2", "Harbor Tech", "Harbor Tech", std::string("104")),
           school("3", "Valley State", "Valley State", std::string("48")), start),
      game("NCAAM", "College Basketball", "4", school("130", "Harbor Owls", "Harbor", std::string("7")),
           school("2509", "Lakeside Otters", "Lakeside", std::string("7")), start),
      game("D3MBB", "College Basketball", "5", school("ncaa:bay", "Bay", "", std::string("ncaa:little-east")),
           school("ncaa:point", "Point", "", std::string("ncaa:little-east")), start),
      game("NCAAWV", "Volleyball", "6", school("130", "Harbor Owls", "Harbor", std::nullopt),
           school("2509", "Lakeside Otters", "Lakeside", std::nullopt), start),
  };
  sports::schedule::learnConferences(saved);
  return sports::schedule::toJson(saved);
}

bool hasRowShown(const std::vector<EventRow>& rows, const BoardShows& shows, const std::string& id) {
  for (size_t r : views::boardShownRows(rows, shows))
    if (rows[r].id == id) return true;
  return false;
}

}  // namespace

TEST_CASE("store: a version 1 My Sports file is migrated once on load, with the board's note") {
  fake::Tv tv;
  tv.files.write("my-sports.json", R"({"configured": true, "leagues": ["NFL", "EPL"], "teams": ["NFL|1"]})", false);
  Store store(tv.host());
  const sports::MySportsPrefs& p = store.mySports();
  CHECK(p.version == sports::kMySportsVersion);
  CHECK(p.leagues == std::vector<std::string>{"NFL", "EPL", "UCL", sports::kUSAPick});
  CHECK_FALSE(p.updateNoted);
  // Saved migrated: read again, it stays as it is.
  auto saved = tv.files.read("my-sports.json");
  REQUIRE(saved);
  auto back = sports::mySportsFromJson(*saved);
  REQUIRE(back);
  CHECK(*back == p);
  // The note read: gone for good.
  store.dismissBoardNote();
  CHECK(store.mySports().updateNoted);
  Store again(tv.host());
  CHECK(again.mySports().updateNoted);
  CHECK(again.mySports().leagues == p.leagues);
}

TEST_CASE("store: the defaults and why, by the lineup's countries") {
  fake::Tv tv;
  Store store(tv.host());
  store.setLineupCountries({"US", "US-ES", "IN"});
  sports::MySportsPrefs d = store.defaultMySports();
  CHECK_FALSE(d.configured);
  CHECK(std::find(d.leagues.begin(), d.leagues.end(), "LMX") != d.leagues.end());
  CHECK(std::find(d.leagues.begin(), d.leagues.end(), "S:Cricket") != d.leagues.end());
  CHECK(store.pickReason("LMX") == "For Espa\xC3\xB1ol");
  CHECK(store.pickReason("S:Cricket") == "For Hindi");
  CHECK(store.pickReason("NFL").empty());
}

TEST_CASE("store: college conferences and their schools, from the schedules already fetched") {
  fake::Tv tv;
  tv.files.write("sports-schedule.json", savedSchedule(), false);
  Store store(tv.host());
  std::vector<sports::Conference> football = store.collegeConferences("NCAAF");
  // FCS counts for college football; a Division II conference with no
  // known name is left out.
  REQUIRE(football.size() == 3);
  CHECK(football[0].name == "Big Ten");
  CHECK(football[0].key == "5");
  CHECK(football[1].name == "CAA Football");
  CHECK(football[2].name == "SEC");
  CHECK(store.collegeConferences("FCS").size() == 3);
  auto big10 = store.conferenceSchools("NCAAF", "5");
  CHECK(big10 == std::vector<std::pair<std::string, std::string>>{{"130", "Harbor"}, {"194", "Valley"}});
  // Basketball: its own keys, the small-college pages' conferences too.
  std::vector<sports::Conference> hoops = store.collegeConferences("NCAAM");
  REQUIRE(hoops.size() == 2);
  CHECK(hoops[0].name == "Big Ten");
  CHECK(hoops[0].key == "7");
  CHECK(hoops[1].name == "Little East");
  CHECK(hoops[1].key == "ncaa:little-east");
  CHECK(store.conferenceSchools("NCAAM", "ncaa:little-east").size() == 2);
  // Volleyball: its schools' conferences from the basketball schedule.
  std::vector<sports::Conference> volleyball = store.collegeConferences("NCAAWV");
  REQUIRE(volleyball.size() == 1);
  CHECK(volleyball[0].name == "Big Ten");
  CHECK(store.conferenceSchools("NCAAWV", "7").size() == 2);
  // Nothing was asked for.
  CHECK(tv.pool.asked.empty());
  CHECK(store.collegeConferences("NFL").empty());
}

TEST_CASE("store: hide from the board, show again, the Hidden list, follow a team") {
  fake::Tv tv;
  Store store(tv.host());
  sports::MySportsPrefs p;
  p.configured = true;
  p.leagues = {"NBA", "LMX"};
  store.setMySports(p);
  REQUIRE(store.hiddenFromBoard().size() == 2);  // pro wrestling and high school
  store.hideFromBoard("LMX");
  CHECK(store.mySports().leagues == std::vector<std::string>{"NBA"});
  CHECK(store.mySports().hidden == std::vector<std::string>{"LMX"});
  std::vector<Store::HiddenItem> hidden = store.hiddenFromBoard();
  REQUIRE(hidden.size() == 3);
  CHECK(hidden[0].id == "LMX");
  CHECK(hidden[0].name == "Liga MX");
  CHECK(hidden[0].why == "Hidden from a game card");
  CHECK(hidden[1].why == "Hidden unless you turn it on");
  store.showOnBoard("LMX");
  CHECK(store.mySports().hidden.empty());
  CHECK(store.mySports().leagues == std::vector<std::string>{"NBA", "LMX"});
  store.followTeam("LMX|10", true);
  CHECK(store.mySports().teams == std::vector<std::string>{"LMX|10"});
  CHECK(store.followsTeam("LMX|10"));
  store.followTeam("LMX|10", false);
  CHECK(store.mySports().teams.empty());
  // Saved each time.
  auto saved = tv.files.read("my-sports.json");
  REQUIRE(saved);
  CHECK(*sports::mySportsFromJson(*saved) == store.mySports());
}

TEST_CASE("views: a row the board always shows is drawn whatever Show on the board says") {
  EventRow more;
  more.id = "more";
  more.title = "More events";
  EventRow nfl;
  nfl.id = "sport|NFL";
  std::vector<EventRow> rows = {nfl, more};
  BoardShows off;
  CHECK_FALSE(hasRowShown(rows, off, "more"));
  rows[1].alwaysShown = true;
  CHECK(hasRowShown(rows, off, "more"));
  CHECK(views::boardHidden(rows, off).empty());
}
