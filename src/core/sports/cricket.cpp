#include "core/sports/cricket.hpp"

#include <algorithm>
#include <cmath>

#include "core/sports/parse.hpp"
#include "core/sports/profiles.hpp"
#include "core/sports/text.hpp"
#include "core/util/json.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::sports::cricket {
namespace {

// Every match in the listing, with its series' name.
void forEachMatch(std::string_view body, const std::function<void(const json::Value&, const std::string&)>& fn) {
  auto doc = json::parse(body);
  if (!doc) return;
  for (const json::Value& sport : (*doc)["sports"].items()) {
    for (const json::Value& series : sport["leagues"].items()) {
      std::string name = text::text(series["name"]).value_or("");
      for (const json::Value& match : series["events"].items()) {
        if (match.isObject()) fn(match, name);
      }
    }
  }
}

// "18.4" overs -> 112 balls.
int balls(std::string_view overs) {
  size_t dot = overs.find('.');
  int whole = text::toInt(std::string(overs.substr(0, dot))).value_or(0);
  int part = dot == std::string_view::npos ? 0 : text::toInt(std::string(overs.substr(dot + 1))).value_or(0);
  return whole * 6 + part;
}

}  // namespace

std::string listingUrl() { return "https://site.web.api.espn.com/apis/v2/scoreboard/header?sport=cricket"; }

std::string shown(std::string_view score) {
  std::string out;
  int depth = 0;
  for (char c : score) {
    if (c == '(') depth += 1;
    else if (c == ')') depth = std::max(0, depth - 1);
    else if (depth == 0) out += c;
  }
  // Collapse the spaces the details leave behind.
  std::string tidy;
  for (char c : out) {
    if (c == ' ' && (tidy.empty() || tidy.back() == ' ')) continue;
    tidy += c;
  }
  return std::string(str::trimUnicode(tidy));
}

bool batting(std::string_view score) { return rx::test("\\(\\s*\\d+(\\.\\d)?(/\\d+)?\\s*ov", score); }

std::optional<std::string> situation(std::string_view battingScore) {
  std::string s(battingScore);
  // The innings batting now: the last one ("317 & 168/8 (...)" -> "168/8").
  std::string last = shown(s);
  if (size_t amp = last.rfind('&'); amp != std::string::npos) last = std::string(str::trimUnicode(last.substr(amp + 1)));
  int runs = text::toInt(last.substr(0, last.find('/'))).value_or(-1);
  auto overs = rx::first("(\\d+(?:\\.\\d)?)(?:/(\\d+))?\\s*ov", s);
  auto target = rx::first("target\\s+(\\d+)", s, true);
  bool followOn = s.find("(f/o)") != std::string::npos;
  std::string out;
  if (target && runs >= 0) {
    int need = text::toInt(std::string(target->group(1))).value_or(0) - runs;
    if (need > 0 && overs && !overs->group(2).empty()) {
      int left = text::toInt(std::string(overs->group(2))).value_or(0) * 6 - balls(overs->group(1));
      if (left > 0) out = "Need " + std::to_string(need) + " from " + std::to_string(left) + (left == 1 ? " ball" : " balls");
    }
    if (out.empty() && need > 0) out = "Need " + std::to_string(need) + " to win";
  }
  if (out.empty() && overs) {
    out = std::string(overs->group(1)) + (overs->group(2).empty() ? "" : "/" + std::string(overs->group(2))) + " ov";
  }
  if (followOn) out += out.empty() ? "Follow-on" : " \xC2\xB7 follow-on";
  if (out.empty()) return std::nullopt;
  return out;
}

