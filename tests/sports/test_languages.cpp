// Build 132: the language each Live Events source is in (a channel's name
// marks, its market, its sports brand; "AR" read as Arabic or Argentina),
// the viewer's languages from the countries picked, and the table of other
// countries' sports brands (what it is and what it isn't). Every channel
// name is made up in the brands' style.
#include "core/catalog/catalog.hpp"
#include "core/sports/brands.hpp"
#include "core/sports/languages.hpp"
#include "core/sports/store.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::sports;
using catalog::LineupCategory;

namespace {

std::string channel(const std::string& name, std::optional<std::string> market,
                    LineupCategory category = LineupCategory::sports) {
  return languages::ofChannel(name, market, category);
}

}  // namespace

TEST_CASE("languages: a market's language, read from the language map the other way") {
  CHECK(catalog::marketLanguage("US") == "en");
  CHECK(catalog::marketLanguage("US-ES") == "es");
  CHECK(catalog::marketLanguage("MX") == "es");
  CHECK(catalog::marketLanguage("UK") == "en");
  CHECK(catalog::marketLanguage("DE") == "de");
  CHECK(catalog::marketLanguage("AE") == "ar");
  CHECK(catalog::marketLanguage("BR") == "pt");
  CHECK(catalog::marketLanguage("PT") == "pt");
  CHECK(catalog::marketLanguage("PK") == "ur");
  // Several languages, or two places: not one.
  CHECK(catalog::marketLanguage("IN").empty());
  CHECK(catalog::marketLanguage("CA").empty());
  CHECK(catalog::marketLanguage("AR").empty());
  CHECK(catalog::marketLanguage("ZZ").empty());
}

TEST_CASE("languages: a channel by its name's mark, its market, its brand; unknown when unsure") {
  // Marks in the name win.
  CHECK(channel("Harbor Sports (ES)", std::string("US")) == "es");
  CHECK(channel("Harbor Sports Espa\xC3\xB1ol", std::nullopt) == "es");
  CHECK(channel("beIN SPORTS en Espa\xC3\xB1ol", std::string("US")) == "es");
  CHECK(channel("Harbor Cricket Hindi", std::string("IN")) == "hi");
  CHECK(channel("Harbor Cricket Tamil", std::string("IN")) == "ta");
  CHECK(channel("beIN SPORTS 1 English", std::string("AR")) == "en");
  CHECK(channel("Harbor Sports (EN)", std::nullopt) == "en");
  // The market.
  CHECK(channel("Harbor Sports Network", std::string("US")) == "en");
  CHECK(channel("Canal Uno", std::string("US"), LineupCategory::spanish) == "es");
  CHECK(channel("Canal Uno", std::string("US-ES"), LineupCategory::local) == "es");
  CHECK(channel("Harbor Deportivo", std::string("MX"), LineupCategory::international) == "es");
  CHECK(channel("Sky Sport Bundesliga 1", std::string("DE"), LineupCategory::international) == "de");
  CHECK(channel("TSN 2", std::string("CA"), LineupCategory::international) == "en");
  CHECK(channel("RDS 2", std::string("CA"), LineupCategory::international) == "fr");
  // India's channels are in several languages: not known without a mark.
  CHECK(channel("Star Sports 1", std::string("IN"), LineupCategory::international).empty());
  // No market: the brand's language, when it has one.
  CHECK(channel("Sky Sports Main Event", std::nullopt) == "en");
  CHECK(channel("Willow Xtra", std::nullopt) == "en");
  CHECK(channel("beIN SPORTS 3", std::nullopt).empty());
  CHECK(channel("Harbor Sports Network", std::nullopt).empty());
}

