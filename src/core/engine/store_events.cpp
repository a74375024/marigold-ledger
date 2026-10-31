// Live Events in the store (the "Sports events" parts of LiveTVStore in
// tvOS): the schedule and live scores, asked for only while a Live Events
// screen is open; the rebuild that matches games to channels and provider
// feeds (on a worker); the board snapshot; feeds for a game in automatic
// backup order, starting on the source used last time (6 hours); reminders.
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "core/engine/store.hpp"
#include "core/engine/store_impl.hpp"
#include "core/catalog/catalog.hpp"
#include "core/report/live_events_report.hpp"
#include "core/sports/game_banner.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/network_map.hpp"
#include "core/sports/special_feeds.hpp"
#include "core/util/log.hpp"
#include "core/util/strings.hpp"

namespace marigold::engine {

// A sport icon's name on the tile's top line; empty for "other" (not known).
std::string tileSport(const std::string& icon) {
  static const std::unordered_map<std::string, std::string> kWords = {
      {"athletics", "ATHLETICS"}, {"baseball", "BASEBALL"},     {"basketball", "BASKETBALL"},
      {"cricket", "CRICKET"},     {"cycling", "CYCLING"},       {"darts", "DARTS"},
      {"fieldHockey", "FIELD HOCKEY"}, {"fighting", "FIGHTING"}, {"football", "FOOTBALL"},
      {"golf", "GOLF"},           {"hockey", "HOCKEY"},         {"horseRacing", "HORSE RACING"},
      {"lacrosse", "LACROSSE"},   {"motorsport", "MOTORSPORT"}, {"rugby", "RUGBY"},
      {"soccer", "SOCCER"},       {"softball", "SOFTBALL"},     {"tennis", "TENNIS"},
      {"volleyball", "VOLLEYBALL"}, {"wrestling", "WRESTLING"}};
  auto it = kWords.find(icon);
  return it == kWords.end() ? std::string() : it->second;
}

// The competition after the sport: the league's name where it says more
// than the sport ("ATP", "Premier League", "Women's Division II"), then the
// tile's tag ("3rd Test", "Swiss Indoors Basel · R2").
std::string tileCompetition(const sports::SportsEvent* event, const std::string& sport, const std::string& tag) {
  std::string name;
  if (event) {
    if (const sports::League* league = sports::leagueByShort(event->leagueShort)) name = league->name;
    std::string lower = str::lower(name);
    if (lower == str::lower(sport) || lower == str::lower(event->league) || lower == "other") name.clear();
  }
  // A tag that starts with the sport ("SOCCER · WOMEN'S") doesn't repeat it.
  std::string rest = tag;
  std::string lead = str::lower(sport) + " \xc2\xb7 ";
  if (!sport.empty() && str::startsWith(str::lower(rest), lead)) rest = rest.substr(lead.size());
  if (!sport.empty() && str::lower(rest) == str::lower(sport)) rest.clear();
  if (rest.empty() || str::lower(rest) == str::lower(name)) return name;
  return name.empty() ? rest : name + " \xc2\xb7 " + rest;
}

std::string matchup(const EventTile& t) {
  if (t.sides.size() != 2) return t.title;
  return t.sides[0].name + (t.sides[1].home && !t.sides[0].home ? " at " : " vs ") + t.sides[1].name;
}

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
// Opened with events older than this: "Updating" until the fresh ones are in.
constexpr time::Seconds kBoardStaleAfter = 30 * 60;
constexpr uint32_t kUpdatingAtMostMs = 60000;

// "4 min", "9 h", "2 days": how old the board is, for the log.
std::string ageText(time::Seconds s) {
  if (s < 3600) return std::to_string(std::max<time::Seconds>(1, s / 60)) + " min";
  if (s < 2 * 86400) return std::to_string(s / 3600) + " h";
  return std::to_string(s / 86400) + " days";
}

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
  if (s.pens) o.pens = std::to_string(*s.pens);
  o.leads = s.leads.value_or(false);
  o.trails = s.leads && !*s.leads;
  o.ball = s.possession;
  o.flags = s.flags;
  o.plain = s.plain;
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
    case sb::Pill::notLive: return EventTile::Pill::NotLive;
  }
  return EventTile::Pill::None;
}

