#include "core/catalog/directory_sources.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "core/catalog/core_channels.hpp"
#include "core/logos/logos.hpp"
#include "core/util/json.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::catalog::directory::sources {
namespace {

std::string percentEncoded(std::string_view s) {
  static const char* const kHex = "0123456789ABCDEF";
  std::string out;
  for (char ch : s) {
    unsigned char c = static_cast<unsigned char>(ch);
    bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                 c == '.' || c == '_' || c == '~';
    if (plain) {
      out += ch;
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 15];
    }
  }
  return out;
}

std::string trimmed(std::string_view s) { return std::string(str::trimUnicode(s)); }

// Splits on `sep` ("||", "!!" or "|") outside [[links]] and {{templates}}.
std::vector<std::string> splitOutside(std::string_view s, std::string_view sep) {
  std::vector<std::string> out;
  int links = 0, templates = 0;
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s.compare(i, 2, "[[") == 0) {
      ++links;
      ++i;
    } else if (s.compare(i, 2, "]]") == 0) {
      links = std::max(0, links - 1);
      ++i;
    } else if (s.compare(i, 2, "{{") == 0) {
      ++templates;
      ++i;
    } else if (s.compare(i, 2, "}}") == 0) {
      templates = std::max(0, templates - 1);
      ++i;
    } else if (links == 0 && templates == 0 && s.compare(i, sep.size(), sep) == 0) {
      out.emplace_back(s.substr(start, i - start));
      i += sep.size() - 1;
      start = i + 1;
    }
  }
  out.emplace_back(s.substr(start));
  return out;
}

// A cell's content without its attributes ("style=... | [[X]]" -> "[[X]]").
std::string cellContent(std::string_view cell) {
  std::vector<std::string> parts = splitOutside(cell, "|");
  if (parts.size() >= 2 && parts.front().find("[[") == std::string::npos &&
      rx::test("^\\s*([a-z-]+\\s*=|scope)", parts.front(), true)) {
    std::string rest;
    for (size_t i = 1; i < parts.size(); ++i) rest += (i > 1 ? "|" : "") + parts[i];
    return rest;
  }
  return std::string(cell);
}

bool skippedNamespace(std::string_view target) {
  return rx::test("^\\s*:?\\s*(file|image|category|template|wikipedia|help|portal|wikt|w|special|talk|user)\\s*:",
                  target, true);
}

struct Link {
  std::string page;      // empty: a link into another article's section, or this page's
  std::string text;
};

// The first link that names something (not a file, category or this page's section).
std::optional<Link> firstLink(std::string_view s) {
  size_t at = 0;
  while ((at = s.find("[[", at)) != std::string_view::npos) {
    size_t end = s.find("]]", at + 2);
    if (end == std::string_view::npos) return std::nullopt;
    std::string inner(s.substr(at + 2, end - at - 2));
    at = end + 2;
    if (skippedNamespace(inner)) continue;
    std::string target = inner, text;
    if (size_t bar = inner.find('|'); bar != std::string::npos) {
      target = inner.substr(0, bar);
      text = inner.substr(bar + 1);
    }
    std::string page = target, fragment;
    if (size_t hash = target.find('#'); hash != std::string::npos) {
      page = target.substr(0, hash);
      fragment = target.substr(hash + 1);
    }
    page = trimmed(page);
    if (page.empty() && fragment.empty()) continue;
    Link link;
    link.text = trimmed(plainText(text.empty() ? (fragment.empty() ? page : fragment) : text));
    if (link.text.empty()) continue;
    if (fragment.empty()) link.page = page;
    return link;
  }
  return std::nullopt;
}

std::string withoutBrackets(std::string_view s) {
  return trimmed(rx::replace(s, "\\s*\\([^)]*\\)", ""));
}

void addAlias(Entry& e, std::set<std::string>& keys, const std::string& alias) {
  std::string a = withoutBrackets(alias);
  std::string key = exactKey(a);
  if (key.empty() || a.find(':') != std::string::npos || a.size() > 80) return;
  if (key == exactKey(e.name) || !keys.insert(key).second) return;
  e.aliases.push_back(a);
}

