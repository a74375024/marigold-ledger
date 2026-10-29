// Logo specs, the iptv-org index and the CSV reader.
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
      "Quoted.us,,,1,1,,https://example.invalid/QUOTED.JPG\n";
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
                           "Reelz Famous and Infamous", "REELZ FAMOUS & INFAMOUS", "STINGRAY SPA", "NBC Sports California Plus",
                           "NBC Sports California Plus 2"}) {
    INFO(name);
    CHECK(handPicked(name));
  }
  CHECK(handPicked("NBC Sports California Plus") == std::string(kLogoBase) + "nbcsn-california-us.png");
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
