// Build 177, Skip break in the store: a sports game's play-by-play, asked
// for only while the player says its replay plays (wantBreaks), read on a
// worker into its breaks (sports::plays), the body dropped there. One
// request at a time in the background's lane, with the score requests'
// politeness: spaced, capped, backed off, and a site that refused us (429,
// 403) left alone for the rest of the day, whichever of the two it refused.
// UI thread only.
#include <memory>

#include "core/engine/store.hpp"
#include "core/engine/store_impl.hpp"
#include "core/sports/leagues.hpp"
#include "core/sports/plays.hpp"
#include "core/util/log.hpp"

namespace marigold::engine {
namespace {

namespace plays = sports::plays;

// The board's game by id: today's, else the day shown's (Build 176).
const sports::SportsEvent* boardEvent(const EventsState& e, const std::string& id) {
  auto it = e.board.events.find(id);
  if (it != e.board.events.end()) return &it->second;
  auto day = e.dayBoardGames.events.find(id);
  return day == e.dayBoardGames.events.end() ? nullptr : &day->second;
}

}  // namespace

void Store::wantBreaks(const std::string& eventId) {
  EventsState& e = impl_->ev;
  const sports::SportsEvent* ev = eventId.empty() ? nullptr : boardEvent(e, eventId);
  if (!ev || !ev->game) return;
  const sports::SportsGame& g = *ev->game;
  const sports::League* league = sports::leagueByShort(g.leagueShort);
  if (!league) return;
  if (!plays::rulesFor(*league)) {
    // No play-by-play read for the league: said once, nothing else.
    if (e.breaksSaid.insert(eventId).second) {
      std::string note = "breaks: no play-by-play for " + g.leagueShort;
      MLOG_I("events", "%s", note.c_str());
      e.logNotes.push_back(std::move(note));
    }
    return;
  }
  // "NFL|401..." -> "401...".
  size_t bar = g.id.find('|');
  if (bar == std::string::npos) return;
  size_t end = g.id.find('|', bar + 1);
  std::string summaryId = g.id.substr(bar + 1, end == std::string::npos ? std::string::npos : end - bar - 1);
  if (summaryId.empty()) return;
  time::Seconds now = time::now();
  bool over = (ev->live && *ev->live == sports::LivePhase::final) || g.completed ||
               ev->state(now) == sports::SportsEvent::State::finished;
  e.plays.want({eventId, g.leagueShort, std::move(summaryId), !over}, now);
}

std::shared_ptr<const sports::plays::Breaks> Store::breaks(const std::string& eventId) const {
  return impl_->ev.plays.breaks(eventId);
}

void Store::pumpBreaks(uint32_t) {
  EventsState& e = impl_->ev;
  time::Seconds now = time::now();
  // A site refusing the score requests is left alone here too.
  for (const auto& [source, until] : e.scores.refusedUntil()) {
    auto mine = e.plays.refusedUntil().find(source);
    if (until > now && (mine == e.plays.refusedUntil().end() || mine->second < until)) e.plays.refuse(source, until);
  }
  std::optional<plays::Fetch::Ask> ask = e.plays.next(now);
  if (!ask) return;
  const sports::League* league = sports::leagueByShort(ask->league);
  std::optional<plays::Rules> rules = league ? plays::rulesFor(*league) : std::nullopt;
  if (!rules) {
    e.plays.cancel();
    return;
  }
  // Read on the worker; only the breaks come back.
  struct Read {
    std::optional<std::vector<plays::Break>> breaks;
    size_t seen = 0, timed = 0;
  };
  auto read = std::make_shared<Read>();
  http::Request q;
  q.url = ask->request.url;
  for (const sources::Header& h : ask->request.headers) q.headers.push_back(h.name + ": " + h.value);
  q.timeoutMs = static_cast<unsigned>(ask->request.timeoutSeconds) * 1000u;
  q.priority = http::Priority::Background;
  q.tag = "events.plays";
  q.maxBytes = plays::kMaxBodyBytes;
  q.afterTransfer = [read, rules = *rules, drives = plays::byDrive(*league), live = ask->live](http::Response& r) {
    if (r.status == 200) {
      plays::Parsed p = plays::parse(r.body, drives);
      read->seen = p.seen;
      read->timed = p.plays.size();
      if (p.ok) read->breaks = plays::detect(std::move(p.plays), rules, live, time::now());
    }
    std::string().swap(r.body);
  };
  host_.pool.submit(std::move(q), [this, read, id = ask->eventId, name = ask->league](http::Response&& r) {
    EventsState& ev = impl_->ev;
    size_t found = read->breaks ? read->breaks->size() : 0;
    bool read200 = r.status == 200 && read->breaks;
    ev.plays.done(id, r.status, std::move(read->breaks), time::now(), r.retryAfterS, host_.zone());
    if (r.status == 429 || r.status == 403)
      MLOG_W("events", "play-by-play refused (HTTP %d): left alone for the rest of the day", r.status);
    // Once a game a session: what its play-by-play gave (and, once, that
    // none came).
    std::string note;
    if (read200 && ev.breaksSaid.insert(id).second)
      note = read->timed ? "breaks: " + std::to_string(found) + " found in " + name + " play-by-play"
                         : "breaks: " + name + " play-by-play has no times";
    else if (!read200 && ev.breaksSaid.insert(id + "|none").second)
      note = "breaks: no " + name + " play-by-play for this game (" +
             (r.status == 200 ? std::string("unreadable") : r.status ? "HTTP " + std::to_string(r.status) : "no answer") +
             ")";
    if (note.empty()) return;
    MLOG_I("events", "%s", note.c_str());
    ev.logNotes.push_back(std::move(note));
  });
}

}  // namespace marigold::engine
