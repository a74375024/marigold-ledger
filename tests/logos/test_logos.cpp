// Logo specs, the iptv-org index and the CSV reader.
#include <unordered_set>

#include "core/catalog/reference.hpp"
#include "core/logos/logos.hpp"
#include "core/logos/table.hpp"
#include "core/util/strings.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::logos;
using catalog::LineupCategory;

namespace {

std::string base(const std::string& file) { return std::string(kLogoBase) + file; }

// Any station in the table with exactly one file, and its file.
std::pair<std::string, std::string> singleFileStation() {
  for (const auto& [sign, files] : stationLogos()) {
    if (files.size() == 1) return {sign, files.begin()->second};
  }
  return {};
}

}  // namespace

TEST_CASE("logo table data") {
  CHECK(std::string(kLogoBase).rfind("https://", 0) == 0);
  CHECK(coreLogos().count("CNN"));
  CHECK(networkLogos().size() == 11);
  CHECK(networkLogos().count("MYTV"));
  CHECK(stationLogos().size() > 400);
}

TEST_CASE("spec: a local station") {
  auto [sign, file] = singleFileStation();
  REQUIRE_FALSE(sign.empty());
  const std::string provider = "https://example.invalid/provider.png";
  LogoSpec s = spec("FOX 2", std::nullopt, false, true, str::upper(sign), 2, provider, std::nullopt,
                    LineupCategory::local, nullptr);
  CHECK(s.style == LogoSpec::Style::local);
  CHECK(s.callsign == str::upper(sign));
  CHECK(s.line == "FOX 2");
  REQUIRE(s.urls.size() == 3);
  CHECK(s.urls[0] == base(networkLogos().at("FOX")));
  CHECK(s.urls[1] == base(file));  // the only file, whatever its network
  CHECK(s.urls[2] == provider);
  CHECK(s.subtitle == std::string("Local"));
  CHECK_FALSE(s.isWest);

  LogoSpec mytv = spec("MyNetworkTV 20", std::nullopt, true, true, std::string("KZZZ"),
                       std::nullopt, "", std::nullopt, std::nullopt, nullptr);
  CHECK(mytv.line == "MyNetworkTV");
  CHECK(mytv.urls == std::vector<std::string>{base(networkLogos().at("MYTV"))});
  CHECK_FALSE(mytv.subtitle);
  LogoSpec unknown = spec("Station 9", std::nullopt, false, true, std::string("KZZZ"), 9, "",
                          std::nullopt, LineupCategory::local, nullptr);
  CHECK(unknown.urls.empty());
  CHECK(unknown.line == "9");
}

TEST_CASE("spec: a curated core network, then everything else") {
  LogoSpec core =
      spec("CNN", std::string("CNN"), true, false, std::nullopt, std::nullopt,
           "https://example.invalid/p.png", std::nullopt, LineupCategory::news, nullptr);
  CHECK(core.style == LogoSpec::Style::logoOnly);
  CHECK(core.urls ==
        std::vector<std::string>{base(coreLogos().at("CNN")), "https://example.invalid/p.png"});
  CHECK(core.isWest);
  CHECK(core.subtitle == std::string("News"));

  // No curated logo: named, with the network's logo for a local without a sign.
  LogoSpec local = spec("FOX 5 Testville", std::nullopt, false, false, std::nullopt, 5, "",
                        std::nullopt, LineupCategory::local, nullptr);
  CHECK(local.style == LogoSpec::Style::named);
  CHECK(local.urls == std::vector<std::string>{base(networkLogos().at("FOX"))});
  LogoSpec plain = spec("Dream Channel", std::string("Not In Table"), false, false, std::nullopt,
                        std::nullopt, "", std::nullopt, LineupCategory::entertainment, nullptr);
  CHECK(plain.style == LogoSpec::Style::named);
  CHECK(plain.urls.empty());
}

