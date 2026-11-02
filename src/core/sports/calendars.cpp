#include "core/sports/calendars.hpp"

#include <algorithm>
#include <cctype>

#include "core/sports/parse.hpp"
#include "core/sports/profiles.hpp"
#include "core/sports/refusals.hpp"
#include "core/sports/text.hpp"
#include "core/util/strings.hpp"

namespace marigold::sports::calendars {

using time::Seconds;

namespace {

// Saved games older than this are dropped (the scoreboard's kSavedFor).
constexpr Seconds kKeptFor = 7 * 86'400;
// Round records older than this are forgotten.
constexpr Seconds kRoundsKeptFor = 30 * 86'400;
// After errors: 1 h, 1 h, 6 h, 24 h, then weekly (as the scoreboard).
constexpr Seconds kBackoff[] = {3600, 3600, 6 * 3600, 24 * 3600, 7 * 86'400};
// Errors in one pass before it gives up for the day.
constexpr int kErrorsPerPass = 3;

Calendar make(const char* key, const char* shortName, const char* name, const char* group, Calendar::Kind kind,
              const char* pick, std::vector<std::string> keywords, int hour = 18) {
  Calendar c;
  c.key = key;
  c.shortName = shortName;
  c.name = name;
  c.group = group;
  c.kind = kind;
  c.pick = pick;
  c.keywords = std::move(keywords);
  c.dateOnlyHourUtc = hour;
  return c;
}

bool isWordChar(unsigned char c) { return std::isalnum(c) || c >= 0x80; }

// A keyword as whole words of a lower-case title.
bool hasWords(std::string_view title, std::string_view keyword) {
  for (size_t at = title.find(keyword); at != std::string_view::npos; at = title.find(keyword, at + 1)) {
    bool before = at == 0 || !isWordChar(static_cast<unsigned char>(title[at - 1]));
    size_t end = at + keyword.size();
    bool after = end >= title.size() || !isWordChar(static_cast<unsigned char>(title[end]));
    if (before && after) return true;
  }
  return false;
}

std::string field(const json::Value& event, const char* name) { return text::text(event[name]).value_or(""); }

std::string twoDigits(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }

std::string isoDayUtc(Seconds t) {
  time::Civil c = time::civilFromSeconds(t);
  return std::to_string(c.year) + "-" + twoDigits(c.month) + "-" + twoDigits(c.day);
}

Seconds backoffFor(int failures) { return kBackoff[std::min(std::max(failures, 0), 4)]; }

int requestsToday(const Saved& saved, Seconds now, const time::Zone& zone) {
  return saved.requestDay == text::isoDay(now, zone) ? saved.requestCount : 0;
}

}  // namespace

const std::vector<Calendar>& calendars() {
  using K = Calendar::Kind;
  static const std::vector<Calendar> kAll = [] {
    std::vector<Calendar> v;
    // Soccer: any soccer league picked (the European cups and every-game
    // listing come with it too).
    v.push_back(make("4617", "ALB", "Kategoria Superiore", "Soccer", K::teams, "soccer",
                     {"kategoria superiore", "superliga - albania", "superliga albania", "abissnet superiore"}));
    v.push_back(make("5247", "MTN", "Super D1", "Soccer", K::teams, "soccer",
                     {"premier league - mauritania", "premier league mauritania", "super d-1", "super d1"}));
    v.push_back(make("4422", "POL", "Ekstraklasa", "Soccer", K::teams, "soccer", {"ekstraklasa"}));
    v.push_back(make("4403", "SWE2", "Superettan", "Soccer", K::teams, "soccer", {"superettan"}));
    v.push_back(make("5088", "ESP3", "Primera Federaci\xC3\xB3n", "Soccer", K::teams, "soccer",
                     {"primera federaci\xC3\xB3n", "primera federacion", "1\xC2\xAA rfef", "1a rfef", "primera rfef"}));
    v.push_back(make("5235", "BDI", "Ligue A", "Soccer", K::teams, "soccer", {"burundi"}));
    v.push_back(make("5656", "LTU", "A Lyga", "Soccer", K::teams, "soccer", {"a lyga", "a-lyga"}));
    // Hockey with the NHL picked (as Division III hockey).
    v.push_back(make("4930", "DEN", "Metal Ligaen", "Hockey", K::teams, "NHL", {"metal ligaen", "metalligaen"}));
    v.push_back(make("5162", "SWEH2", "HockeyAllsvenskan", "Hockey", K::teams, "NHL",
                     {"hockeyallsvenskan", "hockey allsvenskan"}));
    // Racing: every session of the round.
    v.push_back(make("4413", "WEC", "WEC", "Motorsport", K::sessions, "S:Motorsport",
                     {"wec", "world endurance", "fia wec"}));
    Calendar motogp = make("4407", "MOTOGP", "MotoGP", "Motorsport", K::sessions, "S:Motorsport", {"motogp", "moto gp"});
    motogp.weekendSuffix = " GP";
    v.push_back(motogp);
    v.push_back(make("4489", "SC", "Supercars", "Motorsport", K::sessions, "S:Motorsport",
                     {"supercars", "v8 supercars"}));
    // One event a round, often with no time: the day at an hour that suits.
    v.push_back(make("4554", "PDC", "PDC", "Darts", K::titles, "S:Darts", {"pdc", "darts"}, 12));
    v.push_back(make("4445", "BOX", "Boxing", "Fighting", K::cards, "S:Fighting", {"boxing", "boxeo"}, 19));
    v.push_back(make("5702", "OKT", "Oktagon", "Fighting", K::cards, "S:Fighting", {"oktagon"}, 17));
    Calendar ufc = make("4443", "MMA", "UFC", "Fighting", K::cards, "S:Fighting", {"ufc"}, 23);
    ufc.sameAs = "UFC";
    v.push_back(ufc);
    v.push_back(make("4405", "CFL", "CFL", "Football", K::teams, "NFL|NCAAF", {"cfl", "grey cup", "canadian football"}));
    // Snooker has no pick: seen is enough (one request a day).
    v.push_back(make("snooker", "SNOOKER", "Snooker", "Snooker", K::day, "", {"snooker"}, 10));
    return v;
  }();
  return kAll;
}

const Calendar* byKey(std::string_view key) {
  for (const Calendar& c : calendars())
    if (c.key == key) return &c;
  return nullptr;
}

const Calendar* byShort(std::string_view shortName) {
  for (const Calendar& c : calendars())
    if (c.shortName == shortName) return &c;
  return nullptr;
}

bool isCalendarGame(const SportsGame& game) { return byShort(game.leagueShort) != nullptr; }

std::vector<const Calendar*> active(const MySportsPrefs& prefs, const SportFeedsSeen& seen, Seconds now,
                                    const std::set<std::string>& covered) {
  std::set<std::string> picked(prefs.leagues.begin(), prefs.leagues.end());
  bool soccer = std::any_of(leagues().begin(), leagues().end(),
                            [&](const League& l) { return l.soccer && picked.count(l.shortName); });
  std::vector<const Calendar*> out;
  for (const Calendar& c : calendars()) {
    if (!seen.calendarSeen(c.key, now) || covered.count(c.key)) continue;
    bool relevant = !prefs.configured || c.pick.empty();
    for (std::string_view p : str::split(c.pick, '|')) {
      if (relevant) break;
      relevant = p == "soccer" ? soccer : picked.count(std::string(p)) > 0;
    }
    if (relevant) out.push_back(&c);
  }
  return out;
}

std::set<std::string> coveredBy(const std::set<std::string>& scoreboard, const std::set<std::string>& competitions) {
  std::set<std::string> out;
  for (const Calendar& c : calendars()) {
    if (!c.sameAs.empty() && scoreboard.count(c.sameAs)) {
      out.insert(c.key);
      continue;
    }
    if (c.group != "Soccer") continue;
    for (const std::string& name : competitions) {
      if (std::any_of(c.keywords.begin(), c.keywords.end(), [&](const std::string& k) { return hasWords(name, k); })) {
        out.insert(c.key);
        break;
      }
    }
  }
  return out;
}

std::set<std::string> namedIn(const std::vector<catalog::LiveEvent>& events) {
  // Feeds of sports none of these are.
  static const std::set<std::string> kOther = {"NFL",      "NBA",        "WNBA",         "MLB",      "MiLB",
                                               "College Football", "College Basketball", "Tennis", "Golf",
                                               "Cricket",  "Rugby",      "Volleyball",   "Basketball", "Baseball",
                                               "Softball", "Lacrosse",   "Field Hockey", "Wrestling"};
  std::set<std::string> found;
  const size_t all = calendars().size();
  std::string lower;
  for (const catalog::LiveEvent& e : events) {
    if (found.size() == all) break;
    if (e.title.empty() || kOther.count(e.league)) continue;
    lower = str::lower(e.title);
    for (const Calendar& c : calendars()) {
      if (found.count(c.key)) continue;
      for (const std::string& k : c.keywords) {
        if (hasWords(lower, k)) {
          found.insert(c.key);
          break;
        }
      }
    }
  }
  return found;
}

// MARK: Requests

namespace {

sources::Request get(std::string path) {
  sources::Request r;
  r.url = std::string(kApiBase) + path;
  r.timeoutSeconds = kTimeoutSeconds;
  return r;
}

std::string encoded(std::string_view s) {
  std::string out;
  for (unsigned char ch : s) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
      out += static_cast<char>(ch);
    } else {
      static const char* kHex = "0123456789ABCDEF";
      out += '%';
      out += kHex[ch >> 4];
      out += kHex[ch & 15];
    }
  }
  return out;
}

}  // namespace

