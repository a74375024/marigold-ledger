// Synthetic channels for the catalog tests.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/model/channel.hpp"
#include "core/util/time.hpp"

namespace testdata {

inline marigold::Channel channel(const std::string& id, const std::string& name,
                                 std::optional<std::string> group = std::nullopt,
                                 const std::string& server = "xt:1") {
  marigold::Channel c;
  c.id = server + "|" + id;
  c.serverId = server;
  c.itemId = id;
  c.name = name;
  c.group = std::move(group);
  return c;
}

// 2026-10-21 12:00 UTC, a Wednesday (08:00 in New York, 13:00 in London).
inline marigold::time::Seconds wednesdayNoon() {
  return marigold::time::secondsFromCivil({2026, 10, 21, 12, 0, 0});
}

inline marigold::time::Seconds utc(int y, int mo, int d, int h, int mi) {
  return marigold::time::secondsFromCivil({y, mo, d, h, mi, 0});
}

}  // namespace testdata
