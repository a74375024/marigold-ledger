// The data store: accounts, channel lists, the catalog, My Lineup and guide
// listings. Network and parsing run on the pool; results become immutable
// snapshots swapped in on the UI thread (see model.hpp).
//
// Request budget (docs/plan.md §6): channel lists re-read every 30 minutes,
// the provider guide every 12 hours, outside guides every 20 hours with
// ETag, and short EPG only for rows on screen that have no full guide.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <string>
#include <vector>

#include "core/engine/disk.hpp"
#include "core/engine/events_model.hpp"
#include "core/engine/host.hpp"
#include "core/engine/membership.hpp"
#include "core/engine/model.hpp"
#include "core/engine/quality.hpp"
#include "core/engine/search.hpp"

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
namespace xtream {
struct AccountInfo;
}
}  // namespace sources
namespace sports {
struct MySportsPrefs;
struct Conference;
namespace cameras {
struct CameraPanel;
}
struct SportsTeam;
namespace schedule {
struct Context;
}
namespace scores {
struct RoundInput;
}
}  // namespace sports
}  // namespace marigold

namespace marigold::engine {

struct Fetched;
struct OpenStream;
namespace events_build {
struct BoardInput;
struct Inputs;
}

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
  std::string state;  // "Signed in", "Couldn't sign in" (never a channel count)
  bool signInFailed = false;  // the last read of its channels failed
  // Its end date, trial and streams at once (Xtream, or an M3U playlist from
  // an Xtream panel); in words with membership::accountLine and the rest.
  membership::Membership membership;
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

// Settings › Guide › Guide listings, and Help & info › Status.
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

// First-run setup's "Getting your guide ready" (Build 130): its three steps,
// worked out from what the store holds whenever it is asked. Only the
// provider's guide is waited for, never the missing listings.
struct GuideReady {
  bool channels = false;  // the channel list is in and sorted, its rows on screen
  bool lineup = false;    // My Lineup is made and its rows are published
  // The provider's listings are in: every provider with a guide file has
  // one built (none with a file: done at once; nothing of theirs to read:
  // done once the list is in).
  bool listings = false;
  // Its download or read failed and nothing is running now: the line says so.
  bool listingsProblem = false;
  std::string problemLine;
  // How far the download is, 0..1, while it runs and the server gave its
  // size; -1 otherwise.
  float listingsProgress = -1;
};

// The guide header's line while a guide builds (Build 130; the channel
// list's own lines go first): the provider's guide ("for the first time"
// when none was ever saved), else the missing listings, which say so while
// they wait for full-screen playback to end. Empty when neither runs.
struct GuideBuildLine {
  std::string line;
  StoreStatus::Mark mark = StoreStatus::Mark::None;
};
GuideBuildLine guideBuildLine(bool providerRunning, bool firstProviderGuide, bool outsideRunning, bool outsideWaiting);

// A show in the loaded guide (search, other airings).
struct ProgramHit {
  std::string entryId;
  Listing listing;
};

struct Settings {
  bool previewChannels = true;  // the guide corner plays the focused channel
  bool scheduleEnabled = true;  // Live Events: game schedule and scores
  bool showUnwatchable = true;  // games not on my channels
  // Settings › Live Events › Show on the board (Build 119): the last three
  // rows of the board, off until turned on (installs from before too). The
  // board keeps them; only the board screen, its SPORTS column and the
  // games rail leave them out (boardShows()).
  bool boardMoreEvents = false;     // More events
  bool boardUntimed = false;        // Time not listed
  bool boardEventChannels = false;  // Event channels
  // Build 125: "Games without a schedule": events only the provider's event
  // channels list (no schedule has them) as tiles in More events. Off: the
  // board leaves them out (sports::board::Settings::showsUnscheduled).
  bool boardUnscheduled = false;
  bool showScores = true;
  bool allChannelsTab = false;
  bool preferTS = true;         // Xtream: MPEG-TS rather than HLS
  // Experimental: Emby / Jellyfin channels start from the server's direct
  // link (no open step first), with the lean live load on webOS.
  bool embyQuickStart = false;
  bool fillMissingListings = true;  // outside guides for channels without one
  bool clock24 = false;
  // Search keeps the names of what was picked (Settings › Guide › Recent
  // searches); off also clears them.
  bool recentSearches = true;
  bool playerTipsShown = false;  // the one-time "Getting around" card in the player
  // Subtitles: off until turned on, then on for every channel that has them
  // until turned off. English-language channels and Spanish-language ones
  // each keep their own choice (subtitleChoice()).
  bool subtitles = false;
  std::string subtitleLanguage;  // the language picked last ("" = each channel's own)
  bool subtitlesEs = false;      // the same, for Spanish-language channels
  std::string subtitleLanguageEs;
  int subtitleSize = 1;          // 0 small, 1 medium, 2 large, 3 extra large
  int subtitleLook = 0;          // 0 box, 1 outline, 2 shadow
  bool subtitleSentenceCase = false;
  bool subtitleTop = false;
  // Caption timing, ms in half-second steps (+ later, - earlier): for every
  // channel, and the channels given their own (guide entry id -> ms).
  int captionTiming = 0;
  std::map<std::string, int> captionTimingByChannel;
  // webOS: every channel goes through the app's local relay (captions can
  // be read on the way); off plays continuous TS feeds directly again.
  bool playThroughApp = true;
  // The player's Up arrow goes to the channel above in the list (lower
  // numbers), as the guide does; on, it goes to higher numbers, as CH+ does.
  bool upGoesHigher = false;
  // The system's captions switch when last seen (Android, Apple TV): -1 not
  // read yet. Turning it on or off there turns subtitles on or off here.
  int systemCaptions = -1;
  std::set<std::string> guideShards;  // outside listings to use; empty = the defaults
};

// The rows the board screen draws besides the sports: Event channels
// always while Game schedule is off (the board is the provider's events).
inline BoardShows boardShows(const Settings& s) {
  return {s.boardMoreEvents, s.boardUntimed, s.boardEventChannels || !s.scheduleEnabled, s.boardUnscheduled};
}

// The subtitles choice for a channel's language: on/off and the language
// picked last ("" = the channel's own).
struct SubtitleChoice {
  bool on = false;
  std::string language;
};
SubtitleChoice subtitleChoice(const Settings& s, bool spanishChannel);
void setSubtitleChoice(Settings& s, bool spanishChannel, const SubtitleChoice& c);
// Subtitles are on for either kind of channel.
inline bool anySubtitles(const Settings& s) { return s.subtitles || s.subtitlesEs; }

// Caption timing: -3 s to +3 s in half seconds.
inline constexpr int kCaptionTimingStepMs = 500;
inline constexpr int kCaptionTimingMaxMs = 3000;
int clampCaptionTiming(int ms);
// A channel's Caption timing: its own when it has one, else every channel's.
int captionTiming(const Settings& s, const std::string& entryId);
bool hasOwnCaptionTiming(const Settings& s, const std::string& entryId);
// Sets the channel's own (`entryId`), or every channel's ("").
void setCaptionTiming(Settings& s, const std::string& entryId, int ms);
// The channel follows every channel's again.
void clearOwnCaptionTiming(Settings& s, const std::string& entryId);

// How long a version this TV couldn't play is tried after the others.
inline constexpr time::Seconds kUnplayableFor = 7 * 24 * 3600;
// How long a channel the viewer keeps listening to (no picture anywhere)
// plays as sound only, without the picture check.
inline constexpr time::Seconds kAudioOnlyFor = 7 * 24 * 3600;
// How long a failed direct (Xtream / M3U) version lets a server-opened one
// that played last start first.
inline constexpr time::Seconds kDirectFailedFor = 6 * 3600;

class Store {
 public:
  explicit Store(Host host);
  ~Store();
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  // UI thread, once per loop. True when a snapshot changed.
  bool tick(uint32_t ms);

