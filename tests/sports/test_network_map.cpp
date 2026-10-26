// Networks to channels: regional sports networks, call signs, station
// ranks by market, the channel index and each game's channel sources
// (national network on your local affiliate, regional networks, stations
// by call sign, team and fight channels, Spanish, regional splits).
#include "core/catalog/lineup.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/network_map.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using catalog::EntryInfo;
using catalog::LineupCategory;
using sportsdata::game;
using sportsdata::team;
using sportsdata::utc;

TEST_CASE("regional network keys under their many names") {
  CHECK(regional::key("FDSN Sun") == "fd-sun");
  CHECK(regional::key("BALLY SPORTS SUN (MIAMI) HD") == "fd-sun");
  CHECK(regional::key("FOX SPORTS SOUTHEAST HD") == "fd-southeast");
  CHECK(regional::key("FanDuel Sports Network Tennessee") == "fd-south");
  CHECK(regional::key("NBCS-BA") == "nbcs-ba");
  CHECK(regional::key("NBC Sports Bay Area") == "nbcs-ba");
  CHECK(regional::key("Spectrum SportsNet Dodgers") == "snla");
  CHECK(regional::key("Spectrum Sports Net Lakers") == "spectrum");
  CHECK(regional::key("YES") == "yes");
  CHECK(regional::key("MSG+") == "msg2");
  CHECK(regional::key("MLB.TV") == std::nullopt);
  CHECK(regional::key("Bally Sports") == std::nullopt);  // no region
  CHECK(regional::key("Harbor News") == std::nullopt);
}

TEST_CASE("call signs alone, in channel names, and station brands") {
  CHECK(regional::callsign("KJZZ") == "KJZZ");
  CHECK(regional::callsign("KJZZ-TV") == "KJZZ");
  CHECK(regional::callsign("kjzz-dt") == "KJZZ");
  CHECK(regional::callsign("KUNP 16") == "KUNP");
  CHECK(regional::callsign("KING 5") == "KING");
  CHECK(regional::callsign("WEST 2") == std::nullopt);
  CHECK(regional::callsign("KABCDE") == std::nullopt);
  CHECK(regional::callsignInName("FOX 13 (KSTU)") == "KSTU");
  CHECK(regional::callsignInName("PBS (PBS WKYU-DT)") == "WKYU");
  CHECK(regional::callsignInName("SALT LAKE CITY, UT KJZZ IND 14") == "KJZZ");
  CHECK(regional::callsignInName("IND KFAA") == "KFAA");
  CHECK(regional::callsignInName("WILD TV") == std::nullopt);
  CHECK(regional::stationBrand("Arizona's Family CBS5") == "KPHO");
  CHECK(regional::stationBrand("Harbor CBS5") == std::nullopt);
}

TEST_CASE("schedule network names to keys") {
  CHECK(network_map::networkKey("FOX") == "net:FOX");
  CHECK(network_map::networkKey("CBS") == "net:CBS");
  CHECK(network_map::networkKey("ESPN") == "core:ESPN");
  CHECK(network_map::networkKey("FS1") == "core:FS1");
  CHECK(network_map::networkKey("NFL Net") == "core:NFL Network");
  CHECK(network_map::networkKey("FDSN Sun") == "rsn:fd-sun");
  CHECK(network_map::networkKey("KJZZ") == "call:KJZZ");
  CHECK(network_map::networkKey("AZFamily 3TV") == "call:KTVK");
  CHECK(network_map::networkKey("NBA League Pass") == std::nullopt);
}

namespace {

EntryInfo info(const std::string& id, const std::string& name, LineupCategory category,
               std::optional<std::string> callsign = std::nullopt, std::optional<std::string> core = std::nullopt,
               std::optional<std::string> market = std::string("US")) {
  EntryInfo e;
  e.id = id;
  e.name = name;
  e.category = category;
  e.callsign = std::move(callsign);
  e.core = std::move(core);
  e.market = std::move(market);
  return e;
}

Channel entry(const std::string& id, const std::string& name, const std::string& group = "US") {
  Channel c;
  c.id = id;
  c.name = name;
  c.group = group;
  return c;
}

catalog::LineupPrefs prefsWithMarkets() {
  catalog::LineupPrefs prefs;
  prefs.markets = {{"Harbor City", std::nullopt, {"KSTU-DT", "KHBR"}}, {"River Town", std::nullopt, {"KJZZ", "KSTU"}}};
  return prefs;
}

}  // namespace

