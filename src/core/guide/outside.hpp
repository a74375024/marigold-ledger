// Free TV listings from outside the user's server (OutsideGuide in tvOS).
// A background job downloads free XMLTV guides, streams through them keeping
// only the channels in the user's lineup, merges the same channel across
// sources (filling gaps from lower-priority ones) and caches the small
// result. It pauses during playback and runs about once a day.
//
// Here the job is `build`, with the downloads and file reads done by the
// caller through BuildIO (the platform layer inflates and streams files into
// an XmltvReader); everything else is the same logic.
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

#include "core/guide/names.hpp"
#include "core/model/channel.hpp"
#include "core/util/json.hpp"
#include "core/util/time.hpp"

namespace marigold::guide {

// MARK: Sources

struct Shard {
  std::string id;
  std::string title;
  std::string detail;
  bool defaultOn = false;
};

struct Source {
  std::string name;
  std::string shard;
  std::string url;
  // A locals file: bare four-letter call signs count as call signs.
  bool locals = false;
  // Only used when a main source for its listings is stale or short (it
  // fills hours the main sources don't have).
  bool backup = false;
  // Also used when these shards are on.
  std::set<std::string> alsoShards;
  // Its times are written this many minutes late (epg.pw writes China time
  // as UTC): the starting guess until one is learned.
  int assumedShift = 0;
  // Learn its clock shift from listings other sources already have.
  bool calibrate = false;
  // Channels named by network and city ("5.1 CBS San Francisco"), resolved
  // to a station through the station directory.
  bool stationNames = false;

  // File name in the cache and key of its saved state.
  std::string id() const;
  bool appliesTo(const std::set<std::string>& shards) const;
};

const std::vector<Shard>& shards();
// In priority order: when two sources list the same channel, the first one's
// listings win and later ones only fill gaps.
const std::vector<Source>& sources();
std::set<std::string> defaultShards();
// The station directory is only needed for these shards.
bool needsStationDirectory(const std::set<std::string>& shards);

// Rebuild when the saved listings are older than this. The sources cover
// several days ahead, so a day-old build still fills the guide.
constexpr time::Seconds kRefreshAge = 20 * 3600;
// How far ahead listings are kept (hours).
constexpr int kKeepHours = 48;
// Main sources older than this, or reaching less than kShortHorizon minutes
// ahead, are stale: backups fill in and the update is retried soon.
constexpr time::Seconds kStaleAge = 36 * 3600;
constexpr int kShortHorizon = 12 * 60;
constexpr time::Seconds kStaleRetry = 2 * 3600;
// Sports descriptions kept for the whole window (2): older builds kept only
// the next few hours', so they're rebuilt once.
constexpr int kDetailVersion = 2;

// MARK: Data

// One programme, compact. Times in minutes since 1970.
struct RawProgram {
  int s = 0;                      // start
  int m = 0;                      // length in minutes
  std::string t;                  // title
  std::optional<std::string> st;  // subtitle / episode title
  std::optional<std::string> d;   // description
  int f = 0;                      // flags: 1 movie, 2 sports, 4 news, 8 kids, 16 live, 32 new

  int end() const { return s + m; }
};

// User channel id -> its programmes.
using ProgramsById = std::map<std::string, std::vector<RawProgram>>;

// A user channel a guide channel stands for. `shift`: minutes added to its
// times (a West feed made from the East one: +180).
struct Match {
  std::string id;
  std::string how;
  int shift = 0;
};

// What happened with one source in the last update.
struct SourceState {
  std::string id;
  std::string name;
  bool backup = false;
  std::optional<std::string> etag;
  std::optional<time::Seconds> lastModified;
  std::optional<time::Seconds> checkedAt;
  std::optional<time::Seconds> downloadedAt;
  // 200 downloaded, 304 unchanged, 0 saved copy (download failed).
  std::optional<int> status;
  std::optional<int64_t> bytes;
  std::optional<std::string> problem;
  std::optional<int> guideChannels;
  std::optional<int> matched;
  // Half of the matched channels' listings end before this (minutes since 1970).
  std::optional<int> horizonMedian;
  std::optional<double> parseSeconds;
  // Minutes subtracted from its times (backups), and how that was decided.
  std::optional<int> shift;
  std::optional<std::string> shiftNote;
  std::optional<bool> stale;
  // A backup used in the last update, and how many channels have listings
  // from it.
  std::optional<bool> used;
  std::optional<int> extended;

  // One line for the Guide Listings screen. Dates as "Wed Oct 21, 3:05 PM"
  // in `zone` (the device's, like DateFormatter's default).
  std::string line(const time::Zone& zone = time::Zone::device()) const;
  bool operator==(const SourceState& o) const;
};

// The saved result: listings for the user's channels only.
struct Saved {
  time::Seconds built = 0;
  std::vector<std::string> shards;
  int channelCount = 0;
  ProgramsById programs;
  // How each channel was matched ("US locals: WNBC-DT (by call sign WNBC)").
  std::optional<std::map<std::string, std::string>> notes;
  // Each source's state from the last update (by Source::id).
  std::optional<std::map<std::string, SourceState>> sources;
  // When to look again: soon while a source is stale, else a day later.
  std::optional<time::Seconds> nextCheck;
  // The channel list and shards these listings were matched for.
  std::optional<std::string> targetsKey;
  std::optional<int> detailVersion;
};

// JSON. Dates are Unix seconds (Swift's JSONEncoder wrote its own reference
// date; these files are never shared with the tvOS app).
json::Value toJson(const RawProgram& p);
std::optional<RawProgram> rawProgramFromJson(const json::Value& v);
json::Value toJson(const ProgramsById& programs);
ProgramsById programsFromJson(const json::Value& v);
json::Value toJson(const SourceState& s);
std::optional<SourceState> sourceStateFromJson(const json::Value& v);
std::string toJson(const Saved& saved);
std::optional<Saved> savedFromJson(std::string_view text);
// "listings-<signature, alphanumerics only, 80 max>.json"
std::string savedFileName(std::string_view signature);

bool needsRebuild(const Saved* saved, const std::set<std::string>& shards, int channelCount,
                  time::Seconds now);

// MARK: Matching

// The user's channels as lookup keys: call signs (from the name, or from
// network + city via the station directory) and normalized names.
struct Targets {
  std::unordered_map<std::string, std::vector<std::string>> bySign;
  std::unordered_map<std::string, std::vector<std::string>> byName;
  StationsByNetwork stationsByNetwork;

