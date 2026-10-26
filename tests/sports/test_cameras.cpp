// The cameras panel view model: the header, quick picks (with "now" on the
// camera on screen, a backup counting too), the driver grid with followed
// drivers first, team colours, numbers and photos (race data first, the
// player search when it has none, initials), NASCAR car numbers, where
// focus starts, what opening asks for, the follow set, the camera index and
// what Select tunes. Drivers and races are invented.
#include "core/sports/cameras.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::cameras;
using Kind = CameraFeed::Kind;
using sportsdata::plainEvent;
using sportsdata::utc;

namespace {

const Seconds kNow = utc(2026, 10, 24, 18, 0);

CameraFeed feed(const std::string& id, const std::string& title, const std::string& detail, Kind kind,
                std::optional<std::string> team = std::nullopt, std::vector<std::string> alternates = {},
                std::optional<int> number = std::nullopt) {
  CameraFeed f;
  f.channelID = id;
  f.title = title;
  f.detail = detail;
  f.kind = kind;
  f.team = std::move(team);
  f.alternates = std::move(alternates);
  f.number = number;
  return f;
}

Channel channel(const std::string& id, const std::string& name) {
  Channel c;
  c.id = id;
  c.name = name;
  return c;
}

SportsEvent f1Race() {
  SportsGame g = sportsdata::game("F1", "Motorsport", "ev1|race", sportsdata::team("gp", "Grand Prix Alpha"),
                                  sportsdata::team("gp", "Grand Prix Alpha"), kNow);
  g.racing = true;
  g.customTitle = "Grand Prix Alpha - Race";
  return sportsdata::eventOf(g);
}

std::vector<CameraFeed> f1Feeds() {
  return {feed("xt|main", "Main broadcast", "The race feed", Kind::main),
          feed("xt|sky", "Sky Sports", "Sky commentary", Kind::alt),
          feed("c|trk", "Driver Tracker", "Bird's-eye map", Kind::tracker),
          feed("c|alpha", "Driver Alpha", "McLaren", Kind::driver, std::string("McLaren"), {"c|alpha2"}),
          feed("c|bravo", "Driver Bravo", "Ferrari", Kind::driver, std::string("Ferrari")),
          feed("c|charlie", "Driver Charlie", "Onboard", Kind::driver),
          feed("c|delta", "Driver Delta", "Team Delta", Kind::driver, std::string("Team Delta"))};
}

race_data::RaceData raceData() {
  race_data::Saved saved;
  saved.drivers = {
      race_data::RaceDriver{4, "DRA", "Driver Alpha", "McLaren", std::string("FF8000"),
                            std::string("https://example.invalid/alpha.png")},
      race_data::RaceDriver{16, "DRB", "Driver Bravo", "Ferrari", std::nullopt, std::nullopt},
  };
  return race_data::RaceData(saved);
}

player_photos::PlayerPhotos photos() {
  player_photos::Saved saved;
  saved.entries["driver bravo"] = player_photos::Entry{"https://example.invalid/bravo-cut.png", kNow};
  saved.entries["driver delta"] = player_photos::Entry{"https://example.invalid/delta-cut.png", kNow};
  saved.entries["driver charlie"] = player_photos::Entry{"", kNow};  // a miss
  return player_photos::PlayerPhotos(saved);
}

const DriverPill& pill(const CameraPanel& panel, std::string_view name) {
  for (const DriverPill& p : panel.drivers) {
    if (p.name == name) return p;
  }
  FAIL("no pill " << name);
  return panel.drivers.front();
}

}  // namespace

