// Special feeds by name: which channels are race cameras, in-car feeds by
// car number (and what isn't one: ranked games, team sports' shows), the
// race a camera belongs to, the F1 camera sets (provider family first, the
// others as backups, one camera per surname), NASCAR in-car feeds and
// multiviews of a race, F1 vs NASCAR events, and the F1 channels. Drivers
// and races are invented; team names are the ones the rules know.
#include <algorithm>

#include "core/sports/board.hpp"
#include "core/sports/special_feeds.hpp"
#include "core/sports/store.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using namespace marigold::sports::special_feeds;
using Kind = CameraFeed::Kind;
using sportsdata::plainEvent;

namespace {

Channel channel(const std::string& id, const std::string& name, std::optional<std::string> group = std::nullopt) {
  Channel c;
  c.id = id;
  c.name = name;
  c.group = std::move(group);
  return c;
}

std::vector<std::string> titles(const std::vector<CameraFeed>& feeds) {
  std::vector<std::string> out;
  for (const CameraFeed& f : feeds) out.push_back(f.title);
  return out;
}

const CameraFeed& named(const std::vector<CameraFeed>& feeds, std::string_view title) {
  auto it = std::find_if(feeds.begin(), feeds.end(), [&](const CameraFeed& f) { return f.title == title; });
  REQUIRE(it != feeds.end());
  return *it;
}

}  // namespace

TEST_CASE("camera names: the cheap test over the whole list") {
  CHECK(isCameraName("(F1 TV CAM) McLaren Driver Alpha"));
  CHECK(isCameraName("UK: F1 CAM Driver Bravo"));
  CHECK(isCameraName("Motor Sports: Driver Golf Speedway Driver Cam (10.25 1:40PM ET)"));
  CHECK(isCameraName("Race Alpha 500: NASCAR Chase Leaders Multiview (10.25 1:40PM ET)"));
  CHECK(isCameraName("US| Race Alpha 500: No. 5 Driver Alpha (10.25 1:45PM ET)"));
  CHECK(isCameraName("Driver Alpha | McLaren | DRA"));
  CHECK(isCameraName("F1: DRIVER ALPHA | FERRARI | DRA"));
  CHECK(isCameraName("F1 DATA"));
  CHECK(isCameraName("F1: F1-TRACKER"));
  CHECK_FALSE(isCameraName("US: Sports Channel FHD"));
  CHECK_FALSE(isCameraName("Channel One | UK | ENG"));      // not an F1 team
  CHECK_FALSE(isCameraName("Driver Alpha | Team Alpha | DRA"));
  CHECK_FALSE(isCameraName("Tennis Multiview"));            // no NASCAR, no driver
}

TEST_CASE("in-car feeds by car number, not ranked games or team shows") {
  auto car = carCamera("Race Alpha 500: No. 5 Driver Alpha (10.25 1:45PM ET)");
  REQUIRE(car);
  CHECK(car->number == 5);
  CHECK(car->driver == "Driver Alpha");
  auto bare = carCamera("NASCAR: No 24 Driver Delta Echo");
  REQUIRE(bare);
  CHECK(bare->number == 24);
  CHECK(bare->driver == "Driver Delta Echo");
  CHECK_FALSE(carCamera("NCAAF: No. 5 Team Alpha vs No. 8 Team Beta"));     // a game
  CHECK_FALSE(carCamera("Rankings: No. 5 Team Alpha - No. 8 Team Beta"));   // another ranking
  CHECK_FALSE(carCamera("College GameDay: No. 3 Team Alpha Aggies"));       // a team sport's show
  CHECK_FALSE(carCamera("College Football: No. 3 Team Alpha"));
  CHECK_FALSE(carCamera("Race Alpha 500: No. 5 Driver"));                   // one word
  CHECK_FALSE(carCamera("Race Alpha 500: No. 5 One Two Three Four Five"));  // five words
  CHECK_FALSE(carCamera("Race Alpha 500: No. 5 Driver 2 Alpha"));           // a digit
  CHECK_FALSE(carCamera("Race Alpha 500: No. 0 Driver Alpha"));             // no car 0
  CHECK_FALSE(carCamera("Race Alpha 500 Driver Alpha"));
}