sources::Request nextRequest(const Calendar& c) { return get("eventsnextleague.php?id=" + c.key); }

sources::Request roundRequest(const Calendar& c, int round, std::string_view season) {
  return get("eventsround.php?id=" + c.key + "&r=" + std::to_string(round) + "&s=" + encoded(season));
}

sources::Request dayRequest(const Calendar& c, std::string_view day) {
  if (c.kind == Calendar::Kind::day) return get("eventsday.php?d=" + std::string(day) + "&s=" + encoded(c.name));
  return get("eventsday.php?d=" + std::string(day) + "&l=" + c.key);
}

// MARK: Parsing

std::optional<Seconds> timestamp(std::string_view s) {
  auto digits = [&](size_t at, size_t n) {
    if (at + n > s.size()) return false;
    for (size_t i = at; i < at + n; ++i)
      if (s[i] < '0' || s[i] > '9') return false;
    return true;
  };
  auto number = [&](size_t at, size_t n) {
    int v = 0;
    for (size_t i = at; i < at + n; ++i) v = v * 10 + (s[i] - '0');
    return v;
  };
  if (s.size() < 19 || !digits(0, 4) || s[4] != '-' || !digits(5, 2) || s[7] != '-' || !digits(8, 2) ||
      (s[10] != 'T' && s[10] != ' ') || !digits(11, 2) || s[13] != ':' || !digits(14, 2) || s[16] != ':' ||
      !digits(17, 2))
    return std::nullopt;
  std::string_view rest = s.substr(19);
  if (!rest.empty() && rest != "Z" && rest != "+00:00" && rest != "+0000") return std::nullopt;
  time::Civil c{number(0, 4), number(5, 2), number(8, 2), number(11, 2), number(14, 2), number(17, 2)};
  if (c.month < 1 || c.month > 12 || c.day < 1 || c.day > time::daysInMonth(c.year, c.month) || c.hour > 23 ||
      c.minute > 59 || c.second > 59)
    return std::nullopt;
  return time::secondsFromCivil(c);
}