// A redirect or Wikidata alias that names something other than a channel:
// a call sign or a translator's ("WLS-DT", "K38CX"), a web address, a
// sentence.
bool notAChannelName(const std::string& alias) {
  return rx::test("^[KW][A-Z0-9]{2,6}(-[A-Z]{1,3}\\d?)?$", alias) ||
         rx::test("\\.(com|net|org)\\b", alias, true) ||
         std::count(alias.begin(), alias.end(), ' ') >= 6;
}

std::string language3to2(std::string_view code) {
  static const std::unordered_map<std::string, std::string> kCodes = {
      {"eng", "en"}, {"spa", "es"}, {"fra", "fr"}, {"por", "pt"}, {"hin", "hi"}, {"urd", "ur"},
      {"ara", "ar"}, {"zho", "zh"}, {"kor", "ko"}, {"jpn", "ja"}, {"ita", "it"}, {"deu", "de"},
      {"pol", "pl"}, {"rus", "ru"}, {"vie", "vi"}, {"fas", "fa"}, {"tgl", "tl"}, {"ben", "bn"},
      {"pan", "pa"}, {"guj", "gu"}, {"tam", "ta"}, {"tel", "te"}, {"mal", "ml"}, {"heb", "he"},
      {"ell", "el"}, {"tur", "tr"}, {"hat", "ht"}, {"yue", "zh"}, {"cmn", "zh"}};
  auto it = kCodes.find(std::string(code));
  return it == kCodes.end() ? std::string(code) : it->second;
}

}  // namespace

const std::vector<ListPage>& listPages() {
  static const std::vector<ListPage> kPages = {
      {"pay", {"List of United States pay television channels"}},
      {"broadcast",
       {"List of United States over-the-air television networks", "List of United States television networks"}},
      {"spanish",
       {"List of Spanish-language television networks in the United States",
        "List of Spanish-language television channels in the United States"}},
      {"regional sports", {"List of United States regional sports networks", "Regional sports network"}},
      {"defunct", {"List of defunct television networks in the United States"}},
  };
  return kPages;
}

std::string plainText(std::string_view wikitext) {
  std::string s(wikitext);
  s = rx::replace(s, "<!--[\\s\\S]*?-->", "");
  s = rx::replace(s, "<ref[^>/]*/>", "", true);
  s = rx::replace(s, "<ref[^>]*>[\\s\\S]*?</ref>", "", true);
  // Templates: the ones that wrap a name keep their last part; the rest go.
  for (int pass = 0; pass < 4 && s.find("{{") != std::string::npos; ++pass) {
    s = rx::replace(s, "\\{\\{\\s*(?:nowrap|nobr|small|lang\\|[a-z-]+|sort\\|[^|{}]*|sortname\\|[^|{}]*)\\|([^{}]*)\\}\\}",
                    "$1", true);
    s = rx::replace(s, "\\{\\{[^{}]*\\}\\}", "");
  }
  s = rx::replace(s, "\\[\\[\\s*(?:file|image|category)\\s*:[^\\[\\]]*(?:\\[\\[[^\\]]*\\]\\][^\\[\\]]*)*\\]\\]", "", true);
  s = rx::replace(s, "\\[\\[(?:[^|\\]]*\\|)?([^\\]]*)\\]\\]", "$1");
  s = rx::replace(s, "\\[https?://[^\\s\\]]+\\s*([^\\]]*)\\]", "$1");
  s = rx::replace(s, "'{2,}", "");
  s = rx::replace(s, "<br\\s*/?>", " ", true);
  s = rx::replace(s, "<[^>]+>", "");
  s = str::replaceAll(str::replaceAll(s, "&nbsp;", " "), "&amp;", "&");
  return trimmed(rx::replace(s, "\\s+", " "));
}

