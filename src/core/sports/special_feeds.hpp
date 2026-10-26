// The lineup's special sports feeds, recognised by name (SpecialFeeds.swift):
// the cameras and other views of a race (F1 onboards, the driver tracker,
// NASCAR in-car cams and multiviews, a race's main and Sky feeds), and the
// channels that carry every F1 session (LiveTVStore's "F1 channels").
// Whip-arounds are in board.hpp (whipNamed). Pure; no requests.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/model/channel.hpp"
#include "core/sports/types.hpp"

namespace marigold::sports::special_feeds {

// A camera or alternate view of an event.
struct CameraFeed {
  // Order of the panel: the race's own feeds, then the quick picks, the
  // drivers last.
  enum class Kind : int { main = 0, alt, mixed, tracker, data, podium, multiview, driver };
  // The raw channel that plays it.
  std::string channelID;
  // "Driver A", "Driver Tracker", "Sky Sports".
  std::string title;
  // "Team Alpha", "Bird's-eye map", "Follows P1".
  std::string detail;
  Kind kind = Kind::main;
  // Team, for drivers (colours and grouping).
  std::optional<std::string> team;
  // Other channels carrying the same camera (automatic backups).
  std::vector<std::string> alternates;
  // The car number (NASCAR in-car feeds).
  std::optional<int> number;

  const std::string& id() const { return channelID; }
  bool operator==(const CameraFeed& o) const {
    return channelID == o.channelID && title == o.title && detail == o.detail && kind == o.kind && team == o.team &&
           alternates == o.alternates && number == o.number;
  }
  bool operator!=(const CameraFeed& o) const { return !(*this == o); }
};

// MARK: Names

// A channel worth indexing as a camera (cheap test over the whole list).
// The shape board::Input::isCameraName and store::RebuildInput::isCameraName take.
bool isCameraName(std::string_view name);

// An in-car camera named by car: "Race 500: No. 5 Driver Alpha
// (10.25 1:45PM ET)" -> (5, "Driver Alpha"). Not a ranked game or a team
// sport's show.
struct CarCamera {
  int number = 0;
  std::string driver;
};
std::optional<CarCamera> carCamera(std::string_view title);

// Race parts of in-car and multiview titles that name no particular race.
const std::set<std::string>& genericNascarRaces();

// The race an in-car feed or multiview belongs to ("Race 500: No. 5 Driver
// Alpha" -> "race 500"); nullopt for other cameras. The shape
// board::Input::cameraRace takes.
std::optional<std::string> cameraRace(std::string_view title);

// The other F1 camera sets: "Driver Alpha | Team Alpha | DRA" (the team must
// be an F1 team), "F1 DATA", "F1: F1-TRACKER".
bool isF1CameraName(std::string_view name);

// MARK: Teams and series

struct F1Team {
  const char* name;
  const char* color;  // hex, "DC0000"
};
// The current teams, longest names first ("Red Bull Racing" before "Red Bull").
const std::vector<F1Team>& f1Teams();
// A team's colour by its exact name.
std::optional<std::string> teamColor(const std::optional<std::string>& team);

bool isF1(const SportsEvent& event);
// NASCAR, or a race named only by its sponsor (league Motorsport, Other or
// none, no game, not F1): its cameras still have to share its name.
bool isNASCAR(const SportsEvent& event);

// MARK: Cameras

// The cameras for an event, best first: its own feeds (main, Sky...), then
// mixed onboard, tracker, data, podium feeds, multiviews and every driver.
// Empty for events without cameras (only the race's own feeds isn't a
// cameras panel). `eventFeeds`: the event's provider feeds as raw channels;
// `candidates`: the channels isCameraName accepts (cameraCandidates).
std::vector<CameraFeed> cameras(const SportsEvent& event, const std::vector<Channel>& eventFeeds,
                                const std::vector<Channel>& candidates);

// The channels that could be a race camera (index once per channel list).
std::vector<Channel> cameraCandidates(const std::vector<Channel>& channels);

// MARK: F1 channels

// Channels that carry every F1 session (Sky Sports F1, DAZN F1, Canal+ F1,
// Viaplay F1, F1 TV's main feeds), as "F1 channel" sources of kind team:
// yours first, then up to 3 English and up to 4 others. Not cameras,
// trackers, data or pit lane feeds; hidden groups and non-channels skipped.
// For store::RebuildInput::f1Sources; work it out once per f1ChannelsKey.
std::vector<EventSource> f1ChannelSources(const std::vector<Channel>& entries,
                                          const std::unordered_map<std::string, catalog::EntryInfo>& entryInfo,
                                          const std::set<std::string>& hiddenGroups, const std::string& otherGroup,
                                          const std::set<std::string>& lineup);
// When f1ChannelSources must be worked out again.
std::string f1ChannelsKey(int entriesRevision, size_t entryCount, const std::set<std::string>& lineup,
                          const std::set<std::string>& hiddenGroups);
// The rebuild asks for F1 channels only while the schedule has an F1
// session that isn't over.
bool wantsF1Sources(const std::vector<SportsGame>& games);

constexpr const char* kF1ChannelDetail = "F1 channel";

}  // namespace marigold::sports::special_feeds
