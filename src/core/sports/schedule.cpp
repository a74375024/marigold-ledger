#include "core/sports/schedule.hpp"

#include <algorithm>
#include <unordered_set>

#include "core/sports/matcher.hpp"
#include "core/sports/parse.hpp"
#include "core/sports/profiles.hpp"
#include "core/sports/text.hpp"
#include "core/util/json.hpp"
#include "core/util/json_stream.hpp"

namespace marigold::sports::schedule {

// MARK: Requests

sources::Request scoreboardRequest(const League& league, std::string_view query, bool plainForm) {
  std::vector<std::string> parts;
  if (!query.empty()) parts.emplace_back(query);
  std::string extra = league.extra.empty() ? "" : league.extra.substr(1);
  if (!extra.empty()) parts.push_back(extra);
  std::string base = std::string(kApiBase) + league.path + "/scoreboard?" + str::join(parts, "&");
  sources::Request request;
  request.url = plainForm ? base : base + "&limit=1000";
  request.timeoutSeconds = kTimeoutSeconds;
  return request;
}

sources::Request schoolsRequest() {
  sources::Request request;
  request.url = std::string(kApiBase) + "basketball/mens-college-basketball/teams?limit=1000";
  request.timeoutSeconds = kTimeoutSeconds;
  return request;
}

sources::Request teamsRequest(const League& league) {
  sources::Request request;
  request.url = std::string(kApiBase) + league.path + "/teams?limit=1000";
  request.timeoutSeconds = kTimeoutSeconds;
  return request;
}

std::vector<std::string> queriesFor(const League& league, Seconds now, const time::Zone& zone) {
  std::vector<std::string> queries;
  switch (league.mode) {
    case League::Mode::month: {
      // Date ranges stopped working (HTTP 400); a whole month, a week number
      // or a single day still do.
      std::string first = text::yearMonth(text::addDays(now, -2, zone), zone);
      std::string last = text::yearMonth(text::addDays(now, 8, zone), zone);
      queries.push_back("dates=" + first);
      if (last != first) queries.push_back("dates=" + last);
      // Fight cards: also the default listing (the next cards), in case the
      // month form leaves some out.
      if (league.fights) queries.emplace_back();
      break;
    }
    case League::Mode::days:
      for (int i = 0; i < league.days; ++i) {
        queries.push_back("dates=" + text::yearMonthDay(text::addDays(now, i, zone), zone));
      }
      break;
    case League::Mode::week:
    case League::Mode::current: queries.emplace_back(); break;
  }
  return queries;
}

// MARK: Notes

namespace {

const std::set<std::string>& resultWords() {
  static const std::set<std::string> kWords = {
      "lead",   "leads",    "led",      "won",     "win",     "wins",       "tied",
      "ties",   "tie",      "advance",  "advances", "advanced", "eliminated", "eliminates",
      "clinch", "clinches", "clinched", "beat",    "beats",   "defeat",     "defeats",
      "final",  "score"};
  return kWords;
}

const std::set<std::string>& plainStages() {
  static const std::set<std::string> kStages = {"regular-season", "preseason", "pre-season", "post-season",
                                                "postseason",     "off-season", "offseason", "regular"};
  return kStages;
}

}  // namespace

std::optional<std::string> note(const std::vector<std::string>& headlines,
                                const std::optional<std::string>& stage, const League& league,
                                Seconds start) {
  for (const std::string& raw : headlines) {
    bool result = false;
    const std::string lowered = str::lower(raw);  // the matches point into it
    for (const rx::Match& m : rx::all("\\p{L}+", lowered)) {
      if (resultWords().count(std::string(m.group(0)))) {
        result = true;
        break;
      }
    }
    if (result) continue;
    if (rx::test("\\d\\s*[-\xE2\x80\x93]\\s*\\d", raw)) continue;
    std::string value = str::replaceAll(raw, " - ", " \xC2\xB7 ");
    for (const std::string& prefix : {league.shortName + " ", league.name + " "}) {
      if (text::count(value) > text::count(prefix) && str::startsWith(str::lower(value), str::lower(prefix))) {
        value = text::dropPrefix(value, text::count(prefix));
      }
    }
    if (str::endsWith(value, " Game") && text::count(value) > 5) value = value.substr(0, value.size() - 5);
    value = std::string(str::trimUnicode(value));
    if (!value.empty() && text::count(value) <= 26) return value;
  }
  if (league.shortName == "NFL") {
    time::Civil et = time::Zone::usEastern().toLocal(start);
    if (et.hour >= 19) {
      switch (time::weekday(et.year, et.month, et.day)) {
        case 0: return std::string("Sunday Night");
        case 1: return std::string("Monday Night");
        case 4: return std::string("Thursday Night");
        case 6: return std::string("Saturday Night");
        default: break;
      }
    }
  }
  if (stage && !plainStages().count(str::lower(*stage))) {
    std::vector<std::string> words;
    for (std::string_view w : str::split(*stage, '-')) {
      words.push_back(str::upper(text::prefix(w, 1)) + text::dropPrefix(w, 1));
    }
    std::string value = str::join(words, " ");
    if (!value.empty() && text::count(value) <= 26) return value;
  }
  return std::nullopt;
}

// MARK: Scoreboard

std::optional<SportsGame> parseGame(const json::Value& event, const League& league) {
  auto id = text::text(event["id"]);
  auto start = text::date(text::text(event["date"]));
  const json::Value& competition = event["competitions"][size_t{0}];
  if (!id || !start || !competition.isObject()) return std::nullopt;
  std::vector<const json::Value*> competitors;
  for (const json::Value& c : competition["competitors"].items()) {
    if (c.isObject()) competitors.push_back(&c);
  }
  if (competitors.size() != 2) return std::nullopt;
  std::optional<SportsTeam> home, away;
  for (const json::Value* competitor : competitors) {
    auto team = parse::team((*competitor)["team"]);
    if (!team) continue;
    team->rank = parse::pollRank(text::text((*competitor)["curatedRank"]["current"]));
    if (text::text((*competitor)["homeAway"]) == std::optional<std::string>("home")) {
      home = std::move(team);
    } else {
      away = std::move(team);
    }
  }
  if (!home || !away) return std::nullopt;
  const json::Value& status =
      competition["status"].isObject() ? competition["status"]["type"] : event["status"]["type"];
  std::string statusName = str::upper(text::text(status["name"]).value_or(""));
  std::optional<int> week;
  if (auto number = text::text(event["week"]["number"])) week = text::toInt(*number);
  bool showsWeek = league.group == "NFL" || league.group == "College Football";
  std::vector<std::string> headlines;
  for (const json::Value& n : competition["notes"].items()) {
    if (auto headline = text::text(n["headline"])) headlines.push_back(*headline);
  }
  SportsGame game;
  game.id = league.shortName + "|" + *id;
  game.league = league.group;
  game.leagueShort = league.shortName;
  game.homeFirst = profile(league.group).homeFirst;
  if (showsWeek && week) game.weekLabel = "Week " + std::to_string(*week);
  game.home = std::move(*home);
  game.away = std::move(*away);
  game.start = *start;
  game.networks = parse::networks(competition);
  game.venue = parse::venue(competition["venue"]);
  auto contains = [&](const char* w) { return statusName.find(w) != std::string::npos; };
  game.postponed = contains("POSTPONED") || contains("CANCELED") || contains("CANCELLED");
  game.completed = text::flag(status["completed"]) || contains("FINAL");
  game.note = note(headlines, text::text(event["season"]["slug"]), league, *start);
  return game;
}

std::optional<Document> parseScoreboard(std::string_view body, const League& league) {
  Document doc;
  bool found = parse::forEachEvent(body, [&](const json::Value& event) {
    if (auto game = parseGame(event, league)) doc.games.push_back(std::move(*game));
  });
  if (!found) return std::nullopt;
  parse::TopFields top = parse::topFields(body);
  if (top.weekNumber) doc.week = text::toInt(*top.weekNumber);
  if (top.seasonType) doc.seasonType = text::toInt(*top.seasonType);
  return doc;
}

std::optional<std::vector<SportsTeam>> parseSchools(std::string_view body) {
  json::ArrayLimits limits;
  limits.maxElementBytes = parse::kMaxEventBytes;
  std::optional<std::vector<SportsTeam>> out;
  bool first = true;
  json::ArrayReader reader(
      [&](json::Value&& sport) {
        if (!first) return false;
        first = false;
        const json::Value& teams = sport["leagues"][size_t{0}]["teams"];
        if (!teams.isArray()) return false;
        std::vector<SportsTeam> list;
        for (const json::Value& item : teams.items()) {
          if (auto team = parse::team(item["team"])) list.push_back(std::move(*team));
        }
        out = std::move(list);
        return false;  // only the first sport is read
      },
      {"sports"}, limits);
  reader.feed(body);
  reader.finish();
  return out;
}

// MARK: Fight cards

std::optional<FightCards> parseFightCards(std::string_view body, const League& league) {
  FightCards out;
  const std::string org = str::lower(league.shortName);
  const std::map<std::string, int> partOrder = {{"early", 0}, {"prelims", 1}, {"main", 2}};
  const std::map<std::string, std::string> label = {
      {"early", "Early Prelims"}, {"prelims", "Prelims"}, {"main", "Main Card"}};
  std::vector<std::string> lines;
  std::set<std::string> keysSeen;
  size_t eventCount = 0;
  bool found = parse::forEachEvent(body, [&](const json::Value& event) {
    eventCount += 1;
    auto id = text::text(event["id"]);
    auto name = text::text(event["name"]);
    if (!name) name = text::text(event["shortName"]);
    if (!id || !name) return;
    auto eventStart = text::date(text::text(event["date"]));
    std::vector<json::Value> bouts;
    for (const json::Value& bout : event["competitions"].items()) {
      if (bout.isObject()) bouts.push_back(bout);
    }
    if (!bouts.empty()) {
      for (const auto& field : bouts.front().fields()) keysSeen.insert(field.first);
    }
    struct Part {
      std::optional<Seconds> start;
      std::vector<std::string> networks;
      std::vector<std::pair<SportsTeam, SportsTeam>> bouts;
      int done = 0;
      int count = 0;
      std::optional<std::string> venue;
    };
    std::map<std::string, Part> parts;
    std::vector<std::string> keys = parse::partKeys(bouts);
    for (size_t i = 0; i < bouts.size(); ++i) {
      const json::Value& bout = bouts[i];
      Part& part = parts[keys[i]];
      part.count += 1;
      auto start = text::date(text::text(bout["startDate"]));
      if (!start) start = text::date(text::text(bout["date"]));
      if (start && (!part.start || *start < *part.start)) part.start = start;
      for (const std::string& network : parse::networks(bout)) {
        bool known = std::any_of(part.networks.begin(), part.networks.end(),
                                 [&](const std::string& n) { return str::iequals(n, network); });
        if (!known) part.networks.push_back(network);
      }
      std::vector<SportsTeam> fighters;
      for (const json::Value& c : bout["competitors"].items()) {
        if (auto f = parse::fighter(c)) fighters.push_back(std::move(*f));
      }
      if (fighters.size() == 2) part.bouts.emplace_back(fighters[0], fighters[1]);
      if (text::flag(bout["status"]["type"]["completed"])) part.done += 1;
      if (!part.venue) part.venue = parse::venue(bout["venue"]);
    }
    if (parts.empty()) parts[""].start = eventStart;
    // "UFC 333: Pereira vs. Hill" -> "UFC 333"; its number, if any.
    std::string head = std::string(str::trimUnicode(text::components(*name, ":").front()));
    std::string number;
    if (auto m = rx::first("(?<=\\b" + league.shortName + " )\\d{2,4}\\b", head, true)) {
      number = std::string(m->group(0));
    }
    std::string lowered = matcher::normalized(*name);
    std::vector<std::pair<std::string, Part>> ordered(parts.begin(), parts.end());
    std::stable_sort(ordered.begin(), ordered.end(), [&](const auto& a, const auto& b) {
      auto oa = partOrder.find(a.first), ob = partOrder.find(b.first);
      return (oa == partOrder.end() ? 3 : oa->second) < (ob == partOrder.end() ? 3 : ob->second);
    });
    std::vector<std::string> described;
    for (const auto& [key, part] : ordered) {
      std::optional<Seconds> start = part.start ? part.start : eventStart;
      if (!start) continue;
      // The headline bout: the one the card is named after, else the last
      // listed (bouts are usually listed in running order).
      auto lastWord = [](const std::string& s) {
        std::vector<std::string> words = text::components(s, " ");
        return words.back();
      };
      std::optional<std::pair<SportsTeam, SportsTeam>> headline;
      for (const auto& bout : part.bouts) {
        std::string a = matcher::normalized(lastWord(bout.first.name));
        std::string b = matcher::normalized(lastWord(bout.second.name));
        if (lowered.find(a) != std::string::npos && lowered.find(b) != std::string::npos) {
          headline = bout;
          break;
        }
      }
      if (!headline && !part.bouts.empty()) headline = part.bouts.back();
      std::pair<SportsTeam, SportsTeam> sides;
      if (headline) {
        sides = *headline;
      } else {
        sides.first.id = "f" + *id + "a";
        sides.first.name = head;
        sides.second.id = "f" + *id + "b";
        sides.second.name = *name;
      }
      auto labelText = label.find(key);
      std::string title =
          key.empty() || key == "main" ? *name : head + " " + (labelText == label.end() ? key : labelText->second);
      SportsGame game;
      game.id = league.shortName + "|" + *id + "|" + key;
      game.league = league.group;
      game.leagueShort = league.shortName;
      game.homeFirst = profile(league.group).homeFirst;
      game.home = sides.first;
      game.away = sides.second;
      game.start = *start;
      game.networks = part.networks;
      game.venue = part.venue;
      game.completed = part.count > 0 && part.done == part.count;
      game.customTitle = title;
      game.cardKey = org + "|" + number + "|" + key;
      out.games.push_back(std::move(game));
      time::Civil c = time::civilFromSeconds(*start);
      std::string clock = std::to_string(c.hour) + ":" + (c.minute < 10 ? "0" : "") + std::to_string(c.minute);
      described.push_back((key.empty() ? std::string("card") : key) + " " + std::to_string(part.count) +
                          " bouts " + clock + "Z" +
                          (part.networks.empty() ? "" : " (" + str::join(part.networks, ", ") + ")"));
    }
    lines.push_back(head + ": " + str::join(described, ", "));
  });
  if (!found) return std::nullopt;
  std::vector<std::string> firstLines(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(4, lines.size())));
  std::vector<std::string> keyList(keysSeen.begin(), keysSeen.end());
  out.summary = std::to_string(eventCount) + " cards, " + std::to_string(out.games.size()) + " parts; " +
                str::join(firstLines, "; ") + (keyList.empty() ? "" : "; bout fields [" + str::join(keyList, ",") + "]");
  return out;
}

