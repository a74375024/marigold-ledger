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
// logo (tried before the provider's). Only US channels with PNG or JPEG
// logos are kept, by guide id ("cnn.us", lower-cased) and by catalog core
// key. CI makes the index (tools/channeldir) and the app carries it in the
// channel directory (catalog/directory.hpp); the TV never fetches it.
class IptvOrgIndex {
 public:
  struct Saved {
    std::unordered_map<std::string, std::string> byID;
    std::unordered_map<std::string, std::string> byName;
  };

  IptvOrgIndex() = default;
  explicit IptvOrgIndex(Saved saved) : saved_(std::move(saved)) {}

  // Builds the index from iptv-org's channels.csv and logos.csv (and with
  // feeds.csv, other countries' channels with an English feed). nullopt
  // when the columns are missing or no US channel has a usable logo.
  static std::optional<Saved> build(std::string_view channelsCsv, std::string_view logosCsv,
                                    std::string_view feedsCsv = {});

  // By the channel's guide id, then its directory entry's iptv-org id
  // ("RacerNetwork.us"; empty when it has none), then its name.
  std::optional<std::string> url(const std::optional<std::string>& epgID, std::string_view name,
                                 const std::string& directoryIptvOrg = std::string()) const;
  const Saved& saved() const { return saved_; }
  bool empty() const { return saved_.byID.empty() && saved_.byName.empty(); }

 private:
  Saved saved_;
};

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
  // "provider", "public database" (for the channel report; never a link).
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

// The library's national logo file for a channel name ("MTV2" ->
// "mtv-2-us.png"), when it has one.
std::optional<std::string> logoSetFile(std::string_view name);

// A hand-picked logo link for a channel no logo database has ("Bowling
// TV"), when there is one.
std::optional<std::string> handPicked(std::string_view name);

// A station's network word ("FOX", "UNIMAS", "INDEPENDENT"), for its logo
// and the line under its call sign: from its name ("FOX 2"), else from the
// reference data by its call sign ("KFSF" is UniMás); nullopt when neither
// tells.
std::optional<std::string> stationNetwork(std::string_view name, const std::string& callsign,
                                          const catalog::reference::Index& reference);

// Logos for a guide row. Locals show a logo with the call sign and "FOX 2";
// any other channel with a logo to try shows it alone when the one that
// loads is good (fillsCard()), whether curated, from the public database or
// the provider's; a channel with none shows its name. `core` / `callsign`
// come from the catalog; `provider` is the logo the provider gave (empty for
// none). `iptvOrg` may be null.
LogoSpec spec(std::string_view name, const std::optional<std::string>& core, bool isWest,
              bool isLocal, const std::optional<std::string>& callsign,
              std::optional<int> virtualNumber, const std::string& provider,
              const std::optional<std::string>& epgID,
              std::optional<catalog::LineupCategory> category, const IptvOrgIndex* iptvOrg,
              const std::string& directoryIptvOrg = std::string());

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
