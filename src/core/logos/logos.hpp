// Which logos a guide row tries for its channel, best first, and how its
// cell reads (ChannelLogos.spec in tvOS); plus the iptv-org fallback index
// built from that project's public-domain CSV files, and a small CSV reader.
// Loading and decoding images is platform code.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/util/pixels.hpp"
#include "core/util/time.hpp"

namespace marigold::catalog::reference {
class Index;
}

namespace marigold::logos {

// iptv-org channel database logos, for national channels with no curated
// logo (tried before the provider's). US channels (and other countries'
// with an English feed) with a usable PNG or JPEG logo are kept by guide id
// ("cnn.us", lower-cased) and by catalog core key; every other country's
// only by guide id, packed (Abroad). Of a channel's logos the best for the
// guide's wide cell is kept (logoQuality()). CI makes the index
// (tools/channeldir) and the app carries it in the channel directory
// (catalog/directory.hpp); the TV never fetches it.
class IptvOrgIndex {
 public:
  // Other countries' channels by guide id, packed to stay small: the link
  // beginnings ("https://i.imgur.com/") and, sorted by id, each channel as
  // two words separated by spaces: its id lower-cased, then its link as the
  // beginning's index (0-9, a-z) and the rest ("tf1.fr 0AbCdEf.png ...").
  struct Abroad {
    std::vector<std::string> prefixes;
    std::string words;
  };
  struct Saved {
    std::unordered_map<std::string, std::string> byID;
    std::unordered_map<std::string, std::string> byName;
    // Abroad's beginnings and words. The words aren't copied: the text must
    // outlive the index (the built-in directory's lives as long as the app).
    std::vector<std::string> abroadPrefixes;
    std::string_view abroadWords;
  };

  IptvOrgIndex() = default;
  explicit IptvOrgIndex(Saved saved);

  // Builds the index from iptv-org's channels.csv and logos.csv (and with
  // feeds.csv, other countries' channels with an English feed). nullopt
  // when the columns are missing or no US channel has a usable logo.
  static std::optional<Saved> build(std::string_view channelsCsv, std::string_view logosCsv,
                                    std::string_view feedsCsv = {});
  // Every other open channel with a usable logo on a host the index
  // already uses (abroadHosts()), not in `known`'s ids, packed.
  static Abroad buildAbroad(std::string_view channelsCsv, std::string_view logosCsv, const Saved& known);
  // The link beginnings buildAbroad() keeps, best used first.
  static const std::vector<std::string>& abroadHosts();

  // By the channel's guide id, then its directory entry's iptv-org id
  // ("RacerNetwork.us"; empty when it has none), then its name, then the
  // guide id among other countries' channels.
  std::optional<std::string> url(const std::optional<std::string>& epgID, std::string_view name,
                                 const std::string& directoryIptvOrg = std::string()) const;
  // Another country's channel's logo by its guide id (any case).
  std::optional<std::string> abroad(std::string_view id) const;
  const Saved& saved() const { return saved_; }
  size_t abroadCount() const { return abroadCount_; }
  bool empty() const { return saved_.byID.empty() && saved_.byName.empty() && abroadCount_ == 0; }

 private:
  Saved saved_;
  // Open addressing over abroadWords: each id's offset + 1 (0: free).
  std::vector<uint32_t> slots_;
  size_t abroadCount_ = 0;
};

// How well a logo of iptv-org's suits the guide's wide cell, higher is
// better; nullopt for one never to use: not PNG or JPEG, not https, a
// search engine's thumbnail or a small JPEG (a thumbnail too). PNG, the
// channel's own (not a feed's), a wide shape (1.3 to 5 times as wide as
// tall), 200 pixels or more and a "white" or "transparent" tag each count;
// a shape the cell can't show alone (a speck, a sliver, a thread) scores
// below zero.
std::optional<int> logoQuality(std::string_view format, std::string_view url, std::string_view width,
                               std::string_view height, std::string_view tags, bool main);

struct LogoSpec {
  enum class Style {
    // The logo alone, filling the cell, once one loads that fillsCard()
    // passes (whatever its source); a small logo next to the name when the
    // one that loads is poor; the name while none has loaded.
    logoOnly,
    // A local station: logo, call sign, network and number.
    local,
    // Logo (if any) next to the channel's name: a channel with no logo to
    // try, or a local with no call sign (its logo is its network's, so the
    // name tells it apart).
    named,
  };

