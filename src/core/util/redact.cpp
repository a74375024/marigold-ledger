#include "core/util/redact.hpp"

#include <array>
#include <cctype>

namespace marigold {
namespace {

bool isSchemeChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.';
}

// A URL ends at whitespace, a quote or an angle bracket.
bool endsUrl(char c) {
  return std::isspace(static_cast<unsigned char>(c)) || c == '"' || c == '\'' || c == '<' ||
         c == '>' || c == '`';
}

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

constexpr std::array<std::string_view, 9> kSecretKeys = {
    "username", "password", "user", "pass", "passwd", "token", "key", "auth", "mac"};

// If a credential name followed by '=' ends at text[pos-1], returns its length.
size_t secretKeyBefore(std::string_view text, size_t eq) {
  for (std::string_view key : kSecretKeys) {
    if (eq < key.size()) continue;
    size_t start = eq - key.size();
    bool same = true;
    for (size_t i = 0; i < key.size() && same; ++i) same = lower(text[start + i]) == key[i];
    if (!same) continue;
    // The name must start a word: "monkey=" is not "key=".
    if (start > 0 && std::isalnum(static_cast<unsigned char>(text[start - 1]))) continue;
    return key.size();
  }
  return 0;
}

}  // namespace

std::string redact(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    // scheme://
    if (text.compare(i, 3, "://") == 0) {
      size_t start = out.size();
      while (start > 0 && isSchemeChar(out[start - 1])) --start;
      if (start < out.size()) {
        out.resize(start);
        out += "[link]";
        i += 3;
        while (i < text.size() && !endsUrl(text[i])) ++i;
        continue;
      }
    }
    if (text[i] == '=' && secretKeyBefore(text, i) > 0) {
      out += "=***";
      ++i;
      while (i < text.size() && text[i] != '&' && !endsUrl(text[i])) ++i;
      continue;
    }
    out += text[i];
    ++i;
  }
  return out;
}

bool containsSecret(std::string_view text) { return redact(text) != text; }

}  // namespace marigold
