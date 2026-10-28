// The data store: accounts, channel lists, the catalog, My Lineup and guide
// listings. Network and parsing run on the pool; results become immutable
// snapshots swapped in on the UI thread (see model.hpp).
//
// Request budget (docs/plan.md §6): channel lists re-read every 30 minutes,
// the provider guide every 12 hours, outside guides every 20 hours with
// ETag, and short EPG only for rows on screen that have no full guide.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <string>
#include <vector>

#include "core/engine/events_model.hpp"
#include "core/engine/host.hpp"
#include "core/engine/model.hpp"

namespace marigold {
struct Channel;
struct Program;
namespace catalog {
struct CatalogResult;
struct LineupPrefs;
enum class Coast : int;
struct MarketChoice;
namespace cache {
struct Saved;
}
}  // namespace catalog
namespace sources {
struct Account;
}
namespace sports {
struct MySportsPrefs;
namespace cameras {
struct CameraPanel;
}
struct SportsTeam;
namespace schedule {
struct Context;
}
}  // namespace sports
}  // namespace marigold

namespace marigold::engine {

struct Fetched;
struct OpenStream;

// A country / language of the channel list, for the lineup setup.
struct MarketSummary {
  std::string code;   // "US", "US-ES", "MX"...
  std::string name;   // "United States (English)"
  int count = 0;      // channels
  bool on = false;
};

// TV markets for "Where do you watch?" (US Nielsen markets).
struct TvMarkets {
  std::vector<std::string> all;        // biggest first
  std::vector<std::string> suggested;  // from the channel names
  std::string problem;                 // shown when the list couldn't load
};

// A lineup category with how many channels it would suggest.
struct CategorySummary {
  std::string key;  // saved value ("local", "news"...)
  std::string title, detail;
  int suggested = 0, total = 0;
  bool on = false;
};

struct AccountSummary {
  std::string id;
  std::string name;   // the name the person gave it
  std::string kind;   // "Xtream", "Emby", "M3U playlist"
  std::string state;  // "1,240 channels", "Couldn't sign in"...
};

// Fields of the add-account form. Passwords and playlist links go to the
// secrets file only.
struct AccountForm {
  enum class Kind { Xtream, Emby, M3U } kind = Kind::Xtream;
  std::string name;
  std::string server;    // Xtream/Emby base address, or the M3U playlist link
  std::string user;
  std::string password;
  std::string guideUrl;  // optional XMLTV link (M3U)
};

// A reminder that just came due: the card in the corner.
struct DueReminder {
  std::string channelId, channelName, title;
  time::Seconds start = 0;
};

// A provider group, for hiding it from All Channels.
struct GroupSummary {
  std::string name;
  int count = 0;
  bool hidden = false;
};

// Options › Guide Listings.
struct GuideListingsStatus {
  struct Provider {
    std::string name, line;
  };
  std::vector<Provider> providers;  // full guide files (Xtream, M3U guide links)
  bool providerRunning = false;
  std::string outsideLine;
  struct Shard {
    std::string id, title, detail;
    bool on = false;
  };
  std::vector<Shard> shards;
  std::vector<std::string> sourceLines;  // one per listings file, no links
};

// A show in the loaded guide (search, other airings).
struct ProgramHit {
  std::string entryId;
  Listing listing;
};

struct Settings {
  bool previewChannels = true;  // the guide corner plays the focused channel
  bool scheduleEnabled = true;  // Live Events: game schedule and scores
  bool showUnwatchable = true;  // games not in my channels
  bool showScores = true;
  bool allChannelsTab = false;
  bool preferTS = true;         // Xtream: MPEG-TS rather than HLS
  bool fillMissingListings = true;  // outside guides for channels without one
  bool clock24 = false;
  bool playerTipsShown = false;  // the one-time "Getting around" card in the player
  std::set<std::string> guideShards;  // outside listings to use; empty = the defaults
};

class Store {
 public:
  explicit Store(Host host);
  ~Store();
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  // UI thread, once per loop. True when a snapshot changed.
  bool tick(uint32_t ms);

  std::shared_ptr<const Channels> channels() const { return channels_; }
  std::shared_ptr<const Listings> listings() const { return listings_; }
  const StoreStatus& status() const { return status_; }

  // Accounts.
  std::vector<AccountSummary> accounts() const;
  // Checks the login with one request, then saves and loads. `done` runs on
  // the UI thread with an empty string or a message to show.
  void addAccount(const AccountForm& form, std::function<void(std::string)> done);
  void removeAccount(const std::string& id);
  void refreshAll();

  // Guide rows on screen: fetches short EPG for those with no listings,
  // a few rows at a time. Cheap to call every frame.
  void wantListings(const std::vector<std::string>& entryIds);

  // Personal lists.
  void toggleFavorite(const std::string& entryId);
  void toggleLineup(const std::string& entryId);
  // Puts these channels in My Lineup in one go (the preview tool's copy of
  // a report's lineup).
  void addToLineup(const std::vector<std::string>& entryIds);
  void noteWatched(const std::string& entryId);

