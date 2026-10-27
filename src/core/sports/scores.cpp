#include "core/sports/scores.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_set>

#include "core/sports/parse.hpp"
#include "core/sports/profiles.hpp"
#include "core/sports/schedule.hpp"
#include "core/sports/text.hpp"

namespace marigold::sports::scores {

// MARK: Parsing

LivePhase livePhase(const std::optional<std::string>& state, std::string_view name) {
  std::string upper = str::upper(name);
  auto has = [&](const char* w) { return upper.find(w) != std::string::npos; };
  if (has("POSTPONED") || has("CANCELED") || has("CANCELLED") || has("SUSPENDED") || has("FORFEIT")) {
    return LivePhase::postponed;
  }
  std::string s = state ? str::lower(*state) : "";
  if (s == "in") return has("DELAY") ? LivePhase::delayed : LivePhase::live;
  if (s == "post") return LivePhase::final;
  return LivePhase::pre;
}

LivePhase livePhase(const League& league, const std::optional<std::string>& state, std::string_view name,
                    bool* suspended) {
  LivePhase phase = livePhase(state, name);
  std::string upper = str::upper(name);
  bool halted = upper.find("SUSPENDED") != std::string::npos;
  bool off = upper.find("POSTPONED") != std::string::npos || upper.find("CANCELED") != std::string::npos ||
             upper.find("CANCELLED") != std::string::npos || upper.find("FORFEIT") != std::string::npos;
  bool resumes = phase == LivePhase::postponed && halted && !off && profile(league.group).suspendResumes;
  if (suspended) *suspended = resumes;
  return resumes ? LivePhase::delayed : phase;
}

std::string liveDetail(const League& league, LivePhase phase, std::string_view name, int period,
                       std::string_view clock, const std::optional<std::string>& shortDetail) {
  std::string upper = str::upper(name);
  switch (phase) {
    case LivePhase::final: {
      std::string value = shortDetail.value_or("Final");
      std::string v = str::upper(value);
      return v == "FT" || v == "FINAL" ? "Final" : value;
    }
    case LivePhase::delayed:
    case LivePhase::postponed:
    case LivePhase::pre: return shortDetail.value_or("");
    case LivePhase::live: break;
  }
  if (upper.find("HALFTIME") != std::string::npos) return "Halftime";
  if ((upper.find("END_PERIOD") != std::string::npos || upper.find("END_OF") != std::string::npos) && shortDetail) {
    return *shortDetail;
  }
  const std::string& path = league.path;
  auto ordinal = [](int v) -> std::string {
    switch (v) {
      case 1: return "1st";
      case 2: return "2nd";
      case 3: return "3rd";
      default: return std::to_string(v) + "th";
    }
  };
  std::string c(clock);
  if (str::startsWith(path, "football/") || str::endsWith(path, "/nba") || str::endsWith(path, "/wnba") ||
      str::endsWith(path, "/womens-college-basketball")) {
    if (period > 4) return (period - 4 > 1 ? std::to_string(period - 4) + "OT" : std::string("OT")) + " " + c;
    return "Q" + std::to_string(std::max(1, period)) + " " + c;
  }
  if (str::endsWith(path, "/mens-college-basketball")) {
    if (period > 2) return (period - 2 > 1 ? std::to_string(period - 2) + "OT" : std::string("OT")) + " " + c;
    return ordinal(std::max(1, period)) + " " + c;
  }
  if (str::startsWith(path, "hockey/")) {
    if (period == 4) return "OT " + c;
    if (period > 4) return "SO";
    return ordinal(std::max(1, period)) + " " + c;
  }
  if (str::startsWith(path, "soccer/")) return c.empty() ? shortDetail.value_or("") : c;
  return shortDetail.value_or(c);
}

namespace {

std::optional<int> intOf(const json::Value& v) {
  if (v.isNumber()) return static_cast<int>(std::lround(v.asNumber()));  // "aggregateScore": 2.0
  auto t = text::text(v);
  if (!t) return std::nullopt;
  return text::toInt(*t);
}

}  // namespace

Statuses gameStatuses(std::string_view body, const League& league) {
  Statuses result;
  bool baseball = str::startsWith(league.path, "baseball/");
  parse::forEachEvent(body, [&](const json::Value& event) {
    auto id = text::text(event["id"]);
    const json::Value& competition = event["competitions"][size_t{0}];
    if (!id || !competition.isObject()) return;
    const json::Value& status = competition["status"].isObject() ? competition["status"] : event["status"];
    const json::Value& type = status["type"];
    std::string name = text::text(type["name"]).value_or("");
    bool suspended = false;
    LivePhase phase = livePhase(league, text::text(type["state"]), name, &suspended);
    if (phase == LivePhase::pre && text::flag(type["completed"])) phase = LivePhase::final;
    int period = intOf(status["period"]).value_or(0);
    std::string clock = text::text(status["displayClock"]).value_or("");
    LiveStatus live;
    live.phase = phase;
    live.suspended = suspended;
    live.detail = liveDetail(league, phase, name, period, clock, text::text(type["shortDetail"]));
    // A second leg's aggregate; a first leg's is only its own score.
    bool secondLeg = intOf(competition["leg"]["value"]).value_or(0) >= 2;
    for (const json::Value& competitor : competition["competitors"].items()) {
      if (!competitor.isObject()) continue;
      bool home = text::text(competitor["homeAway"]) == std::optional<std::string>("home");
      (home ? live.homeScore : live.awayScore) = text::text(competitor["score"]);
      (home ? live.homePens : live.awayPens) = intOf(competitor["shootoutScore"]);
      if (secondLeg) (home ? live.homeAggregate : live.awayAggregate) = intOf(competitor["aggregateScore"]);
    }
    if (!live.homePens || !live.awayPens) live.homePens = live.awayPens = std::nullopt;
    if (!live.homeAggregate || !live.awayAggregate) live.homeAggregate = live.awayAggregate = std::nullopt;
    if (phase == LivePhase::pre) {
      live.homeScore.reset();
      live.awayScore.reset();
      live.homePens = live.awayPens = std::nullopt;
      live.homeAggregate = live.awayAggregate = std::nullopt;
    }
    // Soccer has no situation line: the shootout while it's on, else the
    // aggregate ("Aggregate 2–2", home first as soccer tiles are).
    bool on = phase == LivePhase::live || phase == LivePhase::delayed;
    if (on && live.homePens) {
      live.situation = std::string("Penalties");
    } else if (live.homeAggregate) {
      live.situation = "Aggregate " + std::to_string(*live.homeAggregate) + "\xE2\x80\x93" +
                       std::to_string(*live.awayAggregate);
    }
    const json::Value& situation = competition["situation"];
    if ((phase == LivePhase::live || phase == LivePhase::delayed) && situation.isObject()) {
      if (auto down = text::text(situation["downDistanceText"])) live.situation = down;
      live.possession = text::text(situation["possession"]);
      live.redZone = text::flag(situation["isRedZone"]);
      if (baseball) {
        std::vector<std::string> parts;
        if (auto outs = intOf(situation["outs"])) parts.push_back(*outs == 1 ? "1 out" : std::to_string(*outs) + " outs");
        auto balls = text::text(situation["balls"]);
        auto strikes = text::text(situation["strikes"]);
        if (balls && strikes) parts.push_back(*balls + "-" + *strikes + " count");
        if (!parts.empty()) live.situation = str::join(parts, " \xC2\xB7 ");
        live.bases = std::vector<bool>{text::flag(situation["onFirst"]), text::flag(situation["onSecond"]),
                                       text::flag(situation["onThird"])};
      }
      if (auto play = text::text(situation["lastPlay"]["text"])) {
        live.lastPlay = text::count(*play) > 110 ? text::prefix(*play, 107) + "\xE2\x80\xA6" : *play;
      }
    }
    result[league.shortName + "|" + *id] = std::move(live);
  });
  return result;
}

Statuses fightStatuses(std::string_view body, const League& league) {
  Statuses result;
  parse::forEachEvent(body, [&](const json::Value& event) {
    auto id = text::text(event["id"]);
    if (!id) return;
    std::vector<json::Value> bouts;
    for (const json::Value& bout : event["competitions"].items()) {
      if (bout.isObject()) bouts.push_back(bout);
    }
    // The card itself over: every part is final, whatever its bouts say (a
    // scrapped bout can stay "scheduled" forever).
    const json::Value& eventType = event["status"]["type"];
    auto eventState = text::text(eventType["state"]);
    bool cardOver = text::flag(eventType["completed"]) || (eventState && str::lower(*eventState) == "post");
    std::vector<std::string> keys = parse::partKeys(bouts);
    struct Part {
      int total = 0, done = 0, postponed = 0;
      std::optional<size_t> live, next;
    };
    std::map<std::string, Part> parts;
    for (size_t i = 0; i < bouts.size(); ++i) {
      Part& part = parts[keys[i]];
      part.total += 1;
      const json::Value& type = bouts[i]["status"]["type"];
      LivePhase phase = livePhase(text::text(type["state"]), text::text(type["name"]).value_or(""));
      if (text::flag(type["completed"]) || phase == LivePhase::final) {
        part.done += 1;
      } else if (phase == LivePhase::postponed) {
        part.postponed += 1;
      } else if (phase == LivePhase::live || phase == LivePhase::delayed) {
        if (!part.live) part.live = i;
      } else if (!part.next) {
        part.next = i;
      }
    }
    // A part with fights left but none on, once a later part has begun, is
    // over too (its remaining bouts were scrapped).
    auto order = [](const std::string& key) { return key == "early" ? 0 : key == "prelims" ? 1 : 2; };
    auto laterStarted = [&](const std::string& key) {
      return std::any_of(parts.begin(), parts.end(), [&](const auto& other) {
        return order(other.first) > order(key) && (other.second.done > 0 || other.second.live.has_value());
      });
    };
    for (const auto& [key, part] : parts) {
      if (part.total <= 0) continue;
      bool finished = cardOver || part.done + part.postponed >= part.total ||
                      (!part.live && part.done > 0 && laterStarted(key));
      LivePhase phase = finished ? (part.done == 0 && !cardOver ? LivePhase::postponed : LivePhase::final)
                                 : (part.live || part.done > 0 ? LivePhase::live : LivePhase::pre);
      int total = part.total - part.postponed;
      LiveStatus live;
      live.phase = phase;
      live.detail = phase == LivePhase::final ? "Final" : "";
      live.boutsDone = part.done;
      live.boutsTotal = total;
      if (phase == LivePhase::live) {
        live.detail = "fight " + std::to_string(std::min(part.done + 1, std::max(1, total))) + " of " +
                      std::to_string(std::max(1, total));
      }
      std::optional<size_t> bout = part.live ? part.live : part.next;
      if (phase != LivePhase::final && bout) {
        std::vector<SportsTeam> fighters;
        for (const json::Value& c : bouts[*bout]["competitors"].items()) {
          if (auto f = parse::fighter(c)) fighters.push_back(std::move(*f));
        }
        if (fighters.size() == 2) {
          live.boutA = fighters[0].name;
          live.boutB = fighters[1].name;
          live.boutACountry = fighters[0].country;
          live.boutBCountry = fighters[1].country;
        }
      }
      result[league.shortName + "|" + *id + "|" + key] = std::move(live);
    }
  });
  return result;
}

Statuses raceStatuses(std::string_view body, const League& league) {
  Statuses result;
  parse::forEachEvent(body, [&](const json::Value& event) {
    auto id = text::text(event["id"]);
    if (!id) return;
    for (const json::Value& competition : event["competitions"].items()) {
      auto sessionID = text::text(competition["id"]);
      if (!sessionID) continue;
      const json::Value& type = competition["status"]["type"];
      std::string name = text::text(type["name"]).value_or("");
      LivePhase phase = livePhase(text::text(type["state"]), name);
      if (phase == LivePhase::pre && text::flag(type["completed"])) phase = LivePhase::final;
      LiveStatus status;
      status.phase = phase;
      if (phase == LivePhase::final) {
        status.detail = "Final";
      } else if (phase == LivePhase::live || phase == LivePhase::delayed) {
        auto shortText = text::text(type["shortDetail"]);
        if (!shortText) shortText = text::text(type["detail"]);
        if (shortText) {
          // "Lap 34 of 56"; not a date or a time.
          std::string upper = str::upper(*shortText);
          auto has = [&](const char* w) { return upper.find(w) != std::string::npos; };
          bool isTime = has(" AM") || has(" PM") || has("/") || has(" EDT") || has(" EST");
          if (!isTime && text::count(*shortText) <= 24) status.detail = *shortText;
        }
      }
      result[league.shortName + "|" + *id + "|" + *sessionID] = std::move(status);
    }
  });
  return result;
}

Statuses tennisStatuses(std::string_view body, const League& league) {
  Statuses result;
  parse::forEachEvent(body, [&](const json::Value& event) {
    auto eventID = text::text(event["id"]);
    if (!eventID) return;
    for (const json::Value* competitionPtr : parse::tennisCompetitions(event)) {
      const json::Value& competition = *competitionPtr;
      auto matchID = text::text(competition["id"]);
      if (!matchID) continue;
      const json::Value& statusObject = competition["status"];
      const json::Value& type = statusObject["type"];
      bool suspended = false;
      LivePhase phase = livePhase(league, text::text(type["state"]), text::text(type["name"]).value_or(""), &suspended);
      if (phase == LivePhase::pre && text::flag(type["completed"])) phase = LivePhase::final;
      int period = intOf(statusObject["period"]).value_or(0);
      LiveStatus status;
      status.phase = phase;
      status.suspended = suspended;
      if (phase == LivePhase::final) {
        status.detail = "Final";
      } else if (phase == LivePhase::live || phase == LivePhase::delayed) {
        status.detail = period > 0 ? "Set " + std::to_string(period) : "Live";
      }
      if (phase != LivePhase::pre) {
        const json::Value::Array& competitors = competition["competitors"].items();
        for (size_t index = 0; index < competitors.size(); ++index) {
          const json::Value& competitor = competitors[index];
          std::vector<std::string> sets;
          for (const json::Value& line : competitor["linescores"].items()) {
            auto value = text::text(line["value"]);
            if (!value) continue;
            sets.push_back(str::endsWith(*value, ".0") ? value->substr(0, value->size() - 2) : *value);
          }
          auto homeAway = text::text(competitor["homeAway"]);
          if (homeAway == std::optional<std::string>("home") || (!homeAway && index == 0)) {
            status.homeSets = sets;
          } else {
            status.awaySets = sets;
          }
        }
      }
      result[league.shortName + "|" + *eventID + "|" + *matchID] = std::move(status);
    }
  });
  return result;
}

Statuses statuses(std::string_view body, const League& league) {
  if (league.racing) return raceStatuses(body, league);
  if (league.tennis) return tennisStatuses(body, league);
  return league.fights ? fightStatuses(body, league) : gameStatuses(body, league);
}

// MARK: Requests

sources::Request scoreboardRequest(const League& league, std::string_view day) {
  sources::Request request;
  std::string base = std::string(schedule::kApiBase) + league.path + "/scoreboard";
  request.url = day.empty() ? base : base + "?dates=" + std::string(day) + league.extra;
  request.timeoutSeconds = kTimeoutSeconds;
  return request;
}

// MARK: Polling

bool LiveScores::start(Holder holder, bool scheduleEnabled) {
  bool wasLooking = looking();
  holders_.insert(holder);
  if (!scheduleEnabled) return false;
  // The guide looks now instead of after the current wait.
  if (holder == Holder::guide && wasLooking) return true;
  return !wasLooking;
}

bool LiveScores::stop(Holder holder) {
  holders_.erase(holder);
  if (!holders_.empty()) return false;
  cancelRound();
  return true;
}

std::optional<LiveStatus> LiveScores::status(const std::string& gameID) const {
  auto it = statuses_.find(gameID);
  if (it == statuses_.end()) return std::nullopt;
  return it->second;
}

int LiveScores::requestsToday(Seconds now, const time::Zone& zone) const {
  return daily_.day == text::isoDay(now, zone) ? daily_.count : 0;
}

bool LiveScores::takeRequest(Seconds now) {
  std::string today = text::isoDay(now, zone_);
  if (daily_.day != today) {
    daily_.day = today;
    daily_.count = 0;
    cappedNoted_ = false;
  }
  if (daily_.count >= kDailyCap) return false;
  daily_.count += 1;
  return true;
}

void LiveScores::beginRound(const RoundInput& input) {
  round_.reset();
  zone_ = input.zone;
  const Seconds now = input.now;
  // Nothing while a channel is playing, unless the in-player guide is open.
  if (input.watching && !holders_.count(Holder::guide)) {
    lastWait_ = kWatchingWait;
    return;
  }
  Round r;
  r.games = input.games;
  r.showsScores = input.showsScores;
  for (const SportsGame& game : input.games) {
    if (game.isTennis() && !input.watchableTennis.count(game.id)) continue;
    auto known = statuses_.find(game.id);
    std::optional<LivePhase> phase;
    if (known != statuses_.end()) phase = known->second.phase;
    if (phase == LivePhase::final || phase == LivePhase::postponed) continue;
    auto miss = missing_.find(game.id);
    if (!phase && miss != missing_.end() && miss->second >= kMissingLimit) continue;
    // A game "live" for half a day is a status that never flipped: stop asking.
    bool inProgress = (phase == LivePhase::live || phase == LivePhase::delayed) && now <= game.start + kLiveAtMost;
    bool inWindow = !game.completed && now >= game.start - kLeadBeforeStart && now <= game.start + kWindowAfterStart;
    if (!inProgress && !inWindow) continue;
    r.windows[game.leagueShort].push_back(game);
  }
  if (r.windows.empty()) {
    lastWait_ = kIdleWait;
    return;
  }
  recent_.erase(std::remove_if(recent_.begin(), recent_.end(), [&](Seconds t) { return now - t > 3600; }),
                recent_.end());
  // Several leagues on at once share the pace: each waits longer.
  r.spread = std::max(1.0, static_cast<double>(r.windows.size()) / 2);
  for (const auto& [key, games] : r.windows) r.order.push_back(key);
  round_ = std::move(r);
}

std::optional<LiveScores::Request> LiveScores::nextRequest(Seconds now) {
  if (!round_ || round_->over || round_->awaiting) return std::nullopt;
  Round& r = *round_;
  while (true) {
    if (!r.inLeague) {
      if (r.nextIndex >= r.order.size()) {
        r.over = true;
        lastWait_ = r.soonest;
        return std::nullopt;
      }
      const std::string& shortName = r.order[r.nextIndex];
      r.nextIndex += 1;
      auto due = nextDue_.find(shortName);
      if (due != nextDue_.end() && due->second > now) {
        r.soonest = std::min(r.soonest, due->second - now);
        continue;
      }
      const League* league = leagueByShort(shortName);
      if (!league) continue;
      if (!takeRequest(now)) {
        cappedNoted_ = true;  // daily cap reached: paused until tomorrow
        r.over = true;
        lastWait_ = kCappedWait;
        return std::nullopt;
      }
      r.inLeague = true;
      r.shortName = shortName;
      r.league = *league;
      r.days.clear();
      // A race weekend's listing has every session: one request, no day.
      if (league->racing || league->tennis) {
        r.days.emplace_back();
      } else {
        std::set<std::string> days;
        for (const SportsGame& g : r.windows[shortName]) {
          days.insert(text::yearMonthDay(g.start, time::Zone::usEastern()));
        }
        r.days.assign(days.begin(), days.end());
        if (r.days.size() > 2) r.days.resize(2);
      }
      r.dayIndex = 0;
      r.merged.clear();
      r.ok = false;
      r.bytes = 0;
    } else if (r.dayIndex >= r.days.size()) {
      endLeague(now);
      continue;
    } else if (r.dayIndex > 0 && !takeRequest(now)) {
      r.dayIndex = r.days.size();
      continue;
    }
    Request request;
    request.league = r.league;
    request.day = r.days[r.dayIndex];
    request.request = scoreboardRequest(r.league, request.day);
    request.waitMs = r.sent > 0 ? kRequestSpacingMs : 0;
    r.sent += 1;
    r.awaiting = true;
    return request;
  }
}

void LiveScores::onResponse(int status, std::string_view body, Seconds now) {
  if (!round_ || !round_->awaiting) return;
  if (status != 200) {
    onParsed(false, 0, {}, now);
    return;
  }
  onParsed(true, body.size(), scores::statuses(body, round_->league), now);
}

void LiveScores::onParsed(bool ok, size_t bytes, Statuses parsed, Seconds now) {
  if (!round_ || !round_->awaiting) return;
  Round& r = *round_;
  r.awaiting = false;
  r.dayIndex += 1;
  if (ok) {
    recent_.push_back(now);
    r.bytes += bytes;
    for (auto& [id, status] : parsed) r.merged[id] = std::move(status);
    r.ok = true;
  }
}

void LiveScores::endLeague(Seconds now) {
  Round& r = *round_;
  r.inLeague = false;
  const std::string& shortName = r.shortName;
  if (r.ok) {
    bool anyLive = std::any_of(r.merged.begin(), r.merged.end(), [](const auto& item) {
      return item.second.phase == LivePhase::live || item.second.phase == LivePhase::delayed;
    });
    for (const SportsGame& game : r.windows[shortName]) {
      if (!r.merged.count(game.id)) {
        missing_[game.id] += 1;
      } else {
        missing_.erase(game.id);
      }
    }
    failures_[shortName] = 0;
    lastBytes_[shortName] = r.bytes;
    apply(r.merged, now, r.games);
    double interval = static_cast<double>(anyLive ? (r.showsScores ? kLiveInterval : kQuietInterval) : kQuietInterval);
    if (r.bytes > kLargeScoreboardBytes) interval = std::max(interval, static_cast<double>(kQuietInterval));
    // Racing only needs the session's status.
    if (r.league.racing) interval = std::max(interval, static_cast<double>(kQuietInterval));
    interval *= r.spread;
    if (recent_.size() >= static_cast<size_t>(kHourlyPace)) interval = std::max(interval, static_cast<double>(kPacedInterval));
    Seconds wait = static_cast<Seconds>(interval);
    nextDue_[shortName] = now + wait;
    r.soonest = std::min(r.soonest, wait);
  } else {
    int count = failures_[shortName] + 1;
    failures_[shortName] = count;
    Seconds wait = kBackoff[std::min(count - 1, 3)];
    nextDue_[shortName] = now + wait;
    r.soonest = std::min(r.soonest, wait);
  }
}

void LiveScores::cancelRound() {
  if (round_) lastWait_ = kIdleWait;
  round_.reset();
}

Seconds LiveScores::waitSeconds() const { return std::max(kMinimumWait, lastWait_); }

void LiveScores::apply(const Statuses& fresh, Seconds now, const std::vector<SportsGame>& games) {
  bool changed = false;
  for (const auto& [id, status] : fresh) {
    auto old = statuses_.find(id);
    if (old != statuses_.end() && old->second == status) continue;
    if (status.phase == LivePhase::final && !finalSeen_.count(id)) {
      // Seen ending now, or already over when first looked at.
      bool wasLive = old != statuses_.end() &&
                     (old->second.phase == LivePhase::live || old->second.phase == LivePhase::delayed);
      finalSeen_[id] = wasLive ? now : now - 20 * 60;
    }
    statuses_[id] = status;
    changed = true;
  }
  // Forget games from earlier days.
  if (statuses_.size() > 600) {
    std::unordered_set<std::string> keep;
    for (const SportsGame& g : games) keep.insert(g.id);
    for (auto it = statuses_.begin(); it != statuses_.end();) {
      it = keep.count(it->first) ? std::next(it) : statuses_.erase(it);
    }
    for (auto it = finalSeen_.begin(); it != finalSeen_.end();) {
      it = keep.count(it->first) ? std::next(it) : finalSeen_.erase(it);
    }
  }
  if (changed) revision_ += 1;
}

}  // namespace marigold::sports::scores
