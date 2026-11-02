// Live Events in the store (the "Sports events" parts of LiveTVStore in
// tvOS): the schedule and live scores, asked for only while a Live Events
// screen is open; the rebuild that matches games to channels and provider
// feeds, and the board made from it, each on a worker from a snapshot and
// put in by the tick (events_build.hpp); feeds for a game in automatic
// backup order, starting on the source used last time (6 hours); reminders.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "core/engine/store.hpp"
#include "core/engine/events_build.hpp"
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

uint32_t sportColor(const std::string& icon) {
  // Baseball navy and darts olive: no chip is red.
  static const std::unordered_map<std::string, uint32_t> kColors = {
      {"soccer", 0x2E9D57},     {"basketball", 0xE0752D}, {"football", 0x8A5A33},   {"hockey", 0x4FA3E0},
      {"fieldHockey", 0x2F9C8F}, {"baseball", 0x34489A},  {"softball", 0x34489A},   {"tennis", 0x9CAD2C},
      {"cricket", 0x8F7A4A},    {"rugby", 0x6C4F9E},      {"golf", 0x3B7A57},       {"motorsport", 0x555B66},
      {"fighting", 0x9B3B6F},   {"wrestling", 0x9B3B6F},  {"volleyball", 0xC49A1C}, {"lacrosse", 0x4A6FA5},
      {"cycling", 0xC98B2F},    {"darts", 0x5A6B2F},      {"athletics", 0xC0713F},  {"horseRacing", 0x4F6F6A},
      {"multi", 0xA88A3A},      {"other", 0x555B66}};
  auto it = kColors.find(icon);
  return it == kColors.end() ? 0 : it->second;
}

EventTile::Mark tileMark(const std::string& icon, bool game, bool highSchool) {
  if (icon != "other" && sportColor(icon)) return EventTile::Mark::Chip;
  if (highSchool) return EventTile::Mark::Chip;
  return game ? EventTile::Mark::Unknown : EventTile::Mark::None;
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

std::string tileTopLine(const EventTile& t, bool mixed) {
  std::string comp = t.competition;
  if (comp.empty()) comp = !t.sport.empty() && mixed ? t.sport : t.sport.empty() && !t.highSchool ? "LIVE EVENT" : "";
  if (t.highSchool) comp = comp.empty() ? "HIGH SCHOOL" : "HIGH SCHOOL \xc2\xb7 " + comp;
  return comp;
}

void tileText(EventTile& t) {
  t.topLine = tileTopLine(t, false);
  t.topLineMixed = tileTopLine(t, true);
  t.extraLine = t.statusExtra.empty() ? std::string() : " \xc2\xb7 " + t.statusExtra;
  t.textMade = true;
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
// Boards asked for by scores, a rebuild or the clock: not sooner than this
// after the last began (your own changes don't wait).
constexpr uint32_t kBoardGapMs = 1500;
constexpr uint32_t kTimingLogMs = 60000;
// The day's schedule in the background: this long after the channels are
// in, then looked at again this often (a new day while the app runs).
constexpr uint32_t kScheduleBackgroundAfterMs = 20000;
constexpr uint32_t kScheduleBackgroundCheckMs = 5 * 60000;

using Clock = std::chrono::steady_clock;

// Shown on opening without waiting for a board made now (its clock texts
// are a minute at most behind, as between the board's own refreshes).
constexpr time::Seconds kBoardShownFor = 60;

// Which lineup a copy or the whip-around list was made from: its revision
// and size (a reset empties it without a new revision).
uint64_t lineupKeyOf(const Store::Impl& m) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(m.lineupRevision)) << 32) ^ m.lineup.entries.size();
}

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
  if (o.score.find(' ') != std::string::npos) {
    // Sets: a column each on the tile.
    for (size_t i = 0; i < o.score.size();) {
      size_t j = o.score.find(' ', i);
      if (j == std::string::npos) j = o.score.size();
      if (j > i) o.sets.push_back(o.score.substr(i, j - i));
      i = j + 1;
    }
  }
  o.home = s.home;
  if (s.pens) {
    o.pens = std::to_string(*s.pens);
    o.pensText = "(" + o.pens + ")";
  }
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
  o.mark = tileMark(t.icon, (event && event->game) || o.sides.size() == 2, o.highSchool);
  if (o.mark == EventTile::Mark::Chip) {
    o.chipColor = sportColor(t.icon);
    if (!o.chipColor) o.chipColor = sportColor("other");
  }
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
  if (event && t.state == sports::SportsEvent::State::upcoming && !event->sources.empty()) {
    sports::EventSource::Kind best = event->sources.front().kind;  // best first
    o.channelFirst = best == sports::EventSource::Kind::lineup || best == sports::EventSource::Kind::guide;
  }
  // Hero / detail lines from the tile and its event.
  std::string chip = event ? event->leagueShort : "";
  if (!o.tag.empty() && !o.tagAlert) chip += chip.empty() ? o.tag : " \xc2\xb7 " + o.tag;
  o.leagueChip = chip;
  if (o.sides.size() == 2 && !o.sides[0].score.empty() && !o.sides[1].score.empty())
    o.heroTitle = o.sides[0].name + " " + o.sides[0].score + " \xe2\x80\x93 " + o.sides[1].score + " " + o.sides[1].name;
  else
    o.heroTitle = matchup(o);
  o.heroStatus = o.status + (o.statusExtra.empty() ? "" : " " + o.statusExtra);
  if (!o.situation.empty()) o.heroStatus += (o.heroStatus.empty() ? "" : " \xc2\xb7 ") + o.situation;
  o.whereLine = o.channelName.empty() ? o.footer : "On " + o.channelName + (o.footer.empty() ? "" : " \xc2\xb7 " + o.footer);
  if (event && event->start) o.start = *event->start;
  if (t.state == sports::SportsEvent::State::upcoming && !t.notStarted && o.start) {
    time::Seconds until = o.start - time::now();
    o.soon = until > 0 && until <= 3600;
  }
  (void)zone;
  tileText(o);
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
  saveSchedule();
  st.write(kRemindersFile, e.reminders.toJson());
  st.write(kMemoryFile, e.memory.toJson());
  st.write(kSeenFile, json::write(sports::toJson(e.seen)));
}