std::pair<std::string, std::string> weekendAndSession(std::string_view event) {
  static const char* const kSessions[] = {"free practice", "practice", "qualifying", "hyperpole", "sprint",
                                          "warm up",       "warmup",   "shootout",   "top 10",    "race",
                                          "gp",            "grand prix"};
  std::string lower = str::lower(event);
  size_t cut = std::string::npos;
  for (const char* s : kSessions) {
    for (size_t at = lower.find(s); at != std::string::npos; at = lower.find(s, at + 1)) {
      // A session word after the weekend's name, as whole words.
      size_t end = at + std::char_traits<char>::length(s);
      bool before = at > 0 && lower[at - 1] == ' ';
      bool after = end >= lower.size() || !isWordChar(static_cast<unsigned char>(lower[end]));
      if (before && after) {
        cut = std::min(cut, at);
        break;
      }
    }
  }
  if (cut == std::string::npos) return {std::string(str::trim(event)), ""};
  std::string weekend(str::trim(event.substr(0, cut)));
  std::string session(str::trim(event.substr(cut)));
  // "Qatar GP", "Sandown 500 Race 1": the race.
  std::string ls = str::lower(session);
  if (ls == "gp" || ls == "grand prix") session.clear();
  return {weekend, session};
}

std::optional<std::pair<std::string, std::string>> fighters(std::string_view card) {
  std::string name(card);
  size_t vs = str::lower(name).find(" vs ");
  if (vs == std::string::npos) return std::nullopt;
  std::string left = name.substr(0, vs), right = name.substr(vs + 4);
  // "UFC Fight Night 293 Bonfim": the words after the card's number.
  std::vector<std::string_view> words = str::split(left, ' ');
  size_t from = 0;
  for (size_t i = 0; i < words.size(); ++i) {
    if (!words[i].empty() && std::all_of(words[i].begin(), words[i].end(), [](char ch) { return ch >= '0' && ch <= '9'; }))
      from = i + 1;
  }
  if (from > 0 && from < words.size()) {
    std::vector<std::string> kept;
    for (size_t i = from; i < words.size(); ++i) kept.emplace_back(words[i]);
    left = str::join(kept, " ");
  }
  // "Joshua Buatsi vs Willy Hutchinson 2": a rematch's number isn't a name.
  std::vector<std::string_view> tail = str::split(right, ' ');
  if (tail.size() > 1 && std::all_of(tail.back().begin(), tail.back().end(), [](char ch) { return ch >= '0' && ch <= '9'; })) {
    tail.pop_back();
    std::vector<std::string> kept(tail.begin(), tail.end());
    right = str::join(kept, " ");
  }
  std::string a(str::trim(left)), b(str::trim(right));
  if (a.empty() || b.empty()) return std::nullopt;
  return std::make_pair(a, b);
}