  // The provider a raw channel comes from, by the name it was given.
  std::string providerName(const std::string& channelId) const;
  // The guide entry a raw provider channel belongs to ("" when none).
  std::string entryOfChannel(const std::string& channelId) const;
  // Feeds for an entry, best first ("Always start with" honoured).
  std::vector<Feed> feeds(const std::string& entryId) const;
  // Emby: asks the server to open a feed (needsOpen); `done` gets it with
  // its URL, or nothing. Closes the stream opened before.
  void open(const Feed& feed, std::function<void(std::optional<Feed>)> done);
  void closeStream();
  // Feed outcomes move reliable sources up.
  void recordFeed(const std::string& channelId, bool worked);
  // The version of a channel that just played: it starts first next time
  // (unless one is pinned with pinFeed).
  void rememberFeed(const std::string& entryId, const std::string& channelId);
  void pinFeed(const std::string& entryId, const std::string& channelId);  // "" = automatic
  std::string pinnedFeed(const std::string& entryId) const;

  // My Lineup setup (LineupSetupView). The draft is the caller's until
  // buildLineup().
  catalog::LineupPrefs lineupPrefs() const;
  // Market list: built into the app (the build reads it from Wikipedia).
  void loadMarkets(std::function<void(TvMarkets)> done);
  // Each picked market with its stations' call signs (kept from the draft
  // when it already has them).
  void resolveMarkets(const std::vector<std::string>& names, const catalog::LineupPrefs& draft,
                      std::function<void(std::vector<catalog::MarketChoice>)> done);
  // Countries and languages of the channel list (US English and US Spanish
  // always listed), marked on when in `selected`.
  std::vector<MarketSummary> countries(const std::set<std::string>& selected) const;
  std::vector<CategorySummary> categories(const catalog::LineupPrefs& draft) const;
  void buildLineup(catalog::LineupPrefs draft);
  static std::string marketName(const std::string& code);

  // Live Events. The board snapshot; scores refresh only while at least one
  // screen holds it open (board, event guide).
  std::shared_ptr<const EventsBoard> events() const { return events_; }
  void eventsOpen(bool open);
  bool isEvent(const std::string& id) const;
  const EventTile* eventTile(const std::string& id) const;
  WhereToWatch whereToWatch(const std::string& eventId) const;
  // How a tile came about, in plain lines (the preview tool's detail card):
  // the schedule game or the channel title, its live status, its feeds.
  std::vector<std::string> eventNotes(const std::string& eventId) const;
  // A provider feed family's feeds (raw channel ids, by number).
  std::vector<std::string> familyFeeds(const std::string& familyId) const;
  // The games Up/Down flips through when watching `eventId`.
  std::vector<std::string> eventFlipList(const std::string& eventId) const;
  // Feeds for a game with the source `via` (entry or raw channel) first.
  std::vector<Feed> eventFeeds(const std::string& eventId, const std::string& via) const;
  bool hasReminder(const std::string& id) const;
  bool toggleReminder(const std::string& id);  // true = now set
  // Leaving or switching a game: remember the source used (6 hours).
  void noteEventFeed(const std::string& eventId, const std::string& channelId, bool sawPicture);
  const std::optional<DueReminder>& dueReminder() const { return dueReminder_; }
  uint32_t dueReminderAt() const { return dueReminderAt_; }
  void dismissReminder();
  // My Sports.
  const sports::MySportsPrefs& mySports() const;
  void setMySports(const sports::MySportsPrefs& prefs);
  std::vector<sports::SportsTeam> teamsOf(const std::string& leagueShort) const;
  void requestRosters(const std::set<std::string>& leagueShorts);
  // Bumped when a league's team list arrives (My Sports reloads its grid).
  int teamsRevision() const;
  void releaseRosters();
  // League (or other sport) id -> provider events that look like it.
  std::map<std::string, int> sportSuggestions() const;
  void refreshSchedule();

  // Options pages (store_options.cpp).
  std::vector<GroupSummary> groups() const;
  void setGroupHidden(const std::string& name, bool hidden);
  void setLineupCountries(const std::set<std::string>& countries);
  void refreshSuggestions();
  // East/West feeds: the setting, what Automatic stands for here, and the
  // channels that have both versions (their names, East first).
  void setCoast(catalog::Coast coast);
  catalog::Coast resolvedCoast() const;
  std::vector<std::string> coastPairNames() const;
  // Channels with both versions one by one: the East ids (the chosen
  // countries', guide order), and which versions My Lineup holds (east,
  // west, both; automatic = neither). Setting it keeps that channel's
  // choice as the East/West setting or suggestions change.
  std::vector<std::string> coastPairs() const;
  catalog::Coast channelCoast(const std::string& entryId) const;
  void setChannelCoast(const std::string& entryId, catalog::Coast coast);
  // Home city for Live Events (only with more than one market).
  std::string homeMarketName() const;
  void cycleHomeMarket();
  GuideListingsStatus guideListings() const;
  void setGuideShard(const std::string& id, bool on);
  void refreshListings();
  std::set<std::string> guideShards() const;
  // Remind Me on a show (reminders are shared with games).
  bool hasShowReminder(const std::string& entryId, time::Seconds start) const;
  bool toggleShowReminder(const std::string& entryId, const Listing& l);
  // The same show later on any channel with loaded listings.
  std::vector<ProgramHit> otherAirings(const std::string& entryId, const Listing& l) const;
  // Search: games and shows on now and later (loaded listings).
  std::vector<std::string> searchEvents(const std::string& query) const;
  std::vector<ProgramHit> searchPrograms(const std::string& query) const;
  // My Sports: asked once, the first time the board opens.
  bool shouldPromptMySports() const;
  void markMySportsPrompted();

