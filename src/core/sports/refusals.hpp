// A schedule or score source that refuses us: 429 ("too many requests") or
// 403 ("forbidden"). It is left alone for the rest of the viewer's day, or
// longer when it says so (Retry-After), so a TV never keeps knocking on a
// door that was shut. Sources are told apart by their site ("espn.com",
// "ncaa.com"), so one refusing doesn't stop the others.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/sports/leagues.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::refusals {

// The site a request goes to: "site.api.espn.com" and "site.web.api.espn.com"
// are both "espn.com".
std::string sourceOf(std::string_view url);
// The site a league's schedule and scores come from.
std::string sourceOf(const League& league);

// When to ask again after an answer: nullopt unless it was a refusal.
std::optional<time::Seconds> until(int status, int retryAfterSeconds, time::Seconds now, const time::Zone& zone);

}  // namespace marigold::sports::refusals