namespace {

bool finished(const std::string& status) {
  static const std::set<std::string> kDone = {"FT",  "AET", "AP",  "AOT",           "PEN",     "FIN",
                                              "AWD", "WO",  "FTR", "Match Finished", "Finished"};
  return kDone.count(status) > 0;
}

bool called(const std::string& status) {
  static const std::set<std::string> kOff = {"CANC", "ABD", "Cancelled", "Canceled", "Abandoned"};
  return kOff.count(status) > 0;
}

SportsTeam side(std::string id, std::string name) {
  SportsTeam t;
  t.id = std::move(id);
  t.name = std::move(name);
  return t;
}

std::optional<SportsGame> gameOf(const json::Value& event, const Calendar& c) {
  std::string id = field(event, "idEvent");
  std::string name = field(event, "strEvent");
  if (id.empty() || name.empty()) return std::nullopt;
  std::string status = field(event, "strStatus");
  if (called(status)) return std::nullopt;
  std::string clock = field(event, "strTime");
  std::optional<Seconds> start = timestamp(field(event, "strTimestamp"));
  std::string day = field(event, "dateEvent");
  if (!start && !day.empty()) start = timestamp(day + "T" + (clock.size() >= 8 ? clock.substr(0, 8) : "00:00:00"));
  if (!start) return std::nullopt;
  bool titled = c.kind != Calendar::Kind::teams && c.kind != Calendar::Kind::sessions;
  // No time given (darts, fight cards): the day, at an hour that suits.
  if (titled && (clock.empty() || clock == "00:00:00") && *start % 86'400 == 0) {
    *start += static_cast<Seconds>(c.dateOnlyHourUtc) * 3600;
  }
  SportsGame g;
  g.league = c.group;
  g.leagueShort = c.shortName;
  g.homeFirst = profile(c.group).homeFirst;
  g.start = *start;
  g.completed = finished(status);
  g.postponed = str::lower(field(event, "strPostponed")) == "yes" || status == "PST" || status == "Postponed";
  std::string venue = field(event, "strVenue");
  std::string city = field(event, "strCity");
  if (!venue.empty()) g.venue = !city.empty() && venue.find(city) == std::string::npos ? venue + ", " + city : venue;
  g.note = c.name;
  switch (c.kind) {
    case Calendar::Kind::teams: {
      std::string home = field(event, "strHomeTeam"), away = field(event, "strAwayTeam");
      if (home.empty() || away.empty()) return std::nullopt;
      std::string homeID = field(event, "idHomeTeam"), awayID = field(event, "idAwayTeam");
      g.id = c.shortName + "|" + id;
      g.home = side(homeID.empty() ? "n" + home : homeID, home);
      g.away = side(awayID.empty() ? "n" + away : awayID, away);
      break;
    }
    case Calendar::Kind::sessions: {
      auto [weekend, session] = weekendAndSession(name);
      if (weekend.empty()) weekend = name;
      std::string shown = weekend;
      if (!c.weekendSuffix.empty() && !str::endsWith(str::lower(shown), str::lower(c.weekendSuffix)))
        shown += c.weekendSuffix;
      std::string round = field(event, "intRound");
      std::string season = field(event, "strSeason");
      std::string weekendID = season + "-" + (round.empty() ? id : round);
      g.id = c.shortName + "|" + weekendID + "|" + id;
      g.home = side("r" + c.shortName + weekendID, shown);
      g.away = g.home;
      std::string kind = session.empty() ? std::string("race") : race_session::kind(session);
      g.customTitle = shown + " \xC2\xB7 " + (session.empty() ? std::string("Race") : session);
      g.cardKey = str::lower(c.shortName) + "|" + weekendID + "|" + kind;
      g.racing = true;
      break;
    }
    case Calendar::Kind::cards: {
      g.id = c.shortName + "|" + id;
      if (auto pair = fighters(name)) {
        g.home = side("f" + id + "a", pair->first);
        g.away = side("f" + id + "b", pair->second);
      } else {
        g.home = side("t" + id, name);
        g.away = g.home;
      }
      g.customTitle = name;
      g.cardKey = str::lower(c.shortName) + "|" + id + "|main";
      break;
    }
    case Calendar::Kind::titles:
    case Calendar::Kind::day: {
      g.id = c.shortName + "|" + id;
      g.home = side("t" + id, name);
      g.away = g.home;
      g.customTitle = name;
      if (c.kind == Calendar::Kind::day) {
        std::string league = field(event, "strLeague");
        if (!league.empty()) g.note = league;
      }
      break;
    }
  }
  return g;
}

}  // namespace

