// The pure parts of LiveTVStore's Live Events sections ("Sports events",
// "Saved result" and "Last source per game" in LiveTVStore.swift): reading
// game listings out of the guide, turning provider events into matcher
// input, the rebuild (schedule + catalog events -> SportsEvents), the
// "feeds moved on" end signal, event backups for a channel, every source
// that can play an event in automatic backup order, the saved result shown
// at launch, and the source remembered per game for 6 hours.
//
// LiveTVStore itself, playback and UI state are not here: what Swift reads
// from the store comes in as plain values or small callbacks.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/guide/outside.hpp"
#include "core/model/channel.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/matcher.hpp"
#include "core/sports/network_map.hpp"
#include "core/sports/reminders.hpp"
#include "core/sports/types.hpp"

namespace marigold::sports::store {

// MARK: Guide listings

// Game listings are read from the guide this far back and ahead.
constexpr Seconds kListingsBefore = 5 * 3600;
constexpr Seconds kListingsAhead = 48 * 3600;
// Live Events opened: listings older than this are read again...
constexpr Seconds kListingsStale = 600;
// ...or older than this when the guide changed since.
constexpr Seconds kListingsMoved = 60;

// A channel whose guide may show games: sports, local and Spanish channels
// in My Lineup or the browse list, and sports channels of unknown country.
struct ListingChannel {
  std::string id;
  bool inLineup = false;
  bool spanish = false;
  int rank = station_rank::elsewhere;
};
// Which channels to read, with their rank (station_rank) and language.
std::vector<ListingChannel> listingChannels(const std::set<std::string>& lineup,
                                            const std::vector<std::string>& browseIDs,
                                            const std::unordered_map<std::string, catalog::EntryInfo>& entryInfo,
                                            const StationMarkets& markets);

// One programme as a game listing: a title (with its episode title) that
// names two sides, or a plain sports title ("NBA Basketball") whose
// description names the venue (or maybe the teams). nullopt otherwise.
std::optional<matcher::GuideListing> listingFor(const ListingChannel& channel, const std::string& title,
                                                const std::optional<std::string>& subtitle,
                                                const std::optional<std::string>& overview, bool sports,
                                                Seconds start, Seconds end);

// A channel's guide programmes, plus the saved listings past what the guide
// holds (outside and provider listings, minutes since the epoch). The
// pointers are read during the call only; null means none.
struct ListingInput {
  ListingChannel channel;
  const std::vector<Program>* programs = nullptr;
  const std::vector<guide::RawProgram>* outside = nullptr;
  const std::vector<guide::RawProgram>* provider = nullptr;
};
struct ExtractedListings {
  std::vector<matcher::GuideListing> listings;  // by start, channel, title
  int ahead = 0;  // from saved listings past the guide
};
ExtractedListings extractListings(const std::vector<ListingInput>& channels, Seconds now);

// Live Events opened: whether the guide's game listings should be read
// again first (over 10 minutes old, or the guide moved on over a minute ago).
bool listingsNeedRead(std::optional<Seconds> extractedAt, int programsRevision, int listingsProgramsRevision,
                      Seconds now);
// The guide changed: listings are read after a quiet minute, or at most
// every 10 minutes while it keeps changing. Checked every 15 s; returns
// "quiet" or "deadline" when they're due.
constexpr int kListingsCheckSeconds = 15;
std::optional<std::string> listingsDue(Seconds changedAt, Seconds pendingSince,
                                       std::optional<Seconds> extractedAt, Seconds now);

// MARK: Rebuild

// Provider events as matcher input: Spanish when the provider marks it so
// or the title says so; `guessed` from the event, else from its feed's raw
// slot text (lists saved by older builds).
std::vector<matcher::ProviderEvent> providerEvents(
    const std::vector<catalog::LiveEvent>& events,
    const std::unordered_map<std::string, catalog::ChannelTraits>& traits, Seconds now);

// Teams of the leagues being looked up (logos by name, team channels).
std::vector<matcher::RosterLeague> rosterLeagues(const std::vector<League>& active,
                                                 const std::map<std::string, std::vector<SportsTeam>>& rosters);

// When the channel index must be rebuilt: the channel list, lineup, home and
// other markets, hidden groups or rosters changed.
std::string channelIndexKey(int entriesRevision, const std::set<std::string>& lineup,
                            const StationMarkets& markets, const std::set<std::string>& hidden,
                            const std::vector<matcher::RosterLeague>& rosters);

// Rebuilds run only when an input changes: provider events, the schedule,
// the lineup, the sports listings, a 30-minute time bucket, hidden groups,
// the Spanish preference, the schedule switch or the school list.
struct Fingerprint {
  int eventsRevision = 0;
  int scheduleRevision = 0;
  int entriesRevision = 0;
  int listingsRevision = 0;
  std::set<std::string> hidden;
  bool preferSpanish = false;
  bool enabled = true;
  size_t schools = 0;
};
std::string fingerprint(const Fingerprint& f, Seconds now);

struct RebuildInput {
  std::vector<SportsGame> games;
  std::vector<SportsTeam> schools;
  std::vector<matcher::RosterLeague> rosters;
  // The viewer picked US Spanish ("US-ES" in the lineup countries).
  bool preferSpanish = false;
  std::vector<catalog::LiveEvent> events;
  std::unordered_map<std::string, catalog::ChannelTraits> traits;
  std::vector<matcher::GuideListing> listings;
  // Last result: tile ids stay stable, and "feeds moved on" is read from it.
  std::vector<SportsEvent> previous;
  network_map::ChannelIndex channelIndex;
  // F1 sessions are also on the F1 channels (a later milestone; empty).
  std::vector<EventSource> f1Sources;
  Seconds now = 0;
  time::Zone zone = time::Zone::device();
  std::function<std::optional<std::string>()> shouldStop;
  std::function<bool(std::string_view)> isCameraName;
  // A raw provider channel still exists (renamed slots still exist).
  std::function<bool(const std::string&)> channelExists;
};
struct RebuildResult {
  std::vector<SportsEvent> events;
  matcher::Stats stats;
  network_map::SourceStats sources;
  // Leagues with a game on your channels (Schedule::noteWatchable).
  std::set<std::string> watchableLeagues;
  int endedByFeeds = 0;
};
// nullopt when shouldStop stopped it (keep the last result; retry in 5 min).
std::optional<RebuildResult> rebuild(const RebuildInput& input);
constexpr Seconds kAbortRetry = 300;

// A started game carried only by provider feeds is over once every one of
// those feeds has been renamed or cleared (they still exist, but now show
// something else). Games on guide or network channels keep the usual
// estimate, since those channels may still be running it.
std::vector<SportsEvent> markEndedByFeeds(const std::vector<SportsEvent>& built,
                                          const std::vector<SportsEvent>& previous, bool haveRawEvents,
                                          Seconds now, const std::function<bool(const std::string&)>& channelExists,
                                          int* ended = nullptr);

// Where the next two days' games can be watched, by kind of source (log).
std::string coverageSummary(const std::vector<SportsEvent>& events, const network_map::SourceStats& sources,
                            Seconds now);

// Launch pacing and coalescing of rebuild requests (scheduleSportsRebuild):
// while the first ~40 s of data pour in, rebuilds run at most every 6 s;
// your own changes skip the wait. Times are the caller's monotonic seconds.
class RebuildScheduler {
 public:
  static constexpr double kLaunchWindow = 40;
  static constexpr double kLaunchGap = 6;
  // "setting", "home city", "my sports", "hidden".
  static bool isUserReason(std::string_view reason);

