#include "core/util/strings.hpp"

#include <cstdint>
#include <cstdio>

namespace marigold::str {
namespace {

char lowerChar(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
char upperChar(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }
bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// Decodes one code point at s[i]; advances i. Invalid bytes decode as 0xFFFD.
uint32_t decode(std::string_view s, size_t& i) {
  auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
  unsigned char c = b(i);
  if (c < 0x80) {
    ++i;
    return c;
  }
  int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
  if (len == 0 || i + static_cast<size_t>(len) > s.size()) {
    ++i;
    return 0xFFFD;
  }
  uint32_t cp = c & (len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07);
  for (int k = 1; k < len; ++k) {
    unsigned char n = b(i + static_cast<size_t>(k));
    if ((n & 0xC0) != 0x80) {
      ++i;
      return 0xFFFD;
    }
    cp = (cp << 6) | (n & 0x3F);
  }
  i += static_cast<size_t>(len);
  return cp;
}

void encode(uint32_t cp, std::string& out) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Base letter for Latin-1 Supplement and Latin Extended-A, or 0 if none.
const char* baseLetter(uint32_t cp) {
  static const char* const kLatin1[64] = {
      "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
      "D", "N", "O", "O", "O", "O", "O", nullptr, "O", "U", "U", "U", "U", "Y", "TH", "ss",
      "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
      "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "y"};
  if (cp >= 0xC0 && cp <= 0xFF) return kLatin1[cp - 0xC0];
  if (cp >= 0x100 && cp <= 0x17F) {
    // Latin Extended-A, generated from Unicode decompositions (plus the
    // letters that have none: Đ, Ł, Œ...).
    static const char* const kExtA[128] = {"A", "a", "A", "a", "A", "a", "C", "c", "C", "c", "C", "c", "C", "c", "D", "d", "D", "d", "E", "e", "E", "e", "E", "e", "E", "e", "E", "e", "G", "g", "G", "g", "G", "g", "G", "g", "H", "h", "H", "h", "I", "i", "I", "i", "I", "i", "I", "i", "I", "i", "IJ", "ij", "J", "j", "K", "k", "k", "L", "l", "L", "l", "L", "l", "L", "l", "L", "l", "N", "n", "N", "n", "N", "n", "n", "N", "n", "O", "o", "O", "o", "O", "o", "OE", "oe", "R", "r", "R", "r", "R", "r", "S", "s", "S", "s", "S", "s", "S", "s", "T", "t", "T", "t", "T", "t", "U", "u", "U", "u", "U", "u", "U", "u", "U", "u", "U", "u", "W", "w", "Y", "y", "Y", "Z", "z", "Z", "z", "Z", "z", "s"};
    return kExtA[cp - 0x100];
  }
  return nullptr;
}

uint32_t lowerCode(uint32_t cp) {
  if (cp < 0x80) return cp >= 'A' && cp <= 'Z' ? cp + 32 : cp;
  if ((cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)) return cp + 32;
  if (cp >= 0x100 && cp <= 0x17F) {
    if (cp == 0x130) return 'i';  // İ (Swift gives i + combining dot; close enough)
    if (cp == 0x178) return 0xFF;
    bool oddUpper = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
    if (cp == 0x131 || cp == 0x138 || cp == 0x149 || cp == 0x17F) return cp;
    if (oddUpper) return (cp & 1) ? cp + 1 : cp;
    return (cp & 1) ? cp : cp + 1;
  }
  if (cp >= 0x391 && cp <= 0x3AB && cp != 0x3A2) return cp + 32;
  if (cp == 0x386) return 0x3AC;
  if (cp >= 0x388 && cp <= 0x38A) return cp + 37;
  if (cp == 0x38C) return 0x3CC;
  if (cp == 0x38E || cp == 0x38F) return cp + 63;
  if (cp >= 0x410 && cp <= 0x42F) return cp + 32;
  if (cp >= 0x400 && cp <= 0x40F) return cp + 80;
  if (cp >= 0x460 && cp <= 0x4FF && cp != 0x482 && !(cp >= 0x483 && cp <= 0x489) && cp < 0x4C0) {
    return (cp & 1) ? cp : cp + 1;
  }
  if (cp >= 0xFF21 && cp <= 0xFF3A) return cp + 32;
  return cp;
}

uint32_t upperCode(uint32_t cp) {
  if (cp < 0x80) return cp >= 'a' && cp <= 'z' ? cp - 32 : cp;
  if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 32;
  if (cp == 0xFF) return 0x178;
  if (cp == 0xB5) return 0x39C;
  if (cp >= 0x100 && cp <= 0x17F) {
    if (cp == 0x131) return 'I';
    if (cp == 0x17F) return 'S';
    if (cp == 0x130 || cp == 0x138 || cp == 0x149 || cp == 0x178) return cp;
    bool oddUpper = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
    if (oddUpper) return (cp & 1) ? cp : cp - 1;
    return (cp & 1) ? cp - 1 : cp;
  }
  if (cp >= 0x3B1 && cp <= 0x3CB && cp != 0x3C2) return cp - 32;
  if (cp == 0x3C2) return 0x3A3;
  if (cp == 0x3AC) return 0x386;
  if (cp >= 0x3AD && cp <= 0x3AF) return cp - 37;
  if (cp == 0x3CC) return 0x38C;
  if (cp == 0x3CD || cp == 0x3CE) return cp - 63;
  if (cp >= 0x430 && cp <= 0x44F) return cp - 32;
  if (cp >= 0x450 && cp <= 0x45F) return cp - 80;
  if (cp >= 0x460 && cp < 0x4C0 && !(cp >= 0x482 && cp <= 0x489)) return (cp & 1) ? cp - 1 : cp;
  if (cp >= 0xFF41 && cp <= 0xFF5A) return cp - 32;
  return cp;
}

template <typename Map>
std::string mapCase(std::string_view s, Map map, bool upperSharpS) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      out += static_cast<char>(map(c));
      ++i;
      continue;
    }
    size_t before = i;
    uint32_t cp = decode(s, i);
    if (cp == 0xFFFD) {
      out.append(s.substr(before, i - before));  // keep invalid bytes as they are
    } else if (upperSharpS && cp == 0xDF) {
      out += "SS";
    } else {
      encode(map(cp), out);
    }
  }
  return out;
}

bool unicodeSpace(uint32_t cp) {
  return cp == ' ' || cp == '\t' || cp == 0xA0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) ||
         cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

bool unicodeNewline(uint32_t cp) {
  return cp == '\n' || cp == '\r' || cp == 0x0B || cp == 0x0C || cp == 0x85 || cp == 0x2028 ||
         cp == 0x2029;
}

// Code point ending at s[end) (end > 0), and where it starts.
uint32_t decodeBack(std::string_view s, size_t end, size_t& start) {
  start = end - 1;
  while (start > 0 && end - start < 4 && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) --start;
  size_t i = start;
  uint32_t cp = decode(s, i);
  if (i != end) {
    start = end - 1;
    return 0xFFFD;
  }
  return cp;
}

}  // namespace

