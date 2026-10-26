#include "core/catalog/markets.hpp"

#include <algorithm>
#include <set>
#include <unordered_set>

#include "core/catalog/text.hpp"
#include "core/util/json.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::catalog::markets {
namespace {

std::vector<std::string> partsOf(const std::string& name) {
  MarketChoice choice;
  choice.name = name;
  return choice.cityParts();
}

}  // namespace

std::vector<Market> parseMarkets(std::string_view wikitext) {
  std::vector<Market> list;
  std::unordered_set<std::string> seen;
  for (const rx::Match& m : rx::all("\\[\\[\\s*(Template:[^|\\]]+)\\|([^\\]]+)\\]\\]", wikitext)) {
    std::string templ(str::trimUnicode(m.group(1)));
    std::string name = rx::replace(m.group(2), "\\s*\\(\\d+\\)\\s*$", "");
    name = std::string(str::trimUnicode(name));
    name = str::replaceAll(name, "&nbsp;", " ");
    // Skip navigation templates ("Template:TV lists" etc.).
    std::string lowerName = str::lower(name);
    if (name.empty() || text::count(name) >= 60 || text::contains(lowerName, "list") ||
        text::contains(lowerName, "template") || !seen.insert(lowerName).second) {
      continue;
    }
    list.push_back({name, templ});
  }
  return list;
}

