#include "core/sports/titles.hpp"

#include <cctype>
#include <cstdlib>
#include <set>
#include <vector>

#include "core/catalog/text.hpp"
#include "core/sports/text.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::sports::titles {
namespace {

// A word a title starts with ("Boxing: ...") or names a discipline with.
struct Word {
  const char* key;    // lower case
  const char* chip;   // nullptr: the league's chip
  const char* icon;   // nullptr: the league's icon
  bool sport = true;  // a sport ("Golf"); else a series kept as the line under ("MotoGP")
  bool people = false;
};

const std::vector<Word>& words() {
  static const std::vector<Word> kWords = {
      // Sports the league already says: no chip or icon of their own.
      {"football", nullptr, nullptr}, {"american football", nullptr, nullptr},
      {"basketball", nullptr, nullptr}, {"baseball", nullptr, nullptr},
      {"softball", nullptr, nullptr}, {"hockey", nullptr, nullptr},
      {"ice hockey", nullptr, nullptr}, {"field hockey", nullptr, nullptr},
      {"soccer", nullptr, nullptr}, {"volleyball", nullptr, nullptr},
      {"beach volleyball", nullptr, nullptr}, {"lacrosse", nullptr, nullptr},
      {"rugby", nullptr, nullptr}, {"rugby union", nullptr, nullptr},
      {"rugby league", nullptr, nullptr}, {"cricket", nullptr, nullptr},
      {"golf", nullptr, nullptr}, {"tennis", nullptr, nullptr},
      {"wrestling", nullptr, "wrestling"}, {"motorsport", nullptr, "motorsport"},
      {"motorsports", nullptr, "motorsport"},
      // Sports between people.
      {"boxing", "BOXING", "fighting", true, true}, {"mma", "MMA", "fighting", true, true},
      {"kickboxing", "KICKBOX", "fighting", true, true}, {"darts", "DARTS", "darts", true, true},
      {"snooker", "SNOOKER", nullptr, true, true}, {"pool", "POOL", nullptr, true, true},
      {"table tennis", "TT", nullptr, true, true}, {"badminton", "BADMINTON", nullptr, true, true},
      {"squash", "SQUASH", nullptr, true, true},
      // Disciplines with an icon.
      {"athletics", nullptr, "athletics"}, {"track and field", nullptr, "athletics"},
      {"marathon", nullptr, "athletics"}, {"cycling", "CYCLING", "cycling"},
      {"horse racing", "RACING", "horseRacing"},
      // Series: their name stays as the line under.
      {"motogp", "MOTOGP", "motorsport", false}, {"f1", "F1", "motorsport", false},
      {"formula 1", "F1", "motorsport", false}, {"nascar", "NASCAR", "motorsport", false},
      {"indycar", "INDYCAR", "motorsport", false}, {"wrc", "RALLY", "motorsport", false},
      {"ufc", "UFC", "fighting", false, true}, {"pfl", "PFL", "fighting", false, true},
      {"bkfc", "BKFC", "fighting", false, true}, {"wwe", "WWE", "wrestling", false},
      {"aew", "AEW", "wrestling", false}, {"olympics", "OLYMPICS", "multi", false},
      {"paralympics", "PARALYMPICS", "multi", false}, {"x games", "X GAMES", "multi", false},
      {"tour de france", "CYCLING", "cycling", false},
  };
  return kWords;
}

const Word* wordFor(std::string_view s) {
  std::string key = str::lower(str::trimUnicode(s));
  for (const Word& w : words()) {
    if (key == w.key) return &w;
  }
  return nullptr;
}

// An icon for a title the catalog couldn't place ("Other"), from words in it.
const char* iconIn(const std::string& lowerTitle) {
  struct Hint {
    const char* icon;
    std::vector<const char*> words;
  };
  static const std::vector<Hint> kHints = {
      {"athletics", {"athletics", "track and field", "marathon", "diamond league"}},
      {"cycling", {"cycling", "tour de france", "giro d'italia", "vuelta"}},
      {"horseRacing", {"horse racing", "kentucky derby", "breeders' cup", "royal ascot"}},
      {"darts", {"darts", "pdc"}},
      {"wrestling", {"wwe", "aew", "wrestling"}},
      {"fighting", {"boxing", "mma"}},
      {"multi", {"olympics", "paralympics", "x games", "commonwealth games"}},
  };
  for (const Hint& h : kHints) {
    for (const char* w : h.words) {
      if (catalog::text::hasWord(lowerTitle, w)) return h.icon;
    }
  }
  return nullptr;
}

const char* const kStage =
    "\\b(?:(?:round|night|day|draw|stage|game|leg|match|matchday|week|heat|race)\\s+\\d+"
    "|\\d+(?:st|nd|rd|th)\\s+(?:test|odi|t20i?|match|round|stage|leg)"
    "|(?:first|second|third|final|opening|championship)\\s+round"
    "|(?:quarter|semi)[- ]?finals?|grand\\s+final|finals?(?!\\s+four)|play-?offs?"
    "|sprint(?:\\s+(?:race|qualifying|shootout))?|qualifying|practice(?:\\s+\\d)?|fp[1-3]|heats"
    "|group\\s+[a-h]|race)\\b";

std::string trimmed(std::string_view s) {
  std::string out(str::trimUnicode(s));
  // A stage taken out of the middle leaves its separators behind.
  while (!out.empty() && (out.back() == ',' || out.back() == ':' || out.back() == '-')) out.pop_back();
  while (!out.empty() && (out.front() == ',' || out.front() == ':' || out.front() == '-')) out.erase(0, 1);
  return std::string(str::trimUnicode(out));
}

// The provider's stand-ins: "`" for an apostrophe, "_" for a hyphen
// ("UMass_Boston", "Tip_Off") or a comma before a state ("Western Colo._
// CO"); feed details in brackets ("[Main Broadcast]", "(UHD)") and a
// leading serial number ("2618 Mexico City GP F1") aren't the event's.
std::string cleanTitle(std::string s) {
  s = str::replaceAll(s, "`", "'");
  s = rx::replace(s, "_\\s+(?=[A-Z]{2}\\b)", ", ");
  s = rx::replace(s, "(?<=[\\p{L}\\p{N}.])_(?=[\\p{L}\\p{N}])", "-");
  s = rx::replace(s,
                  "\\s*[\\(\\[]\\s*(?:main broadcast|uhd|fhd|hd|sd|4k|1080p|720p|english|eng|spanish|esp|"
                  "espa\xc3\xb1ol|en espa\xc3\xb1ol|multi ?cam|alt(?:ernate)? feed|backup)\\s*[\\)\\]]",
                  "", true);
  if (auto m = rx::first("^(\\d{4,})\\s+(?=\\p{L})", s)) {
    int n = std::atoi(std::string(m->group(1)).c_str());
    if (n < 1900 || n > 2100) s = s.substr(m->end);
  }
  return std::string(str::trimUnicode(s));
}

// Words kept in capitals when a shouted (or all lower case) title is given
// ordinary capitals.
bool keepsCapitals(const std::string& upperWord) {
  static const std::set<std::string> kKeep = {
      "FC", "AFC", "SC", "CF", "CD", "AC", "KF", "KS", "FK", "SK", "NK", "BK", "IF", "IK", "SV", "VFB", "VFL", "TSG",
      "PSV", "AZ", "MMA", "UFC", "PFL", "BKFC", "WWE", "AEW", "NBA", "NFL", "NHL", "MLB", "MLS", "WNBA", "NCAA",
      "NCAAF", "NCAAB", "F1", "GP", "USA", "US", "UK", "USSR", "II", "III", "IV", "VI", "VII", "VIII", "IX", "XI",
      "TV", "ESPN", "CBS", "NBC", "ABC", "FOX", "BYU", "UCF", "USC", "UCLA", "LSU", "TCU", "SMU", "UNLV", "UTEP",
      "MIT", "WPI", "CWRU", "RIT", "UAB", "UNC", "NC", "DC", "OT", "PPV", "WTA", "ATP", "ITF", "LPGA", "PGA", "LIV",
      "PSG", "CA", "CO", "NY", "TX", "FL", "OH", "PA", "MA", "VA", "MI", "MN", "WI", "IL", "IN", "GA", "SC", "AL"};
  if (kKeep.count(upperWord)) return true;
  // High-school associations ("KSHSAA", "MHSA", "WIAA", "UIL") and classes
  // ("4A", "6C").
  if (upperWord == "UIL" || upperWord == "CIF" || upperWord == "NFHS" ||
      rx::test("^[A-Z]{1,5}(HSAA|HSA|HSL|IAA)$", upperWord))
    return true;
  if (rx::test("^\\d{1,2}[A-Z]{1,2}$", upperWord) && !rx::test("^\\d+(ST|ND|RD|TH)$", upperWord)) return true;
  // "U21", "67'S", "3RD": numbers aside.
  if (rx::test("\\d", upperWord)) return rx::test("^[A-Z]+\\d+$", upperWord);
  // No vowels: an abbreviation ("KSV", "BSC").
  return upperWord.size() >= 2 && upperWord.size() <= 4 && !rx::test("[AEIOUY]", upperWord);
}

// "FENCIBLES UNITED VS. EASTERN SUBURBS" -> "Fencibles United vs. Eastern
// Suburbs", "detroit, mi" -> "Detroit, MI". Titles with both cases are left
// as they are.
std::string readable(const std::string& s) {
  bool upper = rx::test("\\p{Lu}", s), lower = rx::test("\\p{Ll}", s);
  if (upper && lower) return s;
  if (!upper && !lower) return s;
  static const std::set<std::string> kSmall = {"vs", "vs.", "v", "v.", "at", "of", "the", "and", "de", "la", "del",
                                               "y", "e", "di", "da", "on", "in", "for", "a", "an", "to"};
  std::string out;
  bool first = true;
  for (const rx::Match& m : rx::all("[^\\s]+|\\s+", s)) {
    std::string w(m.group(0));
    if (std::isspace(static_cast<unsigned char>(w[0]))) {
      out += w;
      continue;
    }
    std::string up = str::upper(w), low = str::lower(w);
    std::string core = rx::replace(up, "^[^\\p{L}\\p{N}]+|[^\\p{L}\\p{N}']+$", "");
    if (!first && kSmall.count(low)) {
      out += low;
    } else if (keepsCapitals(core)) {
      out += up;
    } else {
      // Each part of "district-17" or "o'neil" capitalised.
      std::string word;
      bool start = true;
      for (size_t i = 0; i < low.size();) {
        size_t n = 1;
        unsigned char c = static_cast<unsigned char>(low[i]);
        if (c >= 0xF0) n = 4;
        else if (c >= 0xE0) n = 3;
        else if (c >= 0xC0) n = 2;
        std::string ch = low.substr(i, n);
        word += start ? str::upper(ch) : ch;
        start = ch == "-" || ch == "/" || ch == "(" || ch == "." ;
        i += n;
      }
      out += word;
    }
    first = false;
  }
  return out;
}

// "Saddleback College vs Orange Coast College _ Women`s (Saddleback vs
// Orange Coast)": the piece without its asides (brackets, closed or cut
// off, and what follows " _ ").
std::string withoutAsides(const std::string& piece) {
  std::string out = rx::replace(piece, "\\s*[\\(\\[][^\\)\\]]*[\\)\\]]?", "");
  size_t cut = out.find(" _ ");
  if (cut != std::string::npos) out.resize(cut);
  return std::string(str::trimUnicode(out));
}

// "Live" says what every listing here is.
constexpr const char* kLiveWords = "^(?:live|live now|live coverage|en vivo|ao vivo|in diretta|en direct)$";

// "A vs B" in one piece of a title.
std::optional<std::tuple<std::string, std::string, bool>> matchup(const std::string& whole, bool places) {
  const std::pair<const char*, bool> separators[] = {
      {" vs. ", false}, {" vs ", false}, {" v. ", false}, {" v ", false}, {" at ", true}, {" @ ", true}};
  // As named, else without its asides (which often repeat the matchup).
  std::string bare = withoutAsides(whole);
  for (int pass = 0; pass < 2; ++pass) {
    const std::string& piece = pass == 0 ? whole : bare;
    if (pass == 1 && bare == whole) break;
    std::string lowerPiece = str::lower(piece);
    for (const auto& [separator, homeSecond] : separators) {
      // Any case ("VS.", "Vs", "AT"), once.
      std::string sep = str::lower(separator);
      // A race or a round "at" a track or course: a place, not a side.
      if (places && (sep == " at " || sep == " @ ")) continue;
      size_t at = lowerPiece.find(sep);
      if (at == std::string::npos || lowerPiece.find(sep, at + 1) != std::string::npos) continue;
      std::string a = trimmed(piece.substr(0, at)), b = trimmed(piece.substr(at + sep.size()));
      size_t na = text::count(a), nb = text::count(b);
      if (na < 2 || nb < 2 || na > 40 || nb > 40) continue;
      // "Round 2 at Augusta" is a place, not a side.
      if (stageIn(a) || stageIn(b)) continue;
      return std::make_tuple(a, b, homeSecond);
    }
  }
  return std::nullopt;
}

// "A. Harbor": a person named by an initial.
bool initialed(const std::string& name) { return rx::test("^\\p{Lu}\\.\\s*\\p{L}", name); }

}  // namespace

