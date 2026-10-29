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
#include "core/util/time.hpp"

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
    // Curated logo, shown alone (name only if it fails to load).
    logoOnly,
    // A local station: logo, call sign, network and number.
    local,
    // Logo (if any) next to the channel's name.
    named,
  };

  std::vector<std::string> urls;
  // Where each of `urls` comes from: "logo set", "provider", "public
  // database" (for the channel report; never a link).
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

// Logos for a guide row. National channels whose logo is curated show the
// logo alone; locals show a logo with the call sign and "FOX 2"; anything
// else shows a logo (when one loads) next to its name. `core` / `callsign`
// come from the catalog; `provider` is the logo the provider gave (empty for
// none). `iptvOrg` may be null.
LogoSpec spec(std::string_view name, const std::optional<std::string>& core, bool isWest,
              bool isLocal, const std::optional<std::string>& callsign,
              std::optional<int> virtualNumber, const std::string& provider,
              const std::optional<std::string>& epgID,
              std::optional<catalog::LineupCategory> category, const IptvOrgIndex* iptvOrg,
              const std::string& directoryIptvOrg = std::string());

// A small CSV reader (quoted fields, doubled quotes, CRLF).
std::vector<std::vector<std::string>> csvRows(std::string_view text);

}  // namespace marigold::logos
