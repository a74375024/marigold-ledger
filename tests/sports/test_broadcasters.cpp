// Build 143: every broadcaster name the schedule writes (the broadcaster
// table, regional networks, stations by brand or call sign anywhere in the
// name, streaming services), and the channels that carry them: Spanish
// stations, alternates and event slots named after a network.
#include "core/catalog/lineup.hpp"
#include "core/report/live_events_report.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/network_map.hpp"
#include "doctest.h"
#include "sports/helpers.hpp"

using namespace marigold;
using namespace marigold::sports;
using catalog::EntryInfo;
using catalog::LineupCategory;
using network_map::networkKey;
using network_map::streamingName;
using sportsdata::game;
using sportsdata::team;
using sportsdata::utc;

TEST_CASE("broadcaster names the rules didn't read") {
  CHECK(networkKey("Golf Chnl") == "core:Golf Channel");
  CHECK(networkKey("NBCSN") == "core:NBCSN");
  CHECK(networkKey("ION") == "core:ION");  // one national feed
  CHECK(networkKey("NBC Sports Phil") == "rsn:nbcs-ph");
  CHECK(networkKey("NBC Sports Phil +") == "rsn:nbcs-ph");
  CHECK(networkKey("NBC Sports BA") == "rsn:nbcs-ba");
  CHECK(networkKey("NBC Sports BA +") == "rsn:nbcs-ba");
  CHECK(networkKey("NBCSCA+") == "rsn:nbcs-ca");
  CHECK(networkKey("NBC Sports CA") == "rsn:nbcs-ca");
  CHECK(networkKey("NBC Sports BO") == "rsn:nbcs-bos");
  CHECK(networkKey("MSGSN") == "rsn:msgsn");
  CHECK(networkKey("MSG") == "rsn:msg");
  CHECK(networkKey("MSGB") == "rsn:msgb");
  CHECK(networkKey("MS2") == "rsn:msg2");
  CHECK(networkKey("MSG2") == "rsn:msg2");
  CHECK(networkKey("Marquee Sports Net") == "rsn:marquee");
  CHECK(networkKey("MASN") == "rsn:masn");
  CHECK(networkKey("SNY") == "rsn:sny");
  CHECK(networkKey("YES") == "rsn:yes");
  CHECK(networkKey("ABTV") == "rsn:abtv");
  CHECK(networkKey("Midco Sports Net") == "rsn:midco");
  CHECK(networkKey("MNMT2") == "rsn:monumental");
  CHECK(networkKey("FanDuel SN IN") == "rsn:fd-indiana");
  CHECK(networkKey("FanDuel SN WI") == "rsn:fd-wisconsin");
  CHECK(networkKey("FanDuel SN WIX") == "rsn:fd-wisconsin");
  CHECK(networkKey("FanDuel SN OK") == "rsn:fd-oklahoma");
  CHECK(networkKey("FanDuel SN DET") == "rsn:fd-detroit");
  CHECK(networkKey("FanDuel SN DET Ext") == "rsn:fd-detroit");
  CHECK(networkKey("FanDuel SN OH") == "rsn:fd-ohio");
  CHECK(networkKey("FanDuel SN MW") == "rsn:fd-midwest");
  CHECK(networkKey("FanDuel SN West") == "rsn:fd-west");
  CHECK(networkKey("FanDuel SN SoCal") == "rsn:fd-socal");
  // Spanish.
  CHECK(networkKey("ESPN Deportes") == "core:ESPN Deportes");
  CHECK(networkKey("Universo") == "core:NBC Universo");
  CHECK(networkKey("Tele") == "net:TELEMUNDO");
  CHECK(networkKey("UniMás") == "net:UNIMAS");
  CHECK(networkKey("Unimas") == "net:UNIMAS");
  CHECK(networkKey("Telemundo") == "net:TELEMUNDO");
  CHECK(networkKey("Fox Deportes") == "core:Fox Deportes");
  CHECK(networkKey("FOX SPORTS DEPORTES") == "core:Fox Deportes");
  CHECK(networkKey("Ryz Sports Network") == "core:Ryz Sports");
  CHECK(networkKey("TUDN") == "core:TUDN");
  CHECK(networkKey("Galavisión") == "core:Galavision");
  // As before.
  CHECK(networkKey("ESPN") == "core:ESPN");
  CHECK(networkKey("NFL Net") == "core:NFL Network");
  CHECK(networkKey("FOX") == "net:FOX");
}

