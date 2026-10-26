#include "core/sources/common.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/util/redact.hpp"
#include "core/util/strings.hpp"

namespace marigold::sources {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Swift's CharacterSet.whitespaces is Unicode category Zs plus tab;
// .whitespacesAndNewlines adds U+000A..U+000D and U+0085 (and the Zl/Zp
// separators U+2028, U+2029).
bool isSwiftSpace(uint32_t cp, bool newlines) {
  if (cp == 0x20 || cp == 0x09 || cp == 0xA0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F ||
      cp == 0x205F || cp == 0x3000)
    return true;
  if (!newlines) return false;
  return (cp >= 0x0A && cp <= 0x0D) || cp == 0x85 || cp == 0x2028 || cp == 0x2029;
}

// Decodes the code point starting at s[i]; returns 0xFFFD (length 1) for an
// invalid sequence.
uint32_t codePointAt(std::string_view s, size_t i, size_t& len) {
  auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
  unsigned char c = b(i);
  len = 1;
  if (c < 0x80) return c;
  size_t n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
  if (n == 0 || i + n > s.size()) return 0xFFFD;
  uint32_t cp = c & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
  for (size_t k = 1; k < n; ++k) {
    if ((b(i + k) & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | (b(i + k) & 0x3F);
  }
  len = n;
  return cp;
}

std::string_view trimSwift(std::string_view s, bool newlines) {
  size_t start = 0;
  while (start < s.size()) {
    size_t len = 1;
    if (!isSwiftSpace(codePointAt(s, start, len), newlines)) break;
    start += len;
  }
  size_t end = s.size();
  while (end > start) {
    size_t lead = end - 1;
    while (lead > start && (static_cast<unsigned char>(s[lead]) & 0xC0) == 0x80) --lead;
    size_t len = 1;
    uint32_t cp = codePointAt(s, lead, len);
    if (lead + len != end || !isSwiftSpace(cp, newlines)) break;
    end = lead;
  }
  return s.substr(start, end - start);
}

// Swift Int(String): optional sign, then digits only.
std::optional<int64_t> parseInteger(std::string_view t) {
  size_t i = 0;
  bool negative = false;
  if (i < t.size() && (t[i] == '+' || t[i] == '-')) negative = t[i++] == '-';
  if (i == t.size()) return std::nullopt;
  uint64_t v = 0;
  const uint64_t limit = negative ? 9223372036854775808ull : 9223372036854775807ull;
  for (; i < t.size(); ++i) {
    if (!isDigit(t[i])) return std::nullopt;
    uint64_t d = static_cast<uint64_t>(t[i] - '0');
    if (v > (limit - d) / 10) return std::nullopt;
    v = v * 10 + d;
  }
  if (negative) return v == limit ? INT64_MIN : -static_cast<int64_t>(v);
  return static_cast<int64_t>(v);
}

// Decimal text Swift's Double(String) reads ("1.5", ".5", "5.", "1e3").
// Hex floats, "inf" and "nan" are not accepted (they'd trap in Int()).
std::optional<double> parseDecimal(std::string_view t) {
  size_t i = 0;
  if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
  size_t intDigits = 0, fracDigits = 0;
  while (i < t.size() && isDigit(t[i])) ++i, ++intDigits;
  if (i < t.size() && t[i] == '.') {
    ++i;
    while (i < t.size() && isDigit(t[i])) ++i, ++fracDigits;
  }
  if (intDigits + fracDigits == 0) return std::nullopt;
  if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
    ++i;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
    size_t expDigits = 0;
    while (i < t.size() && isDigit(t[i])) ++i, ++expDigits;
    if (expDigits == 0) return std::nullopt;
  }
  if (i != t.size()) return std::nullopt;
  std::string copy(t);
  return std::strtod(copy.c_str(), nullptr);
}

std::optional<int64_t> truncateToInt(double d) {
  if (!std::isfinite(d)) return std::nullopt;
  double t = std::trunc(d);
  if (t < -9223372036854775808.0 || t >= 9223372036854775808.0) return std::nullopt;
  return static_cast<int64_t>(t);
}

// Shortest text that reads back as the same double, like NSNumber's
// stringValue ("1.5", "0.1").
std::string shortestNumber(double v) {
  if (v == std::trunc(v) && std::fabs(v) < 9007199254740992.0) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
    return buf;
  }
  char buf[40];
  for (int precision = 15; precision <= 17; ++precision) {
    std::snprintf(buf, sizeof buf, "%.*g", precision, v);
    if (std::strtod(buf, nullptr) == v) break;
  }
  return buf;
}

int digits(std::string_view s, size_t at, size_t n) {
  int v = 0;
  for (size_t k = 0; k < n; ++k) {
    if (at + k >= s.size() || !isDigit(s[at + k])) return -1;
    v = v * 10 + (s[at + k] - '0');
  }
  return v;
}

}  // namespace

std::string Request::header(std::string_view name) const {
  for (const Header& h : headers)
    if (str::iequals(h.name, name)) return h.value;
  return std::string();
}

Error Error::make(std::string message, std::optional<int> status) {
  Error e;
  e.status = status;
  e.message = redact(message);
  return e;
}

