#include "core/sports/leagues.hpp"

#include <algorithm>
#include <set>

#include "core/sports/matcher.hpp"
#include "core/sports/my_sports.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::sports {

namespace {

League make(std::string path, std::string group, std::string shortName, std::string name,
            std::string sport, bool soccer) {
  League l;
  l.path = std::move(path);
  l.group = std::move(group);
  l.shortName = std::move(shortName);
  l.name = std::move(name);
  l.sport = std::move(sport);
  l.soccer = soccer;
  return l;
}

League standard(League l) {
  l.standard = true;
  return l;
}

League keywords(League l, std::vector<std::string> words) {
  l.keywords = std::move(words);
  return l;
}

}  // namespace

const std::vector<League>& leagues() {
  static const std::vector<League> kLeagues = [] {
    std::vector<League> v;
    v.push_back(standard(make("football/nfl", "NFL", "NFL", "NFL", "Football", false)));
    League cfb = standard(make("football/college-football", "College Football", "NCAAF",
                               "College Football", "Football", false));
    cfb.extra = "&groups=80";
    cfb.mode = League::Mode::week;
    v.push_back(cfb);
    v.push_back(standard(make("basketball/nba", "NBA", "NBA", "NBA", "Basketball", false)));
    v.push_back(make("basketball/wnba", "WNBA", "WNBA", "WNBA", "Basketball", false));
    League mcb = make("basketball/mens-college-basketball", "College Basketball", "NCAAM",
                      "Men's College", "Basketball", false);
    mcb.extra = "&groups=50";
    mcb.mode = League::Mode::days;
    mcb.days = 3;
    v.push_back(mcb);
    League wcb = make("basketball/womens-college-basketball", "College Basketball", "NCAAW",
                      "Women's College", "Basketball", false);
    wcb.extra = "&groups=50";
    wcb.mode = League::Mode::days;
    wcb.days = 3;
    wcb.keywords = {"women", "womens"};
    v.push_back(wcb);
    v.push_back(standard(make("baseball/mlb", "MLB", "MLB", "MLB", "Baseball", false)));
    v.push_back(standard(make("hockey/nhl", "NHL", "NHL", "NHL", "Hockey", false)));
    v.push_back(keywords(standard(make("soccer/usa.1", "MLS", "MLS", "MLS", "Soccer", true)),
                         {"mls", "major league soccer"}));
    v.push_back(keywords(
        standard(make("soccer/eng.1", "Soccer", "EPL", "Premier League", "Soccer", true)),
        {"premier league", "epl"}));
    v.push_back(keywords(standard(make("soccer/uefa.champions", "Soccer", "UCL",
                                       "Champions League", "Soccer", true)),
                         {"champions league"}));
    v.push_back(keywords(standard(make("soccer/esp.1", "Soccer", "LIGA", "La Liga", "Soccer", true)),
                         {"laliga", "la liga"}));
    v.push_back(keywords(make("soccer/mex.1", "Soccer", "LMX", "Liga MX", "Soccer", true), {"liga mx"}));
    v.push_back(keywords(make("soccer/ita.1", "Soccer", "SA", "Serie A", "Soccer", true), {"serie a"}));
    v.push_back(
        keywords(make("soccer/ger.1", "Soccer", "BUN", "Bundesliga", "Soccer", true), {"bundesliga"}));
    v.push_back(keywords(make("soccer/fra.1", "Soccer", "L1", "Ligue 1", "Soccer", true),
                         {"ligue 1", "ligue1"}));
    v.push_back(keywords(make("soccer/uefa.europa", "Soccer", "UEL", "Europa League", "Soccer", true),
                         {"europa league"}));
    v.push_back(keywords(
        make("soccer/uefa.europa.conf", "Soccer", "UECL", "Conference League", "Soccer", true),
        {"conference league"}));
    v.push_back(keywords(make("soccer/eng.2", "Soccer", "EFL", "Championship", "Soccer", true),
                         {"championship", "efl"}));
    v.push_back(keywords(make("soccer/por.1", "Soccer", "POR", "Liga Portugal", "Soccer", true),
                         {"liga portugal", "primeira liga"}));
    v.push_back(
        keywords(make("soccer/ned.1", "Soccer", "ERE", "Eredivisie", "Soccer", true), {"eredivisie"}));
    v.push_back(keywords(make("soccer/usa.nwsl", "Soccer", "NWSL", "NWSL", "Soccer", true), {"nwsl"}));
    return v;
  }();
  return kLeagues;
}