TEST_CASE("station markets, ranks and the home city") {
  catalog::LineupPrefs prefs = prefsWithMarkets();
  StationMarkets markets = stationMarkets(prefs);
  CHECK(homeMarketName(prefs) == "Harbor City");
  CHECK(markets.home == std::set<std::string>{"KSTU", "KHBR"});
  CHECK(markets.others == std::set<std::string>{"KJZZ"});  // KSTU is home already
  CHECK(nextHomeMarket(prefs) == "River Town");
  prefs.homeMarket = "River Town";
  CHECK(homeMarketName(prefs) == "River Town");
  CHECK(nextHomeMarket(prefs) == "Harbor City");
  CHECK(stationMarkets(prefs).home == std::set<std::string>{"KJZZ", "KSTU"});
  prefs.homeMarket = "Nowhere";
  CHECK(homeMarketName(prefs) == "Harbor City");

  EntryInfo fox13 = info("e|fox13", "FOX 13 (KSTU)", LineupCategory::local, std::string("KSTU-DT"));
  CHECK(stationRank(fox13, markets, false) == station_rank::home);
  CHECK(stationRank(fox13, markets, true) == station_rank::lineupHome);
  EntryInfo kjzz = info("e|kjzz", "KJZZ 14", LineupCategory::local, std::string("KJZZ"));
  CHECK(stationRank(kjzz, markets, false) == station_rank::market);
  CHECK(stationRank(kjzz, markets, true) == station_rank::lineup);
  EntryInfo far = info("e|far", "FOX 2 (KTVU)", LineupCategory::local);
  CHECK(stationRank(far, markets, false) == station_rank::elsewhere);
  EntryInfo sports = info("e|fs1", "FS1", LineupCategory::sports, std::nullopt, std::string("FS1"));
  CHECK(stationRank(sports, markets, false) == station_rank::elsewhere);
  CHECK(stationDetail(station_rank::lineupHome) == "In My Lineup");
  CHECK(stationDetail(station_rank::lineup) == "In My Lineup");
  CHECK(stationDetail(station_rank::home) == "Your local station");
  CHECK(stationDetail(station_rank::market) == "Your local station");
  CHECK(stationDetail(station_rank::elsewhere) == "In your channels");
}