TEST_CASE("iptv-org index from the two CSV files") {
  std::string channels =
      "id,name,alt_names,network,country,closed\r\n"
      "DreamChannel.us,Dream Channel,Dream TV;DC,,US,\r\n"
      "OldChannel.us,Old Channel,,,US,2020-01-01\r\n"
      "Foreign.uk,Foreign,,,UK,\r\n"
      "NoLogo.us,No Logo,,,US,\r\n"
      "\"Quoted.us\",\"Quoted, Inc\",,,US,\r\n";
  std::string logos =
      "channel,feed,tags,width,height,format,url\n"
      "DreamChannel.us,East,,1,1,PNG,https://example.invalid/feed.png\n"
      "DreamChannel.us,,,1,1,PNG,https://example.invalid/main.png\n"
      "DreamChannel.us,,,1,1,PNG,https://example.invalid/second.png\n"
      "OldChannel.us,,,1,1,PNG,https://example.invalid/old.png\n"
      "Foreign.uk,,,1,1,PNG,https://example.invalid/foreign.png\n"
      "NoLogo.us,,,1,1,SVG,https://example.invalid/vector.svg\n"
      "NoLogo.us,,,1,1,,http://example.invalid/insecure.png\n"
      "Quoted.us,,,400,150,,https://example.invalid/QUOTED.JPG\n";
  auto saved = IptvOrgIndex::build(channels, logos);
  REQUIRE(saved);
  CHECK(saved->byID.size() == 2);
  CHECK(saved->byID.at("dreamchannel.us") == "https://example.invalid/main.png");
  CHECK(saved->byID.at("quoted.us") == "https://example.invalid/QUOTED.JPG");
  CHECK(saved->byName.at("dream channel") == "https://example.invalid/main.png");
  CHECK(saved->byName.at("dream tv") == "https://example.invalid/main.png");
  CHECK(saved->byName.at("dc") == "https://example.invalid/main.png");
  CHECK(saved->byName.at("quoted inc") == "https://example.invalid/QUOTED.JPG");

  IptvOrgIndex index(*saved);
  CHECK(index.url(std::string("DREAMCHANNEL.US"), "anything") ==
        std::string("https://example.invalid/main.png"));
  CHECK(index.url(std::nullopt, "Dream TV HD") == std::string("https://example.invalid/main.png"));
  CHECK_FALSE(index.url(std::nullopt, "Unknown"));
  CHECK_FALSE(index.url(std::nullopt, "HD"));
  // A channel without a guide id, by its directory entry's iptv-org id.
  CHECK(index.url(std::nullopt, "Some Other Name", "Quoted.us") == std::string("https://example.invalid/QUOTED.JPG"));
  CHECK(index.url(std::string("unknown.us"), "Some Other Name", "DreamChannel.us") ==
        std::string("https://example.invalid/main.png"));
  CHECK_FALSE(index.url(std::nullopt, "Some Other Name", "NoLogo.us"));

  LogoSpec named = spec("Dream TV", std::nullopt, false, false, std::nullopt, std::nullopt,
                        "https://example.invalid/p.png", std::nullopt, std::nullopt, &index);
  // The provider's logo last, after the public database's.
  CHECK(named.urls == std::vector<std::string>{"https://example.invalid/main.png",
                                               "https://example.invalid/p.png"});
  CHECK(named.sources == std::vector<std::string>{"public database", "provider"});
  // It stands alone once a good one loads, like a curated one (Build 116).
  CHECK(named.style == LogoSpec::Style::logoOnly);

  CHECK_FALSE(IptvOrgIndex::build("name\nx\n", logos));                     // columns missing
  CHECK_FALSE(IptvOrgIndex::build("id,name,country\nA.uk,A,UK\n", logos));  // nothing in the US
  CHECK_FALSE(IptvOrgIndex::build("", ""));
}

TEST_CASE("CSV rows: quotes, doubled quotes, CRLF, line breaks in fields") {
  auto rows = csvRows("a,\"b,c\",\"say \"\"hi\"\"\"\r\n\"multi\nline\",,x\nlast");
  REQUIRE(rows.size() == 3);
  CHECK(rows[0] == std::vector<std::string>{"a", "b,c", "say \"hi\""});
  CHECK(rows[1] == std::vector<std::string>{"multi\nline", "", "x"});
  CHECK(rows[2] == std::vector<std::string>{"last"});
  CHECK(csvRows("").empty());
  CHECK(csvRows("x,\n") == std::vector<std::vector<std::string>>{{"x", ""}});
  CHECK(csvRows("\"open") == std::vector<std::vector<std::string>>{{"open"}});
  CHECK(csvRows("ab\"c,d\"e\n") == std::vector<std::vector<std::string>>{{"abc,de"}});
}

TEST_CASE("spec: national channels the tables miss, by name in the same library") {
  CHECK(logoSetFile("MTV2") == std::string("mtv-2-us.png"));
  CHECK(logoSetFile("FOX WEATHER") == std::string("fox-weather-us.png"));
  CHECK(logoSetFile("Cinemax Hits HD") == std::string("cinemax-hits-us.png"));
  CHECK(logoSetFile("LAW & CRIME") == std::string("law-and-crime-network-us.png"));
  CHECK(logoSetFile("MGM+ HITS") == std::string("mgm-plus-hits-us.png"));
  CHECK(logoSetFile("HEROES & ICONS") == std::string("heroes-and-icons-us.png"));
  CHECK(logoSetFile("The Cowboy Channel") == std::string("cowboy-channel-us.png"));
  CHECK(logoSetFile("Hallmark Family") == std::string("hallmark-family-us.png"));
  CHECK_FALSE(logoSetFile("Confess by Nosey"));
  CHECK_FALSE(logoSetFile("HD"));
  CHECK_FALSE(logoSetFile(""));

  const std::string provider = "https://example.invalid/p.png";
  LogoSpec mtv2 = spec("MTV2", std::nullopt, false, false, std::nullopt, std::nullopt, provider, std::nullopt,
                       LineupCategory::music, nullptr);
  CHECK(mtv2.style == LogoSpec::Style::logoOnly);
  CHECK(mtv2.urls == std::vector<std::string>{base("mtv-2-us.png"), provider});
  // A core network with no curated logo also finds it by name.
  LogoSpec hallmark = spec("Hallmark Family", std::string("Hallmark Family"), false, false, std::nullopt,
                           std::nullopt, "", std::nullopt, LineupCategory::entertainment, nullptr);
  CHECK(hallmark.urls == std::vector<std::string>{base("hallmark-family-us.png")});
  // Not for other countries' channels or locals.
  LogoSpec abroad = spec("MTV2", std::nullopt, false, false, std::nullopt, std::nullopt, "", std::nullopt,
                         LineupCategory::international, nullptr);
  CHECK(abroad.urls.empty());
  // Curated tables still come first.
  CHECK(spec("CNN", std::string("CNN"), false, false, std::nullopt, std::nullopt, "", std::nullopt,
             LineupCategory::news, nullptr)
            .urls.front() == base(coreLogos().at("CNN")));
}