  Targets() = default;
  Targets(const std::vector<Channel>& channels, const std::vector<Station>& stations);

  // The user's channels a guide channel stands for. Call signs win over
  // names, since a call sign can't be ambiguous. `stationNames`: a
  // network-and-city name ("5.1 CBS San Francisco") is looked up in the
  // station directory, and its channel number must agree. `deriveWest`: an
  // East feed also stands for the user's West feed, 3 hours later.
  std::vector<Match> matches(const std::vector<std::string>& names, std::string_view xmltvID,
                             bool locals, bool stationNames = false, bool deriveWest = false) const;
};

// MARK: Merging

struct LearnedShift {
  std::optional<int> shift;  // minutes
  std::map<std::string, int> perChannel;
  std::string note;
};

// The whole-hour clock shift (minutes, subtracted) that lines a source's
// titles up with listings already merged. Only titles that occur once on
// both sides of a channel count (a show that repeats would match at many
// offsets), and each channel casts one vote. A channel with a clear vote of
// its own keeps it.
LearnedShift learnShift(const ProgramsById& reference, const ProgramsById& candidate,
                        int sample = 800);

// Adds `extra` programmes that don't overlap `base` by more than 5 minutes.
std::vector<RawProgram> mergePrograms(const std::vector<RawProgram>& base,
                                      const std::vector<RawProgram>& extra);

// Programmes for one channel inside [from, to). `idPrefix` "pg" marks the
// provider's own guide (which counts as the server's, not outside).
std::vector<Program> programs(const std::vector<RawProgram>& raw, const std::string& channelID,
                              time::Seconds from, time::Seconds to,
                              std::string_view idPrefix = "og", bool outside = true);

// MARK: Building

struct Window {
  int low = 0;  // minutes since 1970
  int high = 0;
};

// One source's matches from a file (XMLTVReader.SourceResult).
struct SourceResult {
  struct Entry {
    std::vector<RawProgram> programs;
    std::string note;
  };
  std::map<std::string, Entry> programs;  // user channel id
  int guideChannels = 0;
  int programmes = 0;
  // When half of the matched channels' listings have ended (minutes since
  // 1970): how far ahead this source really reaches.
  std::optional<int> horizonMedian;
  double seconds = 0;
};

// A guide file kept between updates, with what the server said about its
// version (GuideFiles.Fetched). `file` is the caller's handle for it.
struct Fetched {
  std::string file;
  int status = 200;  // 200 downloaded, 304 not changed, 0 the saved copy after a failure
  std::optional<std::string> etag;
  std::optional<time::Seconds> lastModified;
  int64_t bytes = 0;
  std::optional<std::string> problem;
  bool changed() const { return status == 200; }
};

struct BuildIO {
  struct FetchOutcome {
    std::optional<Fetched> fetched;
    std::string error;  // when fetched is empty
  };
  struct ReadOutcome {
    std::optional<SourceResult> result;
    std::string error;
  };
  // Download `source` into the cache, asking the server to skip it when
  // unchanged. `waitForPlayback`: big files wait while video plays.
  std::function<FetchOutcome(const Source& source, const std::optional<std::string>& etag,
                             const std::optional<time::Seconds>& lastModified,
                             bool waitForPlayback)>
      fetch;
  // Stream the (inflated) file through XmltvReader::forSource(source,
  // targets, window) and return its sourceResult().
  std::function<ReadOutcome(const Source& source, const std::string& file,
                            const std::shared_ptr<const Targets>& targets, Window window)>
      read;
  std::function<void(const std::string&)> progress;
  std::function<bool()> cancelled;
  // Seconds since the build started, for the log (0 when not given).
  std::function<double()> elapsed;
};

struct BuildResult {
  ProgramsById programs;
  std::vector<std::string> problems;
  std::map<std::string, std::string> notes;
  std::map<std::string, SourceState> states;
  time::Seconds nextCheck = 0;
  std::string targetsKey;
  // Lines for the log.
  std::vector<std::string> log;
};

// Builds listings for `channels` from the enabled sources. Cheap when nothing
// changed: each file is kept, and the server is asked to skip it when
// unchanged. When every main file is unchanged and the channels are the
// same, the saved listings are reused as they are. `stations` is the station
// directory (needed when needsStationDirectory(shards)).
BuildResult build(const std::vector<Channel>& channels, const std::set<std::string>& shards,
                  const Saved* previous, const std::vector<Station>& stations, time::Seconds now,
                  const BuildIO& io);

// "<count>|<hash>|<shards>": the channel list and shards listings were matched for.
std::string targetsKey(const std::vector<Channel>& channels, const std::set<std::string>& shards);

}  // namespace marigold::guide