  // Race cameras (store_race.cpp): Down while watching a race.
  bool hasCameras(const std::string& eventId);
  // The panel for a race; `opening` asks for what it is missing (drivers,
  // photos) once.
  sports::cameras::CameraPanel cameraPanel(const std::string& eventId, const std::string& playingChannel, bool opening);
  // Bumped when drivers, photos or followed drivers change.
  int camerasRevision() const;
  bool toggleFollowDriver(const std::string& name);  // true = now followed
  // Feeds for a camera, best first; nullopt when it is already on screen.
  std::optional<std::vector<Feed>> cameraFeeds(const std::string& channelId, const std::vector<std::string>& alternates,
                                               const std::string& playingChannel) const;

  // Options › Help › Export Channel Report: the CSV, built on a worker.
  // `done` runs on the UI thread (null on failure).
  void buildChannelReport(std::function<void(std::shared_ptr<std::string>)> done);

  const Settings& settings() const { return settings_; }
  void setSettings(const Settings& s);

  struct Impl;

 private:
  void saveAccounts();
  void saveLists();
  void saveM3uStreams();
  void setPrefs(const catalog::LineupPrefs& p);
  std::string currentSignature() const;
  void reset();
  void load(bool force);
  void installSaved(catalog::cache::Saved saved, bool partial);
  void downloadChannels(uint64_t gen);
  void channelsDownloaded(uint64_t gen, std::shared_ptr<std::vector<Fetched>> parts);
  void readStations();
  void readLogoIndex();
  void fetchLogoIndex();
  void rebuildCatalog();
  void installCatalog(std::shared_ptr<const std::vector<Channel>> list,
                      std::shared_ptr<const catalog::CatalogResult> cat, bool partial);
  void saveCatalog();
  void rebuildEntries();
  time::Seconds windowStart() const;
  bool entryHasGuide(const std::string& id) const;
  void startGuideFetches();
  std::vector<std::string> entriesOf(const std::vector<std::string>& rawIds) const;
  bool providerCovers(const std::string& id, time::Seconds start) const;
  void landed(const std::vector<std::string>& ids,
              const std::unordered_map<std::string, std::vector<Program>>* results, time::Seconds start);
  void fetchXtreamGuide(const std::vector<std::string>& ids, const sources::Account& account);
  void fetchEmbyGuide(const std::vector<std::string>& ids, const sources::Account& account);
  std::vector<Program> providerListing(const std::string& id) const;
  std::vector<Program> outsideListing(const std::string& id) const;
  std::vector<Program> bestListing(const std::string& id) const;
  void applyEntryGuides(const std::vector<std::string>& entryIDs);
  void rebuildOutsidePrograms();
  // After a channel sort or a guide build: freed memory back to the system,
  // and a memory line in the playback log.
  void afterBigJob(const char* what);
  void startGuides(bool requestRows);
  void readSavedListings();
  void installProviderSaved();
  void scheduleProvider(bool force);
  void scheduleOutside(bool force);
  void scheduleLineupRefresh();
  void runProviderGuide();
  void runOutsideGuide();
  void publishChannels();
  void publishListings();
  double reliabilityOf(const std::string& channelId) const;
  std::vector<std::string> versionsOf(const std::string& id) const;
  std::vector<Feed> feedsFor(const std::vector<std::string>& rawIds) const;
  std::vector<std::string> eventBackups(const std::string& entryId) const;
  // Live Events (store_events.cpp).
  void loadEvents();
  void eventsTick(uint32_t ms);
  void rebuildEvents();
  void buildBoard();
  void pumpSchedule(uint32_t ms);
  void pumpScores(uint32_t ms);
  void saveEvents();
  sports::schedule::Context scheduleContext() const;
  void loadRace();
  void pumpRace(uint32_t ms);
  void pumpPhotos(uint32_t ms);
  void savePhotos();
  std::vector<Channel> eventFeedChannels(const std::string& eventId) const;

  Host host_;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<const Channels> channels_ = std::make_shared<Channels>();
  std::shared_ptr<const Listings> listings_ = std::make_shared<Listings>();
  StoreStatus status_;
  Settings settings_;
  std::shared_ptr<OpenStream> openStream_;
  std::shared_ptr<const EventsBoard> events_ = std::make_shared<EventsBoard>();
  int eventsOpen_ = 0;
  std::optional<DueReminder> dueReminder_;
  uint32_t dueReminderAt_ = 0;
};

}  // namespace marigold::engine
