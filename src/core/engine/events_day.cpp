#include "core/engine/events_day.hpp"

#include <algorithm>
#include <set>

#include "core/engine/replay.hpp"
#include "core/sports/board.hpp"
#include "core/sports/text.hpp"
#include "core/util/json.hpp"

namespace marigold::engine::events_day {
namespace {

namespace text = sports::text;

const char* const kLongDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kShortDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kDot = " \xc2\xb7 ";

// The local date in the middle of the day (never the day before across a
// clock change).
time::Civil civilOf(time::Seconds day, const time::Zone& zone) { return zone.toLocal(dayOf(day, zone) + 12 * 3600); }

std::string weekday(time::Seconds day, const time::Zone& zone, bool longName) {
  time::Civil c = civilOf(day, zone);
  int wd = time::weekday(c.year, c.month, c.day);
  if (wd < 0 || wd > 6) return "";
  return longName ? kLongDays[wd] : kShortDays[wd];
}

std::string monthDay(time::Seconds day, const time::Zone& zone) {
  time::Civil c = civilOf(day, zone);
  if (c.month < 1 || c.month > 12) return "";
  return std::string(kMonths[c.month - 1]) + " " + std::to_string(c.day);
}

// Days from today to `day` (negative: past).
int offset(time::Seconds day, time::Seconds now, const time::Zone& zone) {
  return text::dayDifference(dayOf(now, zone), dayOf(day, zone), zone);
}

// "Monday's", or "yesterday's", "tomorrow's".
std::string possessive(time::Seconds day, const time::Zone& zone) { return weekday(day, zone, true) + "'s"; }

}  // namespace

time::Seconds dayOf(time::Seconds t, const time::Zone& zone) { return text::startOfDay(t, zone); }

time::Seconds nextDay(time::Seconds day, const time::Zone& zone) {
  return text::startOfDay(text::addDays(dayOf(day, zone) + 12 * 3600, 1, zone), zone);
}

std::string dayWord(time::Seconds day, time::Seconds now, const time::Zone& zone) {
  switch (offset(day, now, zone)) {
    case -1: return "Yesterday";
    case 0: return "Today";
    case 1: return "Tomorrow";
    default: return weekday(day, zone, true);
  }
}

std::string dayDate(time::Seconds day, time::Seconds now, const time::Zone& zone) {
  int off = offset(day, now, zone);
  if (off >= -1 && off <= 1) return weekday(day, zone, false) + ", " + monthDay(day, zone);
  return monthDay(day, zone);
}

std::string dayTitle(time::Seconds day, const time::Zone& zone) {
  return weekday(day, zone, true) + ", " + monthDay(day, zone);
}

std::string loadingLine(time::Seconds day, const time::Zone& zone) {
  return "Getting " + possessive(day, zone) + " games\xe2\x80\xa6";
}

std::string failedLine(time::Seconds day, const time::Zone& zone) {
  return possessive(day, zone) + " games couldn't be loaded.";
}

std::string emptyLine(time::Seconds day, const time::Zone& zone) {
  return "No games on " + weekday(day, zone, true) + " in your sports.";
}

std::string introLine(time::Seconds day, bool past, const time::Zone& zone) {
  if (past) return possessive(day, zone) + " games. The ones you can watch from the start come first.";
  return possessive(day, zone) + " games, and where you can watch them.";
}

std::vector<DayRow> dayRows(time::Seconds now, const time::Zone& zone, uint16_t longestArchive,
                            const std::vector<time::Seconds>& starts, time::Seconds shown) {
  std::vector<DayRow> rows;
  const time::Seconds today = dayOf(now, zone);
  if (shown == 0) shown = today;
  auto row = [&](time::Seconds day) {
    DayRow r;
    r.day = day;
    r.word = dayWord(day, now, zone);
    r.date = dayDate(day, now, zone);
    r.today = day == today;
    r.past = day < today;
    r.shown = day == shown;
    return r;
  };
  // Past days, back while any part of one is still in the longest archive
  // (the day's end after where it begins), oldest first.
  if (longestArchive > 0) {
    const time::Seconds earliest = replay::archiveStart(now, longestArchive);
    std::vector<DayRow> past;
    time::Seconds day = today;
    for (int k = 1; k <= 31; ++k) {
      time::Seconds end = day;
      day = dayOf(text::addDays(day + 12 * 3600, -1, zone), zone);
      if (earliest >= end) break;
      DayRow r = row(day);
      r.replay = earliest <= day ? DayRow::Replay::All : DayRow::Replay::Some;
      past.push_back(std::move(r));
    }
    rows.insert(rows.end(), past.rbegin(), past.rend());
  }
  rows.push_back(row(today));
  // Days ahead the schedule has games for.
  time::Seconds day = today;
  for (int k = 1; k <= kDaysAhead; ++k) {
    day = nextDay(day, zone);
    time::Seconds end = nextDay(day, zone);
    bool any = std::any_of(starts.begin(), starts.end(), [&](time::Seconds s) { return s >= day && s < end; });
    if (any) rows.push_back(row(day));
  }
  return rows;
}

time::Seconds replayLeaves(time::Seconds from, uint16_t days) {
  if (days == 0) return from;
  // covers() holds while from >= archiveStart(now): archiveStart passes
  // `from` a minute after it reaches it.
  time::Seconds t = from + static_cast<time::Seconds>(days) * 86400 - 3600;
  while (replay::covers(days, from, t)) t += 60;
  while (t - 60 > from && !replay::covers(days, from, t - 60)) t -= 60;
  return t;
}

std::string replayEnds(time::Seconds leaves, time::Seconds now) {
  time::Seconds left = leaves - now;
  if (left <= 0 || left > kReplayEndsWithin) return "";
  if (left < 3600) return "replay ends in " + std::to_string(std::max<time::Seconds>(1, (left + 59) / 60)) + " min";
  return "replay ends in " + std::to_string(left / 3600) + " h";
}

std::string replayUntil(time::Seconds leaves, time::Seconds now, const time::Zone& zone, bool twelveHour) {
  std::string clock = sports::board::clockText(leaves, zone, twelveHour);
  int off = offset(leaves, now, zone);
  if (off <= 0) return "replay until " + clock;
  if (off == 1) return "replay until tomorrow " + clock;
  return "replay until " + weekday(leaves, zone, true) + " " + clock;
}

std::vector<sports::SportsEvent> pastEvents(
    const std::vector<sports::SportsGame>& games,
    const std::function<std::vector<sports::EventSource>(const sports::SportsGame&)>& known,
    const std::map<std::string, std::vector<sports::EventSource>>& network) {
  std::vector<sports::SportsEvent> out;
  out.reserve(games.size());
  for (const sports::SportsGame& g : games) {
    sports::SportsEvent e;
    e.id = "sev|" + g.id;
    e.title = g.title();
    e.league = g.league;
    e.leagueShort = g.leagueShort;
    e.start = g.start;
    e.game = g;
    if (known) e.sources = known(g);
    auto n = network.find(g.id);
    if (n != network.end()) {
      for (const sports::EventSource& s : n->second) {
        bool listed = std::any_of(e.sources.begin(), e.sources.end(),
                                  [&](const sports::EventSource& x) { return x.channelID == s.channelID; });
        if (!listed) e.sources.push_back(s);
      }
    }
    out.push_back(std::move(e));
  }
  return out;
}

void pastTile(EventTile& t, const sports::SportsEvent& event, const std::optional<game_replay::Pick>& pick,
              time::Seconds now, const time::Zone& zone, bool twelveHour) {
  const time::Seconds start = event.start.value_or(t.start);
  const std::string startText = start ? sports::board::clockText(start, zone, twelveHour) : std::string();
  // The hero: the matchup, then (after the Final pill) the score as the
  // settings show it and the venue.
  t.heroTitle = matchup(t);
  std::vector<std::string> status;
  if (t.sides.size() == 2 && !t.sides[0].score.empty() && !t.sides[1].score.empty()) {
    auto named = [](const EventSide& s) { return s.abbr.empty() ? s.name : s.abbr; };
    status.push_back(named(t.sides[0]) + " " + t.sides[0].score + " \xe2\x80\x93 " + named(t.sides[1]) + " " +
                     t.sides[1].score);
  }
  if (event.game && event.game->venue && !event.game->venue->empty()) status.push_back(*event.game->venue);
  std::string line;
  for (const std::string& s : status) line += (line.empty() ? "" : kDot) + s;
  t.heroStatus = line;
  t.venue.clear();
  t.footReplay = false;
  t.footWarn = false;
  if (pick) {
    t.dimmed = false;
    t.playsReplay = true;
    time::Seconds leaves = replayLeaves(pick->from, pick->days);
    std::string ends = replayEnds(leaves, now);
    t.footLeft = ends.empty() ? startText : ends;
    t.footWarn = !ends.empty();
    t.footRight = pick->channelName;
    t.footReplay = true;
    // "On ESPN, 5:15 – 8:31 PM · replay until tomorrow 5:15 PM": the
    // listing's end when the channel's listing had one.
    std::optional<time::Seconds> until;
    for (const sports::EventSource& s : event.sources)
      if (s.channelID == pick->channelId && s.until && *s.until > start) until = s.until;
    std::string when = startText;
    if (until && start) {
      bool sameHalf = twelveHour && (zone.toLocal(start).hour < 12) == (zone.toLocal(*until).hour < 12);
      when = (sameHalf ? sports::board::shortClockText(start, zone, twelveHour) : startText) + " \xe2\x80\x93 " +
             sports::board::clockText(*until, zone, twelveHour);
    }
    t.whereLine = "On " + pick->channelName + (when.empty() ? "" : ", " + when) + kDot +
                  replayUntil(leaves, now, zone, twelveHour);
    return;
  }
  t.dimmed = true;
  t.playsReplay = false;
  t.footLeft = startText;
  t.footRight = event.sources.empty() ? "Not on your channels" : "No replay";
  if (event.sources.empty()) t.whereLine = "Not on your channels";
  else if (!t.channelName.empty()) t.whereLine = "On " + t.channelName + (startText.empty() ? "" : ", " + startText) + kDot + "no replay";
  else t.whereLine = "No replay";
}

// ------------------------------------------------------------ ReplaySources

bool ReplaySources::note(const std::vector<sports::SportsEvent>& events, time::Seconds now) {
  bool changed = false;
  for (const sports::SportsEvent& e : events) {
    if (!e.game || !e.start || *e.start > now || *e.start < now - kKeptFor) continue;
    const time::Seconds start = *e.start;
    std::vector<sports::EventSource> listed;
    for (const sports::EventSource& s : e.sources) {
      // game_replay's (a): a guide channel whose listing has it at its start.
      if (s.kind != sports::EventSource::Kind::lineup && s.kind != sports::EventSource::Kind::guide) continue;
      if (!s.from || *s.from > start + game_replay::kListingSlack) continue;
      if (s.until && *s.until <= start) continue;
      listed.push_back(s);
      if (listed.size() >= kPerGame) break;
    }
    if (listed.empty()) continue;
    Seen& seen = games_[e.game->id];
    if (seen.start == start && seen.sources == listed) continue;
    seen.start = start;
    seen.sources = std::move(listed);
    changed = true;
  }
  size_t before = games_.size();
  trim(now);
  return changed || games_.size() != before;
}

const std::vector<sports::EventSource>* ReplaySources::find(const std::string& gameId) const {
  auto it = games_.find(gameId);
  return it == games_.end() ? nullptr : &it->second.sources;
}

void ReplaySources::trim(time::Seconds now) {
  for (auto it = games_.begin(); it != games_.end();) {
    if (it->second.start < now - kKeptFor) it = games_.erase(it);
    else ++it;
  }
  if (games_.size() <= kMost) return;
  // The oldest go first.
  std::vector<std::pair<time::Seconds, std::string>> order;
  order.reserve(games_.size());
  for (const auto& [id, seen] : games_) order.emplace_back(seen.start, id);
  std::sort(order.begin(), order.end());
  for (size_t i = 0; i + kMost < order.size(); ++i) games_.erase(order[i].second);
}

std::string ReplaySources::toJson() const {
  json::Value root = json::Value::object();
  root.set("version", 1);
  json::Value games = json::Value::object();
  for (const auto& [id, seen] : games_) {
    json::Value g = json::Value::object();
    g.set("start", static_cast<int64_t>(seen.start));
    json::Value list = json::Value::array();
    for (const sports::EventSource& s : seen.sources) list.push(sports::toJson(s));
    g.set("sources", std::move(list));
    games.set(id, std::move(g));
  }
  root.set("games", std::move(games));
  return json::write(root);
}

ReplaySources ReplaySources::fromJson(std::string_view text, time::Seconds now) {
  ReplaySources out;
  auto v = json::parse(text);
  if (!v || (*v)["version"].asInt() != 1) return out;
  for (const auto& [id, g] : (*v)["games"].fields()) {
    Seen seen;
    seen.start = g["start"].asInt();
    for (const json::Value& s : g["sources"].items())
      if (auto src = sports::sourceFromJson(s)) seen.sources.push_back(std::move(*src));
    if (seen.start > 0 && !seen.sources.empty()) out.games_.emplace(id, std::move(seen));
  }
  out.trim(now);
  return out;
}

}  // namespace marigold::engine::events_day
