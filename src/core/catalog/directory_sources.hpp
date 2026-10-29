// How the channel directory is made from its sources (catalog/directory.hpp),
// as parsing and assembly the tests can run; tools/channeldir does the
// fetching, in CI only.
//
// Layers, in order of trust: Wikipedia's channel lists (which channels
// there are), each listed article's redirects and its Wikidata facts (other
// names, language, closing date), then iptv-org, only to add the same for
// channels already listed. Our corrections (directory.hpp) win at match
// time.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/directory.hpp"

namespace marigold::catalog::directory::sources {

// A list page: which list, and the titles to try (the first that loads).
struct ListPage {
  std::string list;
  std::vector<std::string> titles;
};
const std::vector<ListPage>& listPages();

// One channel a list names: its article (empty for an unlinked name or a
// link into another article's section), the name as written, the section
// it sits in and its row's plain text.
struct Row {
  std::string title;
  std::string text;
  std::string section;
  std::string rowText;
  // Its table's closing-date cell ("2025-12-31", "2024-04", "1998"; "yes"
  // when the table has the column but the row no date); empty without one.
  std::string closed = {};
};
// A closing date as written ("December 31, 2025", "April 2024", "1998")
// as "2025-12-31", "2024-04", "1998"; empty when it holds no year.
std::string closingDate(std::string_view text);
// Per table row the first cell with a link (or the first cell's text); per
// bullet its first link. Files, categories, references and the closing
// sections (See also, References...) are skipped.
std::vector<Row> parseList(std::string_view wikitext);
// Wikitext as plain text: links as their text, no templates, references,
// formatting or tags.
std::string plainText(std::string_view wikitext);

// Wikipedia's facts about up to 50 articles: where each title leads, the
// redirects to it (other names; redirects to a section are another
// channel's) and its Wikidata item.
std::string pagesRequestUrl(const std::vector<std::string>& titles, const std::string& continueQuery);
struct PageFacts {
  std::vector<std::string> redirects;
  std::string wikidata;
};
struct PagesAnswer {
  std::map<std::string, std::string> leadsTo;  // asked title -> the article ("" for a section or a missing page)
  std::map<std::string, PageFacts> pages;      // by article title
  std::string continueQuery;                   // "&rdcontinue=...&continue=..." while there's more
};
std::optional<PagesAnswer> parsePagesAnswer(std::string_view body);

// Wikidata's facts about up to 50 items.
std::string entitiesRequestUrl(const std::vector<std::string>& ids);
struct EntityFacts {
  std::vector<std::string> aliases;
  std::string language;  // "en", "es"...; the item id when unmapped
  std::string closed;    // "2024-07-01"
};
std::map<std::string, EntityFacts> parseEntities(std::string_view body);
// A language item as a code ("Q1321" -> "es"); the id itself when unknown.
std::string languageOfItem(const std::string& item);

// iptv-org's channels with their main feed's language.
constexpr const char* kIptvOrgBase = "https://cdn.jsdelivr.net/gh/iptv-org/database@master/data/";
struct IptvChannel {
  std::string id;
  std::string name;
  std::vector<std::string> altNames;
  std::string country;
  std::string closed;
  std::string categories;
  bool nsfw = false;
  std::string network;   // "Pluto TV"
  std::string owners;    // "FX Networks LLC;The Walt Disney Company"
  std::string launched;  // "1994-10-31"
  std::string language;  // "en", "es"...
  std::string area;      // the main feed's: "c/US" (the country), "ct/USNYC" (a city)...
};
std::vector<IptvChannel> parseIptvOrg(std::string_view channelsCsv, std::string_view feedsCsv);
// Entries that never become or enrich directory entries: local stations,
// government and public access, adult channels, infomercials, and (for
// the lists' channels) free-streaming copies.
bool iptvNoise(const IptvChannel& channel);
// A US free streaming channel the lists don't have, for the directory's
// free tier: national, open, not noise (a free-streaming service's copy
// is fine here).
bool iptvFreeChannel(const IptvChannel& channel);
// A free streaming channel's name without its service ("Pluto TV Cheers"
// -> "Cheers").
std::string withoutService(std::string_view name);
// Two iptv-org rows of one channel under an old and a new name ("FXM" and
// "FX Movie Channel"): the same launch date, or the same owners with a
// launch date missing. C-SPAN and C-SPAN 2 have different launch dates.
bool sameIptvChannel(const IptvChannel& a, const IptvChannel& b);

// The directory from the parsed sources. `rows` by list ("pay" -> rows),
// `pages` and `leadsTo` from parsePagesAnswer, `entities` by item.
struct Sources {
  std::map<std::string, std::vector<Row>> rows;
  std::map<std::string, std::string> leadsTo;
  std::map<std::string, PageFacts> pages;
  std::map<std::string, EntityFacts> entities;
  std::vector<IptvChannel> iptv;
};
Data assemble(const Sources& sources, const std::string& made);

// Channels every week's directory must know; a list page read wrong or an
// odd edit shows here first.
const std::vector<std::string>& wellKnownChannels();
// Why this week's directory shouldn't replace last week's (empty: it can):
// a well-known channel gone, or many fewer entries (over 10%) or logos
// (over 20%). Logos aren't compared when last week's had none.
std::vector<std::string> tooDifferent(const Data& now, const Data& before);

}  // namespace marigold::catalog::directory::sources