TEST_CASE("regional networks under their channel names") {
  CHECK(regional::key("MSG+") == "msgsn");
  CHECK(regional::key("MSG SPORTSNET") == "msgsn");
  CHECK(regional::key("MSG PLUS HD") == "msgsn");
  CHECK(regional::key("MSG HD") == "msg");
  CHECK(regional::key("MADISON SQUARE GARDEN") == "msg");
  CHECK(regional::key("MSG 2 HD") == "msg2");
  CHECK(regional::key("MSG SPORTSZONE") == std::nullopt);  // a streaming channel
  CHECK(regional::key("Mid-Atlantic Sports Network") == "masn");
  CHECK(regional::key("SportsNet New York") == "sny");
  CHECK(regional::key("FOX SPORTS YES HD") == "yes");
  CHECK(regional::key("US: FOX SPORTS YES NETWORK HD") == "yes");
  CHECK(regional::key("Marquee Sports Network") == "marquee");
  CHECK(regional::key("Marquee") == "marquee");
  CHECK(regional::key("MGM+ Marquee") == std::nullopt);
  CHECK(regional::key("SportsTime Ohio") == "fd-greatlakes");
  CHECK(regional::key("FOX SPORTS SPORTSTIME OHIO HD") == "fd-greatlakes");
  CHECK(regional::key("FOX SPORTSTIME OHIO HD") == "fd-greatlakes");
  CHECK(regional::key("FOX SPORTS OHIO") == "fd-ohio");
  CHECK(regional::key("Angels Broadcast Television") == "abtv");
  CHECK(regional::key("Midco Sports Plus") == std::nullopt);
  // West (Kings, Angels) and SoCal (Prime Ticket: Clippers, Ducks) apart.
  CHECK(regional::key("FOX SPORTS WEST HD") == "fd-west");
  CHECK(regional::key("BALLY SPORTS WEST HD") == "fd-west");
  CHECK(regional::key("Prime Sports West") == "fd-west");
  CHECK(regional::key("FOX SPORTS PRIME TICKET HD") == "fd-socal");
  CHECK(regional::key("BALLY SPORTS PRIME TICKET HD") == "fd-socal");
  CHECK(regional::key("FanDuel Sports Network SoCal") == "fd-socal");
  // Two-word regions aren't West.
  CHECK(regional::key("FanDuel Sports Network South West") == "fd-southwest");
  CHECK(regional::key("FOX SPORTS MID WEST") == "fd-midwest");
  CHECK(regional::key("FOX SPORTS SOUTH EAST") == "fd-southeast");
  CHECK(regional::key("Prime Sports") == std::nullopt);
  CHECK(regional::key("Bally Sports") == std::nullopt);
}