  std::shared_ptr<const Channels> channels() const { return channels_; }
  // Every row worked out here and now from what the store holds, as each
  // publish did before the rows were made once per lineup (what the
  // published ones must equal), for checks. Nothing changes.
  std::shared_ptr<const Channels> channelsNow() const;
  std::shared_ptr<const Listings> listings() const { return listings_; }
  const StoreStatus& status() const { return status_; }
  // How far the full sort under way has got (Stage::Done when none is).
  LoadProgress loadProgress() const;
  // The list on its way is the one saved on this TV (a relaunch).
  bool openingSaved() const;

  // Accounts.
  std::vector<AccountSummary> accounts() const;
  // Checks the login with one request, then saves and loads. `done` runs on
  // the UI thread with an empty string or a message to show.
  void addAccount(const AccountForm& form, std::function<void(std::string)> done);
  void removeAccount(const std::string& id);
  void refreshAll();
  // The guide was opened: in a membership's last week (and after it ends)
  // its status line says so for a few seconds, the first time each day
  // (per provider, kept across launches), unless a more important line
  // (loading, updating) is up; then it waits its turn.
  void guideOpened();

  // Guide rows on screen: fetches short EPG for those with no listings,
  // a few rows at a time. Cheap to call every frame.
  void wantListings(const std::vector<std::string>& entryIds);