void Store::saveSchedule() {
  EventsState& e = impl_->ev;
  Files& st = host_.files;
  if (e.schedule.takeDirty()) {
    // Made into text and written on a worker; the newest save wins.
    auto saved = std::make_shared<const sports::schedule::Saved>(e.schedule.saved());
    std::string path = st.path(kScheduleFile);
    std::shared_ptr<events_build::Writes> writes = e.scheduleWrites;
    uint64_t turn = 0;
    {
      std::lock_guard<std::mutex> lock(writes->mu);
      turn = ++writes->asked;
    }
    host_.pool.post([path, saved, writes, turn] {
      events_build::writeFile(path, sports::schedule::toJson(*saved), writes.get(), turn);
    }, nullptr, http::Priority::Background);
  }
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
    e.scores.start(sports::scores::Holder::board, settings_.scheduleEnabled);
    eventsArrive(0);
  } else if (!open && eventsOpen_ == 0) {
    e.holding = e.freshNote = e.wholeFirst = false;
    if (e.scores.stop(sports::scores::Holder::board)) {
      e.scores.cancelRound();
      e.scoresRound = false;
      e.scoresPending.reset();
    }
  }
}

void Store::eventsArrive(time::Seconds away) {
  EventsState& e = impl_->ev;
  // The channel list if older than a few minutes, new sport feeds seen, the
  // schedule if due: then what is coming is known.
  liveEventsOpened();
  time::Seconds now = time::now();
  if (sports::noteSportFeeds(e.seen, e.seenCheckedAt, impl_->catalog->events, now))
    e.schedule.leaguesChanged(scheduleContext());
  e.schedule.refreshIfNeeded(scheduleContext());
  // Games worked out over 15 minutes ago (or another day), from a channel
  // list or a schedule a newer one is coming for, or kept through a long
  // sleep never show: placeholders, and boards leave out what old inputs
  // decide until fresh ones are in (at most kHoldAtMostMs). Newer ones show
  // as made again from the clock now.
  e.checkedSinceOpen = false;
  e.noteBoard = true;
  events_build::Hold old = events_build::holdFor(eventsInputs(), now, host_.zone());
  bool keep = !e.events->empty() && away < events_build::kKeptBoardFor &&
              events_build::keptBoardShows(e.eventsBuiltAt, now, host_.zone()) && !old.list && !old.schedule;
  std::string what = away ? "woke after " + ageText(away) : std::string("opened");
  if (e.events->empty() || !e.eventsBuiltAt) {
    e.logNotes.push_back(what + "; no board yet");
  } else {
    time::Seconds age = std::max<time::Seconds>(0, now - e.eventsBuiltAt);
    e.logNotes.push_back(what + "; the board is from " + ageText(age) + " ago" + (keep ? "" : "; placeholders until new"));
  }
  e.freshAfter = keep ? 0 : now;
  e.holding = e.freshNote = !keep;
  // Not kept: placeholders until the whole board is in (boardFlags()).
  e.wholeFirst = !keep;
  e.holdCapped = false;
  e.holdSinceMs = host_.ms();
  e.heldMost = e.oldLiveMost = 0;
  if (!keep) e.lastFingerprint.clear();  // worked out again, even with nothing new
  if (!keep || now - e.boardMadeAt > kBoardShownFor || e.boardSerial != e.eventsSerial) showPlaceholders();
  // Scores: a round for this look, now (each league still at its own pace).
  ++e.lookSerial;
  if (!e.scoresRound) e.scoresAt = host_.ms();
  if (ss::listingsNeedRead(e.listingsAt, 0, 0, now)) e.listingsAt.reset();
  e.nextCheckMs = 0;
  e.boardDirty = true;
}

events_build::Inputs Store::eventsInputs() const {
  const Impl& m = *impl_;
  const EventsState& e = m.ev;
  events_build::Inputs in;
  in.made = e.madeFrom;
  in.listAt = m.listFetchedAt;
  in.listComing = m.loading;
  in.scheduleAt = e.schedule.saved().fetchedAt.value_or(0);
  in.scheduleComing = e.schedule.fetching();
  in.scoresChecked = !e.scores.looking() || e.scoresDone == EventsState::Look{e.lookSerial, e.scheduleRevision};
  // The games the last board (of this look and these games) left out for
  // their scores: still waited for while one of their leagues can answer.
  if (!in.scoresChecked && e.heldScoresOf == std::make_pair(e.lookSerial, e.eventsSerial)) {
    time::Seconds now = time::now();
    in.scoresAwaited = std::any_of(e.heldScores.begin(), e.heldScores.end(),
                                   [&e, now](const std::string& league) { return e.scores.awaits(league, now); });
  }
  return in;
}

// ------------------------------------------------------------ requests

