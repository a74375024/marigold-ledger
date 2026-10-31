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
  std::optional<std::vector<sports::EventSource>> f1Sources;           // made again
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
  size_t tiles = 0;
  double ms = 0;
};
BoardMade board(const BoardInput& in);

}  // namespace marigold::engine::events_build