TEST_CASE("logos: another country's channel with an English feed, US names first, other spellings") {
  std::string channels =
      "id,name,alt_names,network,country,closed\n"
      "HitList.ca,Stingray Hit List,,,CA,\n"
      "Kanal.de,Kanal Eins,,,DE,\n"
      "Shared.us,Shared Name,,,US,\n"
      "Shared.uk,Shared Name,,,UK,\n"
      "EarthxTV.us,EarthxTV,,,US,\n";
  std::string logos =
      "channel,feed,in_use,tags,width,height,format,url\n"
      "HitList.ca,,TRUE,,1,1,PNG,https://example.invalid/hits.png\n"
      "Kanal.de,,TRUE,,1,1,PNG,https://example.invalid/kanal.png\n"
      "Shared.us,,TRUE,,1,1,PNG,https://example.invalid/us.png\n"
      "Shared.uk,,TRUE,,1,1,PNG,https://example.invalid/uk.png\n"
      "EarthxTV.us,,TRUE,,1,1,PNG,https://example.invalid/earthx.png\n";
  std::string feeds =
      "channel,id,name,alt_names,is_main,broadcast_area,timezones,languages,format\n"
      "HitList.ca,SD,SD,,TRUE,c/CA,,eng,576i\n"
      "Kanal.de,SD,SD,,TRUE,c/DE,,deu,576i\n"
      "Shared.uk,SD,SD,,TRUE,c/UK,,eng,576i\n";
  auto saved = IptvOrgIndex::build(channels, logos, feeds);
  REQUIRE(saved);
  IptvOrgIndex index(*saved);
  CHECK(index.url(std::nullopt, "STINGRAY HIT LIST") == std::string("https://example.invalid/hits.png"));
  CHECK_FALSE(index.url(std::nullopt, "Kanal Eins"));  // no English feed
  CHECK(index.url(std::nullopt, "Shared Name") == std::string("https://example.invalid/us.png"));
  CHECK(index.url(std::nullopt, "EARTHX") == std::string("https://example.invalid/earthx.png"));
  // Without feeds, the US channels only (as before).
  auto usOnly = IptvOrgIndex::build(channels, logos);
  REQUIRE(usOnly);
  CHECK_FALSE(IptvOrgIndex(*usOnly).url(std::nullopt, "Stingray Hit List"));
}

TEST_CASE("logos: hand-picked logos for free streaming channels no database has") {
  // As the lineup spells them.
  for (const char* name : {"MAYDAY: AIR DISASTER", "BIG A ANIME", "TORQUE BY HISTORY", "Torque", "BOWLING TV",
                           "Reelz Famous and Infamous", "REELZ FAMOUS & INFAMOUS", "STINGRAY SPA", "ALWAYS FUNNY",
                           "Confess by Nosey", "ZENLIFE BY STINGRAY"}) {
    INFO(name);
    CHECK(handPicked(name));
  }
  CHECK(str::startsWith(*handPicked("Always Funny"), "https://images.pluto.tv/"));
  // The library has NBC Sports California (and its Plus feeds) by name now.
  CHECK(logoSetFile("NBC Sports California Plus") == std::string("nbcsn-california-us.png"));
  CHECK(logoSetFile("NBC Sports California Plus 2") == std::string("nbcsn-california-us.png"));
  CHECK_FALSE(handPicked("Mayday"));
  CHECK_FALSE(handPicked("Reelz"));
  CHECK_FALSE(handPicked("NBC Sports California"));
  CHECK_FALSE(handPicked(""));

  const std::string provider = "https://example.invalid/p.png";
  LogoSpec bowling = spec("BOWLING TV", std::nullopt, false, false, std::nullopt, std::nullopt, provider, std::nullopt,
                          LineupCategory::sports, nullptr);
  CHECK(bowling.style == LogoSpec::Style::logoOnly);
  REQUIRE(bowling.urls.size() == 2);
  CHECK(bowling.urls[0] == *handPicked("Bowling TV"));
  CHECK(bowling.urls[1] == provider);
  CHECK(bowling.sources == std::vector<std::string>{"hand-picked", "provider"});
}

TEST_CASE("logos: any channel with a logo to try may show it alone, a local keeps its name") {
  const std::string provider = "https://example.invalid/p.png";
  // The provider's logo only: alone once it loads and passes fillsCard().
  LogoSpec onlyProvider = spec("Dream Channel", std::nullopt, false, false, std::nullopt, std::nullopt, provider,
                               std::nullopt, LineupCategory::entertainment, nullptr);
  CHECK(onlyProvider.style == LogoSpec::Style::logoOnly);
  CHECK(onlyProvider.sources == std::vector<std::string>{"provider"});
  // Another country's channel too.
  CHECK(spec("Kanal Eins", std::nullopt, false, false, std::nullopt, std::nullopt, provider, std::nullopt,
             LineupCategory::international, nullptr)
            .style == LogoSpec::Style::logoOnly);
  // Nothing to try: the name.
  CHECK(spec("Dream Channel", std::nullopt, false, false, std::nullopt, std::nullopt, "", std::nullopt,
             LineupCategory::entertainment, nullptr)
            .style == LogoSpec::Style::named);
  // A local with no call sign: its network's logo beside its name, which
  // tells it from the network's other stations.
  LogoSpec local = spec("FOX 5 Testville", std::nullopt, false, false, std::nullopt, 5, provider, std::nullopt,
                        LineupCategory::local, nullptr);
  CHECK(local.style == LogoSpec::Style::named);
  CHECK(local.urls == std::vector<std::string>{base(networkLogos().at("FOX")), provider});
}

