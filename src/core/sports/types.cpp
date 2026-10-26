#include "core/sports/types.hpp"

#include <algorithm>

#include "core/sports/text.hpp"

namespace marigold::sports {

// MARK: SportsTeam

std::string SportsTeam::abbreviation() const {
  if (shortName && !shortName->empty() && text::count(*shortName) <= 4) return str::upper(*shortName);
  std::vector<std::string> words;
  for (const std::string& w : text::words(name)) {
    if (text::startsWithLetter(w)) words.push_back(w);
  }
  if (words.size() >= 2) {
    std::string out;
    for (size_t i = 0; i < words.size() && i < 3; ++i) out += text::firstCharacter(words[i]);
    return str::upper(out);
  }
  return str::upper(text::prefix(name, 3));
}

bool SportsTeam::operator==(const SportsTeam& o) const {
  return id == o.id && name == o.name && shortName == o.shortName && alternates == o.alternates &&
         badge == o.badge && location == o.location && color == o.color && altColor == o.altColor &&
         rank == o.rank && country == o.country;
}

// MARK: SportsGame

std::string SportsGame::title() const {
  if (customTitle) return *customTitle;
  return isSoccer ? home.name + " vs " + away.name : away.name + " at " + home.name;
}

bool SportsGame::operator==(const SportsGame& o) const {
  return id == o.id && league == o.league && leagueShort == o.leagueShort && isSoccer == o.isSoccer &&
         weekLabel == o.weekLabel && home == o.home && away == o.away && start == o.start &&
         networks == o.networks && venue == o.venue && postponed == o.postponed &&
         completed == o.completed && customTitle == o.customTitle && cardKey == o.cardKey &&
         note == o.note && racing == o.racing && tennis == o.tennis;
}

// MARK: LivePhase / LiveStatus

const char* rawValue(LivePhase p) {
  switch (p) {
    case LivePhase::pre: return "pre";
    case LivePhase::live: return "live";
    case LivePhase::delayed: return "delayed";
    case LivePhase::final: return "final";
    case LivePhase::postponed: return "postponed";
  }
  return "pre";
}

std::optional<LivePhase> livePhaseFromRaw(std::string_view raw) {
  if (raw == "pre") return LivePhase::pre;
  if (raw == "live") return LivePhase::live;
  if (raw == "delayed") return LivePhase::delayed;
  if (raw == "final") return LivePhase::final;
  if (raw == "postponed") return LivePhase::postponed;
  return std::nullopt;
}

bool LiveStatus::operator==(const LiveStatus& o) const {
  return phase == o.phase && detail == o.detail && homeScore == o.homeScore && awayScore == o.awayScore &&
         possession == o.possession && situation == o.situation && redZone == o.redZone &&
         bases == o.bases && lastPlay == o.lastPlay && boutsDone == o.boutsDone &&
         boutsTotal == o.boutsTotal && boutA == o.boutA && boutB == o.boutB &&
         boutACountry == o.boutACountry && boutBCountry == o.boutBCountry && homeSets == o.homeSets &&
         awaySets == o.awaySets;
}

// MARK: SportsEvent

bool SportsEvent::operator==(const SportsEvent& o) const {
  return id == o.id && sources == o.sources && game == o.game && listingEnd == o.listingEnd &&
         teams == o.teams && feedsEndedAt == o.feedsEndedAt && moreStations == o.moreStations &&
         live == o.live;
}

SportsEvent SportsEvent::withId(const std::string& newID) const {
  if (newID == id) return *this;
  SportsEvent copy;
  copy.id = newID;
  copy.title = title;
  copy.league = league;
  copy.leagueShort = leagueShort;
  copy.start = start;
  copy.game = game;
  copy.sources = sources;
  copy.family = family;
  copy.listingEnd = listingEnd;
  copy.teams = teams;
  copy.feedsEndedAt = feedsEndedAt;
  copy.moreStations = moreStations;
  return copy;
}

SportsEvent SportsEvent::retitled(const std::string& newLeague, const std::string& shortName) const {
  SportsEvent copy = withId(id);
  copy.league = newLeague;
  copy.leagueShort = shortName;
  copy.live.reset();
  return copy;
}

std::vector<EventSource> SportsEvent::channels() const {
  std::vector<EventSource> out;
  for (const EventSource& s : sources) {
    if (s.kind != EventSource::Kind::provider) out.push_back(s);
  }
  return out;
}

std::vector<EventSource> SportsEvent::feeds() const {
  std::vector<EventSource> out;
  for (const EventSource& s : sources) {
    if (s.kind == EventSource::Kind::provider) out.push_back(s);
  }
  return out;
}

bool SportsEvent::hasFeeds() const {
  return std::any_of(sources.begin(), sources.end(),
                     [](const EventSource& s) { return s.kind == EventSource::Kind::provider; });
}

Seconds SportsEvent::expectedLength() const {
  const std::string& l = league;
  if (l == "NFL" || l == "College Football" || l == "Football") return 12'960;  // 3.6 h
  if (l == "MLB" || l == "Baseball") return 11'160;                              // 3.1 h
  if (l == "NBA" || l == "WNBA" || l == "NHL" || l == "College Basketball" || l == "Basketball" ||
      l == "Hockey") {
    return 9'360;  // 2.6 h
  }
  if (l == "Soccer" || l == "MLS") return 7'560;  // 2.1 h
  if (l == "Fighting") {
    // A card's prelims run about 2 h, its main card about 3.
    if (game && game->cardKey) {
      const std::string& card = *game->cardKey;
      if (str::endsWith(card, "|early") || str::endsWith(card, "|prelims")) return 9'000;
      if (str::endsWith(card, "|main")) return 4 * 3600;
    }
    return 5 * 3600;
  }
  if (l == "Motorsport") {
    // A NASCAR Cup race runs 3-4 hours; a race weekend session its own length.
    if (game && game->leagueShort == "NASCAR") return 13'500;  // 3.75 h
    if (game && game->isRacing() && game->cardKey) {
      size_t cut = game->cardKey->rfind('|');
      if (cut != std::string::npos) return race_session::length(game->cardKey->substr(cut + 1));
    }
    return 4 * 3600;
  }
  if (l == "Golf") return 4 * 3600;
  return 9'000;  // 2.5 h
}

SportsEvent::State SportsEvent::state(Seconds now) const {
  if (game && game->postponed) return State::postponed;
  // The scoreboard's word, while it's believable: a game still "live" half
  // a day after its start falls back to the usual timing.
  if (live) {
    bool stale = start && now - *start > std::max<Seconds>(12 * 3600, expectedLength() * 2);
    switch (*live) {
      case LivePhase::postponed: return State::postponed;
      case LivePhase::live:
      case LivePhase::delayed:
        if (!stale) return State::live;
        break;
      case LivePhase::final: return State::finished;
      case LivePhase::pre:
        // Racing statuses can lag behind the green flag: past its start,
        // the clock decides.
        if (start && !stale && !(game && game->isRacing() && now >= *start)) return State::upcoming;
        break;
    }
  }
  if (!start) return State::unknownTime;
  if (now < *start) return State::upcoming;
  if (game && game->completed) return State::finished;
  if (feedsEndedAt && now >= *feedsEndedAt) return State::finished;
  Seconds end = *start + expectedLength();
  if (listingEnd && *listingEnd > end && *listingEnd < *start + 8 * 3600) end = *listingEnd;
  if (now < end) return State::live;
  if (now < end + 30 * 60) return State::mayHaveEnded;
  return State::finished;
}

double SportsEvent::progress(Seconds now) const {
  if (!start) return 0;
  double gone = static_cast<double>(now - *start) / static_cast<double>(expectedLength());
  return std::min(1.0, std::max(0.0, gone));
}

const char* rawValue(SportsEvent::State s) {
  switch (s) {
    case SportsEvent::State::upcoming: return "upcoming";
    case SportsEvent::State::live: return "live";
    case SportsEvent::State::mayHaveEnded: return "mayHaveEnded";
    case SportsEvent::State::finished: return "finished";
    case SportsEvent::State::postponed: return "postponed";
    case SportsEvent::State::unknownTime: return "unknownTime";
  }
  return "upcoming";
}

// MARK: Race sessions

namespace race_session {

std::string kind(std::string_view type) {
  std::string value = str::lower(type);
  auto has = [&](const char* w) { return value.find(w) != std::string::npos; };
  if (has("fp1") || has("practice 1")) return "fp1";
  if (has("fp2") || has("practice 2")) return "fp2";
  if (has("fp3") || has("practice 3")) return "fp3";
  if (has("sprint") && (has("qual") || has("shoot"))) return "sq";
  if (value == "sq" || value == "ss") return "sq";
  if (value == "sr" || value == "spr") return "sprint";
  if (has("sprint")) return "sprint";
  if (has("qual")) return "qual";
  if (has("race") || has("grand prix")) return "race";
  return value.empty() ? "session" : value;
}

std::string name(std::string_view kind) {
  if (kind == "fp1") return "Practice 1";
  if (kind == "fp2") return "Practice 2";
  if (kind == "fp3") return "Practice 3";
  if (kind == "sq") return "Sprint Qualifying";
  if (kind == "sprint") return "Sprint";
  if (kind == "qual") return "Qualifying";
  if (kind == "race") return "Race";
  return catalog::text::capitalized(kind);
}

std::string shortName(std::string_view kind) {
  if (kind == "fp1" || kind == "fp2" || kind == "fp3") return str::upper(kind);
  if (kind == "sq") return "SQ";
  if (kind == "sprint") return "Sprint";
  if (kind == "qual") return "Quali";
  if (kind == "race") return "Race";
  return catalog::text::capitalized(kind);
}

Seconds length(std::string_view kind) {
  if (kind == "race") return 8'280;                  // 2.3 h
  if (kind == "qual" || kind == "sq") return 4'500;  // 1.25 h
  return 3'960;                                      // 1.1 h
}

std::optional<std::string> kindInTitle(std::string_view title) {
  std::string value = " " + str::lower(title) + " ";
  auto has = [&](const std::string& w) { return value.find(w) != std::string::npos; };
  if (has("sprint qualifying") || has("sprint shootout") || has("sprint quali")) return "sq";
  if (has(" sprint ")) return "sprint";
  if (has("qualifying") || has(" quali ")) return "qual";
  for (int number = 1; number <= 3; ++number) {
    std::string n = std::to_string(number);
    if (has("practice " + n) || has(" fp" + n + " ")) return "fp" + n;
  }
  if (has(" race ") || has(" grand prix ") || has(" gp ")) return "race";
  return std::nullopt;
}

}  // namespace race_session

// MARK: JSON

namespace {

void setOpt(json::Value& v, const char* key, const std::optional<std::string>& s) {
  if (s) v.set(key, *s);
}
void setOpt(json::Value& v, const char* key, const std::optional<int>& n) {
  if (n) v.set(key, *n);
}
void setOpt(json::Value& v, const char* key, const std::optional<Seconds>& n) {
  if (n) v.set(key, static_cast<int64_t>(*n));
}
void setOpt(json::Value& v, const char* key, const std::optional<bool>& b) {
  if (b) v.set(key, *b);
}

std::optional<std::string> optString(const json::Value& v) {
  if (!v.isString()) return std::nullopt;
  return v.asString();
}
std::optional<int> optInt(const json::Value& v) {
  if (!v.isNumber()) return std::nullopt;
  return static_cast<int>(v.asInt());
}
std::optional<Seconds> optSeconds(const json::Value& v) {
  if (!v.isNumber()) return std::nullopt;
  return v.asInt();
}
std::optional<bool> optBool(const json::Value& v) {
  if (!v.isBool()) return std::nullopt;
  return v.asBool();
}
std::vector<std::string> strings(const json::Value& v) {
  std::vector<std::string> out;
  for (const json::Value& item : v.items()) {
    if (item.isString()) out.push_back(item.asString());
  }
  return out;
}
json::Value stringArray(const std::vector<std::string>& list) {
  json::Value out = json::Value::array();
  for (const std::string& s : list) out.push(s);
  return out;
}

}  // namespace

json::Value toJson(const SportsTeam& t) {
  json::Value v = json::Value::object();
  v.set("id", t.id);
  v.set("name", t.name);
  setOpt(v, "short", t.shortName);
  v.set("alternates", stringArray(t.alternates));
  setOpt(v, "badge", t.badge);
  setOpt(v, "location", t.location);
  setOpt(v, "color", t.color);
  setOpt(v, "altColor", t.altColor);
  setOpt(v, "rank", t.rank);
  setOpt(v, "country", t.country);
  return v;
}

std::optional<SportsTeam> teamFromJson(const json::Value& v) {
  if (!v["id"].isString() || !v["name"].isString()) return std::nullopt;
  SportsTeam t;
  t.id = v["id"].asString();
  t.name = v["name"].asString();
  t.shortName = optString(v["short"]);
  t.alternates = strings(v["alternates"]);
  t.badge = optString(v["badge"]);
  t.location = optString(v["location"]);
  t.color = optString(v["color"]);
  t.altColor = optString(v["altColor"]);
  t.rank = optInt(v["rank"]);
  t.country = optString(v["country"]);
  return t;
}

json::Value toJson(const SportsGame& g) {
  json::Value v = json::Value::object();
  v.set("id", g.id);
  v.set("league", g.league);
  v.set("leagueShort", g.leagueShort);
  v.set("isSoccer", g.isSoccer);
  setOpt(v, "weekLabel", g.weekLabel);
  v.set("home", toJson(g.home));
  v.set("away", toJson(g.away));
  v.set("start", static_cast<int64_t>(g.start));
  v.set("networks", stringArray(g.networks));
  setOpt(v, "venue", g.venue);
  v.set("postponed", g.postponed);
  v.set("completed", g.completed);
  setOpt(v, "customTitle", g.customTitle);
  setOpt(v, "cardKey", g.cardKey);
  setOpt(v, "note", g.note);
  setOpt(v, "racing", g.racing);
  setOpt(v, "tennis", g.tennis);
  return v;
}

std::optional<SportsGame> gameFromJson(const json::Value& v) {
  auto home = teamFromJson(v["home"]);
  auto away = teamFromJson(v["away"]);
  if (!v["id"].isString() || !v["league"].isString() || !v["leagueShort"].isString() || !home ||
      !away || !v["start"].isNumber()) {
    return std::nullopt;
  }
  SportsGame g;
  g.id = v["id"].asString();
  g.league = v["league"].asString();
  g.leagueShort = v["leagueShort"].asString();
  g.isSoccer = v["isSoccer"].asBool();
  g.weekLabel = optString(v["weekLabel"]);
  g.home = std::move(*home);
  g.away = std::move(*away);
  g.start = v["start"].asInt();
  g.networks = strings(v["networks"]);
  g.venue = optString(v["venue"]);
  g.postponed = v["postponed"].asBool();
  g.completed = v["completed"].asBool();
  g.customTitle = optString(v["customTitle"]);
  g.cardKey = optString(v["cardKey"]);
  g.note = optString(v["note"]);
  g.racing = optBool(v["racing"]);
  g.tennis = optBool(v["tennis"]);
  return g;
}

json::Value toJson(const EventSource& s) {
  json::Value v = json::Value::object();
  v.set("channelID", s.channelID);
  v.set("kind", static_cast<int>(s.kind));
  v.set("spanish", s.spanish);
  v.set("detail", s.detail);
  setOpt(v, "rank", s.rank);
  return v;
}

std::optional<EventSource> sourceFromJson(const json::Value& v) {
  int64_t kind = v["kind"].asInt(-1);
  if (!v["channelID"].isString() || !v["kind"].isNumber() || kind < 0 || kind > 3) return std::nullopt;
  EventSource s;
  s.channelID = v["channelID"].asString();
  s.kind = static_cast<EventSource::Kind>(kind);
  s.spanish = v["spanish"].asBool();
  s.detail = v["detail"].asString();
  s.rank = optInt(v["rank"]);
  return s;
}

json::Value toJson(const SportsEvent& e) {
  json::Value v = json::Value::object();
  v.set("id", e.id);
  v.set("title", e.title);
  v.set("league", e.league);
  v.set("leagueShort", e.leagueShort);
  setOpt(v, "start", e.start);
  if (e.game) v.set("game", toJson(*e.game));
  json::Value sources = json::Value::array();
  for (const EventSource& s : e.sources) sources.push(toJson(s));
  v.set("sources", sources);
  setOpt(v, "family", e.family);
  setOpt(v, "listingEnd", e.listingEnd);
  if (e.teams) {
    json::Value teams = json::Value::array();
    for (const SportsTeam& t : *e.teams) teams.push(toJson(t));
    v.set("teams", teams);
  }
  setOpt(v, "feedsEndedAt", e.feedsEndedAt);
  setOpt(v, "moreStations", e.moreStations);
  if (e.live) v.set("live", rawValue(*e.live));
  return v;
}

std::optional<SportsEvent> eventFromJson(const json::Value& v) {
  if (!v["id"].isString() || !v["title"].isString() || !v["league"].isString() ||
      !v["leagueShort"].isString()) {
    return std::nullopt;
  }
  SportsEvent e;
  e.id = v["id"].asString();
  e.title = v["title"].asString();
  e.league = v["league"].asString();
  e.leagueShort = v["leagueShort"].asString();
  e.start = optSeconds(v["start"]);
  if (v.has("game")) {
    e.game = gameFromJson(v["game"]);
    if (!e.game) return std::nullopt;
  }
  for (const json::Value& item : v["sources"].items()) {
    auto s = sourceFromJson(item);
    if (!s) return std::nullopt;
    e.sources.push_back(std::move(*s));
  }
  e.family = optString(v["family"]);
  e.listingEnd = optSeconds(v["listingEnd"]);
  if (v["teams"].isArray()) {
    std::vector<SportsTeam> teams;
    for (const json::Value& item : v["teams"].items()) {
      if (auto t = teamFromJson(item)) teams.push_back(std::move(*t));
    }
    e.teams = std::move(teams);
  }
  e.feedsEndedAt = optSeconds(v["feedsEndedAt"]);
  e.moreStations = optInt(v["moreStations"]);
  if (v["live"].isString()) e.live = livePhaseFromRaw(v["live"].asString());
  return e;
}

}  // namespace marigold::sports
