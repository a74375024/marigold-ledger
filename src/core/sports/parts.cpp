#include "core/sports/parts.hpp"

#include <algorithm>
#include <unordered_map>

namespace marigold::sports::parts {

namespace {

std::optional<LivePhase> phaseOf(const SportsEvent& part, const scores::Statuses& statuses) {
  if (!part.game) return std::nullopt;
  auto it = statuses.find(part.game->id);
  if (it == statuses.end()) return std::nullopt;
  return it->second.phase;
}

bool isLivePhase(std::optional<LivePhase> p) { return p == LivePhase::live || p == LivePhase::delayed; }

// A race weekend: the session on now, else the next one not over (by its
// status, else its usual length), else the last.
size_t raceCurrent(const std::vector<SportsEvent>& parts, Seconds now, const scores::Statuses& statuses) {
  // Past its usual end (plus half an hour): a status nobody refreshed while
  // it ran doesn't hold it.
  auto over = [&](const SportsEvent& p) { return p.start && now > *p.start + p.expectedLength() + 30 * 60; };
  auto stale = [&](const SportsEvent& p) { return p.start && now > *p.start + 12 * 3600; };
  for (size_t i = 0; i < parts.size(); ++i) {
    const SportsEvent& p = parts[i];
    if ((p.live == LivePhase::live || isLivePhase(phaseOf(p, statuses))) && !stale(p)) return i;
  }
  for (size_t i = 0; i < parts.size(); ++i) {
    const SportsEvent& p = parts[i];
    auto value = phaseOf(p, statuses);
    if (value == LivePhase::final || value == LivePhase::postponed || (p.game && p.game->completed)) continue;
    if ((!value || value == LivePhase::pre) && over(p)) continue;
    if (isLivePhase(value) && stale(p)) continue;
    return i;
  }
  return parts.size() - 1;
}

// Its guide listings say it ended: every channel's listing with an end is
// over (and there is one).
bool listingsOver(const SportsEvent& part, Seconds now) {
  bool any = false;
  for (const EventSource& s : part.sources) {
    if (s.kind == EventSource::Kind::provider || !s.until) continue;
    if (*s.until > now) return false;
    any = true;
  }
  return any;
}

bool fight(const SportsEvent& event) {
  return event.league == "Fighting" || (event.game && event.game->league == "Fighting");
}

}  // namespace

std::string kind(const SportsEvent& part) {
  if (!part.game || !part.game->cardKey) return "";
  size_t cut = part.game->cardKey->rfind('|');
  return cut == std::string::npos ? "" : part.game->cardKey->substr(cut + 1);
}

std::optional<size_t> onNow(const std::vector<SportsEvent>& parts, Seconds now, const scores::Statuses& statuses) {
  if (parts.empty()) return std::nullopt;
  if (parts.front().game && parts.front().game->isRacing()) return raceCurrent(parts, now, statuses);
  const size_t n = parts.size();
  std::vector<std::optional<LivePhase>> phases(n);
  for (size_t i = 0; i < n; ++i) phases[i] = phaseOf(parts[i], statuses);
  // Being played, by the scoreboard: the latest such part.
  for (size_t i = n; i-- > 0;) {
    if (isLivePhase(phases[i])) return i;
  }
  // The first part not over; a part that's final moves to the next.
  auto over = [&](size_t i) {
    return phases[i] == LivePhase::final || phases[i] == LivePhase::postponed ||
           (parts[i].game && parts[i].game->completed);
  };
  size_t current = 0;
  while (current < n && over(current)) ++current;
  if (current == n) return n - 1;
  // Then by the clock: a later part once it started and the one on before
  // it had its grace to finish (a card runs late), none when that one is
  // over (its status, or its listings ended).
  for (size_t next = current + 1; next < n; ++next) {
    if (!parts[next].start) continue;
    if (phases[next] == LivePhase::pre) break;  // the scoreboard: not started
    bool done = over(current) || listingsOver(parts[current], now);
    if (now < *parts[next].start + (done ? 0 : kGrace)) break;
    current = next;
  }
  // Between parts: the next one.
  while (current + 1 < n && over(current)) ++current;
  return current;
}

int tierOf(size_t part, size_t current) {
  if (part >= current) return static_cast<int>(part - current);
  return kOver + static_cast<int>(current - part);
}

std::vector<int> sourceTiers(const SportsEvent& event, const std::vector<SportsEvent>* parts, Seconds now,
                             const scores::Statuses& statuses, std::optional<size_t>* current) {
  std::vector<int> out(event.sources.size(), 0);
  if (current) current->reset();
  if (parts && !parts->empty()) {
    std::optional<size_t> on = onNow(*parts, now, statuses);
    if (current) *current = on;
    if (!on) return out;
    // A source in several parts (a whole-night feed): its best.
    std::unordered_map<std::string, int> best;
    best.reserve(event.sources.size());
    for (size_t i = 0; i < parts->size(); ++i) {
      int tier = tierOf(i, *on);
      for (const EventSource& s : (*parts)[i].sources) {
        auto [it, added] = best.emplace(s.channelID, tier);
        if (!added && tier < it->second) it->second = tier;
      }
    }
    for (size_t i = 0; i < event.sources.size(); ++i) {
      auto it = best.find(event.sources[i].channelID);
      if (it != best.end()) out[i] = it->second;
    }
    return out;
  }
  if (!fight(event)) return out;
  // One part, slots at times far apart: each run of them a part, the one
  // on now by the clock as above.
  std::vector<Seconds> times;
  for (const EventSource& s : event.sources) {
    if (s.kind == EventSource::Kind::provider && s.from) times.push_back(*s.from);
  }
  if (times.size() < 2) return out;
  std::sort(times.begin(), times.end());
  std::vector<Seconds> starts = {times.front()};
  for (Seconds t : times) {
    if (t - starts.back() >= kSlotGap) starts.push_back(t);
  }
  if (starts.size() < 2) return out;
  size_t on = 0;
  for (size_t i = 1; i < starts.size() && now >= starts[i] + kGrace; ++i) on = i;
  for (size_t i = 0; i < event.sources.size(); ++i) {
    const EventSource& s = event.sources[i];
    if (s.kind != EventSource::Kind::provider || !s.from) continue;
    size_t part = static_cast<size_t>(std::upper_bound(starts.begin(), starts.end(), *s.from) - starts.begin()) - 1;
    out[i] = tierOf(part, on);
  }
  return out;
}

}  // namespace marigold::sports::parts
