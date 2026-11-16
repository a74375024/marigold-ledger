#include "core/engine/game_replay.hpp"

#include "core/engine/replay.hpp"
#include "core/sports/board.hpp"
#include "core/sports/languages.hpp"

namespace marigold::engine::game_replay {

using sports::EventSource;
using State = sports::SportsEvent::State;

bool sameLanguage(std::string_view a, std::string_view b) {
  if (a.empty()) a = "en";
  if (b.empty()) b = "en";
  return a == b;
}

std::optional<Pick> pick(const sports::SportsEvent& event, State state, time::Seconds now, const Channels& channels,
                         const std::set<std::string>& viewer) {
  // (e) Under way or over: never before the start, postponed or untimed.
  if (state != State::live && state != State::mayHaveEnded && state != State::finished) return std::nullopt;
  if (!event.start) return std::nullopt;
  if (event.game && (event.game->postponed || event.game->isTimeTbd() || event.game->isDayOnly())) return std::nullopt;
  const time::Seconds start = *event.start;
  const time::Seconds from = replay::minuteOf(start - kBeforeStart);
  // (d) The language of the source OK plays.
  const EventSource* lead = sports::languages::lead(event.sources, viewer);
  std::string_view lang = lead ? std::string_view(lead->lang) : std::string_view();
  std::optional<Pick> best;
  const Channel* bestChannel = nullptr;
  bool bestLead = false;
  for (const EventSource& s : event.sources) {
    // (a) A guide channel whose listing has the game at its start.
    if (s.kind != EventSource::Kind::lineup && s.kind != EventSource::Kind::guide) continue;
    if (!s.from || *s.from > start + kListingSlack) continue;
    if (s.until && *s.until <= start) continue;
    if (lead && !sameLanguage(s.lang, lang)) continue;
    // (b) An archive not found broken; (c) holding the minute before the start.
    auto c = channels.find(s.channelID);
    if (c == channels.end() || c->second.days == 0) continue;
    if (!replay::covers(c->second.days, from, now)) continue;
    bool isLead = lead && lead->channelID == s.channelID;
    bool better = !best;
    if (best) {
      if (c->second.days != bestChannel->days) better = c->second.days > bestChannel->days;
      else if (c->second.lineup != bestChannel->lineup) better = c->second.lineup;
      else if (isLead != bestLead) better = isLead;
    }
    if (!better) continue;
    Pick p;
    p.channelId = s.channelID;
    p.channelName = c->second.name;
    p.start = start;
    p.from = from;
    p.days = c->second.days;
    best = std::move(p);
    bestChannel = &c->second;
    bestLead = isLead;
  }
  if (best) best->otherChannel = !bestLead;
  return best;
}

std::string startedLine(const Pick& p, const time::Zone& zone, bool twelveHour) {
  std::string line = "Started " + sports::board::clockText(p.start, zone, twelveHour);
  if (p.otherChannel && !p.channelName.empty()) line += " \xc2\xb7 on " + p.channelName;
  return line;
}

}  // namespace marigold::engine::game_replay
