// The full guide an Xtream provider publishes for its whole lineup
// (`xmltv.php`), ProviderGuide in tvOS. A background job downloads the file,
// streams through it keeping only the user's in-scope channels (matched by
// their exact guide id), and caches the small result. Rebuilt about twice a
// day. The download and the streaming are the caller's (through
// ReadExactFn); the rules are here.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/guide/outside.hpp"
#include "core/model/channel.hpp"
#include "core/util/time.hpp"

namespace marigold::guide::provider {

// Rebuild when the saved guide is older than this.
constexpr time::Seconds kRefreshAge = 12 * 3600;
// How far ahead listings are kept (hours).
constexpr int kKeepHours = 48;
// Descriptions are kept for programmes starting up to this many hours after
// the guide is read: until the next read and 6 h for a late one (see
// guide::kDescHours for what they cost).
constexpr int kDescHours = static_cast<int>(kRefreshAge / 3600) + 6;

struct Saved {
  time::Seconds built = 0;
  // In-scope channels when this was built (a big jump means rebuild).
  int channelCount = 0;
  SharedPrograms programs;
  std::map<std::string, std::string> notes;
  // Channels and programmes in the provider's whole file.
  int guideChannels = 0;
  int guideProgrammes = 0;
  int64_t bytes = 0;
};

std::string toJson(const Saved& saved);
std::optional<Saved> savedFromJson(std::string_view text);
// The same, a piece at a time.
bool writeSaved(const Saved& saved, const json::Writer::Sink& sink);
std::optional<Saved> readSaved(std::istream& in);

struct Status {
  enum class Kind { updating, ready, unavailable, failed };
  Kind kind = Kind::updating;
  std::string message;  // updating / unavailable / failed
  // ready:
  int matched = 0;
  int total = 0;
  int guideChannels = 0;
  time::Seconds updated = 0;

  bool operator==(const Status& o) const {
    return kind == o.kind && message == o.message && matched == o.matched && total == o.total &&
           guideChannels == o.guideChannels && updated == o.updated;
  }
};

struct State {
  std::string name;
  Status status;
  bool operator==(const State& o) const { return name == o.name && status == o.status; }
};

struct Outcome {
  enum class Kind { built, unavailable, failed };
  Kind kind = Kind::failed;
  Saved saved;          // built
  std::string message;  // unavailable / failed
};

// "US| NBC ᴴᴰ" -> "US". Empty when the group has no country prefix.
std::optional<std::string> countryPrefix(const std::optional<std::string>& group);

// Whether a channel's listings come from the provider's guide: it has a
// guide id and is from a selected country (`countries`, which tvOS kept in a
// static; groups without a country prefix always take part).
bool inScope(const Channel& channel, const std::set<std::string>& countries);
// Build 132: also a channel of another country that `forEvents` keeps for
// Live Events (a sports channel: sports/brands.hpp). Asked only of channels
// with a guide id whose country isn't picked. The provider's whole file is
// read either way: this keeps a few more channels' listings, downloads
// nothing more.
bool inScope(const Channel& channel, const std::set<std::string>& countries,
             const std::function<bool(const Channel&)>& forEvents);

std::string cacheKey(std::string_view profile, std::string_view sourceID);
// "provider-<key>.json"
std::string savedFileName(std::string_view key);
bool needsRebuild(const Saved* saved, int channelCount, time::Seconds now);

// Lower-cased guide id -> the user's channel ids with it.
std::unordered_map<std::string, std::vector<std::string>> guideIds(
    const std::vector<Channel>& channels);

// What the caller's read of the provider's file gives back.
struct ExactRead {
  std::map<std::string, SourceResult::Entry> programs;  // XmltvReader::result()
  int guideChannels = 0;
  int programmes = 0;
  int64_t bytes = 0;  // size of the downloaded (compressed) file
};
struct ReadFailure {
  int httpStatus = 0;  // the server's answer, 0 for other failures
  std::string message;
};
// Downloads the guide and streams it through XmltvReader::exact(ids, window).
// Returns the read, or fills `failure`.
using ReadExactFn = std::function<std::optional<ExactRead>(
    const std::unordered_map<std::string, std::vector<std::string>>& ids, Window window,
    ReadFailure& failure)>;

// Build 132: a channel kept for Live Events only keeps what Live Events
// reads of it: the next 14 hours of a guide up to kRefreshAge old.
constexpr int kEventsOnlyHours = static_cast<int>(kRefreshAge / 3600) + 14;

// Reads the provider's guide for `channels` (already in scope). Those in
// `eventsOnly` (channel ids; null: none) keep kEventsOnlyHours ahead.
Outcome build(const std::vector<Channel>& channels, time::Seconds now, const ReadExactFn& read,
              const std::function<void(const std::string&)>& progress = nullptr,
              const std::unordered_set<std::string>* eventsOnly = nullptr);

}  // namespace marigold::guide::provider