std::vector<Row> parseList(std::string_view wikitext) {
  std::vector<Row> rows;
  std::string section;
  bool skip = false;
  int tables = 0;
  struct Cell {
    std::string text;
    bool header;
  };
  std::vector<Cell> cells;
  // The table so far: rows still covered by a rowspan, by column, and the
  // column its header names as the channel's ("Channel", "Network").
  std::vector<int> spans;
  int nameColumn = -1;
  auto addRow = [&](std::string text, std::string page, const std::string& rowText) {
    text = trimmed(text);
    if (text.empty() || text.size() > 80 || rx::test("^[\\d\\s.,–—-]*$", text)) return;
    if (rx::test("^list of ", page, true)) return;
    rows.push_back({std::move(page), std::move(text), section, rowText});
  };
  auto number = [](const std::string& attrs, const char* name) {
    if (auto m = rx::first(std::string(name) + "\\s*=\\s*\"?\\s*(\\d+)", attrs, true)) {
      return std::max(1, std::atoi(std::string(m->group(1)).c_str()));
    }
    return 1;
  };
  auto finishRow = [&] {
    if (cells.empty()) return;
    bool headerOnly = std::all_of(cells.begin(), cells.end(), [](const Cell& c) { return c.header; });
    std::string rowText;
    for (const Cell& c : cells) rowText += (rowText.empty() ? "" : " | ") + plainText(c.text);
    // Each cell's column, past the columns earlier rows' rowspans cover.
    struct Placed {
      size_t column;
      std::string content;
    };
    std::vector<Placed> placed;
    std::vector<int> fresh;
    size_t column = 0;
    for (const Cell& c : cells) {
      std::string content = cellContent(c.text);
      std::string attrs = content.size() < c.text.size() ? c.text.substr(0, c.text.size() - content.size()) : "";
      int rowspan = number(attrs, "rowspan"), colspan = number(attrs, "colspan");
      while (column < spans.size() && spans[column] > 0) ++column;
      placed.push_back({column, content});
      if (fresh.size() < column + static_cast<size_t>(colspan)) fresh.resize(column + static_cast<size_t>(colspan), 0);
      for (int k = 0; k < colspan; ++k) fresh[column + static_cast<size_t>(k)] = rowspan - 1;
      column += static_cast<size_t>(colspan);
    }
    for (int& span : spans) span = std::max(0, span - 1);
    if (spans.size() < fresh.size()) spans.resize(fresh.size(), 0);
    for (size_t k = 0; k < fresh.size(); ++k) spans[k] = std::max(spans[k], fresh[k]);
    cells.clear();
    if (headerOnly) {
      for (const Placed& p : placed) {
        std::string head = plainText(p.content);
        if (nameColumn < 0 && rx::test("\\b(channel|network|name|service|station|title)s?\\b", head, true) &&
            !rx::test("\\b(owner|parent|logo|former|affiliat\\w*|launch\\w*|date|notes?|type|language)\\b", head, true)) {
          nameColumn = static_cast<int>(p.column);
        }
      }
      return;
    }
    // The name column's cell (the next one when it holds only a logo).
    for (const Placed& p : placed) {
      if (nameColumn >= 0 && p.column < static_cast<size_t>(nameColumn)) continue;
      std::string text = plainText(p.content);
      if (text.empty()) continue;
      if (auto link = firstLink(p.content)) {
        addRow(link->text, link->page, rowText);
      } else {
        addRow(text, "", rowText);
      }
      break;
    }
  };
  for (std::string_view line : str::split(wikitext, '\n')) {
    std::string t = trimmed(line);
    if (auto heading = rx::first("^(={2,6})\\s*(.+?)\\s*\\1\\s*$", t)) {
      finishRow();
      section = plainText(heading->group(2));
      skip = rx::test("^(see also|references|external links|notes|further reading|sources|bibliography|footnotes|citations)$",
                      section, true);
      continue;
    }
    if (skip || t.empty()) continue;
    if (str::startsWith(t, "{|")) {
      finishRow();
      ++tables;
      spans.clear();
      nameColumn = -1;
      continue;
    }
    if (str::startsWith(t, "|}")) {
      finishRow();
      tables = std::max(0, tables - 1);
      continue;
    }
    if (tables > 0) {
      if (str::startsWith(t, "|-")) {
        finishRow();
      } else if (str::startsWith(t, "|+")) {
        continue;
      } else if (t[0] == '|' || t[0] == '!') {
        bool header = t[0] == '!';
        for (std::string& c : splitOutside(std::string_view(t).substr(1), header ? "!!" : "||")) {
          for (std::string& c2 : header ? splitOutside(c, "||") : std::vector<std::string>{c}) cells.push_back({c2, header});
        }
      } else if (!cells.empty()) {
        cells.back().text += " " + t;
      }
      continue;
    }
    if (t[0] == '*' || t[0] == '#') {
      std::string item = trimmed(rx::replace(t, "^[*#:]+", ""));
      if (auto link = firstLink(item)) {
        addRow(link->text, link->page, plainText(item));
      } else {
        std::string text = plainText(item);
        if (text.size() <= 60) addRow(text, "", text);
      }
    }
  }
  finishRow();
  return rows;
}

