#include "core/sports/text.hpp"

#include <cstdio>

#include "core/sources/common.hpp"

namespace marigold::sports::text {

std::optional<std::string> text(const json::Value& v) {
  if (v.isString()) {
    std::string trimmed(sources::trimWhitespaceAndNewlines(v.asString()));
    if (trimmed.empty() || str::lower(trimmed) == "null") return std::nullopt;
    return trimmed;
  }
  if (v.isNumber()) return sources::lenientString(v);
  return std::nullopt;
}

namespace {

bool digits(std::string_view s, size_t at, size_t n) {
  if (at + n > s.size()) return false;
  for (size_t i = at; i < at + n; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

int number(std::string_view s, size_t at, size_t n) {
  int v = 0;
  for (size_t i = at; i < at + n; ++i) v = v * 10 + (s[i] - '0');
  return v;
}

// One code point's byte length at s[i] (invalid bytes count as one).
size_t codePointLength(std::string_view s, size_t i) {
  unsigned char c = static_cast<unsigned char>(s[i]);
  size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
  if (i + n > s.size()) return 1;
  for (size_t k = 1; k < n; ++k) {
    if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return 1;
  }
  return n;
}

size_t bytesFor(std::string_view s, size_t codePoints) {
  size_t i = 0, n = 0;
  while (i < s.size() && n < codePoints) {
    i += codePointLength(s, i);
    ++n;
  }
  return i;
}

std::string twoDigits(int v) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d", v);
  return buf;
}

}  // namespace

std::optional<Seconds> date(const std::optional<std::string>& value) {
  if (!value) return std::nullopt;
  std::string_view s = *value;
  // "yyyy-MM-dd'T'HH:mm'Z'" in UTC.
  if (s.size() == 17 && digits(s, 0, 4) && s[4] == '-' && digits(s, 5, 2) && s[7] == '-' &&
      digits(s, 8, 2) && s[10] == 'T' && digits(s, 11, 2) && s[13] == ':' && digits(s, 14, 2) &&
      s[16] == 'Z') {
    time::Civil c{number(s, 0, 4), number(s, 5, 2), number(s, 8, 2), number(s, 11, 2),
                  number(s, 14, 2), 0};
    if (c.month >= 1 && c.month <= 12 && c.day >= 1 && c.day <= time::daysInMonth(c.year, c.month) &&
        c.hour <= 23 && c.minute <= 59) {
      return time::secondsFromCivil(c);
    }
    return std::nullopt;
  }
  return sources::parseInternetDateTime(s);
}

std::string prefix(std::string_view s, size_t n) { return std::string(s.substr(0, bytesFor(s, n))); }

std::string dropPrefix(std::string_view s, size_t n) { return std::string(s.substr(bytesFor(s, n))); }

std::string firstCharacter(std::string_view s) { return prefix(s, 1); }

bool startsWithLetter(std::string_view s) { return !s.empty() && rx::test("^\\p{L}", s); }
bool isAllDigits(std::string_view s) { return !s.empty() && rx::test("^\\p{N}+$", s); }
bool isAllLetters(std::string_view s) { return !s.empty() && rx::test("^\\p{L}+$", s); }
bool hasDigit(std::string_view s) { return rx::test("\\p{N}", s); }
bool hasLetter(std::string_view s) { return rx::test("\\p{L}", s); }
bool hasLowercase(std::string_view s) { return rx::test("\\p{Ll}", s); }

std::vector<std::string> components(std::string_view s, std::string_view sep) {
  std::vector<std::string> out;
  if (sep.empty()) {
    out.emplace_back(s);
    return out;
  }
  size_t from = 0;
  while (true) {
    size_t at = s.find(sep, from);
    if (at == std::string_view::npos) {
      out.emplace_back(s.substr(from));
      return out;
    }
    out.emplace_back(s.substr(from, at - from));
    from = at + sep.size();
  }
}

std::vector<std::string> words(std::string_view s) {
  std::vector<std::string> out;
  for (std::string_view w : str::split(s, ' ')) out.emplace_back(w);
  return out;
}

bool lessCaseInsensitive(std::string_view a, std::string_view b) {
  return str::lower(str::foldDiacritics(a)) < str::lower(str::foldDiacritics(b));
}

bool containsCaseInsensitive(std::string_view hay, std::string_view needle) {
  return str::lower(str::foldDiacritics(hay)).find(str::lower(str::foldDiacritics(needle))) !=
         std::string::npos;
}

Seconds startOfDay(Seconds t, const time::Zone& zone) {
  time::Civil c = zone.toLocal(t);
  return zone.fromLocal({c.year, c.month, c.day, 0, 0, 0});
}

Seconds addDays(Seconds t, int days, const time::Zone& zone) {
  time::Civil c = zone.toLocal(t);
  time::Civil moved = time::civilFromSeconds(
      time::secondsFromCivil({c.year, c.month, c.day, 12, 0, 0}) + int64_t{days} * 86'400);
  return zone.fromLocal({moved.year, moved.month, moved.day, c.hour, c.minute, c.second});
}

int dayDifference(Seconds from, Seconds to, const time::Zone& zone) {
  time::Civil a = zone.toLocal(from), b = zone.toLocal(to);
  return static_cast<int>(time::daysFromCivil(b.year, b.month, b.day) -
                          time::daysFromCivil(a.year, a.month, a.day));
}

std::string yearMonth(Seconds t, const time::Zone& zone) {
  time::Civil c = zone.toLocal(t);
  return std::to_string(c.year) + twoDigits(c.month);
}

std::string yearMonthDay(Seconds t, const time::Zone& zone) {
  time::Civil c = zone.toLocal(t);
  return std::to_string(c.year) + twoDigits(c.month) + twoDigits(c.day);
}

std::string isoDay(Seconds t, const time::Zone& zone) {
  time::Civil c = zone.toLocal(t);
  return std::to_string(c.year) + "-" + twoDigits(c.month) + "-" + twoDigits(c.day);
}

}  // namespace marigold::sports::text