// MARK: Race weekends

std::string raceName(std::string_view name) {
  // Place names of more than one word, longest first.
  static const std::vector<std::pair<std::string, std::string>> kPlaces = {
      {"United States", "United States"},  {"Mexico City", "Mexico City"},
      {"Ciudad de M\xC3\xA9xico", "Mexico City"}, {"Ciudad de Mexico", "Mexico City"},
      {"S\xC3\xA3o Paulo", "S\xC3\xA3o Paulo"}, {"Sao Paulo", "S\xC3\xA3o Paulo"},
      {"Las Vegas", "Las Vegas"},          {"Abu Dhabi", "Abu Dhabi"},
      {"Saudi Arabian", "Saudi Arabian"},  {"Emilia Romagna", "Emilia Romagna"},
      {"Great Britain", "British"},        {"Monte Carlo", "Monaco"},
      {"d'Italia", "Italian"},             {"Italia", "Italian"},
      {"de Espa\xC3\xB1" "a", "Spanish"},  {"de Espana", "Spanish"},
      {"Barcelona", "Barcelona"},          {"Madrid", "Madrid"},
  };
  for (const auto& [match, place] : kPlaces) {
    if (text::containsCaseInsensitive(name, match)) return place + " GP";
  }
  std::string cleaned(name);
  for (const char* phrase : {"Grand Prix", "Gran Premio", "Grande Pr\xC3\xAAmio", "Grande Premio", "Grand Prize"}) {
    cleaned = rx::replace(cleaned, phrase, "GP", true);
  }
  std::vector<std::string> words;
  for (const std::string& w : text::components(cleaned, " ")) {
    if (w.empty() || (text::count(w) == 4 && text::isAllDigits(w))) continue;
    words.push_back(w);
  }
  auto gp = std::find_if(words.begin(), words.end(), [](const std::string& w) { return str::upper(w) == "GP"; });
  if (gp == words.end()) return str::join(words, " ");
  size_t at = static_cast<size_t>(gp - words.begin());
  // The place is the word before "GP" ("Australian GP"), else the last one
  // after it ("Formula 1 GP de Monaco" -> "Monaco GP").
  if (at > 0) {
    const std::string& before = words[at - 1];
    if (text::count(before) >= 3 && text::hasLetter(before) && str::upper(before) != "F1") return before + " GP";
  }
  if (at + 1 < words.size()) return words.back() + " GP";
  return cleaned;
}