TEST_CASE("logos: names the library spells otherwise, coast feeds, language feeds") {
  CHECK(logoSetFile("TMC Xtra") == std::string("the-movie-channel-xtra-us.png"));
  CHECK(logoSetFile("The Movie Channel Extra") == std::string("the-movie-channel-xtra-us.png"));
  CHECK(logoSetFile("STARZKIDS WEST") == std::string("starz-kids-and-family-us.png"));
  CHECK(logoSetFile("All Reality WE tv") == std::string("we-tv-us.png"));
  CHECK(logoSetFile("Yes Network Latin") == std::string("yes-network-us.png"));
  CHECK(logoSetFile("CA Fox Sports Racing") == logoSetFile("Fox Sports Racing"));
  // A name the library has as written wins over the plainer one.
  CHECK(logoSetFile("Bally Sports West") == std::string("bally-sports-west-us.png"));
  CHECK(logoSetFile("HBO Latino") == std::string("hbo-latino-us.png"));
  CHECK_FALSE(logoSetFile("West"));
  CHECK_FALSE(logoSetFile("Latin"));

  // A station with no logo of its own: the network the provider names
  // beside its call sign, from the same library.
  LogoSpec metv = spec("MeTV Plus (KZZZ) HD", std::nullopt, false, true, std::string("KZZZ"), 43, "", std::nullopt,
                       LineupCategory::local, nullptr);
  CHECK(metv.style == LogoSpec::Style::local);
  CHECK(metv.urls == std::vector<std::string>{base("me-tv-plus-us.png")});
  CHECK(metv.callsign == "KZZZ");
  CHECK(spec("KZZZ", std::nullopt, false, true, std::string("KZZZ"), 9, "", std::nullopt, LineupCategory::local,
             nullptr)
            .urls.empty());
}

TEST_CASE("logos: a station's network from its name, else from the reference data by its call sign") {
  auto data = catalog::reference::fromJson(R"({"made": "2026-11-01", "channels": [], "stations": [
    {"c": "KFSF-DT", "n": "UNIMAS", "m": "San Francisco-Oakland-San Jose", "v": "66", "l": "es"},
    {"c": "KPYX", "n": "INDEPENDENT", "m": "San Francisco-Oakland-San Jose", "v": "44"},
    {"c": "WZME", "n": "Story Television", "m": "New York", "v": "43"}]})");
  REQUIRE(data);
  catalog::reference::Index index(*data);
  CHECK(stationNetwork("KFSF", "KFSF", index) == std::string("UNIMAS"));
  CHECK(stationNetwork("KPYX", "KPYX", index) == std::string("INDEPENDENT"));
  // The name first: what the provider carries on it.
  CHECK(stationNetwork("FOX 2 (KFSF)", "KFSF", index) == std::string("FOX"));
  // Only a network word the name could have said.
  CHECK_FALSE(stationNetwork("MeTV Plus (WZME) HD", "WZME", index));
  CHECK_FALSE(stationNetwork("KQQQ", "KQQQ", index));
}

TEST_CASE("logos: the public database's names spelled otherwise") {
  std::string channels =
      "id,name,alt_names,network,country,closed\n"
      "JewelryTV.us,Jewelry TV,,,US,\n"
      "TheGrioTV.us,TheGrio TV,,,US,\n"
      "VictoryChannel.us,Victory Channel,,,US,\n"
      "FoxSportsRacing.us,Fox Sports Racing,,,US,\n";
  std::string logos =
      "channel,feed,in_use,tags,width,height,format,url\n"
      "JewelryTV.us,,TRUE,,1,1,PNG,https://example.invalid/jtv.png\n"
      "TheGrioTV.us,,TRUE,,1,1,PNG,https://example.invalid/grio.png\n"
      "VictoryChannel.us,,TRUE,,1,1,PNG,https://example.invalid/victory.png\n"
      "FoxSportsRacing.us,,TRUE,,1,1,PNG,https://example.invalid/fsr.png\n";
  auto saved = IptvOrgIndex::build(channels, logos);
  REQUIRE(saved);
  IptvOrgIndex index(*saved);
  CHECK(index.url(std::nullopt, "Jewelry Television") == std::string("https://example.invalid/jtv.png"));
  CHECK(index.url(std::nullopt, "TheGrio") == std::string("https://example.invalid/grio.png"));
  CHECK(index.url(std::nullopt, "Victory") == std::string("https://example.invalid/victory.png"));
  CHECK(index.url(std::nullopt, "CA Fox Sports Racing") == std::string("https://example.invalid/fsr.png"));
  CHECK_FALSE(index.url(std::nullopt, "Dream"));
  CHECK_FALSE(index.url(std::nullopt, "CA"));
}