TEST_CASE("languages: AR is the Gulf's sports brands in Arabic, Argentina's in Spanish, else unknown") {
  auto ar = [](const std::string& name) { return channel(name, std::string("AR"), LineupCategory::international); };
  CHECK(ar("beIN SPORTS 1 HD") == "ar");
  CHECK(ar("beIN SPORTS MAX 4") == "ar");
  CHECK(ar("SSC Sport 2") == "ar");
  CHECK(ar("Alkass One") == "ar");
  CHECK(ar("AD Sports Premium 1") == "ar");
  CHECK(ar("\xD8\xB3\xD8\xA8\xD9\x88\xD8\xB1\xD8\xAA 1") == "ar");  // Arabic script
  CHECK(ar("TyC Sports") == "es");
  CHECK(ar("TNT Sports Argentina") == "es");
  CHECK(ar("ESPN 2 Sur") == "es");
  CHECK(ar("Harbor Channel").empty());
}

TEST_CASE("languages: a provider feed by its market or Spanish words; brackets alone mark others") {
  CHECK(languages::ofFeed("Team Alpha vs Harbor City Gulls", "us (espn+ #)", std::string("US")) == "en");
  CHECK(languages::ofFeed("Team Alpha vs Harbor City Gulls", "fam", std::string("US-ES")) == "es");
  CHECK(languages::ofFeed("Team Alpha vs Harbor City Gulls (Espa\xC3\xB1ol)", "fam", std::nullopt) == "es");
  CHECK(languages::ofFeed("Team Alpha vs Harbor City Gulls [English]", "fam", std::string("AR")) == "en");
  // A title's words name places and teams, not the language.
  CHECK(languages::ofFeed("French Open: Ann Player vs Bea Racket", "fam", std::nullopt).empty());
  CHECK(languages::ofFeed("Saudi Arabia vs Harbor", "fam", std::nullopt).empty());
  CHECK(languages::ofFeed("ENG v AUS", "fam", std::string("UK")) == "en");
  CHECK(languages::ofFeed("Harbor vs Valley", "ar (bein #)", std::string("AR")) == "ar");
  CHECK(languages::ofFeed("Harbor vs Valley", "fam", std::string("AR")).empty());
  CHECK(languages::ofFeed("Harbor vs Valley", "fam", std::nullopt).empty());
}

TEST_CASE("languages: the viewer's are the countries' and languages' picked; English when none says") {
  CHECK(languages::ofViewer({"US"}) == std::set<std::string>{"en"});
  CHECK(languages::ofViewer({"US-ES"}) == std::set<std::string>{"es"});
  CHECK(languages::ofViewer({"US", "US-ES"}) == std::set<std::string>{"en", "es"});
  CHECK(languages::ofViewer({"MX"}) == std::set<std::string>{"es"});
  CHECK(languages::ofViewer({"UK"}) == std::set<std::string>{"en"});
  CHECK(languages::ofViewer({"IN"}) == std::set<std::string>{"en", "hi"});
  CHECK(languages::ofViewer({"CA"}) == std::set<std::string>{"en", "fr"});
  CHECK(languages::ofViewer({"AR"}) == std::set<std::string>{"ar", "es"});
  CHECK(languages::ofViewer({}) == std::set<std::string>{"en"});
  CHECK(languages::ofViewer({"ASIA"}) == std::set<std::string>{"en"});
  CHECK(languages::name("es") == "Spanish");
  CHECK(languages::name("ar") == "Arabic");
  CHECK(languages::name("de") == "German");
  CHECK(languages::name("xx").empty());
}

TEST_CASE("languages: how a source fits; My Lineup's always welcome") {
  std::set<std::string> english = {"en"};
  CHECK(languages::fit("en", english) == languages::Fit::yours);
  CHECK(languages::fit("", english) == languages::Fit::unknown);
  CHECK(languages::fit("es", english) == languages::Fit::other);
  CHECK(languages::fit("es", {"en", "es"}) == languages::Fit::yours);
  CHECK(languages::fit("en", {}) == languages::Fit::yours);  // none given: English
  EventSource s;
  s.kind = EventSource::Kind::guide;
  s.lang = "ar";
  CHECK_FALSE(languages::welcome(s, english));
  s.kind = EventSource::Kind::lineup;
  CHECK(languages::welcome(s, english));
  s.kind = EventSource::Kind::provider;
  s.lang = "";
  CHECK(languages::welcome(s, english));
}

