// The channel report (LiveTVStore "Channel report" in tvOS): a spreadsheet
// (CSV) of every Live TV channel, for diagnosing guide matching on another
// device. One row per raw channel: what it is (number, name, group, tags,
// call sign, kind, market...), what the server's guide has, which outside
// listing matched and how, what the provider's full guide says, and what is
// on now.
//
// No stream links, logins, tokens or server addresses: the inputs carry none
// (channel ids and guide ids never hold a login), and every cell still goes
// through redact(); `privateWords` (the caller's server hosts and user
// names) are blanked as well.
//
// Pure: the store gathers the inputs (the per-channel guide check first, see
// guideCheckIds) and calls channelReportCSV, on a worker if it likes. Every
// pointer in ChannelReportInput may be null (= empty) and is read only
// during the call, so the caller keeps the data alive and unchanged until it
// returns (publish snapshots or copies for a worker).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/catalog/work.hpp"
#include "core/guide/names.hpp"
#include "core/guide/outside.hpp"
#include "core/guide/provider.hpp"
#include "core/model/channel.hpp"
#include "core/util/time.hpp"

namespace marigold::report {

// MARK: Checking guides first

// checkAllGuides: at most this many per-channel guides are asked for, for
// up to kCheckTimeout seconds, polled every kCheckPollMs. Asking a big
// provider for tens of thousands of per-channel guides takes ages and
// hammers it.
constexpr int kCheckLimit = 2500;
constexpr time::Seconds kCheckTimeout = 120;
constexpr int kCheckPollMs = 700;

// The channels whose server / short guide to fetch before writing the
// report (checkAllGuides): each entry's main version (its first member, or
// the id itself when it is a raw channel), lineup entries first, then the
// rest; skipping event feeds (a family key) and channels the provider's full
// guide already covers in the window, each once, at most `limit`.
// `entryIds`: lineupEntries then browseEntries ids, in order.
// `isRawChannel(id)`: a raw channel id (indexByID). `providerCovers(id)`:
// the provider guide has a programme for it in the next guideHours.
std::vector<std::string> guideCheckIds(const std::vector<std::string>& entryIds,
                                       const catalog::CatalogResult& catalog,
                                       const std::function<bool(const std::string&)>& isRawChannel,
                                       const std::function<bool(const std::string&)>& providerCovers,
                                       int limit = kCheckLimit);

// Whether `raw` has a programme in [windowStart, windowStart + guideHours)
// (providerCovers in tvOS).
bool covers(const std::vector<guide::RawProgram>& raw, time::Seconds windowStart,
            double guideHours = catalog::kGuideHours);

// MARK: The report

// guideState of a channel's server / short guide.
enum class ServerGuideState : uint8_t { NotChecked, Loading, Loaded, Failed };

struct ChannelReportInput {
  // Raw channels, in the order rows are written.
  const std::vector<Channel>* channels = nullptr;
  // Traits, entries and entryOf.
  const catalog::CatalogResult* catalog = nullptr;
  // My Lineup's entry ids.
  const std::set<std::string>* lineupIds = nullptr;
  // Groups the viewer hid; a channel without a group is in `otherGroup`.
  const std::set<std::string>* hiddenGroups = nullptr;
  std::string otherGroup = "Other";

  // Server / short-guide state and programmes by raw channel id.
  const std::unordered_map<std::string, ServerGuideState>* guideState = nullptr;
  const std::unordered_map<std::string, std::vector<Program>>* serverPrograms = nullptr;
  // The merged listing shown in the guide, by raw channel id ("now").
  const std::unordered_map<std::string, std::shared_ptr<const std::vector<Program>>>* programs = nullptr;

  // Outside listings: matches and how each channel was matched.
  const guide::SharedPrograms* outsideMatches = nullptr;
  const std::map<std::string, std::string>* outsideNotes = nullptr;
  // The provider's full guide (Xtream xmltv.php): matches, notes, each
  // account's state (by server id "xt:...") and the countries in scope.
  const guide::SharedPrograms* providerMatches = nullptr;
  const std::map<std::string, std::string>* providerNotes = nullptr;
  const std::map<std::string, guide::provider::Status>* providerStates = nullptr;
  const std::set<std::string>* providerCountries = nullptr;
  // Account display names by server id.
  const std::map<std::string, std::string>* providerNames = nullptr;
  // Each entry's logo: where its logos come from ("logo set, provider") and
  // whether one is on screen ("shown", "failed"...), by entry id.
  const std::unordered_map<std::string, std::pair<std::string, std::string>>* logos = nullptr;
  // The station directory (tvOS read it from the outside guide's cache).
  const guide::StationsByNetwork* stations = nullptr;

  time::Seconds now = 0;
  // The guide grid's start (catalog::windowStart(now)).
  time::Seconds windowStart = 0;
  double guideHours = catalog::kGuideHours;
  // outside_listings_end is written in this zone (DateFormatter's default,
  // the device's).
  time::Zone zone = time::Zone::device();
  // Server hosts, user names: removed from every cell (shorter than 4
  // characters are ignored, they would blank ordinary words).
  std::vector<std::string> privateWords;
};

// The CSV header (one line, no newline).
const std::vector<std::string>& channelReportColumns();

// The report: header, one row per channel, "\n" after every line.
// `progress(done, total)` is called every 2000 rows (from the calling
// thread).
std::string channelReportCSV(const ChannelReportInput& input,
                             const std::function<void(int, int)>& progress = nullptr);

}  // namespace marigold::report
