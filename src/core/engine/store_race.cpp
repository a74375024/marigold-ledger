// Race weekends and tennis in the store (the "Race cameras", RaceData and
// PlayerPhotos parts of LiveTVStore in tvOS): drivers and results for race
// weekends you can watch, tennis player photos for the tiles, and the
// cameras panel. Each source is one request at a time, spaced and capped by
// the core rules; nothing is asked for unless a race or a tennis match is on
// the board (or the cameras panel opens). UI thread only.
#include <algorithm>
#include <set>

#include "core/engine/store.hpp"
#include "core/engine/store_impl.hpp"
#include "core/sports/cameras.hpp"
#include "core/sports/player_photos.hpp"
#include "core/sports/race_data.hpp"
#include "core/sports/special_feeds.hpp"
#include "core/util/log.hpp"

namespace marigold::engine {
namespace {

namespace rd = sports::race_data;
namespace pp = sports::player_photos;
namespace cam = sports::cameras;

constexpr const char* kRaceFile = "race-data.json";
constexpr const char* kNascarFile = "nascar-drivers.json";
constexpr const char* kPhotosFile = "player-photos.json";
constexpr const char* kFollowedFile = "followed-drivers.json";

http::Request toHttp(const sources::Request& r, std::string tag) {
  http::Request q;
  q.method = r.method == "GET" ? std::string() : r.method;
  q.url = r.url;
  for (const sources::Header& h : r.headers) q.headers.push_back(h.name + ": " + h.value);
  q.timeoutMs = static_cast<unsigned>(std::max(1, r.timeoutSeconds)) * 1000u;
  q.priority = http::Priority::Background;
  q.tag = std::move(tag);
  q.maxBytes = 8u << 20;
  q.follow = true;
  return q;
}

}  // namespace

void Store::loadRace() {
  EventsState& e = impl_->ev;
  Files& st = host_.files;
  rd::Saved saved;
  rd::NascarSaved nascar;
  if (auto text = st.read(kRaceFile))
    if (auto s = rd::savedFromJson(*text)) saved = std::move(*s);
  if (auto text = st.read(kNascarFile))
    if (auto s = rd::nascarFromJson(*text)) nascar = std::move(*s);
  e.race = rd::RaceData(std::move(saved), std::move(nascar));
  if (auto text = st.read(kPhotosFile))
    if (auto s = pp::savedFromJson(*text)) e.photos = pp::PlayerPhotos(std::move(*s));
  if (auto text = st.read(kFollowedFile)) e.followed = cam::FollowedDrivers::fromJson(*text);
}

void Store::savePhotos() {
  EventsState& e = impl_->ev;
  if (auto s = e.photos.persist(time::now())) host_.files.write(kPhotosFile, pp::toJson(*s));
}

// One race-data request at a time while a pass runs; the pass ends (and is
// saved) when nextRequest has nothing more.
void Store::pumpRace(uint32_t ms) {
  EventsState& e = impl_->ev;
  if (e.raceWasRunning && !e.race.running() && !e.raceBusy) {
    e.raceWasRunning = false;
    host_.files.write(kRaceFile, rd::toJson(e.race.saved()));
    if (e.race.revision() != e.raceSeen) e.boardDirty = true;
  }
  if (!e.race.running() || e.raceBusy || ms < e.raceAt) return;
  e.raceWasRunning = true;
  if (!e.racePending) {
    auto req = e.race.nextRequest(time::now(), host_.zone());
    if (!req) return;  // over (saved next tick) or an answer is awaited
    e.racePending = std::move(*req);
    if (e.racePending->waitMs > 0) {
      e.raceAt = ms + static_cast<uint32_t>(e.racePending->waitMs);
      return;
    }
  }
  rd::RaceData::Request send = std::move(*e.racePending);
  e.racePending.reset();
  e.raceBusy = true;
  auto body = std::make_shared<std::string>();
  http::Request q = toHttp(send.request, "events.race");
  q.afterTransfer = [body](http::Response& r) { body->swap(r.body); };
  host_.pool.submit(std::move(q), [this, body](http::Response&& r) {
    EventsState& ev = impl_->ev;
    ev.raceBusy = false;
    ev.race.onResponse(r.status, *body, time::now());
  });
}

void Store::pumpPhotos(uint32_t ms) {
  EventsState& e = impl_->ev;
  if (!e.photos.draining() || e.photosBusy || ms < e.photosAt) return;
  if (!e.photoPending) {
    auto req = e.photos.nextRequest(time::now(), host_.zone());
    if (!req) {
      savePhotos();
      return;
    }
    e.photoPending = std::move(*req);
    if (e.photoPending->waitSeconds > 0) {
      e.photosAt = ms + static_cast<uint32_t>(e.photoPending->waitSeconds) * 1000u;
      return;
    }
  }
  pp::PlayerPhotos::Request send = std::move(*e.photoPending);
  e.photoPending.reset();
  e.photosBusy = true;
  auto body = std::make_shared<std::string>();
  http::Request q = toHttp(send.request, "events.photo");
  q.afterTransfer = [body](http::Response& r) { body->swap(r.body); };
  host_.pool.submit(std::move(q), [this, body](http::Response&& r) {
    EventsState& ev = impl_->ev;
    ev.photosBusy = false;
    ev.photos.onResponse(r.status, *body, time::now());
    if (ev.photos.persistDue()) savePhotos();
  });
}

// ------------------------------------------------------------ cameras

std::vector<Channel> Store::eventFeedChannels(const std::string& eventId) const {
  const Impl& m = *impl_;
  std::vector<Channel> out;
  auto ev = m.ev.board.events.find(eventId);
  if (ev == m.ev.board.events.end()) return out;
  for (const sports::EventSource& src : ev->second.sources) {
    auto it = m.indexByID.find(src.channelID);
    if (it != m.indexByID.end()) out.push_back((*m.channels)[it->second]);
  }
  return out;
}

bool Store::hasCameras(const std::string& eventId) {
  Impl& m = *impl_;
  auto ev = m.ev.board.events.find(eventId);
  if (ev == m.ev.board.events.end() || m.channels->empty()) return false;
  m.ev.cameraIndex.update(*m.channels, std::to_string(m.catalogRevision) + "|" + std::to_string(m.channels->size()));
  return !m.ev.cameraIndex.cameras(ev->second, eventFeedChannels(eventId)).empty();
}

sports::cameras::CameraPanel Store::cameraPanel(const std::string& eventId, const std::string& playingChannel,
                                                bool opening) {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  cam::CameraPanel panel;
  auto ev = e.board.events.find(eventId);
  if (ev == e.board.events.end() || m.channels->empty()) return panel;
  e.cameraIndex.update(*m.channels, std::to_string(m.catalogRevision) + "|" + std::to_string(m.channels->size()));
  std::vector<sports::special_feeds::CameraFeed> feeds = e.cameraIndex.cameras(ev->second, eventFeedChannels(eventId));
  std::optional<std::string> playing;
  if (!playingChannel.empty()) playing = playingChannel;
  panel = cam::build(ev->second, feeds, playing, e.followed, e.race, e.photos);
  if (opening) {
    time::Seconds now = time::now();
    if (panel.onOpen.raceData) e.race.refreshIfNeeded({}, false, now);
    if (panel.onOpen.nascarDrivers && !e.nascarBusy) {
      if (auto req = e.race.refreshNascarIfNeeded(now)) {
        e.nascarBusy = true;
        auto body = std::make_shared<std::string>();
        http::Request q = toHttp(*req, "events.nascar");
        q.afterTransfer = [body](http::Response& r) { body->swap(r.body); };
        host_.pool.submit(std::move(q), [this, body](http::Response&& r) {
          EventsState& es = impl_->ev;
          es.nascarBusy = false;
          es.race.onNascarResponse(r.status, *body, time::now());
          if (r.ok) host_.files.write(kNascarFile, rd::toJson(es.race.nascarSaved()));
        });
      }
    }
    if (!panel.onOpen.photos.empty()) e.photos.request(panel.onOpen.photos, now);
  }
  return panel;
}

int Store::camerasRevision() const {
  const EventsState& e = impl_->ev;
  return e.race.revision() * 7919 + e.photos.revision() * 31 + e.followRevision;
}

bool Store::toggleFollowDriver(const std::string& name) {
  EventsState& e = impl_->ev;
  bool on = e.followed.toggle(name);
  ++e.followRevision;
  host_.files.write(kFollowedFile, e.followed.toJson());
  return on;
}

std::optional<std::vector<Feed>> Store::cameraFeeds(const std::string& channelId,
                                                    const std::vector<std::string>& alternates,
                                                    const std::string& playingChannel) const {
  const Impl& m = *impl_;
  std::optional<std::string> playing;
  if (!playingChannel.empty()) playing = playingChannel;
  auto isEntry = [&m](const std::string& id) { return m.entryInfo.count(id) > 0; };
  auto versions = [this](const std::string& id) { return versionsOf(id); };
  auto list = cam::watchList(channelId, alternates, playing, isEntry, versions);
  if (!list) return std::nullopt;
  std::vector<Feed> out;
  std::set<std::string> seen;
  for (const std::string& id : *list) {
    std::vector<Feed> part = isEntry(id) ? feeds(id) : feedsFor({id});
    for (Feed& f : part)
      if (seen.insert(f.channelId).second) out.push_back(std::move(f));
  }
  return out;
}

}  // namespace marigold::engine
