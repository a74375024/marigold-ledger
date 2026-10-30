// Reading a provider's event title when no schedule game matched it:
// "Rugby: Ireland vs France - Six Nations", "Darts: Premier League Night
// 12 - A. Harbor vs B. Valley", "Olympics: Athletics - Men's 100m Final".
// What it yields is what a title tile shows: the matchup or the event's
// name, a line under it, the round or stage for the tag, and the sport's
// chip and icon.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace marigold::sports::titles {

struct Title {
  std::string name;                     // "Ireland vs France", "The Masters", "Men's 100m"
  std::optional<std::string> subtitle;  // the series or discipline ("MotoGP", "Athletics")
  std::optional<std::string> stage;     // the tag ("Round 2", "3rd ODI", "Night 12", "Final")
  std::optional<std::string> chip;      // a sport or series named in it ("BOXING", "OLYMPICS")
  std::string icon;                     // a sport icon named in it ("athletics"); empty: the league's
  // A matchup: the two sides as named, and whether the second is at home
  // ("A at B", "A @ B").
  std::optional<std::pair<std::string, std::string>> sides;
  bool homeSecond = false;
  bool players = false;  // the sides are people (darts, boxing, snooker, "A. Name")
};

// `league` is the row the catalog gave it ("Rugby", "Fighting", "Other").
Title read(std::string_view title, std::string_view league);

// A title from another country's feed, its round and stage words in English
// ("Kvartfinaler, Wien" -> "Quarterfinals, Wien", "Giorno 2" -> "Day 2",
// "Grand Slam De Judô: Finais" -> "Grand Slam De Judo: Finals"), so it
// reads like the rest and meets the same event's English feed.
std::string inEnglish(std::string_view title);

// The stage named in a piece of a title ("Round 2", "3rd ODI", "Semi-final"),
// or nullopt.
std::optional<std::string> stageIn(std::string_view text);

}  // namespace marigold::sports::titles
