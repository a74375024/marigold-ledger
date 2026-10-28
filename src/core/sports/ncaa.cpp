#include "core/sports/ncaa.hpp"

#include <functional>
#include <map>
#include <set>

#include "core/sports/profiles.hpp"
#include "core/sports/text.hpp"
#include "core/util/json.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::sports::ncaa {
namespace {

constexpr std::string_view kSettingsStart = "data-drupal-selector=\"drupal-settings-json\">";

// The day's games in the page's settings, each a contest object.
bool forEachContest(std::string_view body, const std::function<void(const json::Value&)>& fn) {
  size_t at = body.find(kSettingsStart);
  if (at == std::string_view::npos) return false;
  at += kSettingsStart.size();
  size_t end = body.find("</script>", at);
  if (end == std::string_view::npos) return false;
  auto settings = json::parse(body.substr(at, end - at));
  if (!settings) return false;
  const json::Value& games = (*settings)["scoreboard"]["initialGames"];
  if (!games.isArray()) return false;
  for (const json::Value& contest : games.items()) {
    if (contest.isObject()) fn(contest);
  }
  return true;
}

// "Women's" / "Men's" for the two-sex sports, from the page path.
std::string sideOfSport(const League& league) {
  std::string sport = league.path.substr(0, league.path.find('/'));
  if (str::endsWith(sport, "-women")) return "Women's";
  if (str::endsWith(sport, "-men")) return "Men's";
  return "";
}

std::string divisionName(const League& league) {
  if (str::endsWith(league.path, "/d2")) return "Division II";
  if (str::endsWith(league.path, "/d3")) return "Division III";
  return "Division I";
}

// "1ST HALF" -> "1st Half", "SET 3" -> "Set 3"; "OT", "2OT", "Q2" stay.
std::string readablePeriod(std::string_view period) {
  std::vector<std::string> words;
  for (std::string_view w : str::split(period, ' ')) {
    if (w.empty()) continue;
    std::string upper = str::upper(w);
    if (rx::test("^(\\d?OT|SO|Q\\d)$", upper)) {
      words.push_back(upper);
      continue;
    }
    std::string lower = str::lower(w);
    if (lower[0] >= 'a' && lower[0] <= 'z') lower[0] = static_cast<char>(lower[0] - 'a' + 'A');
    words.push_back(lower);
  }
  return str::join(words, " ");
}

}  // namespace

std::string pageUrl(const League& league, std::string_view day) {
  if (day.size() != 8) return "";
  return "https://www.ncaa.com/scoreboard/" + league.path + "/" + std::string(day.substr(0, 4)) + "/" +
         std::string(day.substr(4, 2)) + "/" + std::string(day.substr(6, 2)) + "/all-conf";
}

std::string writtenOut(std::string_view name) {
  static const std::map<std::string, std::string> kWords = {
      {"Col.", "College"},       {"Univ.", "University"}, {"Mt.", "Mount"},       {"Ft.", "Fort"},
      {"Wm.", "William"},        {"Conn.", "Connecticut"}, {"Me.", "Maine"},       {"Wis.", "Wisconsin"},
      {"Minn.", "Minnesota"},    {"Wash.", "Washington"}, {"Mass.", "Massachusetts"}, {"Ark.", "Arkansas"},
      {"Ala.", "Alabama"},       {"Fla.", "Florida"},     {"Ga.", "Georgia"},     {"Ill.", "Illinois"},
      {"Ind.", "Indiana"},       {"Ky.", "Kentucky"},     {"La.", "Louisiana"},   {"Md.", "Maryland"},
      {"Mich.", "Michigan"},     {"Miss.", "Mississippi"}, {"Mo.", "Missouri"},   {"Neb.", "Nebraska"},
      {"Okla.", "Oklahoma"},     {"Ore.", "Oregon"},      {"Pa.", "Pennsylvania"}, {"Tenn.", "Tennessee"},
      {"Tex.", "Texas"},         {"Va.", "Virginia"},     {"Vt.", "Vermont"},     {"Colo.", "Colorado"},
      {"Calif.", "California"},  {"Cal.", "California"},  {"Ariz.", "Arizona"},   {"Kan.", "Kansas"},
      {"Ia.", "Iowa"},           {"Del.", "Delaware"},
  };
  std::string out;
  std::string word;
  std::string_view rest = name;
  // Word by word, keeping what's between words (spaces, hyphens, brackets).
  auto flush = [&](char next) {
    if (word.empty()) return;
    if (word == "St.") {
      // At the end (or before a hyphen or bracket) a state; else a saint.
      out += next == '\0' || next == '-' || next == ')' ? "State" : "Saint";
    } else if (auto found = kWords.find(word); found != kWords.end()) {
      out += found->second;
    } else {
      out += word;
    }
    word.clear();
  };
  for (size_t i = 0; i < rest.size(); ++i) {
    char c = rest[i];
    if (c == ' ' || c == '-' || c == '(' || c == ')' || c == '/') {
      size_t j = i;
      while (j < rest.size() && rest[j] == ' ') ++j;
      flush(j < rest.size() ? rest[j] : '\0');
      out += c;
    } else {
      word += c;
    }
  }
  flush('\0');
  return out;
}