std::optional<std::string> stageIn(std::string_view s) {
  auto m = rx::first(kStage, s, true);
  if (!m) return std::nullopt;
  return std::string(m->group(0));
}

Title read(std::string_view title, std::string_view league) {
  Title out;
  std::string full = cleanTitle(std::string(str::trimUnicode(title)));
  std::string lowerTitle = str::lower(full);

  // "Rugby: ..." / "MotoGP: ..." / "Pro Bull Riding: ...": a sport or a series first.
  std::string prefix, body = full;
  const Word* lead = nullptr;
  std::vector<std::string> pieces = text::components(full, ": ");
  if (pieces.size() >= 2) {
    std::string head = trimmed(pieces[0]);
    lead = wordFor(head);
    if (rx::test(kLiveWords, head, true)) {
      // "live: Harbor vs Valley": no prefix to show.
      lead = nullptr;
      body = trimmed(full.substr(full.find(": ") + 2));
      pieces.erase(pieces.begin());
    } else if (lead || text::count(head) <= 16) {
      prefix = head;
      body = trimmed(full.substr(full.find(": ") + 2));
    }
  }

  // The rest in pieces: "Ireland vs France - Six Nations".
  // A long head is the event's own name ("The ... Show: Preview"): the
  // pieces are its name and what follows.
  std::vector<std::string> chunks = prefix.empty() && pieces.size() >= 2 ? pieces : std::vector<std::string>{body};
  std::vector<std::string> segments;
  for (const std::string& chunk : chunks) {
    for (const std::string& a : text::components(chunk, " - ")) {
      for (const std::string& b : text::components(a, " \xE2\x80\x93 ")) {  // en dash
        for (const std::string& c : text::components(b, " | ")) {
          // " _ ": the provider's separator ("... _ Women's", "... _ Oct 29th").
          for (const std::string& d : text::components(c, " _ ")) {
            if (std::string s = trimmed(d); !s.empty()) segments.push_back(s);
          }
        }
      }
    }
  }
  if (segments.empty()) segments.push_back(body);

  std::string lowerLeagueRow = str::lower(league);
  bool placesAt = lowerLeagueRow == "motorsport" || lowerLeagueRow == "golf" || lowerLeagueRow == "racing";
  // A matchup in any piece; the first piece's stage only at its end, so a
  // "Race of Champions" keeps its name.
  size_t sideAt = segments.size();
  for (size_t i = 0; i < segments.size() && sideAt == segments.size(); ++i) {
    if (auto m = matchup(segments[i], placesAt)) {
      out.sides = std::make_pair(std::get<0>(*m), std::get<1>(*m));
      out.homeSecond = std::get<2>(*m);
      sideAt = i;
    }
  }
  if (out.sides) {
    // "(Home)" says which side is at home; "(Mat 1)" is where (a tag). Other
    // brackets on a side go.
    auto side = [&](std::string& name, bool second) {
      for (int guard = 0; guard < 4; ++guard) {
        auto m = rx::first("\\s*[\\(\\[]([^\\)\\]]*)[\\)\\]]?\\s*$", name);
        if (!m || m->begin == 0) break;
        std::string inner = str::lower(str::trimUnicode(m->group(1)));
        if (inner == "home") {
          out.homeSecond = second;
        } else if (auto mat = rx::first("^mat\\W*0*(\\d+)$", inner)) {
          if (!out.stage) out.stage = "Mat " + std::string(mat->group(1));
        }
        name = trimmed(name.substr(0, m->begin));
      }
    };
    side(out.sides->first, false);
    side(out.sides->second, true);
    // The same name twice ("Western Colo., CO at Western Colo., CO (Mat 1)"):
    // one school's event, not a matchup.
    if (str::lower(out.sides->first) == str::lower(out.sides->second)) {
      segments[sideAt] = out.sides->first;
      out.sides.reset();
      out.homeSecond = false;
      sideAt = segments.size();
    }
  }
  // Pieces that only repeat the sport ("Field Hockey") or give a date ("Oct
  // 29th") say nothing on a tile.
  std::string lowerLeagueName = str::lower(league);
  auto filler = [&](const std::string& piece) {
    std::string p = str::lower(piece);
    if (p == lowerLeagueName || (wordFor(p) && wordFor(p)->sport)) return true;
    return rx::test("^(?:(?:mon|tue|wed|thu|fri|sat|sun)[a-z]*,?\\s+)?(?:jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec)[a-z]*\\.?\\s+\\d{1,2}(?:st|nd|rd|th)?(?:,?\\s+\\d{4})?$", p);
  };
  std::vector<std::string> rest;
  for (size_t i = 0; i < segments.size(); ++i) {
    if (i == sideAt) continue;
    std::string s = segments[i];
    if (i > 0 && filler(s) && !rx::test("\\bwomen\\W?s\\b|\\bmen\\W?s\\b", str::lower(s))) continue;
    // "Live" (first or last) says what every listing here is.
    if (rx::test(kLiveWords, s, true)) continue;
    // Men's / Women's become the tag, below.
    if (out.sides && rx::test("^(?:wo)?men'?s$", str::lower(s))) continue;
    if (!out.stage) {
      auto m = rx::first(kStage, s, true);
      if (m && (i > 0 || out.sides || m->end == s.size())) {
        out.stage = std::string(m->group(0));
        s = trimmed(s.substr(0, m->begin) + " " + s.substr(m->end));
      }
    }
    if (!s.empty()) rest.push_back(s);
  }

  // The people in a matchup.
  std::string lowerLeague = str::lower(league);
  if (out.sides) {
    out.players = (lead && lead->people) || lowerLeague == "darts" || lowerLeague == "fighting" ||
                  (initialed(out.sides->first) && initialed(out.sides->second));
  }

  // Chip and icon: the prefix's, else (for a title the catalog couldn't
  // place) words in it.
  if (lead && lead->chip) out.chip = lead->chip;
  if (lead && lead->icon) out.icon = lead->icon;

  if (out.sides) {
    out.name = out.sides->first + (out.homeSecond ? " at " : " vs ") + out.sides->second;
    if (text::count(segments[sideAt]) <= text::count(out.name) + 2) out.name = segments[sideAt];
    // "_ Women`s" / "(Women's)": the side of the sport it is.
    if (!out.stage) {
      // "(EXH)": a preseason exhibition (no schedule lists it).
      if (rx::test("\\b(exh|exhibition)\\b", lowerTitle)) out.stage = "Exhibition";
      else if (rx::test("\\bwomen\\W?s\\b", lowerTitle)) out.stage = "Women's";
      else if (rx::test("\\bmen\\W?s\\b", lowerTitle)) out.stage = "Men's";
    }
    // A short piece beside the matchup tags it ("Six Nations", "Heavyweight").
    if (!out.stage && !rest.empty() && text::count(rest.front()) <= 22) {
      out.stage = rest.front();
      rest.erase(rest.begin());
    }
    if (!rest.empty()) out.subtitle = rest.front();
  } else {
    // "Athletics - Men's 100m": the discipline goes under the event.
    const Word* discipline = rest.size() >= 2 ? wordFor(rest.front()) : nullptr;
    if (discipline && discipline->sport) {
      out.subtitle = rest.front();
      if (discipline->icon) out.icon = discipline->icon;
      rest.erase(rest.begin());
    }
    out.name = rest.empty() ? (out.stage ? *out.stage : body) : rest.front();
    if (rest.empty() && out.stage) out.stage.reset();
    if (!out.subtitle && rest.size() >= 2) {
      std::vector<std::string> under(rest.begin() + 1, rest.end());
      out.subtitle = str::join(under, " \xC2\xB7 ");
    }
  }
  // Else a series' name ("MotoGP") goes under; a plain sport's doesn't.
  if (!out.subtitle && !prefix.empty() && !(lead && lead->sport)) {
    out.subtitle = str::upper(text::prefix(prefix, 1)) + text::dropPrefix(prefix, 1);
  }
  if (out.icon.empty() && (lowerLeague == "other" || lowerLeague.empty() || lowerLeague == "fighting")) {
    if (const char* icon = iconIn(lowerTitle)) out.icon = icon;
  }
  // "Falcon Open (MAT_04)": the mat is the tag.
  if (auto mat = rx::first("\\s*[\\(\\[]\\s*mat\\W*0*(\\d+)\\s*[\\)\\]]\\s*$", out.name, true)) {
    if (!out.stage) out.stage = "Mat " + std::string(mat->group(1));
    out.name = trimmed(out.name.substr(0, mat->begin));
  }
  // Shouted or all lower case: ordinary capitals.
  out.name = readable(out.name);
  if (out.sides) {
    out.sides->first = readable(out.sides->first);
    out.sides->second = readable(out.sides->second);
  }
  if (out.subtitle) out.subtitle = readable(*out.subtitle);
  if (out.stage) out.stage = readable(*out.stage);
  return out;
}