TEST_CASE("cameras panel: header, quick picks, drivers, focus") {
  FollowedDrivers followed;
  followed.names = {"Driver Charlie"};
  CameraPanel panel = build(f1Race(), f1Feeds(), std::string("c|alpha2"), followed, raceData(), photos());
  CHECK_FALSE(panel.empty);
  CHECK(panel.f1);
  CHECK(panel.header.live == "LIVE");
  CHECK(panel.header.title == "Grand Prix Alpha - Race");
  CHECK(panel.header.hint == "Select: switch   \xC2\xB7   Play/Pause: follow a driver   \xC2\xB7   Back: close");
  REQUIRE(panel.quick.size() == 3);
  CHECK(panel.quick[0].title == "Main broadcast");
  CHECK(panel.quick[0].detail == "The race feed");
  CHECK_FALSE(panel.quick[0].onScreen);
  CHECK(panel.quick[2].kind == Kind::tracker);
  CHECK(panel.quick[2].channelID == "c|trk");

  REQUIRE(panel.drivers.size() == 4);
  CHECK(panel.onboardLabel == std::string("ONBOARD \xC2\xB7 4 DRIVERS"));
  CHECK(panel.columns == 4);
  CHECK(panel.drivers[0].name == "Driver Charlie");  // followed first
  CHECK(panel.drivers[0].followed);
  CHECK(panel.drivers[1].name == "Driver Alpha");

  // Race data: photo, number, colour; on screen through its backup.
  const DriverPill& alpha = pill(panel, "Driver Alpha");
  CHECK(alpha.detail == "McLaren \xC2\xB7 #4  \xC2\xB7  now");
  CHECK(alpha.onScreen);
  CHECK(alpha.badge);
  CHECK(alpha.code == "DRA");
  CHECK(alpha.color == std::string("FF8000"));
  CHECK(alpha.headshot == std::string("https://example.invalid/alpha.png"));
  CHECK(alpha.channelID == "c|alpha");
  CHECK(alpha.alternates == std::vector<std::string>{"c|alpha2"});
  CHECK_FALSE(alpha.followed);

  // Race data without a photo: the player search's, its number kept, the team colour.
  const DriverPill& bravo = pill(panel, "Driver Bravo");
  CHECK(bravo.detail == "Ferrari \xC2\xB7 #16");
  CHECK(bravo.code == "DRB");
  CHECK(bravo.color == std::string("DC0000"));
  CHECK(bravo.headshot == std::string("https://example.invalid/bravo-cut.png"));

  // Only a photo: initials and the team colour, no number.
  const DriverPill& delta = pill(panel, "Driver Delta");
  CHECK(delta.badge);
  CHECK(delta.code == "DD");
  CHECK(delta.detail == "Team Delta");
  CHECK_FALSE(delta.color);  // not a known team: a dim white
  CHECK(delta.headshot == std::string("https://example.invalid/delta-cut.png"));

  // Nothing known: a colour bar.
  const DriverPill& charlie = pill(panel, "Driver Charlie");
  CHECK_FALSE(charlie.badge);
  CHECK_FALSE(charlie.headshot);
  CHECK(charlie.detail == "Onboard");

  // Focus starts on the camera on screen.
  CHECK(panel.focusID == std::string("c|alpha"));
  CHECK(panel.focusIndex == size_t(4));
  CHECK(indexOf(panel, "xt|sky") == size_t(1));
  CHECK(indexOf(panel, "c|charlie") == size_t(3));
  CHECK_FALSE(indexOf(panel, "nope"));

  // Opening asks for the drivers, and photos for those the race data has none of.
  CHECK(panel.onOpen.raceData);
  CHECK_FALSE(panel.onOpen.nascarDrivers);
  CHECK(panel.onOpen.photos == std::vector<std::string>{"Driver Bravo", "Driver Charlie", "Driver Delta"});
}

TEST_CASE("cameras panel: focus without a camera on screen, quick pick now, empty") {
  CameraPanel panel = build(f1Race(), f1Feeds(), std::string("xt|sky"), {}, race_data::RaceData(), {});
  CHECK(panel.quick[1].detail == "Sky commentary  \xC2\xB7  now");
  CHECK(panel.quick[1].onScreen);
  CHECK(panel.focusIndex == size_t(1));
  CameraPanel other = build(f1Race(), f1Feeds(), std::string("xt|elsewhere"), {}, race_data::RaceData(), {});
  CHECK(other.focusID == std::string("xt|main"));
  CHECK(other.focusIndex == size_t(0));
  CHECK(pill(other, "Driver Alpha").detail == "McLaren");  // no race data: no number
  CHECK(pill(other, "Driver Alpha").color == std::string("FF8000"));  // the team's
  CHECK(other.onOpen.photos.size() == 4);
  std::vector<CameraFeed> onlyDrivers = {feed("c|a", "Driver Alpha", "McLaren", Kind::driver)};
  CameraPanel drivers = build(f1Race(), onlyDrivers, std::nullopt, {}, race_data::RaceData(), {});
  CHECK(drivers.quick.empty());
  CHECK(drivers.focusIndex == size_t(0));
  CameraPanel none = build(f1Race(), {}, std::nullopt, {}, race_data::RaceData(), {});
  CHECK(none.empty);
  CHECK_FALSE(none.focusIndex);
  CHECK_FALSE(none.onboardLabel);
}