TEST_CASE("brands: other countries' sports channels, and what isn't one") {
  for (const char* yes :
       {"Sky Sports Main Event", "UK| SKY SPORTS PREMIER LEAGUE FHD", "Sky Sports F1 UHD", "Sky Sports Cricket",
        "Sky Sports Golf", "Sky Sports Tennis", "TNT Sports 1", "TNT SPORTS 4 HD", "Premier Sports 2", "LaLiga TV",
        "M+ LaLiga TV", "M+ Liga de Campeones 2", "Movistar LaLiga", "DAZN 1", "DAZN F1", "Sky Sport Bundesliga 3",
        "Sky Sport Calcio", "Sky Sport Uno", "Sky Sport 1 NZ", "Magenta Sport", "ServusTV", "Servus TV HD",
        "Canal+ Foot", "CANAL+ SPORT 360", "Canal+ F1", "Canal+ Top 14", "beIN SPORTS 1", "beIN Sport MAX 2",
        "Sport TV 1", "SPORT TV+", "SportTV 3", "Premiere Clubes", "Premiere 2", "SporTV 2", "TUDN", "Azteca Deportes",
        "TyC Sports", "TSN1", "TSN 4", "Sportsnet One", "Sportsnet 360", "RDS 2", "TVA Sports", "SSC Sport 1",
        "SSC Extra 2", "Alkass One", "Al Kass Two", "AD Sports 1", "Abu Dhabi Sports 2", "Willow", "Willow Xtra",
        "Star Sports 1 Hindi", "PTV Sports", "Ten Sports", "A Sports HD", "Fox Cricket", "SuperSport Grandstand",
        "Eurosport 1", "Tennis Channel"}) {
    CAPTURE(yes);
    CHECK(brands::isSportsBrand(yes));
  }
  for (const char* no : {"Sky News", "Sky Sports News", "Sky Sport News", "Sky Sport 24", "Sky Cinema Action",
                         "Sportsman Channel", "Sports Grid", "Harbor Sports Network", "Premiere Movies",
                         "Canal Plus Cinema", "SSC Napoli TV", "Tennis Highlights", "Star Plus", "Sporting Life",
                         "Willowbrook Local", ""}) {
    CAPTURE(no);
    CHECK_FALSE(brands::isSportsBrand(no));
  }
  // How the table reads a name.
  CHECK(brands::words("UK| Sky Sports F1 \xE1\xB4\xB4\xE1\xB4\xB0") == " uk sky sports f 1 ");
  CHECK(brands::words("M+ LaLiga") == " m plus laliga ");
  CHECK(brands::words("TSN1") == " tsn 1 ");
  CHECK(brands::words("Espa\xC3\xB1ol") == " espanol ");
  CHECK(brands::find("Sky Sports Cricket")->sport == std::string("cricket"));
  CHECK(brands::find("beIN SPORTS 2")->lang == std::string());
  CHECK(brands::all().size() >= 40);
}

TEST_CASE("cricket channels: Willow and Sky Sports Cricket, and Star, PTV, Ten, A Sports and SuperSport") {
  for (const char* yes : {"Willow", "Willow Cricket Xtra", "Sky Sports Cricket", "Fox Cricket", "Star Sports 1",
                          "Star Sports 2 Hindi", "PTV Sports", "Ten Sports HD", "A Sports", "SuperSport Grandstand",
                          "SuperSport Cricket"}) {
    CAPTURE(yes);
    CHECK(store::cricketChannelName(yes));
  }
  for (const char* no : {"Harbor Sports Network", "Star Plus", "Ten Network", "Sports Grid", "TSN 1"}) {
    CAPTURE(no);
    CHECK_FALSE(store::cricketChannelName(no));
  }
}