const std::vector<League>& fightLeagues() {
  static const std::vector<League> kFights = [] {
    League ufc = make("mma/ufc", "Fighting", "UFC", "UFC", "Fighting", false);
    ufc.fights = true;
    League pfl = make("mma/pfl", "Fighting", "PFL", "PFL", "Fighting", false);
    pfl.fights = true;
    pfl.keywords = {"pfl"};
    return std::vector<League>{ufc, pfl};
  }();
  return kFights;
}

const std::vector<League>& racingLeagues() {
  static const std::vector<League> kRacing = [] {
    League f1 = make("racing/f1", "Motorsport", "F1", "Formula 1", "Motorsport", false);
    f1.mode = League::Mode::current;
    f1.keywords = {"formula 1", "f1"};
    f1.racing = true;
    // NASCAR Cup: its race and TV network, so your local stations carrying
    // it are on the race's tile.
    League nascar = make("racing/nascar-premier", "Motorsport", "NASCAR", "NASCAR Cup Series",
                         "Motorsport", false);
    nascar.mode = League::Mode::current;
    nascar.keywords = {"nascar"};
    nascar.racing = true;
    League indycar = make("racing/irl", "Motorsport", "IRL", "IndyCar Series", "Motorsport", false);
    indycar.mode = League::Mode::current;
    indycar.keywords = {"indycar", "indy 500", "indianapolis 500"};
    indycar.racing = true;
    return std::vector<League>{f1, nascar, indycar};
  }();
  return kRacing;
}

const std::vector<League>& tennisLeagues() {
  static const std::vector<League> kTennis = [] {
    League atp = make("tennis/atp", "Tennis", "ATP", "ATP", "Tennis", false);
    atp.mode = League::Mode::current;
    atp.keywords = {"atp"};
    atp.tennis = true;
    League wta = make("tennis/wta", "Tennis", "WTA", "WTA", "Tennis", false);
    wta.mode = League::Mode::current;
    wta.keywords = {"wta"};
    wta.tennis = true;
    return std::vector<League>{atp, wta};
  }();
  return kTennis;
}

namespace {

// College sports: too many games for a month at once, so a few days at a time.
League college(const char* path, const char* group, const char* shortName, const char* name) {
  League l = make(path, group, shortName, name, group, false);
  l.mode = League::Mode::days;
  l.days = 3;
  return l;
}

struct SportList {
  const char* id;
  std::vector<League> leagues;
};

const std::vector<SportList>& sportLists() {
  static const std::vector<SportList> kLists = [] {
    std::vector<SportList> v;
    auto rugby = [](const char* id, const char* shortName, const char* name, std::vector<std::string> words) {
      return keywords(make(std::string("rugby/") + id, "Rugby", shortName, name, "Rugby", false), std::move(words));
    };
    v.push_back({"S:Rugby",
                 {rugby("180659", "6N", "Six Nations", {"six nations", "6 nations"}),
                  rugby("17567", "RNC", "Nations Championship", {"nations championship"}),
                  rugby("244293", "TRC", "The Rugby Championship", {"rugby championship"}),
                  rugby("164205", "RWC", "Rugby World Cup", {"rugby world cup"}),
                  rugby("289237", "WRWC", "Women's Rugby World Cup", {"women's rugby world cup"}),
                  rugby("289234", "TEST", "Test Matches", {"test match", "autumn nations"}),
                  rugby("268565", "LIONS", "Lions Tour", {"lions tour", "british and irish lions"}),
                  rugby("267979", "PREM", "Premiership", {"premiership rugby", "gallagher prem"}),
                  rugby("270557", "URC", "United Rugby Championship", {"urc", "united rugby championship"}),
                  rugby("270559", "T14", "Top 14", {"top 14"}),
                  rugby("271937", "ECC", "Champions Cup", {"champions cup"}),
                  rugby("272073", "ECH", "Challenge Cup", {"challenge cup"}),
                  rugby("242041", "SRP", "Super Rugby Pacific", {"super rugby"}),
                  rugby("289262", "MLR", "Major League Rugby", {"major league rugby", "mlr"})}});
    v.push_back({"S:Volleyball",
                 {college("volleyball/womens-college-volleyball", "Volleyball", "NCAAWV", "Women's College"),
                  college("volleyball/mens-college-volleyball", "Volleyball", "NCAAMV", "Men's College")}});
    League pll = make("lacrosse/pll", "Lacrosse", "PLL", "Premier Lacrosse League", "Lacrosse", false);
    pll.keywords = {"pll", "premier lacrosse"};
    v.push_back({"S:Lacrosse",
                 {college("lacrosse/mens-college-lacrosse", "Lacrosse", "NCAAML", "Men's College"),
                  college("lacrosse/womens-college-lacrosse", "Lacrosse", "NCAAWL", "Women's College"), pll}});
    v.push_back({"S:FieldHockey",
                 {college("field-hockey/womens-college-field-hockey", "Field Hockey", "NCAAFH", "College")}});
    v.push_back({"S:Softball", {college("baseball/college-softball", "Softball", "NCAASB", "College")}});
    return v;
  }();
  return kLists;
}

}  // namespace