  std::vector<std::string> urls;
  // Where each of `urls` comes from: "logo set", "hand-picked",
  // "provider", "public database", "Wikidata", "former brand" (for the
  // channel report; never a link).
  std::vector<std::string> sources;
  Style style = Style::named;
  // Style::local only.
  std::string callsign;
  std::string line;
  bool isWest = false;
  // Second line under a name with no logo ("News").
  std::optional<std::string> subtitle;

  bool operator==(const LogoSpec& o) const {
    return urls == o.urls && style == o.style && callsign == o.callsign && line == o.line &&
           isWest == o.isWest && subtitle == o.subtitle;
  }
};

// The links a row tries, in one string, one per line (the logo cache's key).
std::string chain(const std::vector<std::string>& urls);

// The library's logo file for a channel name, when it has one: a national
// one ("MTV2" -> "mtv-2-us.png"), one the library has under another name
// ("Family Movie Classics" -> "fmc-family-movie-classics-us.png"), a free
// streaming channel's by its exact name ("AFV TV" ->
// "misc/vod/afv-tv-vod.png"), else for a numbered, extra or alternate feed
// its parent's ("Big Ten Alternate 2" -> "big-ten-network-us.png"). Files
// are under kLogoBase, those starting "misc/" under kLibraryRoot
// (logoSetUrl()).
std::optional<std::string> logoSetFile(std::string_view name);
// A logoSetFile() file's link.
std::string logoSetUrl(const std::string& file);
// The same for a logo standing alone in the wide guide cell: the file's
// horizontal version where that fills the cell better (horizontalNames()).
std::string wideLogoSetUrl(const std::string& file);
// A regional sports network renamed since the library's file: its former
// brand's file ("FanDuel Sports Network Detroit" ->
// "bally-sports-detroit-us.png"), tried only when nothing shows the new one.
std::optional<std::string> formerBrandFile(std::string_view name);

// A hand-picked logo link for a channel no logo database has ("Bowling
// TV"), when there is one.
std::optional<std::string> handPicked(std::string_view name);

// A station's network word ("FOX", "UNIMAS", "INDEPENDENT"), for its logo
// and the line under its call sign: from its name ("FOX 2"), else from the
// reference data by its call sign ("KFSF" is UniMás); nullopt when neither
// tells. A name with two networks' words ("NBC (KSTS) TELEMUNDO") goes by
// the one the reference data gives the station.
std::optional<std::string> stationNetwork(std::string_view name, const std::string& callsign,
                                          const catalog::reference::Index& reference);

// Logos for a guide row. Stations (`isStation`: a row with a call sign, in
// whatever section) show a logo with the call sign and "FOX 2"; any other
// channel with a logo to try shows it alone when the one that loads is good
// (fillsCard()), whether curated, from the public database or the
// provider's; a channel with none shows its name. `core` / `callsign` come
// from the catalog; `provider` is the logo the provider gave (empty for
// none). `iptvOrg` may be null. `directoryLogo` is the channel directory
// entry's Wikidata logo (empty for none).
LogoSpec spec(std::string_view name, const std::optional<std::string>& core, bool isWest,
              bool isStation, const std::optional<std::string>& callsign,
              std::optional<int> virtualNumber, const std::string& provider,
              const std::optional<std::string>& epgID,
              std::optional<catalog::LineupCategory> category, const IptvOrgIndex* iptvOrg,
              const std::string& directoryIptvOrg = std::string(),
              const std::string& referenceId = std::string(),
              const std::string& directoryLogo = std::string());

// The reference channel id in a lineup row's id ("e|r:hbo~west" -> "hbo");
// empty for rows the reference data doesn't know.
std::string referenceIdOf(std::string_view rowId);

// Whether a loaded logo is good enough to stand alone on a channel card with
// no name beside it (the guide's channel cell has about 154 x 60 for it):
// something visible, not a blank one-colour tile, not a speck, not a thin
// sliver or thread, and not blown up more than twice to fill the card. The
// mini guide, Search, Live Events and the player use the same rule to show
// a logo or, in its place, the channel's name.
bool fillsCard(const px::Shape& shape);

// A small CSV reader (quoted fields, doubled quotes, CRLF).
std::vector<std::vector<std::string>> csvRows(std::string_view text);

}  // namespace marigold::logos