void Store::pumpSchedule(uint32_t ms, bool background) {
  EventsState& e = impl_->ev;
  if (e.scheduleBusy) return;
  if (!e.schedulePending) {
    e.schedulePending = e.schedule.nextRequest();
    // Today's games are in (the rest of the week still coming): matched at
    // once, one rebuild for the lot.
    if (e.schedule.takeBatch()) {
      MLOG_I("events", "schedule: today's games in");
      ++e.scheduleRevision;
      e.nextCheckMs = 0;
      if (eventsOpen_ > 0 && !e.scoresRound) e.scoresAt = ms;
      saveSchedule();
    }
    if (!e.schedulePending) {
      if (auto done = e.schedule.takeFinished()) {
        MLOG_I("events", "schedule: %s", done->note.c_str());
        ++e.scheduleRevision;
        e.nextCheckMs = 0;
        // Its games around now are looked at in a round of their own.
        if (eventsOpen_ > 0 && !e.scoresRound) e.scoresAt = ms;
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
  bool calendar = pending->kind == sports::schedule::Schedule::Pending::Kind::calendar;
  http::Request q = toHttp(pending->request, calendar ? "events.calendar" : "events.schedule");
  // In the background (Live Events closed): behind anything on screen.
  if (background) q.priority = http::Priority::Background;
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

void Store::scheduleInBackground(uint32_t ms) {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  if (!settings_.scheduleEnabled || m.channels->empty() || status_.phase != StoreStatus::Phase::Ready) return;
  // Nothing else needs the TV: the list isn't loading or sorting, no key
  // pressed this moment, no channel starting (host idle) and nothing playing
  // full screen (a weak TV's decoder and relay come first; opening Live
  // Events still fetches at once).
  bool quiet = !m.loading && *m.foregroundJobs == 0 && (!host_.idle || host_.idle()) && !host_.playingFull();
  if (!e.scheduleBackgroundFrom) {
    // Shortly after the first frame and the channel list.
    e.scheduleBackgroundFrom = std::max<uint32_t>(1, ms + kScheduleBackgroundAfterMs);
    return;
  }
  if (!quiet || ms < e.scheduleBackgroundFrom) return;
  if (e.schedule.busy()) {
    pumpSchedule(ms, true);
    return;
  }
  if (ms < e.scheduleBackgroundCheck) return;
  e.scheduleBackgroundCheck = ms + kScheduleBackgroundCheckMs;
  // Only on a TV that uses Live Events; a first open fetches as before.
  if (!e.schedule.usedLately(time::now())) return;
  if (e.schedule.refreshIfNeeded(scheduleContext())) MLOG_I("events", "schedule: the day's fetch in the background");
}

void Store::pumpScores(uint32_t ms) {
  EventsState& e = impl_->ev;
  if (!e.scores.looking() || e.scoresBusy || ms < e.scoresAt) return;
  time::Seconds now = time::now();
  if (!e.scoresRound) {
    sports::scores::RoundInput in;
    in.games = e.schedule.games(scheduleContext());
    in.watching = host_.playingFull();
    in.showsScores = settings_.showScores;
    in.now = now;
    in.zone = host_.zone();
    in.watchableListed = e.watchableListed;  // worked out with the board
    e.scores.beginRound(in);
    e.scoresRound = true;
    e.roundFor = {e.lookSerial, e.scheduleRevision};
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
      e.scoresDone = e.roundFor;
      // Opened again, woken or a new schedule since it began: one more for
      // them (the leagues just asked wait for their next look).
      if (e.roundFor != EventsState::Look{e.lookSerial, e.scheduleRevision})
        e.scoresAt = ms + static_cast<uint32_t>(sports::scores::kRequestSpacingMs);
      if (e.scores.revision() != e.statusesRevision) e.boardStale = true;
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
    if (ev.scores.revision() != ev.statusesRevision) ev.boardStale = true;
  });
}

// ------------------------------------------------------------ rebuild

namespace events_build {

void writeFile(const std::string& path, const std::string& text, Writes* order, uint64_t turn) {
  // A name of its own while it's written: a job dropped at an account
  // change and the one after it never share one.
  static std::atomic<unsigned> serial{0};
  std::string tmp = path + ".tmp" + std::to_string(++serial);
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return;
  bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  ok = std::fclose(f) == 0 && ok;
  std::unique_lock<std::mutex> lock;
  if (order) {
    lock = std::unique_lock<std::mutex>(order->mu);
    ok = ok && turn == order->asked;  // a newer one is on its way
  }
  if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) std::remove(tmp.c_str());
}

namespace {

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

}  // namespace

RebuildMade rebuild(RebuildJob& job) {
  RebuildMade made;
  Clock::time_point start = Clock::now();
  ss::RebuildInput& in = job.in;
  const LineupCopy& lineup = *job.lineup;
  // Every row by id.
  std::unordered_map<std::string, EntryInfo> infos;
  infos.reserve(job.catalog->entries.size());
  for (const EntryInfo& info : job.catalog->entries) infos.emplace(info.id, info);
  // The guide's game listings when due (sports, local and Spanish channels
  // of the lineup and the browse list).
  if (job.readListings) {
    std::vector<ss::ListingChannel> chans = ss::listingChannels(lineup.lineupIDs, lineup.browseIDs, infos, job.markets);
    std::vector<ss::ListingInput> inputs;
    inputs.reserve(chans.size());
    for (const ss::ListingChannel& lc : chans) {
      ss::ListingInput li;
      li.channel = lc;
      auto p = job.programs.find(lc.id);
      if (p != job.programs.end()) li.programs = p->second.get();
      inputs.push_back(li);
    }
    made.listings = std::move(ss::extractListings(inputs, in.now).listings);
    in.listings = *made.listings;
  } else {
    in.listings = std::move(job.listings);
  }
  if (job.previous) in.previous = *job.previous;
  in.channelIndex = sports::network_map::channelIndex(lineup.lineupIDs, lineup.lineupEntries, lineup.browseEntries,
                                                      lineup.entries, infos, {}, in.rosters, job.markets);
  // F1 sessions are also on the F1 channels, while the schedule has one.
  if (job.f1) {
    if (!job.f1Sources)
      made.f1Sources = sports::special_feeds::f1ChannelSources(lineup.entries, infos, job.hiddenGroups, "Other",
                                                               lineup.lineupIDs);
    in.f1Sources = job.f1Sources ? *job.f1Sources : *made.f1Sources;
  }
  made.prepMs = msSince(start);
  Clock::time_point match = Clock::now();
  made.result = ss::rebuild(in);
  made.matchMs = msSince(match);
  made.lastSaved = job.lastSaved;
  if (made.result) {
    // Saved for an instant board next launch.
    Clock::time_point save = Clock::now();
    if (auto saved = ss::resultToSave(made.result->events, job.catalog->events, in.listings, true, time::now(),
                                      made.lastSaved, made.result->slotsSeen)) {
      if (!job.savePath.empty()) writeFile(job.savePath, ss::toJson(*saved));
      made.saved = true;
    }
    made.saveMs = msSince(save);
  }
  return made;
}

}  // namespace events_build

void Store::rebuildEvents() {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  if (e.rebuilding || e.restoring || m.channels->empty()) return;
  time::Seconds now = time::now();
  sports::schedule::Context ctx = scheduleContext();
  sports::schedule::Published pub = e.schedule.publish(ctx);
  std::vector<sports::League> active = sports::activeLeagues(e.mySports, e.seen, now);
  std::vector<sports::matcher::RosterLeague> rosters = ss::rosterLeagues(active, pub.rosters);
  sports::StationMarkets markets = sports::stationMarkets(m.prefs);
  // The guide's game listings are read again when due (on the worker): a
  // new listings revision, so a rebuild follows.
  bool readListings = !e.listingsAt || now - *e.listingsAt > ss::kListingsStale;

  ss::Fingerprint fp;
  fp.eventsRevision = m.catalogRevision;
  fp.scheduleRevision = e.scheduleRevision;
  fp.entriesRevision = m.lineupRevision;
  fp.listingsRevision = e.listingsRevision + (readListings ? 1 : 0);
  fp.preferSpanish = m.prefs.countries.count("US-ES") > 0;
  fp.enabled = settings_.scheduleEnabled;
  fp.schools = pub.schools.size();
  std::string key = ss::fingerprint(fp, now);
  events_build::MadeFrom from{m.listFetchedAt, e.schedule.saved().fetchedAt.value_or(0)};
  if (!readListings && key == e.lastFingerprint) {
    e.checkedSinceOpen = true;  // nothing new to build from
    e.madeFrom = from;          // the same inputs, fetched again
    return;
  }
  e.lastFingerprint = key;

  // What the rebuild reads of the lineup: copied once per lineup.
  uint64_t lineupKey = lineupKeyOf(m);
  if (!e.lineupCopy || e.lineupCopyKey != lineupKey) {
    auto copy = std::make_shared<events_build::LineupCopy>();
    copy->lineupIDs = m.lineup.lineupIDs;
    copy->lineupEntries = m.lineup.lineupEntries;
    copy->browseEntries = m.lineup.browseEntries;
    copy->entries = m.lineup.entries;
    for (const Channel& c : m.lineup.browseEntries) copy->browseIDs.push_back(c.id);
    e.lineupCopy = std::move(copy);
    e.lineupCopyKey = lineupKey;
  }
  auto job = std::make_shared<events_build::RebuildJob>();
  ss::RebuildInput& in = job->in;
  in.games = std::move(pub.games);
  in.schools = std::move(pub.schools);
  in.rosters = std::move(rosters);
  in.preferSpanish = fp.preferSpanish;
  // Shared with the live catalog (immutable once installed), not copied.
  in.sharedEvents = std::shared_ptr<const std::vector<catalog::LiveEvent>>(m.catalog, &m.catalog->events);
  in.sharedTraits =
      std::shared_ptr<const std::unordered_map<std::string, catalog::ChannelTraits>>(m.catalog, &m.catalog->traits);
  in.now = now;
  in.zone = host_.zone();
  // Every raw channel has traits: they answer "does it still exist".
  auto traits = in.sharedTraits;
  in.channelExists = [traits](const std::string& id) { return traits->count(id) > 0; };
  in.isCameraName = sports::special_feeds::isCameraName;
  in.slotsSeen = e.slotsSeen;
  auto stop = m.stopJobs;
  in.shouldStop = [stop]() -> std::optional<std::string> {
    if (stop->load()) return std::string("closing");
    return std::nullopt;
  };
  job->previous = e.events;
  job->catalog = m.catalog;
  job->lineup = e.lineupCopy;
  job->markets = markets;
  job->readListings = readListings;
  if (readListings) job->programs = m.programs;  // the lists are shared, not copied
  else job->listings = e.listings;
  std::string f1Key;
  if (sports::special_feeds::wantsF1Sources(in.games)) {
    job->f1 = true;
    f1Key = sports::special_feeds::f1ChannelsKey(m.lineupRevision, m.lineup.entries.size(), m.lineup.lineupIDs,
                                                m.hiddenGroups);
    if (f1Key == e.f1Key) job->f1Sources = e.f1Sources;
    else job->hiddenGroups = m.hiddenGroups;
  }
  job->savePath = host_.files.path(kResultFile);
  job->lastSaved = e.lastSaved;
  e.rebuilding = true;
  e.rebuildStartMs = host_.ms();
  auto made = std::make_shared<events_build::RebuildMade>();
  uint64_t gen = m.generation;
  host_.pool.post([job, made] { *made = events_build::rebuild(*job); },
                  [this, gen, made, now, f1Key, from] {
                    Clock::time_point start = Clock::now();
                    EventsState& ev = impl_->ev;
                    ev.rebuilding = false;
                    if (gen != impl_->generation) return;
                    if (made->listings) {
                      ev.listings = std::move(*made->listings);
                      ev.listingsAt = now;
                      ++ev.listingsRevision;
                    }
                    if (made->f1Sources) {
                      ev.f1Key = f1Key;
                      ev.f1Sources = std::move(*made->f1Sources);
                    }
                    if (!made->result) return;
                    ev.lastSaved = made->lastSaved;
                    ev.slotsSeen = std::move(made->result->slotsSeen);
                    // The last list is freed on a worker (unless a board job still reads it).
                    auto old = std::move(ev.events);
                    ev.events = std::make_shared<const std::vector<sports::SportsEvent>>(std::move(made->result->events));
                    ++ev.eventsSerial;
                    host_.pool.post([old]() mutable { old.reset(); }, nullptr, http::Priority::Background);
                    ev.schedule.noteWatchable(made->result->watchableLeagues, time::now());
                    ev.boardStale = true;
                    ev.eventsBuiltAt = time::now();
                    ev.madeFrom = from;
                    ev.checkedSinceOpen = true;
                    ev.noteBoard = true;
                    sports::matcher::Stats& st = made->result->stats;
                    ev.hidden = std::move(st.hidden);
                    MLOG_I("events", "rebuilt: %zu events", ev.events->size());
                    uint32_t took = host_.ms() - ev.rebuildStartMs;
                    char secs[16];
                    std::snprintf(secs, sizeof secs, "%.1f", took / 1000.0);
                    auto n = [](int v) { return std::to_string(v); };
                    ev.logNotes.push_back(
                        "rebuilt in " + std::string(secs) + " s; " + std::to_string(ev.events->size()) + " events from " +
                        n(st.events) + " feed titles and " + n(st.games) + " scheduled games; hidden: " +
                        n(st.shows) + " shows, " + n(st.replays) + " replays, " + n(st.stale + st.seenBefore) +
                        " leftovers, " +
                        n(st.pastCards) + " past fight cards; " + n(st.mergedPlain) + " duplicate feeds merged");
                    ++ev.timing.rebuilds;
                    ev.timing.rebuildMax = std::max(ev.timing.rebuildMax, made->prepMs + made->matchMs + made->saveMs);
                    ev.timing.uiMax = std::max(ev.timing.uiMax, events_build::msSince(start));
                  },
                  http::Priority::Normal);
}

// ------------------------------------------------------------ board

namespace events_build {

BoardMade board(const BoardInput& in) {
  Clock::time_point start = Clock::now();
  BoardMade made;
  made.index = in.index;
  if (!made.index) {
    auto index = std::make_shared<ChannelIndex>();
    index->reserve(in.channels->size());
    for (size_t i = 0; i < in.channels->size(); ++i) index->emplace((*in.channels)[i].id, i);
    made.index = std::move(index);
  }
  const ChannelIndex& index = *made.index;
  const std::vector<Channel>& channels = *in.channels;
  sb::Input b;
  b.events = *in.events;
  // Tiles come from the schedule once one is in (sb::Settings::fromSchedule),
  // told from every event, before any are left out while waiting below.
  b.scheduleLoaded = std::any_of(in.events->begin(), in.events->end(),
                                 [](const sports::SportsEvent& ev) { return ev.game.has_value(); });
  b.families = in.catalog->families;
  if (!in.proWrestling) {
    // Pro wrestling only when picked in My Sports.
    auto wrestling = [](const std::string& league) { return league == catalog::kProWrestling; };
    b.events.erase(std::remove_if(b.events.begin(), b.events.end(), [&](const auto& ev) { return wrestling(ev.league); }),
                   b.events.end());
    b.families.erase(std::remove_if(b.families.begin(), b.families.end(),
                                    [&](const catalog::EventFamily& f) { return wrestling(f.league); }),
                     b.families.end());
  }
  if (!in.highSchool) {
    // High-school games only when picked in My Sports (the board leaves out
    // their games; their provider groups go too).
    b.families.erase(std::remove_if(b.families.begin(), b.families.end(),
                                    [](const catalog::EventFamily& f) { return sports::store::highSchoolFamily(f.id); }),
                     b.families.end());
  }
  if (in.hold.any()) {
    // Waiting for fresh inputs: what older ones decide is left out (games
    // around now until a score answers for them while one can, or the
    // clock does). The leagues waited for are noted even under the
    // schedule's wait, so the wait for scores never ends before they answer.
    auto old = [&in, &made](const sports::SportsEvent& ev) {
      if (ev.game) {
        const sports::SportsGame& g = *ev.game;
        bool around = !g.completed && in.now >= g.start - sports::scores::kLeadBeforeStart &&
                      in.now <= g.start + sports::scores::kWindowAfterStart;
        bool scores = in.hold.scores && around && !in.statuses.count(g.id) && in.awaited.from(g.leagueShort);
        if (scores) made.heldScores.insert(g.leagueShort);
        if (in.hold.schedule || scores) return true;
      }
      return in.hold.list && (!ev.game || ev.hasFeeds());
    };
    size_t before = b.events.size();
    b.events.erase(std::remove_if(b.events.begin(), b.events.end(), old), b.events.end());
    made.held = static_cast<int>(before - b.events.size());
    if (in.hold.list) {
      made.held += static_cast<int>(b.families.size());
      b.families.clear();
    }
  }
  made.hold = in.hold;
  made.oldLive = in.oldLive;
  b.prefs = in.prefs;
  b.statuses = in.statuses;
  b.finalSeen = in.finalSeen;
  b.whips = in.whips;
  b.now = in.now;
  b.zone = in.zone;
  b.settings = in.settings;
  b.channelFor = [&in, &index, &channels](const sports::EventSource& src) -> std::optional<sb::ChannelLabel> {
    if (const GuideChannel* g = in.guide->find(src.channelID)) {
      sb::ChannelLabel l;
      l.id = g->id;
      l.name = g->name;
      l.display = g->name;
      if (!g->logoUrl.empty()) l.logoUrls.push_back(g->logoUrl);
      return l;
    }
    auto it = index.find(src.channelID);
    if (it == index.end()) return std::nullopt;
    const Channel& c = channels[it->second];
    sb::ChannelLabel l;
    l.id = c.id;
    l.name = c.displayName();
    l.display = c.displayName();
    if (!c.imageUrl.empty()) l.logoUrls.push_back(c.imageUrl);
    return l;
  };
  b.isCameraName = sports::special_feeds::isCameraName;
  b.cameraRace = sports::special_feeds::cameraRace;
  const catalog::CatalogResult& cat = *in.catalog;
  if (!in.failing.empty()) {
    b.feedFails = [&cat, &in](const std::string& id) {
      auto t = cat.traits.find(id);
      return t != cat.traits.end() && t->second.familyKey && in.failing.count(*t->second.familyKey) > 0;
    };
  }
  b.feedAbroad = [&cat, &in](const std::string& id) {
    static const std::set<std::string> kEnglish = {"US", "UK", "CA", "AU", "IE", "NZ"};
    auto t = cat.traits.find(id);
    if (t == cat.traits.end() || !t->second.market) return false;
    const std::string& market = *t->second.market;
    return !kEnglish.count(market) && !in.lineupPrefs.includes(market);
  };
  b.podium = in.podium;
  if (in.titles) {
    // Titles that come and go through the day: kept to a few boards' worth.
    sb::trimTitleMemo(*in.titles, 4 * in.events->size() + 4096);
    b.titles = in.titles;
  }
  made.board = sb::build(b);
  const sb::Board& board = made.board;
  for (const auto& [id, tile] : board.tiles) {
    auto ev = board.events.find(id);
    if (ev == board.events.end() || !ev->second.game) continue;
    const sports::League* league = sports::leagueByShort(ev->second.game->leagueShort);
    if ((ev->second.game->isTennis() && tile.watchable) || (league && league->everyGame))
      made.watchableListed.insert(ev->second.game->id);
  }

  // A race with onboard feeds says so where the game detail goes.
  auto hasCameras = [&](const sports::SportsEvent& event) {
    if (channels.empty() || !in.cameras) return false;
    in.cameras->update(channels, in.camerasKey);
    std::vector<Channel> feeds;
    for (const sports::EventSource& src : event.sources) {
      auto it = index.find(src.channelID);
      if (it != index.end()) feeds.push_back(channels[it->second]);
    }
    return !in.cameras->cameras(event, feeds).empty();
  };
  auto out = std::make_shared<EventsBoard>();
  out->scoresOn = in.settings.showsScores;
  for (const sb::Row& r : board.rows) {
    EventRow row;
    row.id = r.id;
    row.title = r.title;
    row.live = r.live;
    row.count = r.count;
    row.families = r.id == "families";
    for (const sb::Group& g : r.groups) {
      EventGroup group;
      group.title = g.label;
      group.tiles.reserve(g.tiles.size());
      for (const sb::Tile& t : g.tiles) {
        auto ev = board.events.find(t.id);
        EventTile tile = toTile(t, ev == board.events.end() ? nullptr : &ev->second, in.zone);
        if (tile.detail.empty() && ev != board.events.end() && ev->second.game && ev->second.game->isRacing() &&
            hasCameras(ev->second))
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
        t.league = f.leagueChip;
        t.footer = t.detail = f.hint;
        t.watchable = true;
        t.symbol = "\xe2\x96\xa0";
        t.leagueChip = f.leagueChip;
        t.heroTitle = f.name;
        tileText(t);
        group.tiles.push_back(std::move(t));
      }
      row.groups.push_back(std::move(group));
    }
    for (const EventGroup& g : row.groups) made.tiles += g.tiles.size();
    if (!row.groups.empty()) out->rows.push_back(std::move(row));
  }
  out->index();
  made.events = std::move(out);
  made.madeAt = in.now;
  made.ms = msSince(start);
  return made;
}

}  // namespace events_build

std::shared_ptr<events_build::BoardInput> Store::boardInput() {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  auto in = std::make_shared<events_build::BoardInput>();
  time::Seconds now = time::now();
  in->events = e.events;
  in->catalog = m.catalog;
  in->guide = channels_;
  in->channels = m.channels;
  if (e.channelIndex && e.channelIndexOf.lock() == m.channels) in->index = e.channelIndex;
  in->proWrestling = sports::proWrestlingOn(e.mySports);
  in->highSchool = sports::highSchoolOn(e.mySports);
  in->prefs = e.mySports;
  // Only scores answered lately: an older LIVE goes by the clock.
  in->statuses = e.scores.current(now, &in->oldLive);
  if (e.holding) in->hold = events_build::holdFor(eventsInputs(), now, host_.zone());
  if (in->hold.scores) in->awaited = e.scores.awaited(now);
  in->finalSeen = e.scores.finalSeen();
  // Whip-around channels: once per lineup.
  uint64_t lineupKey = lineupKeyOf(m);
  if (e.whipsKey != lineupKey) {
    e.whips = sb::whipChannels(m.lineup.entries, m.lineup.lineupIDs);
    e.whipsKey = lineupKey;
  }
  in->whips = e.whips;
  in->now = now;
  in->zone = host_.zone();
  in->settings.showsUnwatchable = settings_.showUnwatchable;
  in->settings.showsScores = settings_.showScores;
  in->settings.twelveHour = !settings_.clock24;
  in->settings.fromSchedule = settings_.scheduleEnabled;
  in->settings.showsUnscheduled = settings_.boardUnscheduled;
  for (const auto& [family, starts] : m.groupStarts)
    if (groupKeepsFailing(family)) in->failing.insert(family);
  in->lineupPrefs = m.prefs;
  in->podium = e.race.podiumHook();
  in->cameras = e.boardCameras;
  in->titles = e.boardTitles;
  in->camerasKey = std::to_string(m.catalogRevision) + "|" + std::to_string(m.channels->size());
  return in;
}

void Store::startBoard(uint32_t ms) {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  std::shared_ptr<events_build::BoardInput> in = boardInput();
  e.raceSeen = e.race.revision();
  e.statusesRevision = e.scores.revision();
  e.boardDirty = e.boardStale = false;
  e.boardHold = in->hold;
  e.boardBusy = true;
  e.boardStartMs = e.boardBuiltMs = ms;
  auto made = std::make_shared<events_build::BoardMade>();
  uint64_t gen = m.generation, serial = e.eventsSerial, look = e.lookSerial;
  host_.pool.post([in, made] { *made = events_build::board(*in); },
                  [this, gen, serial, look, in, made] {
                    EventsState& ev = impl_->ev;
                    ev.boardBusy = false;
                    if (gen != impl_->generation) return;
                    ev.boardDoneSerial = serial;
                    ev.boardDoneLook = look;
                    ev.channelIndex = made->index;
                    ev.channelIndexOf = in->channels;
                    ev.boardDone = made;  // put in by the tick
                  },
                  eventsOpen_ > 0 ? http::Priority::Visible : http::Priority::Normal);
}

void Store::putBoard() {
  Impl& m = *impl_;
  EventsState& e = m.ev;
  std::shared_ptr<events_build::BoardMade> made = std::move(e.boardDone);
  // The old board goes back with the job's shell, and is freed on a worker.
  std::swap(e.board, made->board);
  // Games left out while waiting stay asked about.
  if (made->hold.any()) e.watchableListed.insert(made->watchableListed.begin(), made->watchableListed.end());
  else e.watchableListed.swap(made->watchableListed);
  // What it left out for scores: waited for while those leagues can answer.
  if (made->hold.scores) {
    e.heldScores.swap(made->heldScores);
    e.heldScoresOf = {e.boardDoneLook, e.boardDoneSerial};
  }
  std::shared_ptr<EventsBoard> out = std::move(made->events);
  out->version = events_->version + 1;
  boardFlags(*out);
  // Whole: from now on a board still coming in shows its rows.
  if (!out->loading && !out->more) e.wholeFirst = false;
  e.boardMadeAt = made->madeAt;
  e.boardSerial = e.boardDoneSerial;
  e.retired.push_back(std::move(events_));
  events_ = out;
  size_t tiles = made->tiles;
  double took = made->ms;
  host_.pool.post([made]() mutable { made.reset(); }, nullptr, http::Priority::Background);
  // Race weekends on the board ask for drivers and results (spaced and
  // capped by the rules behind this call).
  sports::race_data::refreshForBoard(e.race, e.board, settings_.showScores, time::now());
  ++e.timing.boards;
  e.timing.tiles = tiles;
  e.timing.boardMax = std::max(e.timing.boardMax, took);
  if (e.freshNote) {
    // After an open or a wake: how long until a board from fresh inputs (or
    // the wait's end), and what was left out meanwhile.
    e.heldMost = std::max(e.heldMost, made->held);
    e.oldLiveMost = std::max(e.oldLiveMost, made->oldLive);
    if (!e.holding && !made->hold.any() && eventsOpen_ > 0) {
      e.freshNote = false;
      MLOG_I("events", "timing fresh=%ums capped=%d held=%d oldLive=%d", host_.ms() - e.holdSinceMs,
             e.holdCapped ? 1 : 0, e.heldMost, e.oldLiveMost);
    }
  }
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
                       " rows, " + std::to_string(live) + " live now" +
                       (out->loading ? "; more coming, placeholders shown" : out->more ? "; more coming" : "");
    if (note != e.lastBoardNote) {
      e.lastBoardNote = note;
      e.logNotes.push_back(std::move(note));
    }
  }
}

std::shared_ptr<const EventsBoard> Store::boardNow() {
  std::shared_ptr<events_build::BoardInput> in = boardInput();
  in->cameras = std::make_shared<sports::cameras::CameraIndex>();  // not the worker's
  in->titles.reset();
  events_build::BoardMade made = events_build::board(*in);
  made.events->version = events_->version;
  boardFlags(*made.events);
  return made.events;
}

void Store::boardFlags(EventsBoard& out) const {
  const Impl& m = *impl_;
  const EventsState& e = m.ev;
  // Open and still coming in: the list, the saved board, the schedule, a
  // rebuild, or games worked out since an open on old ones.
  bool coming = eventsOpen_ > 0 && (m.loading || e.restoring || e.schedule.fetching() || e.rebuilding ||
                                    !e.checkedSinceOpen || e.eventsBuiltAt < e.freshAfter || e.holding);
  // Opened on an old board (or none): placeholders in place of the rows in
  // until the whole board is, so it never shows part and then moves.
  bool whole = coming && e.wholeFirst;
  out.loading = whole || (coming && out.rows.empty()) || (e.schedule.fetching() && e.events->empty());
  out.more = coming && !out.rows.empty() && !whole;
  out.stage = out.loading || out.more ? loadStage() : EventsBoard::Stage::None;
}

EventsBoard::Stage Store::loadStage() const {
  const Impl& m = *impl_;
  const EventsState& e = m.ev;
  events_build::Loading l;
  l.list = m.loading;
  l.schedule = e.schedule.fetching();
  l.matching = e.rebuilding || e.restoring || !e.checkedSinceOpen || e.eventsBuiltAt < e.freshAfter;
  l.holding = e.holding;
  if (l.holding && !l.list && !l.schedule && !l.matching)
    l.hold = events_build::holdFor(eventsInputs(), time::now(), host_.zone());
  return events_build::loadStage(l);
}

void Store::restage(EventsBoard::Stage stage) {
  auto out = std::make_shared<EventsBoard>(*events_);
  out->version = events_->version + 1;
  out->stage = stage;
  impl_->ev.retired.push_back(std::move(events_));
  events_ = out;
}

void Store::showPlaceholders() {
  auto out = std::make_shared<EventsBoard>();
  out->version = events_->version + 1;
  out->loading = true;
  out->stage = loadStage();
  out->scoresOn = settings_.showScores;
  impl_->ev.retired.push_back(std::move(events_));
  events_ = out;
}

namespace events_build {

bool keptBoardShows(time::Seconds builtAt, time::Seconds now, const time::Zone& zone) {
  if (builtAt <= 0 || now < builtAt || now - builtAt >= kKeptBoardFor) return false;
  time::Civil a = zone.toLocal(builtAt), b = zone.toLocal(now);
  return a.year == b.year && a.month == b.month && a.day == b.day;
}

bool listFresh(time::Seconds fetchedAt, time::Seconds now) {
  return fetchedAt > 0 && now >= fetchedAt && now - fetchedAt < kListFreshFor;
}

bool scheduleCurrent(time::Seconds fetchedAt, time::Seconds now, const time::Zone& zone) {
  if (fetchedAt <= 0 || now < fetchedAt || now - fetchedAt >= sports::schedule::kFreshFor) return false;
  time::Civil a = zone.toLocal(fetchedAt), b = zone.toLocal(now);
  return a.year == b.year && a.month == b.month && a.day == b.day;
}

Hold holdFor(const Inputs& in, time::Seconds now, const time::Zone& zone) {
  // Old, and a newer one is coming or already in (the games not matched
  // from it yet).
  Hold h;
  h.list = !listFresh(in.made.listAt, now) && (in.listComing || in.made.listAt != in.listAt);
  h.schedule = !scheduleCurrent(in.made.scheduleAt, now, zone) &&
               (in.scheduleComing || in.made.scheduleAt != in.scheduleAt);
  h.scores = !in.scoresChecked && in.scoresAwaited;
  return h;
}

EventsBoard::Stage loadStage(const Loading& l) {
  using Stage = EventsBoard::Stage;
  if (l.list) return Stage::Channels;
  if (l.schedule) return Stage::Schedule;
  if (l.matching) return Stage::Matching;
  if (l.holding) return l.hold.list || l.hold.schedule ? Stage::Matching : Stage::Scores;
  return Stage::Matching;
}

}  // namespace events_build

void Store::buildLiveEventsReport(std::function<void(std::shared_ptr<std::string>)> done) {
  const Impl& m = *impl_;
  const EventsState& e = m.ev;
  // Copies of what the report reads; the worker owns them.
  struct Job {
    std::shared_ptr<const CatalogResult> catalog;
    std::shared_ptr<const std::vector<sports::SportsEvent>> events;
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
        in.events = job->events.get();
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
  // The tick's own time, for the timing log, however it returns.
  struct Timed {
    Store& store;
    uint32_t ms;
    Clock::time_point start = Clock::now();
    ~Timed() { store.noteEventsTiming(ms, start); }
  } timed{*this, ms};
  // No tick for minutes (the TV was off, the app asleep) with Live Events
  // open: opened again on waking, on fresh inputs.
  time::Seconds wall = time::now();
  uint32_t gapMs = ms >= e.lastTickMs ? ms - e.lastTickMs : 0;
  time::Seconds away = std::max<time::Seconds>(wall - e.lastTickAt, static_cast<time::Seconds>(gapMs / 1000));
  bool woke = e.lastTickAt && eventsOpen_ > 0 && away >= events_build::kAsleepFor;
  e.lastTickAt = wall;
  e.lastTickMs = ms;
  if (woke) eventsArrive(away);
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
  // A board made on a worker goes in; boards no screen holds any more are
  // freed on a worker.
  if (e.boardDone) putBoard();
  for (auto it = e.retired.begin(); it != e.retired.end();) {
    if (it->use_count() > 1) {
      ++it;
      continue;
    }
    std::shared_ptr<const EventsBoard> old = std::move(*it);
    it = e.retired.erase(it);
    host_.pool.post([old]() mutable { old.reset(); }, nullptr, http::Priority::Background);
  }
  // Closed: the day's schedule in the background (Build 125).
  if (eventsOpen_ <= 0) scheduleInBackground(ms);
  // Closed: only the launch's first pass (the saved list in, a rebuild and a
  // board from it).
  if (eventsOpen_ <= 0 && !e.events->empty() && !e.launchPass) return;
  if (m.channels->empty()) return;
  if (eventsOpen_ > 0) {
    pumpSchedule(ms);
    pumpScores(ms);
  }
  if (!e.restoredTried) {
    // The list saved last time, read on a worker: shown until the rebuild.
    e.restoredTried = true;
    if (auto text = host_.files.read(kResultFile)) {
      e.restoring = true;
      struct Restore {
        std::string text;
        std::optional<ss::Restored> restored;
        std::shared_ptr<const sports::matcher::SlotsSeen> slotsSeen;
        time::Seconds savedAt = 0;
      };
      auto job = std::make_shared<Restore>();
      job->text = std::move(*text);
      auto catalog = m.catalog;
      uint64_t gen = m.generation;
      host_.pool.post(
          [job, catalog] {
            if (auto saved = ss::savedResultFromJson(job->text)) {
              job->savedAt = saved->savedAt;
              job->slotsSeen = saved->slotsSeen;
              job->restored = ss::restore(*saved, catalog->events, time::now());
              // A scoreboard's word saved with a game is from then: the
              // clock decides until scores answer again.
              if (job->restored)
                for (sports::SportsEvent& ev : job->restored->events) ev.live.reset();
            }
            job->text.clear();
          },
          [this, gen, job] {
            EventsState& ev = impl_->ev;
            ev.restoring = false;
            if (gen != impl_->generation) return;
            ev.launchPass = true;  // the next tick rebuilds and makes the board, open or not
            if (job->slotsSeen && !ev.slotsSeen) ev.slotsSeen = std::move(job->slotsSeen);
            if (!job->restored) return;
            ev.events = std::make_shared<const std::vector<sports::SportsEvent>>(std::move(job->restored->events));
            ++ev.eventsSerial;
            if (ev.listings.empty()) ev.listings = std::move(job->restored->listings);
            ev.boardDirty = true;
            ev.eventsBuiltAt = job->savedAt;
            // Matched last session: from no list fetched in this one.
            ev.madeFrom = {0, ev.schedule.saved().fetchedAt.value_or(0)};
            time::Seconds age = std::max<time::Seconds>(0, time::now() - job->savedAt);
            // Opened before it was in: shown (made from the clock) only while new.
            if (eventsOpen_ > 0 && events_build::keptBoardShows(job->savedAt, time::now(), host_.zone())) {
              ev.freshAfter = 0;
              ev.wholeFirst = false;  // shown at once, as a board kept from before
            }
            ev.logNotes.push_back("saved board from " + ageText(age) + " ago shown until the rebuild");
          },
          http::Priority::Visible);
    }
  }
  if (e.restoring) return;
  e.launchPass = false;
  if (e.holding) {
    // Waiting for fresh inputs: a list or schedule just in is matched at
    // once; the wait ends when nothing old decides a tile, or after
    // kHoldAtMostMs (every tile then, live only by recent scores or the clock).
    events_build::Inputs inputs = eventsInputs();
    if (inputs.made.listAt != inputs.listAt || inputs.made.scheduleAt != inputs.scheduleAt)
      e.nextCheckMs = std::min(e.nextCheckMs, ms);
    events_build::Hold h = events_build::holdFor(inputs, time::now(), host_.zone());
    bool capped = ms - e.holdSinceMs >= events_build::kHoldAtMostMs;
    if (!h.any() || capped) {
      e.holding = false;
      e.holdCapped = h.any();
      if (e.boardHold.any()) e.boardDirty = true;
    } else if (h != e.boardHold) {
      e.boardDirty = true;  // something it waited for is in
    }
  }
  if (ms >= e.nextCheckMs) {
    e.nextCheckMs = ms + kCheckMs;
    rebuildEvents();
  }
  // Loading since the open ends once the channel list, the schedule and a
  // rebuild from them are in.
  if (m.loading || e.schedule.fetching() || e.rebuilding) e.checkedSinceOpen = false;
  // The placeholders' time is up (the list or the schedule still coming,
  // the scores' wait over by then): the board as it is, now.
  if (e.wholeFirst && ms - e.holdSinceMs >= kPlaceholdersAtMostMs) {
    e.wholeFirst = false;
    e.boardDirty = true;
  }
  // One board at a time: asked for meanwhile, the next starts once it is in.
  // Scores, a rebuild and the clock wait a moment after the last began, so
  // a burst of answers makes one board; your own changes don't wait.
  if (e.scores.revision() != e.statusesRevision) e.boardStale = true;
  if (eventsOpen_ > 0 && ms - e.boardBuiltMs > kBoardRefreshMs) e.boardStale = true;
  if (eventsOpen_ > 0) {
    // Loading ended or began since the board shown was made: its
    // placeholders come or go.
    EventsBoard flags;
    flags.rows.resize(events_->rows.empty() ? 0 : 1);
    boardFlags(flags);
    if (flags.loading != events_->loading || flags.more != events_->more) {
      e.boardStale = true;
    } else if (flags.stage != events_->stage && events_->loading) {
      // The next stage of a load the screen names (under the placeholders;
      // with rows shown it names none): the placeholders' board again with
      // it at once; a board with rows waiting under them is made again.
      if (events_->rows.empty()) restage(flags.stage);
      else e.boardStale = true;
    }
  }
  // Games too old to show (an open on old ones) wait to be worked out again.
  bool fresh = e.eventsBuiltAt >= e.freshAfter;
  if (fresh && !e.boardBusy && (e.boardDirty || (e.boardStale && ms - e.boardStartMs >= kBoardGapMs))) startBoard(ms);
}

// The timing log: counts and milliseconds, a line at most every minute
// while boards are made (never a title or a link).
void Store::noteEventsTiming(uint32_t ms, std::chrono::steady_clock::time_point start) {
  EventsState::Timing& t = impl_->ev.timing;
  t.uiMax = std::max(t.uiMax, std::chrono::duration<double, std::milli>(Clock::now() - start).count());
  if (!t.since) t.since = ms;
  if (ms - t.since < kTimingLogMs) return;
  if (t.boards || t.rebuilds) {
    double minutes = (ms - t.since) / 60000.0;
    MLOG_I("events", "timing boards=%d perMin=%.1f boardMax=%.0fms tiles=%zu rebuilds=%d rebuildMax=%.0fms uiMax=%.1fms",
           t.boards, t.boards / minutes, t.boardMax, t.tiles, t.rebuilds, t.rebuildMax, t.uiMax);
  }
  t = EventsState::Timing{};
  t.since = ms;
}

// ------------------------------------------------------------ lookups

namespace {

sb::ChannelFor makeChannelFor(const Store::Impl& m, std::shared_ptr<const Channels> ch) {
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
}

}  // namespace

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

const EventTile* Store::eventTile(const std::string& id) const { return events_->tile(id); }

WhereToWatch Store::whereToWatch(const std::string& eventId) const {
  const Impl& m = *impl_;
  WhereToWatch w;
  auto ev = m.ev.board.events.find(eventId);
  if (ev == m.ev.board.events.end()) return w;
  sb::WherePanel p = sb::wherePanel(m.ev.board, ev->second, makeChannelFor(m, channels_),
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
    s.sets.clear();
    s.pensText.clear();
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
  auto at = impl_->familyAt.find(familyId);
  if (at == impl_->familyAt.end()) return {};
  std::vector<std::string> out;
  for (const std::string& id : impl_->catalog->families[at->second].feeds)
    if (impl_->indexByID.count(id)) out.push_back(id);
  return out;
}

std::vector<std::string> Store::eventFlipList(const std::string& eventId) const {
  std::vector<std::string> list = sb::flipEvents(impl_->ev.board, eventId, time::now());
  if (list.empty()) list.push_back(eventId);
  return list;
}

std::vector<std::string> Store::eventBackups(const std::string& entryId) const {
  const EventsState& e = impl_->ev;
  if (e.events->empty()) return {};
  return ss::eventBackupFeeds(entryId, *e.events, ss::backupIndex(*e.events), time::now());
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
  ++e.mySportsRevision;
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