std::optional<Parsed> parse(std::string_view body, const Calendar& c) {
  Parsed out;
  bool first = true;
  bool found = parse::forEachEvent(body, [&](const json::Value& event) {
    if (!event.isObject()) return;
    if (first) {
      first = false;
      if (auto season = text::text(event["strSeason"])) out.season = *season;
      if (auto round = text::text(event["intRound"])) out.round = text::toInt(*round);
    }
    if (auto g = gameOf(event, c)) out.games.push_back(std::move(*g));
  });
  if (found) return out;
  // Nothing on: {"events": null} (or "event"), a few bytes.
  if (body.size() < 256) {
    auto doc = json::parse(body);
    if (doc && doc->isObject() && (doc->has("events") || doc->has("event"))) return Parsed();
  }
  return std::nullopt;
}

// MARK: Saved

namespace {

json::Value gamesJson(const std::vector<SportsGame>& games) {
  json::Value list = json::Value::array();
  for (const SportsGame& g : games) list.push(toJson(g));
  return list;
}

}  // namespace

json::Value toJson(const Saved& s) {
  json::Value v = json::Value::object();
  v.set("schema", kSchema);
  json::Value leagues = json::Value::object();
  for (const auto& [key, st] : s.leagues) {
    json::Value l = json::Value::object();
    if (!st.season.empty()) l.set("season", st.season);
    if (st.round) l.set("round", *st.round);
    if (st.checkedAt) l.set("checkedAt", static_cast<int64_t>(*st.checkedAt));
    json::Value rounds = json::Value::object();
    for (const auto& [rk, at] : st.rounds) rounds.set(rk, static_cast<int64_t>(at));
    l.set("rounds", rounds);
    json::Value buckets = json::Value::object();
    for (const auto& [name, b] : st.buckets) {
      json::Value bv = json::Value::object();
      bv.set("fetchedAt", static_cast<int64_t>(b.fetchedAt));
      bv.set("games", gamesJson(b.games));
      buckets.set(name, bv);
    }
    l.set("buckets", buckets);
    leagues.set(key, l);
  }
  v.set("leagues", leagues);
  if (s.requestDay) v.set("requestDay", *s.requestDay);
  v.set("requestCount", s.requestCount);
  if (s.failedAt) v.set("failedAt", static_cast<int64_t>(*s.failedAt));
  v.set("failures", s.failures);
  if (s.refusedUntil) v.set("refusedUntil", static_cast<int64_t>(*s.refusedUntil));
  return v;
}