std::string pagesRequestUrl(const std::vector<std::string>& titles, const std::string& continueQuery) {
  return "https://en.wikipedia.org/w/api.php?action=query&format=json&formatversion=2&redirects=1"
         "&prop=redirects%7Cpageprops&ppprop=wikibase_item&rdprop=title%7Cfragment&rdlimit=max&titles=" +
         percentEncoded(str::join(titles, "|")) + continueQuery;
}

std::optional<PagesAnswer> parsePagesAnswer(std::string_view body) {
  auto doc = json::parse(body);
  if (!doc || !doc->isObject() || !(*doc)["query"].isObject()) return std::nullopt;
  PagesAnswer answer;
  const json::Value& query = (*doc)["query"];
  for (const json::Value& n : query["normalized"].items()) {
    if (n["from"].isString() && n["to"].isString()) answer.leadsTo[n["from"].asString()] = n["to"].asString();
  }
  for (const json::Value& r : query["redirects"].items()) {
    if (!r["from"].isString() || !r["to"].isString()) continue;
    // A redirect to a section is about something inside that article.
    answer.leadsTo[r["from"].asString()] = r["tofragment"].isString() ? std::string() : r["to"].asString();
  }
  for (const json::Value& p : query["pages"].items()) {
    if (!p["title"].isString() || p.has("missing") || p.has("invalid")) continue;
    PageFacts& facts = answer.pages[p["title"].asString()];
    for (const json::Value& r : p["redirects"].items()) {
      if (r["title"].isString() && !r["fragment"].isString()) facts.redirects.push_back(r["title"].asString());
    }
    if (p["pageprops"]["wikibase_item"].isString()) facts.wikidata = p["pageprops"]["wikibase_item"].asString();
  }
  if ((*doc)["continue"].isObject()) {
    for (const auto& [key, value] : (*doc)["continue"].fields()) {
      answer.continueQuery += "&" + key + "=" + percentEncoded(value.asString());
    }
  }
  return answer;
}

std::string entitiesRequestUrl(const std::vector<std::string>& ids) {
  return "https://www.wikidata.org/w/api.php?action=wbgetentities&format=json&props=aliases%7Cclaims"
         "&languages=en&ids=" +
         percentEncoded(str::join(ids, "|"));
}

std::string languageOfItem(const std::string& item) {
  static const std::unordered_map<std::string, std::string> kLanguages = {
      {"Q1860", "en"},  {"Q1321", "es"}, {"Q150", "fr"},  {"Q5146", "pt"}, {"Q1568", "hi"}, {"Q1617", "ur"},
      {"Q13955", "ar"}, {"Q7850", "zh"}, {"Q9176", "ko"}, {"Q5287", "ja"}, {"Q652", "it"},  {"Q188", "de"},
      {"Q809", "pl"},   {"Q7737", "ru"}, {"Q9199", "vi"}, {"Q9168", "fa"}};
  auto it = kLanguages.find(item);
  return it == kLanguages.end() ? item : it->second;
}