  // Personal lists.
  void toggleFavorite(const std::string& entryId);
  // Build 149: whether a row is a favourite now (a toggle shows at once;
  // the published rows follow on the next tick).
  bool isFavorite(const std::string& entryId) const;
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
  // Settings::embyQuickStart: the feed with the server's direct link, no
  // open step (closes what is open first); nullopt if it can't be made.
  std::optional<Feed> quickOpen(const Feed& feed);
  // The server of that channel refused a quick start: normal opens there
  // from now on (saved).
  void quickStartRefused(const std::string& channelId);
  bool quickStartRefusedBy(const std::string& channelId) const;
  // A feed's stream carried captions (remembered, so the player can offer
  // them before they're switched on).
  void captionsSeen(const std::string& channelId);
  bool captionsKnown(const std::string& channelId) const;
  // `first`: the server's stream is closed before the next open (on the
  // playback lane, briefly), the rest in the background.
  void closeStream(bool first = false);
  // Feed outcomes move reliable sources up.
  void recordFeed(const std::string& channelId, bool worked);
  // Build 136: the picture a raw channel's stream showed (the playback
  // log's "video" line), for its rows next time (pictures.json).
  void notePicture(const std::string& channelId, const quality::Picture& picture);
  // An event group ("grp:us| nfhs ppv") whose feeds mostly failed to start
  // this week: its tiles go after the others. The count for the report
  // ("1 of 4 started"; "" when none this week).
  bool groupKeepsFailing(const std::string& familyKey) const;
  std::string groupStartsText(const std::string& familyKey) const;
  // This TV couldn't play that version (playback::cannotPlayHere): it is
  // tried after the channel's other versions for kUnplayableFor.
  void markUnplayable(const std::string& channelId);
  // "Keep listening" on a channel (an entry or a raw channel id) with no
  // picture: it plays as sound only, unchecked, for kAudioOnlyFor (saved).
  void markAudioOnly(const std::string& id);
  bool audioOnly(const std::string& id) const;
  // The version of a channel that just played: it starts first next time
  // (unless one is pinned with pinFeed).
  void rememberFeed(const std::string& entryId, const std::string& channelId);
  void pinFeed(const std::string& entryId, const std::string& channelId);  // "" = automatic
  std::string pinnedFeed(const std::string& entryId) const;

  // Build 153: "Listings look wrong?" The distinct schedules a row's guide
  // could show (guide/choice.hpp), the one shown first: what each has on
  // now and next. Empty when the row's listings haven't been weighed yet.
  struct GuideOption {
    std::string key;  // pickGuide's argument
    std::string now, next;
    time::Seconds nextStart = 0;
    bool current = false;  // shown now
  };
  std::vector<GuideOption> guideOptions(const std::string& entryId) const;
  // The viewer's pick for a row ("" = automatic), saved with the lists; it
  // is dropped once that schedule is gone.
  void pickGuide(const std::string& entryId, const std::string& key);
  bool guidePicked(const std::string& entryId) const;
  // The row's listings have been weighed and there is one to show.
  bool hasGuideChoice(const std::string& entryId) const;

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

