#include "core/sports/refusals.hpp"

#include <algorithm>

#include "core/sports/ncaa.hpp"
#include "core/sports/schedule.hpp"
#include "core/sports/text.hpp"

namespace marigold::sports::refusals {

std::string sourceOf(std::string_view url) {
  size_t start = url.find("://");
  start = start == std::string_view::npos ? 0 : start + 3;
  size_t end = url.find_first_of("/:?#", start);
  std::string host(url.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
  // The last two labels.
  size_t last = host.rfind('.');
  if (last == std::string::npos || last == 0) return host;
  size_t before = host.rfind('.', last - 1);
  return before == std::string::npos ? host : host.substr(before + 1);
}

std::string sourceOf(const League& league) {
  if (league.ncaaPage) return sourceOf(ncaa::pageUrl(league, "20260101"));
  return sourceOf(schedule::scoreboardRequest(league, "", false).url);
}

std::optional<time::Seconds> until(int status, int retryAfterSeconds, time::Seconds now, const time::Zone& zone) {
  if (status != 429 && status != 403) return std::nullopt;
  time::Seconds tomorrow = text::startOfDay(now, zone) + 86'400;
  return std::max(tomorrow, now + static_cast<time::Seconds>(std::max(0, retryAfterSeconds)));
}

}  // namespace marigold::sports::refusals