std::string percentEncode(std::string_view text) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    unsigned char u = static_cast<unsigned char>(c);
    if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '-' || u == '.' ||
        u == '_' || u == '~') {
      out += c;
    } else {
      out += '%';
      out += kHex[u >> 4];
      out += kHex[u & 15];
    }
  }
  return out;
}

std::string queryString(std::vector<std::pair<std::string, std::string>> items) {
  std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::string out;
  for (const auto& [key, value] : items) {
    if (!out.empty()) out += '&';
    out += percentEncode(key);
    out += '=';
    out += percentEncode(value);
  }
  return out;
}

bool isValidBaseUrl(std::string_view url) {
  std::string lower = str::lowerAscii(url);
  size_t start;
  if (str::startsWith(lower, "http://")) start = 7;
  else if (str::startsWith(lower, "https://")) start = 8;
  else return false;
  size_t hostEnd = url.find('/', start);
  std::string_view host = url.substr(start, hostEnd == std::string_view::npos ? std::string_view::npos : hostEnd - start);
  if (host.empty() || host[0] == ':') return false;
  for (char c : url) {
    unsigned char u = static_cast<unsigned char>(c);
    if (u <= 0x20 || u == 0x7F || c == '?' || c == '#' || c == '"' || c == '<' || c == '>' || c == '\\' ||
        c == '^' || c == '`' || c == '{' || c == '|' || c == '}')
      return false;
  }
  return host.find('@') == std::string_view::npos;
}

std::string_view basePath(std::string_view url) {
  size_t scheme = url.find("://");
  if (scheme == std::string_view::npos) return {};
  size_t slash = url.find('/', scheme + 3);
  if (slash == std::string_view::npos) return {};
  return url.substr(slash);
}

std::string_view trimWhitespaceAndNewlines(std::string_view s) { return trimSwift(s, true); }
std::string_view trimWhitespace(std::string_view s) { return trimSwift(s, false); }

std::optional<int64_t> lenientInt(const json::Value& v) {
  if (v.isBool()) return v.asBool() ? 1 : 0;
  if (v.isNumber()) return truncateToInt(v.asNumber());
  if (!v.isString()) return std::nullopt;
  std::string text = v.asString();
  if (auto i = parseInteger(text)) return i;
  if (auto d = parseDecimal(text)) return truncateToInt(*d);
  return std::nullopt;
}

std::optional<std::string> lenientString(const json::Value& v) {
  if (v.isString()) {
    std::string text = v.asString();
    if (text.empty()) return std::nullopt;
    return text;
  }
  if (v.isBool()) return std::string(v.asBool() ? "1" : "0");
  if (v.isNumber()) return shortestNumber(v.asNumber());
  return std::nullopt;
}

std::optional<std::string> stringField(const json::Value& v) {
  if (!v.isString()) return std::nullopt;
  std::string text = v.asString();
  if (text.empty()) return std::nullopt;
  return text;
}

bool isSafeItemId(std::string_view id) {
  if (id.empty() || id.size() > kMaxItemIdBytes) return false;
  for (char c : id) {
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || isDigit(c) || c == '-' || c == '_' || c == '.';
    if (!ok) return false;
  }
  return id != "." && id != "..";
}

std::optional<time::Seconds> parseInternetDateTime(std::string_view s) {
  // yyyy-MM-ddTHH:mm:ss
  if (s.size() < 20) return std::nullopt;
  int year = digits(s, 0, 4), month = digits(s, 5, 2), day = digits(s, 8, 2);
  int hour = digits(s, 11, 2), minute = digits(s, 14, 2), second = digits(s, 17, 2);
  if (year < 0 || month < 0 || day < 0 || hour < 0 || minute < 0 || second < 0) return std::nullopt;
  if (s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != 't') || s[13] != ':' || s[16] != ':')
    return std::nullopt;
  if (month < 1 || month > 12 || day < 1 || day > time::daysInMonth(year, month) || hour > 23 || minute > 59 ||
      second > 59)
    return std::nullopt;
  std::string_view zone = s.substr(19);
  int offset = 0;
  if (zone == "Z" || zone == "z") {
    offset = 0;
  } else if ((zone.size() == 6 && zone[3] == ':') || zone.size() == 5) {
    if (zone[0] != '+' && zone[0] != '-') return std::nullopt;
    int oh = digits(zone, 1, 2);
    int om = digits(zone, zone.size() == 6 ? 4 : 3, 2);
    if (oh < 0 || om < 0 || oh > 23 || om > 59) return std::nullopt;
    offset = (oh * 3600 + om * 60) * (zone[0] == '-' ? -1 : 1);
  } else {
    return std::nullopt;
  }
  time::Civil c;
  c.year = year;
  c.month = month;
  c.day = day;
  c.hour = hour;
  c.minute = minute;
  c.second = second;
  return time::secondsFromCivil(c) - offset;
}

std::string formatInternetDateTime(time::Seconds t) {
  time::Civil c = time::civilFromSeconds(t);
  char buf[40];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", c.year, c.month, c.day, c.hour, c.minute, c.second);
  return buf;
}

}  // namespace marigold::sources
