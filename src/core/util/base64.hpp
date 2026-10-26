// Base64 (RFC 4648) decoding, for the Xtream short guide, whose titles and
// descriptions arrive base64-encoded.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace marigold::base64 {

// Strict decoding, like Foundation's Data(base64Encoded:) with no options:
// the standard alphabet only, no whitespace, length a multiple of 4, '='
// padding only at the end (at most two). nullopt for anything else. Empty
// input decodes to an empty string.
std::optional<std::string> decode(std::string_view text);

// Forgiving decoding for hand-made data: skips ASCII whitespace, accepts the
// URL-safe alphabet ('-' and '_') and missing padding. Still nullopt for
// other characters, data after padding, or a dangling single character.
std::optional<std::string> decodeLenient(std::string_view text);

// Standard alphabet with padding (tests and caches).
std::string encode(std::string_view bytes);

}  // namespace marigold::base64
