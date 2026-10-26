// Small Foundation stand-ins for the sports port (internal).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/text.hpp"
#include "core/util/json.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::text {

using time::Seconds;

using catalog::text::contains;
using catalog::text::count;
using catalog::text::toInt;

// SportsParse.text: trimmed text (nil when empty or "null"), or a number as
// text ("401", "2.5"). The lean Swift decoder never reads a bool as text.
std::optional<std::string> text(const json::Value& v);
// A JSON true (Swift's `as? Bool == true` / LenientBool).
inline bool flag(const json::Value& v) { return v.isBool() && v.asBool(); }

// "2026-10-25T17:00Z" (no seconds) or full ISO 8601 internet date-time.
std::optional<Seconds> date(const std::optional<std::string>& value);

// The first `n` code points.
std::string prefix(std::string_view s, size_t n);
// Everything after the first `n` code points.
std::string dropPrefix(std::string_view s, size_t n);
// The first code point (empty for an empty string).
std::string firstCharacter(std::string_view s);
// Swift Character.isLetter / isNumber for the first code point.
bool startsWithLetter(std::string_view s);
bool isAllDigits(std::string_view s);   // non-empty, every code point a number
bool isAllLetters(std::string_view s);  // non-empty, every code point a letter
bool hasDigit(std::string_view s);
bool hasLetter(std::string_view s);
bool hasLowercase(std::string_view s);

// Swift's components(separatedBy:) with a string separator (keeps empties).
std::vector<std::string> components(std::string_view s, std::string_view sep);
// Swift's split(separator: " ") (drops empties).
std::vector<std::string> words(std::string_view s);

// localizedCaseInsensitiveCompare == .orderedAscending (accents and case
// folded, then byte order).
bool lessCaseInsensitive(std::string_view a, std::string_view b);
// localizedCaseInsensitiveContains.
bool containsCaseInsensitive(std::string_view hay, std::string_view needle);

// Calendar helpers in a zone (Calendar.current in Swift).
Seconds startOfDay(Seconds t, const time::Zone& zone);
// The same wall-clock time `days` later (Calendar.date(byAdding: .day)).
Seconds addDays(Seconds t, int days, const time::Zone& zone);
// Whole days between the starts of the two days (dateComponents([.day])).
int dayDifference(Seconds from, Seconds to, const time::Zone& zone);
// "yyyyMM", "yyyyMMdd", "yyyy-MM-dd" in a zone.
std::string yearMonth(Seconds t, const time::Zone& zone);
std::string yearMonthDay(Seconds t, const time::Zone& zone);
std::string isoDay(Seconds t, const time::Zone& zone);

}  // namespace marigold::sports::text
