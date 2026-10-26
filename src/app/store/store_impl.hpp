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

#include "app/store/store.hpp"
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

namespace marigold::app {

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
  int raceSeen = 0, photosSeen = 0;  // revisions the board was built with
  std::string f1Key;
  std::vector<sports::EventSource> f1Sources;
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
  std::map<std::string, std::vector<int>> reliability;  // source tag -> {worked, failed}
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
  time::Seconds channelsLoadedAt = 0;
  bool loading = false;
  uint64_t generation = 1;   // bumps when accounts change
  uint64_t rebuildGeneration = 0;
  std::string signature;
  uint32_t lineupRefreshAt = 0;

  // Guide.
  std::unordered_map<std::string, std::vector<Program>> serverPrograms;  // short EPG / Emby
  guide::ProgramsById providerMatches;
  guide::ProgramsById outsideMatches;
  std::unordered_map<std::string, std::vector<Program>> programs;  // raw and entry ids
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
  // Station directory (Wikipedia affiliate lists, about monthly).
  std::shared_ptr<const std::vector<guide::Station>> stations = std::make_shared<std::vector<guide::Station>>();
  bool stationsFresh = false, stationsFetching = false;
  time::Seconds stationsTriedAt = 0;
  // Fallback logos for national channels (weekly).
  std::shared_ptr<const logos::IptvOrgIndex> iptvOrg;
  bool iptvOrgFetching = false;
  time::Seconds iptvOrgTriedAt = 0;

  // Shared with workers.
  std::shared_ptr<std::atomic<bool>> playingFull = std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<std::atomic<bool>> stopJobs = std::make_shared<std::atomic<bool>>(false);

  uint64_t publishVersion = 0;
  bool channelsDirty = false, listingsDirty = false;
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

}  // namespace marigold::app
