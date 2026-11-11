// Build 153: a row's listings by channel identity (guide/choice.hpp). Which
// rows are weighed (My Lineup, favourites, rows asked for on screen, rows
// with a pick), their copies' guides gathered on the UI thread (pointers to
// shared lists, a few notes), the weighing on a worker, and "Listings look
// wrong?" (the viewer's pick, saved with the lists).
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/engine/store.hpp"
#include "core/engine/store_impl.hpp"
#include "core/guide/choice.hpp"
#include "core/guide/provider.hpp"
#include "core/util/strings.hpp"

namespace marigold::engine {
namespace {

// A row's copies looked at: a row of a hundred copies of one network has
// its schedules in its first few dozen.
constexpr size_t kMaxMembers = 40;

}  // namespace

bool Store::weighsGuide(const std::string& entryId) const {
  const Impl& m = *impl_;
  return m.lineup.lineupIDs.count(entryId) > 0 || m.choiceRows.count(entryId) > 0 ||
         m.favorites.count(entryId) > 0 || m.guidePicks.count(entryId) > 0;
}

void Store::decideRows(const std::vector<std::string>& entryIDs) {
  Impl& m = *impl_;
  if (entryIDs.empty() || !m.catalog) return;
  struct Row {
    std::string id;
    guide::choice::Row row;
    std::vector<guide::choice::MemberGuide> members;
    std::string pick;
    uint64_t serial = 0;
    // The worker's answer.
    std::shared_ptr<const std::vector<Program>> list;
    std::shared_ptr<Impl::RowGuide> guide;
    bool pickGone = false;
    std::vector<std::string> sources;  // of its schedules
  };
  auto job = std::make_shared<std::vector<Row>>();
  job->reserve(entryIDs.size());
  std::unordered_map<std::string, const std::map<std::string, std::string>*> notesOf;  // by server id
  auto providerNotes = [&](const std::string& serverId) -> const std::map<std::string, std::string>* {
    auto it = notesOf.find(serverId);
    if (it != notesOf.end()) return it->second;
    auto saved = m.providerSaved.find(guide::provider::cacheKey("default", serverId));
    const std::map<std::string, std::string>* notes = saved == m.providerSaved.end() ? nullptr : &saved->second.notes;
    notesOf.emplace(serverId, notes);
    return notes;
  };
  const std::map<std::string, std::string>* outsideNotes =
      m.outsideSaved && m.outsideSaved->notes ? &*m.outsideSaved->notes : nullptr;
  const guide::ApartLists* apart = m.outsideSaved ? &m.outsideSaved->others : nullptr;
  for (const std::string& id : entryIDs) {
    auto info = m.entryInfo.find(id);
    if (info == m.entryInfo.end() || info->second >= m.catalog->entries.size()) continue;
    const catalog::EntryInfo& entry = m.catalog->entries[info->second];
    Row r;
    r.id = id;
    r.row = guide::choice::rowOf(id, entry.country);
    for (const std::string& member : entry.members) {
      if (r.members.size() >= kMaxMembers) break;
      auto ci = m.indexByID.find(member);
      if (ci == m.indexByID.end() || ci->second >= m.channels->size()) continue;
      const Channel& c = (*m.channels)[ci->second];
      guide::choice::MemberGuide g;
      if (auto p = m.providerMatches.find(member); p != m.providerMatches.end()) g.provider = p->second;
      if (auto o = m.outsideMatches.find(member); o != m.outsideMatches.end()) g.outside = o->second;
      if (auto s = m.serverPrograms.find(member); s != m.serverPrograms.end() && !s->second.empty())
        g.server = std::make_shared<const std::vector<Program>>(s->second);
      if (apart) {
        if (auto a = apart->find(member); a != apart->end())
          for (const guide::Apart& list : a->second) g.others.emplace_back(list.programs, list.note);
      }
      if (!g.provider && !g.outside && !g.server && g.others.empty()) continue;
      g.id = member;
      g.serverId = c.serverId;
      g.epgId = c.epgId.value_or("");
      g.xtream = str::startsWith(c.serverId, "xt:");
      if (g.provider) {
        if (const auto* notes = providerNotes(c.serverId)) {
          if (auto n = notes->find(member); n != notes->end()) g.providerNote = n->second;
        }
      }
      if (g.outside && outsideNotes) {
        if (auto n = outsideNotes->find(member); n != outsideNotes->end()) g.outsideNote = n->second;
      }
      r.members.push_back(std::move(g));
    }
    if (auto pick = m.guidePicks.find(id); pick != m.guidePicks.end()) r.pick = pick->second;
    r.serial = ++m.choiceSerial;
    m.choiceAsked[id] = r.serial;
    job->push_back(std::move(r));
  }
  if (job->empty()) return;
  time::Seconds ws = windowStart();
  int now = static_cast<int>(time::now() / 60);
  uint64_t gen = m.generation;
  host_.pool.post(
      [job, ws, now] {
        for (Row& r : *job) {
          std::vector<guide::choice::Candidate> cands = guide::choice::candidates(r.members);
          guide::choice::Decision d = guide::choice::decide(r.row, cands, now, ws, r.pick);
          std::vector<Program> list = guide::choice::listing(d, cands, ws);
          if (!list.empty()) r.list = std::make_shared<const std::vector<Program>>(std::move(list));
          auto g = std::make_shared<Impl::RowGuide>();
          if (d.chosen >= 0) g->chosen = cands[static_cast<size_t>(d.chosen)].key;
          if (d.automatic >= 0) g->automatic = cands[static_cast<size_t>(d.automatic)].key;
          g->picked = d.picked;
          g->why = std::move(d.why);
          g->rejected = std::move(d.rejected);
          for (const guide::choice::Option& o : d.options) {
            const guide::choice::Candidate& c = cands[o.index];
            // Only what the panel needs: its key, its lists and the shift.
            Impl::RowGuide::Option kept;
            kept.key = c.key;
            kept.outside = c.outside;
            kept.channelId = c.channelId;
            kept.raw = c.raw;
            kept.extra = c.extra;
            kept.shift = o.shift;
            g->options.push_back(std::move(kept));
          }
          for (const guide::choice::Candidate& c : cands) r.sources.push_back(c.source);
          r.guide = std::move(g);
          r.pickGone = d.pickGone;
          r.members.clear();
          r.members.shrink_to_fit();
        }
      },
      [this, gen, job] {
        Impl& mm = *impl_;
        if (gen != mm.generation) return;
        // A pick is dropped once its schedule is gone: the row still has
        // listings from that source, or the source's guide is in without it.
        auto sourceIn = [&mm](const std::string& source) {
          if (str::startsWith(source, "pg:"))
            return mm.providerSaved.count(guide::provider::cacheKey("default", source.substr(3))) > 0;
          if (str::startsWith(source, "og:"))
            return mm.outsideSaved && mm.outsideSaved->sources && mm.outsideSaved->sources->count(source.substr(3)) > 0;
          return true;
        };
        bool picksChanged = false;
        for (Row& r : *job) {
          auto asked = mm.choiceAsked.find(r.id);
          if (asked == mm.choiceAsked.end() || asked->second != r.serial) continue;  // a newer one is coming
          mm.choiceAsked.erase(asked);
          if (!mm.entryInfo.count(r.id)) continue;
          if (r.pickGone && !r.pick.empty()) {
            std::string source = r.pick.substr(0, r.pick.find('|'));
            bool listed = std::find(r.sources.begin(), r.sources.end(), source) != r.sources.end();
            if ((listed || sourceIn(source)) && mm.guidePicks.erase(r.id)) picksChanged = true;
          }
          std::string key;
          if (r.list && !r.list->empty()) {
            int tagged = 0, described = 0;
            for (const Program& p : *r.list) {
              tagged += p.genres ? 1 : 0;
              described += p.overview.empty() ? 0 : 1;
            }
            key = r.guide->chosen + "|" + std::to_string(r.list->size()) + "|" + r.list->front().id + "|" +
                  r.list->back().id + "|" + std::to_string(tagged) + "|" + std::to_string(described);
          }
          if (key.empty() || mm.entryGuideKeys[r.id] != key || !mm.programs.count(r.id)) {
            bool had = mm.programs.count(r.id) > 0;
            mm.entryGuideKeys[r.id] = key;
            if (r.list) mm.programs[r.id] = r.list;
            else mm.programs.erase(r.id);
            if (had || r.list) mm.dirtyEntries.insert(r.id);
          }
          if (r.list && mm.guideState[r.id] != Impl::GuideState::Loaded) {
            mm.guideState[r.id] = Impl::GuideState::Loaded;
            mm.dirtyEntries.insert(r.id);
          }
          mm.rowGuides[r.id] = std::move(r.guide);
        }
        if (!mm.dirtyEntries.empty()) mm.listingsDirty = true;
        if (picksChanged) saveListsLater();
      },
      http::Priority::Normal);
}

std::vector<Store::GuideOption> Store::guideOptions(const std::string& entryId) const {
  const Impl& m = *impl_;
  std::vector<GuideOption> out;
  auto it = m.rowGuides.find(entryId);
  if (it == m.rowGuides.end() || !it->second) return out;
  const Impl::RowGuide& g = *it->second;
  time::Seconds ws = windowStart();
  time::Seconds now = time::now();
  for (const Impl::RowGuide::Option& o : g.options) {
    guide::choice::Candidate schedule;
    schedule.outside = o.outside;
    schedule.channelId = o.channelId;
    schedule.raw = o.raw;
    schedule.extra = o.extra;
    std::vector<Program> list = guide::choice::programsOf(schedule, o.shift, ws);
    GuideOption option;
    option.key = o.key;
    option.current = o.key == g.chosen;
    time::Seconds after = now;
    for (const Program& p : list) {
      if (p.isOn(now) && option.now.empty()) {
        option.now = p.title;
        after = p.end;
      }
    }
    for (const Program& p : list) {
      if (p.start >= after - 60 && p.start > now) {
        option.next = p.title;
        option.nextStart = p.start;
        break;
      }
    }
    if (option.now.empty() && option.next.empty()) continue;
    // Two schedules that read the same on the panel are one line.
    bool same = std::any_of(out.begin(), out.end(), [&](const GuideOption& o2) {
      return o2.now == option.now && o2.next == option.next && o2.nextStart == option.nextStart;
    });
    if (same && !option.current) continue;
    out.push_back(std::move(option));
  }
  return out;
}

void Store::pickGuide(const std::string& entryId, const std::string& key) {
  Impl& m = *impl_;
  auto it = m.rowGuides.find(entryId);
  std::string automatic = it != m.rowGuides.end() && it->second ? it->second->automatic : std::string();
  if (key.empty() || key == automatic) m.guidePicks.erase(entryId);
  else m.guidePicks[entryId] = key;
  saveListsLater();
  decideRows({entryId});
}

bool Store::guidePicked(const std::string& entryId) const { return impl_->guidePicks.count(entryId) > 0; }

bool Store::hasGuideChoice(const std::string& entryId) const {
  auto it = impl_->rowGuides.find(entryId);
  return it != impl_->rowGuides.end() && it->second && !it->second->options.empty();
}

}  // namespace marigold::engine
