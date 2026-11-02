// The store's private state, shared by store.cpp (channels and guide) and
// store_events.cpp (Live Events). UI thread only.
#pragma once

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/engine/store.hpp"
#include "core/engine/events_build.hpp"
#include "core/engine/search_build.hpp"
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
#include "core/sports/network_map.hpp"
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
  int mySportsRevision = 0;  // bumped when My Sports changes (Search's index)
  sports::Reminders reminders;
  sports::store::SourceMemory memory;
  sports::SportFeedsSeen seen;
  std::optional<time::Seconds> seenCheckedAt;

  // The last rebuild's (or the saved list's), shared with the workers.
  std::shared_ptr<const std::vector<sports::SportsEvent>> events =
      std::make_shared<const std::vector<sports::SportsEvent>>();
  std::vector<sports::matcher::GuideListing> listings;
  std::optional<time::Seconds> listingsAt;
  int listingsRevision = 0;
  int scheduleRevision = 0;
  std::string lastFingerprint, lastSaved;
  // Build 125: the day-guessed slots seen (read with the saved list, noted
  // by each rebuild and saved with its result on the worker).
  std::shared_ptr<const sports::matcher::SlotsSeen> slotsSeen;
  bool rebuilding = false;
  uint32_t nextCheckMs = 0;
  bool restoredTried = false, restoring = false;  // the saved list is read on a worker
  bool launchPass = false;  // it's in: the next tick rebuilds and makes the board, open or not
  // Opened with games too old to show (events_build::keptBoardShows): boards
  // wait for games worked out at or after this.
  time::Seconds freshAfter = 0;
  time::Seconds boardMadeAt = 0;  // the clock the board shown was made by
  // Which games (`events`) the board shown, and the one being made, were made from.
  uint64_t eventsSerial = 1, boardSerial = 0, boardDoneSerial = 0;
  // The saved schedule's writes (on workers, the newest wins).
  std::shared_ptr<events_build::Writes> scheduleWrites = std::make_shared<events_build::Writes>();
  // What the rebuild reads of the lineup, copied once per lineup.
  std::shared_ptr<const events_build::LineupCopy> lineupCopy;
  uint64_t lineupCopyKey = ~0ull;  // the lineup it was copied from (its revision and size)

  // Requests in flight.
  bool scheduleBusy = false;
  uint32_t scheduleAt = 0;   // send the pending schedule request at
  std::optional<sports::schedule::Schedule::Pending> schedulePending;
  // The day's schedule in the background (Build 125): looked at from this
  // time on (0: the channels aren't in yet), and next at.
  uint32_t scheduleBackgroundFrom = 0;
  uint32_t scheduleBackgroundCheck = 0;
  bool scoresBusy = false;
  bool scoresRound = false;
  uint32_t scoresAt = 0;     // next request or round at
  std::optional<sports::scores::LiveScores::Request> scoresPending;
  int statusesRevision = -1;

  sports::board::Board board;
  std::set<std::string> watchableListed;  // the board's (events_build::BoardMade)
  uint32_t boardBuiltMs = 0;
  // The board is made on a worker from a snapshot and put in by the tick,
  // one at a time: asked for again meanwhile, the next starts once it is in.
  // `boardDirty`: made again now (opened, a setting, My Sports); `boardStale`:
  // soon, but not sooner than a moment after the last began (scores, a
  // rebuild, the clock), so a burst of answers makes one board.
  bool boardDirty = true;
  bool boardStale = false;
  bool boardBusy = false;
  uint32_t boardStartMs = 0;
  std::shared_ptr<events_build::BoardMade> boardDone;
  std::shared_ptr<const events_build::ChannelIndex> channelIndex;  // of channelIndexOf
  std::weak_ptr<const std::vector<Channel>> channelIndexOf;
  std::vector<sports::board::WhipChannel> whips;  // whip-around channels, once per lineup
  uint64_t whipsKey = ~0ull;
  // The board jobs' own (one at a time): the race cameras' index and what
  // the titles say.
  std::shared_ptr<sports::cameras::CameraIndex> boardCameras = std::make_shared<sports::cameras::CameraIndex>();
  std::shared_ptr<sports::board::TitleMemo> boardTitles = sports::board::newTitleMemo();
  // The boards put in before, freed on a worker once no screen holds them.
  std::vector<std::shared_ptr<const EventsBoard>> retired;
  // The timing log (counts and milliseconds), a line at most every minute.
  struct Timing {
    uint32_t since = 0;
    int boards = 0, rebuilds = 0;
    size_t tiles = 0;
    double boardMax = 0, rebuildMax = 0, uiMax = 0;
  };
  Timing timing;

  // When the events shown were made (a rebuild, or the saved list's time;
  // 0: none). Opened, the board loads until the fresh channel list, schedule
  // and rebuild are in (checkedSinceOpen).
  time::Seconds eventsBuiltAt = 0;
  bool checkedSinceOpen = false;
  // What the games were matched from (events_build::MadeFrom), and the
  // wait for fresh inputs after an open or a wake (events_build::Hold):
  // since when (app ms), the hold the last board began with, the most games
  // left out meanwhile and old live scores set aside, for the timing log.
  events_build::MadeFrom madeFrom;
  bool holding = false, freshNote = false, holdCapped = false;
  uint32_t holdSinceMs = 0;
  events_build::Hold boardHold;
  int heldMost = 0, oldLiveMost = 0;
  // Build 123: the leagues of the games the last board left out for their
  // scores, and the look and games it was made for (BoardMade::heldScores;
  // the wait for scores ends once none of them can still answer).
  std::set<std::string> heldScores;
  std::pair<uint64_t, uint64_t> heldScoresOf{0, 0};
  uint64_t boardDoneLook = 0;
  // Opened on an old board (or none): the board shows placeholders in place
  // of its rows until it is whole (EventsBoard::loading), at most
  // kPlaceholdersAtMostMs after the open (holdSinceMs).
  bool wholeFirst = false;
  // The live scores round for this open and schedule: each open or wake
  // (`lookSerial`) and new schedule asks for one; the round begun for them
  // and the last one over.
  struct Look {
    uint64_t serial = 0;
    int schedule = 0;
    bool operator==(const Look& o) const { return serial == o.serial && schedule == o.schedule; }
    bool operator!=(const Look& o) const { return !(*this == o); }
  };
  uint64_t lookSerial = 0;
  Look roundFor, scoresDone;
  // The last tick's clocks: a long gap is a sleep (events_build::kAsleepFor).
  time::Seconds lastTickAt = 0;
  uint32_t lastTickMs = 0;
  uint32_t rebuildStartMs = 0;
  // The last rebuild's reasons for the feeds no tile took (raw feed id ->
  // why), for the Live Events report.
  std::map<std::string, std::string> hidden;
  // Lines for the playback log (Store::takeEventsLog); the board's line is
  // said when it changes after an open or a rebuild.
  std::vector<std::string> logNotes;
  std::string lastBoardNote;
  bool noteBoard = false;

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

