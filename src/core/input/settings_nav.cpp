#include "core/input/settings_nav.hpp"

#include <cstdlib>

namespace marigold::settings_nav {

std::string crumb(const std::vector<std::string>& names) {
  std::string out = "SETTINGS";
  for (const std::string& name : names) {
    if (name.empty()) continue;
    out += " \xe2\x80\xba ";
    for (char ch : name) out += ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
  }
  return out;
}

Back back(bool armed, size_t depth, bool inRows, size_t opened) {
  if (armed) return Back::Disarm;
  if (depth > 0 && depth == opened) return Back::Close;
  if (depth > 0) return Back::UpOneLevel;
  return inRows ? Back::ToCategories : Back::Close;
}

int refocus(const std::vector<std::string>& titles, int index, const std::string& title) {
  if (!title.empty()) {
    // The nearest row with that title (a list can hold a name twice).
    int best = -1;
    for (int i = 0; i < static_cast<int>(titles.size()); ++i) {
      if (titles[static_cast<size_t>(i)] != title) continue;
      if (best < 0 || std::abs(i - index) < std::abs(best - index)) best = i;
    }
    if (best >= 0) return best;
  }
  return index;
}

}  // namespace marigold::settings_nav