std::vector<std::string> otherNames(std::string_view name) {
  std::vector<std::string> out;
  std::set<std::string> seen{std::string(name)};
  auto add = [&](std::string n) {
    n = std::string(str::trimUnicode(n));
    if (!n.empty() && seen.insert(n).second) out.push_back(std::move(n));
  };
  std::string full = writtenOut(name);
  std::string bare = std::string(str::trimUnicode(rx::replace(full, "\\s*\\([^)]*\\)", "")));
  add(full);
  add(bare);
  // "Plymouth State" is often "Plymouth"; "Rhode Island College" "Rhode Island".
  for (const char* tail : {" State", " College", " University"}) {
    if (str::endsWith(bare, tail) && bare.size() > std::string_view(tail).size() + 3) {
      add(bare.substr(0, bare.size() - std::string_view(tail).size()));
    }
  }
  // "N.C. Wesleyan" as "NC Wesleyan".
  std::string undotted = rx::replace(name, "\\s*\\([^)]*\\)", "");
  undotted = rx::replace(undotted, "\\b(\\p{Lu})\\.(?=\\p{Lu}\\.)", "$1");
  undotted = rx::replace(undotted, "\\b(\\p{Lu}{2})\\.", "$1");
  add(undotted);
  return out;
}

std::optional<std::vector<SportsGame>> parseGames(std::string_view body, const League& league) {
  std::vector<SportsGame> games;
  std::string side = sideOfSport(league);
  std::string tagBase = divisionName(league) + (side.empty() ? "" : " \xC2\xB7 " + side);
  bool found = forEachContest(body, [&](const json::Value& contest) {
    auto id = text::text(contest["contestId"]);
    const json::Value& epoch = contest["startTimeEpoch"];
    if (!id || !epoch.isNumber()) return;
    std::optional<SportsTeam> home, away;
    for (const json::Value& t : contest["teams"].items()) {
      auto name = text::text(t["nameShort"]);
      auto seo = text::text(t["seoname"]);
      if (!name || !seo) continue;
      SportsTeam team;
      team.id = "ncaa:" + *seo;
      team.name = writtenOut(*name);
      team.alternates = otherNames(*name);
      if (*name != team.name) team.alternates.insert(team.alternates.begin(), *name);
      if (text::flag(t["isHome"])) home = std::move(team);
      else away = std::move(team);
    }
    if (!home || !away) return;
    std::string state = str::upper(text::text(contest["gameState"]).value_or(""));
    std::string display = str::lower(text::text(contest["statusCodeDisplay"]).value_or(""));
    SportsGame game;
    game.id = league.shortName + "|" + *id;
    game.league = league.group;
    game.leagueShort = league.shortName;
    game.homeFirst = profile(league.group).homeFirst;
    game.home = std::move(*home);
    game.away = std::move(*away);
    game.start = epoch.asInt();
    game.postponed = state == "O" || display == "postponed" || display == "canceled" || display == "cancelled";
    game.completed = state == "F";
    // A championship's round in place of the side of the sport, when it fits.
    game.note = tagBase;
    auto round = text::text(contest["roundDescription"]);
    if (round && text::count(divisionName(league) + " \xC2\xB7 " + *round) <= 26) {
      game.note = divisionName(league) + " \xC2\xB7 " + *round;
    }
    games.push_back(std::move(game));
  });
  if (!found) return std::nullopt;
  return games;
}

scores::Statuses statuses(std::string_view body, const League& league) {
  scores::Statuses result;
  forEachContest(body, [&](const json::Value& contest) {
    auto id = text::text(contest["contestId"]);
    if (!id) return;
    std::string state = str::upper(text::text(contest["gameState"]).value_or(""));
    std::string display = str::lower(text::text(contest["statusCodeDisplay"]).value_or(""));
    LiveStatus live;
    if (state == "I" || display == "live") live.phase = LivePhase::live;
    else if (state == "F" || display == "final") live.phase = LivePhase::final;
    else if (state == "O" || display == "postponed" || display == "canceled" || display == "cancelled")
      live.phase = LivePhase::postponed;
    else if (display == "delayed" || display == "suspended") live.phase = LivePhase::delayed;
    std::string period = text::text(contest["currentPeriod"]).value_or("");
    std::string clock = text::text(contest["contestClock"]).value_or("");
    if (live.phase == LivePhase::final) {
      live.detail = "Final";
      // "FINAL/OT", "FINAL - 2OT": the overtime stays.
      std::string message = str::upper(text::text(contest["finalMessage"]).value_or(""));
      if (auto m = rx::first("(\\d?OT|SO)\\b", message)) live.detail += "/" + std::string(m->group(1));
    } else if (live.phase == LivePhase::live || live.phase == LivePhase::delayed) {
      std::string lower = str::lower(period);
      if (lower.find("half") != std::string::npos && lower.find("1st") == std::string::npos &&
          lower.find("2nd") == std::string::npos) {
        live.detail = "Halftime";
      } else {
        live.detail = readablePeriod(period);
        if (rx::test("^0?\\d+:\\d\\d$", clock) && clock != "00:00" && clock != "0:00") {
          if (clock[0] == '0' && clock.size() > 4) clock = clock.substr(1);  // "08:12" -> "8:12"
          live.detail += (live.detail.empty() ? "" : " ") + clock;
        }
      }
    }
    if (live.phase == LivePhase::live || live.phase == LivePhase::delayed || live.phase == LivePhase::final) {
      for (const json::Value& t : contest["teams"].items()) {
        auto score = text::text(t["score"]);
        if (!score) continue;
        (text::flag(t["isHome"]) ? live.homeScore : live.awayScore) = *score;
      }
    }
    result[league.shortName + "|" + *id] = std::move(live);
  });
  return result;
}

}  // namespace marigold::sports::ncaa