const std::vector<Market>& builtinMarkets() {
  static const std::vector<Market> kMarkets = [] {
    static const char* const kNames[] = {
      "New York",
      "Los Angeles",
      "Chicago",
      "Philadelphia",
      "Dallas-Ft. Worth",
      "Houston",
      "Atlanta",
      "Washington, DC (Hagerstown)",
      "Boston (Manchester)",
      "San Francisco-Oakland-San Jose",
      "Phoenix (Prescott)",
      "Seattle-Tacoma",
      "Tampa-St. Petersburg (Sarasota)",
      "Detroit",
      "Minneapolis-St. Paul",
      "Orlando-Daytona Beach-Melbourne",
      "Denver",
      "Miami-Ft. Lauderdale",
      "Cleveland-Akron (Canton)",
      "Sacramento-Stockton-Modesto",
      "Charlotte",
      "Portland, OR",
      "Raleigh-Durham (Fayetteville)",
      "St. Louis",
      "Indianapolis",
      "Pittsburgh",
      "Nashville",
      "Baltimore",
      "Salt Lake City",
      "San Diego",
      "San Antonio",
      "Columbus, OH",
      "Kansas City",
      "Hartford & New Haven",
      "Austin",
      "Cincinnati",
      "Greenville-Spartanburg-Asheville-Anderson",
      "Milwaukee",
      "West Palm Beach-Ft. Pierce",
      "Las Vegas",
      "Jacksonville",
      "Harrisburg-Lancaster-Lebanon-York",
      "Grand Rapids-Kalamazoo-Battle Creek",
      "Norfolk-Portsmouth-Newport News",
      "Birmingham (Anniston and Tuscaloosa)",
      "Greensboro-High Point-Winston Salem",
      "Oklahoma City",
      "Albuquerque-Santa Fe",
      "Louisville",
      "New Orleans",
      "Memphis",
      "Providence-New Bedford",
      "Ft. Myers-Naples",
      "Buffalo",
      "Fresno-Visalia",
      "Richmond-Petersburg",
      "Mobile-Pensacola (Ft. Walton Beach)",
      "Little Rock-Pine Bluff",
      "Wilkes Barre-Scranton-Hazleton",
      "Knoxville",
      "Tulsa",
      "Albany-Schenectady-Troy",
      "Lexington",
      "Dayton",
      "Tucson (Sierra Vista)",
      "Spokane",
      "Des Moines-Ames",
      "Green Bay-Appleton",
      "Honolulu",
      "Roanoke-Lynchburg",
      "Wichita-Hutchinson",
      "Flint-Saginaw-Bay City",
      "Omaha",
      "Springfield, MO",
      "Huntsville-Decatur (Florence)",
      "Columbia, SC",
      "Madison",
      "Portland-Auburn",
      "Rochester, NY",
      "Harlingen-Weslaco-Brownsville-McAllen",
      "Toledo",
      "Charleston-Huntington",
      "Waco-Temple-Bryan",
      "Savannah",
      "Charleston, SC",
      "Chattanooga",
      "Colorado Springs-Pueblo",
      "Syracuse",
      "El Paso (Las Cruces)",
      "Paducah-Cape Girardeau-Harrisburg",
      "Shreveport",
      "Champaign & Springfield-Decatur",
      "Burlington-Plattsburgh",
      "Cedar Rapids-Waterloo-Iowa City & Dubuque",
      "Baton Rouge",
      "Ft. Smith-Fayetteville-Springdale-Rogers",
      "Myrtle Beach-Florence",
      "Boise",
      "Jackson, MS",
      "South Bend-Elkhart",
      "Tri-Cities, TN-VA",
      "Greenville-New Bern-Washington",
      "Reno",
      "Davenport-Rock Island-Moline",
      "Tallahassee-Thomasville",
      "Tyler-Longview (Lufkin & Nacogdoches)",
      "Lincoln & Hastings-Kearney",
      "Augusta-Aiken",
      "Evansville",
      "Ft. Wayne",
      "Sioux Falls (Mitchell)",
      "Johnstown-Altoona-State College",
      "Fargo-Valley City",
      "Yakima-Pasco-Richland-Kennewick",
      "Springfield-Holyoke",
      "Traverse City-Cadillac",
      "Lansing",
      "Youngstown",
      "Macon",
      "Eugene",
      "Montgomery-Selma",
      "Peoria-Bloomington",
      "Santa Barbara-Santa Maria-San Luis Obispo",
      "Lafayette, LA",
      "Bakersfield",
      "Wilmington",
      "Columbus, GA (Opelika, AL)",
      "Monterey-Salinas",
      "La Crosse-Eau Claire",
      "Corpus Christi",
      "Salisbury",
      "Amarillo",
      "Wausau-Rhinelander",
      "Columbus-Tupelo-West Point-Houston",
      "Columbia-Jefferson City",
      "Chico-Redding",
      "Rockford",
      "Duluth-Superior",
      "Medford-Klamath Falls",
      "Lubbock",
      "Topeka",
      "Monroe-El Dorado",
      "Beaumont-Port Arthur",
      "Odessa-Midland",
      "Palm Springs",
      "Anchorage",
      "Minot-Bismarck-Dickinson",
      "Panama City",
      "Sioux City",
      "Wichita Falls & Lawton",
      "Joplin-Pittsburg",
      "Albany, GA",
      "Rochester-Mason City-Austin",
      "Erie",
      "Idaho Falls-Pocatello (Jackson)",
      "Bangor",
      "Gainesville",
      "Biloxi-Gulfport",
      "Terre Haute",
      "Sherman-Ada",
      "Missoula",
      "Binghamton",
      "Wheeling-Steubenville",
      "Yuma-El Centro",
      "Billings",
      "Abilene-Sweetwater",
      "Bluefield-Beckley-Oak Hill",
      "Hattiesburg-Laurel",
      "Rapid City",
      "Dothan",
      "Utica",
      "Clarksburg-Weston",
      "Harrisonburg",
      "Jackson, TN",
      "Quincy-Hannibal-Keokuk",
      "Charlottesville",
      "Lake Charles",
      "Elmira (Corning)",
      "Watertown",
      "Bowling Green",
      "Marquette",
      "Jonesboro",
      "Alexandria, LA",
      "Laredo",
      "Butte-Bozeman",
      "Bend, OR",
      "Grand Junction-Montrose",
      "Twin Falls",
      "Lafayette, IN",
      "Lima",
      "Great Falls",
      "Meridian",
      "Cheyenne-Scottsbluff",
      "Parkersburg",
      "Greenwood-Greenville",
      "Eureka",
      "San Angelo",
      "Casper-Riverton",
      "Mankato",
      "Ottumwa-Kirksville",
      "St. Joseph",
      "Fairbanks",
      "Zanesville",
      "Victoria",
      "Presque Isle",
      "Helena",
      "Juneau",
      "Alpena",
      "North Platte",
      "Glendive",
    };
    std::vector<Market> list;
    for (const char* name : kNames) list.push_back({name, std::nullopt});
    return list;
  }();
  return kMarkets;
}

