#include "core/catalog/text.hpp"

#include <climits>

namespace marigold::catalog::text {

std::optional<int> toInt(std::string_view s) {
  if (s.empty()) return std::nullopt;
  size_t i = 0;
  bool negative = false;
  if (s[0] == '+' || s[0] == '-') {
    negative = s[0] == '-';
    i = 1;
  }
  if (i == s.size()) return std::nullopt;
  long long value = 0;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return std::nullopt;
    value = value * 10 + (s[i] - '0');
    if (value > INT_MAX) return std::nullopt;
  }
  return static_cast<int>(negative ? -value : value);
}

std::optional<std::string> firstMatch(std::string_view pattern, std::string_view text,
                                      bool caseInsensitive) {
  auto m = rx::first(pattern, text, caseInsensitive);
  if (!m) return std::nullopt;
  return std::string(m->group(0));
}

std::optional<std::string> firstGroup(std::string_view pattern, std::string_view text, size_t group,
                                      bool caseInsensitive) {
  auto m = rx::first(pattern, text, caseInsensitive);
  if (!m || !m->has(group)) return std::nullopt;
  return std::string(m->group(group));
}

namespace {

// UTF-8 sequence length at s[i] (1 for invalid bytes).
size_t seqLen(std::string_view s, size_t i) {
  unsigned char c = static_cast<unsigned char>(s[i]);
  size_t n = c < 0x80             ? 1
             : (c & 0xE0) == 0xC0 ? 2
             : (c & 0xF0) == 0xE0 ? 3
             : (c & 0xF8) == 0xF0 ? 4
                                  : 1;
  if (i + n > s.size()) return 1;
  return n;
}

bool wordChar(std::string_view ch) {
  if (ch.size() == 1) {
    char c = ch[0];
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\'';
  }
  if (ch == "\xE2\x80\x99") return true;  // ’
  return rx::test("^[\\p{L}\\p{N}\\p{M}]$", ch);
}

}  // namespace

std::string capitalized(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  bool inWord = false;
  size_t i = 0;
  while (i < s.size()) {
    size_t n = seqLen(s, i);
    std::string_view ch = s.substr(i, n);
    bool word = wordChar(ch);
    if (word && !inWord) {
      out += str::upper(ch);
    } else if (word) {
      out += str::lower(ch);
    } else {
      out += ch;
    }
    inWord = word;
    i += n;
  }
  return out;
}

int standardCompare(std::string_view a, std::string_view b) {
  std::string x = str::lower(str::foldDiacritics(a));
  std::string y = str::lower(str::foldDiacritics(b));
  size_t i = 0, j = 0;
  auto digit = [](char c) { return c >= '0' && c <= '9'; };
  while (i < x.size() && j < y.size()) {
    if (digit(x[i]) && digit(y[j])) {
      size_t si = i, sj = j;
      while (si < x.size() && x[si] == '0') ++si;
      while (sj < y.size() && y[sj] == '0') ++sj;
      size_t ei = si, ej = sj;
      while (ei < x.size() && digit(x[ei])) ++ei;
      while (ej < y.size() && digit(y[ej])) ++ej;
      if (ei - si != ej - sj) return ei - si < ej - sj ? -1 : 1;
      int c = x.compare(si, ei - si, y, sj, ej - sj);
      if (c != 0) return c < 0 ? -1 : 1;
      i = ei;
      j = ej;
      continue;
    }
    unsigned char ci = static_cast<unsigned char>(x[i]), cj = static_cast<unsigned char>(y[j]);
    if (ci != cj) return ci < cj ? -1 : 1;
    ++i;
    ++j;
  }
  if (i < x.size()) return 1;
  if (j < y.size()) return -1;
  return 0;
}

}  // namespace marigold::catalog::text