Saved savedFromJson(const json::Value& v) {
  Saved s;
  if (!v.isObject() || !v["schema"].isNumber() || v["schema"].asInt() != kSchema) return s;
  for (const auto& [key, l] : v["leagues"].fields()) {
    if (!byKey(key) || !l.isObject()) continue;
    LeagueState st;
    if (l["season"].isString()) st.season = l["season"].asString();
    if (l["round"].isNumber()) st.round = static_cast<int>(l["round"].asInt());
    if (l["checkedAt"].isNumber()) st.checkedAt = l["checkedAt"].asInt();
    for (const auto& [rk, at] : l["rounds"].fields())
      if (at.isNumber()) st.rounds[rk] = at.asInt();
    for (const auto& [name, b] : l["buckets"].fields()) {
      LeagueState::Bucket bucket;
      bucket.fetchedAt = b["fetchedAt"].asInt();
      for (const json::Value& item : b["games"].items())
        if (auto g = gameFromJson(item)) bucket.games.push_back(std::move(*g));
      st.buckets[name] = std::move(bucket);
    }
    s.leagues[key] = std::move(st);
  }
  if (v["requestDay"].isString()) s.requestDay = v["requestDay"].asString();
  s.requestCount = static_cast<int>(v["requestCount"].asInt());
  if (v["failedAt"].isNumber()) s.failedAt = v["failedAt"].asInt();
  s.failures = static_cast<int>(v["failures"].asInt());
  if (v["refusedUntil"].isNumber()) s.refusedUntil = v["refusedUntil"].asInt();
  return s;
}

std::vector<SportsGame> games(const Saved& saved, const std::vector<const Calendar*>& active, Seconds now) {
  std::vector<SportsGame> out;
  for (const Calendar* c : active) {
    auto st = saved.leagues.find(c->key);
    if (st == saved.leagues.end()) continue;
    // Oldest copy first; a newer one replaces it.
    std::vector<const LeagueState::Bucket*> order;
    for (const auto& [name, b] : st->second.buckets) order.push_back(&b);
    std::stable_sort(order.begin(), order.end(),
                     [](const LeagueState::Bucket* a, const LeagueState::Bucket* b) { return a->fetchedAt < b->fetchedAt; });
    std::map<std::string, const SportsGame*> byID;
    for (const LeagueState::Bucket* b : order)
      for (const SportsGame& g : b->games) byID[g.id] = &g;
    for (const auto& [id, g] : byID) {
      if (g->start >= now - kKeptFor && g->start <= now + kAhead) out.push_back(*g);
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const SportsGame& a, const SportsGame& b) { return a.start < b.start; });
  return out;
}

