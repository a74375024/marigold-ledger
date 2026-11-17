#include "core/sports/day_fetch.hpp"

#include <algorithm>

#include "core/sports/refusals.hpp"
#include "core/sports/text.hpp"
#include "core/util/log.hpp"

namespace marigold::sports::days {

bool byDate(const League& league) { return !league.cricket; }

std::string dateOf(Seconds day, const time::Zone& zone) {
  // The middle of the day: never the day before across a clock change.
  return text::yearMonthDay(text::startOfDay(day, zone) + 12 * 3600, zone);
}

sources::Request request(const League& league, std::string_view date, bool plainForm) {
  return schedule::scoreboardRequest(league, "dates=" + std::string(date), plainForm);
}

Answer parse(const League& league, std::string_view body, Seconds from, Seconds to) {
  Answer a;
  // parseLeague keeps [from, to]; a game at the next midnight is the next day's.
  a.games = schedule::parseLeague(body, league, from, to - 1);
  if (!a.games) return a;
  scores::Statuses all = scores::statuses(body, league);
  for (const SportsGame& g : a.games->games) {
    auto s = all.find(g.id);
    if (s != all.end()) a.statuses.emplace(s->first, std::move(s->second));
  }
  // The day's games only: no rosters kept.
  a.games->teams.clear();
  return a;
}

// ------------------------------------------------------------------ the fetch

bool DayFetch::begin(Seconds day, const std::vector<League>& active, const std::map<std::string, Seconds>& refusedUntil,
                     const std::map<std::string, std::string>& forms, Seconds now, const time::Zone& zone) {
  State s;
  s.day.day = text::startOfDay(day, zone);
  s.now = now;
  s.zone = zone;
  s.refused = refusedUntil;
  std::set<std::string> seen;
  for (const League& l : active) {
    if (!byDate(l) || !seen.insert(l.shortName).second) continue;
    s.leagues.push_back(l);
    auto form = forms.find(l.shortName);
    if (form != forms.end() && form->second == "plain") s.plainKnown.insert(l.shortName);
  }
  s.day.leagues = static_cast<int>(s.leagues.size());
  finished_.reset();
  state_ = std::move(s);
  if (state_->leagues.empty()) {
    finish();
    return false;
  }
  return true;
}

std::optional<DayFetch::Pending> DayFetch::nextRequest() {
  if (!state_ || state_->awaiting) return std::nullopt;
  State& s = *state_;
  while (s.index < s.leagues.size()) {
    const League& league = s.leagues[s.index];
    // A source that refused us waits until its time is up.
    auto refused = s.refused.find(refusals::sourceOf(league));
    if (refused != s.refused.end() && refused->second > s.now) {
      s.day.failed += 1;
      s.index += 1;
      continue;
    }
    Pending p;
    p.league = league;
    p.date = dateOf(s.day.day, s.zone);
    p.from = s.day.day;
    p.to = text::startOfDay(text::addDays(s.day.day + 12 * 3600, 1, s.zone), s.zone);
    p.request = request(league, p.date, s.retrying || s.plainKnown.count(league.shortName) > 0);
    p.waitMs = s.sent > 0 ? schedule::kRequestSpacingMs : 0;
    s.sent += 1;
    s.awaiting = true;
    s.current = p;
    return p;
  }
  finish();
  return std::nullopt;
}

void DayFetch::onResponse(int status, std::string_view body, int retryAfterSeconds) {
  if (!state_ || !state_->awaiting || !state_->current) return;
  Answer a;
  if (status == 200) a = parse(state_->current->league, body, state_->current->from, state_->current->to);
  onAnswer(status, std::move(a), retryAfterSeconds);
}

void DayFetch::onAnswer(int status, Answer answer, int retryAfterSeconds) {
  if (!state_ || !state_->awaiting || !state_->current) return;
  State& s = *state_;
  s.awaiting = false;
  const League league = s.current->league;
  // Refused: that source is left alone for the rest of the day (its other
  // leagues skipped), as the schedule does; the schedule is told.
  if (auto until = refusals::until(status, retryAfterSeconds, s.now, s.zone)) {
    std::string source = refusals::sourceOf(league);
    s.refused[source] = *until;
    refusals_[source] = *until;
    // The log names no host: the league whose source it was.
    MLOG_W("events", "live events: %s's day refused (HTTP %d): its source is left alone for the rest of the day",
           league.shortName.c_str(), status);
    s.retrying = false;
    s.day.failed += 1;
    s.index += 1;
    return;
  }
  // The long form refused as a bad request: the plain one next.
  if (status == 400 && !s.retrying && !s.plainKnown.count(league.shortName)) {
    s.retrying = true;
    return;
  }
  bool wasRetry = s.retrying;
  s.retrying = false;
  s.index += 1;
  // An NCAA page for a day without games is "not found".
  if (status == 404 && league.ncaaPage) {
    s.day.answered += 1;
    return;
  }
  if (status != 200 || !answer.games) {
    s.day.failed += 1;
    return;
  }
  if (wasRetry) plain_.insert(league.shortName);
  s.day.answered += 1;
  for (SportsGame& g : answer.games->games) s.day.games.push_back(std::move(g));
  for (auto& [id, st] : answer.statuses) s.day.statuses[id] = std::move(st);
}

void DayFetch::finish() {
  if (!state_) return;
  Day d = std::move(state_->day);
  d.fetchedAt = state_->now;
  // Each game once (a league asked in two forms, the all-soccer listing).
  std::set<std::string> ids;
  std::vector<SportsGame> games;
  games.reserve(d.games.size());
  for (SportsGame& g : d.games)
    if (ids.insert(g.id).second) games.push_back(std::move(g));
  std::stable_sort(games.begin(), games.end(), [](const SportsGame& a, const SportsGame& b) { return a.start < b.start; });
  d.games = std::move(games);
  state_.reset();
  finished_ = std::move(d);
}

std::optional<Day> DayFetch::takeFinished() {
  std::optional<Day> out = std::move(finished_);
  finished_.reset();
  return out;
}

std::map<std::string, Seconds> DayFetch::takeRefusals() {
  std::map<std::string, Seconds> out;
  out.swap(refusals_);
  return out;
}

std::set<std::string> DayFetch::takePlain() {
  std::set<std::string> out;
  out.swap(plain_);
  return out;
}

std::string DayFetch::note(const Day& day, const time::Zone& zone) {
  static const char* const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  time::Civil c = zone.toLocal(day.day + 12 * 3600);
  int wd = time::weekday(c.year, c.month, c.day);
  std::string name = wd >= 0 && wd < 7 ? kDays[wd] : "the day";
  std::string verdict = day.failedAll() ? "failed" : day.failed > 0 ? "fine, " + std::to_string(day.failed) + " failed"
                                                                        : "fine";
  return name + "'s games, " + std::to_string(day.leagues) + " leagues, " + verdict;
}

// ------------------------------------------------------------------ the cache

const Day* DayCache::find(Seconds day) const {
  for (const Day& d : days_)
    if (d.day == day) return &d;
  return nullptr;
}

bool DayCache::fresh(Seconds day, Seconds now) const {
  const Day* d = find(day);
  return d && !d->failedAll() && now >= d->fetchedAt && now - d->fetchedAt < kFreshFor;
}

bool DayCache::mayFetch(Seconds now) const {
  if (failures_ < 2) return true;
  constexpr size_t kSteps = sizeof(schedule::kBackoff) / sizeof(schedule::kBackoff[0]);
  Seconds wait = schedule::kBackoff[std::min(static_cast<size_t>(failures_ - 2), kSteps - 1)];
  return now < failedAt_ || now - failedAt_ >= wait;
}

void DayCache::put(Day day) {
  if (day.failedAll()) {
    failures_ += 1;
    failedAt_ = day.fetchedAt;
    // A day that came before stays; a failure is kept only to say so.
    if (const Day* had = find(day.day); had && !had->failedAll()) return;
  } else {
    failures_ = 0;
  }
  days_.erase(std::remove_if(days_.begin(), days_.end(), [&](const Day& d) { return d.day == day.day; }), days_.end());
  days_.push_back(std::move(day));
  while (days_.size() > kKept) days_.erase(days_.begin());
}

}  // namespace marigold::sports::days
