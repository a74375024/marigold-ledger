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

}  // namespace

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

}  // namespace marigold::str