std::optional<std::vector<SportsGame>> parseMatches(std::string_view body, const League& league) {
  if (!json::parse(body)) return std::nullopt;
  std::vector<SportsGame> games;
  forEachMatch(body, [&](const json::Value& match, const std::string& series) {
    auto id = text::text(match["id"]);
    auto start = text::date(text::text(match["date"]));
    if (!id || !start) return;
    std::optional<SportsTeam> home, away;
    for (const json::Value& c : match["competitors"].items()) {
      auto side = parse::team(c);
      if (!side) continue;
      if (text::text(c["homeAway"]) == std::optional<std::string>("home")) home = std::move(side);
      else away = std::move(side);
    }
    if (!home || !away) return;
    SportsGame game;
    game.id = league.shortName + "|" + *id;
    game.league = league.group;
    game.leagueShort = league.shortName;
    game.homeFirst = profile(league.group).homeFirst;
    game.home = *home;
    game.away = *away;
    game.start = *start;
    std::string state = str::lower(text::text(match["status"]).value_or(""));
    game.completed = state == "post";
    game.matchFormat = text::text(match["class"]["eventType"]);
    if (auto end = text::date(text::text(match["endDate"])); end && *end > *start) game.end = end;
    // "3rd ODI, India tour of New Zealand at Hamilton, ..." -> "3rd ODI".
    std::string label = text::components(text::text(match["description"]).value_or(""), ",").front();
    label = std::string(str::trimUnicode(label));
    if (!label.empty() && text::count(label) <= 26) game.note = label;
    if (!series.empty()) game.venue = series;
    games.push_back(std::move(game));
  });
  return games;
}

scores::Statuses statuses(std::string_view body, const League& league) {
  scores::Statuses result;
  forEachMatch(body, [&](const json::Value& match, const std::string&) {
    auto id = text::text(match["id"]);
    if (!id) return;
    const json::Value& type = match["fullStatus"]["type"];
    std::string name = text::text(type["name"]).value_or("");
    std::string summary = text::text(match["summary"]).value_or("");
    bool suspended = false;
    LiveStatus live;
    live.phase = scores::livePhase(league, text::text(match["status"]), name, &suspended);
    if (live.phase == LivePhase::pre && text::flag(type["completed"])) live.phase = LivePhase::final;
    std::string lower = str::lower(summary);
    // Rain or bad light: play stopped, to resume.
    if (live.phase == LivePhase::live && (lower.find("delayed") != std::string::npos || lower.find("rain") != std::string::npos ||
                                          lower.find("bad light") != std::string::npos)) {
      live.phase = LivePhase::delayed;
    }
    live.suspended = suspended;
    // A break in play, or the end of a day's play: under way all the same.
    std::string described = text::text(type["description"]).value_or("");
    for (const std::string& word : {described, summary}) {
      std::string w = str::lower(word);
      if (live.phase != LivePhase::live || live.pause) break;
      if (w == "stumps" || w == "lunch" || w == "tea" || w == "dinner" || w == "drinks" || w == "innings break")
        live.pause = word;
    }
    live.dayOver = live.pause && str::lower(*live.pause) == "stumps";
    if (live.phase == LivePhase::final) {
      live.detail = "Final";
    } else if (live.phase == LivePhase::live || live.phase == LivePhase::delayed) {
      int period = static_cast<int>(match["period"].isNumber() ? std::lround(match["period"].asNumber()) : 0);
      static const char* const kInnings[] = {"1st innings", "2nd innings", "3rd innings", "4th innings"};
      live.detail = !summary.empty() && summary != "Live" ? summary : kInnings[std::clamp(period, 1, 4) - 1];
      if (live.pause) {
        // "Stumps · Day 3"; "Tea"; "Innings break".
        int day = static_cast<int>(match["fullStatus"]["dayNumber"].isNumber()
                                       ? std::lround(match["fullStatus"]["dayNumber"].asNumber())
                                       : 0);
        live.detail = live.dayOver && day > 0 ? *live.pause + " \xc2\xb7 Day " + std::to_string(day) : *live.pause;
      }
    }
    if (live.phase != LivePhase::pre) {
      for (const json::Value& c : match["competitors"].items()) {
        std::string score = text::text(c["score"]).value_or("");
        bool home = text::text(c["homeAway"]) == std::optional<std::string>("home");
        if (!score.empty()) (home ? live.homeScore : live.awayScore) = shown(score);
        if (live.phase != LivePhase::final && batting(score)) {
          live.possession = text::text(c["id"]);
          live.situation = situation(score);
        }
      }
    }
    result[league.shortName + "|" + *id] = std::move(live);
  });
  return result;
}

}  // namespace marigold::sports::cricket
