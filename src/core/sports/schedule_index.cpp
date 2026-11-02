#include "core/sports/schedule_index.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <unordered_set>

#include "core/sports/matcher.hpp"
#include "core/util/strings.hpp"

namespace marigold::sports::schedule {

namespace {

// Club-form words that don't tell sides apart.
const std::unordered_set<std::string>& stopWords() {
  static const std::unordered_set<std::string> kWords = {
      "fc", "cf", "sc", "ac", "afc", "cd", "sd", "ud", "if", "ik", "bk", "ff", "fk", "nk", "kf", "sk", "aif",
      "bc", "hc", "club", "de", "del", "la", "le", "the", "and", "of", "vs", "v", "at"};
  return kWords;
}

// Every word of the smaller list is in the larger one.
bool covers(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  const std::vector<std::string>& small = a.size() <= b.size() ? a : b;
  const std::vector<std::string>& large = a.size() <= b.size() ? b : a;
  if (small.empty()) return false;
  return std::all_of(small.begin(), small.end(),
                     [&](const std::string& w) { return std::find(large.begin(), large.end(), w) != large.end(); });
}

bool coversAny(const std::vector<std::string>& query, const std::vector<std::vector<std::string>>& names) {
  return std::any_of(names.begin(), names.end(), [&](const std::vector<std::string>& n) { return covers(query, n); });
}

std::vector<std::vector<std::string>> namesOf(const SportsTeam& team) {
  std::vector<std::vector<std::string>> out;
  auto add = [&](const std::string& name) {
    std::vector<std::string> w = GameIndex::words(name);
    if (!w.empty() && std::find(out.begin(), out.end(), w) == out.end()) out.push_back(std::move(w));
  };
  add(team.name);
  // An abbreviation ("LAR") only names a side when it's a real word of three
  // letters or more.
  if (team.shortName && team.shortName->size() >= 3) add(*team.shortName);
  for (const std::string& alt : team.alternates) add(alt);
  return out;
}

}  // namespace

std::vector<std::string> GameIndex::words(std::string_view name) {
  std::vector<std::string> all, kept;
  const std::string normalized = matcher::normalized(name);  // the split's words point into it
  for (std::string_view w : str::split(normalized, ' ')) {
    if (w.empty()) continue;
    all.emplace_back(w);
    if (!stopWords().count(all.back())) kept.push_back(all.back());
  }
  return kept.empty() ? all : kept;
}

GameIndex::GameIndex(const std::vector<SportsGame>& games) {
  entries_.reserve(games.size());
  for (const SportsGame& g : games) {
    Entry e;
    e.game = {g.id, g.leagueShort, g.start, g.completed};
    bool oneSide = g.home.id == g.away.id || g.home.name == g.away.name;
    if (!oneSide) {
      e.home = namesOf(g.home);
      e.away = namesOf(g.away);
    }
    if (g.customTitle) e.title = words(*g.customTitle);
    else if (oneSide) e.title = words(g.home.name);
    uint32_t at = static_cast<uint32_t>(entries_.size());
    std::set<std::string> indexed;
    for (const auto* side : {&e.home, &e.away})
      for (const auto& name : *side) indexed.insert(name.begin(), name.end());
    indexed.insert(e.title.begin(), e.title.end());
    for (const std::string& w : indexed) byWord_[w].push_back(at);
    entries_.push_back(std::move(e));
  }
}

std::vector<uint32_t> GameIndex::candidates(const std::vector<std::string>& query) const {
  std::vector<uint32_t> out;
  for (const std::string& w : query) {
    auto it = byWord_.find(w);
    if (it != byWord_.end()) out.insert(out.end(), it->second.begin(), it->second.end());
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::optional<Scheduled> GameIndex::find(std::string_view a, std::string_view b, Seconds around, Seconds within) const {
  std::vector<std::string> wa = words(a), wb = words(b);
  if (wa.empty() || wb.empty()) return std::nullopt;
  std::optional<Scheduled> best;
  Seconds bestGap = 0;
  for (uint32_t i : candidates(wa)) {
    const Entry& e = entries_[i];
    Seconds gap = std::llabs(e.game.start - around);
    if (gap > within || e.home.empty() || e.away.empty()) continue;
    bool ok = (coversAny(wa, e.home) && coversAny(wb, e.away)) || (coversAny(wa, e.away) && coversAny(wb, e.home));
    if (ok && (!best || gap < bestGap)) {
      best = e.game;
      bestGap = gap;
    }
  }
  return best;
}

std::optional<Scheduled> GameIndex::findTitle(std::string_view title, Seconds around, Seconds within) const {
  std::vector<std::string> wt = words(title);
  if (wt.size() < 2) return std::nullopt;
  std::optional<Scheduled> best;
  Seconds bestGap = 0;
  for (uint32_t i : candidates(wt)) {
    const Entry& e = entries_[i];
    Seconds gap = std::llabs(e.game.start - around);
    if (gap > within || e.title.size() < 2) continue;
    if (covers(wt, e.title) && (!best || gap < bestGap)) {
      best = e.game;
      bestGap = gap;
    }
  }
  return best;
}

}  // namespace marigold::sports::schedule