// The guide rows' fields that follow from the channel list, the lineup and
// the reference data alone (names, logos, sections, groups, order), worked
// out once per lineup on its worker. A publish copies them and sets what a
// tune, a favourite or a lineup edit changes (Store::publishChannels).
// Immutable once made.
struct ChannelRows {
  struct Row {
    catalog::LineupCategory category = catalog::LineupCategory::entertainment;
    std::optional<int> number;    // a local's broadcast number
    bool serverFavorite = false;  // an Emby / Jellyfin favourite
  };
  std::vector<GuideChannel> all;  // favorite, inLineup, number and browsable unset
  std::vector<Row> rows;          // by `all` index
  std::unordered_map<std::string, int> byId;
  std::unordered_map<std::string, std::string> renamed;
  std::vector<int> lineup, browse;  // the lineup's and the browse list's rows
};

// M3U links and headers by raw channel id. Secret: memory and a 0600 file.
// Shared with workers: the saved file is read on one after launch (or on
// the spot by a tune that comes first), each download's links are merged on
// its sort's worker, and a changed set is written in the background.
struct StreamLinks {
  using Map = std::unordered_map<std::string, sources::m3u::StreamInfo>;
  std::mutex work;  // held while the file is read or links are merged
  std::mutex mu;    // `map` and `read`, held briefly
  std::shared_ptr<const Map> map = std::make_shared<const Map>();
  bool read = false;
  uint64_t savedPrint = 0;  // what was last written (under `work`)
  std::mutex fileMu;
  std::atomic<uint64_t> writes{0};  // writes asked for
  uint64_t written = 0;             // and done (under `fileMu`)
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
  std::vector<search::Recent> searches;  // names picked from Search, newest first
  std::map<std::string, std::string> pinned;  // entry id -> raw channel id
  // Entry id -> the version that last played on it: it starts first next
  // time unless one is pinned.
  std::map<std::string, std::string> lastFeed;
  std::map<std::string, std::vector<int>> reliability;  // source tag -> {worked, failed}
  // Starts of event feeds by provider group ("grp:us| nfhs ppv"), counted
  // afresh after a quiet week: a group that keeps failing goes last.
  struct GroupStarts {
    int worked = 0, failed = 0;
    time::Seconds at = 0;
  };
  std::map<std::string, GroupStarts> groupStarts;
  // Raw channel id -> when this TV last couldn't play it (playback
  // cannotPlayHere): tried after the channel's other versions for a while.
  std::map<std::string, time::Seconds> unplayable;
  // Entry or raw channel id -> when the viewer chose to keep listening to it
  // without a picture: no picture check for a while.
  std::map<std::string, time::Seconds> audioOnly;
  // Raw channel id -> when it last failed to play (this run only): a
  // direct version that failed lately lets a server one that played lead.
  std::map<std::string, time::Seconds> failedAt;
  std::set<std::string> quickStartRefused;  // server ids (Settings::embyQuickStart)
  std::set<std::string> captionChannels;    // feed channel ids whose stream had captions
  std::set<std::string> hiddenGroups;  // provider groups left out of All Channels
  catalog::LineupPrefs prefs;

