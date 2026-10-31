#include "core/sports/game_banner.hpp"

#include <algorithm>

#include "core/sports/profiles.hpp"
#include "core/util/strings.hpp"

namespace marigold::sports::banner {

namespace {

constexpr const char* kDot = " \xC2\xB7 ";  // " · "
constexpr Seconds kSoonWindow = 3600;     // a guide channel's game starting within the hour

// "in 25 min", "in 1 h 20 min", "in 2 h", "in 9 h": a minute started counts.
std::string untilText(Seconds start, Seconds now) {
  int64_t minutes = std::max<int64_t>(1, (start - now + 59) / 60);
  if (minutes < 60) return "in " + std::to_string(minutes) + " min";
  int64_t h = minutes / 60, m = minutes % 60;
  if (h >= 3 || m == 0) return "in " + std::to_string(h + (h >= 3 && m >= 30 ? 1 : 0)) + " h";
  return "in " + std::to_string(h) + " h " + std::to_string(m) + " min";
}

// A race weekend session's kind ("race", "sprint", "qual") from its card key.
std::string sessionKind(const SportsGame& game) {
  if (!game.cardKey) return "";
  size_t cut = game.cardKey->rfind('|');
  return cut == std::string::npos ? "" : game.cardKey->substr(cut + 1);
}

}  // namespace

Status status(std::optional<Seconds> start, std::optional<Seconds> end, bool postponed, Seconds now) {
  if (postponed || !start) return {};
  if (now < *start) return {Chip::soon, untilText(*start, now)};
  if (!end || now < *end) return {Chip::live, "LIVE"};
  return {Chip::likelyOver, "Likely over"};
}

Status status(const SportsEvent& event, Seconds now) {
  return status(event.start, event.scheduledEnd(), event.game && event.game->postponed, now);
}

std::string line(const SportsEvent& event, const std::string& tag, const time::Zone& zone, bool twelveHour) {
  std::vector<std::string> parts;
  auto add = [&](std::string part) {
    part = std::string(str::trimUnicode(part));
    if (part.empty()) return;
    std::string upper = str::upper(part);
    for (const std::string& p : parts) {
      if (p == upper) return;
    }
    parts.push_back(std::move(upper));
  };
  std::string league = event.leagueShort.empty() ? event.league : event.leagueShort;
  add(league);
  const SportsGame* game = event.game ? &*event.game : nullptr;
  if (game && game->weekLabel) add(*game->weekLabel);
  // A tag that starts with the league ("NCAAF · Women's") doesn't repeat it.
  std::string rest = tag;
  std::string lead = str::upper(league) + kDot;
  if (!league.empty() && str::startsWith(str::upper(rest), lead)) rest = rest.substr(lead.size());
  add(rest);
  if (game && game->venue) add(*game->venue);
  if (game && game->isRacing() && event.start && str::upper(game->leagueShort) == "F1") {
    std::string kind = sessionKind(*game);
    if (kind == "race" || kind == "sprint") add("Lights out " + board::clockText(*event.start, zone, twelveHour));
  }
  return str::join(parts, kDot);
}

std::string onChannel(const board::Board& board, const std::vector<std::string>& entryIDs, Seconds now) {
  const SportsEvent* best = nullptr;
  int bestRank = 0;
  for (const auto& [id, e] : board.events) {
    if (str::startsWith(id, "whip|") || board.studio.count(id) || !e.start) continue;
    if (e.game && (e.game->postponed || e.game->completed)) continue;
    bool listed = std::any_of(e.sources.begin(), e.sources.end(), [&](const EventSource& s) {
      return s.kind != EventSource::Kind::provider &&
             std::find(entryIDs.begin(), entryIDs.end(), s.channelID) != entryIDs.end();
    });
    if (!listed) continue;
    // The clock alone, as the banner says it.
    Seconds start = *e.start, end = e.scheduledEnd().value_or(start);
    int rank;
    if (now >= start && now < end) {
      rank = 3;
    } else if (now >= end && now < end + overrunOf(e.league, e.game ? &*e.game : nullptr)) {
      rank = 2;
    } else if (now < start && start - now <= kSoonWindow) {
      rank = 1;
    } else {
      continue;
    }
    // Of two the same: the one that started last, or the next to start
    // (the first by id when they start together).
    bool better = !best || rank > bestRank ||
                  (rank == bestRank && (rank == 1 ? start < *best->start : start > *best->start));
    if (better) {
      best = &e;
      bestRank = rank;
    }
  }
  return best ? best->id : std::string();
}

}  // namespace marigold::sports::banner