// A glyph the bundled font has for a sport icon, until the icons load.
std::string glyphFor(const std::string& icon) {
  if (icon == "motorsport") return "\xe2\x96\xb6";
  for (const char* figure : {"golf", "rugby", "cricket", "fighting", "lacrosse", "fieldHockey", "athletics", "cycling"}) {
    if (icon == figure) return "\xe2\x98\x85";
  }
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
  for (const auto& l : t.leaders) o.leaders.push_back({l.pos, l.name, l.flag, l.score, l.thru});
  o.icon = t.icon;
  o.symbol = glyphFor(t.icon);
  o.sport = tileSport(t.icon);
  o.competition = tileCompetition(event, o.sport, o.tagAlert ? std::string() : o.tag);
  o.highSchool = event && event->highSchool;
  o.detail = t.detail;
  o.channelName = t.channelDisplay.value_or(t.channelName.value_or(""));
  o.channelLogo = t.channelLogos.empty() ? std::string() : t.channelLogos.front();
  o.pill2 = t.footerPill.value_or("");
  o.pill2Outlined = t.footerPillOutlined;
  o.footer = t.footer;
  if (t.bases && t.bases->size() == 3) o.bases = *t.bases;
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
  else
    o.heroTitle = matchup(o);
  o.heroStatus = o.status + (o.statusExtra.empty() ? "" : " " + o.statusExtra);
  if (!o.situation.empty()) o.heroStatus += " \xc2\xb7 " + o.situation;
  o.whereLine = o.channelName.empty() ? o.footer : "On " + o.channelName + (o.footer.empty() ? "" : " \xc2\xb7 " + o.footer);
  if (event && event->start) o.start = *event->start;
  if (t.state == sports::SportsEvent::State::upcoming && !t.notStarted && o.start) {
    time::Seconds until = o.start - time::now();
    o.soon = until > 0 && until <= 3600;
  }
  (void)zone;
  return o;
}

}  // namespace

// ------------------------------------------------------------ persistence

