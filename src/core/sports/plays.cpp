#include "core/sports/plays.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_set>

#include "core/sports/refusals.hpp"
#include "core/sports/schedule.hpp"
#include "core/sports/text.hpp"
#include "core/util/json_stream.hpp"

namespace marigold::sports::plays {
namespace {

// A drive, or a play, is small; a drive of plays a few tens of KB.
constexpr size_t kMaxElementBytes = 1u << 20;

bool has(const std::string& s, const char* word) { return s.find(word) != std::string::npos; }

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

std::optional<int> intOf(const json::Value& v) {
  if (v.isNumber()) return static_cast<int>(std::lround(v.asNumber()));
  auto t = text::text(v);
  if (!t) return std::nullopt;
  return text::toInt(*t);
}

// One play's fields; false when it has no wall clock.
bool readPlay(const json::Value& p, Play& out, std::string& id) {
  if (!p.isObject()) return false;
  id = text::text(p["id"]).value_or("");
  const json::Value& period = p["period"];
  out.period = (period.isObject() ? intOf(period["number"]) : intOf(period)).value_or(0);
  const json::Value& type = p["type"];
  std::string kind = type.isObject() ? text::text(type["text"]).value_or("") : text::text(type).value_or("");
  out.kind = kindOf(kind, text::text(p["text"]).value_or(""), text::flag(p["scoringPlay"]));
  std::optional<Seconds> at = text::date(text::text(p["wallclock"]));
  if (!at || *at <= 0) return false;
  out.at = *at;
  return true;
}

// Reads the array at `path`, each element a play (or, `nested`, a drive
// with its plays). False when the body has no such array.
bool readArray(std::string_view body, std::vector<std::string> path, bool nested, Parsed& out,
               std::unordered_set<std::string>& ids) {
  auto one = [&](const json::Value& p) {
    Play play;
    std::string id;
    bool timed = readPlay(p, play, id);
    if (!id.empty() && !ids.insert(id).second) return;  // the same play twice
    ++out.seen;
    if (timed) out.plays.push_back(play);
  };
  json::ArrayLimits limits;
  limits.maxElementBytes = kMaxElementBytes;
  json::ArrayReader reader(
      [&](json::Value&& element) {
        if (!nested) {
          one(element);
          return true;
        }
        for (const json::Value& p : element["plays"].items()) one(p);
        return true;
      },
      std::move(path), limits);
  if (!reader.feed(body) || !reader.finish()) return false;
  return reader.foundArray();
}

bool halftimeAt(const Play& p, const Rules& r) {
  return p.kind == Kind::HalfEnd || (p.kind == Kind::PeriodEnd && r.halftimeAfter > 0 && p.period == r.halftimeAfter);
}

// A play after which a gap of Rules::gap is a break.
bool breakKind(Kind k) { return k != Kind::Plain && k != Kind::GameEnd; }

}  // namespace

std::optional<Rules> rulesFor(const League& league) {
  if (league.ncaaPage || league.cricket || league.racing || league.tennis || league.golf || league.fights)
    return std::nullopt;
  const std::string& path = league.path;
  if (startsWith(path, "football/")) return kFootball;
  if (path == "basketball/mens-college-basketball") return kCollegeHalves;
  if (startsWith(path, "basketball/")) return kBasketball;
  if (startsWith(path, "hockey/")) return kHockey;
  return std::nullopt;
}

bool byDrive(const League& league) { return league.path.rfind("football/", 0) == 0; }

Kind kindOf(std::string_view type, std::string_view text, bool scoring) {
  std::string t = str::lower(type);
  std::string x = str::lower(text);
  auto either = [&](const char* w) { return has(t, w) || has(x, w); };
  if (has(t, "end of game") || has(t, "end game")) return Kind::GameEnd;
  if (either("two-minute warning") || either("two minute warning") || either("2-minute warning") ||
      either("2 minute warning"))
    return Kind::Warning;
  // The end of a half or a period: by its type, or by its text when it
  // has none.
  const std::string& e = t.empty() ? x : t;
  if (has(e, "halftime") || has(e, "end of half") || has(e, "end half") || has(e, "end of 1st half") ||
      has(e, "end of first half"))
    return Kind::HalfEnd;
  if (has(e, "end period") || has(e, "end of period") || has(e, "end of quarter") || has(e, "end quarter") ||
      has(e, "end of regulation") || has(e, "end of the ") || has(e, "end of 1st") || has(e, "end of 2nd") ||
      has(e, "end of 3rd") || has(e, "end of 4th"))
    return Kind::PeriodEnd;
  if (either("timeout")) return Kind::Timeout;
  if (either("injur") || either("review") || either("challenge") || has(t, "replay") || has(t, "stoppage"))
    return Kind::Stoppage;
  if (scoring) return Kind::Score;
  return Kind::Plain;
}

std::vector<Break> detect(std::vector<Play> plays, const Rules& rules, bool live, Seconds asOf) {
  std::stable_sort(plays.begin(), plays.end(), [](const Play& a, const Play& b) { return a.at < b.at; });
  std::vector<Break> out;
  auto add = [&](Break k) {
    // Only one play between two breaks (a second timeout, the play that
    // ends a period): one break.
    if (!out.empty() && k.from - out.back().to <= 2 * rules.trim) {
      out.back().to = k.to;
      out.back().open = k.open;
      out.back().halftime = out.back().halftime || k.halftime;
      return;
    }
    out.push_back(k);
  };
  for (size_t i = 0; i + 1 < plays.size(); ++i) {
    const Play& a = plays[i];
    const Play& b = plays[i + 1];
    Seconds gap = b.at - a.at;
    if (!((breakKind(a.kind) && gap >= rules.gap) || gap >= rules.anyGap)) continue;
    Break k;
    k.from = a.at + rules.trim;
    k.to = b.at - rules.trim;
    k.halftime = halftimeAt(a, rules);
    if (k.length() < rules.shortest) continue;
    add(k);
  }
  // Still on: what the last play began, as long as it was already a break
  // when the plays were read, runs to live.
  if (live && !plays.empty() && asOf > 0) {
    const Play& last = plays.back();
    Seconds since = asOf - last.at;
    if (last.kind != Kind::GameEnd && ((breakKind(last.kind) && since >= rules.gap) || since >= rules.anyGap)) {
      Break k;
      k.from = last.at + rules.trim;
      k.to = asOf;  // lasted at least this long; runs to live
      k.halftime = halftimeAt(last, rules);
      k.open = true;
      add(k);
    }
  }
  return out;
}

Parsed parse(std::string_view body, bool drives) {
  Parsed out;
  std::unordered_set<std::string> ids;
  bool found = false;
  auto readDrives = [&] {
    bool any = readArray(body, {"drives", "previous"}, true, out, ids);
    // The drive under way (a live game): an object with its plays.
    any = readArray(body, {"drives", "current", "plays"}, false, out, ids) || any;
    return any;
  };
  if (drives) {
    found = readDrives();
    if (!found) found = readArray(body, {"plays"}, false, out, ids);
  } else {
    found = readArray(body, {"plays"}, false, out, ids);
    if (!found) found = readDrives();
  }
  out.ok = found;
  return out;
}

sources::Request summaryRequest(const League& league, std::string_view eventId) {
  sources::Request request;
  request.url = std::string(schedule::kApiBase) + league.path + "/summary?event=" + std::string(eventId);
  request.timeoutSeconds = kTimeoutSeconds;
  return request;
}

// ------------------------------------------------------------ fetch

void Fetch::want(const Game& g, Seconds now) {
  Entry& e = entries_[g.eventId];
  e.game = g;
  e.used = now;
  wanted_ = g.eventId;
  wantedAt_ = now;
  // Kept small: the games looked at least lately go.
  while (entries_.size() > kKept) {
    auto oldest = entries_.end();
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
      if (it->first != wanted_ && (oldest == entries_.end() || it->second.used < oldest->second.used)) oldest = it;
    if (oldest == entries_.end()) break;
    entries_.erase(oldest);
  }
}

int Fetch::sentToday(Seconds now) const {
  return static_cast<int>(std::count_if(sent_.begin(), sent_.end(), [now](Seconds t) { return now - t < 86'400; }));
}

std::optional<Fetch::Ask> Fetch::next(Seconds now) {
  if (busy_ && now - lastSent_ > kLostAfter) busy_ = false;
  if (busy_ || wanted_.empty() || now - wantedAt_ > kWantedFor) return std::nullopt;
  if (lastSent_ && now - lastSent_ < kSpacing) return std::nullopt;
  auto it = entries_.find(wanted_);
  if (it == entries_.end()) return std::nullopt;
  Entry& e = it->second;
  if (now < e.nextAt) return std::nullopt;
  // Read once; a game still on (or read while it was) again every
  // kRefresh; a final read as final, never again.
  if (e.readAt) {
    if (!e.game.live && !e.readLive) return std::nullopt;
    if (now - e.readAt < kRefresh) return std::nullopt;
  }
  const League* league = leagueByShort(e.game.league);
  if (!league || !rulesFor(*league)) return std::nullopt;
  Ask ask;
  ask.eventId = e.game.eventId;
  ask.league = e.game.league;
  ask.live = e.game.live;
  ask.request = summaryRequest(*league, e.game.summaryId);
  std::string source = refusals::sourceOf(ask.request.url);
  auto refused = refusedUntil_.find(source);
  if (refused != refusedUntil_.end() && refused->second > now) return std::nullopt;
  sent_.erase(std::remove_if(sent_.begin(), sent_.end(), [now](Seconds t) { return now - t >= 86'400; }), sent_.end());
  if (static_cast<int>(sent_.size()) >= kDailyCap) return std::nullopt;
  sent_.push_back(now);
  lastSent_ = now;
  busy_ = true;
  return ask;
}

bool Fetch::done(const std::string& eventId, int status, std::optional<std::vector<Break>> breaks, Seconds now,
                 int retryAfterSeconds, const time::Zone& zone) {
  busy_ = false;
  auto it = entries_.find(eventId);
  if (it == entries_.end()) return false;
  Entry& e = it->second;
  if (auto until = refusals::until(status, retryAfterSeconds, now, zone)) {
    const League* league = leagueByShort(e.game.league);
    if (league) refusedUntil_[refusals::sourceOf(summaryRequest(*league, e.game.summaryId).url)] = *until;
    e.nextAt = *until;
    return false;
  }
  if (status != 200 || !breaks) {
    // No answer, or one that can't be read: backed off (2, 5, 15, then
    // 30 minutes).
    size_t step = std::min<size_t>(static_cast<size_t>(e.failures), std::size(kBackoff) - 1);
    e.nextAt = now + kBackoff[step];
    e.failures += 1;
    return false;
  }
  e.failures = 0;
  e.readAt = now;
  e.readLive = e.game.live;
  auto fresh = std::make_shared<Breaks>();
  fresh->league = e.game.league;
  fresh->list = std::move(*breaks);
  fresh->live = e.game.live;
  fresh->readAt = now;
  bool changed = !e.breaks || e.breaks->list != fresh->list;
  e.breaks = std::move(fresh);
  return changed;
}

std::shared_ptr<const Breaks> Fetch::breaks(const std::string& eventId) const {
  auto it = entries_.find(eventId);
  return it == entries_.end() ? nullptr : it->second.breaks;
}

}  // namespace marigold::sports::plays
