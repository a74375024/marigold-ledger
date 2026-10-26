#include "core/guide/genre.hpp"

#include <algorithm>

#include "core/util/rx.hpp"

namespace marigold::guide::genre {
namespace {

const char* const kMovieWords[] = {"movie", "film", "cinema"};
const char* const kSportsWords[] = {
    "sport",    "football",   "basketball",      "baseball",  "hockey",        "soccer",
    "golf",     "tennis",     "boxing",          "wrestling", "mixed martial", "mma",
    "racing",   "motorsport", "olympic",         "rugby",     "cricket",       "volleyball",
    "softball", "lacrosse",   "bowling",         "skiing",    "swimming",      "athletics",
    "cycling",  "gymnastics", "track and field", "rodeo",     "billiards",     "darts",
    "poker"};
const char* const kNewsWords[] = {"news", "weather", "public affairs", "current affairs",
                                  "newsmagazine"};
const char* const kKidsWords[] = {"kids",     "children", "animation",
                                  "animated", "cartoon",  "preschool"};

template <size_t N>
bool any(std::string_view text, const char* const (&words)[N]) {
  for (const char* w : words) {
    if (text.find(w) != std::string_view::npos) return true;
  }
  return false;
}

const char* const kSportsTitle =
    "\\b(NFL|NBA|WNBA|MLB|MiLB|NHL|MLS|NWSL|NCAA[A-Z]*|UFC|WWE|AEW|PGA|LPGA|NASCAR|IndyCar|MotoGP|"
    "Formula 1|F1|UEFA|FIFA|Premier League|Champions League|La Liga|Serie A|Bundesliga|Ligue 1|"
    "Super Bowl|World Series|Stanley Cup|SportsCenter|Pregame|Postgame|Pre-Game|Post-Game)\\b"
    "|\\b(College |Pro |High School |Women's |Men's )?(Football|Basketball|Baseball|Hockey|Soccer|"
    "Softball|Volleyball|Lacrosse|Golf|Tennis|Boxing|Rugby|Cricket|Auto Racing|Horse Racing|"
    "Bowling|Gymnastics|Wrestling)\\b";
const char* const kNewsTitle =
    "\\b(news|newscast|newsroom|newsnight|newshour|good morning america|cbs mornings|meet the "
    "press|face the nation|this week with|60 minutes|20/20|dateline|nightline|first "
    "alert|eyewitness|action news|weather)\\b|^today( show)?(\\b(?!['’])|$)";

bool tagged(const Program& p) { return (p.genres & kGenreBits) != 0; }

}  // namespace

int flags(std::string_view joined) {
  if (joined.empty()) return 0;
  int f = 0;
  if (any(joined, kMovieWords)) f |= kMovie;
  if (any(joined, kSportsWords)) f |= kSports;
  if (any(joined, kNewsWords)) f |= kNews;
  if (any(joined, kKidsWords)) f |= kKids;
  return f;
}

int flagsForTitle(std::string_view title, const std::optional<std::string>& episode) {
  std::string text(title);
  if (episode) {
    text += ' ';
    text += *episode;
  }
  int f = 0;
  if (rx::test(kSportsTitle, text, true)) f |= kSports;
  if (rx::test(kNewsTitle, title, true)) f |= kNews;
  return f;
}

Program withTitleFlags(const Program& program) {
  if (tagged(program)) return program;
  int f = flagsForTitle(program.title, program.episodeTitle);
  if (f == 0) return program;
  Program updated = program;
  updated.genres = static_cast<uint8_t>((updated.genres & ~kGenreBits) | (f & (kSports | kNews)));
  return updated;
}

std::vector<Program> mergeFlags(const std::vector<Program>& list,
                                const std::vector<Program>& other) {
  if (other.empty()) return list;
  std::vector<const Program*> taggedOther;
  for (const Program& p : other) {
    if (tagged(p)) taggedOther.push_back(&p);
  }
  if (taggedOther.empty()) return list;
  std::vector<Program> out;
  out.reserve(list.size());
  for (const Program& program : list) {
    out.push_back(program);
    if (tagged(program)) continue;
    time::Seconds length = program.end - program.start;
    if (length <= 0) continue;
    for (const Program* candidate : taggedOther) {
      time::Seconds overlap =
          std::min(candidate->end, program.end) - std::max(candidate->start, program.start);
      // More than half the programme (Swift compares seconds as doubles).
      if (overlap * 2 > length) {
        Program& updated = out.back();
        updated.genres =
            static_cast<uint8_t>((updated.genres & ~kGenreBits) | (candidate->genres & kGenreBits));
        break;
      }
    }
  }
  return out;
}

}  // namespace marigold::guide::genre