TEST_CASE("stations by brand and by a call sign anywhere in the name") {
  CHECK(networkKey("Utah 16") == "call:KUPX");
  CHECK(networkKey("The Spot - MTN") == "call:WXPX");
  CHECK(networkKey("Vegas 34") == "call:KMCC");
  CHECK(networkKey("CW33") == "call:KDAF");
  CHECK(networkKey("PeachtreeTV") == "call:WPCH");
  CHECK(networkKey("Atlanta News First") == "call:WANF");
  CHECK(networkKey("Fox 5 New York") == "call:WNYW");
  CHECK(regional::stationBrand("US: CW PIX 11 HD") == std::nullopt);  // the name as the schedule writes it only
  CHECK(regional::stationBrand("CW PIX 11 HD") == "WPIX");
  CHECK(regional::stationBrand("Arizona's Family CBS5") == "KPHO");
  CHECK(networkKey("KTVD-TV (My20)") == "call:KTVD");
  CHECK(networkKey("KUSA-TV (9NEWS)") == "call:KUSA");
  CHECK(networkKey("ALT2/KTVD") == "call:KTVD");
  CHECK(networkKey("KCTV5") == "call:KCTV");
  CHECK(networkKey("Fox 5 WTTG") == "call:WTTG");
  CHECK(networkKey("KFMB 8.1 (CBS)") == "call:KFMB");
  CHECK(networkKey("WTHR Channel 13") == "call:WTHR");
  CHECK(networkKey("WXIX FOX19") == "call:WXIX");
  CHECK(networkKey("KING 5") == "call:KING");
  // No sign, two signs, words.
  CHECK(networkKey("KCAL and KTTV") == std::nullopt);
  CHECK(networkKey("WEST WORLD") == std::nullopt);
  CHECK(networkKey("Harbor Sports Live") == std::nullopt);
  CHECK(networkKey("KPIX+") == std::nullopt);  // a station's streaming service
  // A brand that's a station only with its team.
  CHECK(networkKey("Fox4") == std::nullopt);
  CHECK(networkKey("Scripps Sports") == std::nullopt);
  CHECK(networkKey("DAZN SAS") == std::nullopt);  // not known which channel
}

TEST_CASE("streaming services and team apps aren't unknown names") {
  for (const char* name : {"ESPN+", "Peacock", "Prime Video", "Prime Video (Local)", "Apple TV", "HBO Max",
                           "NBA League Pass", "WNBA League Pass", "MLB.TV", "Victory+", "Cardinals.TV",
                           "Pelicans.com", "Thunder App", "Jazz+", "BlazerVision", "B1G+", "ACCNX", "SECN+",
                           "MW+", "Midco Sports Plus", "FloSports", "NEC Front Row", "ERADM"}) {
    CAPTURE(name);
    CHECK(streamingName(name));
    CHECK(networkKey(name) == std::nullopt);
  }
  CHECK_FALSE(streamingName("ESPN"));
  CHECK_FALSE(streamingName("Utah 16"));
  CHECK_FALSE(streamingName("DAZN SAS"));
  CHECK_FALSE(streamingName("KPIX+"));
  CHECK(regional::folded("UniMás HD") == "unimas");
  CHECK(regional::folded("NBCSCA+") == "nbcscaplus");
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

catalog::ChannelTraits slot(const std::string& name, std::optional<std::string> market = std::string("US")) {
  catalog::ChannelTraits t;
  t.kind = catalog::ChannelKind::event;
  t.familyKey = "grp:us| ncaaf ppv";
  t.eventText = name;
  t.displayName = name;
  t.market = std::move(market);
  t.category = LineupCategory::sports;
  return t;
}

bool has(const network_map::ChannelIndex& index, const std::string& key, const std::string& id) {
  auto it = index.byKey.find(key);
  if (it == index.byKey.end()) return false;
  return std::any_of(it->second.begin(), it->second.end(), [&](const auto& item) { return item.id == id; });
}

}  // namespace