std::vector<Market> fallbackMarkets(const std::vector<guide::Station>& stations) {
  std::vector<Market> list;
  std::unordered_set<std::string> seen;
  for (const guide::Station& station : stations) {
    std::vector<std::string> words;
    for (const std::string& t : station.cityTokens) words.push_back(text::capitalized(t));
    std::string city = str::join(words, " ");
    if (seen.insert(city).second) list.push_back({city, std::nullopt});
  }
  std::sort(list.begin(), list.end(),
            [](const Market& a, const Market& b) { return a.name < b.name; });
  return list;
}

std::vector<std::string> callsignsInWikitext(std::string_view wikitext) {
  std::vector<std::string> found;
  for (const rx::Match& m :
       rx::all("\\[\\[\\s*([KW][A-Z]{2,3})(?:-(?:TV|DT|LD|CD|LP|CA))?\\s*(?:\\|[^\\]]*)?\\]\\]",
               wikitext)) {
    std::string sign(m.group(1));
    if (std::find(found.begin(), found.end(), sign) == found.end()) found.push_back(sign);
  }
  return found;
}

std::vector<std::string> fallbackCallsigns(const Market& market,
                                           const std::vector<guide::Station>& stations) {
  std::vector<std::string> parts = partsOf(market.name);
  std::set<std::string> signs;
  for (const guide::Station& station : stations) {
    std::string city = str::join(station.cityTokens, " ");
    bool hit = std::any_of(parts.begin(), parts.end(), [&](const std::string& p) {
      return p == city || text::contains(p, city) || text::contains(city, p);
    });
    if (hit) signs.insert(station.callsign);
  }
  return std::vector<std::string>(signs.begin(), signs.end());
}

std::vector<Market> suggested(const std::vector<Market>& markets,
                              const std::vector<std::string>& channelNames, size_t limit) {
  std::vector<std::string> names;
  names.reserve(channelNames.size());
  for (const std::string& n : channelNames) names.push_back(str::lower(n));
  std::vector<std::pair<const Market*, int>> scored;
  for (const Market& market : markets) {
    std::vector<std::string> parts = partsOf(market.name);
    if (parts.empty()) continue;
    int hits = 0;
    for (const std::string& name : names) {
      bool any = std::any_of(parts.begin(), parts.end(),
                             [&](const std::string& p) { return text::contains(name, p); });
      if (any) ++hits;
    }
    if (hits > 0) scored.emplace_back(&market, hits);
  }
  // Ties keep the list's order (biggest market first).
  std::stable_sort(scored.begin(), scored.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  std::vector<Market> out;
  for (size_t i = 0; i < scored.size() && i < limit; ++i) out.push_back(*scored[i].first);
  return out;
}

std::string toJson(const std::vector<Market>& markets) {
  json::Value list = json::Value::array();
  for (const Market& m : markets) {
    json::Value v = json::Value::object();
    v.set("name", m.name);
    if (m.templateName) v.set("template", *m.templateName);
    list.push(v);
  }
  return json::write(list);
}

std::optional<std::vector<Market>> fromJson(std::string_view textIn) {
  auto parsed = json::parse(textIn);
  if (!parsed || !parsed->isArray()) return std::nullopt;
  std::vector<Market> out;
  for (const json::Value& v : parsed->items()) {
    if (!v["name"].isString()) return std::nullopt;
    Market m;
    m.name = v["name"].asString();
    if (v["template"].isString()) m.templateName = v["template"].asString();
    out.push_back(std::move(m));
  }
  return out;
}

}  // namespace marigold::catalog::markets
