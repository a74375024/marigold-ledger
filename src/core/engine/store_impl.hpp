// The store's private state, shared by store.cpp (channels and guide) and
// store_events.cpp (Live Events). UI thread only.
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/engine/store.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/lineup.hpp"
#include "core/catalog/markets.hpp"
#include "core/catalog/types.hpp"
#include "core/catalog/work.hpp"
#include "core/guide/outside.hpp"
#include "core/guide/provider.hpp"
#include "core/logos/logos.hpp"
#include "core/model/channel.hpp"
#include "core/sources/account.hpp"
#include "core/sources/emby.hpp"
#include "core/sources/m3u.hpp"
#include "core/sports/board.hpp"
#include "core/sports/cameras.hpp"
#include "core/sports/player_photos.hpp"
#include "core/sports/race_data.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/reminders.hpp"
#include "core/sports/schedule.hpp"
#include "core/sports/scores.hpp"
#include "core/sports/store.hpp"

namespace marigold::engine {

using catalog::CatalogResult;
using catalog::EntryInfo;

// Live Events state (store_events.cpp).
struct EventsState {
  sports::schedule::Schedule schedule;
  sports::scores::LiveScores scores;
  sports::MySportsPrefs mySports;
  sports::Reminders reminders;
  sports::store::SourceMemory memory;
  sports::SportFeedsSeen seen;
  std::optional<time::Seconds> seenCheckedAt;

  std::vector<sports::SportsEvent> events;  // last rebuild
  std::vector<sports::matcher::GuideListing> listings;
  std::optional<time::Seconds> listingsAt;
  int listingsRevision = 0;
  int scheduleRevision = 0;
  std::string lastFingerprint, lastSaved;
  bool rebuilding = false;
  uint32_t nextCheckMs = 0;
  bool restoredTried = false;

  // Requests in flight.
  bool scheduleBusy = false;
  uint32_t scheduleAt = 0;   // send the pending schedule request at
  std::optional<sports::schedule::Schedule::Pending> schedulePending;
  bool scoresBusy = false;
  bool scoresRound = false;
  uint32_t scoresAt = 0;     // next request or round at
  std::optional<sports::scores::LiveScores::Request> scoresPending;
  int statusesRevision = -1;

  sports::board::Board board;
  uint32_t boardBuiltMs = 0;
  bool boardDirty = true;

  // The game being watched and its source, remembered on leaving.
  std::string watchingEvent, watchingFeed;
  bool watchingSawPicture = false;
  uint32_t nextReminderMs = 0;
  bool rostersHeld = false;

  // Race weekends (drivers, results), tennis player photos and race
  // cameras (store_race.cpp).
  sports::race_data::RaceData race;
  sports::player_photos::PlayerPhotos photos;
  sports::cameras::CameraIndex cameraIndex;
  sports::cameras::FollowedDrivers followed;
  int followRevision = 0;
  bool raceBusy = false, raceWasRunning = false, photosBusy = false, nascarBusy = false;
  uint32_t raceAt = 0, photosAt = 0;
  std::optional<sports::race_data::RaceData::Request> racePending;
  std::optional<sports::player_photos::PlayerPhotos::Request> photoPending;
  int raceSeen = 0;  // the race data revision the board was built with
  std::string f1Key;
  std::vector<sports::EventSource> f1Sources;

  // Every team of a league for My Sports (one request per league, kept 12 h).
  std::map<std::string, std::vector<sports::SportsTeam>> leagueTeams;
  std::map<std::string, time::Seconds> leagueTeamsAt;
  std::set<std::string> leagueTeamsBusy;
  int teamsRevision = 0;
};

struct Store::Impl {
  EventsState ev;
  int catalogRevision = 0, lineupRevision = 0;
  enum class GuideState : uint8_t { None, Loading, Loaded, Failed };

  struct AccountRun {
    std::string state = "Not loaded yet";
    std::vector<std::string> formats;  // Xtream allowed_output_formats
  };

  // Persisted.
  std::vector<sources::Account> accounts;
  std::set<std::string> favorites;   // entry ids (Xtream and M3U have no favorites API)
  std::vector<std::string> recents;  // entry ids, newest first
  std::map<std::string, std::string> pinned;  // entry id -> raw channel id
  // Entry id -> the version that last played on it: it starts first next
  // time unless one is pinned.
  std::map<std::string, std::string> lastFeed;
  std::map<std::string, std::vector<int>> reliability;  // source tag -> {worked, failed}
  // Raw channel id -> when this TV last couldn't play it (playback
  // cannotPlayHere): tried after the channel's other versions for a while.
  std::map<std::string, time::Seconds> unplayable;
  // Raw channel id -> when it last failed to play (this run only): a
  // direct version that failed lately lets a server one that played lead.
  std::map<std::string, time::Seconds> failedAt;
  std::set<std::string> quickStartRefused;  // server ids (Settings::embyQuickStart)
  std::set<std::string> hiddenGroups;  // provider groups left out of All Channels
  catalog::LineupPrefs prefs;

  std::map<std::string, AccountRun> runs;  // by source id ("xt:..")
  // M3U links and headers by raw channel id. Secret: memory and a 0600 file.
  std::unordered_map<std::string, sources::m3u::StreamInfo> m3uStreams;