TEST_CASE("the race a camera belongs to") {
  CHECK(cameraRace("Race Alpha 500: No. 5 Driver Alpha (10.25 1:45PM ET)") == std::string("race alpha 500"));
  CHECK(cameraRace("Race Alpha 500: NASCAR Chase Leaders Multiview (10.25 1:40PM ET)") ==
        std::string("race alpha 500"));
  CHECK(cameraRace("NASCAR: No 9 Driver Foxtrot") == std::string("nascar"));
  CHECK(genericNascarRaces().count("nascar"));
  CHECK_FALSE(cameraRace("Abc: No. 5 Driver Alpha"));  // too short to name a race
  CHECK_FALSE(cameraRace("(F1 TV CAM) McLaren Driver Alpha"));
  CHECK_FALSE(cameraRace("Driver Multiview"));  // no race part

  // The shapes the board and the rebuild take.
  board::Input in;
  in.isCameraName = isCameraName;
  in.cameraRace = cameraRace;
  store::RebuildInput rebuild;
  rebuild.isCameraName = isCameraName;
  CHECK(in.cameraRace("Race Alpha 500: No. 5 Driver Alpha") == std::string("race alpha 500"));
  CHECK(rebuild.isCameraName("F1 DATA"));
}

TEST_CASE("F1 or NASCAR") {
  SportsEvent f1 = plainEvent("e1", "Formula 1: Grand Prix Alpha", "Motorsport", 0);
  f1.leagueShort = "MOTOR";
  CHECK(isF1(f1));
  CHECK_FALSE(isNASCAR(f1));
  SportsEvent shortF1 = plainEvent("e2", "Grand Prix Alpha", "Motorsport", 0);
  shortF1.leagueShort = "F1";
  CHECK(isF1(shortF1));
  SportsEvent cup = plainEvent("e3", "Cup Series: Race Alpha 500", "Motorsport", 0);
  CHECK(isNASCAR(cup));
  CHECK_FALSE(isF1(cup));
  SportsEvent sponsor = plainEvent("e4", "Race Alpha 500", "Motorsport", 0);
  CHECK(isNASCAR(sponsor));  // named only by its sponsor
  SportsEvent nascarF1 = plainEvent("e5", "F1 fans at Race Alpha", "Motorsport", 0);
  nascarF1.leagueShort = "NASCAR";
  CHECK_FALSE(isF1(nascarF1));
  CHECK(isNASCAR(nascarF1));
  SportsEvent golf = plainEvent("e6", "Open Alpha", "Golf", 0);
  CHECK_FALSE(isNASCAR(golf));
  CHECK_FALSE(isF1(golf));
  CHECK(teamColor(std::string("Ferrari")) == std::string("DC0000"));
  CHECK_FALSE(teamColor(std::string("Team Alpha")));
  CHECK_FALSE(teamColor(std::nullopt));
}