std::map<std::string, EntityFacts> parseEntities(std::string_view body) {
  std::map<std::string, EntityFacts> out;
  auto doc = json::parse(body);
  if (!doc) return out;
  for (const auto& [id, entity] : (*doc)["entities"].fields()) {
    if (!entity.isObject() || entity.has("missing")) continue;
    EntityFacts facts;
    for (const json::Value& a : entity["aliases"]["en"].items()) {
      if (a["value"].isString()) facts.aliases.push_back(a["value"].asString());
    }
    // The first language, or English when it's one of them.
    for (const json::Value& claim : entity["claims"]["P407"].items()) {
      const json::Value& value = claim["mainsnak"]["datavalue"]["value"];
      if (!value["id"].isString()) continue;
      std::string language = languageOfItem(value["id"].asString());
      if (facts.language.empty() || language == "en") facts.language = language;
    }
    for (const json::Value& claim : entity["claims"]["P576"].items()) {
      const json::Value& value = claim["mainsnak"]["datavalue"]["value"];
      if (!value["time"].isString()) continue;
      // "+2024-07-01T00:00:00Z"; a year alone has "-00-00".
      std::string time = value["time"].asString();
      if (auto m = rx::first("^\\+?(\\d{4})-(\\d{2})-(\\d{2})", time)) {
        facts.closed = m->group(2) == "00" ? std::string(m->group(1))
                                           : std::string(m->group(1)) + "-" + std::string(m->group(2)) + "-" +
                                                 (m->group(3) == "00" ? std::string("01") : std::string(m->group(3)));
      } else {
        facts.closed = "yes";
      }
      break;
    }
    out[id] = std::move(facts);
  }
  return out;
}

std::vector<IptvChannel> parseIptvOrg(std::string_view channelsCsv, std::string_view feedsCsv) {
  std::vector<IptvChannel> out;
  auto channelRows = logos::csvRows(channelsCsv);
  if (channelRows.empty()) return out;
  auto column = [](const std::vector<std::string>& head, const char* name) -> size_t {
    auto it = std::find(head.begin(), head.end(), name);
    return it == head.end() ? head.size() : static_cast<size_t>(it - head.begin());
  };
  auto at = [](const std::vector<std::string>& row, size_t i) { return i < row.size() ? row[i] : std::string(); };
  // The main feed's language and area, by channel.
  std::unordered_map<std::string, std::string> languageOf, areaOf;
  auto feedRows = logos::csvRows(feedsCsv);
  if (!feedRows.empty()) {
    const auto& head = feedRows.front();
    size_t channel = column(head, "channel"), main = column(head, "is_main"), languages = column(head, "languages"),
           area = column(head, "broadcast_area");
    for (size_t r = 1; r < feedRows.size(); ++r) {
      const auto& row = feedRows[r];
      std::string id = at(row, channel);
      if (id.empty()) continue;
      bool isMain = at(row, main) == "TRUE";
      std::string firstArea = at(row, area).substr(0, at(row, area).find(';'));
      if (isMain || !areaOf.count(id)) areaOf[id] = firstArea;
      std::string first = at(row, languages).substr(0, at(row, languages).find(';'));
      if (!first.empty() && (isMain || !languageOf.count(id))) languageOf[id] = language3to2(first);
    }
  }
  const auto& head = channelRows.front();
  size_t id = column(head, "id"), name = column(head, "name"), alt = column(head, "alt_names"),
         country = column(head, "country"), closed = column(head, "closed"), categories = column(head, "categories"),
         nsfw = column(head, "is_nsfw"), network = column(head, "network");
  for (size_t r = 1; r < channelRows.size(); ++r) {
    const auto& row = channelRows[r];
    IptvChannel c;
    c.id = at(row, id);
    c.name = at(row, name);
    if (c.id.empty() || c.name.empty()) continue;
    std::string altText = at(row, alt);
    for (std::string_view a : str::split(altText, ';')) {
      if (!trimmed(a).empty()) c.altNames.push_back(trimmed(a));
    }
    c.country = at(row, country);
    c.closed = at(row, closed);
    c.categories = at(row, categories);
    c.nsfw = at(row, nsfw) == "TRUE";
    c.network = at(row, network);
    if (auto it = languageOf.find(c.id); it != languageOf.end()) c.language = it->second;
    if (auto it = areaOf.find(c.id); it != areaOf.end()) c.area = it->second;
    out.push_back(std::move(c));
  }
  return out;
}

