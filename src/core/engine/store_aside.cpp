// Build 181, "This feed is wrong…" (set_aside.hpp): feeds set aside for a
// channel, kept with the lists; the copy moves they ask for; what a copy's
// own listing and a channel's guide have on now, and the channels whose
// guide shows what a copy shows ("Which channel is it?").
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/engine/replay.hpp"
#include "core/engine/store.hpp"
#include "core/engine/store_impl.hpp"

namespace marigold::engine {
namespace {

// Build 182: a copy's live record and its replay record are apart.
bool sameFeed(const setaside::Record& r, const std::string& entryId, const std::string& copyId, bool replay) {
  return r.entryId == entryId && r.copyId == copyId && setaside::isReplay(r.scope) == replay;
}

}  // namespace

setaside::Record Store::setAside(setaside::Record r) {
  Impl& m = *impl_;
  if (r.at == 0) r.at = time::now();
  // Build 182: the scope agrees with the reason (a replay record stays one).
  bool replay = setaside::isReplay(r.scope);
  r.scope = setaside::scopeOf(r.reason, replay);
  auto nameOf = [&](const std::string& id) {
    if (const GuideChannel* g = channels_->find(id)) return g->name;
    auto info = m.entryInfo.find(id);
    return info == m.entryInfo.end() ? std::string() : m.catalog->entries[info->second].name;
  };
  r.entryName = nameOf(r.entryId);
  if (r.reason != setaside::Reason::OtherChannel || r.named == r.entryId || replay) r.named.clear();
  r.namedName = r.named.empty() ? std::string() : nameOf(r.named);
  // The provider's own identity of the copy, to find it again.
  auto ci = m.indexByID.find(r.copyId);
  if (ci != m.indexByID.end() && ci->second < m.channels->size()) {
    const Channel& c = (*m.channels)[ci->second];
    r.server = c.serverId;
    r.item = c.itemId;
    r.name = c.name;
    r.group = c.group.value_or("");
  }
  m.setAside.erase(std::remove_if(m.setAside.begin(), m.setAside.end(),
                                  [&](const setaside::Record& x) { return sameFeed(x, r.entryId, r.copyId, replay); }),
                   m.setAside.end());
  m.setAside.push_back(r);
  if (!r.named.empty()) m.movesBack.erase(r.copyId);
  saveListsLater();
  applyCopyMoves();
  m.channelsDirty = true;
  // (A replay record leaves the guide's listings as they are.)
  if (!replay) listingsAgain({r.entryId, r.named});
  return r;
}

bool Store::useAgain(const std::string& entryId, const std::string& copyId, bool replay) {
  Impl& m = *impl_;
  auto it = std::find_if(m.setAside.begin(), m.setAside.end(),
                         [&](const setaside::Record& x) { return sameFeed(x, entryId, copyId, replay); });
  if (it == m.setAside.end()) return false;
  setaside::Record r = *it;
  m.setAside.erase(it);
  bool stillNamed = std::any_of(m.setAside.begin(), m.setAside.end(), [&](const setaside::Record& x) {
    return x.copyId == copyId && !x.named.empty();
  });
  // Named as another channel: back to the channel it was set aside for.
  if (!r.named.empty() && !stillNamed) m.movesBack[copyId] = r.entryId;
  saveListsLater();
  applyCopyMoves();
  m.channelsDirty = true;
  if (!replay) listingsAgain({r.entryId, r.named});
  return true;
}

std::vector<setaside::Record> Store::setAsides() const {
  std::vector<setaside::Record> out(impl_->setAside.rbegin(), impl_->setAside.rend());
  std::stable_sort(out.begin(), out.end(),
                   [](const setaside::Record& a, const setaside::Record& b) { return a.at > b.at; });
  return out;
}

std::vector<setaside::Record> Store::setAsidesFor(const std::string& entryId) const {
  std::vector<setaside::Record> out;
  for (const setaside::Record& r : impl_->setAside)
    if (r.entryId == entryId) out.push_back(r);
  return out;
}

std::optional<setaside::Record> Store::setAsideOf(const std::string& entryId, const std::string& copyId,
                                                  bool replay) const {
  for (const setaside::Record& r : impl_->setAside)
    if (sameFeed(r, entryId, copyId, replay)) return r;
  return std::nullopt;
}

namespace {

// Any record of the copy for the channel, live or replay, in these scopes.
template <class... S>
bool anyIn(const std::vector<setaside::Record>& all, const std::string& entryId, const std::string& copyId,
           S... scopes) {
  for (const setaside::Record& r : all)
    if (r.entryId == entryId && r.copyId == copyId && ((r.scope == scopes) || ...)) return true;
  return false;
}

}  // namespace

bool Store::notPlayedOn(const std::string& entryId, const std::string& copyId) const {
  return anyIn(impl_->setAside, entryId, copyId, setaside::Scope::Whole);
}

bool Store::notReplayedOn(const std::string& entryId, const std::string& copyId) const {
  return anyIn(impl_->setAside, entryId, copyId, setaside::Scope::Whole, setaside::Scope::Replay);
}

bool Store::listingsSetAside(const std::string& entryId, const std::string& copyId) const {
  return anyIn(impl_->setAside, entryId, copyId, setaside::Scope::Whole, setaside::Scope::Listings);
}

bool Store::ownTableSetAside(const std::string& entryId, const std::string& copyId) const {
  return anyIn(impl_->setAside, entryId, copyId, setaside::Scope::Whole, setaside::Scope::Listings,
               setaside::Scope::Replay, setaside::Scope::ReplayListings);
}

std::vector<int> Store::feedNumbers(const std::string& entryId, const std::vector<Feed>& feeds) const {
  std::vector<int> taken;
  for (const setaside::Record& r : impl_->setAside)
    if (r.entryId == entryId && r.scope == setaside::Scope::Whole && r.feed > 0) taken.push_back(r.feed);
  std::vector<size_t> shown;
  for (size_t i = 0; i < feeds.size(); ++i)
    if (!notPlayedOn(entryId, feeds[i].channelId)) shown.push_back(i);
  std::vector<int> numbers = setaside::feedNumbers(shown.size(), taken);
  std::vector<int> out(feeds.size(), 0);
  for (size_t k = 0; k < shown.size(); ++k) out[shown[k]] = numbers[k];
  return out;
}

std::string Store::copyOnNow(const std::string& copyId, time::Seconds now) const {
  const Impl& m = *impl_;
  int minute = static_cast<int>(now / 60);
  auto p = m.providerMatches.find(copyId);
  if (p != m.providerMatches.end() && p->second)
    for (const guide::RawProgram& raw : *p->second)
      if (raw.s <= minute && minute < raw.end()) return raw.t;
  auto s = m.serverPrograms.find(copyId);
  if (s != m.serverPrograms.end())
    for (const Program& prg : s->second)
      if (prg.isOn(now)) return prg.title;
  return "";
}

std::string Store::guideOnNow(const std::string& entryId, time::Seconds now) const {
  const std::vector<Listing>* v = listings_ ? listings_->forChannel(entryId) : nullptr;
  if (v)
    for (const Listing& l : *v)
      if (l.isOn(now)) return l.title;
  return "";
}

std::vector<std::string> Store::sameShowNow(const std::string& copyId, const std::string& entryId, time::Seconds now,
                                            size_t limit) const {
  std::vector<std::string> lineup, others;
  std::string key = replay::titleKey(copyOnNow(copyId, now));
  if (key.empty() || !channels_ || !listings_) return {};
  for (const GuideChannel& g : channels_->all) {
    if (g.id == entryId || lineup.size() >= limit) continue;
    std::string on = guideOnNow(g.id, now);
    if (on.empty() || replay::titleKey(on) != key) continue;
    (g.inLineup ? lineup : others).push_back(g.id);
  }
  for (std::string& id : others)
    if (lineup.size() < limit) lineup.push_back(std::move(id));
  return lineup;
}

std::map<std::string, std::string> Store::copyMoves() const {
  const Impl& m = *impl_;
  std::map<std::string, std::string> out = m.movesBack;
  for (const setaside::Record& r : m.setAside)
    if (!r.named.empty()) out[r.copyId] = r.named;
  return out;
}

void Store::applyCopyMoves() {
  Impl& m = *impl_;
  std::map<std::string, std::string> moves = copyMoves();
  if (!m.catalog || !catalog::copiesToMove(*m.catalog, moves)) return;
  auto cat = std::make_shared<CatalogResult>(*m.catalog);
  catalog::moveCopies(*cat, moves);
  installCatalog(m.channels, std::move(cat), m.partial);
  if (!m.partial) saveCatalog();
}

bool Store::rematchSetAside() {
  Impl& m = *impl_;
  if (m.setAside.empty() || m.channels->empty()) return false;
  bool changed = false;
  auto find = [&m](const std::string& id) { return m.indexByID.count(id) > 0; };
  auto each = [&m](const auto& f) {
    for (const Channel& c : *m.channels) f(c);
  };
  // An entry's id now, for one it had before (catalog::EntryInfo::formerIds).
  auto entryNow = [&m](const std::string& id) {
    if (id.empty() || m.entryInfo.count(id)) return id;
    for (const catalog::EntryInfo& e : m.catalog->entries)
      if (std::find(e.formerIds.begin(), e.formerIds.end(), id) != e.formerIds.end()) return e.id;
    return id;
  };
  for (setaside::Record& r : m.setAside) {
    std::string id = setaside::rematch(r, find, each);
    if (!id.empty() && id != r.copyId) {
      r.copyId = id;
      auto ci = m.indexByID.find(id);
      if (ci != m.indexByID.end()) r.item = (*m.channels)[ci->second].itemId;
      changed = true;
    }
    for (std::string* e : {&r.entryId, &r.named}) {
      std::string now = entryNow(*e);
      if (now != *e) {
        *e = now;
        changed = true;
      }
    }
  }
  if (changed) saveListsLater();
  return changed;
}

void Store::listingsAgain(const std::vector<std::string>& entryIds) {
  Impl& m = *impl_;
  std::vector<std::string> ids;
  for (const std::string& id : entryIds)
    if (!id.empty() && m.entryInfo.count(id)) ids.push_back(id);
  if (!ids.empty()) applyEntryGuides(ids);
}

}  // namespace marigold::engine
