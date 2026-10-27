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