namespace {

bool iptvJunk(const IptvChannel& c) {
  return c.nsfw || rx::test("\\b(legislative|xxx|shop)\\b", c.categories) ||
         rx::test("^[KW][A-Z]{2,3}(-(TV|DT|LD|LP|CD|CA))?\\b", c.name) || c.network == "Infomercials";
}

}  // namespace

bool iptvNoise(const IptvChannel& c) {
  return iptvJunk(c) ||
         rx::test("^(PlutoTV|Samsung|Roku|Xumo|Tubi|Stirr|Plex|LocalNow|Distro|Vizio|Redbox|Freevee)", c.id);
}

bool iptvFreeChannel(const IptvChannel& c) {
  return c.country == "US" && c.closed.empty() && !iptvJunk(c) && (c.area == "c/US" || c.area == "r/NAM");
}

std::string withoutService(std::string_view name) {
  static const char* const kServices =
      "(pluto tv|samsung tv plus|roku channel|the roku channel|roku|xumo play|xumo|tubi|plex|stirr|local now|"
      "freevee|vizio watchfree\\+?|redbox|distro tv)";
  std::string out = rx::replace(name, std::string("^\\s*") + kServices + "\\s*[:|-]?\\s+", "", true);
  out = rx::replace(out, std::string("\\s+(by|on|from)?\\s*") + kServices + "\\s*$", "", true);
  out = trimmed(out);
  return out.empty() ? std::string(name) : out;
}