const std::vector<League>& sportLeagues(std::string_view sportID) {
  static const std::vector<League> kNone;
  for (const SportList& list : sportLists()) {
    if (list.id == sportID) return list.leagues;
  }
  return kNone;
}

const std::vector<League>& teamSportLeagues() {
  static const std::vector<League> kAll = [] {
    std::vector<League> v;
    for (const SportList& list : sportLists()) v.insert(v.end(), list.leagues.begin(), list.leagues.end());
    return v;
  }();
  return kAll;
}

const std::vector<League>& golfLeagues() {
  static const std::vector<League> kGolf = [] {
    auto tour = [](const char* path, const char* shortName, const char* name, std::vector<std::string> words) {
      League l = make(path, "Golf", shortName, name, "Golf", false);
      l.mode = League::Mode::current;
      l.keywords = std::move(words);
      l.golf = true;
      return l;
    };
    return std::vector<League>{tour("golf/pga", "PGA", "PGA Tour", {"pga tour"}),
                               tour("golf/lpga", "LPGA", "LPGA Tour", {"lpga"}),
                               tour("golf/liv", "LIV", "LIV Golf", {"liv golf"}),
                               tour("golf/eur", "DPWT", "DP World Tour", {"dp world", "european tour"})};
  }();
  return kGolf;
}

const League& cricketLeague() {
  static const League kCricket = [] {
    League l = make("cricket", "Cricket", "CRIC", "Cricket", "Cricket", false);
    l.mode = League::Mode::current;
    l.cricket = true;
    return l;
  }();
  return kCricket;
}

const std::vector<League>& allLeagues() {
  static const std::vector<League> kAll = [] {
    std::vector<League> v = leagues();
    for (const auto* list : {&fightLeagues(), &racingLeagues(), &tennisLeagues(), &golfLeagues(), &teamSportLeagues()}) {
      v.insert(v.end(), list->begin(), list->end());
    }
    v.push_back(cricketLeague());
    return v;
  }();
  return kAll;
}

const League* leagueByShort(std::string_view shortName) {
  for (const League& l : allLeagues()) {
    if (l.shortName == shortName) return &l;
  }
  return nullptr;
}

json::Value toJson(const SportFeedsSeen& seen) {
  json::Value v = json::Value::object();
  if (seen.racing) v.set("racing", static_cast<int64_t>(*seen.racing));
  if (seen.tennis) v.set("tennis", static_cast<int64_t>(*seen.tennis));
  if (seen.nascar) v.set("nascar", static_cast<int64_t>(*seen.nascar));
  if (seen.golf) v.set("golf", static_cast<int64_t>(*seen.golf));
  if (seen.cricket) v.set("cricket", static_cast<int64_t>(*seen.cricket));
  return v;
}

SportFeedsSeen sportFeedsSeenFromJson(const json::Value& v) {
  SportFeedsSeen seen;
  if (v["racing"].isNumber()) seen.racing = v["racing"].asInt();
  if (v["tennis"].isNumber()) seen.tennis = v["tennis"].asInt();
  if (v["nascar"].isNumber()) seen.nascar = v["nascar"].asInt();
  if (v["golf"].isNumber()) seen.golf = v["golf"].asInt();
  if (v["cricket"].isNumber()) seen.cricket = v["cricket"].asInt();
  return seen;
}

