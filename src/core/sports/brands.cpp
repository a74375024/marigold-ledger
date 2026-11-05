#include "core/sports/brands.hpp"

#include <cstdint>
#include <unordered_map>

namespace marigold::sports::brands {

const std::vector<Brand>& all() {
  // "#" stands for a number (" premiere # ": "Premiere 2").
  static const std::vector<Brand> kBrands = {
      // United Kingdom and Ireland.
      {" sky sports main event ", "multi", "en"},
      {" sky sports premier league ", "soccer", "en"},
      {" sky sports football ", "soccer", "en"},
      {" sky sports cricket ", "cricket", "en"},
      {" sky sports f 1 ", "motorsport", "en"},
      {" sky sports golf ", "golf", "en"},
      {" sky sports tennis ", "tennis", "en"},
      {" sky sports ", "multi", "en", " sky sports news "},
      // 1-4 in the UK; Argentina's and Chile's are in Spanish (their country
      // says). Build 143: also as the provider writes it, "TNT SPORT 1".
      {" tnt sports ", "multi", ""},
      {" tnt sport ", "multi", ""},
      {" premier sports ", "multi", "en"},
      {" laliga tv ", "soccer", ""},
      {" laligatv ", "soccer", ""},
      // Spain ("M+ LaLiga", "M. LaLiga", "M.LIGA CAMPEONES 2").
      {" m plus laliga ", "soccer", "es"},
      {" m laliga ", "soccer", "es"},
      {" movistar laliga ", "soccer", "es"},
      {" m plus liga de campeones ", "soccer", "es"},
      {" m liga de campeones ", "soccer", "es"},
      {" m liga campeones ", "soccer", "es"},
      {" m plus liga ", "soccer", "es"},
      {" movistar liga de campeones ", "soccer", "es"},
      {" movistar deportes ", "multi", "es"},
      {" m plus deportes ", "multi", "es"},
      {" dazn ", "multi", ""},
      // Germany, Austria, Italy, New Zealand.
      {" sky sport nz ", "multi", "en"},
      {" sky sport bundesliga ", "soccer", "de"},
      {" sky sport calcio ", "soccer", "it"},
      {" sky calcio ", "soccer", "it"},
      {" sky sport ", "multi", "", " sky sport news | sky sport 24 "},
      {" magenta sport ", "multi", "de"},
      {" servus tv ", "multi", "de"},
      {" servustv ", "multi", "de"},
      {" sportdigital ", "soccer", "de"},
      // Switzerland (German and French feeds: the country says).
      {" blue sport ", "multi", ""},
      {" mysports ", "multi", ""},
      // France (Canal+ Sport is Poland's too).
      {" canal plus foot ", "soccer", "fr"},
      {" canal plus sport ", "multi", ""},
      {" canal plus f 1 ", "motorsport", "fr"},
      {" canal plus top 14 ", "rugby", "fr"},
      {" canal plus live ", "multi", "fr"},
      {" canal plus premier league ", "soccer", "fr"},
      {" canal plus ligue 1 ", "soccer", "fr"},
      {" ligue 1 plus ", "soccer", "fr"},
      {" rmc sport ", "multi", "fr"},
      // Poland.
      {" polsat sport ", "multi", "pl"},
      {" tvp sport ", "multi", "pl"},
      // The Netherlands and the north.
      {" ziggo sport ", "multi", "nl"},
      {" viaplay ", "multi", ""},
      // The Middle East, France, Turkey, Spain, Asia: one brand, many languages.
      {" bein sports ", "multi", ""},
      {" bein sport ", "multi", ""},
      // Portugal and Brazil.
      {" sport tv ", "multi", "pt"},
      {" sporttv ", "multi", "pt"},
      {" premiere clubes ", "soccer", "pt"},
      {" premiere # ", "soccer", "pt"},
      {" sportv ", "multi", "pt"},
      // Mexico and Argentina.
      {" tudn ", "soccer", "es"},
      {" azteca deportes ", "multi", "es"},
      {" tyc sports ", "multi", "es"},
      // Mexico's and Argentina's Fox Sports ("FOX SPORTS 2 MX", "FOX SPORTS
      // PREMIUM ARG"): in Spanish.
      {" fox sports # mx ", "multi", "es"},
      {" fox sports mx ", "multi", "es"},
      {" fox sports # arg ", "multi", "es"},
      {" fox sports premium ", "multi", "es"},
      // Canada (Build 143: also "SPORTSN ET", as the provider writes it).
      {" tsn ", "multi", "en"},
      {" sportsnet ", "multi", "en"},
      {" sportsn et ", "multi", "en"},
      {" rds ", "multi", "fr"},
      {" tva sports ", "multi", "fr"},
      // The Gulf.
      {" ssc ", "multi", "ar", " ssc napoli "},
      {" alkass ", "multi", "ar"},
      {" al kass ", "multi", "ar"},
      {" ad sports ", "multi", "ar"},
      {" abu dhabi sports ", "multi", "ar"},
      // Cricket: India, Pakistan, the US, Australia, South Africa.
      {" willow ", "cricket", "en"},
      {" star sports ", "cricket", ""},
      {" ptv sports ", "cricket", ""},
      {" ten sports ", "cricket", ""},
      {" a sports ", "cricket", ""},
      {" fox cricket ", "cricket", "en"},
      {" supersport ", "multi", "en"},
      // Europe-wide and tennis ("EURO SPORT 1" as the provider writes it).
      {" eurosport ", "multi", ""},
      {" euro sport ", "multi", ""},
      {" eleven sports ", "multi", ""},
      {" eleven ", "multi", ""},
      {" tennis channel ", "tennis", "en"},
      // Build 143, last: ESPN abroad (the Netherlands' "ESPN 2", "SKY NZ
      // ESPN"), Germany's Sport1; their country says the language.
      {" espn ", "multi", ""},
      {" sport 1 ", "multi", ""},
  };
  return kBrands;
}

std::string words(std::string_view name) {
  // Accented Latin letters (U+00C0-U+00FF, "C3 xx" in UTF-8) as plain ones:
  // "Español" is "espanol". "*": not a letter (×, ÷, ß, Æ...).
  static const char kLatin1[] = "aaaaaa*ceeeeiiii*nooooo*ouuuuy**aaaaaa*ceeeeiiii*nooooo*ouuuuy*y";
  std::string out = " ";
  out.reserve(name.size() + 8);
  // 0: a space was last, 1: a letter, 2: a digit.
  int last = 0;
  for (size_t i = 0; i < name.size(); ++i) {
    char ch = name[i];
    unsigned char c = static_cast<unsigned char>(ch);
    int kind = 0;
    if (c == 0xC3 && i + 1 < name.size()) {
      unsigned char next = static_cast<unsigned char>(name[i + 1]);
      if (next >= 0x80 && next <= 0xBF && kLatin1[next - 0x80] != '*') {
        c = static_cast<unsigned char>(kLatin1[next - 0x80]);
        ch = static_cast<char>(c);
        ++i;
      }
    }
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<unsigned char>(c - 'A' + 'a');
      kind = 1;
    } else if (c >= 'a' && c <= 'z') {
      kind = 1;
    } else if (c >= '0' && c <= '9') {
      kind = 2;
    }
    if (kind == 0) {
      if (ch == '+') {
        if (last != 0) out += ' ';
        out += "plus ";
      } else if (last != 0) {
        out += ' ';
      }
      last = 0;
      continue;
    }
    if (last != 0 && last != kind) out += ' ';
    out += static_cast<char>(c);
    last = kind;
  }
  if (last != 0) out += ' ';
  return out;
}

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// `pattern` (with "#" for a number) somewhere in `text`.
bool has(std::string_view text, std::string_view pattern) {
  size_t hash = pattern.find('#');
  if (hash == std::string_view::npos) return text.find(pattern) != std::string_view::npos;
  std::string_view head = pattern.substr(0, hash), tail = pattern.substr(hash + 1);
  for (size_t at = text.find(head); at != std::string_view::npos; at = text.find(head, at + 1)) {
    size_t i = at + head.size();
    size_t digits = i;
    while (digits < text.size() && isDigit(text[digits])) ++digits;
    if (digits > i && text.substr(digits, tail.size()) == tail) return true;
  }
  return false;
}

