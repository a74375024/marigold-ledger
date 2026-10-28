// Reading provider event titles that no schedule game matched.
#include <doctest.h>

#include <string>

#include "core/sports/titles.hpp"

using namespace marigold::sports;

TEST_CASE("titles: a matchup, with the piece beside it as the tag") {
  titles::Title t = titles::read("Rugby: Harbor Sharks vs Coast Kings - Six Nations", "Rugby");
  REQUIRE(t.sides);
  CHECK(t.sides->first == "Harbor Sharks");
  CHECK(t.sides->second == "Coast Kings");
  CHECK_FALSE(t.homeSecond);
  CHECK_FALSE(t.players);
  CHECK(t.stage == "Six Nations");
  CHECK(t.name == "Harbor Sharks vs Coast Kings");
  CHECK_FALSE(t.chip);   // the league's
  CHECK(t.icon.empty());

  titles::Title cricket = titles::read("Cricket: Harborland v Coastia - 3rd ODI", "Cricket");
  REQUIRE(cricket.sides);
  CHECK(cricket.sides->second == "Coastia");
  CHECK(cricket.stage == "3rd ODI");

  // No stage: no tag.
  titles::Title volley = titles::read("NCAA Volleyball: Harbor State vs Valley Tech", "Volleyball");
  REQUIRE(volley.sides);
  CHECK(volley.sides->first == "Harbor State");
  CHECK_FALSE(volley.stage);

  // "A at B": the second side is at home.
  titles::Title away = titles::read("Harbor Hawks at Valley Bears", "Other");
  REQUIRE(away.sides);
  CHECK(away.homeSecond);
}

TEST_CASE("titles: people") {
  titles::Title darts = titles::read("Darts: Premier League Night 12 - A. Harbor vs B. Valley", "Darts");
  REQUIRE(darts.sides);
  CHECK(darts.sides->first == "A. Harbor");
  CHECK(darts.sides->second == "B. Valley");
  CHECK(darts.players);
  CHECK(darts.stage == "Night 12");
  CHECK(darts.subtitle == "Premier League");
  CHECK(darts.chip == "DARTS");
  CHECK(darts.icon == "darts");

  titles::Title boxing = titles::read("Boxing: C. Rivers vs D. Stone - Heavyweight", "Fighting");
  REQUIRE(boxing.sides);
  CHECK(boxing.players);
  CHECK(boxing.stage == "Heavyweight");
  CHECK(boxing.chip == "BOXING");
  CHECK(boxing.icon == "fighting");

  // Initials alone say they're people.
  CHECK(titles::read("E. Harbor vs F. Valley", "Other").players);
  CHECK_FALSE(titles::read("Harbor City vs Valley Town", "Other").players);
}

TEST_CASE("titles: events with a stage") {
  titles::Title golf = titles::read("Golf: The Harbor Classic - Round 2 Featured Groups", "Golf");
  CHECK_FALSE(golf.sides);
  CHECK(golf.name == "The Harbor Classic");
  CHECK(golf.stage == "Round 2");
  CHECK(golf.subtitle == "Featured Groups");

  // A discipline goes under the event; the series is the chip.
  titles::Title games = titles::read("Olympics: Athletics - Men's 100m Final", "Other");
  CHECK(games.name == "Men's 100m");
  CHECK(games.subtitle == "Athletics");
  CHECK(games.stage == "Final");
  CHECK(games.chip == "OLYMPICS");
  CHECK(games.icon == "athletics");
  // Without one, the games' own icon.
  CHECK(titles::read("Olympics: Opening Ceremony", "Other").icon == "multi");

  // A series' name goes under when nothing else does.
  titles::Title moto = titles::read("MotoGP: Grand Prix of Harbor - Race", "Motorsport");
  CHECK(moto.name == "Grand Prix of Harbor");
  CHECK(moto.stage == "Race");
  CHECK(moto.subtitle == "MotoGP");
  CHECK(moto.chip == "MOTOGP");

  titles::Title bull = titles::read("Pro Bull Riding: Harbor City Invitational", "Other");
  CHECK(bull.name == "Harbor City Invitational");
  CHECK(bull.subtitle == "Pro Bull Riding");
  CHECK_FALSE(bull.stage);
  CHECK_FALSE(bull.chip);
  CHECK(bull.icon.empty());

  // A stage ending the only piece; one starting a name stays in it.
  titles::Title round = titles::read("Harbor Championship Final Round", "Golf");
  CHECK(round.name == "Harbor Championship");
  CHECK(round.stage == "Final Round");
  CHECK(titles::read("Race of Champions", "Motorsport").name == "Race of Champions");
  CHECK_FALSE(titles::read("Race of Champions", "Motorsport").stage);
  // A title that is only a stage keeps it as the name.
  titles::Title only = titles::read("Qualifying", "Motorsport");
  CHECK(only.name == "Qualifying");
  CHECK_FALSE(only.stage);
  // "Final Four" is a name, not a final.
  CHECK_FALSE(titles::stageIn("NCAA Final Four"));
}