namespace {

// Old buckets and round records go; then the biggest leagues' oldest
// buckets, until under the caps.
void prune(Saved& saved, Seconds now) {
  size_t total = 0;
  for (auto& entry : saved.leagues) {
    LeagueState& st = entry.second;
    for (auto it = st.rounds.begin(); it != st.rounds.end();) {
      if (now - it->second > kRoundsKeptFor) it = st.rounds.erase(it);
      else ++it;
    }
    for (auto it = st.buckets.begin(); it != st.buckets.end();) {
      Seconds last = 0;
      for (const SportsGame& g : it->second.games) last = std::max(last, g.start);
      bool old = it->second.games.empty() ? now - it->second.fetchedAt > kRoundFreshFor : last < now - kKeptFor;
      if (old) it = st.buckets.erase(it);
      else ++it;
    }
    auto count = [&st] {
      size_t n = 0;
      for (const auto& [name, b] : st.buckets) n += b.games.size();
      return n;
    };
    // The oldest fetched first, never the last one.
    while (count() > kMaxPerLeague && st.buckets.size() > 1) {
      auto oldest = std::min_element(st.buckets.begin(), st.buckets.end(),
                                     [](const auto& a, const auto& b) { return a.second.fetchedAt < b.second.fetchedAt; });
      st.buckets.erase(oldest);
    }
    if (count() > kMaxPerLeague) {
      auto& games = st.buckets.begin()->second.games;
      games.resize(kMaxPerLeague);
    }
    total += count();
  }
  while (total > kMaxGames) {
    LeagueState::Bucket* oldest = nullptr;
    std::map<std::string, LeagueState::Bucket>* owner = nullptr;
    std::string name;
    for (auto& [key, st] : saved.leagues)
      for (auto& [n, b] : st.buckets)
        if (!oldest || b.fetchedAt < oldest->fetchedAt) {
          oldest = &b;
          owner = &st.buckets;
          name = n;
        }
    if (!oldest) break;
    total -= oldest->games.size();
    owner->erase(name);
  }
}

std::string roundKey(const std::string& season, int round) { return season + "|" + std::to_string(round); }

}  // namespace

// MARK: Pass

Pass::Pass(const Saved& saved, std::vector<const Calendar*> leagues, Seconds now, const time::Zone& zone, bool force)
    : leagues_(std::move(leagues)), now_(now), zone_(zone) {
  for (const Calendar* c : leagues_) {
    auto st = saved.leagues.find(c->key);
    bool checked = st != saved.leagues.end() && st->second.checkedAt &&
                   text::dayDifference(*st->second.checkedAt, now, zone) == 0;
    if (checked && !force) continue;
    Ask ask;
    ask.key = c->key;
    if (c->kind == Calendar::Kind::day) {
      ask.what = Ask::What::day;
      ask.day = text::isoDay(now, zone);
      ask.request = dayRequest(*c, ask.day);
    } else {
      ask.what = Ask::What::next;
      ask.request = nextRequest(*c);
    }
    queued_.insert(ask.request.url);
    first_.push_back(std::move(ask));
  }
}

bool Pass::due(const Saved& saved, const std::vector<const Calendar*>& leagues, Seconds now, const time::Zone& zone,
               bool force) {
  if (leagues.empty()) return false;
  if (saved.refusedUntil && *saved.refusedUntil > now) return false;
  if (saved.failedAt && now - *saved.failedAt < backoffFor(saved.failures)) return false;
  if (requestsToday(saved, now, zone) >= kDailyCap) return false;
  if (force) return true;
  for (const Calendar* c : leagues) {
    auto st = saved.leagues.find(c->key);
    if (st == saved.leagues.end() || !st->second.checkedAt ||
        text::dayDifference(*st->second.checkedAt, now, zone) != 0)
      return true;
  }
  return false;
}

bool Pass::pending(const Saved& saved) const {
  if (stopped_ || awaiting_) return false;
  if (saved.refusedUntil && *saved.refusedUntil > now_) return false;
  if (requestsToday(saved, now_, zone_) >= kDailyCap) return false;
  return firstIndex_ < first_.size() || laterIndex_ < later_.size();
}

std::optional<Ask> Pass::next(Saved& saved) {
  if (!pending(saved)) {
    if (!awaiting_ && (firstIndex_ < first_.size() || laterIndex_ < later_.size())) stopped_ = true;
    return std::nullopt;
  }
  Ask ask = firstIndex_ < first_.size() ? first_[firstIndex_++] : later_[laterIndex_++];
  std::string today = text::isoDay(now_, zone_);
  if (saved.requestDay != today) {
    saved.requestDay = today;
    saved.requestCount = 0;
  }
  saved.requestCount += 1;
  asked_ += 1;
  awaiting_ = ask;
  return ask;
}

bool Pass::fresh(const Saved& saved, const std::string& key, const std::string& rk) const {
  auto st = saved.leagues.find(key);
  if (st == saved.leagues.end()) return false;
  auto at = st->second.rounds.find(rk);
  return at != st->second.rounds.end() && now_ - at->second < kRoundFreshFor;
}

