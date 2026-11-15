#include "core/sports/languages.hpp"

#include <map>
#include <string>
#include <utility>

#include "core/catalog/catalog.hpp"
#include "core/sports/brands.hpp"
#include "core/util/rx.hpp"

namespace marigold::sports::languages {

namespace {

bool has(const std::string& words, const char* phrase) { return words.find(phrase) != std::string::npos; }

bool hasAny(const std::string& words, std::initializer_list<const char*> phrases) {
  for (const char* p : phrases) {
    if (has(words, p)) return true;
  }
  return false;
}

// Arabic letters (U+0600-U+06FF: lead bytes D8-DB in UTF-8).
bool arabicScript(std::string_view text) {
  for (size_t i = 0; i + 1 < text.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    unsigned char next = static_cast<unsigned char>(text[i + 1]);
    if (c >= 0xD8 && c <= 0xDB && next >= 0x80 && next <= 0xBF) return true;
  }
  return false;
}

std::string markedWords(const std::string& w) {
  // Spanish before English: "beIN SPORTS en Español" has both "en"s.
  struct Mark {
    const char* lang;
    std::initializer_list<const char*> words;
  };
  static const Mark kMarks[] = {
      {"es", {" es ", " esp ", " espanol ", " spanish ", " castellano ", " deportes ", " latino ", " latam "}},
      {"hi", {" hindi "}},
      {"ta", {" tamil "}},
      {"te", {" telugu "}},
      {"kn", {" kannada "}},
      {"ml", {" malayalam "}},
      {"mr", {" marathi "}},
      {"pa", {" punjabi "}},
      {"bn", {" bangla ", " bengali "}},
      {"ur", {" urdu "}},
      {"ar", {" arabic ", " arabia "}},
      {"fr", {" french ", " francais "}},
      {"de", {" german ", " deutsch "}},
      {"it", {" italian ", " italiano "}},
      {"pt", {" portuguese ", " portugues "}},
      {"tr", {" turkish ", " turkce "}},
      {"en", {" en ", " eng ", " english "}},
  };
  for (const Mark& m : kMarks) {
    if (hasAny(w, m.words)) return m.lang;
  }
  return std::string();
}

// "AR" is a country code for Argentina, and the group code channel lists
// give their Arabic channels: told apart by name.
std::string argentinaOrArabic(const std::string& w) {
  if (hasAny(w, {" bein ", " ssc ", " alkass ", " al kass ", " ad sports ", " abu dhabi ", " dubai ", " sharjah ",
                 " ksa ", " on time ", " arryadia ", " oman ", " bahrain ", " kuwait ", " jordan ", " al jazeera ",
                 " mbc ", " rotana ", " osn ", " nile ", " shahid ", " qatar ", " saudi ", " emirates ", " arab "})) {
    return "ar";
  }
  if (hasAny(w, {" tyc ", " tnt sports ", " espn ", " fox sports ", " dsports ", " directv ", " telefe ",
                 " tv publica ", " deportv ", " el trece ", " canal 13 ", " america tv ", " c 5 n ", " cronica ",
                 " garra ", " argentina ", " argentino "})) {
    return "es";
  }
  return std::string();
}

// Build 160: a language named last in a feed's title, after a word that
// says so or a mark ("em Portugu\xC3\xAAs", "en Espa\xC3\xB1ol", "in English",
// ": English", "- Spanish", "(French)"): its code, else "". Never a language
// word on its own at the end ("Harbor vs Valley Portuguesa"; "French Open"
// isn't last). Plain string work: it runs for every provider event.
std::string trailing(std::string_view title) {
  static const std::pair<const char*, const char*> kWords[] = {
      {" english ", "en"},    {" ingles ", "en"},     {" anglais ", "en"},    {" spanish ", "es"},
      {" espanol ", "es"},    {" castellano ", "es"}, {" portuguese ", "pt"}, {" portugues ", "pt"},
      {" french ", "fr"},     {" francais ", "fr"},   {" frances ", "fr"},    {" german ", "de"},
      {" deutsch ", "de"},    {" aleman ", "de"},     {" italian ", "it"},    {" italiano ", "it"},
      {" arabic ", "ar"},     {" arabe ", "ar"},      {" dutch ", "nl"},      {" polish ", "pl"},
      {" turkish ", "tr"},    {" turkce ", "tr"},     {" russian ", "ru"},    {" hindi ", "hi"}};
  size_t last = title.find_last_not_of(" )]\t");
  if (last == std::string_view::npos || title.size() < 4) return std::string();
  std::string w = brands::words(title);
  const char* code = nullptr;
  size_t wordAt = 0;
  for (const auto& [word, lang] : kWords) {
    size_t n = std::char_traits<char>::length(word);
    if (w.size() >= n && w.compare(w.size() - n, n, word) == 0) {
      code = lang;
      wordAt = w.size() - n;
      break;
    }
  }
  if (!code) return std::string();
  // A word before it that says it's the language ("em", "en", "in"...).
  static const char* const kBefore[] = {" em ", " en ", " in ", " na ", " auf ", " lingua ", " idioma ", " audio "};
  for (const char* before : kBefore) {
    size_t n = std::char_traits<char>::length(before);
    if (wordAt + 1 >= n && w.compare(wordAt + 1 - n, n, before) == 0) return code;
  }
  // Else a mark just before it in the title: ":", "-", "|", "(", "[".
  size_t start = last;
  while (start > 0 && title[start - 1] != ' ' && title[start - 1] != ':' && title[start - 1] != '-' &&
         title[start - 1] != '|' && title[start - 1] != '(' && title[start - 1] != '[') {
    --start;
  }
  if (start == 0) return std::string();
  size_t mark = title.find_last_not_of(' ', start - 1);
  if (mark == std::string_view::npos) return std::string();
  char c = title[mark];
  return c == ':' || c == '-' || c == '|' || c == '(' || c == '[' ? std::string(code) : std::string();
}

// Build 160: a name's own country prefix ("FR: ", "DE| ", "|PT| "): its
// market's language, for a channel or feed whose group names no country.
std::string prefixed(std::string_view name) {
  size_t i = 0;
  while (i < name.size() && name[i] == ' ') ++i;
  if (i < name.size() && name[i] == '|') ++i;
  if (i + 2 > name.size()) return std::string();
  char a = name[i], b = name[i + 1];
  if (a < 'A' || a > 'Z' || b < 'A' || b > 'Z') return std::string();
  size_t j = i + 2;
  while (j < name.size() && name[j] == ' ') ++j;
  if (j >= name.size() || (name[j] != ':' && name[j] != '|')) return std::string();
  std::string code{a, b};
  // Countries lists put first, not competitions ("CL: " the Champions
  // League, "NL: " a National League): only these.
  static const std::set<std::string> kCountries = {"US", "UK", "IE", "AU", "NZ", "EN", "FR", "DE", "AT", "ES", "MX",
                                                   "AR", "IT", "PT", "BR", "TR", "GR", "SE", "NO", "DK", "FI", "PL",
                                                   "RO", "HU", "CZ", "HR", "RS", "BG", "RU", "UA"};
  if (!kCountries.count(code)) return std::string();
  if (code == "EN") return "en";
  if (code == "AR") return argentinaOrArabic(brands::words(name));
  return catalog::marketLanguage(code);
}

}  // namespace

std::string marked(std::string_view name) {
  if (arabicScript(name)) return "ar";
  return markedWords(brands::words(name));
}

std::string ofChannel(std::string_view name, const std::optional<std::string>& market,
                      catalog::LineupCategory category) {
  if (arabicScript(name)) return "ar";
  std::string w = brands::words(name);
  std::string lang = markedWords(w);
  if (!lang.empty()) return lang;
  if (category == catalog::LineupCategory::spanish) return "es";
  if (market) {
    if (*market == "AR") {
      lang = argentinaOrArabic(w);
    } else if (*market == "CA") {
      // Build 160: Quebec's ("|CA| QUEBEC SPORT", "QC") are French.
      lang = hasAny(w, {" rds ", " tva ", " tele quebec ", " ici ", " radio canada ", " quebec ", " qc "}) ? "fr"
                                                                                                         : "en";
    } else {
      lang = catalog::marketLanguage(*market);
    }
    if (!lang.empty()) return lang;
  } else if (lang = prefixed(name); !lang.empty()) {
    // Build 160: no country for its group: its name's own ("FR: ...").
    return lang;
  }
  if (const brands::Brand* brand = brands::find(name)) return brand->lang;
  return std::string();
}

std::string ofFeed(std::string_view title, std::string_view family, const std::optional<std::string>& market) {
  static const char* const kSpanishWords =
      "\\b(spanish|espanol|espa\xC3\xB1ol|deportes|telemundo|univision|tudn|unimas)\\b";
  if (market == std::string("US-ES") || rx::test(kSpanishWords, title, true)) return "es";
  if (arabicScript(title)) return "ar";
  // A title's words name teams and places ("French Open", "Saudi Arabia vs
  // Japan", "ENG v AUS"): only a tag in brackets says the language.
  if (title.find_first_of("([") != std::string_view::npos) {
    if (auto tag = rx::first("[\\(\\[]\\s*([A-Za-z]{2,10})\\s*[\\)\\]]", title)) {
      std::string lang = markedWords(brands::words(tag->group(1)));
      if (!lang.empty()) return lang;
    }
  }
  // Build 160: a language named last ("em Portugu\xC3\xAAs", ": English").
  if (std::string lang = trailing(title); !lang.empty()) return lang;
  // Build 160: no country for the group: the title's own ("FR: ...").
  if (!market) return prefixed(title);
  if (*market == "AR") return argentinaOrArabic(brands::words(title) + brands::words(family));
  // "CA" (English and French) stays not known.
  return catalog::marketLanguage(*market);
}

const std::set<std::string>& fallback() {
  static const std::set<std::string> kEnglish = {"en"};
  return kEnglish;
}

std::set<std::string> ofViewer(const std::set<std::string>& countries) {
  static const std::map<std::string, std::set<std::string>> kSeveral = {
      {"IN", {"hi", "en"}}, {"PK", {"ur", "en"}},       {"CA", {"en", "fr"}},  {"AR", {"es", "ar"}},
      {"BE", {"nl", "fr"}}, {"CH", {"de", "fr", "it"}}, {"PH", {"tl", "en"}},  {"SG", {"en", "zh"}},
      {"EXYU", {"sr", "hr", "bs"}}, {"AFR", {"en", "fr"}}};
  std::set<std::string> out;
  for (const std::string& c : countries) {
    auto several = kSeveral.find(c);
    if (several != kSeveral.end()) {
      out.insert(several->second.begin(), several->second.end());
    } else if (std::string lang = catalog::marketLanguage(c); !lang.empty()) {
      out.insert(std::move(lang));
    }
  }
  return out.empty() ? fallback() : out;
}

std::string name(std::string_view code) {
  static const std::map<std::string, std::string, std::less<>> kNames = {
      {"en", "English"},   {"es", "Spanish"},    {"ar", "Arabic"},     {"fr", "French"},    {"de", "German"},
      {"it", "Italian"},   {"pt", "Portuguese"}, {"hi", "Hindi"},      {"ur", "Urdu"},      {"ta", "Tamil"},
      {"te", "Telugu"},    {"kn", "Kannada"},    {"bn", "Bengali"},    {"ml", "Malayalam"}, {"mr", "Marathi"},
      {"pa", "Punjabi"},   {"gu", "Gujarati"},   {"zh", "Chinese"},    {"ko", "Korean"},    {"ja", "Japanese"},
      {"pl", "Polish"},    {"ru", "Russian"},    {"vi", "Vietnamese"}, {"fa", "Persian"},   {"tl", "Filipino"},
      {"he", "Hebrew"},    {"el", "Greek"},      {"tr", "Turkish"},    {"nl", "Dutch"},     {"sv", "Swedish"},
      {"no", "Norwegian"}, {"da", "Danish"},     {"fi", "Finnish"},    {"ro", "Romanian"},  {"hu", "Hungarian"},
      {"cs", "Czech"},     {"sk", "Slovak"},     {"hr", "Croatian"},   {"sr", "Serbian"},   {"bs", "Bosnian"},
      {"bg", "Bulgarian"}, {"sq", "Albanian"},   {"uk", "Ukrainian"},  {"th", "Thai"},      {"id", "Indonesian"},
      {"ms", "Malay"},     {"ku", "Kurdish"},    {"sl", "Slovenian"},  {"lt", "Lithuanian"}, {"lv", "Latvian"},
      {"et", "Estonian"},  {"is", "Icelandic"},  {"ka", "Georgian"},   {"hy", "Armenian"},  {"az", "Azerbaijani"},
      {"ne", "Nepali"}};
  auto it = kNames.find(code);
  return it == kNames.end() ? std::string() : it->second;
}

Fit fit(std::string_view lang, const std::set<std::string>& viewer) {
  if (lang.empty()) return Fit::unknown;
  const std::set<std::string>& mine = viewer.empty() ? fallback() : viewer;
  return mine.count(std::string(lang)) ? Fit::yours : Fit::other;
}

bool welcome(const EventSource& source, const std::set<std::string>& viewer) {
  return source.kind == EventSource::Kind::lineup || fit(source.lang, viewer) != Fit::other;
}

std::string_view firstLanguage(const std::set<std::string>& viewer) {
  if (viewer.empty() || viewer.count("en")) return "en";
  return *viewer.begin();
}

Tier tier(std::string_view lang, const std::set<std::string>& viewer) {
  if (lang.empty()) return Tier::unknown;
  if (lang == firstLanguage(viewer)) return Tier::first;
  for (const std::string& mine : viewer) {
    if (mine == lang) return Tier::yours;
  }
  return Tier::other;
}

const EventSource* lead(const std::vector<EventSource>& sources, const std::set<std::string>& viewer) {
  const EventSource* best = nullptr;
  int bestKey = 0;
  for (const EventSource& s : sources) {
    if (s.kind == EventSource::Kind::team) continue;
    int key = static_cast<int>(tier(s.lang, viewer)) * 4 + static_cast<int>(s.kind);
    if (!best || key < bestKey) {
      best = &s;
      bestKey = key;
    }
  }
  if (best) return best;
  return sources.empty() ? nullptr : &sources.front();
}

}  // namespace marigold::sports::languages