  // Live Events. The board snapshot; scores refresh at full pace while at
  // least one screen holds it open (board, event guide), and from launch in
  // the background at a gentle pace (Build 141).
  std::shared_ptr<const EventsBoard> events() const { return events_; }
  // Build 142: the line under Live Events' title while the scores of the
  // board's games around now catch up (events_build::scoresLine). `shown`:
  // the board is on screen without placeholders.
  ScoresLine eventsScoresLine(bool shown) const;
  // Which Live Events screen opens or closes: the board (and Search's
  // games), or (Build 145) the in-player event guide, which keeps the
  // scores at full pace over a playing channel while it's open
  // (sports::scores::Holder::guide).
  enum class EventsScreen { board, guide };
  void eventsOpen(bool open, EventsScreen screen = EventsScreen::board);
  // Build 145: what a Live Events screen is on, so its scores come first
  // (sports::scores::RoundInput::focus): the row focused ("sport|Tennis";
  // "" none) and, in the player's guide, what plays (an event id or a
  // guide channel). Cheap: call it when either changes.
  void eventsFocus(EventsScreen screen, const std::string& rowId, const std::string& playing = {});
  bool isEvent(const std::string& id) const;
  const EventTile* eventTile(const std::string& id) const;
  WhereToWatch whereToWatch(const std::string& eventId) const;
  // The player's game banner for what plays: a game (its event id), or a
  // guide channel showing a game the board matched, by the clock now.
  // Nullopt for anything else (the channel's usual banner). Only what the
  // board holds already: nothing is asked for.
  std::optional<GameBanner> gameBanner(const std::string& entryId, time::Seconds now) const;
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
  // Build 127 (choosing your sports v2). Every change goes through
  // setMySports() (the board made again; the schedule asked again only
  // when what is looked up changed).
  // The picks a new viewer starts with: sports::defaultPicks() for the
  // lineup's countries (not configured; the setup saves it so).
  sports::MySportsPrefs defaultMySports() const;
  // Why the defaults tick `id` for the lineup's countries ("For Español"; "").
  std::string pickReason(const std::string& id) const;
  // The conferences of a college league seen in the schedules already
  // fetched (no requests), name A-Z; the leagues that follow its settings
  // (sports::collegeKey()) count: "NCAAM" lists the small-college pages'
  // conferences too. Conferences with no known name are left out.
  std::vector<sports::Conference> collegeConferences(const std::string& leagueShort) const;
  // The schools seen in one of them: (team id, school name) A-Z.
  std::vector<std::pair<std::string, std::string>> conferenceSchools(const std::string& leagueShort,
                                                                     const std::string& conferenceKey) const;
  // Hide from the board (out of the picks, into hidden) and show again
  // (out of hidden, into the picks): the game card and Settings › Live
  // Events › Hidden. Unconfigured picks become the defaults, set up, first.
  void hideFromBoard(const std::string& id);
  void showOnBoard(const std::string& id);
  struct HiddenItem {
    std::string id, name;
    std::string why;  // "Hidden from a game card" / "Hidden unless you turn it on"
  };
  // Hidden ids, then pro wrestling and high school when not picked.
  std::vector<HiddenItem> hiddenFromBoard() const;
  // "LEAGUE|id" into (true) or out of My Sports' teams (EventTile::follow).
  void followTeam(const std::string& teamKey, bool follow);
  // The board's one-time note read: updateNoted = true.
  void dismissBoardNote();
  std::vector<sports::SportsTeam> teamsOf(const std::string& leagueShort) const;
  void requestRosters(const std::set<std::string>& leagueShorts);
  // Bumped when a league's team list arrives (My Sports reloads its grid).
  int teamsRevision() const;
  void releaseRosters();
  // League (or other sport) id -> provider events that look like it.
  std::map<std::string, int> sportSuggestions() const;
  void refreshSchedule();

  // Settings pages (store_options.cpp).
  std::vector<GroupSummary> groups() const;
  void setGroupHidden(const std::string& name, bool hidden);
  void setLineupCountries(const std::set<std::string>& countries);
  // False, and nothing done, while the guide is loading (guideLoading()):
  // the suggestions would miss the listings still on their way.
  bool refreshSuggestions();
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
  // Home city for Live Events (a choice only with more than one market).
  std::string homeMarketName() const;
  void cycleHomeMarket();
  void setHomeMarket(const std::string& name);
  GuideListingsStatus guideListings() const;
  void setGuideShard(const std::string& id, bool on);
  // False, and nothing started, while the guide is loading.
  bool refreshListings();
  // A guide build runs, or the first one is about to start: Settings' rows
  // that refresh the guide or the lineup say "The guide is already loading"
  // instead of starting another.
  bool guideLoading() const;
  // The same for the provider's guide alone (its build runs, or the first
  // one is due): "Reload channels and listings" waits only for this one,
  // since the missing listings can take long (they wait while you watch)
  // and start again after a reload anyway.
  bool providerGuideLoading() const;
  // First-run setup waits on this (cheap: a few lookups; asked a few times
  // a second at most).
  GuideReady guideReady() const;
  // A guide row with no listings yet may still get some (Build 130): the
  // provider's guide builds (or its first is due), the missing listings
  // build (or their first is due), or the row's own request is on its way.
  // The guide says "Listings on the way" then, "No guide info" after.
  bool listingsOnTheWay(const std::string& entryId) const;
  // The listings saved on this TV are being read (a relaunch): rows wait
  // with placeholders rather than saying anything.
  bool savedListingsReading() const;
  std::set<std::string> guideShards() const;
  // Remind Me on a show (reminders are shared with games).
  bool hasShowReminder(const std::string& entryId, time::Seconds start) const;
  bool toggleShowReminder(const std::string& entryId, const Listing& l);
  // The same show later on any channel with loaded listings.
  std::vector<ProgramHit> otherAirings(const std::string& entryId, const Listing& l) const;
  // Search (store_search.cpp): what a query looks through. Never waits:
  // when the channels, listings, board, teams or My Sports have changed it
  // starts making the part they're in on a worker and returns the index it
  // has (tick() puts the new one in). Before any, an empty one.
  std::shared_ptr<const search::Index> searchIndex() const;
  // Recent searches, newest first (search::kRecentKept are kept).
  const std::vector<search::Recent>& recentSearches() const;
  void rememberSearch(const search::Recent& r);
  void forgetSearch(const std::string& id);
  void clearSearches();
  // Follow or unfollow a team ("LEAGUE|id") from Search; true = now followed.
  bool toggleFollowTeam(const std::string& key);
  bool followsTeam(const std::string& key) const;
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

