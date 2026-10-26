// The cameras panel (the pure logic of LiveCameraPanel.swift and
// LiveTVStore's "Race cameras and whip-arounds"): Down while watching a
// race shows its cameras over the bottom of the picture. Quick picks first
// (main and Sky feeds, mixed onboard, tracker, data, the podium cameras,
// multiviews), then every driver in team colours, followed drivers first.
// Select switches, Play/Pause follows a driver, Back closes.
//
// The app drives it like this:
// - Keep a CameraIndex: update() it when the channel list changes; cameras()
//   gives an event's feeds (cached per event).
// - Starting a race with cameras: flash kCamerasHint.
// - Down opens the panel when cameras() isn't empty: build() it, focus
//   panel.focusIndex, and act on panel.onOpen (race data refresh or NASCAR
//   drivers, player photos for drivers with none).
// - Rebuild it when RaceData / PlayerPhotos revision(), the follow set or the
//   playing feed change, keeping focus on the same id (indexOf).
// - Select: closes the panel, then watchList() gives what to tune (nullopt:
//   already on screen). Play/Pause on a driver: FollowedDrivers::toggle,
//   save toJson(), rebuild.
#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/channel.hpp"
#include "core/sports/player_photos.hpp"
#include "core/sports/race_data.hpp"
#include "core/sports/special_feeds.hpp"
#include "core/sports/types.hpp"

namespace marigold::sports::cameras {

using special_feeds::CameraFeed;

constexpr const char* kCamerasHint = "Press \xE2\x96\xBC for onboard cameras";  // "Press ▼ ..."
constexpr const char* kPanelHint =
    "Select: switch   \xC2\xB7   Play/Pause: follow a driver   \xC2\xB7   Back: close";
constexpr const char* kLive = "LIVE";
constexpr const char* kNowText = "now";
// Driver pills per row.
constexpr int kDriverColumns = 4;

// MARK: Followed drivers

// Drivers you follow, by camera title; they lead the driver grid. Persisted
// by the app as a sorted JSON array of names.
struct FollowedDrivers {
  std::set<std::string> names;

  bool contains(const std::string& name) const { return names.count(name) > 0; }
  // Play/Pause on a driver: follow or unfollow. True when now followed.
  bool toggle(const std::string& name);
  std::string toJson() const;
  // Anything unreadable is an empty set.
  static FollowedDrivers fromJson(std::string_view text);
};
constexpr const char* kFollowedKey = "followedDrivers";

// MARK: Camera index

// Channels that could be a race camera, indexed once per channel list, and
// the last event's cameras.
class CameraIndex {
 public:
  // The channel list (raw channels). Re-indexed only when `key` changes
  // (e.g. its revision and size).
  void update(const std::vector<Channel>& channels, const std::string& key);
  const std::vector<Channel>& candidates() const { return candidates_; }
  // An event's cameras and other views, best first; empty for events
  // without any. `eventFeeds`: the event's provider feeds as raw channels.
  std::vector<CameraFeed> cameras(const SportsEvent& event, const std::vector<Channel>& eventFeeds);

 private:
  std::string key_;
  bool indexed_ = false;
  std::vector<Channel> candidates_;
  std::optional<std::string> cachedEvent_;
  std::string cachedKey_;
  std::vector<CameraFeed> cached_;
};

// MARK: View model

struct Header {
  std::string live = kLive;  // the "LIVE" badge
  std::string title;         // "Grand Prix Alpha - Race", the event's title for a feed
  std::string hint = kPanelHint;
};

struct QuickPick {
  std::string id;  // the feed's channel id (focus key)
  std::string title;
  std::string detail;  // with "  ·  now" when on screen
  bool onScreen = false;  // outlined
  CameraFeed::Kind kind = CameraFeed::Kind::main;
  // What Select plays (watchList).
  std::string channelID;
  std::vector<std::string> alternates;
};

struct DriverPill {
  std::string id;
  std::string name;    // "Driver A"
  std::string detail;  // "Team Alpha · #44", "No. 5", with "  ·  now" when on screen
  // Team colour as hex ("DC0000"); nullopt: a dim white.
  std::optional<std::string> color;
  // With driver info: a round photo in the team colour, `code` (initials)
  // until it loads. Without: a thin bar in the colour.
  bool badge = false;
  std::string code;
  std::optional<std::string> headshot;
  bool followed = false;  // a star
  bool onScreen = false;  // outlined
  std::string channelID;
  std::vector<std::string> alternates;
};

// What the panel asks for when it opens (once a weekend for F1 drivers, a
// week for NASCAR; nothing when saved).
struct OpenRequests {
  // RaceData::refreshIfNeeded({}, false, now).
  bool raceData = false;
  // RaceData::refreshNascarIfNeeded(now).
  bool nascarDrivers = false;
  // PlayerPhotos::request(...): F1 drivers the race data has no photo for.
  std::vector<std::string> photos;
};

struct CameraPanel {
  bool empty = true;  // no cameras: nothing to open
  bool f1 = false;
  Header header;
  std::vector<QuickPick> quick;
  // "ONBOARD · 20 DRIVERS" over the grid (none without drivers).
  std::optional<std::string> onboardLabel;
  std::vector<DriverPill> drivers;
  int columns = kDriverColumns;
  // Where focus starts, over quick picks then drivers: the camera on
  // screen, else the first quick pick, else the first driver.
  std::optional<size_t> focusIndex;
  std::optional<std::string> focusID;
  OpenRequests onOpen;
};

// A camera is the one on screen: its channel or one of its backups plays.
bool isOnScreen(const CameraFeed& feed, const std::optional<std::string>& playingID);

// Photo, number and colour for a driver's camera: NASCAR from its driver
// list; F1 from the race data, its photo from the player search when the
// race data has none (initials and the team colour when there's only a photo).
std::optional<race_data::RaceDriver> driverInfo(const CameraFeed& feed, bool f1, const race_data::RaceData& race,
                                                const player_photos::PlayerPhotos& photos);

CameraPanel build(const SportsEvent& event, const std::vector<CameraFeed>& feeds,
                  const std::optional<std::string>& playingID, const FollowedDrivers& followed,
                  const race_data::RaceData& race, const player_photos::PlayerPhotos& photos);

// Position of an id over quick picks then drivers (keep focus after a rebuild).
std::optional<size_t> indexOf(const CameraPanel& panel, const std::string& id);

// MARK: Switching

// What to tune for a camera, in order: the channel (its versions when it is
// an entry), then its other feeds. Try Again on a camera retunes this.
std::vector<std::string> tuneList(const std::string& channelID, const std::vector<std::string>& alternates,
                                  const std::function<bool(const std::string&)>& isEntry,
                                  const std::function<std::vector<std::string>(const std::string&)>& versions);
// Select on a camera: tuneList, or nullopt when it's already on screen. The
// race stays the game on screen (the guide, Up / Down and the panel keep
// working).
std::optional<std::vector<std::string>> watchList(
    const std::string& channelID, const std::vector<std::string>& alternates,
    const std::optional<std::string>& playingID, const std::function<bool(const std::string&)>& isEntry,
    const std::function<std::vector<std::string>(const std::string&)>& versions);

}  // namespace marigold::sports::cameras
