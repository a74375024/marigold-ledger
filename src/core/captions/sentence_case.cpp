#include "core/captions/sentence_case.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace marigold::captions {
namespace {

const std::string_view kAcronyms[] = {"NFL", "NBA", "MLB", "NHL", "NCAA", "NASCAR", "ESPN", "CNN", "NBC", "CBS",
                                      "ABC", "FOX", "BBC", "UK",  "USA",  "TV",     "OK",   "MVP", "UFC", "WWE",
                                      "F1",  "TD",  "QB"};

// Latin-1 letters arrive as 0xC3 xx in UTF-8.
bool latinUpper(uint8_t b) { return b >= 0x80 && b <= 0x9E && b != 0x97; }
bool latinLower(uint8_t b) { return b >= 0x9F && b <= 0xBF && b != 0xB7; }

uint8_t at(const std::string& s, size_t i) { return i < s.size() ? static_cast<uint8_t>(s[i]) : 0; }

bool isDigit(uint8_t c) { return c >= '0' && c <= '9'; }

// Bytes of the letter at i (0 if none); `upper` says which case.
size_t letterAt(const std::string& s, size_t i, bool& upper) {
  uint8_t c = at(s, i);
  if (c >= 'A' && c <= 'Z') {
    upper = true;
    return 1;
  }
  if (c >= 'a' && c <= 'z') {
    upper = false;
    return 1;
  }
  if (c == 0xC3) {
    uint8_t d = at(s, i + 1);
    upper = latinUpper(d);
    if (upper || latinLower(d)) return 2;
  }
  return 0;
}

// Bytes of the apostrophe at i: ' or the typographic one.
size_t apostropheAt(const std::string& s, size_t i) {
  if (at(s, i) == '\'') return 1;
  if (at(s, i) == 0xE2 && at(s, i + 1) == 0x80 && at(s, i + 2) == 0x99) return 3;
  return 0;
}

size_t alnumAt(const std::string& s, size_t i) {
  bool upper = false;
  if (isDigit(at(s, i))) return 1;
  return letterAt(s, i, upper);
}

void appendLower(std::string& out, const std::string& s, size_t from, size_t to) {
  for (size_t i = from; i < to; ++i) {
    uint8_t c = at(s, i);
    if (c >= 'A' && c <= 'Z') {
      out += static_cast<char>(c + 32);
    } else if (c == 0xC3 && i + 1 < to && latinUpper(at(s, i + 1))) {
      out += static_cast<char>(c);
      out += static_cast<char>(at(s, ++i) + 0x20);
    } else {
      out += static_cast<char>(c);
    }
  }
}

// Capitalises the first letter of out[from..].
void capitaliseFirst(std::string& out, size_t from) {
  for (size_t i = from; i < out.size(); ++i) {
    uint8_t c = at(out, i);
    if (c >= 'a' && c <= 'z') {
      out[i] = static_cast<char>(c - 32);
      return;
    }
    if (c >= 'A' && c <= 'Z') return;
    if (c == 0xC3 && i + 1 < out.size()) {
      uint8_t d = at(out, i + 1);
      // ß and ÿ have no Latin-1 capital.
      if (latinLower(d) && d != 0x9F && d != 0xBF) out[i + 1] = static_cast<char>(d - 0x20);
      if (latinLower(d) || latinUpper(d)) return;
    }
  }
}

bool isAcronym(std::string_view key) {
  for (std::string_view a : kAcronyms) {
    if (key == a) return true;
  }
  return false;
}

// "U.S", "A.M": single letters joined by dots (the last dot isn't in the word).
bool isDotted(std::string_view key) {
  if (key.size() < 3) return false;
  for (size_t i = 0; i < key.size(); ++i) {
    bool letter = key[i] >= 'A' && key[i] <= 'Z';
    if (i % 2 == 0 ? !letter : key[i] != '.') return false;
  }
  return key.size() % 2 == 1;
}

}  // namespace

std::string sentenceCase(const std::string& line, bool startsSentence) {
  size_t upperCount = 0, lowerCount = 0;
  for (size_t i = 0; i < line.size();) {
    bool upper = false;
    size_t n = letterAt(line, i, upper);
    if (n == 0) {
      ++i;
      continue;
    }
    ++(upper ? upperCount : lowerCount);
    i += n;
  }
  if (upperCount == 0 || lowerCount * 4 > upperCount) return line;

  std::string out;
  out.reserve(line.size());
  bool capNext = startsSentence;
  bool afterDotted = false;  // a dotted abbreviation's own dot ends no sentence
  bool prevDigits = false;
  bool prevThe = false;
  // Words are short; only these first bytes are looked at for the rules.
  char keyBuf[24];
  size_t i = 0;
  while (i < line.size()) {
    size_t n = alnumAt(line, i);
    if (n == 0) n = apostropheAt(line, i);
    if (n == 0) {
      uint8_t c = at(line, i);
      if (c == '!' || c == '?' || (c == '.' && !afterDotted)) capNext = true;
      if (c == '>' && at(line, i + 1) == '>') {
        capNext = true;  // a speaker change
        out += ">>";
        i += 2;
      } else {
        out += static_cast<char>(c);
        ++i;
      }
      afterDotted = false;
      continue;
    }
    // A word: letters, digits, apostrophes, and dots between letters ("U.S").
    size_t start = i;
    bool digits = false, letters = false;
    size_t keyLen = 0;
    bool keyFits = true;
    while (i < line.size()) {
      size_t k = alnumAt(line, i);
      size_t a = k ? 0 : apostropheAt(line, i);
      bool dot = !k && !a && at(line, i) == '.' && i > start && alnumAt(line, i + 1) > 0;
      if (!k && !a && !dot) break;
      uint8_t c = at(line, i);
      digits = digits || isDigit(c);
      letters = letters || (k && !isDigit(c));
      char keyChar = a ? '\'' : static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
      size_t step = k ? k : a ? a : 1;
      if (keyLen + step < sizeof keyBuf) {
        keyBuf[keyLen++] = keyChar;
        for (size_t j = 1; j < k; ++j) keyBuf[keyLen++] = static_cast<char>(at(line, i + j));
      } else {
        keyFits = false;
      }
      i += step;
    }
    std::string_view key(keyBuf, keyFits ? keyLen : 0);
    size_t from = out.size();
    if (digits) {
      out.append(line, start, i - start);  // scores, times, "49ERS", "3RD"
    } else if (!letters) {
      out.append(line, start, i - start);  // a lone quote mark
    } else if (key == "I" || key == "I'M" || key == "I'LL" || key == "I'VE" || key == "I'D") {
      out += 'I';
      appendLower(out, line, start + 1, i);
    } else if (isDotted(key)) {
      out.append(line, start, i - start);
      afterDotted = true;
    } else if (isAcronym(key) || ((key == "AM" || key == "PM") && prevDigits) || (key == "US" && prevThe)) {
      out.append(line, start, i - start);
    } else if (key.size() > 2 && key.substr(key.size() - 2) == "'S" && isAcronym(key.substr(0, key.size() - 2))) {
      // "NFL'S" -> "NFL's"
      out.append(line, start, i - 1 - start);
      out += 's';
    } else {
      appendLower(out, line, start, i);
      if (capNext) capitaliseFirst(out, from);
    }
    if (digits || letters) {
      capNext = false;
      prevDigits = digits;
      prevThe = key == "THE";
    }
    if (!isDotted(key)) afterDotted = false;
  }
  return out;
}

}  // namespace marigold::captions