Data assemble(const Sources& sources, const std::string& made) {
  Data data;
  data.made = made;
  // Where a list's link leads: through its normalization and redirect, to
  // an article Wikipedia has ("" otherwise).
  auto resolve = [&](const std::string& title) -> std::string {
    if (title.empty()) return "";
    std::string t = title;
    for (int hop = 0; hop < 3; ++hop) {
      auto it = sources.leadsTo.find(t);
      if (it == sources.leadsTo.end()) break;
      t = it->second;
      if (t.empty()) return "";
    }
    return sources.pages.count(t) ? t : std::string();
  };
  // The list a row counts for: a list's "Defunct" section is the defunct
  // list's; its distant locals and radio are stations, not channels.
  auto listOf = [](const std::string& list, const Row& row) -> std::string {
    if (rx::test("\\b(defunct|former)\\b", row.section, true)) return "defunct";
    if (rx::test("\\b(distant locals?|superstations?)\\b", row.section, true)) return "local";
    if (rx::test("\\bradio\\b", row.section, true)) return "radio";
    return list;
  };
  // Several channels linking to one article (a company, a family of
  // channels): only the one the article is named after takes its facts.
  // (A defunct row is an old name, not another channel.)
  std::unordered_map<std::string, std::set<std::string>> textsOf;
  for (const auto& [list, rows] : sources.rows) {
    for (const Row& row : rows) {
      std::string title = resolve(row.title);
      if (!title.empty() && listOf(list, row) != "defunct") textsOf[title].insert(looseKey(row.text));
    }
  }

  struct Draft {
    std::set<std::string> keys;              // alias keys so far
    std::vector<std::string> fromArticle;    // redirects and Wikidata aliases, checked below
    std::string articleClosed;               // Wikidata's closing date
    bool current = false;                    // on a list of channels on the air
  };
  std::vector<Draft> drafts;
  std::unordered_map<std::string, size_t> byId;
  for (const ListPage& page : listPages()) {
    auto rowsIt = sources.rows.find(page.list);
    if (rowsIt == sources.rows.end()) continue;
    for (const Row& row : rowsIt->second) {
      std::string list = listOf(page.list, row);
      std::string title = resolve(row.title);
      const PageFacts* facts = title.empty() ? nullptr : &sources.pages.at(title);
      // A row linking to a bigger article ("Starz Encore Action" to "Starz
      // Encore", "Comedy.TV" to its owner) is its own channel, without the
      // article's facts.
      if (facts) {
        std::string rowKey = looseKey(row.text);
        bool named = rowKey == looseKey(withoutBrackets(title));
        bool same = named;
        for (const std::string& r : facts->redirects) same = same || rowKey == looseKey(withoutBrackets(r));
        if (!same || (!named && textsOf[title].size() > 1)) {
          title.clear();
          facts = nullptr;
        }
      }
      std::string id = facts ? title : withoutBrackets(row.text);
      if (exactKey(id).empty()) continue;
      auto [it, fresh] = byId.emplace(id, data.entries.size());
      if (fresh) {
        Entry e;
        e.id = id;
        e.name = facts ? withoutBrackets(title) : withoutBrackets(row.text);
        e.list = list;
        e.genre = row.section;
        data.entries.push_back(std::move(e));
        drafts.emplace_back();
      }
      Entry& e = data.entries[it->second];
      Draft& d = drafts[it->second];
      addAlias(e, d.keys, row.text);
      if (list == "defunct") {
        // A defunct row of a channel on a current list, or one under another
        // name than its article's ("Fox Sports North" linking to FanDuel
        // Sports Network North), is an old name, not a closing.
        bool rebrand = facts && looseKey(row.text) != looseKey(withoutBrackets(title));
        if (!d.current && fresh && !rebrand) {
          auto years = rx::all("\\b(19|20)\\d{2}\\b", row.rowText);
          e.closed = years.empty() ? std::string("yes") : std::string(years.back().group(0));
        }
      } else {
        if (!d.current) {
          e.list = list;
          e.genre = row.section;
          e.closed.clear();
        }
        d.current = true;
      }
      if (list == "regional sports" || rx::test("\\bregional\\b", row.section, true)) {
        if (e.places.empty()) {
          std::string lowered = str::lower(row.rowText);
          for (const rx::Match& m : rx::all(regionPattern(), lowered)) {
            std::string place(m.group(1));
            if (std::find(e.places.begin(), e.places.end(), place) == e.places.end()) e.places.push_back(place);
          }
        }
      }
      if (!facts) continue;
      for (const std::string& r : facts->redirects) d.fromArticle.push_back(r);
      if (!facts->wikidata.empty()) {
        e.wikidata = facts->wikidata;
        if (auto entity = sources.entities.find(facts->wikidata); entity != sources.entities.end()) {
          for (const std::string& a : entity->second.aliases) d.fromArticle.push_back(a);
          if (e.language.empty()) e.language = entity->second.language;
          d.articleClosed = entity->second.closed;
        }
      }
    }
  }
  for (Entry& e : data.entries) {
    if (e.list == "spanish" && e.language.empty()) e.language = "es";
  }

  // iptv-org's US channels (without its noise), by name.
  std::unordered_map<std::string, std::vector<size_t>> usByKey;
  for (size_t i = 0; i < sources.iptv.size(); ++i) {
    const IptvChannel& c = sources.iptv[i];
    if (c.country != "US" || iptvNoise(c)) continue;
    std::vector<std::string> names = c.altNames;
    names.push_back(c.name);
    for (const std::string& n : names) {
      std::string key = exactKey(n);
      if (key.empty()) continue;
      std::vector<size_t>& list = usByKey[key];
      if (std::find(list.begin(), list.end(), i) == list.end()) list.push_back(i);
    }
  }
  // Each entry's iptv-org channel, by the names the lists give it: one, or none.
  std::unordered_set<std::string> listedNames;
  for (const Entry& e : data.entries) listedNames.insert(exactKey(e.name));
  for (size_t i = 0; i < data.entries.size(); ++i) {
    Entry& e = data.entries[i];
    Draft& d = drafts[i];
    std::set<size_t> found;
    std::vector<std::string> names = e.aliases;
    names.push_back(e.name);
    for (const std::string& n : names) {
      auto it = usByKey.find(exactKey(n));
      if (it != usByKey.end()) found.insert(it->second.begin(), it->second.end());
    }
    const IptvChannel* own = found.size() == 1 ? &sources.iptv[*found.begin()] : nullptr;
    // The article's other names, unless one is another channel: another
    // entry's name, or another iptv-org channel's ("ActionMax" redirects
    // to Cinemax, "C-SPAN2" to C-SPAN).
    if (e.list != "local" && e.list != "radio") {
      for (const std::string& a : d.fromArticle) {
        std::string key = exactKey(withoutBrackets(a));
        if (notAChannelName(a) || listedNames.count(key)) continue;
        auto other = usByKey.find(key);
        if (other != usByKey.end() &&
            (!own || std::find(other->second.begin(), other->second.end(), *found.begin()) == other->second.end())) {
          continue;
        }
        addAlias(e, d.keys, a);
      }
    }
    // Closing dates: a defunct list's own, Wikidata's for channels on no
    // current list; iptv-org's for any (Wikidata's "dissolved" is often an
    // older name's, and a defunct row of a current channel its rebrand).
    if (!d.current && !d.articleClosed.empty()) e.closed = d.articleClosed;
    if (!own) continue;
    e.iptvOrg = own->id;
    addAlias(e, d.keys, own->name);
    for (const std::string& a : own->altNames) addAlias(e, d.keys, a);
    if (!own->closed.empty()) e.closed = own->closed;
    if (e.language.empty()) e.language = own->language;
  }

  // The free tier: iptv-org's US free streaming channels the lists don't
  // have (Pluto TV's, FAST channels'), matched only when no listed channel
  // is ("free" entries).
  std::unordered_set<std::string> taken;
  for (const Entry& e : data.entries) {
    taken.insert(exactKey(e.name));
    for (const std::string& a : e.aliases) taken.insert(exactKey(a));
    if (!e.iptvOrg.empty()) taken.insert("id:" + e.iptvOrg);
  }
  for (const IptvChannel& c : sources.iptv) {
    if (!iptvFreeChannel(c) || taken.count("id:" + c.id)) continue;
    // A service's one-word channel keeps the service's name ("Pluto TV
    // Horror", not "Horror", which names other channels too).
    auto bare = [](const std::string& n) {
      std::string without = withoutService(n);
      return without != n && without.find(' ') == std::string::npos ? n : without;
    };
    std::string name = bare(c.name);
    std::vector<std::string> names = c.altNames;
    names.push_back(c.name);
    names.push_back(name);
    bool listedAlready = false;
    for (const std::string& n : names) listedAlready = listedAlready || taken.count(exactKey(bare(n)));
    if (listedAlready || exactKey(name).empty()) continue;
    Entry e;
    e.id = "iptv:" + c.id;
    e.name = name;
    e.list = "free";
    e.genre = c.categories;
    e.language = c.language;
    e.iptvOrg = c.id;
    std::set<std::string> keys;
    for (const std::string& n : names) {
      addAlias(e, keys, n);
      addAlias(e, keys, bare(n));
    }
    taken.insert(exactKey(name));
    data.entries.push_back(std::move(e));
  }

  // Names only other countries' channels have: a hint at the language
  // ("PK:ur"; "CA:en" is English all the same).
  std::unordered_set<std::string> listed;
  for (const Entry& e : data.entries) {
    listed.insert(exactKey(e.name));
    for (const std::string& a : e.aliases) listed.insert(exactKey(a));
  }
  for (const IptvChannel& c : sources.iptv) {
    if (c.country.empty() || c.country == "US" || iptvNoise(c)) continue;
    std::vector<std::string> names = c.altNames;
    names.push_back(c.name);
    for (const std::string& n : names) {
      std::string key = exactKey(n);
      if (key.size() < 4 || usByKey.count(key) || listed.count(key)) continue;
      std::string country = c.country == "GB" ? std::string("UK") : c.country;
      data.elsewhere.emplace(key, country + ":" + c.language);
    }
  }
  return data;
}

}  // namespace marigold::catalog::directory::sources