std::vector<League> activeLeagues(const MySportsPrefs& prefs, const SportFeedsSeen& seen,
                                  time::Seconds now) {
  std::vector<League> out;
  bool tennisSeen = seen.tennisSeen(now);
  if (!prefs.configured) {
    for (const League& l : leagues()) {
      if (l.standard) out.push_back(l);
    }
    out.insert(out.end(), racingLeagues().begin(), racingLeagues().end());
    if (tennisSeen) out.insert(out.end(), tennisLeagues().begin(), tennisLeagues().end());
    if (seen.golfSeen(now)) out.insert(out.end(), golfLeagues().begin(), golfLeagues().end());
    if (seen.cricketSeen(now)) out.push_back(cricketLeague());
    return out;
  }
  std::set<std::string> picked(prefs.leagues.begin(), prefs.leagues.end());
  for (const League& l : leagues()) {
    if (picked.count(l.shortName)) out.push_back(l);
  }
  if (picked.count("S:Fighting")) out.insert(out.end(), fightLeagues().begin(), fightLeagues().end());
  for (const League& l : racingLeagues()) {
    // F1 and NASCAR also when their feeds were seen lately; IndyCar when picked.
    bool seenIt = l.shortName == "F1" ? seen.racingSeen(now) : l.shortName == "NASCAR" && seen.nascarSeen(now);
    if (picked.count("S:Motorsport") || seenIt) out.push_back(l);
  }
  if (picked.count("S:Tennis") || tennisSeen) {
    out.insert(out.end(), tennisLeagues().begin(), tennisLeagues().end());
  }
  if (picked.count("S:Golf") || seen.golfSeen(now)) out.insert(out.end(), golfLeagues().begin(), golfLeagues().end());
  if (picked.count("S:Cricket") || seen.cricketSeen(now)) out.push_back(cricketLeague());
  for (const SportList& list : sportLists()) {
    if (picked.count(list.id)) out.insert(out.end(), list.leagues.begin(), list.leagues.end());
  }
  return out;
}

bool noteSportFeeds(SportFeedsSeen& seen, std::optional<time::Seconds>& checkedAt,
                    const std::vector<catalog::LiveEvent>& events, time::Seconds now) {
  if (events.empty()) return false;
  if (checkedAt && now - *checkedAt < 6 * 3600) return false;
  checkedAt = now;
  bool found = std::any_of(events.begin(), events.end(), [](const catalog::LiveEvent& e) {
    // Cheap test first; the pattern only for titles that might be F1.
    if (!str::icontains(e.title, "f1") && !str::icontains(e.title, "formula")) return false;
    return str::icontains(e.title, "formula 1") || rx::test("\\bF1(TV)?\\b", e.title, true);
  });
  // Tennis: matches between two players among the event feeds.
  bool tennis = std::any_of(events.begin(), events.end(), [](const catalog::LiveEvent& e) {
    return e.league == "Tennis" && matcher::looksLikeGame(e.title);
  });
  bool nascar = std::any_of(events.begin(), events.end(),
                            [](const catalog::LiveEvent& e) { return str::icontains(e.title, "nascar"); });
  bool golf = std::any_of(events.begin(), events.end(), [](const catalog::LiveEvent& e) { return e.league == "Golf"; });
  bool cricket = std::any_of(events.begin(), events.end(), [](const catalog::LiveEvent& e) { return e.league == "Cricket"; });
  bool changed = false;
  if (nascar) {
    changed = changed || !seen.nascarSeen(now);
    seen.nascar = now;
  }
  if (found) {
    changed = changed || !seen.racingSeen(now);
    seen.racing = now;
  }
  if (tennis) {
    changed = changed || !seen.tennisSeen(now);
    seen.tennis = now;
  }
  if (golf) {
    changed = changed || !seen.golfSeen(now);
    seen.golf = now;
  }
  if (cricket) {
    changed = changed || !seen.cricketSeen(now);
    seen.cricket = now;
  }
  return changed;
}

}  // namespace marigold::sports