  // Catalog.
  std::shared_ptr<const std::vector<Channel>> channels = std::make_shared<std::vector<Channel>>();
  std::unordered_map<std::string, size_t> indexByID;
  std::shared_ptr<const CatalogResult> catalog = std::make_shared<CatalogResult>();
  std::unordered_map<std::string, size_t> entryInfo;  // entry id -> catalog->entries index
  catalog::LineupOutput lineup;
  bool partial = false;
  bool admissionCut = false;
  // The last downloaded list (before admission), and whether what admission
  // keeps has changed since (countries), so the same list must be sorted again.
  uint64_t listFingerprint = 0;
  bool admissionChanged = false;  // the last list was over the cap: groups were left out
  time::Seconds channelsLoadedAt = 0;
  bool loading = false;
  // A full sort under way: how far it has got, and the downloaded names
  // the area suggestion reads before the sorted list is in.
  std::shared_ptr<catalog::BuildProgress> sortProgress;
  std::shared_ptr<const std::vector<std::string>> receivedNames;
  // The guide header's "Channel list updated" / "Couldn't update" line, until
  // this tick time (ms).
  uint32_t updatedUntil = 0, failedUntil = 0, lastTick = 0;
  bool loadUpdated = false, loadFailed = false;  // set off the tick; timed at the next
  std::string updatedLine;  // "Channel list updated", "Channels added"
  // Providers added or removed while a list is on screen: it stays until
  // the new one is sorted; the header line says what's going on.
  bool accountsChanged = false;
  std::string accountChange, accountChangeDone;
  uint64_t generation = 1;   // bumps when accounts change
  uint64_t rebuildGeneration = 0;
  std::string signature;
  uint32_t lineupRefreshAt = 0;

  // Guide.
  std::unordered_map<std::string, std::vector<Program>> serverPrograms;  // short EPG / Emby
  // Shared lists: copies of these maps copy pointers (see guide::share).
  guide::SharedPrograms providerMatches;
  guide::SharedPrograms outsideMatches;
  catalog::ProgramLists programs;  // raw and entry ids
  std::unordered_map<std::string, GuideState> guideState;
  std::unordered_map<std::string, time::Seconds> guideWindowByChannel;
  std::unordered_map<std::string, std::string> entryGuideKeys;
  std::vector<std::string> pendingGuide;
  std::unordered_set<std::string> pendingGuideSet;
  std::vector<std::string> recentlyRequested;
  int activeGuideFetches = 0;
  uint32_t guideFlushAt = 0;
  time::Seconds guideWindowStart = 0;
  uint64_t listingsGeneration = 0;
  std::unordered_set<std::string> touchedListings;

  bool providerRunning = false, outsideRunning = false;
  uint32_t providerAt = 0, outsideAt = 0;  // scheduled start (ms), 0 = none
  bool providerForce = false, outsideForce = false;
  std::map<std::string, guide::provider::Saved> providerSaved;  // by cache key
  std::map<std::string, std::string> providerProblems;          // by cache key, last failure
  std::optional<guide::Saved> outsideSaved;
  bool savedListingsRead = false;
  std::string guideLine;  // status line of a running guide job
  std::vector<catalog::markets::Market> tvMarkets;  // last list loaded for the setup
  // Station directory (Wikipedia's affiliate lists, built into the app).
  std::shared_ptr<const std::vector<guide::Station>> stations = std::make_shared<std::vector<guide::Station>>();
  // Fallback logos for national channels (weekly).
  std::shared_ptr<const logos::IptvOrgIndex> iptvOrg;

  // Shared with workers.
  std::shared_ptr<std::atomic<bool>> playingFull = std::make_shared<std::atomic<bool>>(false);
  // The provider guide build is waiting for playback to stop (status line).
  std::shared_ptr<std::atomic<bool>> providerWaiting = std::make_shared<std::atomic<bool>>(false);
  // Foreground jobs running now (loading the saved lists, the channel sort):
  // guide builds wait for them, so the memory peaks never add up.
  std::shared_ptr<std::atomic<int>> foregroundJobs = std::make_shared<std::atomic<int>>(0);
  std::shared_ptr<std::atomic<bool>> stopJobs = std::make_shared<std::atomic<bool>>(false);

  uint64_t publishVersion = 0;
  bool channelsDirty = false, listingsDirty = false;
  // A first list is sorted but its rows aren't on screen yet (the lineup
  // works them out on a worker): Ready only once they're published, so the
  // guide never shows "No channels" in between. `rowsBuilt` once the latest
  // lineup is in; `saveOnRows` saves the list then (its first-screen file is
  // those rows).
  bool readyOnPublish = false, rowsBuilt = false, saveOnRows = false;
  // The saved channel list is behind the one on screen (only event names
  // changed since), and when it was last written.
  // The list saved on this TV is being opened (a relaunch, not a download).
  bool openingSaved = false;
  bool catalogUnsaved = false;
  time::Seconds catalogSavedAt = 0;
  // What was last written, so an unchanged file isn't written again.
  std::string savedAccounts, savedSecrets;
  uint64_t savedM3uPrint = 0;
  // A big job finished: memory goes back to the system when idle (ms; 0 =
  // nothing due), noted in the playback log as `releaseWhat`.
  // Live Events was last open then (the channel list re-read interval).
  time::Seconds eventsUsedAt = 0;
  uint32_t releaseAfter = 0;
  bool releasing = false;
  std::string releaseWhat;
  std::unordered_set<std::string> dirtyEntries;  // entry ids whose listings changed
  bool allListingsDirty = false;
};

struct OpenStream {
  std::shared_ptr<sources::emby::Client> client;
  sources::emby::Stream stream;
};

struct Fetched {
  std::vector<Channel> channels;
  std::unordered_map<std::string, sources::m3u::StreamInfo> streams;
  std::vector<std::string> formats;
  std::optional<std::string> guideUrl;  // M3U url-tvg
  std::optional<sources::Account> updated;
  std::string problem;
  bool done = false;
};

}  // namespace marigold::engine