  // When to run a rebuild asked for now (absolute time), or nullopt: a
  // sooner one is already due, or one is running (it runs again after).
  std::optional<double> request(const std::string& reason, double delay, double now);
  // The timer fired: the rebuild starts.
  std::string begin();
  // It finished: a request that came in meanwhile, to make now (delay 2 s).
  std::optional<std::string> end(double now);
  bool running() const { return running_; }

 private:
  std::optional<double> first_;
  std::optional<double> last_;
  std::optional<double> due_;
  std::string dueReason_;
  bool running_ = false;
  std::optional<std::string> again_;
};

// MARK: Sources of an event

// Channel id -> positions in the event list of games on it (not provider
// feeds), for event backups.
std::map<std::string, std::vector<size_t>> backupIndex(const std::vector<SportsEvent>& events);
// A game on now (or starting within 15 minutes) on the entry `id`: its
// provider feeds, as extra backups for the channel.
std::vector<std::string> eventBackupFeeds(const std::string& id, const std::vector<SportsEvent>& events,
                                          const std::map<std::string, std::vector<size_t>>& index, Seconds now);

// Source reliability (ok + 1) / (ok + fail + 2) of a channel's source tag;
// the playback module keeps the counts.
using Reliability = std::function<double(const std::string& channelID)>;
// Keeps the given order unless a source is clearly worse (by over 0.25).
std::vector<std::string> orderedByReliability(const std::vector<std::string>& ids, const Reliability& reliability);
// Provider feeds best first: quality (0 SD .. 3 UHD), then reliability.
std::vector<std::string> bestFeeds(const std::vector<std::string>& ids,
                                   const std::function<int(const std::string&)>& quality,
                                   const Reliability& reliability);
// An entry's own versions, best first, the pinned one first; a raw channel
// alone. `exists` filters members the channel list no longer has.
std::vector<std::string> baseVersions(const std::string& id, const catalog::EntryInfo* info,
                                      const std::optional<std::string>& pinned, const Reliability& reliability,
                                      const std::function<bool(const std::string&)>& exists);

// What the store resolves for the candidates.
struct ChannelRef {
  std::string id;
  std::string name;
};
struct Resolver {
  // sourceChannel: a provider feed's raw channel, or the entry (else raw
  // channel) a guide source names; nullopt when it's gone.
  std::function<std::optional<ChannelRef>(const EventSource&)> sourceChannel;
  // An entry's own versions (baseVersions above); a raw channel alone.
  std::function<std::vector<std::string>(const std::string& channelID)> baseVersions;
  // Provider feeds best first (bestFeeds above).
  std::function<std::vector<std::string>(const std::vector<std::string>& feedIDs)> bestFeeds;
  // An entry exists with this id (a guide channel picked: watch it).
  std::function<bool(const std::string&)> isEntry;
};

// Everything that can play an event, in order (the automatic backup order):
// the picked source (if any) first, then My Lineup / guide channels (each
// with its versions), then provider feeds, a team's own channel last.
// Raw channel ids, without repeats.
std::vector<std::string> eventCandidates(const SportsEvent& event, const std::optional<std::string>& preferring,
                                         const Resolver& resolver);

constexpr const char* kNoSourceMessage = "Nothing in your channels is showing this yet";

// watchSportsEvent's choice of what to tune.
struct WatchPlan {
  // Nothing can play it: show kNoSourceMessage.
  bool empty = false;
  // A guide channel was picked: watch that entry itself (the game's feeds
  // back it up through the channel's versions).
  std::optional<std::string> entry;
  std::vector<std::string> candidates;
  // "the source you were on" when the remembered source leads.
  std::optional<std::string> reason;
};
// Back to a game watched in the last few hours: start on the remembered
// source (when nothing was picked).
WatchPlan watchPlan(const SportsEvent& event, const std::optional<std::string>& preferring,
                    const std::optional<std::string>& remembered, const Resolver& resolver);

// What a reminder for an upcoming event points at: its best channel (a
// channel, else a feed), for the event's usual length.
std::optional<LiveReminder> reminderTarget(const SportsEvent& event, const Resolver& resolver);

// MARK: Saved result (shown instantly on the next launch)

struct SavedResult {
  int version = 1;
  Seconds savedAt = 0;
  std::vector<SportsEvent> events;
  // Provider feed id -> the matchup it was showing when saved.
  std::map<std::string, std::string> feedKeys;
  // The guide's game listings, so the next launch's first rebuild still
  // finds games on local stations.
  std::optional<std::vector<matcher::GuideListing>> listings;
};
std::string toJson(const SavedResult& saved);
std::optional<SavedResult> savedResultFromJson(std::string_view text);

// What to save after a rebuild: watchable games only. Never an empty or
// listing-less (weaker) result over a good one, and not the same one twice
// (`lastFingerprint` is updated). nullopt: nothing to write.
std::optional<SavedResult> resultToSave(const std::vector<SportsEvent>& events,
                                        const std::vector<catalog::LiveEvent>& rawEvents,
                                        const std::vector<matcher::GuideListing>& listings, bool listingsReady,
                                        Seconds now, std::string& lastFingerprint);

struct Restored {
  std::vector<SportsEvent> events;
  std::vector<matcher::GuideListing> listings;
  int ageMinutes = 0;
  int dropped = 0;  // stale provider feeds
};
// Last launch's games, checked: finished ones dropped; provider feeds must
// still show the same matchup (event slots get renamed through the day).
// Listings ending later and starting in the next 16 hours are kept.
std::optional<Restored> restore(const SavedResult& saved, const std::vector<catalog::LiveEvent>& rawEvents,
                                Seconds now);
// Only fills in when no game is shown yet: the restored games (sources that
// no longer resolve dropped, once the channel list has loaded) plus the
// current non-game tiles.
std::optional<std::vector<SportsEvent>> fillIn(const std::vector<SportsEvent>& restored,
                                               const std::vector<SportsEvent>& current, bool channelsLoaded,
                                               const std::function<bool(const EventSource&)>& resolvable);

// MARK: Last source per game

// The version on screen when you leave a game (picked, or a backup recovery
// moved to) is where it starts next time. Game -> "feed id|time", at most
// 40 games, each forgotten after 6 hours; written only when leaving or
// switching games.
class SourceMemory {
 public:
  static constexpr Seconds kLife = 6 * 3600;
  static constexpr size_t kCap = 40;

  SourceMemory() = default;
  explicit SourceMemory(std::map<std::string, std::string> stored) : map_(std::move(stored)) {}

  // The game id, else the tile id.
  static std::string key(const SportsEvent& event);
  std::optional<std::string> remembered(const SportsEvent& event, Seconds now) const;
  // True when the map changed (save it). The same source saved in the last
  // minute is not written again.
  bool remember(const SportsEvent& event, const std::string& feedID, Seconds now);
  const std::map<std::string, std::string>& stored() const { return map_; }

  std::string toJson() const;
  static std::map<std::string, std::string> fromJson(std::string_view text);

 private:
  std::map<std::string, std::string> map_;
};

// The feed to remember when leaving a game: what's playing once its
// picture came up, else the one just picked in Sources (if a candidate).
std::optional<std::string> feedToRemember(bool sawFirstFrame, const std::optional<std::string>& playing,
                                          const std::optional<std::string>& manualFeedID,
                                          const std::vector<std::string>& candidates);

}  // namespace marigold::sports::store
