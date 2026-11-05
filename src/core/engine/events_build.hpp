// Making Live Events (store_events.cpp) on a worker: what the matcher's
// rebuild and the board are made from is copied or shared on the UI thread
// first, so nothing here reads the store; the store puts the result in at
// its tick. One of each runs at a time, so what a job owns (the board's
// camera index) is never shared between two.
//
//   rebuild()  the guide's game listings when due, the channel index and the
//              matcher over the provider's event slots and the schedule;
//              the result saved for the next launch.
//   board()    the board's rows and tiles, every string already made.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/catalog/lineup.hpp"
#include "core/catalog/work.hpp"
#include "core/engine/events_model.hpp"
#include "core/engine/model.hpp"
#include "core/sports/board.hpp"
#include "core/sports/cameras.hpp"
#include "core/sports/store.hpp"

namespace marigold::engine::events_build {

// A board kept from before (saved last session, or made before Live Events
// was closed) shows on opening only when its games were worked out under 15
// minutes ago on the same local day, and then only as made again from the
// clock (live, starting soon, ended). Otherwise the board's placeholders
// show until a fresh one is made.
constexpr time::Seconds kKeptBoardFor = 15 * 60;
bool keptBoardShows(time::Seconds builtAt, time::Seconds now, const time::Zone& zone);

// Live Events freshness (Build 119). A board made now can still be made
// from old inputs: the provider's channel list (its event slots) and the
// schedule saved last time, or live scores answered hours ago. So after an
// open (or the TV waking with Live Events on screen) the board leaves out
// what such inputs decide until fresh ones are in, and shows placeholders
// while that is everything; after kHoldAtMostMs it shows every tile, each
// live only by a score answered lately or by the clock (finals kept, games
// over by the clock gone, "Time not listed" as it is).
//
// New enough: the channel list for 15 minutes after it was fetched this
// session; the schedule while fetched the same day under 20 hours ago (it
// isn't fetched again then); a score while its league is polled
// (sports::scores::kHeldFor). An input nothing newer is coming for (offline,
// refused, backing off after failures) holds nothing up.
//
// Build 123: a game waits for its score only while one can still come
// (sports::scores::Awaited: its league polled, answering and not asked
// lately); one whose league has no scoreboard, is refused or failing, or
// already answered without it goes by the clock at once. The wait for
// scores ends once none of the games left out can still get one, so the
// board is whole in a few seconds rather than at kHoldAtMostMs.
constexpr time::Seconds kListFreshFor = 15 * 60;
// Under the screen's placeholders (kPlaceholdersAtMostMs), so the board
// made then is in first.
constexpr uint32_t kHoldAtMostMs = 18000;
static_assert(kHoldAtMostMs < kPlaceholdersAtMostMs, "the whole board comes before the placeholders end");
// No tick for this long (wall clock or app clock): the TV was off or the
// app asleep, and Live Events is opened again on waking.
constexpr time::Seconds kAsleepFor = 120;
bool listFresh(time::Seconds fetchedAt, time::Seconds now);
// Build 141: on opening, a board matched from a channel list read this
// session within this long shows at once (a re-read then comes in place);
// older, or from the list saved last session, it waits as above. The list
// is read again every half hour, but not while a channel plays full screen.
constexpr time::Seconds kListKeptFor = 3 * 3600;
bool listKept(time::Seconds fetchedAt, time::Seconds now);
bool scheduleCurrent(time::Seconds fetchedAt, time::Seconds now, const time::Zone& zone);

// When what the games were matched from was fetched (0: not this session,
// or never).
struct MadeFrom {
  time::Seconds listAt = 0;
  time::Seconds scheduleAt = 0;
};

// What decides the games, and what is coming.
struct Inputs {
  MadeFrom made;             // the games on hand
  time::Seconds listAt = 0;  // the newest channel list in, and one downloading
  bool listComing = false;
  time::Seconds scheduleAt = 0;  // the newest schedule in, and one being fetched
  bool scheduleComing = false;
  // A live scores round since the open (and the newest schedule) is over.
  bool scoresChecked = true;
  // A game the last board left out for its score can still get one
  // (sports::scores::LiveScores::awaits()), or no board has said yet.
  bool scoresAwaited = true;
};

// What a board made now leaves out while it waits for fresh inputs.
struct Hold {
  bool list = false;      // event slots, feed families and games with feeds
  bool schedule = false;  // schedule games
  // Games around now no score answered lately covers, whose score can
  // still come (BoardInput::awaited).
  bool scores = false;
  bool any() const { return list || schedule || scores; }
  bool operator==(const Hold& o) const { return list == o.list && schedule == o.schedule && scores == o.scores; }
  bool operator!=(const Hold& o) const { return !(*this == o); }
};
Hold holdFor(const Inputs& in, time::Seconds now, const time::Zone& zone);

// What a loading board waits for now (Build 124), from what keeps it
// loading (Store::boardFlags()): the channel list first, then the schedule,
// then the games matched to them (a rebuild, the saved board read, or games
// not yet worked out since the open), then the hold: still for a list or
// schedule just in (matching), else for scores.
struct Loading {
  bool list = false;      // the channel list downloading
  bool schedule = false;  // the schedule being fetched
  bool matching = false;  // games being worked out
  bool holding = false;   // the board waits for fresh inputs (Hold)
  Hold hold;              // what for, while holding
};
EventsBoard::Stage loadStage(const Loading& l);

// Raw channels by id: made once per channel list, shared with the workers.
using ChannelIndex = std::unordered_map<std::string, size_t>;

// A file written on a worker (atomic replace, a temporary name of its own).
// With `order`, the newest asked for wins: one asked for earlier that
// finishes later gives up (`turn` is what `++order->asked` gave it, under
// `order->mu`).
struct Writes {
  std::mutex mu;
  uint64_t asked = 0;
};
void writeFile(const std::string& path, const std::string& text, Writes* order = nullptr, uint64_t turn = 0);

// What the rebuild reads of the lineup, copied once per lineup.
struct LineupCopy {
  std::set<std::string> lineupIDs;
  std::vector<Channel> lineupEntries, browseEntries, entries;
  std::vector<std::string> browseIDs;
};

struct RebuildJob {
  // Games, schools, rosters, the catalog's events and traits (shared), the
  // time and the hooks; `previous`, `listings` and `channelIndex` are filled
  // in on the worker.
  sports::store::RebuildInput in;
  std::shared_ptr<const std::vector<sports::SportsEvent>> previous;
  std::shared_ptr<const catalog::CatalogResult> catalog;
  std::shared_ptr<const LineupCopy> lineup;
  sports::StationMarkets markets;
  // The guide's game listings are read again (from `programs`), else
  // `listings` (the last ones) are used.
  bool readListings = false;
  catalog::ProgramLists programs;
  std::vector<sports::matcher::GuideListing> listings;
  // Build 136: read again with nothing else changed since the last match
  // (`sameElse`): listings whose digest (store::listingsDigest) is the last
  // read's aren't matched again.
  bool sameElse = false;
  uint64_t listingsDigest = 0;
  // F1 channels: made again when `f1` and none are given.
  bool f1 = false;
  std::optional<std::vector<sports::EventSource>> f1Sources;
  std::set<std::string> hiddenGroups;
  // The saved result (shown at once next launch): its file, and what was
  // saved last (not the same one twice).
  std::string savePath, lastSaved;
};

struct RebuildMade {
  std::optional<sports::store::RebuildResult> result;  // nullopt: stopped
  std::optional<std::vector<sports::matcher::GuideListing>> listings;  // read again
  uint64_t listingsDigest = 0;                                         // theirs
  bool unchanged = false;  // read again, the same, nothing else new: not matched (no result)
  std::optional<std::vector<sports::EventSource>> f1Sources;           // made again
  // The feeds matched and their titles (report::matchedFeeds), with a result.
  std::shared_ptr<const std::vector<std::pair<uint64_t, uint64_t>>> matched;
  std::string lastSaved;
  bool saved = false;
  double prepMs = 0, matchMs = 0, saveMs = 0;
};
RebuildMade rebuild(RebuildJob& job);

struct BoardInput {
  std::shared_ptr<const std::vector<sports::SportsEvent>> events;
  std::shared_ptr<const catalog::CatalogResult> catalog;  // families, traits
  std::shared_ptr<const Channels> guide;                  // the published guide channels
  std::shared_ptr<const std::vector<Channel>> channels;   // raw
  std::shared_ptr<const ChannelIndex> index;               // of `channels`; made when null
  bool proWrestling = false, highSchool = false;
  sports::MySportsPrefs prefs;
  sports::scores::Statuses statuses;
  std::map<std::string, time::Seconds> finalSeen;
  std::vector<sports::board::WhipChannel> whips;
  time::Seconds now = 0;
  time::Zone zone = time::Zone::device();
  sports::board::Settings settings;
  std::set<std::string> failing;  // provider groups that keep failing to start
  catalog::LineupPrefs lineupPrefs;
  std::function<std::vector<sports::board::PodiumDriver>(const std::string&, time::Seconds)> podium;
  // A race's "Onboard cameras": the board jobs' own index (one at a time).
  std::shared_ptr<sports::cameras::CameraIndex> cameras;
  std::string camerasKey;
  // What the titles say, kept from one board to the next (one at a time).
  std::shared_ptr<sports::board::TitleMemo> titles;
  // Waiting for fresh inputs: what is left out (`statuses` already holds
  // only the ones answered lately; `oldLive` live ones were set aside).
  Hold hold;
  int oldLive = 0;
  // With hold.scores: whose games' scores can still come (the rest go by
  // the clock).
  sports::scores::Awaited awaited;
};

struct BoardMade {
  sports::board::Board board;
  // Rows and tiles; the store sets its version, loading and updating.
  std::shared_ptr<EventsBoard> events;
  std::shared_ptr<const ChannelIndex> index;
  // Games listed whole only where you can watch them (tennis, a league's
  // every game): what a live scores round asks about.
  std::set<std::string> watchableListed;
  time::Seconds madeAt = 0;  // the clock it was made by
  Hold hold;                 // what it waited for
  int held = 0, oldLive = 0;  // games left out for it, old live scores set aside
  std::set<std::string> heldScores;  // the leagues of games left out for their scores
  size_t tiles = 0;
  double ms = 0;
};
BoardMade board(const BoardInput& in);

// Build 142: the line under Live Events' title while its scores catch up
// (as the guide's "Filling in missing listings…"): a spinner and "Updating
// scores…" while this open's round still has a stale league of a game
// around now to ask; "Live scores paused until tomorrow" or "Live scores
// unavailable right now" for a few seconds after the open when they can't
// come; nothing when the scores are fresh, off, or no game is around now.
constexpr uint32_t kScoresNoteMs = 8000;
using engine::ScoresLine;  // events_model.hpp
struct ScoresLineInput {
  bool shown = false;     // Live Events on screen with its board (no placeholders)
  bool scoresOn = true;   // the "Show scores" setting
  bool checked = true;    // this open's scores round is over (Inputs::scoresChecked)
  // The leagues of games around now (live or about to start, not final)
  // on the board: one the poller will still ask this round (stale), one
  // that can't answer now (failing or refused), the day's requests used up.
  bool staleAround = false;
  bool unavailableAround = false;
  bool capped = false;
  bool anyAround = false;
  uint32_t sinceOpenMs = 0;
};
ScoresLine scoresLine(const ScoresLineInput& in);

}  // namespace marigold::engine::events_build
