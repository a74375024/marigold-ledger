// Build 163, the catch-up test (test only): which copies say they keep an
// archive, and the link that asks an Xtream provider for a moment of it.
// The link carries the login, like a live one: it goes to the player only.
#include "core/engine/store.hpp"
#include "core/engine/store_impl.hpp"
#include "core/sources/xtream.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace marigold::engine {
namespace {

// The raw channel and the Xtream account it comes from; null for an M3U or
// Emby channel, or one no longer listed.
const sources::Account* xtreamAccount(const std::vector<sources::Account>& accounts,
                                      const std::vector<Channel>& channels,
                                      const std::unordered_map<std::string, size_t>& index,
                                      const std::string& channelId, const Channel** channel) {
  auto ci = index.find(channelId);
  if (ci == index.end() || ci->second >= channels.size()) return nullptr;
  const Channel& c = channels[ci->second];
  for (const sources::Account& a : accounts) {
    if (a.sourceId() != c.serverId) continue;
    if (a.kind != sources::AccountKind::Xtream) return nullptr;
    *channel = &c;
    return &a;
  }
  return nullptr;
}

}  // namespace

uint16_t Store::catchupDays(const std::string& channelId) const {
  const Impl& m = *impl_;
  const Channel* c = nullptr;
  return xtreamAccount(m.accounts, *m.channels, m.indexByID, channelId, &c) ? c->archiveDays : 0;
}

std::optional<Store::CatchupFeed> Store::catchupFeed(const Feed& live, time::Seconds from, time::Seconds now) const {
  const Impl& m = *impl_;
  const Channel* c = nullptr;
  const sources::Account* account = xtreamAccount(m.accounts, *m.channels, m.indexByID, live.channelId, &c);
  if (!account || c->archiveDays == 0 || from >= now) return std::nullopt;
  auto client = sources::xtream::Client::make(*account);
  if (!client) return std::nullopt;
  CatchupFeed out;
  out.serverOffset = account->serverOffset;
  out.minutes = sources::xtream::timeshiftMinutes(from, now);
  std::optional<std::string> url =
      client.value().timeshiftURL(c->itemId, from, out.minutes, account->serverOffset.value_or(0));
  if (!url) return std::nullopt;
  out.feed = live;
  out.feed.url = std::move(*url);
  out.feed.headers.clear();
  out.feed.needsOpen = false;
  return out;
}

}  // namespace marigold::engine
