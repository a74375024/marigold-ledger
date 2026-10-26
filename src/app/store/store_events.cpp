// Live Events in the store (the "Sports events" parts of LiveTVStore in
// tvOS): the schedule and live scores, asked for only while a Live Events
// screen is open; the rebuild that matches games to channels and provider
// feeds (on a worker); the board snapshot; feeds for a game in automatic
// backup order, starting on the source used last time (6 hours); reminders.
#include <algorithm>
#include <cstdlib>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/store/store.hpp"
#include "app/store/store_impl.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/network_map.hpp"
#include "core/sports/special_feeds.hpp"
#include "platform/logo_cache.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

namespace marigold::app {
namespace {

namespace sb = sports::board;
namespace ss = sports::store;

constexpr const char* kScheduleFile = "sports-schedule.json";
constexpr const char* kMySportsFile = "my-sports.json";
constexpr const char* kRemindersFile = "reminders.json";
constexpr const char* kMemoryFile = "game-sources.json";
constexpr const char* kSeenFile = "sport-feeds.json";
constexpr const char* kResultFile = "live-events.json";
constexpr uint32_t kCheckMs = 15000;       // rebuild / listings check while open
constexpr uint32_t kBoardRefreshMs = 30000;  // clock texts ("in 25 m")

http::Request toHttp(const sources::Request& r, std::string tag) {
  http::Request q;
  q.method = r.method == "GET" ? std::string() : r.method;
  q.url = r.url;
  for (const sources::Header& h : r.headers) q.headers.push_back(h.name + ": " + h.value);
  q.timeoutMs = static_cast<unsigned>(std::max(1, r.timeoutSeconds)) * 1000u;
  q.priority = http::Priority::Normal;
  q.tag = std::move(tag);
  q.maxBytes = 16u << 20;
  return q;
}

uint32_t hexColor(const std::optional<std::string>& hex, uint32_t fallback) {
  if (!hex || hex->empty()) return fallback;
  std::string h = *hex;
  if (h[0] == '#') h = h.substr(1);
  if (h.size() != 6) return fallback;
  char* end = nullptr;
  unsigned long v = std::strtoul(h.c_str(), &end, 16);
  return end && *end == '\0' ? static_cast<uint32_t>(v) : fallback;
}

EventSide toSide(const sb::SideLine& s) {
  EventSide o;
  o.abbr = s.abbreviation;
  o.name = s.name;
  o.country = s.country.value_or("");
  o.logoUrl = s.logoUrl.value_or("");
  o.color = hexColor(s.color, 0x3A3D44);
  o.alt = hexColor(s.altColor, 0xFFFFFF);
  if (s.rank) o.rank = std::to_string(*s.rank);
  if (!s.sets.empty()) o.score = str::join(s.sets, " ");
  else o.score = s.score.value_or("");
  o.home = s.home;
  o.leads = s.leads.value_or(false);
  o.trails = s.leads && !*s.leads;
  o.ball = s.possession;
  std::vector<std::string> photos;
  if (s.photoSmallUrl) photos.push_back(*s.photoSmallUrl);
  if (s.photoUrl) photos.push_back(*s.photoUrl);
  o.photoUrl = logos::LogoCache::chain(photos);
  o.initials = s.initials;
  o.doubles = s.doubles;
  return o;
}

EventTile::Pill toPill(sb::Pill p) {
  switch (p) {
    case sb::Pill::none: return EventTile::Pill::None;
    case sb::Pill::live: return EventTile::Pill::Live;
    case sb::Pill::liveOutlined: return EventTile::Pill::LiveOutline;
    case sb::Pill::delayed: return EventTile::Pill::Delayed;
    case sb::Pill::onNow: return EventTile::Pill::OnNow;
    case sb::Pill::likelyOver: return EventTile::Pill::LikelyOver;
    case sb::Pill::postponed: return EventTile::Pill::Postponed;
    case sb::Pill::ended: return EventTile::Pill::Final;
    case sb::Pill::timeNotListed: return EventTile::Pill::NoTime;
    case sb::Pill::notStarted: return EventTile::Pill::NotStarted;
  }
  return EventTile::Pill::None;
}

// The sport icon key board.cpp gives (SF Symbol names in tvOS) as a glyph
// the bundled font has.
std::string glyphFor(const std::string& icon) {
  if (icon.find("racing") != std::string::npos || icon.find("car") != std::string::npos) return "\xe2\x96\xb6";
  if (icon.find("tennis") != std::string::npos) return "\xe2\x97\x8f";
  if (icon.find("figure") != std::string::npos) return "\xe2\x98\x85";
  return "\xe2\x97\x8f";
}

EventTile toTile(const sb::Tile& t, const sports::SportsEvent* event, const time::Zone& zone) {
  EventTile o;
  o.id = t.id;
  o.pill = toPill(t.pill);
  o.pillText = t.pillText;
  o.status = t.statusText;
  o.statusExtra = t.statusDetail;
  o.statusBold = t.state == sports::SportsEvent::State::upcoming && !t.notStarted;
  o.favorite = t.favorite;
  o.tag = t.tag.value_or("");
  o.tagAlert = t.tagAlert;
  o.league = t.leagueChip.value_or(event ? event->leagueShort : "");
  for (const sb::SideLine& s : t.sides) o.sides.push_back(toSide(s));
  if (t.layout != sb::Tile::Layout::sides) o.sides.clear();
  o.title = t.title;
  o.subtitle = t.boutLine.value_or(t.podiumLabel.value_or(t.subtitle.value_or("")));
  for (const sb::PartLine& p : t.parts) o.parts.push_back({p.text.empty() ? p.label : p.text, p.fill, p.current});
  for (const sb::PodiumDriver& d : t.podium) o.podium.push_back({d.code, hexColor(d.color, 0x8A8D93)});
  o.symbol = glyphFor(t.icon);
  o.channelName = t.channelDisplay.value_or(t.channelName.value_or(""));
  o.channelLogo = t.channelLogos.empty() ? std::string() : t.channelLogos.front();
  o.pill2 = t.footerPill.value_or("");
  o.pill2Outlined = t.footerPillOutlined;
  o.footer = t.footer;
  o.situation = t.situation.value_or("");
  o.redZone = t.redZone;
  o.progress = t.progress.value_or(-1);
  o.watchable = t.watchable;
  o.finished = t.state == sports::SportsEvent::State::finished;
  o.upcoming = !t.selectWatches;
  // Hero / detail lines from the tile and its event.
  std::string chip = event ? event->leagueShort : "";
  if (!o.tag.empty() && !o.tagAlert) chip += chip.empty() ? o.tag : " \xc2\xb7 " + o.tag;
  o.leagueChip = chip;
  if (o.sides.size() == 2 && !o.sides[0].score.empty() && !o.sides[1].score.empty())
    o.heroTitle = o.sides[0].name + " " + o.sides[0].score + " \xe2\x80\x93 " + o.sides[1].score + " " + o.sides[1].name;
  else if (o.sides.size() == 2)
    o.heroTitle = o.sides[0].name + " at " + o.sides[1].name;
  else
    o.heroTitle = o.title;
  o.heroStatus = o.status + (o.statusExtra.empty() ? "" : " " + o.statusExtra);
  if (!o.situation.empty()) o.heroStatus += " \xc2\xb7 " + o.situation;
  o.whereLine = o.channelName.empty() ? o.footer : "On " + o.channelName + (o.footer.empty() ? "" : " \xc2\xb7 " + o.footer);
  if (event && event->start) o.start = *event->start;
  (void)zone;
  return o;
}

}  // namespace

// ------------------------------------------------------------ persistence

void Store::loadEvents() {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  platform::Storage& st = app_.storage();
  time::Seconds now = time::now();
  sports::schedule::Saved saved;
  if (auto text = st.read(kScheduleFile))
    if (auto s = sports::schedule::savedFromJson(*text)) saved = std::move(*s);
  e.schedule = sports::schedule::Schedule(std::move(saved), app_.build().build);
  if (auto text = st.read(kMySportsFile))
    if (auto p = sports::mySportsFromJson(*text)) e.mySports = *p;
  if (auto text = st.read(kRemindersFile))
    if (auto r = sports::Reminders::fromJson(*text)) e.reminders = sports::Reminders(std::move(*r), now);
  if (auto text = st.read(kMemoryFile)) e.memory = ss::SourceMemory(ss::SourceMemory::fromJson(*text));
  if (auto text = st.read(kSeenFile))
    if (auto v = json::parse(*text)) e.seen = sports::sportFeedsSeenFromJson(*v);
  loadRace();
}

void Store::saveEvents() {
  EventsState& e = impl_->ev;
  platform::Storage& st = app_.storage();
  if (e.schedule.takeDirty()) {
    std::string text = sports::schedule::toJson(e.schedule.saved());
    std::string path = st.path(kScheduleFile);
    app_.pool().post([path, text] {
      std::FILE* f = std::fopen((path + ".tmp").c_str(), "wb");
      if (!f) return;
      std::fwrite(text.data(), 1, text.size(), f);
      std::fclose(f);
      std::rename((path + ".tmp").c_str(), path.c_str());
    }, nullptr, http::Priority::Background);
  }
  st.write(kRemindersFile, e.reminders.toJson());
  st.write(kMemoryFile, e.memory.toJson());
  st.write(kSeenFile, json::write(sports::toJson(e.seen)));
}

sports::schedule::Context Store::scheduleContext() const {
  sports::schedule::Context ctx;
  ctx.prefs = impl_->ev.mySports;
  ctx.seen = impl_->ev.seen;
  ctx.enabled = settings_.scheduleEnabled;
  ctx.now = time::now();
  ctx.zone = app_.zone();
  return ctx;
}

// ------------------------------------------------------------ opening

void Store::eventsOpen(bool open) {
  EventsState& e = impl_->ev;
  int before = eventsOpen_;
  eventsOpen_ = std::max(0, eventsOpen_ + (open ? 1 : -1));
  if (open && before == 0) {
    // Opened: new sport feeds seen, the schedule if due, scores, listings.
    if (sports::noteSportFeeds(e.seen, e.seenCheckedAt, impl_->catalog->events, time::now()))
      e.schedule.leaguesChanged(scheduleContext());
    e.schedule.refreshIfNeeded(scheduleContext());
    if (e.scores.start(sports::scores::Holder::board, settings_.scheduleEnabled)) e.scoresAt = app_.ms();
    if (ss::listingsNeedRead(e.listingsAt, 0, 0, time::now())) e.listingsAt.reset();
    e.nextCheckMs = 0;
    e.boardDirty = true;
  } else if (!open && eventsOpen_ == 0) {
    if (e.scores.stop(sports::scores::Holder::board)) {
      e.scores.cancelRound();
      e.scoresRound = false;
      e.scoresPending.reset();
    }
  }
}

// ------------------------------------------------------------ requests

void Store::pumpSchedule(uint32_t ms) {
  EventsState& e = impl_->ev;
  if (e.scheduleBusy) return;
  if (!e.schedulePending) {
    e.schedulePending = e.schedule.nextRequest();
    if (!e.schedulePending) {
      if (auto done = e.schedule.takeFinished()) {
        MLOG_I("events", "schedule: %s", done->note.c_str());
        ++e.scheduleRevision;
        e.nextCheckMs = 0;
        if (done->recheck) e.schedule.leaguesChanged(scheduleContext());
        saveEvents();
        releaseRosters();
      }
      return;
    }
    e.scheduleAt = ms + static_cast<uint32_t>(std::max(0, e.schedulePending->waitMs));
  }
  if (ms < e.scheduleAt) return;
  e.scheduleBusy = true;
  auto pending = std::make_shared<sports::schedule::Schedule::Pending>(*e.schedulePending);
  e.schedulePending.reset();
  auto answer = std::make_shared<sports::schedule::Schedule::Answer>();
  http::Request q = toHttp(pending->request, "events.schedule");
  q.afterTransfer = [pending, answer](http::Response& r) {
    if (r.status == 200) *answer = sports::schedule::Schedule::parse(*pending, r.body);
    r.body.clear();
  };
  uint64_t gen = impl_->generation;
  app_.pool().submit(std::move(q), [this, gen, answer](http::Response&& r) {
    EventsState& ev = impl_->ev;
    ev.scheduleBusy = false;
    if (gen != impl_->generation) return;
    ev.schedule.onAnswer(r.status, std::move(*answer));
  });
}

void Store::pumpScores(uint32_t ms) {
  EventsState& e = impl_->ev;
  if (!e.scores.looking() || e.scoresBusy || ms < e.scoresAt) return;
  time::Seconds now = time::now();
  if (!e.scoresRound) {
    sports::scores::RoundInput in;
    in.games = e.schedule.publish(scheduleContext()).games;
    in.watching = app_.playback().active() && app_.playback().full();
    in.showsScores = settings_.showScores;
    in.now = now;
    in.zone = app_.zone();
    for (const auto& [id, tile] : e.board.tiles) {
      auto ev = e.board.events.find(id);
      if (ev != e.board.events.end() && ev->second.game && ev->second.game->isTennis() && tile.watchable)
        in.watchableTennis.insert(ev->second.game->id);
    }
    e.scores.beginRound(in);
    e.scoresRound = true;
  }
  auto req = e.scores.nextRequest(now);
  if (!req) {
    e.scoresRound = false;
    e.scoresAt = ms + static_cast<uint32_t>(e.scores.waitSeconds()) * 1000u;
    if (e.scores.revision() != e.statusesRevision) e.boardDirty = true;
    return;
  }
  if (req->waitMs > 0 && !e.scoresPending) {
    e.scoresPending = *req;
    e.scoresAt = ms + static_cast<uint32_t>(req->waitMs);
    return;
  }
  sports::scores::LiveScores::Request send = e.scoresPending ? *e.scoresPending : *req;
  e.scoresPending.reset();
  e.scoresBusy = true;
  auto body = std::make_shared<std::string>();
  http::Request q = toHttp(send.request, "events.scores");
  q.afterTransfer = [body](http::Response& r) { body->swap(r.body); };
  app_.pool().submit(std::move(q), [this, body](http::Response&& r) {
    EventsState& ev = impl_->ev;
    ev.scoresBusy = false;
    ev.scores.onResponse(r.status, *body, time::now());
    if (ev.scores.revision() != ev.statusesRevision) ev.boardDirty = true;
  });
}

// ------------------------------------------------------------ rebuild

void Store::rebuildEvents() {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  if (e.rebuilding || m.channels->empty()) return;
  time::Seconds now = time::now();
  sports::schedule::Context ctx = scheduleContext();
  sports::schedule::Published pub = e.schedule.publish(ctx);
  std::vector<sports::League> active = sports::activeLeagues(e.mySports, e.seen, now);
  std::vector<sports::matcher::RosterLeague> rosters = ss::rosterLeagues(active, pub.rosters);

  // Read the guide's game listings when due (sports, local and Spanish
  // channels of the lineup and the browse list).
  std::unordered_map<std::string, EntryInfo> infos;
  for (const EntryInfo& info : m.catalog->entries) infos.emplace(info.id, info);
  sports::StationMarkets markets = sports::stationMarkets(m.prefs);
  if (!e.listingsAt || now - *e.listingsAt > ss::kListingsStale) {
    std::vector<std::string> browse;
    for (const Channel& c : m.lineup.browseEntries) browse.push_back(c.id);
    std::vector<ss::ListingChannel> chans = ss::listingChannels(m.lineup.lineupIDs, browse, infos, markets);
    std::vector<ss::ListingInput> inputs;
    for (const ss::ListingChannel& lc : chans) {
      ss::ListingInput in;
      in.channel = lc;
      auto p = m.programs.find(lc.id);
      if (p != m.programs.end()) in.programs = &p->second;
      inputs.push_back(in);
    }
    ss::ExtractedListings ex = ss::extractListings(inputs, now);
    e.listings = std::move(ex.listings);
    e.listingsAt = now;
    ++e.listingsRevision;
  }

  ss::Fingerprint fp;
  fp.eventsRevision = m.catalogRevision;
  fp.scheduleRevision = e.scheduleRevision;
  fp.entriesRevision = m.lineupRevision;
  fp.listingsRevision = e.listingsRevision;
  fp.preferSpanish = m.prefs.countries.count("US-ES") > 0;
  fp.enabled = settings_.scheduleEnabled;
  fp.schools = pub.schools.size();
  std::string key = ss::fingerprint(fp, now);
  if (key == e.lastFingerprint) return;
  e.lastFingerprint = key;

  auto in = std::make_shared<ss::RebuildInput>();
  in->games = std::move(pub.games);
  in->schools = std::move(pub.schools);
  in->rosters = rosters;
  in->preferSpanish = fp.preferSpanish;
  in->events = m.catalog->events;
  in->traits = m.catalog->traits;
  in->listings = e.listings;
  in->previous = e.events;
  in->channelIndex = sports::network_map::channelIndex(m.lineup.lineupIDs, m.lineup.lineupEntries, m.lineup.browseEntries,
                                                       m.lineup.entries, infos, {}, rosters, markets);
  in->now = now;
  in->zone = app_.zone();
  auto channels = m.channels;
  auto index = std::make_shared<std::unordered_map<std::string, size_t>>(m.indexByID);
  in->channelExists = [index](const std::string& id) { return index->count(id) > 0; };
  in->isCameraName = sports::special_feeds::isCameraName;
  // F1 sessions are also on the F1 channels, while the schedule has one.
  if (sports::special_feeds::wantsF1Sources(in->games)) {
    std::set<std::string> lineupIds(m.lineup.lineupIDs.begin(), m.lineup.lineupIDs.end());
    std::string f1Key = sports::special_feeds::f1ChannelsKey(m.lineupRevision, m.lineup.entries.size(), lineupIds,
                                                           m.hiddenGroups);
    if (f1Key != e.f1Key) {
      e.f1Key = f1Key;
      e.f1Sources = sports::special_feeds::f1ChannelSources(m.lineup.entries, infos, m.hiddenGroups, "Other", lineupIds);
    }
    in->f1Sources = e.f1Sources;
  }
  auto stop = m.stopJobs;
  in->shouldStop = [stop]() -> std::optional<std::string> {
    if (stop->load()) return std::string("closing");
    return std::nullopt;
  };
  e.rebuilding = true;
  auto out = std::make_shared<std::optional<ss::RebuildResult>>();
  uint64_t gen = m.generation;
  app_.pool().post([in, out] { *out = ss::rebuild(*in); },
                   [this, gen, out] {
                     EventsState& ev = impl_->ev;
                     ev.rebuilding = false;
                     if (gen != impl_->generation || !*out) return;
                     ev.events = std::move((*out)->events);
                     ev.schedule.noteWatchable((*out)->watchableLeagues, time::now());
                     ev.boardDirty = true;
                     MLOG_I("events", "rebuilt: %zu events", ev.events.size());
                     // Saved for an instant board next launch.
                     std::vector<sports::matcher::GuideListing> listings = ev.listings;
                     if (auto saved = ss::resultToSave(ev.events, impl_->catalog->events, listings, true, time::now(),
                                                       ev.lastSaved))
                       app_.storage().write(kResultFile, ss::toJson(*saved));
                   },
                   http::Priority::Normal);
}

// ------------------------------------------------------------ board

namespace {

sb::ChannelFor makeChannelFor(const Store& store, const Store::Impl& m, std::shared_ptr<const Channels> ch) {
  return [&m, ch](const sports::EventSource& src) -> std::optional<sb::ChannelLabel> {
    if (const GuideChannel* g = ch->find(src.channelID)) {
      sb::ChannelLabel l;
      l.id = g->id;
      l.name = g->name;
      l.display = g->name;
      if (!g->logoUrl.empty()) l.logoUrls.push_back(g->logoUrl);
      return l;
    }
    auto it = m.indexByID.find(src.channelID);
    if (it == m.indexByID.end()) return std::nullopt;
    const Channel& c = (*m.channels)[it->second];
    sb::ChannelLabel l;
    l.id = c.id;
    l.name = c.displayName();
    l.display = c.displayName();
    if (!c.imageUrl.empty()) l.logoUrls.push_back(c.imageUrl);
    return l;
  };
  (void)store;
}

}  // namespace

void Store::buildBoard() {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  time::Seconds now = time::now();
  sb::Input in;
  in.events = e.events;
  in.families = m.catalog->families;
  in.prefs = e.mySports;
  in.statuses = e.scores.statuses();
  in.finalSeen = e.scores.finalSeen();
  in.whips = sb::whipChannels(m.lineup.entries, m.lineup.lineupIDs);
  in.now = now;
  in.zone = app_.zone();
  in.settings.showsUnwatchable = settings_.showUnwatchable;
  in.settings.showsScores = settings_.showScores;
  in.settings.twelveHour = !settings_.clock24;
  in.channelFor = makeChannelFor(*this, m, channels_);
  in.isCameraName = sports::special_feeds::isCameraName;
  in.cameraRace = sports::special_feeds::cameraRace;
  in.podium = e.race.podiumHook();
  in.playerPhoto = e.photos.photoHook();
  e.raceSeen = e.race.revision();
  e.photosSeen = e.photos.revision();
  e.board = sb::build(in);
  // Race weekends on the board ask for drivers and results; tennis players
  // for their photos (spaced and capped by the rules behind these calls).
  sports::race_data::refreshForBoard(e.race, e.board, settings_.showScores, now);
  e.photos.request(e.board.tennisPlayers, now);
  e.statusesRevision = e.scores.revision();
  e.boardDirty = false;
  e.boardBuiltMs = app_.ms();

  auto out = std::make_shared<EventsBoard>();
  out->version = events_->version + 1;
  out->loading = e.schedule.fetching() && e.events.empty();
  out->scoresOn = settings_.showScores;
  for (const sb::Row& r : e.board.rows) {
    EventRow row;
    row.id = r.id;
    row.title = r.title;
    row.live = r.live;
    row.count = r.count;
    row.families = r.id == "families";
    for (const sb::Group& g : r.groups) {
      EventGroup group;
      group.title = g.label;
      for (const sb::Tile& t : g.tiles) {
        auto ev = e.board.events.find(t.id);
        group.tiles.push_back(toTile(t, ev == e.board.events.end() ? nullptr : &ev->second, app_.zone()));
      }
      if (!group.tiles.empty()) row.groups.push_back(std::move(group));
    }
    // Feed families ("Event channels"): a tile each, Up/Down to surf.
    if (!r.families.empty()) {
      EventGroup group;
      for (const sb::FamilyTile& f : r.families) {
        EventTile t;
        t.id = f.id;
        t.title = f.name;
        t.subtitle = f.countText;
        t.league = f.leagueChip;
        t.footer = f.hint;
        t.watchable = true;
        t.symbol = "\xe2\x96\xa0";
        t.leagueChip = f.leagueChip;
        t.heroTitle = f.name;
        t.heroStatus = f.countText;
        group.tiles.push_back(std::move(t));
      }
      row.groups.push_back(std::move(group));
    }
    if (!row.groups.empty()) out->rows.push_back(std::move(row));
  }
  events_ = out;
}

void Store::eventsTick(uint32_t ms) {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  // Reminders: checked every 15 s anywhere in the app.
  if (ms >= e.nextReminderMs) {
    e.nextReminderMs = ms + sports::Reminders::kCheckSeconds * 1000u;
    sports::Reminders::Check c = e.reminders.check(time::now());
    if (c.changed) saveEvents();
    if (c.due) {
      dueReminder_ = DueReminder{c.due->channelID, c.due->channelName, c.due->title, c.due->start};
      dueReminderAt_ = ms;
    }
  }
  pumpRace(ms);
  pumpPhotos(ms);
  if (eventsOpen_ <= 0 && !e.events.empty()) return;
  if (m.channels->empty()) return;
  if (eventsOpen_ > 0) {
    pumpSchedule(ms);
    pumpScores(ms);
  }
  if (!e.restoredTried) {
    e.restoredTried = true;
    if (auto text = app_.storage().read(kResultFile)) {
      if (auto saved = ss::savedResultFromJson(*text)) {
        if (auto r = ss::restore(*saved, m.catalog->events, time::now())) {
          e.events = std::move(r->events);
          if (e.listings.empty()) e.listings = std::move(r->listings);
          e.boardDirty = true;
        }
      }
    }
  }
  if (ms >= e.nextCheckMs) {
    e.nextCheckMs = ms + kCheckMs;
    rebuildEvents();
  }
  if (e.boardDirty || (eventsOpen_ > 0 && ms - e.boardBuiltMs > kBoardRefreshMs) ||
      e.scores.revision() != e.statusesRevision)
    buildBoard();
}

// ------------------------------------------------------------ lookups

bool Store::isEvent(const std::string& id) const { return impl_->ev.board.events.count(id) > 0; }

const EventTile* Store::eventTile(const std::string& id) const {
  for (const EventRow& r : events_->rows)
    for (const EventGroup& g : r.groups)
      for (const EventTile& t : g.tiles)
        if (t.id == id) return &t;
  return nullptr;
}

WhereToWatch Store::whereToWatch(const std::string& eventId) const {
  const Impl& m = *impl_;
  WhereToWatch w;
  auto ev = m.ev.board.events.find(eventId);
  if (ev == m.ev.board.events.end()) return w;
  sb::WherePanel p = sb::wherePanel(m.ev.board, ev->second, makeChannelFor(*this, m, channels_),
                                    [this](const std::string& feed) {
                                      std::vector<Feed> f = feedsFor({feed});
                                      return f.empty() ? std::string() : f.front().label;
                                    });
  w.title = p.title;
  for (const sb::WhereSection& s : p.sections) {
    std::vector<WatchSource> list;
    for (const sb::SourceItem& it : s.items) {
      WatchSource ws;
      ws.title = it.name;
      ws.detail = it.detail;
      ws.spanish = it.spanish;
      if (m.entryInfo.count(it.channelID)) ws.entryId = it.channelID;
      else ws.channelId = it.channelID;
      list.push_back(std::move(ws));
    }
    w.sections.push_back({s.title, std::move(list)});
  }
  w.note = p.moreStations.value_or("");
  return w;
}

std::vector<std::string> Store::familyFeeds(const std::string& familyId) const {
  for (const catalog::EventFamily& f : impl_->catalog->families) {
    if (f.id != familyId) continue;
    std::vector<std::string> out;
    for (const std::string& id : f.feeds)
      if (impl_->indexByID.count(id)) out.push_back(id);
    return out;
  }
  return {};
}

std::vector<std::string> Store::eventFlipList(const std::string& eventId) const {
  std::vector<std::string> list = sb::flipEvents(impl_->ev.board, eventId, time::now());
  if (list.empty()) list.push_back(eventId);
  return list;
}

std::vector<std::string> Store::eventBackups(const std::string& entryId) const {
  const EventsState& e = impl_->ev;
  if (e.events.empty()) return {};
  return ss::eventBackupFeeds(entryId, e.events, ss::backupIndex(e.events), time::now());
}

std::vector<Feed> Store::eventFeeds(const std::string& eventId, const std::string& via) const {
  const Impl& m = *impl_;
  const EventsState& e = m.ev;
  auto ev = e.board.events.find(eventId);
  if (ev == e.board.events.end()) return {};
  ss::Resolver r;
  r.sourceChannel = [&m, this](const sports::EventSource& src) -> std::optional<ss::ChannelRef> {
    if (const GuideChannel* g = channels_->find(src.channelID)) return ss::ChannelRef{g->id, g->name};
    auto it = m.indexByID.find(src.channelID);
    if (it == m.indexByID.end()) return std::nullopt;
    return ss::ChannelRef{src.channelID, (*m.channels)[it->second].displayName()};
  };
  r.baseVersions = [this](const std::string& id) { return versionsOf(id); };
  r.bestFeeds = [this, &m](const std::vector<std::string>& ids) {
    return ss::bestFeeds(
        ids,
        [&m](const std::string& id) {
          auto t = m.catalog->traits.find(id);
          return t == m.catalog->traits.end() ? 1 : t->second.quality;
        },
        [this](const std::string& id) { return reliabilityOf(id); });
  };
  r.isEntry = [&m](const std::string& id) { return m.entryInfo.count(id) > 0; };
  std::optional<std::string> preferring;
  if (!via.empty()) preferring = via;
  ss::WatchPlan plan = ss::watchPlan(ev->second, preferring, e.memory.remembered(ev->second, time::now()), r);
  if (plan.empty) return {};
  if (plan.entry) {
    // A guide channel picked: its own versions (and the game's feeds as
    // backups through eventBackups).
    std::vector<std::string> members = versionsOf(*plan.entry);
    for (const std::string& c : plan.candidates)
      if (std::find(members.begin(), members.end(), c) == members.end()) members.push_back(c);
    return feedsFor(members);
  }
  return feedsFor(plan.candidates);
}

void Store::noteEventFeed(const std::string& eventId, const std::string& channelId, bool sawPicture) {
  // Leaving or switching a game: remember its source for 6 hours.
  EventsState& e = impl_->ev;
  auto ev = e.board.events.find(eventId);
  if (ev == e.board.events.end() || channelId.empty() || !sawPicture) return;
  if (e.memory.remember(ev->second, channelId, time::now())) saveEvents();
}

bool Store::hasReminder(const std::string& id) const {
  const EventsState& e = impl_->ev;
  auto ev = e.board.events.find(id);
  if (ev == e.board.events.end() || !ev->second.start) return false;
  for (const sports::LiveReminder& r : e.reminders.items())
    if (r.start == *ev->second.start && r.title == ev->second.title) return true;
  return false;
}

bool Store::toggleReminder(const std::string& id) {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  auto ev = e.board.events.find(id);
  if (ev == e.board.events.end()) return false;
  ss::Resolver r;
  r.sourceChannel = [&m, this](const sports::EventSource& src) -> std::optional<ss::ChannelRef> {
    if (const GuideChannel* g = channels_->find(src.channelID)) return ss::ChannelRef{g->id, g->name};
    auto it = m.indexByID.find(src.channelID);
    if (it == m.indexByID.end()) return std::nullopt;
    return ss::ChannelRef{src.channelID, (*m.channels)[it->second].displayName()};
  };
  r.baseVersions = [this](const std::string& c) { return versionsOf(c); };
  r.bestFeeds = [](const std::vector<std::string>& ids) { return ids; };
  r.isEntry = [&m](const std::string& c) { return m.entryInfo.count(c) > 0; };
  auto target = ss::reminderTarget(ev->second, r);
  if (!target) return false;
  bool was = e.reminders.isSet(target->channelID, target->start);
  e.reminders.toggle(*target);
  saveEvents();
  return !was;
}

void Store::dismissReminder() {
  impl_->ev.reminders.dismiss();
  dueReminder_.reset();
}

const sports::MySportsPrefs& Store::mySports() const { return impl_->ev.mySports; }

void Store::setMySports(const sports::MySportsPrefs& prefs) {
  EventsState& e = impl_->ev;
  sports::MySportsChange change = sports::compare(e.mySports, prefs);
  e.mySports = prefs;
  app_.storage().write(kMySportsFile, sports::toJson(prefs));
  if (change.leaguesChanged) e.schedule.leaguesChanged(scheduleContext());
  e.lastFingerprint.clear();
  e.nextCheckMs = 0;
  e.boardDirty = true;
}

std::vector<sports::SportsTeam> Store::teamsOf(const std::string& leagueShort) const {
  const sports::League* l = sports::leagueByShort(leagueShort);
  if (!l) return {};
  // The league's whole list, with the schedule's (fresher) copies on top.
  std::map<std::string, sports::SportsTeam> byId;
  auto full = impl_->ev.leagueTeams.find(leagueShort);
  if (full != impl_->ev.leagueTeams.end())
    for (const sports::SportsTeam& t : full->second) byId[t.id] = t;
  for (const sports::SportsTeam& t : impl_->ev.schedule.teams(*l)) byId[t.id] = t;
  std::vector<sports::SportsTeam> out;
  out.reserve(byId.size());
  for (auto& [id, t] : byId) out.push_back(std::move(t));
  return out;
}

int Store::teamsRevision() const { return impl_->ev.teamsRevision; }

void Store::requestRosters(const std::set<std::string>& shorts) {
  EventsState& e = impl_->ev;
  time::Seconds now = time::now();
  for (const std::string& id : shorts) {
    const sports::League* l = sports::leagueByShort(id);
    if (!l || l->college() || l->fights || l->racing || l->tennis || e.leagueTeamsBusy.count(id)) continue;
    auto at = e.leagueTeamsAt.find(id);
    if (at != e.leagueTeamsAt.end() && now - at->second < 12 * 3600) continue;
    e.leagueTeamsBusy.insert(id);
    auto teams = std::make_shared<std::optional<std::vector<sports::SportsTeam>>>();
    http::Request q = toHttp(sports::schedule::teamsRequest(*l), "events.teams");
    q.afterTransfer = [teams](http::Response& r) {
      if (r.ok && r.status == 200) *teams = sports::schedule::parseTeams(r.body);
    };
    app_.pool().submit(std::move(q), [this, id, teams](http::Response&&) {
      EventsState& ev = impl_->ev;
      ev.leagueTeamsBusy.erase(id);
      ev.leagueTeamsAt[id] = time::now();
      if (!*teams || (*teams)->empty()) {
        MLOG_W("events", "teams for %s: couldn't load", id.c_str());
        return;
      }
      MLOG_I("events", "teams for %s: %zu", id.c_str(), (*teams)->size());
      ev.leagueTeams[id] = std::move(**teams);
      ++ev.teamsRevision;
    });
  }
  impl_->ev.schedule.requestRosters(shorts, scheduleContext());
  ++eventsOpen_;  // the setup screen drives the fetch like the board
  impl_->ev.rostersHeld = true;
}

void Store::releaseRosters() {
  if (impl_->ev.rostersHeld) {
    impl_->ev.rostersHeld = false;
    eventsOpen_ = std::max(0, eventsOpen_ - 1);
  }
}

std::map<std::string, int> Store::sportSuggestions() const { return sports::suggestionCounts(impl_->catalog->events); }

void Store::refreshSchedule() {
  impl_->ev.schedule.refreshNow(scheduleContext());
  ++eventsOpen_;
  impl_->ev.rostersHeld = true;  // released when the fetch finishes
}

}  // namespace marigold::app