void Pass::queueFollowUps(const Saved& saved, const Ask& ask, const Parsed& parsed) {
  const Calendar* c = byKey(ask.key);
  if (!c) return;
  auto queueRound = [&](const std::string& season, int round, bool following) {
    if (round <= 0 || season.empty() || fresh(saved, c->key, roundKey(season, round))) return;
    Ask r;
    r.what = Ask::What::round;
    r.key = c->key;
    r.season = season;
    r.round = round;
    r.following = following;
    r.request = roundRequest(*c, round, season);
    if (!queued_.insert(r.request.url).second) return;
    later_.push_back(std::move(r));
  };
  auto queueDay = [&](Seconds day) {
    Ask d;
    d.what = Ask::What::day;
    d.key = c->key;
    d.day = isoDayUtc(day);
    d.request = dayRequest(*c, d.day);
    if (!queued_.insert(d.request.url).second) return;
    later_.push_back(std::move(d));
  };
  // The round ends within the week: the one after it too.
  auto endsSoon = [&](const std::vector<SportsGame>& games) {
    if (games.empty()) return false;
    Seconds last = 0;
    for (const SportsGame& g : games) last = std::max(last, g.start);
    return last <= now_ + kAhead;
  };
  bool rounds = c->kind == Calendar::Kind::teams || c->kind == Calendar::Kind::sessions;
  if (ask.what == Ask::What::next) {
    if (!parsed.round || !parsed.season) return;
    int round = *parsed.round;
    const std::string& season = *parsed.season;
    if (round == 0) {
      // No rounds (Swedish hockey's second tier): today's and tomorrow's
      // games (three at most each), for the teams.
      if (c->kind == Calendar::Kind::teams) {
        queueDay(now_);
        queueDay(now_ + 86'400);
      }
      return;
    }
    if (rounds) {
      if (!fresh(saved, c->key, roundKey(season, round))) {
        queueRound(season, round, false);
        return;
      }
      // The round saved is fresh: its end decides the next one.
      auto st = saved.leagues.find(c->key);
      if (st == saved.leagues.end()) return;
      auto b = st->second.buckets.find("round:" + roundKey(season, round));
      if (b != st->second.buckets.end() && endsSoon(b->second.games)) queueRound(season, round + 1, true);
      return;
    }
    // One event a round: the next one when this one is within the week.
    if (endsSoon(parsed.games)) queueRound(season, round + 1, true);
    return;
  }
  if (ask.what == Ask::What::round && !ask.following && rounds && endsSoon(parsed.games)) {
    queueRound(ask.season, ask.round + 1, true);
  }
}

void Pass::onAnswer(Saved& saved, int status, std::optional<Parsed> parsed, int retryAfterSeconds) {
  if (!awaiting_) return;
  Ask ask = std::move(*awaiting_);
  awaiting_.reset();
  if (auto until = refusals::until(status, retryAfterSeconds, now_, zone_)) {
    saved.refusedUntil = *until;
    stopped_ = true;
    return;
  }
  // No response (offline, a timeout): tried again next time, not counted.
  if (status == 0) {
    stopped_ = true;
    return;
  }
  if (status != 200 || !parsed) {
    errors_ += 1;
    if (errors_ >= kErrorsPerPass) {
      saved.failedAt = now_;
      saved.failures += 1;
      stopped_ = true;
    }
    return;
  }
  errors_ = 0;
  saved.failedAt.reset();
  saved.failures = 0;
  LeagueState& st = saved.leagues[ask.key];
  LeagueState::Bucket bucket;
  bucket.fetchedAt = now_;
  bucket.games = parsed->games;
  switch (ask.what) {
    case Ask::What::next:
      st.checkedAt = now_;
      if (parsed->season) st.season = *parsed->season;
      st.round = parsed->round;
      st.buckets["next"] = std::move(bucket);
      break;
    case Ask::What::round:
      st.rounds[roundKey(ask.season, ask.round)] = now_;
      st.buckets["round:" + roundKey(ask.season, ask.round)] = std::move(bucket);
      break;
    case Ask::What::day: {
      const Calendar* c = byKey(ask.key);
      if (c && c->kind == Calendar::Kind::day) st.checkedAt = now_;
      st.buckets["day:" + ask.day] = std::move(bucket);
      break;
    }
  }
  queueFollowUps(saved, ask, *parsed);
  prune(saved, now_);
}

}  // namespace marigold::sports::calendars
