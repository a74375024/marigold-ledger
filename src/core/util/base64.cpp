#include "core/util/base64.hpp"

#include <cstdint>

namespace marigold::base64 {
namespace {

constexpr std::string_view kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 0..63 for a digit, -1 otherwise.
int digit(char c, bool urlSafe) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  if (urlSafe && c == '-') return 62;
  if (urlSafe && c == '_') return 63;
  return -1;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// Decodes digits (no padding, no whitespace). A group of 1 digit is invalid.
std::optional<std::string> decodeDigits(std::string_view digits, bool urlSafe) {
  if (digits.size() % 4 == 1) return std::nullopt;
  std::string out;
  out.reserve(digits.size() / 4 * 3 + 2);
  uint32_t acc = 0;
  int bits = 0;
  for (char c : digits) {
    int d = digit(c, urlSafe);
    if (d < 0) return std::nullopt;
    acc = (acc << 6) | static_cast<uint32_t>(d);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((acc >> bits) & 0xFF);
    }
    acc &= (1u << bits) - 1;  // keep only the bits not yet written
  }
  return out;
}

}  // namespace

std::optional<std::string> decode(std::string_view text) {
  if (text.size() % 4 != 0) return std::nullopt;
  size_t pad = 0;
  while (pad < 2 && pad < text.size() && text[text.size() - 1 - pad] == '=') ++pad;
  std::string_view digits = text.substr(0, text.size() - pad);
  // "=" anywhere else (a third pad, or inside) fails the alphabet check below.
  return decodeDigits(digits, false);
}

std::optional<std::string> decodeLenient(std::string_view text) {
  std::string digits;
  digits.reserve(text.size());
  size_t pad = 0;
  for (char c : text) {
    if (isSpace(c)) continue;
    if (c == '=') {
      ++pad;
      continue;
    }
    if (pad > 0) return std::nullopt;  // data after padding
    digits += c;
  }
  if (pad > 2) return std::nullopt;
  if (pad > 0 && (digits.size() + pad) % 4 != 0) return std::nullopt;
  return decodeDigits(digits, true);
}

std::string encode(std::string_view bytes) {
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  size_t i = 0;
  auto at = [&](size_t k) { return static_cast<uint32_t>(static_cast<unsigned char>(bytes[k])); };
  for (; i + 3 <= bytes.size(); i += 3) {
    uint32_t v = (at(i) << 16) | (at(i + 1) << 8) | at(i + 2);
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += kAlphabet[(v >> 6) & 63];
    out += kAlphabet[v & 63];
  }
  size_t rest = bytes.size() - i;
  if (rest == 1) {
    uint32_t v = at(i) << 16;
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += "==";
  } else if (rest == 2) {
    uint32_t v = (at(i) << 16) | (at(i + 1) << 8);
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += kAlphabet[(v >> 6) & 63];
    out += '=';
  }
  return out;
}

}  // namespace marigold::base64
