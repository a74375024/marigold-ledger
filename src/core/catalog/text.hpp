// Small Foundation stand-ins used by the catalog port (internal).
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::catalog::text {

// Swift's Int(String): optional sign, ASCII digits only, nil on overflow.
std::optional<int> toInt(std::string_view s);

// Swift's String.count. Swift counts grapheme clusters; we count code points,
// which only differs for combining sequences and emoji.
inline size_t count(std::string_view s) { return str::utf8Length(s); }

inline bool contains(std::string_view hay, std::string_view needle) {
  return hay.find(needle) != std::string_view::npos;
}

// `word` (lower case; spaces around it ignored) as a whole word of `hay`:
// no letter just before or after it, so "nfl" is not in "conflict" and
// "fc" is not in "ufc". Digits don't join words ("formula1" has "formula");
// bytes of accented letters do.
inline bool hasWord(std::string_view hay, std::string_view word) {
  while (!word.empty() && word.front() == ' ') word.remove_prefix(1);
  while (!word.empty() && word.back() == ' ') word.remove_suffix(1);
  if (word.empty()) return false;
  auto letter = [](char c) {
    auto u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u >= 0x80;
  };
  for (size_t at = hay.find(word); at != std::string_view::npos; at = hay.find(word, at + 1)) {
    size_t end = at + word.size();
    bool startOk = at == 0 || !letter(hay[at - 1]) || !letter(word.front());
    bool endOk = end == hay.size() || !letter(hay[end]) || !letter(word.back());
    if (startOk && endOk) return true;
  }
  return false;
}

// First match of `pattern` (the whole match, Swift's RX.range), if any.
std::optional<std::string> firstMatch(std::string_view pattern, std::string_view text,
                                      bool caseInsensitive = false);
// Capture group `group` of the first match, if the match and group exist.
std::optional<std::string> firstGroup(std::string_view pattern, std::string_view text, size_t group,
                                      bool caseInsensitive = false);

// Foundation's `capitalized`: the first letter of each word upper-cased, the
// rest lower-cased. Words start after anything that isn't a letter, digit or
// apostrophe (ICU word breaks, which is what Foundation uses in practice).
std::string capitalized(std::string_view s);

// Finder-style ordering (Foundation's localizedStandardCompare): case and
// accents ignored, runs of digits compared as numbers. -1, 0 or 1.
int standardCompare(std::string_view a, std::string_view b);

}  // namespace marigold::catalog::text