TEST_CASE("channel index and each game's channels") {
  catalog::LineupPrefs prefs = prefsWithMarkets();
  StationMarkets markets = stationMarkets(prefs);
  std::unordered_map<std::string, EntryInfo> infos;
  auto add = [&](EntryInfo e) { infos[e.id] = std::move(e); };
  add(info("e|fox13", "FOX 13 (KSTU)", LineupCategory::local, std::string("KSTU")));
  add(info("e|foxfar", "FOX 2 (KTVU)", LineupCategory::local, std::string("KTVU")));
  add(info("e|kjzz", "KJZZ 14", LineupCategory::local, std::string("KJZZ")));
  add(info("e|espn", "ESPN", LineupCategory::sports, std::nullopt, std::string("ESPN")));
  add(info("e|rsn", "FanDuel Sports Sun HD", LineupCategory::sports));
  add(info("e|hawks", "NBA Valley Hawks", LineupCategory::sports));
  add(info("e|ufcpre", "UFC PPV Prelims", LineupCategory::sports, std::nullopt, std::nullopt, std::nullopt));
  add(info("e|tele", "Telemundo 39 (KHBR)", LineupCategory::local, std::string("KHBR"), std::nullopt,
           std::string("US-ES")));
  add(info("e|hidden", "FDSN Sun Alt", LineupCategory::sports));
  std::vector<Channel> lineupEntries = {entry("e|fox13", "FOX 13 (KSTU)")};
  std::vector<Channel> browse = {entry("e|foxfar", "FOX 2 (KTVU)"), entry("e|kjzz", "KJZZ 14"),
                                 entry("e|espn", "ESPN"), entry("e|rsn", "FanDuel Sports Sun HD"),
                                 entry("e|hawks", "NBA Valley Hawks"), entry("e|tele", "Telemundo 39 (KHBR)"),
                                 entry("e|hidden", "FDSN Sun Alt", "Hidden Group")};
  std::vector<Channel> all = browse;
  all.push_back(entry("e|ufcpre", "UFC PPV Prelims", "PPV"));
  std::vector<matcher::RosterLeague> rosters = {
      {"NBA", "NBA", false, {team("4", "Valley Hawks"), team("3", "River Town Otters")}}};
  auto index = network_map::channelIndex({"e|fox13"}, lineupEntries, browse, all, infos, {"Hidden Group"}, rosters,
                                         markets);
  CHECK(index.byKey.count("net:FOX"));
  CHECK(index.byKey.at("net:FOX").size() == 2);
  CHECK(index.byKey.count("call:KSTU"));
  CHECK(index.byKey.count("core:ESPN"));
  CHECK(index.byKey.at("rsn:fd-sun").size() == 1);  // the hidden group's isn't
  CHECK(index.byKey.count("team:NBA|4"));
  CHECK(index.byKey.count("card:ufc|prelims"));
  CHECK(index.teamChannels == 1);
  CHECK(index.fightChannels == 1);
  CHECK(index.byKey.at("net:TELEMUNDO").front().spanish);

  Seconds sunday = utc(2026, 10, 25, 17, 0);
  SportsGame onFox = game("NFL", "NFL", "1", team("1", "Harbor City Gulls"), team("2", "Team Alpha"), sunday, {"FOX"});
  SportsGame regional = game("NBA", "NBA", "2", team("3", "River Town Otters"), team("4", "Valley Hawks"),
                             sunday + 6 * 3600, {"ESPN", "FDSN Sun", "NBA League Pass"});
  SportsGame station = game("NHL", "NHL", "3", team("5", "Mountain Goats"), team("6", "Lake Pikes"),
                            sunday + 3 * 3600, {"KJZZ", "Unknown Net"});
  SportsGame spanish = game("MLS", "MLS", "4", team("7", "Harbor FC"), team("8", "Coast FC"), sunday + 8 * 3600,
                            {"Telemundo"}, std::nullopt, true);
  SportsGame card = game("UFC", "Fighting", "600|prelims", team("f1", "Max Stone"), team("f2", "Lee River"),
                         sunday + 10 * 3600);
  card.cardKey = "ufc|333|prelims";
  SportsGame done = onFox;
  done.id = "NFL|9";
  done.completed = true;
  auto mapped = network_map::networkSources({onFox, regional, station, spanish, card, done}, index);
  // A national game on a broadcast network: your own markets' stations only.
  REQUIRE(mapped.sources.count("NFL|1"));
  const auto& fox = mapped.sources.at("NFL|1");
  REQUIRE(fox.size() == 1);
  CHECK(fox[0].channelID == "e|fox13");
  CHECK(fox[0].kind == EventSource::Kind::lineup);
  CHECK(fox[0].rank == station_rank::lineupHome);
  CHECK(fox[0].detail == "In My Lineup");
  // A network that is the broadcast itself, a regional network, a team's channel.
  const auto& nba = mapped.sources.at("NBA|2");
  REQUIRE(nba.size() == 3);
  CHECK(nba[0].channelID == "e|espn");
  CHECK(nba[0].kind == EventSource::Kind::guide);
  CHECK(nba[1].channelID == "e|rsn");
  CHECK(nba[2].channelID == "e|hawks");
  CHECK(nba[2].kind == EventSource::Kind::team);
  CHECK(nba[2].detail == "Team channel");
  // A station by its call sign.
  const auto& nhl = mapped.sources.at("NHL|3");
  REQUIRE(nhl.size() == 1);
  CHECK(nhl[0].channelID == "e|kjzz");
  CHECK(nhl[0].detail == "Your local station");
  CHECK(mapped.stats.unknown.at("Unknown Net") == 1);
  CHECK(mapped.stats.unknown.at("NBA League Pass") == 1);
  // Spanish stations are marked so.
  const auto& mls = mapped.sources.at("MLS|4");
  REQUIRE(mls.size() == 1);
  CHECK(mls[0].spanish);
  // A numbered card's part on the promotion's channel for it.
  const auto& ufc = mapped.sources.at("UFC|600|prelims");
  REQUIRE(ufc.size() == 1);
  CHECK(ufc[0].channelID == "e|ufcpre");
  CHECK(ufc[0].detail == "Fight channel");
  CHECK_FALSE(mapped.sources.count("NFL|9"));  // finished games aren't looked up

  // Two games on FOX at once (regional coverage): neither gets the station.
  SportsGame other = onFox;
  other.id = "NFL|5";
  auto split = network_map::networkSources({onFox, other}, index);
  CHECK(split.sources.empty());
  CHECK(split.stats.split == 2);
  CHECK(split.stats.summary().find("2 regional splits") != std::string::npos);
}