  std::map<std::string, AccountRun> runs;  // by source id ("xt:..")
  std::shared_ptr<StreamLinks> links = std::make_shared<StreamLinks>();
  bool linksAsked = false;  // the saved links' read has been started
  int ticks = 0;            // tick() calls (the read waits for the first frame)

  // Catalog.
  std::shared_ptr<const std::vector<Channel>> channels = std::make_shared<std::vector<Channel>>();
  std::unordered_map<std::string, size_t> indexByID;
  std::shared_ptr<const CatalogResult> catalog = std::make_shared<CatalogResult>();
  std::unordered_map<std::string, size_t> entryInfo;  // entry id -> catalog->entries index
  std::unordered_map<std::string, size_t> familyAt;   // family id -> catalog->families index (the first)
  catalog::LineupOutput lineup;
  uint64_t lineupSerial = 0;  // bumped whenever `lineup` is replaced
  // The rows' list-dependent fields and what they were made from: a publish
  // from anything else works them out on the spot.
  std::shared_ptr<const ChannelRows> rows;
  struct RowsFrom {
    std::weak_ptr<const std::vector<Channel>> channels;
    std::weak_ptr<const CatalogResult> catalog;
    uint64_t lineup = 0;
  };
  RowsFrom rowsFrom;
  // A publish's copy and patch made on a worker (a tune, a favourite), put
  // in by tick(): only the newest one asked for.
  struct PatchJob {
    std::shared_ptr<Channels> out;
    double ms = 0;
    bool done = false;  // set by the job's UI half
  };
  std::shared_ptr<PatchJob> patching;
  bool partial = false;
  bool admissionCut = false;
  // The last downloaded list (before admission), and whether what admission
  // keeps has changed since (countries), so the same list must be sorted again.
  uint64_t listFingerprint = 0;
  bool admissionChanged = false;  // the last list was over the cap: groups were left out
  time::Seconds channelsLoadedAt = 0;
  // When a download this session last brought the list in (Live Events'
  // event slots; the saved list doesn't count).
  time::Seconds listFetchedAt = 0;
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
  // When each guide last started (wall clock), and the next look at whether
  // one is due by its age (tick ms; waking from standby looks at once).
  time::Seconds providerTriedAt = 0, outsideTriedAt = 0;
  uint32_t guidesLookAt = 0;
  time::Seconds lastTickAt = 0;
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
  // Search's index (store_search.cpp): the guide's part and the board's,
  // each made on a worker and put in by tick(), then joined.
  using SearchKey = std::array<uint64_t, 3>;
  // What the event channel groups' marks were made from: the catalog's
  // revision, the channels' version, and the catalog and list themselves.
  using MarksKey = std::array<uint64_t, 4>;
  struct SearchMade {
    std::shared_ptr<const search::Index> index;
    SearchKey key{};
    bool quick = false;
    double ms = 0;
    int catalogRevision = 0;
    sports::StationMarkets markets;
    std::shared_ptr<const std::vector<search::TeamEntry>> teams;
    std::string teamsKey;
    std::shared_ptr<const search_build::FamilyMarksById> marks;
    MarksKey marksKey{};
  };
  struct SearchPart {
    std::shared_ptr<const search::Index> index;
    SearchKey key{};    // what it was made from
    bool quick = false;  // the channels' names only (Search opened before any)
    int catalogRevision = 0;
    sports::StationMarkets markets;
    bool busy = false, quickBusy = false;
    std::shared_ptr<SearchMade> done, quickDone;  // made, not put in yet
  };
  SearchPart searchGuide, searchBoard;
  std::shared_ptr<const search::Index> searchIndex;
  // The board part's teams, made once per team lists.
  std::shared_ptr<const std::vector<search::TeamEntry>> searchTeams;
  std::string searchTeamsKey;
  // The board part's event channel group marks, made once per catalog and
  // channels.
  std::shared_ptr<const search_build::FamilyMarksById> searchMarks;
  MarksKey searchMarksKey{};
  // Made ahead in the background: when there is none, and after a guide job.
  bool searchWarm = false, searchGuideJobs = false;
  uint32_t searchWarmAt = 0;
  // lists.json, written on a worker for recent searches (the newest text
  // wins; one written synchronously since supersedes it).
  struct ListsWriter {
    std::mutex mu, fileMu;
    std::string pending;
    uint64_t seq = 0, pendingSeq = 0, written = 0;
  };
  std::shared_ptr<ListsWriter> listsWriter = std::make_shared<ListsWriter>();
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
  // The timing lines (counts and milliseconds): publishes, at most a line a
  // minute; the launch's steps (ms since the process started), once.
  struct PublishTiming {
    uint32_t since = 0;
    int full = 0, patched = 0, patchedLater = 0, made = 0;
    double fullMax = 0, patchMax = 0, laterMax = 0, madeMax = 0;
  };
  PublishTiming publishTiming;
  struct LaunchSteps {
    double ready = 0, firstRows = -1;
    bool said = false;
  };
  LaunchSteps launch;
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
