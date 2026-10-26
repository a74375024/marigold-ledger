// Small string helpers shared by the core modules. UTF-8 throughout.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace marigold::str {

std::string lowerAscii(std::string_view s);
std::string upperAscii(std::string_view s);
// Swift's lowercased() / uppercased() for the scripts channel names use:
// ASCII, Latin-1, Latin Extended-A, Greek, Cyrillic and fullwidth Latin
// (simple one-to-one mappings, plus "ß" -> "SS"). Other text is kept.
std::string lower(std::string_view s);
std::string upper(std::string_view s);
std::string_view trim(std::string_view s);
// Swift's trimmingCharacters(in:): `.whitespaces` (Unicode spaces and tab),
// plus newlines when `newlines`, plus every code point in `extra` (UTF-8).
std::string_view trimUnicode(std::string_view s, bool newlines = false,
                             std::string_view extra = std::string_view());
bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);
bool iequals(std::string_view a, std::string_view b);         // ASCII case-insensitive
bool icontains(std::string_view haystack, std::string_view needle);  // ASCII case-insensitive
std::vector<std::string_view> split(std::string_view s, char sep, bool keepEmpty = false);
std::string join(const std::vector<std::string>& parts, std::string_view sep);
std::string replaceAll(std::string_view s, std::string_view from, std::string_view to);

// Strips accents from Latin letters ("Pacífico" -> "Pacifico", "Ñ" -> "N"),
// like Swift's folding(options: .diacriticInsensitive). Other text is kept.
std::string foldDiacritics(std::string_view s);
// 12345 -> "12,345".
std::string groupedThousands(long long n);

// Number of UTF-8 code points.
size_t utf8Length(std::string_view s);
// True when `s` is valid UTF-8.
bool utf8Valid(std::string_view s);

// 64-bit FNV-1a, for stable ids and cache keys.
uint64_t fnv1a64(std::string_view s);
std::string hex64(uint64_t v);

}  // namespace marigold::str
