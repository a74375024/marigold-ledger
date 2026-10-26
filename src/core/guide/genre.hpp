// Sorts guide programmes into Movies / Sports / News / Kids (ProgramGenre in
// tvOS). The bits are the same as RawProgram::f and Program::genres. Guides
// tag programmes when they can (XMLTV <category>, Emby flags); the Xtream
// short guide has none, so titles are read as well ("NFL Football",
// "Eyewitness News").
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/channel.hpp"

namespace marigold::guide::genre {

constexpr int kMovie = 1;
constexpr int kSports = 2;
constexpr int kNews = 4;
constexpr int kKids = 8;
constexpr int kGenreBits = 15;

// From XMLTV categories (lower-cased, joined with spaces).
int flags(std::string_view categoriesJoined);

// From a programme's title (and episode title) when a guide gives no
// categories. Only sports and news can be told from titles.
int flagsForTitle(std::string_view title, const std::optional<std::string>& episode = std::nullopt);

// Fills in genre flags a programme doesn't have yet, from its title.
Program withTitleFlags(const Program& program);

// Genre tags from `other` for programmes in `list` that have none: guides for
// the same channel often differ only in which one tags programmes.
std::vector<Program> mergeFlags(const std::vector<Program>& list,
                                const std::vector<Program>& other);

}  // namespace marigold::guide::genre
