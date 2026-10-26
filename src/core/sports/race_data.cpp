#include "core/sports/race_data.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>

#include "core/sports/text.hpp"

namespace marigold::sports::race_data {

namespace {

std::string trimmed(std::string_view s) { return std::string(str::trimUnicode(s)); }

// "Driver Ülpha" -> "driver ulpha" (folding accents and case).
std::string fold(std::string_view s) { return str::lower(str::foldDiacritics(s)); }

// RaceData.int: a number (NSNumber.intValue: truncated, a bool is 1 or 0),
// or text that reads as an integer.
std::optional<int> intOf(const json::Value& v) {
  if (v.isNumber()) return static_cast<int>(v.asNumber());
  if (v.isBool()) return v.asBool() ? 1 : 0;
  if (v.isString()) return text::toInt(v.asString());
  return std::nullopt;
}

std::optional<std::string> stringOf(const json::Value& v) {
  if (!v.isString()) return std::nullopt;
  return v.asString();
}

// ISO 8601 with or without fractional seconds (the fraction is dropped).
std::optional<Seconds> dateOf(const json::Value& v) {
  if (!v.isString()) return std::nullopt;
  std::string value = v.asString();
  if (auto plain = sources::parseInternetDateTime(value)) return plain;
  if (value.size() > 20 && value[19] == '.') {
    size_t end = 20;
    while (end < value.size() && value[end] >= '0' && value[end] <= '9') ++end;
    if (end == 20) return std::nullopt;
    return sources::parseInternetDateTime(value.substr(0, 19) + value.substr(end));
  }
  return std::nullopt;
}

json::Value::Array listOf(std::string_view body) {
  auto root = json::parse(body);
  if (!root || !root->isArray()) return {};
  json::Value::Array out;
  for (const json::Value& item : root->items()) {
    if (item.isObject()) out.push_back(item);
  }
  return out;
}

std::optional<Session> sessionOf(const json::Value& item) {
  auto key = intOf(item["session_key"]);
  auto start = dateOf(item["date_start"]);
  if (!key || !start) return std::nullopt;
  std::string name = stringOf(item["session_name"]).value_or(stringOf(item["session_type"]).value_or(""));
  Session s;
  s.key = *key;
  s.kind = race_session::kind(name);
  s.start = *start;
  s.end = dateOf(item["date_end"]);
  return s;
}

std::optional<RaceDriver> driverOf(const json::Value& item) {
  auto number = intOf(item["driver_number"]);
  if (!number) return std::nullopt;
  std::string first = stringOf(item["first_name"]).value_or("");
  std::string last = stringOf(item["last_name"]).value_or("");
  std::string name = first;
  if (!last.empty()) name += (name.empty() ? "" : " ") + last;
  if (name.empty()) name = stringOf(item["full_name"]).value_or("#" + std::to_string(*number));
  RaceDriver d;
  d.number = *number;
  d.code = stringOf(item["name_acronym"]).value_or(str::upper(text::prefix(last, 3)));
  d.name = name;
  d.team = stringOf(item["team_name"]).value_or("");
  if (auto color = stringOf(item["team_colour"]); color && text::count(*color) == 6) d.color = str::upper(*color);
  d.headshot = stringOf(item["headshot_url"]);
  return d;
}

sources::Request get(const std::string& path) {
  sources::Request r;
  r.url = std::string(kBase) + path;
  r.timeoutSeconds = kTimeoutSeconds;
  return r;
}

// MARK: JSON

json::Value driverJson(const RaceDriver& d) {
  json::Value v = json::Value::object();
  v.set("number", d.number);
  v.set("code", d.code);
  v.set("name", d.name);
  v.set("team", d.team);
  if (d.color) v.set("color", *d.color);
  if (d.headshot) v.set("headshot", *d.headshot);
  return v;
}

std::optional<RaceDriver> driverFromJson(const json::Value& v) {
  if (!v.isObject() || !v["number"].isNumber() || !v["name"].isString()) return std::nullopt;
  RaceDriver d;
  d.number = static_cast<int>(v["number"].asInt());
  d.code = v["code"].asString();
  d.name = v["name"].asString();
  d.team = v["team"].asString();
  d.color = stringOf(v["color"]);
  d.headshot = stringOf(v["headshot"]);
  return d;
}

std::optional<Seconds> secondsOf(const json::Value& v) {
  if (!v.isNumber()) return std::nullopt;
  return static_cast<Seconds>(v.asInt());
}

json::Value driversJson(const std::vector<RaceDriver>& drivers) {
  json::Value list = json::Value::array();
  for (const RaceDriver& d : drivers) list.push(driverJson(d));
  return list;
}

std::vector<RaceDriver> driversFromJson(const json::Value& v) {
  std::vector<RaceDriver> out;
  for (const json::Value& item : v.items()) {
    if (auto d = driverFromJson(item)) out.push_back(std::move(*d));
  }
  return out;
}

}  // namespace

// MARK: Requests and parsing

sources::Request sessionsRequest() { return get("sessions?meeting_key=latest"); }
sources::Request driversRequest() { return get("drivers?session_key=latest"); }
sources::Request resultRequest(int sessionKey) {
  return get("session_result?session_key=" + std::to_string(sessionKey));
}

sources::Request nascarRequest() {
  sources::Request r;
  r.url = kNascarUrl;
  r.timeoutSeconds = kNascarTimeoutSeconds;
  return r;
}

ParsedSessions parseSessions(std::string_view body) {
  ParsedSessions out;
  json::Value::Array items = listOf(body);
  if (!items.empty()) out.meeting = intOf(items.front()["meeting_key"]);
  for (const json::Value& item : items) {
    if (auto s = sessionOf(item)) out.sessions.push_back(std::move(*s));
  }
  return out;
}

ParsedDrivers parseDrivers(std::string_view body) {
  ParsedDrivers out;
  json::Value::Array items = listOf(body);
  if (!items.empty()) out.meeting = intOf(items.front()["meeting_key"]);
  for (const json::Value& item : items) {
    if (auto d = driverOf(item)) out.drivers.push_back(std::move(*d));
  }
  return out;
}

std::vector<int> parseResult(std::string_view body) {
  std::vector<std::pair<int, int>> placed;
  for (const json::Value& item : listOf(body)) {
    auto position = intOf(item["position"]);
    auto number = intOf(item["driver_number"]);
    if (position && number) placed.emplace_back(*position, *number);
  }
  std::stable_sort(placed.begin(), placed.end(),
                   [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first < b.first; });
  std::vector<int> out;
  for (size_t i = 0; i < placed.size() && i < 3; ++i) out.push_back(placed[i].second);
  return out;
}

std::string bareName(std::string_view name) {
  std::string value = trimmed(str::replaceAll(text::components(name, "(").front(), ".", ""));
  for (const char* suffix : {" Jr", " Sr", " II", " III"}) {
    if (str::endsWith(value, suffix)) value.resize(value.size() - std::char_traits<char>::length(suffix));
  }
  return trimmed(value);
}

std::vector<RaceDriver> parseNascar(std::string_view body) {
  auto root = json::parse(body);
  if (!root) return {};
  std::vector<RaceDriver> cup, other;
  for (const json::Value& item : (*root)["response"].items()) {
    std::string name = bareName(stringOf(item["Full_Name"]).value_or(""));
    if (name.find(' ') == std::string::npos) continue;
    std::optional<std::string> photo;
    for (const json::Value* field : {&item["Image_Small"], &item["Image"]}) {
      auto url = stringOf(*field);
      if (url && str::startsWith(*url, "https://") && url->find("default.png") == std::string::npos &&
          str::lower(*url).find("silhouette") == std::string::npos) {
        photo = url;
        break;
      }
    }
    if (!photo) continue;
    std::string badge = stringOf(item["Badge"]).value_or("");
    if (!item["Badge"].isString()) {
      if (auto n = intOf(item["Badge"])) badge = std::to_string(*n);
    }
    RaceDriver d;
    d.number = text::toInt(badge).value_or(0);
    d.code = badge;
    d.name = name;
    d.team = stringOf(item["Team"]).value_or("");
    d.headshot = photo;
    (stringOf(item["Driver_Series"]) == std::optional<std::string>("nascar-cup-series") ? cup : other).push_back(d);
  }
  cup.insert(cup.end(), other.begin(), other.end());
  return cup;
}

// MARK: Saved state

std::string toJson(const Saved& s) {
  json::Value v = json::Value::object();
  if (s.meetingKey) v.set("meetingKey", *s.meetingKey);
  json::Value sessions = json::Value::array();
  for (const Session& session : s.sessions) {
    json::Value item = json::Value::object();
    item.set("key", session.key);
    item.set("kind", session.kind);
    item.set("start", static_cast<int64_t>(session.start));
    if (session.end) item.set("end", static_cast<int64_t>(*session.end));
    sessions.push(std::move(item));
  }
  v.set("sessions", std::move(sessions));
  if (s.sessionsAt) v.set("sessionsAt", static_cast<int64_t>(*s.sessionsAt));
  v.set("drivers", driversJson(s.drivers));
  if (s.driversMeeting) v.set("driversMeeting", *s.driversMeeting);
  if (s.driversAt) v.set("driversAt", static_cast<int64_t>(*s.driversAt));
  json::Value results = json::Value::object();
  for (const auto& [key, numbers] : s.results) {
    json::Value list = json::Value::array();
    for (int n : numbers) list.push(n);
    results.set(key, std::move(list));
  }
  v.set("results", std::move(results));
  json::Value tried = json::Value::object();
  for (const auto& [key, at] : s.resultTried) tried.set(key, static_cast<int64_t>(at));
  v.set("resultTried", std::move(tried));
  json::Value tries = json::Value::object();
  for (const auto& [key, n] : s.resultTries) tries.set(key, n);
  v.set("resultTries", std::move(tries));
  if (s.requestDay) v.set("requestDay", *s.requestDay);
  v.set("requestCount", s.requestCount);
  return json::write(v);
}

std::optional<Saved> savedFromJson(std::string_view text) {
  auto root = json::parse(text);
  if (!root || !root->isObject()) return std::nullopt;
  const json::Value& v = *root;
  Saved s;
  if (v["meetingKey"].isNumber()) s.meetingKey = static_cast<int>(v["meetingKey"].asInt());
  for (const json::Value& item : v["sessions"].items()) {
    if (!item["key"].isNumber() || !item["start"].isNumber()) continue;
    Session session;
    session.key = static_cast<int>(item["key"].asInt());
    session.kind = item["kind"].asString();
    session.start = static_cast<Seconds>(item["start"].asInt());
    session.end = secondsOf(item["end"]);
    s.sessions.push_back(std::move(session));
  }
  s.sessionsAt = secondsOf(v["sessionsAt"]);
  s.drivers = driversFromJson(v["drivers"]);
  if (v["driversMeeting"].isNumber()) s.driversMeeting = static_cast<int>(v["driversMeeting"].asInt());
  s.driversAt = secondsOf(v["driversAt"]);
  for (const auto& [key, list] : v["results"].fields()) {
    std::vector<int> numbers;
    for (const json::Value& n : list.items()) {
      if (n.isNumber()) numbers.push_back(static_cast<int>(n.asInt()));
    }
    s.results[key] = std::move(numbers);
  }
  for (const auto& [key, at] : v["resultTried"].fields()) {
    if (at.isNumber()) s.resultTried[key] = static_cast<Seconds>(at.asInt());
  }
  for (const auto& [key, n] : v["resultTries"].fields()) {
    if (n.isNumber()) s.resultTries[key] = static_cast<int>(n.asInt());
  }
  s.requestDay = stringOf(v["requestDay"]);
  s.requestCount = static_cast<int>(v["requestCount"].asInt());
  return s;
}

std::string toJson(const NascarSaved& s) {
  json::Value v = json::Value::object();
  v.set("drivers", driversJson(s.drivers));
  if (s.fetchedAt) v.set("fetchedAt", static_cast<int64_t>(*s.fetchedAt));
  return json::write(v);
}

std::optional<NascarSaved> nascarFromJson(std::string_view text) {
  auto root = json::parse(text);
  if (!root || !root->isObject()) return std::nullopt;
  NascarSaved s;
  s.drivers = driversFromJson((*root)["drivers"]);
  s.fetchedAt = secondsOf((*root)["fetchedAt"]);
  return s;
}

// MARK: RaceData

RaceData::RaceData(Saved saved, NascarSaved nascar) : saved_(std::move(saved)), nascar_(std::move(nascar)) {
  rebuildNames();
  rebuildNascarNames();
}

void RaceData::rebuildNames() {
  byName_.clear();
  for (const RaceDriver& d : saved_.drivers) {
    std::string full = fold(d.name);
    byName_[full] = d;
    std::vector<std::string> words = text::words(full);
    if (!words.empty()) byName_[words.back()] = d;
  }
}

void RaceData::rebuildNascarNames() {
  nascarByName_.clear();
  for (const RaceDriver& d : nascar_.drivers) nascarByName_.emplace(fold(bareName(d.name)), d);  // first one wins
}

std::optional<RaceDriver> RaceData::driver(std::string_view name) const {
  std::string key = trimmed(str::replaceAll(fold(name), " jr.", ""));
  auto match = byName_.find(key);
  if (match != byName_.end()) return match->second;
  std::vector<std::string> words = text::words(key);
  if (words.empty()) return std::nullopt;
  match = byName_.find(words.back());
  if (match != byName_.end()) return match->second;
  return std::nullopt;
}

std::optional<std::vector<RaceDriver>> RaceData::top3(std::string_view kind, Seconds start) const {
  const Session* session = nullptr;
  for (const Session& s : saved_.sessions) {
    if (s.kind == kind && std::llabs(s.start - start) < kSameSession) {
      session = &s;
      break;
    }
  }
  if (!session) return std::nullopt;
  auto numbers = saved_.results.find(std::to_string(session->key));
  if (numbers == saved_.results.end() || numbers->second.empty()) return std::nullopt;
  std::vector<RaceDriver> drivers;
  for (size_t i = 0; i < numbers->second.size() && i < 3; ++i) {
    for (const RaceDriver& d : saved_.drivers) {
      if (d.number == numbers->second[i]) {
        drivers.push_back(d);
        break;
      }
    }
  }
  if (drivers.empty()) return std::nullopt;
  return drivers;
}

std::vector<board::PodiumDriver> RaceData::podium(const std::string& kind, Seconds start) const {
  std::vector<board::PodiumDriver> out;
  if (auto top = top3(kind, start)) {
    for (const RaceDriver& d : *top) out.push_back(board::PodiumDriver{d.code, d.color});
  }
  return out;
}

std::function<std::vector<board::PodiumDriver>(const std::string&, Seconds)> RaceData::podiumHook() const {
  RaceData lookups;
  lookups.saved_.sessions = saved_.sessions;
  lookups.saved_.drivers = saved_.drivers;
  lookups.saved_.results = saved_.results;
  return [race = std::move(lookups)](const std::string& kind, Seconds start) { return race.podium(kind, start); };
}

std::optional<RaceDriver> RaceData::nascarDriver(std::string_view name) const {
  auto match = nascarByName_.find(fold(bareName(name)));
  if (match == nascarByName_.end()) return std::nullopt;
  return match->second;
}

RaceData::Plan RaceData::due(const std::vector<WantedSession>& wanted, bool results, Seconds now) const {
  Plan plan;
  bool known = std::all_of(wanted.begin(), wanted.end(), [&](const WantedSession& item) {
    return std::any_of(saved_.sessions.begin(), saved_.sessions.end(), [&](const Session& s) {
      return s.kind == item.kind && std::llabs(s.start - item.start) < kSameSession;
    });
  });
  // Never fetched: older than anything.
  std::optional<Seconds> sessionsAge;
  if (saved_.sessionsAt) sessionsAge = now - *saved_.sessionsAt;
  plan.sessions = !sessionsAge || *sessionsAge > kSessionsMaxAge || (!known && *sessionsAge > kSessionsUnknownAge);
  std::optional<Seconds> driversAge;
  if (saved_.driversAt) driversAge = now - *saved_.driversAt;
  bool newMeeting = saved_.meetingKey && saved_.driversMeeting != saved_.meetingKey;
  plan.drivers = saved_.drivers.empty() || !driversAge || *driversAge > kDriversMaxAge ||
                 (newMeeting && *driversAge > kDriversNewMeetingAge);
  if (results) {
    for (const Session& session : saved_.sessions) {
      std::string key = std::to_string(session.key);
      if (saved_.results.count(key)) continue;
      Seconds end = session.end.value_or(session.start + race_session::length(session.kind));
      if (now - end <= kResultDelay || now - end >= kResultWindow) continue;
      auto tried = saved_.resultTried.find(key);
      if (tried != saved_.resultTried.end() && now - tried->second < kResultRetry) continue;
      auto tries = saved_.resultTries.find(key);
      if (tries != saved_.resultTries.end() && tries->second >= kResultMaxTries) continue;
      plan.results.push_back(session.key);
      if (plan.results.size() == kResultsPerPass) break;
    }
  }
  return plan;
}

bool RaceData::refreshIfNeeded(const std::vector<WantedSession>& sessions, bool results, Seconds now) {
  if (running() || now < nextAllowed_) return false;
  Plan plan = due(sessions, results, now);
  if (plan.empty()) return false;
  pass_ = Pass{};
  pass_->plan = std::move(plan);
  return true;
}

std::optional<sources::Request> RaceData::take(sources::Request request, Seconds now, const time::Zone& zone) {
  std::string today = text::isoDay(now, zone);
  if (saved_.requestDay != today) {
    saved_.requestDay = today;
    saved_.requestCount = 0;
  }
  if (saved_.requestCount >= kDailyCap) return std::nullopt;
  saved_.requestCount += 1;
  return request;
}

int RaceData::requestsToday(Seconds now, const time::Zone& zone) const {
  return saved_.requestDay == text::isoDay(now, zone) ? saved_.requestCount : 0;
}

std::optional<RaceData::Request> RaceData::nextRequest(Seconds now, const time::Zone& zone) {
  if (!pass_ || pass_->awaiting) return std::nullopt;
  Pass& p = *pass_;
  auto ask = [&](Ask what, int key, sources::Request request, int waitMs) -> std::optional<Request> {
    auto taken = take(std::move(request), now, zone);
    if (!taken) {
      // Today's cap: like a failure.
      p.failed = true;
      return std::nullopt;
    }
    p.awaiting = true;
    p.ask = what;
    p.key = key;
    return Request{what, key, std::move(*taken), waitMs};
  };
  while (true) {
    if (p.step == 0) {
      p.step = 1;
      if (p.plan.sessions) {
        if (auto r = ask(Ask::sessions, 0, sessionsRequest(), 0)) return r;
      }
      continue;
    }
    if (p.step == 1) {
      p.step = 2;
      // A new meeting's session list: its drivers too.
      bool drivers = p.plan.drivers || (p.changed && saved_.driversMeeting != saved_.meetingKey);
      if (drivers && !p.failed) {
        if (auto r = ask(Ask::drivers, 0, driversRequest(), p.plan.sessions ? kRequestSpacingMs : 0)) return r;
      }
      continue;
    }
    if (!p.failed && p.resultIndex < p.plan.results.size()) {
      int key = p.plan.results[p.resultIndex++];
      std::string k = std::to_string(key);
      saved_.resultTried[k] = now;
      saved_.resultTries[k] += 1;
      if (auto r = ask(Ask::result, key, resultRequest(key), kRequestSpacingMs)) return r;
      continue;
    }
    finish(now);
    return std::nullopt;
  }
}

void RaceData::onResponse(int status, std::string_view body, Seconds now) {
  if (!pass_ || !pass_->awaiting) return;
  Pass& p = *pass_;
  p.awaiting = false;
  if (status != 200) {
    p.failed = true;
    return;
  }
  switch (p.ask) {
    case Ask::sessions: {
      ParsedSessions parsed = parseSessions(body);
      if (parsed.meeting) saved_.meetingKey = parsed.meeting;
      if (!parsed.sessions.empty()) {
        std::stable_sort(parsed.sessions.begin(), parsed.sessions.end(),
                         [](const Session& a, const Session& b) { return a.start < b.start; });
        saved_.sessions = std::move(parsed.sessions);
      }
      saved_.sessionsAt = now;
      p.changed = true;
      break;
    }
    case Ask::drivers: {
      ParsedDrivers parsed = parseDrivers(body);
      if (!parsed.drivers.empty()) {
        saved_.drivers = std::move(parsed.drivers);
        saved_.driversMeeting = parsed.meeting ? parsed.meeting : saved_.meetingKey;
        rebuildNames();
      }
      saved_.driversAt = now;
      p.changed = true;
      break;
    }
    case Ask::result: {
      std::vector<int> top = parseResult(body);
      if (!top.empty()) {
        saved_.results[std::to_string(p.key)] = std::move(top);
        p.changed = true;
      }
      break;
    }
  }
}

void RaceData::finish(Seconds now) {
  Pass& p = *pass_;
  if (p.failed) nextAllowed_ = now + kFailurePause;
  // Only this weekend's results are kept.
  std::set<std::string> keys;
  for (const Session& s : saved_.sessions) keys.insert(std::to_string(s.key));
  auto prune = [&keys](auto& map) {
    for (auto it = map.begin(); it != map.end();) it = keys.count(it->first) ? std::next(it) : map.erase(it);
  };
  prune(saved_.results);
  prune(saved_.resultTried);
  prune(saved_.resultTries);
  if (p.changed) revision_ += 1;
  pass_.reset();
}

std::optional<sources::Request> RaceData::refreshNascarIfNeeded(Seconds now) {
  if (nascarRunning_ || now < nascarNextAllowed_) return std::nullopt;
  if (nascar_.fetchedAt && now - *nascar_.fetchedAt < kNascarMaxAge && !nascar_.drivers.empty()) return std::nullopt;
  nascarRunning_ = true;
  return nascarRequest();
}

void RaceData::onNascarResponse(int status, std::string_view body, Seconds now) {
  onNascarParsed(status == 200 ? parseNascar(body) : std::vector<RaceDriver>{}, now);
}

void RaceData::onNascarParsed(std::vector<RaceDriver> drivers, Seconds now) {
  nascarRunning_ = false;
  if (drivers.empty()) {
    nascarNextAllowed_ = now + kNascarFailurePause;
    return;
  }
  nascar_.drivers = std::move(drivers);
  nascar_.fetchedAt = now;
  rebuildNascarNames();
  revision_ += 1;
}

// MARK: Board

std::vector<WantedSession> weekendSessions(const std::vector<SportsEvent>& parts) {
  std::vector<WantedSession> out;
  for (const SportsEvent& part : parts) {
    if (!part.start || !part.game || !part.game->cardKey) continue;
    size_t cut = part.game->cardKey->rfind('|');
    if (cut == std::string::npos) continue;
    out.push_back(WantedSession{part.game->cardKey->substr(cut + 1), *part.start});
  }
  return out;
}

bool refreshForBoard(RaceData& race, const board::Board& board, bool showsScores, Seconds now) {
  for (const std::string& id : board.watchableRaceWeekends) {
    auto parts = board.cards.find(id);
    if (parts == board.cards.end()) continue;
    if (race.refreshIfNeeded(weekendSessions(parts->second), showsScores, now)) return true;
  }
  return false;
}

}  // namespace marigold::sports::race_data