TEST_CASE("fillsCard: good logos stand alone whatever their source, poor ones keep the name") {
  auto shape = [](int w, int h, bool plain = false) {
    px::Shape s;
    s.w = w;
    s.h = h;
    s.plain = plain;
    return s;
  };
  // Typical database, library and provider logos (visible box, source pixels).
  CHECK(fillsCard(shape(512, 131)));  // a wide wordmark
  CHECK(fillsCard(shape(141, 42)));   // small but sharp enough
  CHECK(fillsCard(shape(297, 48)));   // a strip
  CHECK(fillsCard(shape(512, 512)));  // square
  CHECK(fillsCard(shape(215, 282)));  // a bit taller than wide
  CHECK(fillsCard(shape(64, 64)));    // a square provider icon
  CHECK(fillsCard(shape(77, 20)));    // twice its size to fill the width: still fine
  // Poor: nothing visible, a blank tile, a speck, a sliver, a thread, blown up.
  CHECK_FALSE(fillsCard(shape(0, 0)));
  CHECK_FALSE(fillsCard(shape(400, 225, true)));
  CHECK_FALSE(fillsCard(shape(32, 32)));
  CHECK_FALSE(fillsCard(shape(47, 20)));
  CHECK_FALSE(fillsCard(shape(60, 130)));
  CHECK_FALSE(fillsCard(shape(400, 20)));
  CHECK_FALSE(fillsCard(shape(70, 24)));  // 2.2 times its size to fill the card
  CHECK_FALSE(fillsCard(shape(50, 28)));  // 2.1 times
}

TEST_CASE("logos: free streaming channels' files in the library's misc/vod, by their exact name") {
  CHECK(std::string(kLogoBase).rfind(kLibraryRoot, 0) == 0);
  CHECK(logoSetFile("AFV TV") == std::string("misc/vod/afv-tv-vod.png"));
  CHECK(logoSetFile("The Bob Ross Channel") == std::string("misc/vod/the-bob-ross-channel-vod.png"));
  CHECK(logoSetFile("Vevo '70s") == std::string("misc/vod/vevo-70s-vod.png"));
  CHECK(logoSetFile("YU-GI-OH!") == std::string("misc/vod/yu-gi-oh-vod.png"));
  CHECK(logoSetFile("THE ARCHIVE HD") == std::string("misc/vod/the-archive-vod-us.png"));
  // The national file of the same name first.
  CHECK(logoSetFile("MTV Classic") == std::string("mtv-classic-us.png"));
  // Only the whole name, and never a bare genre.
  CHECK_FALSE(logoSetFile("AFV"));
  CHECK_FALSE(logoSetFile("Kids"));
  CHECK_FALSE(logoSetFile("Crime"));
  for (std::string_view n : vodNames()) {
    INFO(std::string(n));
    CHECK((str::endsWith(n, "-vod") || str::endsWith(n, "-vod-us")));
  }
  CHECK(logoSetUrl("misc/vod/afv-tv-vod.png") == std::string(kLibraryRoot) + "misc/vod/afv-tv-vod.png");
  CHECK(logoSetUrl("mtv-2-us.png") == base("mtv-2-us.png"));

  const std::string provider = "https://example.invalid/p.png";
  LogoSpec afv = spec("AFV TV", std::nullopt, false, false, std::nullopt, std::nullopt, provider, std::nullopt,
                      LineupCategory::entertainment, nullptr);
  CHECK(afv.style == LogoSpec::Style::logoOnly);
  CHECK(afv.urls == std::vector<std::string>{std::string(kLibraryRoot) + "misc/vod/afv-tv-vod.png", provider});
  CHECK(afv.sources == std::vector<std::string>{"logo set", "provider"});
}

TEST_CASE("logos: channels the library has under another name, and former brands as a last resort") {
  CHECK(logoSetFile("Family Movie Classics") == std::string("fmc-family-movie-classics-us.png"));
  CHECK(logoSetFile("IndiePlex") == std::string("plex-indieplex-us.png"));
  CHECK(logoSetFile("RETROPLEX HD") == std::string("plex-retroplex-us.png"));
  CHECK(logoSetFile("IndiePlex West") == std::string("plex-indieplex-us.png"));
  CHECK(logoSetFile("LiveNOW from FOX") == std::string("live-now-fox-us.png"));
  CHECK(logoSetFile("beIN SPORTS en Español") == std::string("bein-sports-espanol-us.png"));
  CHECK(logoSetFile("beIN Sports Español") == std::string("bein-sports-espanol-us.png"));  // accents folded
  CHECK(logoSetFile("Jewelry Television") == std::string("jtv-us.png"));
  CHECK(logoSetFile("SHO x BET") == std::string("sho-bet-us.png"));
  CHECK(logoSetFile("Great Entertainment Television") == std::string("get-tv-us.png"));
  CHECK(logoSetFile("NBC Sports Bay Area") == std::string("nbcsn-bay-area-us.png"));
  CHECK(logoSetFile("SportsNet LA") == std::string("spectrum-sportsnet-la-us.png"));
  CHECK(logoSetFile("FOX Deportes") == std::string("fox-sports-deportes-us.png"));

  // A FanDuel network keeps its former Bally file for when nothing shows
  // the new brand.
  CHECK_FALSE(logoSetFile("FanDuel Sports Network Detroit"));
  CHECK(formerBrandFile("FanDuel Sports Network Detroit") == std::string("bally-sports-detroit-us.png"));
  CHECK(formerBrandFile("FANDUEL SPORTS WEST PLUS") == std::string("bally-sports-west-plus-us.png"));
  CHECK_FALSE(formerBrandFile("FanDuel Sports Network"));
  CHECK_FALSE(formerBrandFile("FanDuel TV"));
  CHECK_FALSE(formerBrandFile("FanDuel Sports Network Nowhere"));
  const std::string provider = "https://example.invalid/p.png";
  LogoSpec former = spec("FanDuel Sports Network Detroit", std::nullopt, false, false, std::nullopt, std::nullopt,
                         provider, std::string("FSDetroit.us"), LineupCategory::sports, nullptr);
  CHECK(former.urls == std::vector<std::string>{base("bally-sports-detroit-us.png"), provider});
  CHECK(former.sources == std::vector<std::string>{"former brand", "provider"});
  CHECK(former.style == LogoSpec::Style::logoOnly);
  IptvOrgIndex::Saved saved;
  saved.byID["fsdetroit.us"] = "https://example.invalid/fanduel.png";
  IptvOrgIndex index(saved);
  CHECK(spec("FanDuel Sports Network Detroit", std::nullopt, false, false, std::nullopt, std::nullopt, provider,
             std::string("FSDetroit.us"), LineupCategory::sports, &index)
            .urls == std::vector<std::string>{"https://example.invalid/fanduel.png", provider});
}

