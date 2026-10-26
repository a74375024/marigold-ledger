#include "core/catalog/core_channels.hpp"

#include <algorithm>

namespace marigold::catalog {

const std::vector<CoreChannel>& coreChannels() {
  // clang-format off
  static const std::vector<CoreChannel> kList = {
      {"CNN", LineupCategory::news, {"cnn", "cnn us", "cnn usa"}},
      {"HLN", LineupCategory::news, {"hln", "headline news", "cnn headline news"}},
      {"Fox News", LineupCategory::news, {"fox news", "fox news channel", "fnc"}},
      {"Fox Business", LineupCategory::news, {"fox business", "fox business network", "fbn"}},
      {"MSNBC", LineupCategory::news, {"msnbc", "ms now"}},
      {"CNBC", LineupCategory::news, {"cnbc"}},
      {"Bloomberg TV", LineupCategory::news, {"bloomberg", "bloomberg tv", "bloomberg television"}},
      {"NewsNation", LineupCategory::news, {"newsnation", "news nation"}},
      {"Newsmax", LineupCategory::news, {"newsmax", "newsmax tv"}},
      {"C-SPAN", LineupCategory::news, {"c span", "cspan", "c span 1"}},
      {"C-SPAN 2", LineupCategory::news, {"c span 2", "cspan 2", "cspan2"}},
      {"The Weather Channel", LineupCategory::news, {"weather channel", "the weather channel", "twc"}},
      {"ABC News Live", LineupCategory::news, {"abc news live", "abc news"}},
      {"CBS News", LineupCategory::news, {"cbs news", "cbs news 24 7", "cbs news national stream"}},
      {"NBC News Now", LineupCategory::news, {"nbc news now", "nbc news"}},
      {"Court TV", LineupCategory::news, {"court tv"}},
      {"Scripps News", LineupCategory::news, {"scripps news"}},
      {"ESPN", LineupCategory::sports, {"espn"}},
      {"ESPN2", LineupCategory::sports, {"espn2", "espn 2"}},
      {"ESPNU", LineupCategory::sports, {"espnu", "espn u"}},
      {"ESPNews", LineupCategory::sports, {"espnews", "espn news"}},
      {"SEC Network", LineupCategory::sports, {"sec network", "espn sec network", "secn"}},
      {"ACC Network", LineupCategory::sports, {"acc network", "espn acc network", "accn"}},
      {"Big Ten Network", LineupCategory::sports, {"big ten network", "btn", "b1g network", "big ten"}},
      {"FS1", LineupCategory::sports, {"fs1", "fox sports 1", "fox sports1"}},
      {"FS2", LineupCategory::sports, {"fs2", "fox sports 2", "fox sports2"}},
      {"NFL Network", LineupCategory::sports, {"nfl network"}},
      {"NFL RedZone", LineupCategory::sports, {"nfl redzone", "redzone", "nfl red zone"}},
      {"MLB Network", LineupCategory::sports, {"mlb network"}},
      {"NBA TV", LineupCategory::sports, {"nba tv", "nbatv"}},
      {"NHL Network", LineupCategory::sports, {"nhl network"}},
      {"Golf Channel", LineupCategory::sports, {"golf channel", "golf"}},
      {"Tennis Channel", LineupCategory::sports, {"tennis channel"}},
      {"CBS Sports Network", LineupCategory::sports, {"cbs sports network", "cbssn", "cbs sports"}},
      {"beIN Sports", LineupCategory::sports, {"bein sports", "bein sports usa"}},
      {"TNT", LineupCategory::entertainment, {"tnt", "tnt east"}},
      {"TBS", LineupCategory::entertainment, {"tbs", "tbs east"}},
      {"truTV", LineupCategory::entertainment, {"trutv", "tru tv"}},
      {"USA Network", LineupCategory::entertainment, {"usa network", "usa"}},
      {"FX", LineupCategory::entertainment, {"fx"}},
      {"FXX", LineupCategory::entertainment, {"fxx"}},
      {"AMC", LineupCategory::entertainment, {"amc"}},
      {"BBC America", LineupCategory::entertainment, {"bbc america"}},
      {"Bravo", LineupCategory::entertainment, {"bravo"}},
      {"E!", LineupCategory::entertainment, {"e", "e entertainment", "e entertainment television"}},
      {"Syfy", LineupCategory::entertainment, {"syfy"}},
      {"Paramount Network", LineupCategory::entertainment, {"paramount network"}},
      {"Comedy Central", LineupCategory::entertainment, {"comedy central"}},
      {"A&E", LineupCategory::entertainment, {"a and e", "ae"}},
      {"History", LineupCategory::entertainment, {"history", "history channel"}},
      {"Lifetime", LineupCategory::entertainment, {"lifetime"}},
      {"LMN", LineupCategory::entertainment, {"lmn", "lifetime movies", "lifetime movie network"}},
      {"Hallmark Channel", LineupCategory::entertainment, {"hallmark", "hallmark channel"}},
      {"Hallmark Family", LineupCategory::entertainment, {"hallmark family", "hallmark drama"}},
      {"Hallmark Movies & Mysteries", LineupCategory::entertainment, {"hallmark movies and mysteries", "hallmark mystery", "hallmark mysteries", "hmm"}},
      {"Discovery Channel", LineupCategory::entertainment, {"discovery", "discovery channel"}},
      {"TLC", LineupCategory::entertainment, {"tlc"}},
      {"Animal Planet", LineupCategory::entertainment, {"animal planet"}},
      {"Investigation Discovery", LineupCategory::entertainment, {"investigation discovery", "id", "id investigation discovery", "investigation discovery us"}},
      {"Science Channel", LineupCategory::entertainment, {"science", "science channel"}},
      {"HGTV", LineupCategory::entertainment, {"hgtv", "home and garden television", "home and garden tv"}},
      {"Food Network", LineupCategory::entertainment, {"food network", "food"}},
      {"Travel Channel", LineupCategory::entertainment, {"travel channel", "travel", "the travel channel"}},
      {"Cooking Channel", LineupCategory::entertainment, {"cooking channel"}},
      {"Magnolia Network", LineupCategory::entertainment, {"magnolia network", "magnolia"}},
      {"National Geographic", LineupCategory::entertainment, {"national geographic", "nat geo", "national geographic channel"}},
      {"Nat Geo Wild", LineupCategory::entertainment, {"nat geo wild", "national geographic wild", "nat geo wild us"}},
      {"Smithsonian Channel", LineupCategory::entertainment, {"smithsonian channel", "smithsonian", "smithsonian network"}},
      {"Freeform", LineupCategory::entertainment, {"freeform"}},
      {"Oxygen", LineupCategory::entertainment, {"oxygen", "oxygen true crime"}},
      {"WE tv", LineupCategory::entertainment, {"we tv", "wetv"}},
      {"OWN", LineupCategory::entertainment, {"own", "oprah winfrey network"}},
      {"TV Land", LineupCategory::entertainment, {"tv land", "tvland"}},
      {"IFC", LineupCategory::entertainment, {"ifc"}},
      {"SundanceTV", LineupCategory::entertainment, {"sundance", "sundance tv", "sundancetv"}},
      {"Reelz", LineupCategory::entertainment, {"reelz", "reelzchannel", "reelz channel"}},
      {"Game Show Network", LineupCategory::entertainment, {"game show network", "gsn"}},
      {"BET", LineupCategory::entertainment, {"bet"}},
      {"VH1", LineupCategory::entertainment, {"vh1"}},
      {"Pop TV", LineupCategory::entertainment, {"pop", "pop tv"}},
      {"Motor Trend", LineupCategory::entertainment, {"motor trend", "motortrend"}},
      {"AXS TV", LineupCategory::entertainment, {"axs", "axs tv"}},
      {"MTV", LineupCategory::music, {"mtv", "mtv music television"}},
      {"CMT", LineupCategory::music, {"cmt"}},
      {"Disney Channel", LineupCategory::kids, {"disney channel", "disney"}},
      {"Disney Junior", LineupCategory::kids, {"disney junior", "disney jr"}},
      {"Disney XD", LineupCategory::kids, {"disney xd"}},
      {"Nickelodeon", LineupCategory::kids, {"nickelodeon", "nick"}},
      {"Nick Jr.", LineupCategory::kids, {"nick jr", "nickjr", "nick junior"}},
      {"Nicktoons", LineupCategory::kids, {"nicktoons"}},
      {"TeenNick", LineupCategory::kids, {"teennick", "teen nick"}},
      {"Cartoon Network", LineupCategory::kids, {"cartoon network"}},
      {"Boomerang", LineupCategory::kids, {"boomerang"}},
      {"Universal Kids", LineupCategory::kids, {"universal kids"}},
      {"Discovery Family", LineupCategory::kids, {"discovery family", "discovery family channel"}},
      {"HBO", LineupCategory::movies, {"hbo"}},
      {"HBO 2", LineupCategory::movies, {"hbo 2", "hbo2"}},
      {"HBO Signature", LineupCategory::movies, {"hbo signature"}},
      {"HBO Family", LineupCategory::movies, {"hbo family"}},
      {"HBO Comedy", LineupCategory::movies, {"hbo comedy"}},
      {"HBO Zone", LineupCategory::movies, {"hbo zone"}},
      {"HBO Hits", LineupCategory::movies, {"hbo hits"}},
      {"HBO Drama", LineupCategory::movies, {"hbo drama"}},
      {"HBO Movies", LineupCategory::movies, {"hbo movies"}},
      {"Cinemax", LineupCategory::movies, {"cinemax"}},
      {"Showtime", LineupCategory::movies, {"showtime", "sho", "paramount plus with showtime"}},
      {"Showtime 2", LineupCategory::movies, {"showtime 2", "sho 2"}},
      {"Starz", LineupCategory::movies, {"starz"}},
      {"Starz Encore", LineupCategory::movies, {"starz encore", "encore"}},
      {"MGM+", LineupCategory::movies, {"mgm plus", "mgm", "epix"}},
      {"TCM", LineupCategory::movies, {"tcm", "turner classic movies"}},
      {"The Movie Channel", LineupCategory::movies, {"the movie channel", "tmc", "movie channel"}},
      {"FXM", LineupCategory::movies, {"fxm", "fx movie channel"}},
  };
  // clang-format on
  return kList;
}

const std::unordered_map<std::string, CoreHit>& coreByAlias() {
  static const std::unordered_map<std::string, CoreHit> kMap = [] {
    std::unordered_map<std::string, CoreHit> map;
    for (const CoreChannel& entry : coreChannels()) {
      for (const std::string& alias : entry.aliases)
        map.emplace(alias, CoreHit{entry.name, entry.category});
    }
    return map;
  }();
  return kMap;
}

const std::vector<std::string>& regionWords() {
  // clang-format off
  static const std::vector<std::string> kWords = {
      "alabama", "alaska", "arizona", "arkansas", "california", "colorado", "connecticut",
      "delaware", "florida", "georgia", "hawaii", "idaho", "illinois", "indiana", "iowa",
      "kansas", "kentucky", "louisiana", "maine", "maryland", "massachusetts", "michigan",
      "minnesota", "mississippi", "missouri", "montana", "nebraska", "nevada", "new hampshire",
      "new jersey", "new mexico", "new york", "north carolina", "north dakota", "ohio",
      "oklahoma", "oregon", "pennsylvania", "rhode island", "south carolina", "south dakota",
      "tennessee", "texas", "utah", "vermont", "virginia", "washington", "west virginia",
      "wisconsin", "wyoming", "atlanta", "austin", "baltimore", "birmingham", "boston",
      "brooklyn", "buffalo", "charlotte", "chicago", "cincinnati", "cleveland", "columbus",
      "dallas", "denver", "detroit", "el paso", "fresno", "hartford", "honolulu", "houston",
      "indianapolis", "jacksonville", "kansas city", "las vegas", "los angeles", "louisville",
      "memphis", "miami", "milwaukee", "minneapolis", "nashville", "new orleans", "newark",
      "oakland", "oklahoma city", "omaha", "orlando", "philadelphia", "phoenix", "pittsburgh",
      "portland", "providence", "raleigh", "richmond", "sacramento", "salt lake", "san antonio",
      "san diego", "san francisco", "san jose", "seattle", "st louis", "saint louis", "tampa",
      "tucson", "tulsa", "washington dc", "bay area", "new england", "mid atlantic", "midwest",
      "northwest", "southwest", "southeast", "sun", "rocky mountain", "great lakes", "carolinas",
      "tri state", "socal", "norcal", "long island", "hudson valley", "westchester", "bronx",
      "queens", "staten island", "abilene", "burlington", "gainesville", "wheeling"
  };
  // clang-format on
  return kWords;
}

const std::string& regionPattern() {
  static const std::string kPattern = [] {
    std::vector<std::string> words = regionWords();
    std::stable_sort(words.begin(), words.end(), [](const std::string& a, const std::string& b) {
      return a.size() > b.size();
    });
    // The words are plain letters and spaces: nothing to escape.
    std::string p = "\\b(";
    for (size_t i = 0; i < words.size(); ++i) {
      if (i) p += '|';
      p += words[i];
    }
    return p + ")\\b";
  }();
  return kPattern;
}

}  // namespace marigold::catalog
