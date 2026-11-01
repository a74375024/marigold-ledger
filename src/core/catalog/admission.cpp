#include "core/catalog/admission.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>

#include "core/catalog/catalog.hpp"
#include "core/catalog/text.hpp"
#include "core/util/strings.hpp"

namespace marigold::catalog {
namespace {

const std::string& groupKey(const Channel& c) {
  static const std::string kNone;
  if (c.group && !c.group->empty()) return *c.group;
  if (!c.tags.empty() && !c.tags.front().empty()) return c.tags.front();
  return kNone;
}

// A slot with nothing on it: a placeholder by kindFor()'s words, or one
// dated at the providers' "nothing scheduled" mark ("(VIX 17) |
// (2098-12-31 08:00:01)").
bool emptySlot(const Channel& c) {
  std::string lower = str::lower(c.name);
  return placeholderName(lower) || text::contains(lower, "2098-12-31") || text::contains(lower, "2099-01-0");
}

}  // namespace

Admission admitGroups(const std::vector<Channel>& channels, size_t cap, const GroupRank& rank) {
  // The groups and their channels, in first-seen (provider) order.
  std::unordered_map<std::string, size_t> indexOf;
  std::vector<const std::string*> names;
  std::vector<std::vector<const Channel*>> members;
  std::vector<size_t> groupOf(channels.size());
  for (size_t i = 0; i < channels.size(); ++i) {
    const std::string& key = groupKey(channels[i]);
    auto [it, inserted] = indexOf.try_emplace(key, members.size());
    if (inserted) {
      names.push_back(&it->first);
      members.emplace_back();
    }
    members[it->second].push_back(&channels[i]);
    groupOf[i] = it->second;
  }
  std::vector<GroupPlace> places(members.size());
  if (rank) {
    for (size_t g = 0; g < members.size(); ++g) places[g] = rank(*names[g], members[g]);
  }
  // By rank, then by place within it, then provider order.
  std::vector<size_t> order(members.size());
  for (size_t g = 0; g < order.size(); ++g) order[g] = g;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    if (places[a].rank != places[b].rank) return places[a].rank < places[b].rank;
    return places[a].within < places[b].within;
  });
  Admission out;
  std::vector<bool> admitted(members.size(), false);
  size_t total = 0;
  for (size_t g : order) {
    size_t size = members[g].size();
    if (places[g].rank < 0) {
      out.leftOut += 1;
    } else if (size <= cap - total) {
      admitted[g] = true;
      total += size;
      out.admittedGroups += 1;
    } else {
      out.cutGroups += 1;
    }
  }
  out.channels.reserve(total);
  for (size_t i = 0; i < channels.size(); ++i) {
    if (admitted[groupOf[i]]) out.channels.push_back(channels[i]);
  }
  return out;
}

bool freeStreamingGroup(const std::string& group, const std::vector<const Channel*>& members) {
  if (std::any_of(members.begin(), members.end(), [](const Channel* c) { return c->isFavorite; })) return false;
  std::string lower = str::lower(group);
  if (loopName(group, lower)) return true;
  // "IT| AMAZON PRIME PPV" is Prime's football, not its free channels.
  if (freeServiceGroup(lower)) return !isEventGroup(lower);
  size_t free = 0;
  for (const Channel* c : members) {
    if (freeServiceName(c->name) || loopName(c->name, str::lower(c->name))) free += 1;
  }
  return free * 4 >= members.size() * 3;
}

GroupRank countryRank(const std::set<std::string>& wanted, bool freeOn) {
  return [wanted, freeOn](const std::string& group, const std::vector<const Channel*>& members) {
    GroupPlace place;
    // Free streaming and 24/7 channels the viewer doesn't watch: after
    // every group Live Events finds games on.
    auto unwatched = [&] { return !freeOn && freeStreamingGroup(group, members); };
    if (wanted.empty()) {
      place.rank = unwatched() ? 3 : 0;
      return place;
    }
    const Channel& first = *members.front();
    Channel byGroup;  // the group's name alone ("UK| GENERAL" on Xtream)
    byGroup.serverId = first.serverId;
    byGroup.group = group;
    std::optional<std::string> groupCountry = markedCountry(byGroup);
    if (groupCountry && !wanted.count(*groupCountry) && !sportsGroup(group)) {
      place.rank = -1;
      return place;
    }
    if (unwatched()) {
      place.rank = 3;
      return place;
    }
    std::optional<std::string> country = groupCountry ? groupCountry : markedCountry(first);
    if (!country) {
      place.rank = 1;
    } else if (wanted.count(*country)) {
      place.rank = 0;
    } else {
      // Another country's sports: the emptiest groups (mostly "No Event
      // Streaming" or ended slots) last, so a cut takes them first.
      place.rank = 2;
      size_t empty = 0;
      for (const Channel* c : members) empty += emptySlot(*c) ? 1 : 0;
      place.within = static_cast<int>(empty * 1000 / members.size());
    }
    return place;
  };
}

uint64_t fingerprint(const std::vector<Channel>& channels, const std::set<std::string>& also) {
  uint64_t h = 0xcbf29ce484222325ull;
  auto mix = [&h](unsigned char c) { h = (h ^ c) * 0x100000001b3ull; };
  auto text = [&mix](const std::string& t) {
    for (char c : t) mix(static_cast<unsigned char>(c));
    mix(0xff);
  };
  auto opt = [&](const std::optional<std::string>& t) {
    mix(t ? 1 : 0);
    if (t) text(*t);
  };
  for (const Channel& c : channels) {
    text(c.id);
    text(c.serverId);
    text(c.itemId);
    text(c.name);
    opt(c.number);
    text(c.imageUrl);
    mix(c.isFavorite ? 1 : 0);
    mix(static_cast<unsigned char>(c.tags.size()));
    for (const std::string& t : c.tags) text(t);
    mix(c.isRadio ? 1 : 0);
    opt(c.group);
    text(c.shortName);
    opt(c.epgId);
    mix(0xfe);
  }
  for (const std::string& a : also) text(a);
  return h;
}

}  // namespace marigold::catalog