TEST_CASE("logos: a station shows as one in any section, by its network") {
  const std::string provider = "https://example.invalid/p.png";
  // A Telemundo station the lineup files under Spanish.
  LogoSpec spanish = spec("TELEMUNDO 52 [LOS ANGELES]", std::nullopt, false, true, std::string("KQQQ"), 52, provider,
                          std::nullopt, LineupCategory::spanish, nullptr);
  CHECK(spanish.style == LogoSpec::Style::local);
  CHECK(spanish.callsign == "KQQQ");
  CHECK(spanish.line == "Telemundo 52");
  REQUIRE(spanish.urls.size() == 2);
  CHECK(spanish.urls[0] == base(networkLogos().at("TELEMUNDO")));
  CHECK(spanish.urls[1] == provider);

  // Two networks' words in the name: the one the reference data gives it.
  auto data = catalog::reference::fromJson(R"({"made": "2026-11-01", "channels": [], "stations": [
    {"c": "KQQQ", "n": "TELEMUNDO", "m": "Los Angeles", "v": "52", "l": "es"},
    {"c": "KCBS", "n": "CBS", "m": "Los Angeles", "v": "2"}]})");
  REQUIRE(data);
  catalog::reference::Index index(*data);
  CHECK(stationNetwork("NBC (KQQQ) TELEMUNDO", "KQQQ", index) == std::string("TELEMUNDO"));
  CHECK(stationNetwork("NBC (KQQQ)", "KQQQ", index) == std::string("NBC"));
  // A word inside another ("KCBS") isn't the network's.
  CHECK(stationNetwork("FOX 11 (KCBS)", "KCBS", index) == std::string("FOX"));
}

TEST_CASE("logos: a horizontal file where it fills the wide cell better") {
  CHECK(wideLogoSetUrl("syfy-us.png") == base("syfy-hz-us.png"));
  CHECK(wideLogoSetUrl("cnn-us.png") == base("cnn-us.png"));
  CHECK(wideLogoSetUrl("hd/hbo-2-hd-us.png") == base("hd/hbo-2-hd-us.png"));
  CHECK(wideLogoSetUrl("misc/vod/afv-tv-vod.png") == std::string(kLibraryRoot) + "misc/vod/afv-tv-vod.png");
  std::unordered_set<std::string_view> names(logoSetNames().begin(), logoSetNames().end());
  for (std::string_view n : horizontalNames()) {
    INFO(std::string(n));
    CHECK(names.count(n));
  }
  // Alone in the cell: the horizontal file.
  CHECK(spec("SYFY", std::string("Syfy"), false, false, std::nullopt, std::nullopt, "", std::nullopt,
             LineupCategory::entertainment, nullptr)
            .urls.front() == base("syfy-hz-us.png"));
  CHECK(spec("TENNIS CHANNEL", std::nullopt, false, false, std::nullopt, std::nullopt, "", std::nullopt,
             LineupCategory::sports, nullptr)
            .urls.front() == base("tennis-channel-hz-us.png"));
  // A station's network logo sits in a small box beside the call sign:
  // the stacked one.
  REQUIRE(networkLogos().at("NBC") == "nbc-logo-flat-us.png");
  CHECK(spec("NBC 4", std::nullopt, false, true, std::string("KQQQ"), 4, "", std::nullopt, LineupCategory::local,
             nullptr)
            .urls.front() == base("nbc-logo-flat-us.png"));
}

