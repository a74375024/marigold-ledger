// CSV cells for the Help exports.
#pragma once

#include <string>
#include <string_view>

namespace marigold::report {

// A CSV cell: always quoted, inner quotes doubled (the Swift `cell`).
inline std::string csvCell(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 2);
  out += '"';
  for (char c : value) {
    if (c == '"') out += '"';
    out += c;
  }
  out += '"';
  return out;
}

}  // namespace marigold::report
