#include "core/sports/languages.hpp"

#include <map>

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
      lang = hasAny(w, {" rds ", " tva ", " tele quebec ", " ici ", " radio canada "}) ? "fr" : "en";
    } else {
      lang = catalog::marketLanguage(*market);
    }
    if (!lang.empty()) return lang;
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
  if (!market) return std::string();
  if (*market == "AR") return argentinaOrArabic(brands::words(title) + brands::words(family));
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

}  // namespace marigold::sports::languages
