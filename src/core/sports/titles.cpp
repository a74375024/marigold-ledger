#include "core/sports/titles.hpp"

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

// "A vs B" in one piece of a title.
std::optional<std::tuple<std::string, std::string, bool>> matchup(const std::string& piece) {
  const std::pair<const char*, bool> separators[] = {
      {" vs. ", false}, {" vs ", false}, {" VS ", false}, {" v. ", false}, {" v ", false}, {" at ", true}, {" @ ", true}};
  for (const auto& [separator, homeSecond] : separators) {
    std::vector<std::string> parts = text::components(piece, separator);
    if (parts.size() != 2) continue;
    std::string a = trimmed(parts[0]), b = trimmed(parts[1]);
    size_t na = text::count(a), nb = text::count(b);
    if (na < 2 || nb < 2 || na > 40 || nb > 40) continue;
    // "Round 2 at Augusta" is a place, not a side.
    if (stageIn(a) || stageIn(b)) continue;
    return std::make_tuple(a, b, homeSecond);
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
  std::string full(str::trimUnicode(title));
  std::string lowerTitle = str::lower(full);

  // "Rugby: ..." / "MotoGP: ..." / "Pro Bull Riding: ...": a sport or a series first.
  std::string prefix, body = full;
  const Word* lead = nullptr;
  std::vector<std::string> pieces = text::components(full, ": ");
  if (pieces.size() >= 2) {
    std::string head = trimmed(pieces[0]);
    lead = wordFor(head);
    if (lead || text::count(head) <= 16) {
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
          if (std::string s = trimmed(c); !s.empty()) segments.push_back(s);
        }
      }
    }
  }
  if (segments.empty()) segments.push_back(body);

  // A matchup in any piece; the first piece's stage only at its end, so a
  // "Race of Champions" keeps its name.
  size_t sideAt = segments.size();
  for (size_t i = 0; i < segments.size() && sideAt == segments.size(); ++i) {
    if (auto m = matchup(segments[i])) {
      out.sides = std::make_pair(std::get<0>(*m), std::get<1>(*m));
      out.homeSecond = std::get<2>(*m);
      sideAt = i;
    }
  }
  std::vector<std::string> rest;
  for (size_t i = 0; i < segments.size(); ++i) {
    if (i == sideAt) continue;
    std::string s = segments[i];
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
    out.name = segments[sideAt];
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
  return out;
}

}  // namespace marigold::sports::titles