void Store::loadEvents() {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  Files& st = host_.files;
  time::Seconds now = time::now();
  sports::schedule::Saved saved;
  if (auto text = st.read(kScheduleFile))
    if (auto s = sports::schedule::savedFromJson(*text)) saved = std::move(*s);
  e.schedule = sports::schedule::Schedule(std::move(saved), host_.build);
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
  Files& st = host_.files;
  if (e.schedule.takeDirty()) {
    std::string text = sports::schedule::toJson(e.schedule.saved());
    std::string path = st.path(kScheduleFile);
    host_.pool.post([path, text] {
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
  ctx.zone = host_.zone();
  return ctx;
}

// ------------------------------------------------------------ opening

void Store::eventsOpen(bool open) {
  EventsState& e = impl_->ev;
  int before = eventsOpen_;
  eventsOpen_ = std::max(0, eventsOpen_ + (open ? 1 : -1));
  if (!open && eventsOpen_ == 0) impl_->eventsUsedAt = time::now();
  if (open && before == 0) {
    liveEventsOpened();
    // An old board (saved last time, or built hours ago) says it's updating.
    time::Seconds now = time::now();
    e.checkedSinceOpen = false;
    e.noteBoard = true;
    if (e.events.empty() || !e.eventsBuiltAt) {
      e.logNotes.push_back("opened; no board yet");
    } else {
      time::Seconds age = std::max<time::Seconds>(0, now - e.eventsBuiltAt);
      e.updating = age > kBoardStaleAfter;
      e.updatingUntilMs = host_.ms() + kUpdatingAtMostMs;
      e.logNotes.push_back("opened; the board is from " + ageText(age) + " ago" + (e.updating ? "; updating" : ""));
    }
    // Opened: new sport feeds seen, the schedule if due, scores, listings.
    if (sports::noteSportFeeds(e.seen, e.seenCheckedAt, impl_->catalog->events, time::now()))
      e.schedule.leaguesChanged(scheduleContext());
    e.schedule.refreshIfNeeded(scheduleContext());
    if (e.scores.start(sports::scores::Holder::board, settings_.scheduleEnabled)) e.scoresAt = host_.ms();
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
  host_.pool.submit(std::move(q), [this, gen, answer](http::Response&& r) {
    EventsState& ev = impl_->ev;
    ev.scheduleBusy = false;
    if (gen != impl_->generation) return;
    ev.schedule.onAnswer(r.status, std::move(*answer), r.retryAfterS);
  });
}

void Store::pumpScores(uint32_t ms) {
  EventsState& e = impl_->ev;
  if (!e.scores.looking() || e.scoresBusy || ms < e.scoresAt) return;
  time::Seconds now = time::now();
  if (!e.scoresRound) {
    sports::scores::RoundInput in;
    in.games = e.schedule.publish(scheduleContext()).games;
    in.watching = host_.playingFull();
    in.showsScores = settings_.showScores;
    in.now = now;
    in.zone = host_.zone();
    for (const auto& [id, tile] : e.board.tiles) {
      auto ev = e.board.events.find(id);
      if (ev == e.board.events.end() || !ev->second.game) continue;
      const sports::League* league = sports::leagueByShort(ev->second.game->leagueShort);
      if ((ev->second.game->isTennis() && tile.watchable) || (league && league->everyGame))
        in.watchableListed.insert(ev->second.game->id);
    }
    e.scores.beginRound(in);
    e.scoresRound = true;
  }
  // A request waiting out its spacing goes first: the round is awaiting
  // its answer, so it has no other.
  std::optional<sports::scores::LiveScores::Request> req = std::move(e.scoresPending);
  e.scoresPending.reset();
  if (!req) {
    req = e.scores.nextRequest(now);
    if (!req) {
      e.scoresRound = false;
      e.scoresAt = ms + static_cast<uint32_t>(e.scores.waitSeconds()) * 1000u;
      if (e.scores.revision() != e.statusesRevision) e.boardDirty = true;
      return;
    }
    if (req->waitMs > 0) {
      e.scoresPending = std::move(req);
      e.scoresAt = ms + static_cast<uint32_t>(e.scoresPending->waitMs);
      return;
    }
  }
  sports::scores::LiveScores::Request send = std::move(*req);
  e.scoresBusy = true;
  auto body = std::make_shared<std::string>();
  http::Request q = toHttp(send.request, "events.scores");
  q.afterTransfer = [body](http::Response& r) { body->swap(r.body); };
  host_.pool.submit(std::move(q), [this, body](http::Response&& r) {
    EventsState& ev = impl_->ev;
    ev.scoresBusy = false;
    ev.scores.onResponse(r.status, *body, time::now(), r.retryAfterS);
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
  // Every row by id: made only when it's needed (listings due, or something
  // changed), not on every check.
  std::unordered_map<std::string, EntryInfo> infos;
  auto needInfos = [&] {
    if (!infos.empty()) return;
    infos.reserve(m.catalog->entries.size());
    for (const EntryInfo& info : m.catalog->entries) infos.emplace(info.id, info);
  };
  sports::StationMarkets markets = sports::stationMarkets(m.prefs);
  if (!e.listingsAt || now - *e.listingsAt > ss::kListingsStale) {
    needInfos();
    std::vector<std::string> browse;
    for (const Channel& c : m.lineup.browseEntries) browse.push_back(c.id);
    std::vector<ss::ListingChannel> chans = ss::listingChannels(m.lineup.lineupIDs, browse, infos, markets);
    std::vector<ss::ListingInput> inputs;
    for (const ss::ListingChannel& lc : chans) {
      ss::ListingInput in;
      in.channel = lc;
      auto p = m.programs.find(lc.id);
      if (p != m.programs.end()) in.programs = p->second.get();
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
  if (key == e.lastFingerprint) {
    e.checkedSinceOpen = true;  // nothing new to build from
    return;
  }
  e.lastFingerprint = key;
  needInfos();

  auto in = std::make_shared<ss::RebuildInput>();
  in->games = std::move(pub.games);
  in->schools = std::move(pub.schools);
  in->rosters = rosters;
  in->preferSpanish = fp.preferSpanish;
  // Shared with the live catalog (immutable once installed), not copied.
  in->sharedEvents = std::shared_ptr<const std::vector<catalog::LiveEvent>>(m.catalog, &m.catalog->events);
  in->sharedTraits =
      std::shared_ptr<const std::unordered_map<std::string, catalog::ChannelTraits>>(m.catalog, &m.catalog->traits);
  in->listings = e.listings;
  in->previous = e.events;
  in->channelIndex = sports::network_map::channelIndex(m.lineup.lineupIDs, m.lineup.lineupEntries, m.lineup.browseEntries,
                                                       m.lineup.entries, infos, {}, rosters, markets);
  in->now = now;
  in->zone = host_.zone();
  // Every raw channel has traits: they answer "does it still exist".
  auto traits = in->sharedTraits;
  in->channelExists = [traits](const std::string& id) { return traits->count(id) > 0; };
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
  e.rebuildStartMs = host_.ms();
  auto out = std::make_shared<std::optional<ss::RebuildResult>>();
  uint64_t gen = m.generation;
  host_.pool.post([in, out] { *out = ss::rebuild(*in); },
                   [this, gen, out] {
                     EventsState& ev = impl_->ev;
                     ev.rebuilding = false;
                     if (gen != impl_->generation || !*out) return;
                     ev.events = std::move((*out)->events);
                     ev.schedule.noteWatchable((*out)->watchableLeagues, time::now());
                     ev.boardDirty = true;
                     ev.eventsBuiltAt = time::now();
                     ev.checkedSinceOpen = true;
                     ev.noteBoard = true;
                     const sports::matcher::Stats& st = (*out)->stats;
                     ev.hidden = st.hidden;
                     MLOG_I("events", "rebuilt: %zu events", ev.events.size());
                     uint32_t took = host_.ms() - ev.rebuildStartMs;
                     char secs[16];
                     std::snprintf(secs, sizeof secs, "%.1f", took / 1000.0);
                     auto n = [](int v) { return std::to_string(v); };
                     ev.logNotes.push_back(
                         "rebuilt in " + std::string(secs) + " s; " + std::to_string(ev.events.size()) + " events from " +
                         n(st.events) + " feed titles and " + n(st.games) + " scheduled games; hidden: " +
                         n(st.shows) + " shows, " + n(st.replays) + " replays, " + n(st.stale) + " leftovers, " +
                         n(st.pastCards) + " past fight cards; " + n(st.mergedPlain) + " duplicate feeds merged");
                     // Saved for an instant board next launch.
                     std::vector<sports::matcher::GuideListing> listings = ev.listings;
                     if (auto saved = ss::resultToSave(ev.events, impl_->catalog->events, listings, true, time::now(),
                                                       ev.lastSaved))
                       host_.files.write(kResultFile, ss::toJson(*saved));
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
  if (!sports::proWrestlingOn(e.mySports)) {
    // Pro wrestling only when picked in My Sports.
    auto wrestling = [](const std::string& league) { return league == catalog::kProWrestling; };
    in.events.erase(std::remove_if(in.events.begin(), in.events.end(),
                                   [&](const auto& ev) { return wrestling(ev.league); }),
                    in.events.end());
    in.families.erase(std::remove_if(in.families.begin(), in.families.end(),
                                     [&](const catalog::EventFamily& f) { return wrestling(f.league); }),
                      in.families.end());
  }
  if (!sports::highSchoolOn(e.mySports)) {
    // High-school games only when picked in My Sports (the board leaves out
    // their games; their provider groups go too).
    in.families.erase(std::remove_if(in.families.begin(), in.families.end(),
                                     [](const catalog::EventFamily& f) { return sports::store::highSchoolFamily(f.id); }),
                      in.families.end());
  }
  in.prefs = e.mySports;
  in.statuses = e.scores.statuses();
  in.finalSeen = e.scores.finalSeen();
  in.whips = sb::whipChannels(m.lineup.entries, m.lineup.lineupIDs);
  in.now = now;
  in.zone = host_.zone();
  in.settings.showsUnwatchable = settings_.showUnwatchable;
  in.settings.showsScores = settings_.showScores;
  in.settings.twelveHour = !settings_.clock24;
  in.channelFor = makeChannelFor(*this, m, channels_);
  in.isCameraName = sports::special_feeds::isCameraName;
  in.cameraRace = sports::special_feeds::cameraRace;
  std::set<std::string> failing;
  for (const auto& [family, starts] : m.groupStarts)
    if (groupKeepsFailing(family)) failing.insert(family);
  if (!failing.empty()) {
    in.feedFails = [&m, &failing](const std::string& id) {
      auto t = m.catalog->traits.find(id);
      return t != m.catalog->traits.end() && t->second.familyKey && failing.count(*t->second.familyKey) > 0;
    };
  }
  in.feedAbroad = [&m](const std::string& id) {
    static const std::set<std::string> kEnglish = {"US", "UK", "CA", "AU", "IE", "NZ"};
    auto t = m.catalog->traits.find(id);
    if (t == m.catalog->traits.end() || !t->second.market) return false;
    const std::string& market = *t->second.market;
    return !kEnglish.count(market) && !m.prefs.includes(market);
  };
  in.podium = e.race.podiumHook();
  e.raceSeen = e.race.revision();
  e.board = sb::build(in);
  // Race weekends on the board ask for drivers and results (spaced and
  // capped by the rules behind this call).
  sports::race_data::refreshForBoard(e.race, e.board, settings_.showScores, now);
  e.statusesRevision = e.scores.revision();
  e.boardDirty = false;
  e.boardBuiltMs = host_.ms();

  auto out = std::make_shared<EventsBoard>();
  out->version = events_->version + 1;
  out->loading = e.schedule.fetching() && e.events.empty();
  out->updating = e.updating && !out->loading;
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
        EventTile tile = toTile(t, ev == e.board.events.end() ? nullptr : &ev->second, host_.zone());
        // A race with onboard feeds says so where the game detail goes.
        if (tile.detail.empty() && ev != e.board.events.end() && ev->second.game && ev->second.game->isRacing() &&
            hasCameras(t.id))
          tile.detail = "Onboard cameras";
        group.tiles.push_back(std::move(tile));
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
        t.footer = t.detail = f.hint;
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
  if (e.noteBoard && eventsOpen_ > 0) {
    // The board's size after an open or a rebuild, when it changed.
    e.noteBoard = false;
    int rows = 0, live = 0;
    for (const sb::Row& r : e.board.rows) {
      if (r.id == "families") continue;
      ++rows;
      if (r.id == "live") live = r.count;
    }
    std::string note = "board: " + std::to_string(e.board.events.size()) + " games in " + std::to_string(rows) +
                       " rows, " + std::to_string(live) + " live now" + (out->updating ? "; updating" : "");
    if (note != e.lastBoardNote) {
      e.lastBoardNote = note;
      e.logNotes.push_back(std::move(note));
    }
  }
}

void Store::buildLiveEventsReport(std::function<void(std::shared_ptr<std::string>)> done) {
  const Impl& m = *impl_;
  const EventsState& e = m.ev;
  // Copies of what the report reads; the worker owns them.
  struct Job {
    std::shared_ptr<const CatalogResult> catalog;
    std::vector<sports::SportsEvent> events;
    std::map<std::string, std::string> hidden;
    sb::Board board;
    std::map<std::string, std::string> starts;
    std::set<std::string> failing;
    bool wrestling = false;
    time::Seconds now = 0;
    time::Zone zone = time::Zone::device();
  };
  auto job = std::make_shared<Job>();
  job->catalog = m.catalog;
  job->events = e.events;
  job->hidden = e.hidden;
  job->board = e.board;
  for (const auto& [family, g] : m.groupStarts) {
    std::string text = groupStartsText(family);
    if (text.empty()) continue;
    job->starts[family] = text;
    if (groupKeepsFailing(family)) job->failing.insert(family);
  }
  job->wrestling = sports::proWrestlingOn(e.mySports);
  job->now = time::now();
  job->zone = host_.zone();
  auto out = std::make_shared<std::string>();
  host_.pool.post(
      [job, out] {
        report::LiveEventsReportInput in;
        in.catalog = job->catalog.get();
        in.events = &job->events;
        in.hidden = &job->hidden;
        in.board = &job->board;
        in.groupStarts = [job](const std::string& f) {
          auto it = job->starts.find(f);
          return it == job->starts.end() ? std::string() : it->second;
        };
        in.groupFails = [job](const std::string& f) { return job->failing.count(f) > 0; };
        in.proWrestlingOn = job->wrestling;
        in.now = job->now;
        in.zone = job->zone;
        *out = report::liveEventsCsv(in);
      },
      [out, done] { done(out->empty() ? nullptr : out); }, http::Priority::Normal);
}

std::vector<std::string> Store::takeEventsLog() {
  std::vector<std::string> out;
  out.swap(impl_->ev.logNotes);
  return out;
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
    if (auto text = host_.files.read(kResultFile)) {
      if (auto saved = ss::savedResultFromJson(*text)) {
        if (auto r = ss::restore(*saved, m.catalog->events, time::now())) {
          e.events = std::move(r->events);
          if (e.listings.empty()) e.listings = std::move(r->listings);
          e.boardDirty = true;
          e.eventsBuiltAt = saved->savedAt;
          time::Seconds age = std::max<time::Seconds>(0, time::now() - saved->savedAt);
          if (eventsOpen_ > 0 && age > kBoardStaleAfter && !e.updating) {
            e.updating = true;
            e.updatingUntilMs = host_.ms() + kUpdatingAtMostMs;
          }
          e.logNotes.push_back("saved board from " + ageText(age) + " ago shown until the rebuild");
        }
      }
    }
  }
  if (ms >= e.nextCheckMs) {
    e.nextCheckMs = ms + kCheckMs;
    rebuildEvents();
  }
  // "Updating" ends once the channel list, the schedule and a rebuild from
  // them are in (or after a minute).
  if (m.loading || e.schedule.fetching() || e.rebuilding) e.checkedSinceOpen = false;
  if (e.updating && (e.checkedSinceOpen || ms >= e.updatingUntilMs)) {
    e.updating = false;
    e.boardDirty = true;
    e.noteBoard = true;
  }
  if (e.boardDirty || (eventsOpen_ > 0 && ms - e.boardBuiltMs > kBoardRefreshMs) ||
      e.scores.revision() != e.statusesRevision)
    buildBoard();
}

// ------------------------------------------------------------ lookups

std::vector<std::string> Store::eventNotes(const std::string& eventId) const {
  const EventsState& e = impl_->ev;
  std::vector<std::string> out;
  auto ev = e.board.events.find(eventId);
  if (ev == e.board.events.end()) return out;
  const sports::SportsEvent& s = ev->second;
  if (s.game) {
    out.push_back("Schedule game: " + s.game->leagueShort + " " + s.game->id + (s.game->completed ? " (completed)" : ""));
  } else {
    out.push_back("No schedule game: built from the channel's own title");
  }
  auto status = e.scores.statuses().find(s.game ? s.game->id : std::string());
  if (status != e.scores.statuses().end()) {
    out.push_back(std::string("Live score status: ") + sports::rawValue(status->second.phase));
  } else if (s.game) {
    out.push_back("No live score status yet");
  }
  auto when = [&](time::Seconds t) {
    time::Civil c = time::civilFromSeconds(t + host_.zone().offsetAt(t));
    return std::to_string(c.month) + "/" + std::to_string(c.day) + " " + sb::clockText(t, host_.zone(), true);
  };
  if (s.start) out.push_back("Start: " + when(*s.start));
  else out.push_back("No start time in the title");
  size_t feeds = s.feeds().size(), channels = s.channels().size();
  out.push_back(std::to_string(channels) + (channels == 1 ? " channel, " : " channels, ") + std::to_string(feeds) +
                (feeds == 1 ? " event feed" : " event feeds"));
  if (s.family) out.push_back("Feed family: " + *s.family);
  if (s.feedsEndedAt) out.push_back("Its feeds moved on at " + when(*s.feedsEndedAt));
  return out;
}

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

std::optional<GameBanner> Store::gameBanner(const std::string& entryId, time::Seconds now) const {
  const Impl& m = *impl_;
  std::string id = entryId;
  if (!m.ev.board.events.count(id)) {
    // A guide channel (or its other coast's version) showing a game now.
    std::vector<std::string> ids = {entryId};
    if (const GuideChannel* g = channels_ ? channels_->find(entryId) : nullptr; g && !g->partner.empty())
      ids.push_back(g->partner);
    id = sports::banner::onChannel(m.ev.board, ids, now);
  }
  // A studio show isn't a game: the usual banner.
  auto ev = m.ev.board.events.find(id);
  const EventTile* t = id.empty() ? nullptr : eventTile(id);
  if (ev == m.ev.board.events.end() || !t || m.ev.board.studio.count(id)) return std::nullopt;
  const sports::SportsEvent& e = ev->second;
  GameBanner b;
  b.id = id;
  b.title = matchup(*t);
  // A race weekend: the session on now (or next) in the title.
  if (t->sides.empty() && e.game && e.game->isRacing() && e.game->customTitle) b.title = *e.game->customTitle;
  for (EventSide s : t->sides) {
    // Who, not how it's going.
    s.score.clear();
    s.pens.clear();
    s.leads = s.trails = s.ball = false;
    b.sides.push_back(std::move(s));
  }
  b.line = sports::banner::line(e, t->tagAlert ? std::string() : t->tag, host_.zone(), !settings_.clock24);
  b.icon = t->icon;
  b.symbol = t->symbol;
  b.favorite = t->favorite;
  b.start = e.start;
  b.end = e.scheduledEnd();
  b.postponed = e.game && e.game->postponed;
  b.channelName = t->channelName.empty() ? t->pill2 : t->channelName;
  b.channelLogo = t->channelLogo;
  return b;
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
  std::vector<std::string> candidates = plan.candidates;
  lastIfUnplayable(candidates, std::nullopt);
  return feedsFor(candidates);
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
  host_.files.write(kMySportsFile, sports::toJson(prefs));
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
    host_.pool.submit(std::move(q), [this, id, teams](http::Response&&) {
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

}  // namespace marigold::engine