TEST_CASE("F1 cameras: own feeds, quick picks, drivers once per surname") {
  SportsEvent race = plainEvent("e", "Grand Prix Alpha", "Motorsport", 0);
  race.leagueShort = "F1";
  std::vector<Channel> own = {channel("xt|1", "F1: Grand Prix Alpha"), channel("xt|2", "UK: Sky Sports F1 HD"),
                              channel("xt|3", "F1: Grand Prix Alpha (backup)")};
  std::vector<Channel> all = {
      channel("c|mix", "(F1 TV CAM) Mixed Onboard"),
      channel("c|trk", "(F1 TV CAM) Driver Tracker"),
      channel("c|dat", "(F1 TV CAM) Data Channel"),
      channel("c|p2", "(F1 TV CAM) P2 Onboard"),
      channel("c|alpha", "(F1 TV CAM) McLaren Driver Alpha"),
      channel("c|bravo", "(F1 TV CAM) Ferrari Driver Bravo"),
      channel("c|apple-alpha", "(Apple F1 CAM) McLaren Driver Alpha"),
      channel("c|pipe-alpha", "F1: DRIVER ALPHA JR. | MCLAREN | DRA"),
      channel("c|plain", "(F1 TV CAM) Driver Charlie"),
      channel("c|sports", "US: Sports Channel"),
  };
  std::vector<Channel> candidates = cameraCandidates(all);
  CHECK(candidates.size() == 9);
  std::vector<CameraFeed> feeds = cameras(race, own, candidates);
  CHECK(titles(feeds) == std::vector<std::string>{"Main broadcast", "Sky Sports", "Feed 3", "Mixed onboard",
                                                  "Driver Tracker", "Driver Data", "P2 driver", "Driver Alpha",
                                                  "Driver Bravo", "Driver Charlie"});
  CHECK(feeds[0].kind == Kind::main);
  CHECK(feeds[0].detail == "The race feed");
  CHECK(feeds[1].kind == Kind::alt);
  CHECK(feeds[1].detail == "Sky commentary");
  CHECK(feeds[2].detail == "Another feed");
  CHECK(named(feeds, "P2 driver").detail == "Follows P2");
  CHECK(named(feeds, "P2 driver").kind == Kind::podium);
  CHECK(named(feeds, "Driver Tracker").detail == "Bird's-eye map");
  const CameraFeed& alpha = named(feeds, "Driver Alpha");
  CHECK(alpha.channelID == "c|alpha");  // the race feed's family is not the Apple one
  CHECK(alpha.team == std::string("McLaren"));
  CHECK(alpha.detail == "McLaren");
  CHECK(alpha.alternates == std::vector<std::string>{"c|apple-alpha", "c|pipe-alpha"});
  CHECK(named(feeds, "Driver Charlie").detail == "Onboard");
  CHECK_FALSE(named(feeds, "Driver Charlie").team);

  // A race feed of the Apple family: its cameras lead.
  std::vector<Channel> appleOwn = {channel("xt|a", "(Apple) (F1) Grand Prix Alpha")};
  std::vector<CameraFeed> apple = cameras(race, appleOwn, candidates);
  CHECK(named(apple, "Driver Alpha").channelID == "c|apple-alpha");
  CHECK(named(apple, "Driver Alpha").alternates == std::vector<std::string>{"c|alpha", "c|pipe-alpha"});

  // Only the race's own feeds isn't a cameras panel.
  CHECK(cameras(race, own, {}).empty());
  // Not a race: no cameras.
  SportsEvent game = plainEvent("g", "Team Alpha vs Team Beta", "Soccer", 0);
  CHECK(cameras(game, own, candidates).empty());
}

TEST_CASE("F1 camera sets by pipe name") {
  CHECK(isF1CameraName("Driver Alpha | McLaren | DRA"));
  CHECK(isF1CameraName("  f1 tracker "));
  CHECK_FALSE(isF1CameraName("DriverAlpha | McLaren | DRA"));  // one word
  CHECK_FALSE(isF1CameraName("Driver Alpha | McLaren | DR"));  // two letters
  CHECK_FALSE(isF1CameraName("Driver Alpha | McLaren | Dra"));  // not capitals
  SportsEvent race = plainEvent("e", "Grand Prix Alpha", "Motorsport", 0);
  race.leagueShort = "F1";
  std::vector<CameraFeed> feeds =
      cameras(race, {channel("xt|1", "F1: Grand Prix Alpha")},
              {channel("p|1", "F1: DRIVER ALPHA | RED BULL RACING | DRA"), channel("p|2", "F1 DATA")});
  CHECK(titles(feeds) == std::vector<std::string>{"Main broadcast", "Driver Data", "Driver Alpha"});
  CHECK(named(feeds, "Driver Alpha").team == std::string("Red Bull Racing"));
}