std::string lower(std::string_view s) { return mapCase(s, lowerCode, false); }
std::string upper(std::string_view s) { return mapCase(s, upperCode, true); }

std::string_view trimUnicode(std::string_view s, bool newlines, std::string_view extra) {
  auto inSet = [&](uint32_t cp) {
    if (unicodeSpace(cp) || (newlines && unicodeNewline(cp))) return true;
    size_t k = 0;
    while (k < extra.size()) {
      if (decode(extra, k) == cp) return true;
    }
    return false;
  };
  size_t a = 0;
  while (a < s.size()) {
    size_t next = a;
    uint32_t cp = decode(s, next);
    if (!inSet(cp)) break;
    a = next;
  }
  size_t b = s.size();
  while (b > a) {
    size_t start = 0;
    uint32_t cp = decodeBack(s, b, start);
    if (start < a || !inSet(cp)) break;
    b = start;
  }
  return s.substr(a, b - a);
}

std::string lowerAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = lowerChar(c);
  return out;
}

std::string upperAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = upperChar(c);
  return out;
}

std::string_view trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && space(s[a])) ++a;
  while (b > a && space(s[b - 1])) --b;
  return s.substr(a, b - a);
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p) {
  return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (lowerChar(a[i]) != lowerChar(b[i])) return false;
  }
  return true;
}

bool icontains(std::string_view h, std::string_view n) {
  if (n.empty()) return true;
  if (n.size() > h.size()) return false;
  for (size_t i = 0; i + n.size() <= h.size(); ++i) {
    if (iequals(h.substr(i, n.size()), n)) return true;
  }
  return false;
}

std::vector<std::string_view> split(std::string_view s, char sep, bool keepEmpty) {
  std::vector<std::string_view> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t end = s.find(sep, start);
    if (end == std::string_view::npos) end = s.size();
    std::string_view part = s.substr(start, end - start);
    if (keepEmpty || !part.empty()) out.push_back(part);
    start = end + 1;
  }
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::string replaceAll(std::string_view s, std::string_view from, std::string_view to) {
  if (from.empty()) return std::string(s);
  std::string out;
  size_t start = 0, pos;
  while ((pos = s.find(from, start)) != std::string_view::npos) {
    out.append(s.substr(start, pos - start));
    out.append(to);
    start = pos + from.size();
  }
  out.append(s.substr(start));
  return out;
}

std::string foldDiacritics(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    size_t before = i;
    uint32_t cp = decode(s, i);
    if (cp >= 0x300 && cp <= 0x36F) continue;  // combining marks
    const char* base = cp >= 0xC0 ? baseLetter(cp) : nullptr;
    if (base) out += base;
    else if (cp == 0xFFFD) out.append(s.substr(before, i - before));
    else encode(cp, out);
  }
  return out;
}

size_t utf8Length(std::string_view s) {
  size_t n = 0, i = 0;
  while (i < s.size()) {
    decode(s, i);
    ++n;
  }
  return n;
}

bool utf8Valid(std::string_view s) {
  size_t i = 0;
  while (i < s.size()) {
    size_t before = i;
    if (decode(s, i) == 0xFFFD) {
      // A literal U+FFFD (EF BF BD) is valid; a decoding failure is not.
      if (s.substr(before, 3) != "\xEF\xBF\xBD") return false;
    }
  }
  return true;
}

uint64_t fnv1a64(std::string_view s) {
  uint64_t h = 1469598103934665603ull;
  for (char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ull;
  }
  return h;
}

std::string hex64(uint64_t v) {
  char buf[17];
  std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
  return buf;
}

std::string groupedThousands(long long n) {
  bool negative = n < 0;
  unsigned long long v = negative ? 0ull - static_cast<unsigned long long>(n) : static_cast<unsigned long long>(n);
  std::string digits = std::to_string(v), out;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return negative ? "-" + out : out;
}

}  // namespace marigold::str
