#include "core/report/live_events_report.hpp"

#include <algorithm>
#include <unordered_map>

#include "core/catalog/catalog.hpp"
#include "core/report/csv.hpp"
#include "core/util/redact.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::report {
namespace {

std::string stamp(time::Seconds t, const time::Zone& zone) {
  time::Civil c = zone.toLocal(t);
  auto pad = [](int v) { return v < 10 ? "0" + std::to_string(v) : std::to_string(v); };
  return std::to_string(c.year) + "-" + pad(c.month) + "-" + pad(c.day) + " " + pad(c.hour) + ":" + pad(c.minute);
}

// Why the channel list didn't read a slot as an event (catalog.cpp's rules).
std::string slotProblem(const catalog::ChannelTraits& t, time::Seconds now) {
  const std::string& text = *t.eventText;
  if (str::trim(text).empty()) return "an empty slot";
  if (catalog::isPlaceholder(text) || t.kind == catalog::ChannelKind::placeholder ||
      rx::test("^\\W*(ended|end)\\b", text, true))
    return "a placeholder or ended slot";
  catalog::ParsedEvent parsed = catalog::parseEventDetailed(text, now, t.market);
  if (parsed.title.empty()) return "no event title in the slot's name";
  if (parsed.start && now - *parsed.start > 8 * 3600) return "long over (by its time)";
  if (parsed.start && *parsed.start - now > 6 * 24 * 3600) return "more than 6 days away";
  return "not read as an event";
}

}  // namespace

std::string liveEventsCsv(const LiveEventsReportInput& in) {
  static const char* const kColumns[] = {"group",  "feed",   "slot", "read_as", "sport",       "starts",
                                         "result", "reason", "tile", "rows",    "group_starts"};
  std::string out;
  for (size_t i = 0; i < std::size(kColumns); ++i) out += (i ? "," : "") + std::string(kColumns[i]);
  out += "\n";
  if (!in.catalog) return out;
  const catalog::CatalogResult& cat = *in.catalog;

  std::unordered_map<std::string, const catalog::LiveEvent*> eventOf;
  for (const catalog::LiveEvent& e : cat.events)
    for (const std::string& feed : e.feeds) eventOf.emplace(feed, &e);
  std::unordered_map<std::string, std::string> familyName;
  for (const catalog::EventFamily& f : cat.families) {
    familyName.emplace(f.id, f.name);
    if (str::startsWith(f.id, "fam|")) familyName.emplace(f.id.substr(4), f.name);
  }
  // Feed -> the rebuilt event carrying it; a card's part -> its tile.
  std::unordered_map<std::string, const sports::SportsEvent*> tileOf;
  if (in.events) {
    for (const sports::SportsEvent& e : *in.events)
      for (const sports::EventSource& s : e.sources)
        if (s.kind == sports::EventSource::Kind::provider) tileOf.emplace(s.channelID, &e);
  }
  std::map<std::string, std::string> cardOf;
  std::map<std::string, std::vector<std::string>> rowsOf;
  if (in.board) {
    for (const auto& [tile, parts] : in.board->cards)
      for (const sports::SportsEvent& p : parts) cardOf.emplace(p.id, tile);
    for (const sports::board::Row& r : in.board->rows) {
      for (const sports::board::Group& g : r.groups) {
        for (const sports::board::Tile& t : g.tiles) {
          std::vector<std::string>& rows = rowsOf[t.id];
          if (std::find(rows.begin(), rows.end(), r.title) == rows.end()) rows.push_back(r.title);
        }
      }
    }
  }

  struct Slot {
    std::string family;
    int number;
    std::string id;
    const catalog::ChannelTraits* traits;
  };
  std::vector<Slot> slots;
  for (const auto& [id, t] : cat.traits) {
    if (!t.familyKey || !t.eventText) continue;
    slots.push_back({*t.familyKey, t.familyNumber.value_or(0), id, &t});
  }
  std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
    if (a.family != b.family) return a.family < b.family;
    return a.number != b.number ? a.number < b.number : a.id < b.id;
  });

  for (const Slot& slot : slots) {
    const catalog::ChannelTraits& t = *slot.traits;
    std::string group = familyName.count(slot.family) ? familyName[slot.family] : slot.family;
    std::string readAs, sport, starts, result, reason, tile, rows;
    auto ev = eventOf.find(slot.id);
    if (ev == eventOf.end()) {
      result = "not an event";
      reason = slotProblem(t, in.now);
    } else {
      const catalog::LiveEvent& e = *ev->second;
      readAs = e.title;
      sport = e.league;
      if (e.start) starts = stamp(*e.start, in.zone) + (e.startGuessed.value_or(false) ? " (day guessed)" : "");
      auto built = tileOf.find(slot.id);
      const std::string* hiddenWhy = nullptr;
      if (in.hidden) {
        auto w = in.hidden->find(slot.id);
        if (w != in.hidden->end()) hiddenWhy = &w->second;
      }
      result = "left out";
      if (built != tileOf.end()) {
        const sports::SportsEvent& s = *built->second;
        std::string id = cardOf.count(s.id) ? cardOf[s.id] : s.id;
        tile = s.title;
        const std::string* leftWhy = nullptr;
        if (in.board) {
          auto shown = in.board->events.find(id);
          if (shown != in.board->events.end()) tile = shown->second.title;
          auto left = in.board->left.find(s.id);
          if (left != in.board->left.end()) leftWhy = &left->second;
        }
        auto on = rowsOf.find(id);
        if (on != rowsOf.end()) {
          result = "on the board";
          rows = str::join(on->second, " | ");
          if (in.groupFails && in.groupFails(slot.family)) reason = "its group keeps failing to start: shown last";
        } else if (leftWhy) {
          reason = *leftWhy;
        } else if (s.league == catalog::kProWrestling && !in.proWrestlingOn) {
          reason = "pro wrestling (off in My Sports)";
        } else {
          reason = "not on the board";
        }
      } else if (hiddenWhy) {
        reason = *hiddenWhy;
      } else {
        reason = "no tile took it (no sport or matchup recognised)";
      }
    }
    std::string starts2 = in.groupStarts ? in.groupStarts(slot.family) : std::string();
    std::vector<std::string> cells = {group,  slot.number ? std::to_string(slot.number) : std::string(),
                                      *t.eventText, readAs, sport, starts, result, reason, tile, rows, starts2};
    for (size_t i = 0; i < cells.size(); ++i) {
      if (i) out += ",";
      out += csvCell(redact(cells[i]));
    }
    out += "\n";
  }
  return out;
}

}  // namespace marigold::report
