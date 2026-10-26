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
// logo and no provider logo. Only US channels with PNG or JPEG logos are
// kept, by guide id ("cnn.us", lower-cased) and by catalog core key.
class IptvOrgIndex {
 public:
  struct Saved {
    time::Seconds fetched = 0;
    std::unordered_map<std::string, std::string> byID;
    std::unordered_map<std::string, std::string> byName;
  };

  // The two files, downloaded at most once a week.
  static constexpr const char* kBase = "https://cdn.jsdelivr.net/gh/iptv-org/database@master/data/";
  static constexpr const char* kChannelsFile = "channels.csv";
  static constexpr const char* kLogosFile = "logos.csv";
  static constexpr time::Seconds kMaxAge = 7 * 24 * 3600;

  IptvOrgIndex() = default;
  explicit IptvOrgIndex(Saved saved) : saved_(std::move(saved)) {}

  // Builds the index from channels.csv and logos.csv. nullopt when the
  // columns are missing or no US channel has a usable logo.
  static std::optional<Saved> build(std::string_view channelsCsv, std::string_view logosCsv,
                                    time::Seconds now);

  std::optional<std::string> url(const std::optional<std::string>& epgID,
                                 std::string_view name) const;
  bool needsRefresh(time::Seconds now) const { return now - saved_.fetched > kMaxAge; }
  const Saved& saved() const { return saved_; }

  static std::string toJson(const Saved& saved);
  static std::optional<Saved> fromJson(std::string_view text);

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

// Logos for a guide row. National channels whose logo is curated show the
// logo alone; locals show a logo with the call sign and "FOX 2"; anything
// else shows a logo (when one loads) next to its name. `core` / `callsign`
// come from the catalog; `provider` is the logo the provider gave (empty for
// none). `iptvOrg` may be null.
LogoSpec spec(std::string_view name, const std::optional<std::string>& core, bool isWest,
              bool isLocal, const std::optional<std::string>& callsign,
              std::optional<int> virtualNumber, const std::string& provider,
              const std::optional<std::string>& epgID,
              std::optional<catalog::LineupCategory> category, const IptvOrgIndex* iptvOrg);

// A small CSV reader (quoted fields, doubled quotes, CRLF).
std::vector<std::vector<std::string>> csvRows(std::string_view text);

}  // namespace marigold::logos
