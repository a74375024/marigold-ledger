// Synthetic XMLTV files for the guide tests.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/util/time.hpp"

namespace xmltvdata {

// "YYYYMMDDHHMMSS +0000" for minutes since 1970.
inline std::string stamp(int minutes, const std::string& offset = "+0000") {
  marigold::time::Civil c =
      marigold::time::civilFromSeconds(static_cast<marigold::time::Seconds>(minutes) * 60);
  auto two = [](int n) { return (n < 10 ? "0" : "") + std::to_string(n); };
  return std::to_string(c.year) + two(c.month) + two(c.day) + two(c.hour) + two(c.minute) + "00 " +
         offset;
}

struct Builder {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tv generator-info-name=\"test\">\n";

  Builder& channel(const std::string& id, const std::vector<std::string>& names) {
    xml += "  <channel id=\"" + id + "\">";
    for (const std::string& n : names) xml += "<display-name lang=\"en\">" + n + "</display-name>";
    xml += "<icon src=\"https://example.invalid/i.png\"/></channel>\n";
    return *this;
  }
  Builder& programme(const std::string& channelId, int start, int minutes, const std::string& title,
                     const std::string& inner = "") {
    xml += "  <programme start=\"" + stamp(start) + "\" stop=\"" + stamp(start + minutes) +
           "\" channel=\"" + channelId + "\"><title lang=\"en\">" + title + "</title>" + inner +
           "</programme>\n";
    return *this;
  }
  std::string done() const { return xml + "</tv>\n"; }
};

}  // namespace xmltvdata