TEST_CASE("logos: numbered, extra and alternate feeds show their parent's logo") {
  CHECK(logoSetFile("BIG TEN ALTERNATE 2") == std::string("big-ten-network-us.png"));
  CHECK(logoSetFile("Big Ten Network Overflow 3") == std::string("big-ten-network-us.png"));
  CHECK(logoSetFile("ESPN COLLEGE EXTRA 6") == std::string("espn-college-extra-us.png"));
  CHECK(logoSetFile("BEIN SPORTS CONNECT 5") == std::string("bein-sports-us.png"));
  CHECK(logoSetFile("Tennis Channel 2") == std::string("tennis-channel-us.png"));
  CHECK(logoSetFile("FUBO SPORTS 1 HD") == std::string("fubo-sports-network-us.png"));
  CHECK(logoSetFile("MSG ALT-1") == std::string("msg-us.png"));
  // A numbered channel of its own keeps its own file.
  CHECK(logoSetFile("Showtime 2") == std::string("showtime-2-us.png"));
  // A number that is part of the name, or a parent that is only like a file.
  CHECK_FALSE(logoSetFile("The First 48"));
  CHECK_FALSE(logoSetFile("La 1"));
  CHECK_FALSE(logoSetFile("Extra 2"));

  // The reference data's overflow feeds with no file of their own: their
  // parent's.
  const catalog::reference::Index& reference = catalog::reference::builtInIndex();
  const catalog::reference::Channel* feeds = reference.channel("big-ten-network-alternate-overflow-feeds");
  const catalog::reference::Channel* parent = reference.channel("big-ten-network");
  if (feeds && parent && feeds->logo.empty() && feeds->parent == "big-ten-network" && !parent->logo.empty()) {
    CHECK(spec("B1G FEED Z", std::nullopt, false, false, std::nullopt, std::nullopt, "", std::nullopt,
               LineupCategory::sports, nullptr, "", "big-ten-network-alternate-overflow-feeds")
              .urls.front() == wideLogoSetUrl(parent->logo));
  }
}

TEST_CASE("logos: the best of a channel's logos for the wide cell") {
  auto q = [](const char* format, const char* url, const char* w, const char* h, const char* tags = "",
              bool main = true) { return logoQuality(format, url, w, h, tags, main); };
  REQUIRE(q("PNG", "https://x.invalid/a.png", "400", "150"));
  CHECK(*q("PNG", "https://x.invalid/a.png", "400", "150") > *q("JPEG", "https://x.invalid/a.jpg", "400", "150"));
  CHECK(*q("PNG", "https://x.invalid/a.png", "400", "150") > *q("PNG", "https://x.invalid/a.png", "400", "400"));
  CHECK(*q("PNG", "https://x.invalid/a.png", "400", "150") > *q("PNG", "https://x.invalid/a.png", "120", "45"));
  CHECK(*q("PNG", "https://x.invalid/a.png", "400", "150", "white") > *q("PNG", "https://x.invalid/a.png", "400", "150"));
  CHECK(*q("PNG", "https://x.invalid/a.png", "400", "150") >
        *q("PNG", "https://x.invalid/a.png", "400", "150", "", false));
  // A thread, a sliver or a speck only ever next to the name.
  CHECK(*q("PNG", "https://x.invalid/a.png", "400", "20") < 0);
  CHECK(*q("PNG", "https://x.invalid/a.png", "40", "120") < 0);
  CHECK(*q("PNG", "https://x.invalid/a.png", "32", "32") < 0);
  // Never: vector files, plain http, search thumbnails, small JPEGs.
  CHECK_FALSE(q("SVG", "https://x.invalid/a.svg", "400", "150"));
  CHECK_FALSE(q("PNG", "http://x.invalid/a.png", "400", "150"));
  CHECK_FALSE(q("JPEG", "https://encrypted-tbn0.gstatic.com/images?q=tbn:x", "400", "150"));
  CHECK_FALSE(q("JPEG", "https://x.invalid/a.jpg", "200", "100"));
  CHECK(q("", "https://x.invalid/a.png", "", ""));  // by its ending, size unknown

  std::string channels = "id,name,alt_names,network,country,closed\nPick.us,Pick,,,US,\n";
  std::string logos =
      "channel,feed,in_use,tags,width,height,format,url\n"
      "Pick.us,,TRUE,,1000,500,JPEG,https://example.invalid/big.jpg\n"
      "Pick.us,East,TRUE,,400,150,PNG,https://example.invalid/feed.png\n"
      "Pick.us,,TRUE,,512,512,PNG,https://example.invalid/square.png\n"
      "Pick.us,,TRUE,white,512,160,PNG,https://example.invalid/wide.png\n"
      "Pick.us,,TRUE,,512,160,PNG,https://example.invalid/later.png\n";
  auto saved = IptvOrgIndex::build(channels, logos);
  REQUIRE(saved);
  CHECK(saved->byID.at("pick.us") == "https://example.invalid/wide.png");
}