TEST_CASE("NASCAR cameras: the race's in-car feeds and multiviews, by car number") {
  SportsEvent race = plainEvent("e", "Race Alpha 500 at Speedway Bravo", "Motorsport", 0);
  std::vector<Channel> own = {channel("xt|1", "Race Alpha 500")};
  std::vector<Channel> all = {
      channel("c|24", "US| Race Alpha 500: No. 24 Driver Delta (10.25 1:45PM ET)"),
      channel("c|5", "Race Alpha 500: No. 5 Driver Echo"),
      channel("c|9", "NASCAR: No 9 Driver Foxtrot"),
      channel("c|mv", "Race Alpha 500: NASCAR Chase Leaders Multiview (10.25 1:40PM ET)"),
      channel("c|cam", "Motor Sports: Driver Golf Speedway Driver Cam (10.25 1:40PM ET)"),
      channel("c|other", "Other Race 400: No. 7 Driver Hotel"),
      channel("xt|1", "Race Alpha 500: No. 3 Driver India"),  // already one of the event's feeds
  };
  std::vector<CameraFeed> feeds = cameras(race, own, cameraCandidates(all));
  CHECK(titles(feeds) == std::vector<std::string>{"Main broadcast", "Chase Leaders Multiview", "Driver Echo",
                                                  "Driver Foxtrot", "Driver Delta", "Driver Golf"});
  CHECK(named(feeds, "Driver Echo").detail == "No. 5");
  CHECK(named(feeds, "Driver Echo").number == 5);
  CHECK(named(feeds, "Chase Leaders Multiview").detail == "Several cars at once");
  CHECK(named(feeds, "Chase Leaders Multiview").kind == Kind::multiview);
  CHECK(named(feeds, "Driver Golf").detail == "In-car camera");
  CHECK_FALSE(named(feeds, "Driver Golf").number);
  // A title of only stop words has no key: no cameras.
  SportsEvent bare = plainEvent("b", "NASCAR Cup Series Race", "Motorsport", 0);
  CHECK(cameras(bare, own, cameraCandidates(all)).empty());
}

TEST_CASE("F1 channels: dedicated ones, yours first, a few English and others") {
  std::vector<Channel> entries = {
      channel("e|sky", "UK: Sky Sports F1 HD", std::string("UK")),
      channel("e|sky2", "UK: Sky Sports F1 FHD", std::string("UK")),
      channel("e|sky3", "UK: Sky Sports F1 4K", std::string("UK")),
      channel("e|sky4", "UK: Sky Sports F1 Backup", std::string("UK")),
      channel("e|de", "DE: Sky Sport F1", std::string("DE")),
      channel("e|dazn", "ES: DAZN F1", std::string("ES")),
      channel("e|f1tv", "F1 TV"),
      channel("e|mine", "NL: Ziggo Sport F1", std::string("NL")),
      channel("e|hidden", "IT: Sky Sport F1", std::string("IT")),
      channel("e|cam", "F1 TV CAM Driver Alpha"),
      channel("e|data", "UK: Sky Sports F1 Data"),
      channel("e|news", "UK: F1 News"),
      channel("e|event", "UK: Sky Sports F1 Event", std::string("UK")),
  };
  std::unordered_map<std::string, catalog::EntryInfo> info;
  for (const Channel& c : entries) info[c.id].kind = catalog::ChannelKind::channel;
  info["e|event"].kind = catalog::ChannelKind::event;
  std::vector<EventSource> sources = f1ChannelSources(entries, info, {"IT"}, "Other", {"e|mine"});
  std::vector<std::string> ids;
  for (const EventSource& s : sources) ids.push_back(s.channelID);
  CHECK(ids == std::vector<std::string>{"e|mine", "e|sky", "e|sky2", "e|sky3", "e|de", "e|dazn"});
  for (const EventSource& s : sources) {
    CHECK(s.kind == EventSource::Kind::team);
    CHECK(s.detail == "F1 channel");
  }
  CHECK(sources[5].spanish);
  CHECK_FALSE(sources[1].spanish);
  // An entry in the "Other" group hides with it.
  CHECK(f1ChannelSources(entries, info, {"Other"}, "Other", {}).size() == 7);

  CHECK(f1ChannelsKey(1, 13, {"a"}, {}) == f1ChannelsKey(1, 13, {"a"}, {}));
  CHECK(f1ChannelsKey(1, 13, {"a"}, {}) != f1ChannelsKey(1, 13, {"b"}, {}));
  CHECK(f1ChannelsKey(1, 13, {}, {"a"}) != f1ChannelsKey(1, 13, {"a"}, {}));
  CHECK(f1ChannelsKey(2, 13, {}, {}) != f1ChannelsKey(1, 13, {}, {}));
  SportsGame session = sportsdata::game("F1", "Motorsport", "ev|race", sportsdata::team("gp", "Grand Prix Alpha"),
                                        sportsdata::team("gp", "Grand Prix Alpha"), 0);
  CHECK(wantsF1Sources({session}));
  session.completed = true;
  CHECK_FALSE(wantsF1Sources({session}));
}