TEST_CASE("Spanish stations, table names, alternates and event slots in the channel index") {
  std::unordered_map<std::string, EntryInfo> infos;
  auto add = [&](EntryInfo e) { infos[e.id] = std::move(e); };
  add(info("e|wnju", "TELEMUNDO NBC (WNJU)", LineupCategory::spanish, std::string("WNJU"), std::nullopt,
           std::string("US-ES")));
  add(info("e|tmo", "TELEMUNDO", LineupCategory::spanish, std::nullopt, std::nullopt, std::string("US-ES")));
  add(info("e|universo", "NBC Universo", LineupCategory::spanish, std::nullopt, std::nullopt, std::string("US-ES")));
  add(info("e|deportes", "ESPN Deportes", LineupCategory::spanish, std::nullopt, std::nullopt, std::string("US-ES")));
  add(info("e|nbcsn", "NBCSN", LineupCategory::sports));
  add(info("e|pix", "CW PIX 11 HD", LineupCategory::local));
  add(info("e|btn", "Big Ten Network", LineupCategory::sports, std::nullopt, std::string("Big Ten Network")));
  add(info("e|btn2", "Big Ten Network Overflow 2", LineupCategory::sports));
  add(info("e|btnalt", "Big Ten Network Alternate", LineupCategory::sports));
  add(info("e|msgsn", "MSG Sportsnet", LineupCategory::sports));
  add(info("e|show", "Harbor Overflow Stories", LineupCategory::sports));
  add(info("e|ion", "ION", LineupCategory::local));
  add(info("e|ionplus", "ION PLUS", LineupCategory::local));
  add(info("e|kpxn", "ION (KPXN)", LineupCategory::local, std::string("KPXN")));
  std::vector<Channel> browse = {entry("e|nbcsn", "NBCSN"), entry("e|pix", "CW PIX 11 HD"),
                                 entry("e|btn", "Big Ten Network"), entry("e|btn2", "Big Ten Network Overflow 2"),
                                 entry("e|btnalt", "Big Ten Network Alternate"), entry("e|msgsn", "MSG Sportsnet"),
                                 entry("e|show", "Harbor Overflow Stories"), entry("e|ion", "ION"),
                                 entry("e|ionplus", "ION PLUS"), entry("e|kpxn", "ION (KPXN)")};
  // The Spanish channels aren't in the browse list (US in Spanish not picked).
  std::vector<Channel> all = browse;
  for (const char* id : {"e|wnju", "e|tmo", "e|universo", "e|deportes"}) all.push_back(entry(id, infos[id].name));
  std::unordered_map<std::string, catalog::ChannelTraits> slots = {
      {"xt:1|01", slot("NCAAF 01: ESPN")},
      {"xt:1|04", slot("NCAAF 04: ABC")},
      {"xt:1|09", slot("NCAAF 09: BIG TEN NETWORK")},
      {"xt:1|10", slot("NCAAF 10: BIG TEN NETWORK ALT")},
      {"xt:1|17", slot("NCAAF 17:")},
      {"xt:1|34", slot("NCAAF 34: UAlbany at Iowa 6pm")},
      {"xt:1|48", slot("ESPN+ 48: Harbor at Valley")},
      {"xt:1|uk", slot("SOCCER 03: ESPN", std::string("UK"))},
  };
  // A numbered family's slots: the text after the number.
  catalog::ChannelTraits cbs = slot("CBS");
  cbs.familyKey = "ncaaf #";
  cbs.familyNumber = 5;
  slots["xt:1|05"] = cbs;
  catalog::ChannelTraits game = slot("Harbor at Valley");
  game.familyKey = "espn+ #";
  game.familyNumber = 48;
  slots["xt:1|e48"] = game;
  StationMarkets markets;
  auto index = network_map::channelIndex({}, {}, browse, all, infos, {}, {}, markets, &slots);
  CHECK(has(index, "net:TELEMUNDO", "e|wnju"));
  CHECK(has(index, "call:WNJU", "e|wnju"));
  CHECK(has(index, "net:TELEMUNDO", "e|tmo"));
  CHECK_FALSE(has(index, "net:NBC", "e|universo"));  // Telemundo's sister, not NBC's stations
  CHECK_FALSE(has(index, "net:NBC", "e|wnju"));
  CHECK(has(index, "core:NBC Universo", "e|universo"));
  CHECK(has(index, "core:ESPN Deportes", "e|deportes"));
  CHECK(index.byKey.at("core:ESPN Deportes").front().lang == "es");
  CHECK(has(index, "core:NBCSN", "e|nbcsn"));
  CHECK(has(index, "call:WPIX", "e|pix"));
  CHECK(has(index, "rsn:msgsn", "e|msgsn"));
  CHECK(has(index, "core:ION", "e|ion"));
  CHECK(has(index, "core:ION", "e|kpxn"));
  CHECK_FALSE(has(index, "core:ION", "e|ionplus"));  // another channel
  CHECK(has(index, "core:Big Ten Network", "e|btn"));
  CHECK(has(index, "alt:core:Big Ten Network", "e|btn2"));
  CHECK(has(index, "alt:core:Big Ten Network", "e|btnalt"));
  CHECK_FALSE(has(index, "core:Big Ten Network", "e|btn2"));
  CHECK(index.alternates == 2);
  CHECK(has(index, "backup:core:ESPN|NCAAF", "xt:1|01"));  // for college football games
  CHECK(has(index, "backup:net:ABC|NCAAF", "xt:1|04"));
  CHECK(has(index, "backup:core:Big Ten Network|NCAAF", "xt:1|09"));
  CHECK(has(index, "alt:core:Big Ten Network|NCAAF", "xt:1|10"));
  CHECK(has(index, "backup:net:CBS|NCAAF", "xt:1|05"));
  CHECK(index.slotCopies == 5);  // not an empty slot, a game, a title or another country's
}