TEST_CASE("logos: other countries' channels by guide id only, packed") {
  std::string channels =
      "id,name,alt_names,network,country,closed,is_nsfw\n"
      "Home.us,Home One,,,US,,FALSE\n"
      "Kanal.tr,Kanal Yedi,,,TR,,FALSE\n"
      "Old.fr,Old,,,FR,2020-01-01,FALSE\n"
      "Adult.de,Adult,,,DE,,TRUE\n"
      "Elsewhere.mx,Elsewhere,,,MX,,FALSE\n"
      "Wiki.es,Wiki,,,ES,,FALSE\n"
      "Thread.it,Thread,,,IT,,FALSE\n";
  std::string logos =
      "channel,feed,in_use,tags,width,height,format,url\n"
      "Home.us,,TRUE,,400,150,PNG,https://i.imgur.com/home.png\n"
      "Kanal.tr,,TRUE,,400,400,JPEG,https://i.imgur.com/kanal.jpg\n"
      "Kanal.tr,,TRUE,,400,150,PNG,https://i.imgur.com/kanalwide.png\n"
      "Old.fr,,TRUE,,400,150,PNG,https://i.imgur.com/old.png\n"
      "Adult.de,,TRUE,,400,150,PNG,https://i.imgur.com/adult.png\n"
      "Elsewhere.mx,,TRUE,,400,150,PNG,https://cdn.example.invalid/elsewhere.png\n"
      "Wiki.es,,TRUE,,330,120,PNG,https://upload.wikimedia.org/wikipedia/commons/thumb/a/ab/W.svg/330px-W.svg.png\n"
      "Thread.it,,TRUE,,400,20,PNG,https://i.imgur.com/thread.png\n";
  auto known = IptvOrgIndex::build(channels, logos);
  REQUIRE(known);
  IptvOrgIndex::Abroad abroad = IptvOrgIndex::buildAbroad(channels, logos, *known);
  CHECK(abroad.prefixes ==
        std::vector<std::string>{"https://i.imgur.com/", "https://upload.wikimedia.org/wikipedia/commons/thumb/"});
  CHECK(abroad.words == "kanal.tr 0kanalwide.png wiki.es 1a/ab/W.svg/330px-W.svg.png");

  IptvOrgIndex::Saved saved = *known;
  saved.abroadPrefixes = abroad.prefixes;
  saved.abroadWords = abroad.words;
  IptvOrgIndex index(saved);
  CHECK(index.abroadCount() == 2);
  CHECK(index.abroad("KANAL.TR") == std::string("https://i.imgur.com/kanalwide.png"));
  CHECK(index.abroad("wiki.es") ==
        std::string("https://upload.wikimedia.org/wikipedia/commons/thumb/a/ab/W.svg/330px-W.svg.png"));
  CHECK(index.url(std::string("Kanal.tr"), "Whatever") == std::string("https://i.imgur.com/kanalwide.png"));
  CHECK(index.url(std::nullopt, "Whatever", "Kanal.tr") == std::string("https://i.imgur.com/kanalwide.png"));
  CHECK_FALSE(index.url(std::nullopt, "Kanal Yedi"));  // never by name
  for (const char* id : {"old.fr", "adult.de", "elsewhere.mx", "thread.it", "kanal", "kanal.tr2", "home.us", ""}) {
    INFO(id);
    CHECK_FALSE(index.abroad(id));
  }
  CHECK(index.url(std::string("home.us"), "x") == std::string("https://i.imgur.com/home.png"));
  CHECK_FALSE(IptvOrgIndex(*known).abroad("kanal.tr"));

  // Many ids, past the table's collisions.
  std::string words;
  for (int i = 0; i < 5000; ++i) words += (i ? " c" : "c") + std::to_string(i) + ".xx 0l" + std::to_string(i) + ".png";
  IptvOrgIndex::Saved many;
  many.abroadPrefixes = {"https://i.imgur.com/"};
  many.abroadWords = words;
  IptvOrgIndex big(many);
  CHECK(big.abroadCount() == 5000);
  bool all = true;
  for (int i = 0; i < 5000; ++i) {
    all = all && big.abroad("C" + std::to_string(i) + ".XX") == "https://i.imgur.com/l" + std::to_string(i) + ".png";
  }
  CHECK(all);
  CHECK_FALSE(big.abroad("c5000.xx"));

  // Names the provider writes for another country's channel, by its id.
  IptvOrgIndex::Saved spelled;
  spelled.abroadPrefixes = {"https://i.imgur.com/"};
  spelled.abroadWords = "antena3internacional.es 0a3.png";
  CHECK(IptvOrgIndex(spelled).url(std::nullopt, "ANTENA 3 INTERNACIONAL") == std::string("https://i.imgur.com/a3.png"));
}

TEST_CASE("logos: the directory's Wikidata logo after the public database's, before the provider's") {
  const std::string provider = "https://example.invalid/p.png";
  const std::string wiki = "https://upload.wikimedia.org/wikipedia/commons/thumb/a/ab/D.svg/330px-D.svg.png";
  LogoSpec only = spec("Dream Channel", std::nullopt, false, false, std::nullopt, std::nullopt, provider, std::nullopt,
                       LineupCategory::entertainment, nullptr, "", "", wiki);
  CHECK(only.urls == std::vector<std::string>{wiki, provider});
  CHECK(only.sources == std::vector<std::string>{"Wikidata", "provider"});
  CHECK(only.style == LogoSpec::Style::logoOnly);
  IptvOrgIndex::Saved saved;
  saved.byName["dream channel"] = "https://example.invalid/db.png";
  IptvOrgIndex index(saved);
  CHECK(spec("Dream Channel", std::nullopt, false, false, std::nullopt, std::nullopt, provider, std::nullopt,
             LineupCategory::entertainment, &index, "", "", wiki)
            .sources == std::vector<std::string>{"public database", "Wikidata", "provider"});
  // A curated logo first.
  CHECK(spec("MTV2", std::nullopt, false, false, std::nullopt, std::nullopt, "", std::nullopt, LineupCategory::music,
             nullptr, "", "", wiki)
            .urls == std::vector<std::string>{base("mtv-2-us.png")});
}
