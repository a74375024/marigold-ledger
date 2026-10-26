#include "core/sports/cameras.hpp"

#include <algorithm>

#include "core/sports/board.hpp"
#include "core/sports/text.hpp"

namespace marigold::sports::cameras {

namespace {

using Kind = CameraFeed::Kind;

constexpr const char* kNowSuffix = "  \xC2\xB7  ";  // "  ·  "

// "DC0000": six hex digits, else no colour.
std::optional<std::string> validHex(const std::optional<std::string>& hex) {
  if (!hex || hex->size() != 6) return std::nullopt;
  for (char c : *hex) {
    bool digit = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!digit) return std::nullopt;
  }
  return hex;
}

std::string withNow(const std::string& detail, bool onScreen) {
  return onScreen ? detail + kNowSuffix + kNowText : detail;
}

// "Driver Alpha" -> "DA".
std::string initials(const std::string& name) {
  std::string out;
  std::vector<std::string> words = text::words(name);
  for (size_t i = 0; i < words.size() && i < 2; ++i) out += text::firstCharacter(words[i]);
  return str::upper(out);
}

}  // namespace

// MARK: Followed drivers

bool FollowedDrivers::toggle(const std::string& name) {
  if (names.erase(name)) return false;
  names.insert(name);
  return true;
}

std::string FollowedDrivers::toJson() const {
  json::Value list = json::Value::array();
  for (const std::string& name : names) list.push(name);  // a std::set is sorted
  return json::write(list);
}

FollowedDrivers FollowedDrivers::fromJson(std::string_view text) {
  FollowedDrivers out;
  auto root = json::parse(text);
  if (!root) return out;
  for (const json::Value& item : root->items()) {
    if (item.isString()) out.names.insert(item.asString());
  }
  return out;
}

// MARK: Camera index

void CameraIndex::update(const std::vector<Channel>& channels, const std::string& key) {
  if (indexed_ && key == key_) return;
  indexed_ = true;
  key_ = key;
  candidates_ = special_feeds::cameraCandidates(channels);
  cachedEvent_.reset();
}

std::vector<CameraFeed> CameraIndex::cameras(const SportsEvent& event, const std::vector<Channel>& eventFeeds) {
  if (!special_feeds::isF1(event) && !special_feeds::isNASCAR(event)) return {};
  if (cachedEvent_ && *cachedEvent_ == event.id && cachedKey_ == key_) return cached_;
  cached_ = special_feeds::cameras(event, eventFeeds, candidates_);
  cachedEvent_ = event.id;
  cachedKey_ = key_;
  return cached_;
}

// MARK: View model

bool isOnScreen(const CameraFeed& feed, const std::optional<std::string>& playingID) {
  if (!playingID) return false;
  return feed.channelID == *playingID ||
         std::find(feed.alternates.begin(), feed.alternates.end(), *playingID) != feed.alternates.end();
}

std::optional<race_data::RaceDriver> driverInfo(const CameraFeed& feed, bool f1, const race_data::RaceData& race,
                                                const player_photos::PlayerPhotos& photos) {
  if (feed.kind != Kind::driver) return std::nullopt;
  if (!f1) return race.nascarDriver(feed.title);
  std::optional<race_data::RaceDriver> known = race.driver(feed.title);
  if (known && known->headshot) return known;
  // No photo in the race data: the one from the player search (the race
  // data's number and colour kept when it has them).
  std::optional<std::string> photo = photos.photo(feed.title);
  if (!photo) return known;
  if (known) {
    known->headshot = photo;
    return known;
  }
  race_data::RaceDriver d;
  d.number = 0;
  d.code = initials(feed.title);
  d.name = feed.title;
  d.team = feed.team.value_or("");
  d.color = special_feeds::teamColor(feed.team);
  d.headshot = photo;
  return d;
}