std::string inEnglish(std::string_view title) {
  // Danish, Norwegian, Swedish, German, Dutch, Italian, Portuguese, Spanish
  // and French round names, longest first.
  static const std::pair<const char*, const char*> kWords[] = {
      {"\\b(?:kvartfinalerna|kvartfinaler|kvartsfinaler|quarti di finale|quartas de final|cuartos de final|"
       "kwartfinales|quarts de finale)\\b",
       "Quarterfinals"},
      {"\\b(?:kvartfinalen|kvartfinale|kvartsfinal|quarto di finale|viertelfinale|viertelfinal|kwartfinale|"
       "quart de finale)\\b",
       "Quarterfinal"},
      {"\\b(?:semifinalerna|semifinaler|semifinali|semifinais|semifinales|halve finales|demi-finales)\\b",
       "Semifinals"},
      {"\\b(?:semifinalen|semifinale|halbfinale|halbfinal|halve finale|demi-finale)\\b", "Semifinal"},
      {"\\b(?:finalerna|finaler|finali|finais|finales)\\b", "Finals"},
      {"\\b(?:finalen|finale)\\b", "Final"},
      {"\\b(?:dag|giorno|dia|d\xc3\xad" "a|tag|jour)\\s+(\\d{1,2})\\b", "Day $1"},
      {"\\b(?:giornata|jornada|spieltag|journ\xc3\xa9" "e|speelronde|rodada)\\s+(\\d{1,2})\\b", "Matchday $1"},
      {"\\b(?:runde|omgang|omg\\.|ronda|rundi)\\s+(\\d{1,2})\\b", "Round $1"},
      {"\\b(?:kvalifisering|kvalifikation|kvalificering|qualifiche|qualifikation|qualifica\xc3\xa7\xc3\xa3o|"
       "clasificaci\xc3\xb3n)\\b",
       "Qualifying"},
      {"\\b(?:herrer|herrar|herren|masculino|maschile|hommes)\\b", "Men's"},
      {"\\b(?:damer|damen|feminino|femminile|femmes|femenino)\\b", "Women's"},
      {"\\b(\\w+)-touren\\b", "$1 Tour"},
      {"\\bjud\xc3\xb4\\b", "Judo"},
  };
  std::string out(title);
  for (const auto& [pattern, english] : kWords) out = rx::replace(out, pattern, english, true);
  // "Dag 2 Dag 2" (the round named twice) once.
  out = rx::replace(out, "\\b((?:Day|Matchday|Round) \\d{1,2})(?:\\s+\\1\\b)+", "$1");
  return out;
}

}  // namespace marigold::sports::titles
