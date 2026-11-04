#include "core/net/http.hpp"

#include <cctype>

#include "core/util/strings.hpp"

namespace marigold::http {

uint64_t wireLength(std::string_view block) {
  uint64_t length = 0;
  bool encoded = false;
  size_t pos = 0;
  while (pos < block.size()) {
    size_t end = block.find('\n', pos);
    if (end == std::string_view::npos) end = block.size();
    std::string_view line = str::trim(block.substr(pos, end - pos));
    pos = end + 1;
    if (str::startsWith(line, "HTTP/")) {
      // A new response (after a redirect or 100 Continue).
      length = 0;
      encoded = false;
      continue;
    }
    size_t colon = line.find(':');
    if (colon == std::string_view::npos) continue;
    std::string_view name = str::trim(line.substr(0, colon));
    std::string_view value = str::trim(line.substr(colon + 1));
    if (str::iequals(name, "Content-Length")) {
      uint64_t n = 0;
      bool digits = !value.empty();
      for (char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch)) || n > (UINT64_MAX - 9) / 10) {
          digits = false;
          break;
        }
        n = n * 10 + static_cast<uint64_t>(ch - '0');
      }
      length = digits ? n : 0;
    } else if (str::iequals(name, "Content-Encoding")) {
      encoded = !value.empty() && !str::iequals(value, "identity");
    }
  }
  return encoded ? 0 : length;
}

}  // namespace marigold::http