  // Settings › Help & info › Send a report: the channel report CSV, built
  // on a worker.
  // `done` runs on the UI thread (null on failure).
  void buildChannelReport(std::function<void(std::shared_ptr<std::string>)> done);
  // The Live Events report: every event slot of the provider's
  // groups, what it was read as and what became of it (a tile, or why not).
  void buildLiveEventsReport(std::function<void(std::shared_ptr<std::string>)> done);
  // Live Events lines for the playback log since the last call (opened,
  // rebuilt, the board's size). Counts only, never titles or links.
  std::vector<std::string> takeEventsLog();
  // The board made here and now, on this thread, from what the store holds
  // (what the next one made on a worker shows), for checks. Nothing changes.
  std::shared_ptr<const EventsBoard> boardNow();

  const Settings& settings() const { return settings_; }
  void setSettings(const Settings& s);

  struct Impl;

 private:
  void saveAccounts();
  // A provider's account info (Xtream, or the panel an M3U link is from):
  // its end date, trial and streams at once.
  void membershipRead(const std::string& accountId, const sources::xtream::AccountInfo& info);
  // lists.json now (recent searches switched off: forgotten on file too).
  void saveLists();
  // The same on a worker (a tune, a favourite, a search): the newest list
  // is written, a burst of changes once or twice; the destructor writes one
  // still waiting.
  void saveListsLater();
  std::string listsText() const;
  // The saved M3U links read on a worker (after the first frame); the links
  // of the providers with these prefixes ("m3u:..|") dropped on one.
  void readLinksLater();
  void dropLinks(std::vector<std::string> prefixes);
  void setPrefs(const catalog::LineupPrefs& p);
  std::string currentSignature() const;
  // The cache files in use (disk.hpp): this signature's channel list and
  // listings, the providers' guides, the chosen guide areas' files.
  disk::Owners diskOwners() const;
  // On a worker: what has no owner removed, the folder kept in budget.
  void pruneDisk();
  void reset();
  void load(bool force);
  void installSaved(catalog::cache::Saved saved, bool partial);
  void downloadChannels(uint64_t gen);
  void channelsDownloaded(uint64_t gen, std::shared_ptr<std::vector<Fetched>> parts);
  void readStations();
  void useBuiltInLogos();
  void rebuildCatalog();
  void installCatalog(std::shared_ptr<const std::vector<Channel>> list,
                      std::shared_ptr<const catalog::CatalogResult> cat, bool partial);
  // Only event slots were renamed: the new list and catalog, keeping the
  // lineup and listings.
  void installRenamed(std::shared_ptr<const std::vector<Channel>> list,
                      std::shared_ptr<const catalog::CatalogResult> cat);
  void saveCatalog();
  void releaseMemoryWhenIdle(uint32_t ms);
  // How old the channel list may get before it's read again.
  time::Seconds channelLifetime() const;
  void liveEventsOpened();
  void rebuildEntries();
  // Moves versions this TV couldn't play lately after the others (stable);
  // `pinned` stays where it is.
  void lastIfUnplayable(std::vector<std::string>& ids, const std::optional<std::string>& pinned) const;
  // Ready now if rows are on screen; else when the new list's are.
  void readyWhenShown();
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
  // Build 153: rows whose listings are weighed by channel identity (My
  // Lineup, favourites, rows asked for on screen, rows with a pick), and
  // the weighing itself: gathered here, decided on a worker.
  bool weighsGuide(const std::string& entryId) const;
  void decideRows(const std::vector<std::string>& entryIDs);
  void rebuildOutsidePrograms();
  // After a channel sort or a guide build: freed memory back to the system,
  // and a memory line in the playback log.
  void afterBigJob(const char* what);
  void startGuides(bool requestRows);
  // Listings for the rows the guide opens on: favourites, recents and the
  // lineup's first rows.
  void wantFirstRows();
  void readSavedListings();
  void installProviderSaved();
  void scheduleProvider(bool force);
  void scheduleOutside(bool force);
  void scheduleLineupRefresh();
  // Each guide started again once due by its own age, whatever the channel
  // list does: looked at every minute and on waking from standby.
  void lookAtGuides(uint32_t ms);
  void runProviderGuide();
  void runOutsideGuide();
  // The rows made for the list and lineup on screen are still theirs.
  bool rowsCurrent() const;
  void publishChannels();
  void putChannels(std::shared_ptr<Channels> out);
  void noteChannelsTiming(uint32_t ms);
  void publishListings();
  double reliabilityOf(const std::string& channelId) const;
  // Build 136: a raw channel's picture for the rows: measured ("1080p · 60"),
  // else its name's or group's claim ("FHD"), else "".
  std::string qualityText(const std::string& channelId) const;
  void savePicturesLater();
  // Build 136: how likely a game's provider feed is to start (for bestFeeds):
  // 0 when it failed lately, else its group's starts this week, else
  // reliabilityOf.
  double feedReliability(const std::string& channelId) const;
  std::vector<std::string> versionsOf(const std::string& id) const;
  // The version is opened on its server first (Emby / Jellyfin).
  bool serverOpened(const std::string& channelId) const;
  std::vector<Feed> feedsFor(const std::vector<std::string>& rawIds) const;
  std::vector<std::string> eventBackups(const std::string& entryId) const;
  // Search's index (store_search.cpp).
  void startSearchBuilds(bool forSearch) const;
  bool installSearch() const;
  bool searchTick(uint32_t ms);
  // Live Events (store_events.cpp).
  void loadEvents();
  void eventsTick(uint32_t ms);
  // Live Events opened, or the TV woke with it open after `away` seconds.
  void eventsArrive(time::Seconds away);
  // What the games were matched from and what is coming (freshness).
  events_build::Inputs eventsInputs() const;
  void rebuildEvents();
  std::shared_ptr<events_build::BoardInput> boardInput();
  void startBoard(uint32_t ms);
  void putBoard();
  // The board's loading and "more to come" flags (EventsBoard).
  void boardFlags(EventsBoard& out) const;
  // What a load waits for now (EventsBoard::stage).
  EventsBoard::Stage loadStage() const;
  // The board shown (no rows: the placeholders') again with a load's next
  // stage.
  void restage(EventsBoard::Stage stage);
  // Placeholders in place of the board (Live Events opened on old games).
  void showPlaceholders();
  void noteEventsTiming(uint32_t ms, std::chrono::steady_clock::time_point start);
  void pumpSchedule(uint32_t ms, bool background = false);
  // Live Events closed: the day's schedule fetched in the background once
  // the channels are in, while nothing else needs the TV (Build 125); Build
  // 141: from launch, a channel playing full screen or not, with the scores
  // at the background's pace. True when the games may be matched and a
  // board made from them now (Live Events used, the list not loading, no
  // guide being built).
  bool eventsInBackground(uint32_t ms);
  void pumpScores(uint32_t ms);
  // The scores' focus (Build 145) from what the screens are on
  // (eventsFocus) and your teams.
  void scoresFocus(sports::scores::RoundInput& in) const;
  // The game an entry plays: a game's own id, or the game a guide channel
  // shows now ("" none).
  std::string eventPlaying(const std::string& entryId, time::Seconds now) const;
  void saveEvents();
  void saveSchedule();
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
  int guideOpen_ = 0;  // the in-player event guide (Build 145)
  std::optional<DueReminder> dueReminder_;
  uint32_t dueReminderAt_ = 0;
};

}  // namespace marigold::engine