std::optional<std::vector<SportsGame>> parseRaceWeekends(std::string_view body, const League& league) {
  std::vector<SportsGame> games;
  bool found = parse::forEachEvent(body, [&](const json::Value& event) {
    auto id = text::text(event["id"]);
    if (!id) return;
    auto rawName = text::text(event["name"]);
    if (!rawName) rawName = text::text(event["shortName"]);
    std::string gp = raceName(rawName.value_or("Grand Prix"));
    const json::Value& circuit = event["circuit"];
    const json::Value& address = circuit["address"];
    std::optional<std::string> place;
    if (auto name = text::text(circuit["fullName"])) {
      auto city = text::text(address["city"]);
      place = city && name->find(*city) == std::string::npos ? *name + ", " + *city : *name;
    }
    std::optional<std::string> country;
    if (auto c = text::text(address["country"]); c && text::count(*c) <= 3) country = str::upper(*c);
    SportsTeam side;
    side.id = "r" + *id;
    side.name = gp;
    side.country = country;
    const json::Value::Array& competitions = event["competitions"].items();
    for (const json::Value& competition : competitions) {
      auto sessionID = text::text(competition["id"]);
      auto start = text::date(text::text(competition["date"]));
      if (!start) start = text::date(text::text(competition["startDate"]));
      if (!sessionID || !start) continue;
      const json::Value& type = competition["type"];
      std::string typeText = text::text(type["abbreviation"]).value_or(text::text(type["text"]).value_or(""));
      // A weekend listed as its race alone (NASCAR): the race.
      std::string kind = competitions.size() == 1 && (typeText.empty() || league.shortName != "F1")
                             ? std::string("race")
                             : race_session::kind(typeText);
      const json::Value& status = competition["status"]["type"];
      auto state = text::text(status["state"]);
      SportsGame game;
      game.id = league.shortName + "|" + *id + "|" + *sessionID;
      game.league = league.group;
      game.leagueShort = league.shortName;
      game.homeFirst = profile(league.group).homeFirst;
      game.home = side;
      game.away = side;
      game.start = *start;
      game.networks = parse::networks(competition);
      game.venue = place;
      game.completed = text::flag(status["completed"]) || (state && str::lower(*state) == "post");
      game.customTitle = gp + " \xC2\xB7 " + race_session::name(kind);
      game.cardKey = str::lower(league.shortName) + "|" + *id + "|" + kind;
      game.racing = true;
      games.push_back(std::move(game));
    }
  });
  if (!found) return std::nullopt;
  return games;
}