TEST_CASE("titles: icons from words, places that aren't sides") {
  CHECK(titles::read("Tour de France: Stage 14", "Other").icon == "cycling");
  // Only a stage after the series: the stage is the name, the series under it.
  CHECK(titles::read("Tour de France: Stage 14", "Other").name == "Stage 14");
  CHECK(titles::read("Tour de France: Stage 14", "Other").subtitle == "Tour de France");
  CHECK(titles::read("Harbor Downs Horse Racing", "Other").icon == "horseRacing");
  CHECK(titles::read("WWE: Harbor Night of Champions", "Fighting").icon == "wrestling");
  CHECK(titles::read("Harbor City Marathon", "Other").icon == "athletics");
  // Only a title the catalog couldn't place gets a word's icon.
  CHECK(titles::read("Soccer: Harbor FC Marathon Match", "Soccer").icon.empty());
  // "Round 2 at Augusta": a place.
  CHECK_FALSE(titles::read("Golf: Round 2 at Harbor Links", "Golf").sides);
  // Long show names are titles, not series.
  titles::Title show = titles::read("The Harbor Morning Sports Hour: Weekend Preview", "Other");
  CHECK(show.name == "The Harbor Morning Sports Hour");
  CHECK(show.subtitle == "Weekend Preview");
}

TEST_CASE("titles: a matchup repeated in brackets, and tags after it") {
  titles::Title t = titles::read("Harbor College vs Valley Tech _ Women`s (Harbor vs Valley)", "Soccer");
  REQUIRE(t.sides);
  CHECK(t.sides->first == "Harbor College");
  CHECK(t.sides->second == "Valley Tech");
  CHECK(t.stage == "Women's");
  CHECK(t.name == "Harbor College vs Valley Tech");

  // Cut off at the end, with a language tag.
  titles::Title cut = titles::read("Harbor FC vs. Valley United (ESP) (Harbor FC vs. Valley", "Soccer");
  REQUIRE(cut.sides);
  CHECK(cut.sides->first == "Harbor FC");
  CHECK(cut.sides->second == "Valley United");

  titles::Title exh = titles::read("Harbor St vs. Valley (EXH) (Harbor St vs. Valley", "Basketball");
  REQUIRE(exh.sides);
  CHECK(exh.sides->second == "Valley");
  CHECK(exh.stage == "Exhibition");
}

TEST_CASE("titles: the provider's stand-ins, shouting, tags and odd sides") {
  // Any case of the separator; ordinary capitals for a shouted title.
  titles::Title loud = titles::read("HARBOR UNITED VS. VALLEY SUBURBS", "Soccer");
  REQUIRE(loud.sides);
  CHECK(loud.sides->first == "Harbor United");
  CHECK(loud.sides->second == "Valley Suburbs");
  titles::Title fc = titles::read("DISTRICT-17 FC VS. HARBOR FC", "Soccer");
  REQUIRE(fc.sides);
  CHECK(fc.sides->first == "District-17 FC");
  // " _ " tails off the second side; "_" between letters a hyphen; "`" an apostrophe.
  titles::Title tail = titles::read("Saint Harbor`s at Valley_Tech _ Women`s", "Volleyball");
  REQUIRE(tail.sides);
  CHECK(tail.sides->first == "Saint Harbor's");
  CHECK(tail.sides->second == "Valley-Tech");
  CHECK(tail.stage == "Women's");
  titles::Title sport = titles::read("Harbor University at Valley _ Field Hockey", "Field Hockey");
  REQUIRE(sport.sides);
  CHECK(sport.sides->second == "Valley");
  CHECK_FALSE(sport.stage);
  // "(Home)" marks the home side and goes.
  titles::Title home = titles::read("Harbor Hawks at Valley 67`s (Home)", "Hockey");
  REQUIRE(home.sides);
  CHECK(home.sides->second == "Valley 67's");
  CHECK(home.homeSecond);
  // The same school twice: one event, its mat the tag.
  titles::Title mat = titles::read("Harbor Colo._ CO at Harbor Colo._ CO (Mat 1)", "Wrestling");
  CHECK_FALSE(mat.sides);
  CHECK(mat.name == "Harbor Colo., CO");
  CHECK(mat.stage == "Mat 1");
  // Feed details and a serial number aren't the event; a date tail goes.
  titles::Title race = titles::read("2618 Harbor City GP F1", "Motorsport");
  CHECK(race.name == "Harbor City GP F1");
  titles::Title feed = titles::read("Harbor: Practice 1 [Main Broadcast] (UHD)", "Motorsport");
  CHECK(feed.name.find("Main Broadcast") == std::string::npos);
  CHECK(feed.name.find("UHD") == std::string::npos);
  titles::Title radio = titles::read("HarborWrestling Radio Live _ Oct 29th", "Wrestling");
  CHECK(radio.name == "HarborWrestling Radio Live");
  // All lower case: capitals, state codes kept.
  titles::Title low = titles::read("harbor field, mi", "Motorsport");
  CHECK(low.name == "Harbor Field, MI");
  titles::Title open = titles::read("Harbor Open (MAT_04)", "Wrestling");
  CHECK(open.name == "Harbor Open");
  CHECK(open.stage == "Mat 4");
  // "Live" says nothing on a tile.
  titles::Title live = titles::read("Harbor vs Valley - Live", "Soccer");
  CHECK(live.name == "Harbor vs Valley");
  CHECK_FALSE(live.subtitle);
  titles::Title lead = titles::read("live:  Harbor vs Valley _ Field Hockey", "Field Hockey");
  CHECK(lead.name == "Harbor vs Valley");
  CHECK_FALSE(lead.subtitle);
  // A race "at" a track has no sides.
  CHECK_FALSE(titles::read("Harbor Series at Valley Speedway", "Motorsport").sides);
}
