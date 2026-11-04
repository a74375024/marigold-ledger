// Sports channels of other countries that Live Events also reads (Build
// 132): one table of the brands, by sport. A channel you have that is one of
// them but is from a country you didn't pick is read in the provider's own
// guide and becomes a way to watch a game its listing names (a guide
// source, ranked last, two at most per game). It joins nothing else: not the
// guide's lists, My Lineup or search.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace marigold::sports::brands {

struct Brand {
  // Words a channel's name has (brands::words: lower case, letters and
  // digits apart, padded with spaces): " sky sports main event ".
  const char* words;
  const char* sport;  // "multi", "soccer", "cricket", "motorsport", "golf", "tennis", "rugby"
  // The language its channels are in, when they're in one ("" when a
  // country's own versions differ: beIN, DAZN, Eurosport).
  const char* lang;
  // Words that make the name something else (" sky sports news ").
  const char* unless = "";
};

// The table, the more particular names first.
const std::vector<Brand>& all();

// A name as the table reads it: lower case, ASCII letters and digits only
// ("+" is " plus "), a letter-digit change is a word break ("TSN1" is
// "tsn 1"), padded with spaces: "UK| Sky Sports F1 ᴴᴰ" -> " uk sky sports f 1 ".
std::string words(std::string_view name);

// The brand a channel name is, or null.
const Brand* find(std::string_view name);
inline bool isSportsBrand(std::string_view name) { return find(name) != nullptr; }

}  // namespace marigold::sports::brands