TEST_CASE("each game's channels: Big Ten games at once, slots as backups, team stations, unknown names") {
  std::unordered_map<std::string, EntryInfo> infos;
  auto add = [&](EntryInfo e) { infos[e.id] = std::move(e); };
  add(info("e|btn", "Big Ten Network", LineupCategory::sports, std::nullopt, std::string("Big Ten Network")));
  add(info("e|btn2", "Big Ten Network Overflow 2", LineupCategory::sports));
  add(info("e|espn", "ESPN", LineupCategory::sports, std::nullopt, std::string("ESPN")));
  add(info("e|kmcc", "LAS VEGAS, NV KMCC IND 34", LineupCategory::local, std::string("KMCC")));
  add(info("e|west", "FOX SPORTS WEST HD", LineupCategory::sports));
  add(info("e|socal", "FOX SPORTS PRIME TICKET HD", LineupCategory::sports));
  std::vector<Channel> browse;
  for (const auto& [id, e] : infos) browse.push_back(entry(id, e.name));
  std::sort(browse.begin(), browse.end(), [](const Channel& a, const Channel& b) { return a.id < b.id; });
  std::unordered_map<std::string, catalog::ChannelTraits> slots = {{"xt:1|01", slot("NCAAF 01: ESPN")}};
  StationMarkets markets;
  auto index = network_map::channelIndex({}, {}, browse, browse, infos, {}, {}, markets, &slots);

  Seconds noon = utc(2026, 11, 7, 17, 0);
  SportsGame a = game("NCAAF", "College Football", "1", team("1", "Harbor"), team("2", "Valley"), noon, {"BTN"});
  SportsGame b = game("NCAAF", "College Football", "2", team("3", "Coast"), team("4", "Ridge"), noon, {"BTN"});
  SportsGame later = game("NCAAF", "College Football", "3", team("5", "Lake"), team("6", "Peak"), noon + 4 * 3600,
                          {"BTN"});
  SportsGame espn = game("NCAAF", "College Football", "4", team("7", "Bay"), team("8", "Glen"), noon, {"ESPN"});
  SportsGame vgk = game("NHL", "NHL", "5", team("9", "Vegas Golden Knights", std::string("VGK")),
                        team("10", "Florida Panthers", std::string("FLA")), noon, {"Scripps Sports", "ESPN+"});
  SportsGame fla = game("NHL", "NHL", "6", team("10", "Florida Panthers", std::string("FLA")),
                        team("11", "Boston Bruins", std::string("BOS")), noon, {"Scripps Sports", "Gizmo TV"});
  SportsGame kings = game("NHL", "NHL", "7", team("12", "Los Angeles Kings", std::string("LA")),
                          team("13", "Utah Mammoth", std::string("UTA")), noon, {"FanDuel SN West"});
  SportsGame ducks = game("NHL", "NHL", "8", team("14", "Anaheim Ducks", std::string("ANA")),
                          team("13", "Utah Mammoth", std::string("UTA")), noon, {"FanDuel SN SoCal"});
  SportsGame hoops = game("NCAAM", "College Basketball", "9", team("15", "Delta"), team("16", "Mesa"),
                          noon + 9 * 3600, {"ESPN"});
  auto mapped = network_map::networkSources({a, b, later, espn, vgk, fla, kings, ducks, hoops}, index);
  // A football group's slot only backs football games.
  REQUIRE(mapped.sources.at("NCAAM|9").size() == 1);
  CHECK(mapped.sources.at("NCAAM|9")[0].channelID == "e|espn");

  // Two Big Ten games at once: the network and its alternate are likely, the guide says which.
  for (const char* id : {"NCAAF|1", "NCAAF|2"}) {
    REQUIRE(mapped.sources.count(id));
    const auto& list = mapped.sources.at(id);
    REQUIRE(list.size() == 2);
    CHECK(list[0].channelID == "e|btn");
    CHECK(list[1].channelID == "e|btn2");
    for (const auto& s : list) {
      CHECK(s.kind == EventSource::Kind::team);
      CHECK(s.detail == "Several games on this network at once");
    }
  }
  CHECK(mapped.stats.split == 2);
  // One game on it: the network itself.
  REQUIRE(mapped.sources.at("NCAAF|3").size() == 1);
  CHECK(mapped.sources.at("NCAAF|3")[0].channelID == "e|btn");
  CHECK(mapped.sources.at("NCAAF|3")[0].kind == EventSource::Kind::guide);
  // ESPN, then its event slot as a backup.
  const auto& onEspn = mapped.sources.at("NCAAF|4");
  REQUIRE(onEspn.size() == 2);
  CHECK(onEspn[0].channelID == "e|espn");
  CHECK(onEspn[1].channelID == "xt:1|01");
  // Scripps Sports is the Golden Knights' KMCC; not known for the Panthers.
  REQUIRE(mapped.sources.count("NHL|5"));
  CHECK(mapped.sources.at("NHL|5")[0].channelID == "e|kmcc");
  CHECK_FALSE(mapped.sources.count("NHL|6"));
  CHECK(mapped.stats.unknown.at("Scripps Sports") == 1);
  CHECK(mapped.stats.unknown.at("Gizmo TV") == 1);
  CHECK(mapped.stats.unknownLeagues.at("Gizmo TV") == std::set<std::string>{"NHL"});
  CHECK(mapped.stats.streaming.at("ESPN+") == 1);
  CHECK_FALSE(mapped.stats.unknown.count("ESPN+"));
  // The Kings on West, the Ducks on SoCal (Prime Ticket).
  REQUIRE(mapped.sources.count("NHL|7"));
  REQUIRE(mapped.sources.at("NHL|7").size() == 1);
  CHECK(mapped.sources.at("NHL|7")[0].channelID == "e|west");
  REQUIRE(mapped.sources.at("NHL|8").size() == 1);
  CHECK(mapped.sources.at("NHL|8")[0].channelID == "e|socal");

  // The report's rows, most games first.
  report::UnknownBroadcasters rows = report::unknownBroadcasters(mapped.stats.unknown, mapped.stats.unknownLeagues);
  REQUIRE(rows.size() == 2);
  CHECK(rows[0].games == 1);
  CHECK(rows[0].leagues == std::vector<std::string>{"NHL"});
}

TEST_CASE("the Live Events report lists the unknown broadcaster names") {
  report::UnknownBroadcasters rows = {{"Gizmo TV", 12, {"MLB", "NHL"}}, {"Harbor Net", 1, {"NBA"}}};
  report::LiveEventsReportInput in;
  in.unknownBroadcasters = &rows;
  catalog::CatalogResult empty;
  in.catalog = &empty;
  std::string csv = report::liveEventsCsv(in);
  CHECK(csv.find("\"Broadcaster names not recognised\",\"\",\"Gizmo TV\",\"\",\"MLB | NHL\",\"\",\"not recognised\","
                 "\"12 games\"") != std::string::npos);
  CHECK(csv.find("\"Harbor Net\",\"\",\"NBA\",\"\",\"not recognised\",\"1 game\"") != std::string::npos);
}
