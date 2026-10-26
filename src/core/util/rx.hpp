// Regular expressions with ICU-compatible syntax, on PCRE2.
//
// The tvOS code this app ports uses NSRegularExpression (ICU): look-behinds,
// Unicode \b and \w, $1 templates. PCRE2 in UTF + UCP mode reads the same
// patterns the same way. Compiled patterns are cached (thread-safe) because
// channel-name analysis runs the same few hundred patterns over tens of
// thousands of names. Invalid UTF-8 in provider data never matches as a
// character but never throws either.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace marigold::rx {

struct Match {
  // Byte range of the whole match in the subject.
  size_t begin = 0, end = 0;
  // Capture groups; group 0 is the whole match. Unset groups are nullopt.
  std::vector<std::optional<std::string_view>> groups;

  std::string_view group(size_t i) const {
    return i < groups.size() && groups[i] ? *groups[i] : std::string_view();
  }
  bool has(size_t i) const { return i < groups.size() && groups[i].has_value(); }
};

bool test(std::string_view pattern, std::string_view text, bool caseInsensitive = false);

// First match, or nullopt. The views point into `text`.
std::optional<Match> first(std::string_view pattern, std::string_view text,
                           bool caseInsensitive = false);

// Every non-overlapping match, left to right.
std::vector<Match> all(std::string_view pattern, std::string_view text,
                       bool caseInsensitive = false);

// Replaces every match. `templ` uses $0..$9 (and ${n}) like ICU.
std::string replace(std::string_view text, std::string_view pattern, std::string_view templ,
                    bool caseInsensitive = false);

// False (and logged once) when the pattern does not compile.
bool valid(std::string_view pattern, bool caseInsensitive = false);

}  // namespace marigold::rx