CameraPanel build(const SportsEvent& event, const std::vector<CameraFeed>& feeds,
                  const std::optional<std::string>& playingID, const FollowedDrivers& followed,
                  const race_data::RaceData& race, const player_photos::PlayerPhotos& photos) {
  CameraPanel panel;
  panel.empty = feeds.empty();
  panel.f1 = special_feeds::isF1(event);
  panel.header.title = event.game ? board::title(*event.game) : event.title;

  std::vector<const CameraFeed*> drivers, followedFirst;
  for (const CameraFeed& feed : feeds) {
    if (feed.kind != Kind::driver) {
      bool on = isOnScreen(feed, playingID);
      QuickPick pick;
      pick.id = feed.id();
      pick.title = feed.title;
      pick.detail = withNow(feed.detail, on);
      pick.onScreen = on;
      pick.kind = feed.kind;
      pick.channelID = feed.channelID;
      pick.alternates = feed.alternates;
      panel.quick.push_back(std::move(pick));
    } else {
      (followed.contains(feed.title) ? followedFirst : drivers).push_back(&feed);
    }
  }
  followedFirst.insert(followedFirst.end(), drivers.begin(), drivers.end());
  for (const CameraFeed* feed : followedFirst) {
    bool on = isOnScreen(*feed, playingID);
    std::optional<race_data::RaceDriver> info = driverInfo(*feed, panel.f1, race, photos);
    DriverPill pill;
    pill.id = feed->id();
    pill.name = feed->title;
    // NASCAR feeds already say "No. 5"; F1 adds the number to the team.
    std::string detail = feed->detail;
    if (!feed->number && info && info->number > 0) detail += " \xC2\xB7 #" + std::to_string(info->number);
    pill.detail = withNow(detail, on);
    pill.color = validHex(info && info->color ? info->color : special_feeds::teamColor(feed->team));
    pill.badge = info.has_value();
    if (info) {
      pill.code = info->code;
      pill.headshot = info->headshot;
    }
    pill.followed = followed.contains(feed->title);
    pill.onScreen = on;
    pill.channelID = feed->channelID;
    pill.alternates = feed->alternates;
    panel.drivers.push_back(std::move(pill));
  }
  if (!panel.drivers.empty()) {
    panel.onboardLabel = str::upper("Onboard \xC2\xB7 " + std::to_string(panel.drivers.size()) + " drivers");
  }

  // Focus: the camera on screen, else the first quick pick, else the first driver.
  for (const CameraFeed& feed : feeds) {
    if (isOnScreen(feed, playingID)) {
      panel.focusID = feed.id();
      break;
    }
  }
  if (!panel.focusID && !panel.quick.empty()) panel.focusID = panel.quick.front().id;
  if (!panel.focusID && !panel.drivers.empty()) panel.focusID = panel.drivers.front().id;
  if (panel.focusID) panel.focusIndex = indexOf(panel, *panel.focusID);

  // Drivers, once a weekend (F1) or week (NASCAR); nothing when saved.
  if (panel.f1) {
    panel.onOpen.raceData = true;
    // Drivers without race data: their photos from the player search (saved 90 days).
    for (const CameraFeed& feed : feeds) {
      if (feed.kind != Kind::driver) continue;
      std::optional<race_data::RaceDriver> known = race.driver(feed.title);
      if (!known || !known->headshot) panel.onOpen.photos.push_back(feed.title);
    }
  } else {
    panel.onOpen.nascarDrivers = true;
  }
  return panel;
}

std::optional<size_t> indexOf(const CameraPanel& panel, const std::string& id) {
  for (size_t i = 0; i < panel.quick.size(); ++i) {
    if (panel.quick[i].id == id) return i;
  }
  for (size_t i = 0; i < panel.drivers.size(); ++i) {
    if (panel.drivers[i].id == id) return panel.quick.size() + i;
  }
  return std::nullopt;
}

// MARK: Switching

std::vector<std::string> tuneList(const std::string& channelID, const std::vector<std::string>& alternates,
                                  const std::function<bool(const std::string&)>& isEntry,
                                  const std::function<std::vector<std::string>(const std::string&)>& versions) {
  std::vector<std::string> list;
  if (isEntry && isEntry(channelID) && versions) list = versions(channelID);
  if (list.empty()) list.push_back(channelID);
  for (const std::string& alternate : alternates) {
    if (std::find(list.begin(), list.end(), alternate) == list.end()) list.push_back(alternate);
  }
  return list;
}

std::optional<std::vector<std::string>> watchList(
    const std::string& channelID, const std::vector<std::string>& alternates,
    const std::optional<std::string>& playingID, const std::function<bool(const std::string&)>& isEntry,
    const std::function<std::vector<std::string>(const std::string&)>& versions) {
  if (playingID && (channelID == *playingID ||
                    std::find(alternates.begin(), alternates.end(), *playingID) != alternates.end())) {
    return std::nullopt;
  }
  return tuneList(channelID, alternates, isEntry, versions);
}

}  // namespace marigold::sports::cameras