// MARK: Tennis

std::optional<std::string> tennisRound(const std::optional<std::string>& input) {
  if (!input || input->empty()) return std::nullopt;
  std::string value = str::lower(*input);
  std::optional<std::string> number;
  for (char c : value) {
    if (c >= '0' && c <= '9') {
      number = std::string(1, c);
      break;
    }
  }
  auto has = [&](const char* w) { return value.find(w) != std::string::npos; };
  if (has("qualif")) return "Q" + number.value_or("");
  if (has("semi")) return std::string("SF");
  if (has("quarter")) return std::string("QF");
  if (has("final")) return std::string("F");
  if (has("round of")) {
    std::string digits;
    for (char c : value) {
      if (c >= '0' && c <= '9') digits += c;
    }
    if (auto n = text::toInt(digits)) return "R" + std::to_string(*n);
  }
  if (has("round") && number) return "R" + *number;
  if (text::count(*input) <= 12) return *input;
  return std::nullopt;
}

std::string tennisShort(const SportsTeam& team) {
  if (team.name.find('/') != std::string::npos) return team.name;
  std::vector<std::string> words = text::words(team.name);
  return words.empty() ? team.name : words.back();
}

namespace {

std::optional<std::string> tennisCountry(const json::Value& athlete) {
  return parse::countryFromFlag(text::text(athlete["flag"]["href"]));
}

std::optional<std::string> tennisFlag(const json::Value& athlete) { return text::text(athlete["flag"]["href"]); }

// A player, or a doubles pair ("Bucsa / Melichar-Martinez").
std::optional<SportsTeam> tennisSide(const json::Value& competitor) {
  std::vector<const json::Value*> athletes;
  if (competitor["athlete"].isObject()) {
    athletes.push_back(&competitor["athlete"]);
  } else if (competitor["roster"].isObject() && competitor["roster"]["athletes"].isArray()) {
    for (const json::Value& a : competitor["roster"]["athletes"].items()) {
      if (!a.isObject()) return std::nullopt;
      athletes.push_back(&a);
    }
  } else if (competitor["athletes"].isArray()) {
    for (const json::Value& a : competitor["athletes"].items()) {
      if (!a.isObject()) return std::nullopt;
      athletes.push_back(&a);
    }
  }
  std::optional<int> seed;
  if (auto s = text::text(competitor["seed"])) seed = text::toInt(*s);
  auto lastName = [](const json::Value& athlete) -> std::optional<std::string> {
    if (auto last = text::text(athlete["lastName"])) return last;
    if (auto display = text::text(athlete["displayName"])) {
      std::vector<std::string> words = text::words(*display);
      if (!words.empty()) return words.back();
    }
    return std::nullopt;
  };
  if (athletes.size() == 1) {
    const json::Value& athlete = *athletes.front();
    auto name = text::text(athlete["displayName"]);
    if (!name) name = text::text(athlete["fullName"]);
    if (!name) return std::nullopt;
    std::string upper = str::upper(*name);
    if (upper == "TBD" || upper == "TBA" || upper == "BYE" || upper == "QUALIFIER") return std::nullopt;
    SportsTeam team;
    if (auto shortName = text::text(athlete["shortName"]); shortName && *shortName != *name) {
      team.alternates.push_back(*shortName);
    }
    // Feeds write "Rybakina, Elena": surname first finds short surnames too.
    std::vector<std::string> parts = text::words(*name);
    if (parts.size() >= 2) {
      std::string last = text::text(athlete["lastName"]).value_or(parts.back());
      std::vector<std::string> head(parts.begin(), parts.end() - 1);
      std::string first = text::text(athlete["firstName"]).value_or(str::join(head, " "));
      team.alternates.push_back(last + " " + first);
    }
    auto id = text::text(athlete["id"]);
    if (!id) id = text::text(competitor["id"]);
    team.id = "p" + id.value_or(*name);
    team.name = *name;
    team.country = tennisCountry(athlete);
    if (auto flag = tennisFlag(athlete)) team.flags.push_back(*flag);
    if (seed && *seed > 0 && *seed <= 64) team.rank = seed;
    return team;
  }
  if (athletes.size() >= 2) {
    std::vector<std::string> names;
    std::set<std::string> codes;
    std::vector<std::string> flags;
    for (const json::Value* a : athletes) {
      if (auto n = lastName(*a)) names.push_back(*n);
      if (auto code = tennisCountry(*a)) codes.insert(*code);
      if (auto flag = tennisFlag(*a)) flags.push_back(*flag);
    }
    if (names.size() != athletes.size()) return std::nullopt;
    SportsTeam team;
    team.id = "d" + str::join(names, "-");
    team.name = str::join(names, " / ");
    for (const std::string& n : names) {
      if (text::count(n) >= 4) team.alternates.push_back(n);
    }
    if (codes.size() == 1) team.country = *codes.begin();
    if (flags.size() == athletes.size()) team.flags = std::move(flags);
    if (seed && *seed > 0 && *seed <= 32) team.rank = seed;
    return team;
  }
  if (competitor["team"].isObject()) {
    if (auto name = text::text(competitor["team"]["displayName"])) {
      SportsTeam team;
      team.id = "p" + text::text(competitor["team"]["id"]).value_or(*name);
      team.name = *name;
      return team;
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::vector<SportsGame>> parseTennisMatches(std::string_view body, const League& league) {
  std::vector<SportsGame> games;
  bool found = parse::forEachEvent(body, [&](const json::Value& event) {
    auto eventID = text::text(event["id"]);
    if (!eventID) return;
    auto tournament = text::text(event["shortName"]);
    if (!tournament) tournament = text::text(event["name"]);
    for (const json::Value* competitionPtr : parse::tennisCompetitions(event)) {
      const json::Value& competition = *competitionPtr;
      auto matchID = text::text(competition["id"]);
      auto start = text::date(text::text(competition["date"]));
      if (!start) start = text::date(text::text(competition["startDate"]));
      const json::Value& competitors = competition["competitors"];
      if (!matchID || !start || competitors.size() != 2) continue;
      bool objects = std::all_of(competitors.items().begin(), competitors.items().end(),
                                 [](const json::Value& c) { return c.isObject(); });
      if (!objects) continue;
      std::optional<SportsTeam> home, away;
      for (size_t index = 0; index < 2; ++index) {
        const json::Value& competitor = competitors[index];
        auto side = tennisSide(competitor);
        if (!side) continue;
        auto homeAway = text::text(competitor["homeAway"]);
        if (homeAway == std::optional<std::string>("home") || (!homeAway && index == 0)) {
          home = std::move(side);
        } else {
          away = std::move(side);
        }
      }
      if (!home || !away) continue;
      const json::Value& venue = competition["venue"];
      auto court = text::text(venue["court"]);
      auto city = text::text(venue["address"]["city"]);
      if (!city) city = text::text(venue["fullName"]);
      std::optional<std::string> roundText;
      if (competition["round"].isObject()) {
        roundText = text::text(competition["round"]["displayName"]);
        if (!roundText) roundText = text::text(competition["round"]["description"]);
      } else {
        roundText = text::text(competition["round"]);
      }
      std::optional<std::string> place = city ? std::optional<std::string>(text::components(*city, ",").front()) : tournament;
      std::vector<std::string> tag;
      if (auto round = tennisRound(roundText)) tag.push_back(*round);
      if (place) tag.push_back(*place);
      const json::Value& status = competition["status"]["type"];
      auto state = text::text(status["state"]);
      SportsGame game;
      game.id = league.shortName + "|" + *eventID + "|" + *matchID;
      game.league = league.group;
      game.leagueShort = league.shortName;
      game.homeFirst = profile(league.group).homeFirst;
      // No networks: a match is found through feeds and listings naming its
      // players, not every channel of its network.
      game.home = *home;
      game.away = *away;
      game.start = *start;
      game.venue = court;
      game.completed = text::flag(status["completed"]) || (state && str::lower(*state) == "post");
      game.customTitle = tennisShort(*away) + " vs " + tennisShort(*home);
      game.tournament = tournament;
      if (!tag.empty()) game.note = str::join(tag, " \xC2\xB7 ");
      game.tennis = true;
      games.push_back(std::move(game));
    }
  });
  if (!found) return std::nullopt;
  return games;
}

// MARK: Keeping

std::string weekendKey(const SportsGame& game) {
  size_t cut = game.id.rfind('|');
  return cut == std::string::npos ? game.id : game.id.substr(0, cut);
}

std::vector<SportsGame> keeping(const std::vector<SportsGame>& games, Seconds cutoff) {
  std::map<std::string, Seconds> weekendEnd;
  for (const SportsGame& game : games) {
    if (!game.isRacing()) continue;
    std::string key = weekendKey(game);
    auto it = weekendEnd.find(key);
    if (it == weekendEnd.end() || game.start > it->second) weekendEnd[key] = game.start;
  }
  std::vector<SportsGame> out;
  for (const SportsGame& game : games) {
    if (game.isRacing()) {
      auto it = weekendEnd.find(weekendKey(game));
      if (it != weekendEnd.end()) {
        if (it->second > cutoff) out.push_back(game);
        continue;
      }
    }
    if (game.start > cutoff) out.push_back(game);
  }
  return out;
}

// A golf tournament as its round on now (or next): the event's date is the
// first day, so round N is N-1 days later, from 07:00 US Eastern (the
// scoreboard's status says when it is really on).
std::optional<std::vector<SportsGame>> parseGolfTournaments(std::string_view body, const League& league) {
  std::vector<SportsGame> games;
  bool found = parse::forEachEvent(body, [&](const json::Value& event) {
    auto id = text::text(event["id"]);
    auto name = text::text(event["name"]);
    auto first = text::date(text::text(event["date"]));
    if (!id || !name || !first) return;
    const json::Value& competition = event["competitions"][size_t{0}];
    const json::Value& status = competition["status"];
    std::string state = str::lower(text::text(status["type"]["state"]).value_or(""));
    std::string statusName = str::upper(text::text(status["type"]["name"]).value_or(""));
    if (statusName.find("CANCEL") != std::string::npos) return;
    int round = 1;
    if (auto period = text::text(status["period"])) round = std::max(1, text::toInt(*period).value_or(1));
    SportsTeam side;
    side.id = "g" + *id;
    side.name = *name;
    SportsGame game;
    game.id = league.shortName + "|" + *id;
    game.league = league.group;
    game.leagueShort = league.shortName;
    game.homeFirst = profile(league.group).homeFirst;
    game.home = side;
    game.away = side;
    Seconds day = *first - *first % 86'400;
    game.start = day + (round - 1) * 86'400 + 11 * 3600;
    game.networks = parse::networks(competition);
    game.completed = text::flag(status["type"]["completed"]) || state == "post";
    game.customTitle = *name;
    game.note = text::text(event["shortName"]).value_or(*name);
    game.golf = true;
    games.push_back(std::move(game));
  });
  if (!found) return std::nullopt;
  return games;
}

std::optional<LeagueResult> parseLeague(std::string_view body, const League& league, Seconds from, Seconds to) {
  LeagueResult result;
  auto inWindow = [&](const SportsGame& g) { return g.start >= from && g.start <= to; };
  if (league.golf) {
    auto tournaments = parseGolfTournaments(body, league);
    if (!tournaments) return std::nullopt;
    result.total = tournaments->size();
    for (SportsGame& g : *tournaments) {
      if (inWindow(g)) result.games.push_back(std::move(g));
    }
    return result;
  }
  if (league.tennis) {
    auto matches = parseTennisMatches(body, league);
    if (!matches) return std::nullopt;
    result.total = matches->size();
    for (SportsGame& g : *matches) {
      if (inWindow(g)) result.games.push_back(std::move(g));
    }
    return result;
  }
  if (league.racing) {
    auto weekends = parseRaceWeekends(body, league);
    if (!weekends) return std::nullopt;
    result.total = weekends->size();
    // Whole weekends, while any session is still ahead or recent.
    for (SportsGame& g : keeping(*weekends, from)) {
      if (g.start <= to + 7 * 86'400) result.games.push_back(std::move(g));
    }
    return result;
  }
  if (league.fights) {
    auto cards = parseFightCards(body, league);
    if (!cards) return std::nullopt;
    result.total = cards->games.size();
    for (SportsGame& g : cards->games) {
      if (inWindow(g)) result.games.push_back(std::move(g));
    }
    return result;
  }
  auto doc = parseScoreboard(body, league);
  if (!doc) return std::nullopt;
  std::map<std::string, SportsTeam> teams;
  for (const SportsGame& g : doc->games) {
    teams[g.home.id] = g.home;
    teams[g.away.id] = g.away;
  }
  for (auto& [id, team] : teams) result.teams.push_back(team);
  result.total = doc->games.size();
  for (SportsGame& g : doc->games) {
    if (inWindow(g)) result.games.push_back(std::move(g));
  }
  result.week = doc->week;
  result.seasonType = doc->seasonType;
  return result;
}

// MARK: Saved

namespace {

json::Value teamsJson(const std::vector<SportsTeam>& teams) {
  json::Value list = json::Value::array();
  for (const SportsTeam& t : teams) list.push(toJson(t));
  return list;
}

std::vector<SportsTeam> teamsFrom(const json::Value& v) {
  std::vector<SportsTeam> out;
  for (const json::Value& item : v.items()) {
    if (auto t = teamFromJson(item)) out.push_back(std::move(*t));
  }
  return out;
}

json::Value datesJson(const std::map<std::string, Seconds>& map) {
  json::Value v = json::Value::object();
  for (const auto& [key, t] : map) v.set(key, static_cast<int64_t>(t));
  return v;
}

std::map<std::string, Seconds> datesFrom(const json::Value& v) {
  std::map<std::string, Seconds> out;
  for (const auto& [key, t] : v.fields()) {
    if (t.isNumber()) out[key] = t.asInt();
  }
  return out;
}

}  // namespace

std::string toJson(const Saved& s) {
  json::Value v = json::Value::object();
  json::Value games = json::Value::array();
  for (const SportsGame& g : s.games) games.push(toJson(g));
  v.set("games", games);
  if (s.fetchedAt) v.set("fetchedAt", static_cast<int64_t>(*s.fetchedAt));
  v.set("leagueFetched", datesJson(s.leagueFetched));
  v.set("leagueWatchable", datesJson(s.leagueWatchable));
  if (s.failedAt) v.set("failedAt", static_cast<int64_t>(*s.failedAt));
  if (s.failures) v.set("failures", *s.failures);
  json::Value forms = json::Value::object();
  for (const auto& [key, form] : s.leagueForm) forms.set(key, form);
  v.set("leagueForm", forms);
  if (s.requestDay) v.set("requestDay", *s.requestDay);
  if (s.requestCount) v.set("requestCount", *s.requestCount);
  if (s.failureBuild) v.set("failureBuild", *s.failureBuild);
  json::Value rosters = json::Value::object();
  for (const auto& [key, teams] : s.rosters) rosters.set(key, teamsJson(teams));
  v.set("rosters", rosters);
  if (s.schools) v.set("schools", teamsJson(*s.schools));
  if (s.schoolsFetchedAt) v.set("schoolsFetchedAt", static_cast<int64_t>(*s.schoolsFetchedAt));
  if (s.leagueSet) {
    json::Value set = json::Value::array();
    for (const std::string& l : *s.leagueSet) set.push(l);
    v.set("leagueSet", set);
  }
  if (s.schema) v.set("schema", *s.schema);
  return json::write(v);
}

std::optional<Saved> savedFromJson(std::string_view textBody) {
  auto doc = json::parse(textBody);
  if (!doc || !doc->isObject()) return std::nullopt;
  const json::Value& v = *doc;
  Saved s;
  for (const json::Value& item : v["games"].items()) {
    if (auto g = gameFromJson(item)) s.games.push_back(std::move(*g));
  }
  if (v["fetchedAt"].isNumber()) s.fetchedAt = v["fetchedAt"].asInt();
  s.leagueFetched = datesFrom(v["leagueFetched"]);
  s.leagueWatchable = datesFrom(v["leagueWatchable"]);
  if (v["failedAt"].isNumber()) s.failedAt = v["failedAt"].asInt();
  if (v["failures"].isNumber()) s.failures = static_cast<int>(v["failures"].asInt());
  for (const auto& [key, form] : v["leagueForm"].fields()) {
    if (form.isString()) s.leagueForm[key] = form.asString();
  }
  if (v["requestDay"].isString()) s.requestDay = v["requestDay"].asString();
  if (v["requestCount"].isNumber()) s.requestCount = static_cast<int>(v["requestCount"].asInt());
  if (v["failureBuild"].isString()) s.failureBuild = v["failureBuild"].asString();
  for (const auto& [key, teams] : v["rosters"].fields()) s.rosters[key] = teamsFrom(teams);
  if (v["schools"].isArray()) s.schools = teamsFrom(v["schools"]);
  if (v["schoolsFetchedAt"].isNumber()) s.schoolsFetchedAt = v["schoolsFetchedAt"].asInt();
  if (v["leagueSet"].isArray()) {
    std::vector<std::string> set;
    for (const json::Value& l : v["leagueSet"].items()) {
      if (l.isString()) set.push_back(l.asString());
    }
    s.leagueSet = std::move(set);
  }
  if (v["schema"].isNumber()) s.schema = static_cast<int>(v["schema"].asInt());
  return s;
}

// MARK: Schedule

namespace {

std::set<std::string> shortsOf(const std::vector<League>& list) {
  std::set<std::string> out;
  for (const League& l : list) out.insert(l.shortName);
  return out;
}

bool anyCollege(const std::vector<League>& list) {
  return std::any_of(list.begin(), list.end(), [](const League& l) { return l.college(); });
}

std::vector<SportsTeam> sortedByName(std::vector<SportsTeam> teams) {
  std::stable_sort(teams.begin(), teams.end(), [](const SportsTeam& a, const SportsTeam& b) {
    return text::lessCaseInsensitive(a.name, b.name);
  });
  return teams;
}

}  // namespace

Schedule::Schedule(Saved saved, std::string appBuild) : saved_(std::move(saved)), appBuild_(std::move(appBuild)) {}

bool Schedule::refreshIfNeeded(const Context& ctx) {
  if (!ctx.enabled || fetching()) return false;
  const Seconds now = ctx.now;
  if (saved_.fetchedAt && text::dayDifference(*saved_.fetchedAt, now, ctx.zone) == 0 &&
      now - *saved_.fetchedAt < kFreshFor && (saved_.schema == kSchema || saved_.failedAt)) {
    // Already fetched today, but a league added since is looked up now.
    std::set<std::string> known;
    if (saved_.leagueSet) known.insert(saved_.leagueSet->begin(), saved_.leagueSet->end());
    std::set<std::string> missing;
    for (const League& l : activeLeagues(ctx.prefs, ctx.seen, now)) {
      if (!known.count(l.shortName)) missing.insert(l.shortName);
    }
    if (!missing.empty() && !saved_.failedAt) return start(ctx, false, missing);
    return false;
  }
  if (saved_.failureBuild != appBuild_) saved_.failures = 0;
  int failures = saved_.failures.value_or(0);
  if (saved_.failedAt) {
    // 1 h -> 6 h -> 24 h, then weekly; network trouble waits 1 h.
    Seconds wait = kBackoff[std::min(std::max(failures, 0), 4)];
    if (now - *saved_.failedAt < wait) return false;
  }
  return start(ctx, false, std::nullopt);
}

bool Schedule::refreshNow(const Context& ctx) {
  if (!ctx.enabled || fetching()) return false;
  return start(ctx, true, std::nullopt);
}

bool Schedule::leaguesChanged(const Context& ctx) {
  if (!ctx.enabled) return false;
  std::set<std::string> fetched;
  if (saved_.leagueSet) fetched.insert(saved_.leagueSet->begin(), saved_.leagueSet->end());
  std::vector<League> active = activeLeagues(ctx.prefs, ctx.seen, ctx.now);
  std::set<std::string> missing;
  for (const League& l : active) {
    if (!fetched.count(l.shortName)) missing.insert(l.shortName);
  }
  for (const std::string& wanted : wantedRosters_) {
    auto roster = saved_.rosters.find(wanted);
    bool empty = roster == saved_.rosters.end() || roster->second.empty();
    if (empty && !fetched.count(wanted)) missing.insert(wanted);
  }
  bool college = anyCollege(active) || std::any_of(leagues().begin(), leagues().end(), [&](const League& l) {
                   return wantedRosters_.count(l.shortName) && l.college();
                 });
  bool needsSchools = college && (!saved_.schools || saved_.schools->empty());
  if (missing.empty() && !needsSchools) return false;
  if (fetching()) {
    recheckPending_ = true;
    return false;
  }
  return start(ctx, false, missing);
}

bool Schedule::requestRosters(const std::set<std::string>& shorts, const Context& ctx) {
  // Only leagues with a schedule have teams ("S:Golf" has none).
  wantedRosters_.clear();
  for (const std::string& s : shorts) {
    if (std::any_of(leagues().begin(), leagues().end(), [&](const League& l) { return l.shortName == s; })) {
      wantedRosters_.insert(s);
    }
  }
  return leaguesChanged(ctx);
}

void Schedule::noteWatchable(const std::set<std::string>& leagueShorts, Seconds now) {
  bool changed = false;
  for (const std::string& league : leagueShorts) {
    auto last = saved_.leagueWatchable.find(league);
    if (last != saved_.leagueWatchable.end() && now - last->second < kWatchableNoteGap) continue;
    saved_.leagueWatchable[league] = now;
    changed = true;
  }
  if (changed) dirty_ = true;
}

Published Schedule::publish(const Context& ctx) const {
  Published out;
  if (!ctx.enabled) return out;
  std::vector<League> active = activeLeagues(ctx.prefs, ctx.seen, ctx.now);
  std::set<std::string> shorts = shortsOf(active);
  for (SportsGame& g : keeping(saved_.games, ctx.now - kFinishedShown)) {
    if (shorts.count(g.leagueShort)) out.games.push_back(std::move(g));
  }
  std::stable_sort(out.games.begin(), out.games.end(),
                   [](const SportsGame& a, const SportsGame& b) { return a.start < b.start; });
  out.updatedAt = saved_.fetchedAt;
  out.rosters = saved_.rosters;
  if (anyCollege(active) && saved_.schools) out.schools = *saved_.schools;
  return out;
}

std::vector<SportsTeam> Schedule::teams(const League& league) const {
  std::map<std::string, SportsTeam> byID;
  if (league.college() && saved_.schools) {
    for (const SportsTeam& school : *saved_.schools) byID[school.id] = school;
  }
  auto roster = saved_.rosters.find(league.shortName);
  if (roster != saved_.rosters.end()) {
    for (const SportsTeam& team : roster->second) byID[team.id] = team;
  }
  std::vector<SportsTeam> list;
  for (auto& [id, team] : byID) list.push_back(team);
  return sortedByName(std::move(list));
}

bool Schedule::skip(const std::string& league, Seconds now) const {
  auto fetched = saved_.leagueFetched.find(league);
  if (fetched == saved_.leagueFetched.end()) return false;
  auto watchable = saved_.leagueWatchable.find(league);
  if (watchable != saved_.leagueWatchable.end() && now - watchable->second < kSkipFor) return false;
  return now - fetched->second < kSkipFor;
}

bool Schedule::start(const Context& ctx, bool force, std::optional<std::set<std::string>> only) {
  FetchState f;
  f.force = force;
  f.only = std::move(only);
  f.now = ctx.now;
  f.zone = ctx.zone;
  f.from = text::addDays(ctx.now, -2, ctx.zone);
  f.to = text::addDays(ctx.now, 8, ctx.zone);
  std::vector<League> active = activeLeagues(ctx.prefs, ctx.seen, ctx.now);
  // A given list may include leagues Setup is showing but hasn't saved.
  if (f.only) {
    for (const League& l : allLeagues()) {
      if (f.only->count(l.shortName)) f.active.push_back(l);
    }
  } else {
    f.active = active;
  }
  f.configured = ctx.prefs.configured;
  for (const SportsGame& g : saved_.games) f.byLeague[g.leagueShort].push_back(g);
  f.rosters = saved_.rosters;
  // School logos for college events, once a month.
  bool college = anyCollege(active) || anyCollege(f.active);
  bool stale = !saved_.schoolsFetchedAt || ctx.now - *saved_.schoolsFetchedAt > kSchoolsMaxAge;
  f.needsSchools = college && (stale || !saved_.schools || saved_.schools->empty());
  fetch_ = std::move(f);
  finished_.reset();
  return true;
}

std::optional<Schedule::Pending> Schedule::nextRequest() {
  if (!fetch_ || fetch_->awaiting) return std::nullopt;
  FetchState& f = *fetch_;
  auto count = [&] {
    std::string today = text::isoDay(f.now, f.zone);
    if (saved_.requestDay != today) {
      saved_.requestDay = today;
      saved_.requestCount = 0;
    }
    saved_.requestCount = saved_.requestCount.value_or(0) + 1;
  };
  while (true) {
    if (f.leagueIndex < f.active.size()) {
      const League& league = f.active[f.leagueIndex];
      if (!f.inLeague) {
        // Before setup, leagues you can't watch are checked weekly.
        if (!f.force && !f.configured && skip(league.shortName, f.now)) {
          f.leagueIndex += 1;
          continue;
        }
        f.leaguesAsked += 1;
        f.inLeague = true;
        f.queries = queriesFor(league, f.now, f.zone);
        f.queryIndex = 0;
        f.retrying = false;
        f.list.clear();
        f.roster.clear();
        f.ok = false;
      }
      if (f.queryIndex < f.queries.size()) {
        Pending p;
        p.kind = Pending::Kind::league;
        p.league = league;
        p.query = f.queries[f.queryIndex];
        p.from = f.from;
        p.to = f.to;
        if (!f.retrying) {
          // Remember which form works, so a league is asked once, not twice.
          auto form = saved_.leagueForm.find(league.shortName);
          f.plainForm = form != saved_.leagueForm.end() && form->second == "plain";
        }
        p.request = scoreboardRequest(league, p.query, f.retrying || f.plainForm);
        p.waitMs = f.sent > 0 ? kRequestSpacingMs : 0;
        f.sent += 1;
        f.awaiting = true;
        count();
        return p;
      }
      finishLeague();
      f.leagueIndex += 1;
      f.inLeague = false;
      continue;
    }
    if (f.needsSchools && !f.schoolsAsked) {
      f.schoolsAsked = true;
      Pending p;
      p.kind = Pending::Kind::schools;
      p.request = schoolsRequest();
      p.waitMs = f.sent > 0 ? kRequestSpacingMs : 0;
      f.sent += 1;
      f.awaiting = true;
      f.awaitingSchools = true;
      count();
      return p;
    }
    finish();
    return std::nullopt;
  }
}

Schedule::Answer Schedule::parse(const Pending& pending, std::string_view body) {
  Answer answer;
  if (pending.kind == Pending::Kind::schools) {
    answer.schools = parseSchools(body);
  } else {
    answer.league = parseLeague(body, pending.league, pending.from, pending.to);
  }
  return answer;
}

void Schedule::onResponse(int status, std::string_view body) {
  if (!fetch_ || !fetch_->awaiting) return;
  Answer answer;
  if (status == 200) {
    if (fetch_->awaitingSchools) {
      answer.schools = parseSchools(body);
    } else if (fetch_->leagueIndex < fetch_->active.size()) {
      answer.league = parseLeague(body, fetch_->active[fetch_->leagueIndex], fetch_->from, fetch_->to);
    }
  }
  onAnswer(status, std::move(answer));
}

void Schedule::onAnswer(int status, Answer answer) {
  if (!fetch_ || !fetch_->awaiting) return;
  FetchState& f = *fetch_;
  f.awaiting = false;
  // A request that failed for network reasons (offline, timeout) doesn't
  // count toward the backoff.
  if (status == 0) f.networkTrouble = true;
  if (f.awaitingSchools) {
    f.awaitingSchools = false;
    if (status == 200 && answer.schools) {
      saved_.schools = std::move(*answer.schools);
      saved_.schoolsFetchedAt = f.now;
    }
    return;
  }
  if (f.leagueIndex >= f.active.size()) return;
  const League& league = f.active[f.leagueIndex];
  // Only the first request of a league says which query form works.
  bool learnForm = f.queryIndex == 0;
  if (!f.retrying) {
    if (status == 200 && !f.plainForm && learnForm) saved_.leagueForm[league.shortName] = "limit";
    if (status == 400 && !f.plainForm) {
      f.retrying = true;  // the plain form next
      return;
    }
  } else if (status == 200 && learnForm) {
    saved_.leagueForm[league.shortName] = "plain";
  }
  f.retrying = false;
  if (status == 200 && answer.league) {
    LeagueResult& part = *answer.league;
    for (SportsGame& g : part.games) f.list.push_back(std::move(g));
    for (SportsTeam& t : part.teams) f.roster[t.id] = std::move(t);
    f.ok = true;
    // This week's answer says which week it is: ask for next week too.
    if (league.mode == League::Mode::week && f.queryIndex == 0 && part.week) {
      f.queries.push_back("week=" + std::to_string(*part.week + 1) +
                          "&seasontype=" + std::to_string(part.seasonType.value_or(2)));
    }
  }
  f.queryIndex += 1;
}

void Schedule::finishLeague() {
  FetchState& f = *fetch_;
  if (!f.ok) return;
  const League& league = f.active[f.leagueIndex];
  std::unordered_set<std::string> seen;
  std::vector<SportsGame> kept;
  for (SportsGame& g : f.list) {
    if (seen.insert(g.id).second) kept.push_back(std::move(g));
  }
  // Games of the last day and a half that this listing no longer has
  // (day-by-day leagues start at today): kept, so leftover feeds still find them.
  const Seconds recent = f.now - kFinishedShown;
  for (const SportsGame& g : f.byLeague[league.shortName]) {
    if (g.start > recent && g.start < f.now && !seen.count(g.id)) kept.push_back(g);
  }
  f.byLeague[league.shortName] = std::move(kept);
  if (!f.roster.empty()) {
    // Added to, not replaced: teams on a bye week stay pickable.
    for (const SportsTeam& team : f.rosters[league.shortName]) {
      if (!f.roster.count(team.id)) f.roster[team.id] = team;
    }
    std::vector<SportsTeam> list;
    for (auto& [id, team] : f.roster) list.push_back(team);
    f.rosters[league.shortName] = sortedByName(std::move(list));
  }
  saved_.leagueFetched[league.shortName] = f.now;
  f.succeeded.push_back(league.shortName);
  f.fetchedAny = true;
}

void Schedule::finish() {
  FetchState f = std::move(*fetch_);
  fetch_.reset();
  Finished done;
  done.note = "schedule: " + std::to_string(f.leaguesAsked) + " leagues, ok=" + (f.fetchedAny ? "true" : "false") +
              ", " + std::to_string(saved_.requestCount.value_or(0)) + " requests today";
  if (!f.fetchedAny && f.leaguesAsked > 0) {
    saved_.failedAt = f.now;
    if (!f.networkTrouble) {
      saved_.failures = saved_.failures.value_or(0) + 1;
      saved_.failureBuild = appBuild_;
    }
    dirty_ = true;
  } else {
    saved_.failedAt.reset();
    saved_.failures = 0;
    std::vector<SportsGame> all;
    for (auto& [league, games] : f.byLeague) all.insert(all.end(), games.begin(), games.end());
    saved_.games = keeping(all, f.now - kSavedFor);
    saved_.rosters = std::move(f.rosters);
    std::set<std::string> set;
    if (saved_.leagueSet) set.insert(saved_.leagueSet->begin(), saved_.leagueSet->end());
    set.insert(f.succeeded.begin(), f.succeeded.end());
    saved_.leagueSet = std::vector<std::string>(set.begin(), set.end());
    if (!f.only) {
      saved_.fetchedAt = f.now;
      saved_.schema = kSchema;
    }
    dirty_ = true;
    done.succeeded = true;
  }
  done.recheck = recheckPending_;
  recheckPending_ = false;
  finished_ = std::move(done);
}

std::optional<Schedule::Finished> Schedule::takeFinished() {
  std::optional<Finished> out = std::move(finished_);
  finished_.reset();
  return out;
}

bool Schedule::takeDirty() {
  bool d = dirty_;
  dirty_ = false;
  return d;
}

}  // namespace marigold::sports::schedule