bool anyOf(std::string_view text, std::string_view list) {
  while (!list.empty()) {
    size_t bar = list.find('|');
    std::string_view one = list.substr(0, bar);
    // Each one padded: " a | b " lists " a " and " b ".
    while (!one.empty() && one.front() == ' ') one.remove_prefix(1);
    while (!one.empty() && one.back() == ' ') one.remove_suffix(1);
    if (!one.empty() && has(text, " " + std::string(one) + " ")) return true;
    if (bar == std::string_view::npos) break;
    list.remove_prefix(bar + 1);
  }
  return false;
}

// The table's rows by their first word ("sky" -> its rows, in order): a
// name is checked only against rows whose first word it has.
const std::unordered_map<std::string_view, std::vector<size_t>>& byFirstWord() {
  static const std::unordered_map<std::string_view, std::vector<size_t>> kIndex = [] {
    std::unordered_map<std::string_view, std::vector<size_t>> index;
    const std::vector<Brand>& table = all();
    for (size_t i = 0; i < table.size(); ++i) {
      std::string_view w = table[i].words;
      w.remove_prefix(1);
      index[w.substr(0, w.find(' '))].push_back(i);
    }
    return index;
  }();
  return kIndex;
}

}  // namespace

const Brand* find(std::string_view name) {
  std::string text = words(name);
  // The rows whose first word is one of the name's, the table's first.
  const auto& index = byFirstWord();
  size_t best = SIZE_MAX;
  const std::vector<Brand>& table = all();
  for (size_t at = 1; at < text.size();) {
    size_t end = text.find(' ', at);
    if (end == std::string::npos) break;
    auto rows = index.find(std::string_view(text).substr(at, end - at));
    if (rows != index.end()) {
      for (size_t i : rows->second) {
        if (i >= best) break;
        if (has(text, table[i].words)) best = i;
      }
    }
    at = end + 1;
  }
  if (best == SIZE_MAX) return nullptr;
  const Brand& brand = table[best];
  if (*brand.unless && anyOf(text, brand.unless)) return nullptr;
  return &brand;
}

}  // namespace marigold::sports::brands