TEST_CASE("cameras panel: NASCAR in-car feeds keep their car number") {
  SportsEvent race = plainEvent("e", "Race Alpha 500", "Motorsport", kNow);
  std::vector<CameraFeed> feeds = {feed("xt|1", "Main broadcast", "The race feed", Kind::main),
                                   feed("c|5", "Driver Echo", "No. 5", Kind::driver, std::nullopt, {}, 5),
                                   feed("c|g", "Driver Golf", "In-car camera", Kind::driver)};
  race_data::NascarSaved nascar;
  nascar.drivers = {race_data::RaceDriver{5, "5", "Driver Echo", "Team Echo", std::nullopt,
                                          std::string("https://example.invalid/echo.png")},
                    race_data::RaceDriver{48, "48", "Driver Golf", "Team Golf", std::nullopt,
                                          std::string("https://example.invalid/golf.png")}};
  race_data::RaceData data(race_data::Saved{}, nascar);
  CameraPanel panel = build(race, feeds, std::string("xt|1"), {}, data, {});
  CHECK_FALSE(panel.f1);
  CHECK(panel.header.title == "Race Alpha 500");
  CHECK(pill(panel, "Driver Echo").detail == "No. 5");
  CHECK(pill(panel, "Driver Echo").headshot == std::string("https://example.invalid/echo.png"));
  CHECK(pill(panel, "Driver Golf").detail == "In-car camera \xC2\xB7 #48");
  CHECK(pill(panel, "Driver Golf").code == "48");
  CHECK_FALSE(pill(panel, "Driver Golf").color);
  CHECK(panel.onOpen.nascarDrivers);
  CHECK_FALSE(panel.onOpen.raceData);
  CHECK(panel.onOpen.photos.empty());
  CHECK(panel.quick[0].detail == "The race feed  \xC2\xB7  now");
}

TEST_CASE("followed drivers: toggled, saved sorted, read back") {
  FollowedDrivers followed;
  CHECK(followed.toggle("Driver Bravo"));
  CHECK(followed.toggle("Driver Alpha"));
  CHECK(followed.contains("Driver Alpha"));
  CHECK(followed.toJson() == R"(["Driver Alpha","Driver Bravo"])");
  CHECK_FALSE(followed.toggle("Driver Bravo"));
  CHECK_FALSE(followed.contains("Driver Bravo"));
  FollowedDrivers back = FollowedDrivers::fromJson(followed.toJson());
  CHECK(back.names == std::set<std::string>{"Driver Alpha"});
  CHECK(FollowedDrivers::fromJson("not json").names.empty());
  CHECK(FollowedDrivers::fromJson(R"(["Driver A", 3, null])").names == std::set<std::string>{"Driver A"});
}

TEST_CASE("camera index: indexed once per channel list, cached per event") {
  std::vector<Channel> channels = {channel("c|alpha", "(F1 TV CAM) McLaren Driver Alpha"),
                                   channel("c|mix", "(F1 TV CAM) Mixed Onboard"), channel("x|1", "US: Sports One")};
  CameraIndex index;
  index.update(channels, "1|3");
  CHECK(index.candidates().size() == 2);
  SportsEvent race = f1Race();
  std::vector<Channel> own = {channel("xt|main", "F1: Grand Prix Alpha")};
  std::vector<CameraFeed> feeds = index.cameras(race, own);
  CHECK(feeds.size() == 3);
  channels.push_back(channel("c|bravo", "(F1 TV CAM) Ferrari Driver Bravo"));
  index.update(channels, "1|3");  // same key: not indexed again
  CHECK(index.candidates().size() == 2);
  CHECK(index.cameras(race, own).size() == 3);
  index.update(channels, "2|4");
  CHECK(index.candidates().size() == 3);
  CHECK(index.cameras(race, own).size() == 4);
  SportsEvent game = plainEvent("g", "Team Alpha vs Team Beta", "Soccer", kNow);
  CHECK(index.cameras(game, own).empty());
}

TEST_CASE("switching cameras: what to tune") {
  auto isEntry = [](const std::string& id) { return id.rfind("e|", 0) == 0; };
  auto versions = [](const std::string& id) { return std::vector<std::string>{"xt|" + id.substr(2), "jf|" + id.substr(2)}; };
  CHECK(tuneList("c|alpha", {"c|alpha2", "c|alpha"}, isEntry, versions) ==
        std::vector<std::string>{"c|alpha", "c|alpha2"});
  CHECK(tuneList("e|cam", {"xt|cam", "c|b"}, isEntry, versions) ==
        std::vector<std::string>{"xt|cam", "jf|cam", "c|b"});
  CHECK(tuneList("c|alpha", {}, nullptr, nullptr) == std::vector<std::string>{"c|alpha"});
  CHECK_FALSE(watchList("c|alpha", {"c|alpha2"}, std::string("c|alpha2"), isEntry, versions));  // on screen
  CHECK_FALSE(watchList("c|alpha", {}, std::string("c|alpha"), isEntry, versions));
  CHECK(watchList("c|alpha", {"c|alpha2"}, std::string("xt|main"), isEntry, versions) ==
        std::vector<std::string>{"c|alpha", "c|alpha2"});
  CHECK(watchList("c|alpha", {}, std::nullopt, isEntry, versions));
  CHECK(std::string(kCamerasHint) == "Press \xE2\x96\xBC for onboard cameras");
}
