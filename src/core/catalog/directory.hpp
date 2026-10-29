// The channel directory: real US channels with their other names, language,
// genre, area and whether they've shut down, so a provider's channel can be
// told by what it is rather than by guesses at its name.
//
// CI builds it (tools/channeldir) in layers: Wikipedia's edited channel
// lists are the backbone, each listed article's redirects and its Wikidata
// facts add other names, languages and closing dates, and the iptv-org
// database adds the same only for channels already listed (its own local
// stations, public access and adult channels never become entries), and
// its US free streaming channels the lists don't have make a free tier. The
// build packs the file into the app (cmake/channel-directory.cmake); a TV
// never fetches it. Our corrections below win over every source.
//
// The lineup uses it for channels that aren't stations (catalog::build):
// a known channel's row, name, language, area and closing come from it.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace marigold::catalog::directory {

struct Entry {
  std::string id;                    // stable, from the Wikipedia title ("UPtv")
  std::string name;                  // as the list writes it
  std::vector<std::string> aliases;  // other and former names
  std::string language;              // "en", "es", "hi"...; a Wikidata id ("Q9610") when unmapped; empty: unknown
  std::string genre;                 // the list section it sits in ("Sports", "Movies")
  // "pay", "broadcast", "spanish", "regional sports", "defunct", "local",
  // "radio"; "free" for iptv-org's free streaming channels no list has
  std::string list;
  std::vector<std::string> places;   // regional: the area words in its row ("minnesota")
  std::string closed;                // "2024-07-01", "yes" (date unknown), or empty
  std::string wikidata;              // "Q12345"
  std::string iptvOrg;               // "UPtv.us"

  bool operator==(const Entry& o) const;
};

struct Data {
  std::string made;  // "2026-10-28"
  std::vector<Entry> entries;
  // Names found only among other countries' channels in iptv-org, by
  // exact key, with the country and its feed's language: "arydigital" ->
  // "PK:ur". A hint at a channel's language ("CA:en" is English all the same).
  std::map<std::string, std::string> elsewhere;
  // Fallback logos (logos::IptvOrgIndex): iptv-org's US channels with a PNG
  // or JPEG logo, by iptv-org id in lower case ("cnn.us", also a guide id),
  // and by the names' core key ("cnn"). The TV never fetches them.
  std::map<std::string, std::string> logos;
  std::map<std::string, std::string> logosByName;

  bool empty() const { return entries.empty(); }
};

std::string toJson(const Data& data);
std::optional<Data> fromJson(std::string_view text);
// The build's directory (empty when it was built without one).
const Data& builtIn();

// A name's keys. Exact: its letters and digits, lower case, without
// bracketed notes, quality words, a feed's coast at the end or a US marker
// at either end ("The Movie Channel (TMC) HD" -> "themoviechannel").
// Loose: also without a leading "the" and trailing "tv", "network",
// "channel" or "television" ("UP TV", "UPtv" -> "up").
std::string exactKey(std::string_view name);
std::string looseKey(std::string_view name);

enum class MatchKind { none, exact, alias, loose, ambiguous };
const char* rawValue(MatchKind kind);

struct Match {
  const Entry* entry = nullptr;
  MatchKind kind = MatchKind::none;
  // No entry, but the name is another country's channel ("PK:ur").
  std::string elsewhere;
};

// Our corrections, which win over the sources: a name that is another
// listed channel's ("WGN America" is NewsNation).
struct Correction {
  const char* name;
  const char* is;  // the entry's name
};
const std::vector<Correction>& corrections();
// The name to show for an entry: its own, or the channel's where the list
// row names its owner ("Blaze Media" is BlazeTV).
std::string shownName(const Entry& entry);

class Index {
 public:
  explicit Index(const Data& data);
  // A provider channel's cleaned name (catalog::cleanName). A name matches
  // an entry's own name (exact), one of its other names or a correction
  // (alias), or, failing both, a single entry's loose key (loose); a key
  // shared by two entries matches neither (ambiguous). The free tier is
  // only asked when no listed channel has the name, even loosely ("Bounce"
  // is Bounce TV). An other name that is the entry's name and more
  // ("GritXtra") isn't one, and one left with a single word ("The Action
  // Channel") matches only exactly.
  Match match(std::string_view name) const;
  // The entry with this id (nullptr when none).
  const Entry* entryById(const std::string& id) const;

 private:
  const Data* data_;
  std::unordered_map<std::string, size_t> byCorrection_;
  std::unordered_map<std::string, size_t> byId_;
  std::unordered_map<std::string, std::vector<size_t>> byName_;
  std::unordered_map<std::string, std::vector<size_t>> byAlias_;
  std::unordered_map<std::string, std::vector<size_t>> byLoose_;
  std::unordered_map<std::string, std::vector<size_t>> freeByName_;
  std::unordered_map<std::string, std::vector<size_t>> freeByAlias_;
  std::unordered_map<std::string, std::vector<size_t>> freeByLoose_;
};
// The built-in directory's index, made on first use.
const Index& builtInIndex();

}  // namespace marigold::catalog::directory
