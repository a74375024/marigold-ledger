#include "core/sports/store.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <unordered_set>

#include "core/catalog/catalog.hpp"
#include "core/sports/brands.hpp"
#include "core/sports/languages.hpp"
#include "core/sports/text.hpp"
#include "core/sports/titles.hpp"
#include "core/util/json.hpp"

namespace marigold::sports::store {

// MARK: Guide listings

std::vector<std::string> abroadChannels(const std::vector<Channel>& entries, const std::set<std::string>& lineup,
                                        const std::vector<std::string>& browseIDs,
                                        const std::unordered_map<std::string, catalog::EntryInfo>& entryInfo,
                                        const std::set<std::string>& hiddenGroups, const std::string& otherGroup) {
  using catalog::LineupCategory;
  std::unordered_set<std::string> browse(browseIDs.begin(), browseIDs.end());
  std::vector<std::string> out;
  for (const Channel& entry : entries) {
    if (lineup.count(entry.id) || browse.count(entry.id) || hiddenGroups.count(entry.group.value_or(otherGroup))) {
      continue;
    }
    auto found = entryInfo.find(entry.id);
    if (found == entryInfo.end()) continue;
    const catalog::EntryInfo& info = found->second;
    // Another country's (a sports channel of no country is read already).
    if (info.kind != catalog::ChannelKind::channel || !info.market || !info.closed.empty() ||
        (info.category != LineupCategory::international && info.category != LineupCategory::sports)) {
      continue;
    }
    if (brands::isSportsBrand(info.name)) out.push_back(entry.id);
  }
  return out;
}

std::vector<ListingChannel> listingChannels(const std::set<std::string>& lineup,
                                            const std::vector<std::string>& browseIDs,
                                            const std::unordered_map<std::string, catalog::EntryInfo>& entryInfo,
                                            const StationMarkets& markets, const std::vector<std::string>& abroad) {
  using catalog::LineupCategory;
  // Every such channel you have, not only My Lineup's: any station whose
  // guide shows the game is a way to watch it. Sports channels of unknown
  // country too (they're not in the browse list).
  std::set<std::string> candidates = lineup;
  candidates.insert(browseIDs.begin(), browseIDs.end());
  // Build 143: and the US channels in Spanish, picked or not (ranked after
  // English ones by their language).
  for (const auto& [id, info] : entryInfo) {
    if (info.kind != catalog::ChannelKind::channel) continue;
    if ((!info.market && info.category == LineupCategory::sports) ||
        (info.market == std::string("US-ES") && info.category == LineupCategory::spanish && info.closed.empty())) {
      candidates.insert(id);
    }
  }
  // Build 143: entertainment networks that carry games (TNT, TBS, truTV,
  // USA Network): their listings confirm a game too.
  static const std::set<std::string> kCarrySports = {"TNT", "TBS", "truTV", "USA Network"};
  std::vector<ListingChannel> out;
  for (const std::string& id : candidates) {
    auto found = entryInfo.find(id);
    if (found == entryInfo.end()) continue;
    const catalog::EntryInfo& info = found->second;
    // Build 132: another country's channel in the browse list is
    // "international"; a sports brand among them is read too.
    bool brand = info.category == LineupCategory::international && info.kind == catalog::ChannelKind::channel &&
                 brands::isSportsBrand(info.name);
    bool carriesSports = info.core && kCarrySports.count(*info.core) > 0;
    if (info.category != LineupCategory::sports && info.category != LineupCategory::local &&
        info.category != LineupCategory::spanish && !brand && !carriesSports) {
      continue;
    }
    ListingChannel channel;
    channel.id = id;
    channel.inLineup = lineup.count(id) > 0;
    channel.lang = languages::ofChannel(info);
    channel.rank = stationRank(info, markets, channel.inLineup);
    channel.sportsChannel =
        (info.category == LineupCategory::sports || brand) && info.kind == catalog::ChannelKind::channel;
    channel.cricketChannel = channel.sportsChannel && cricketChannelName(info.name);
    out.push_back(std::move(channel));
  }
  // Other countries' sports channels: the games their listings name, ranked
  // last (never tennis coverage: a "likely" source).
  std::set<std::string> taken;
  for (const std::string& id : abroad) {
    auto found = entryInfo.find(id);
    if (found == entryInfo.end() || candidates.count(id) || !taken.insert(id).second) continue;
    const catalog::EntryInfo& info = found->second;
    ListingChannel channel;
    channel.id = id;
    channel.lang = languages::ofChannel(info);
    channel.rank = station_rank::abroad;
    channel.cricketChannel = cricketChannelName(info.name);
    channel.abroad = true;
    out.push_back(std::move(channel));
  }
  return out;
}

bool cricketChannelName(std::string_view name) {
  std::string words = matcher::normalized(name);
  for (const char* brand : {" willow ", " cricket ", " star sports ", " ptv sports ", " ten sports ", " a sports ",
                            " supersport "}) {
    if (words.find(brand) != std::string::npos) return true;
  }
  return false;
}

std::optional<matcher::GuideListing> listingFor(const ListingChannel& channel, const std::string& rawTitle,
                                                const std::optional<std::string>& subtitle,
                                                const guide::Text& overview, bool sports,
                                                Seconds start, Seconds end) {
  std::string title = subtitle ? rawTitle + " " + *subtitle : rawTitle;
  matcher::GuideListing listing;
  listing.entryID = channel.id;
  listing.inLineup = channel.inLineup;
  listing.lang = channel.lang;
  listing.abroad = channel.abroad;
  listing.title = title;
  listing.start = start;
  listing.end = end;
  listing.rank = channel.rank;
  // Build 143: a tour's or series' own programme ("PGA Tour Golf", "Live
  // F1: Sao Paulo GP Qualifying", "Live UFC Fight Night"), on any channel.
  listing.series = matcher::seriesOf(rawTitle, subtitle);
  if (matcher::looksLikeGame(title)) return listing;
  if (channel.sportsChannel && matcher::namesTennisEvent(title)) {
    // "Center Court Live" / "Vienna-ATP & Basel-ATP Semifinals": matched
    // with those tournaments' matches.
    listing.tennisCoverage = true;
    return listing;
  }
  if (channel.cricketChannel && matcher::isLiveCricket(title)) {
    // "Live: T20 Cricket", described "3rd T20I.": the match in play then.
    listing.cricket = true;
    if (overview) listing.detail = text::prefix(overview.view(), 120);
    return listing;
  }
  if (overview && text::count(overview.view()) > 12 && (sports || matcher::isGenericSportsTitle(rawTitle)) &&
      matcher::isGenericSportsTitle(title)) {
    // "NBA Basketball": the description names the venue ("From Chase
    // Center in San Francisco"), or sometimes the teams.
    if (auto place = matcher::venueOf(overview.view())) {
      listing.venue = place->first;
      listing.city = place->second;
    } else {
      listing.detail = text::prefix(overview.view(), 120);
    }
    return listing;
  }
  if (!listing.series.empty()) return listing;
  // Build 143: "Napoli - Lazio", "Omonia FC x SL Benfica" on a sports
  // channel or in a sports slot: a game when its two sides are a scheduled
  // game's teams (the matcher's title check).
  if ((channel.sportsChannel || channel.abroad || sports) && matcher::pairedTitle(title)) return listing;
  return std::nullopt;
}

ExtractedListings extractListings(const std::vector<ListingInput>& channels, Seconds now) {
  ExtractedListings out;
  const Seconds low = now - kListingsBefore, high = now + kListingsAhead;
  const int64_t highMinute = high / 60;
  for (const ListingInput& input : channels) {
    if (!input.programs && !input.outside && !input.provider) continue;
    Seconds guideEnd = low;
    // Build 143: whether this channel's guide marks its live airings (worked
    // out once, for its first tour or series programme): then one it doesn't
    // mark is shown again.
    std::optional<bool> marksLive;
    auto marking = [&]() {
      if (marksLive) return *marksLive;
      marksLive = false;
      if (input.programs) {
        for (const Program& program : *input.programs) {
          if (program.isLive) {
            marksLive = true;
            break;
          }
        }
      }
      for (const auto* raw : {input.outside, input.provider}) {
        if (!raw || *marksLive) continue;
        for (const guide::RawProgram& item : *raw) {
          if (item.f & 16) {
            marksLive = true;
            break;
          }
        }
      }
      return *marksLive;
    };
    auto noteReplay = [&](matcher::GuideListing& listing, bool live) {
      if (!listing.series.empty() && !live && marking()) listing.replay = true;
    };
    if (input.programs) {
      for (const Program& program : *input.programs) {
        if (program.end <= low || program.start >= high) continue;
        guideEnd = std::max(guideEnd, program.end);
        if (auto listing = listingFor(input.channel, program.title, program.episodeTitle, program.overview,
                                      (program.genres & kGenreSports) != 0, program.start, program.end)) {
          noteReplay(*listing, program.isLive);
          out.listings.push_back(std::move(*listing));
        }
      }
    }
    // Then the saved listings past what the guide holds.
    if (!input.outside && !input.provider) continue;
    const int64_t fromMinute = guideEnd / 60;
    std::unordered_set<int> seen;
    for (const auto* raw : {input.outside, input.provider}) {
      if (!raw) continue;
      for (const guide::RawProgram& item : *raw) {
        if (item.s < fromMinute || item.s >= highMinute || !seen.insert(item.s).second) continue;
        Seconds start = Seconds{item.s} * 60;
        Seconds end = Seconds{item.s + item.m} * 60;
        if (auto listing = listingFor(input.channel, item.t, item.st, item.d, (item.f & 2) != 0, start, end)) {
          noteReplay(*listing, (item.f & 16) != 0);
          out.listings.push_back(std::move(*listing));
          out.ahead += 1;
        }
      }
    }
  }
  std::sort(out.listings.begin(), out.listings.end(), [](const auto& a, const auto& b) {
    if (a.start != b.start) return a.start < b.start;
    if (a.entryID != b.entryID) return a.entryID < b.entryID;
    return a.title < b.title;
  });
  return out;
}

bool listingsNeedRead(std::optional<Seconds> extractedAt, int programsRevision, int listingsProgramsRevision,
                      Seconds now) {
  bool stale = !extractedAt || now - *extractedAt > kListingsStale;
  bool guideMoved = programsRevision != listingsProgramsRevision && (!extractedAt || now - *extractedAt > kListingsMoved);
  return stale || guideMoved;
}

std::optional<std::string> listingsDue(Seconds changedAt, Seconds pendingSince, std::optional<Seconds> extractedAt,
                                       Seconds now) {
  Seconds quiet = now - changedAt;
  Seconds waiting = now - pendingSince;
  bool longAgo = !extractedAt || now - *extractedAt >= 600;
  if (quiet >= 60) return std::string("quiet");
  if (waiting >= 600 && longAgo) return std::string("deadline");
  return std::nullopt;
}

// MARK: Rebuild

bool highSchoolFamily(std::string_view family) {
  return rx::test("\\bnfhs\\b|high ?school", std::string(family), true);
}

std::vector<matcher::ProviderEvent> providerEvents(
    const std::vector<catalog::LiveEvent>& events,
    const std::unordered_map<std::string, catalog::ChannelTraits>& traits, Seconds now) {
  std::vector<matcher::ProviderEvent> out;
  out.reserve(events.size());
  for (const catalog::LiveEvent& event : events) {
    matcher::ProviderEvent p;
    p.id = event.id;
    p.title = event.title;
    // Another country's feed: its round and stage words in English (US
    // Spanish stays as sent: it's picked for its language).
    static const std::set<std::string> kAsSent = {"US", "US-ES", "UK", "CA", "AU", "IE", "NZ"};
    if (event.market && !kAsSent.count(*event.market)) p.title = titles::inEnglish(p.title);
    p.league = event.league;
    p.start = event.start;
    p.feeds = event.feeds;
    p.family = event.family;
    p.lang = languages::ofFeed(event.title, event.family, event.market);
    // Each feed by its own channel's market where that says otherwise: a
    // provider group of several countries' channels ("grp:...") has its
    // first channel's market for all its events ("AL: ..." beside "LV: ...").
    const bool group = str::startsWith(event.family, "grp:");
    for (size_t i = 0; i < event.feeds.size(); ++i) {
      auto t = traits.find(event.feeds[i]);
      if (t == traits.end()) continue;
      std::optional<std::string> own = t->second.market;
      if (!own && !group) continue;
      if (own == event.market) continue;
      if (p.feedLangs.empty()) p.feedLangs.assign(event.feeds.size(), p.lang);
      p.feedLangs[i] = languages::ofFeed(event.title, event.family, own);
    }
    // Feeds that are boards of one darts event.
    std::set<std::string> boards;
    for (const std::string& feed : event.feeds) {
      auto t = traits.find(feed);
      if (t == traits.end() || !t->second.eventText) continue;
      if (auto b = rx::first("\\bboard\\s+(\\d{1,2})\\b", *t->second.eventText, true)) boards.insert(std::string(b->group(1)));
    }
    if (boards.size() >= 2) p.boards = static_cast<int>(boards.size());
    // High-school feeds: by the provider's group, else the title's words.
    p.highSchool = highSchoolFamily(event.family) ||
                   rx::test("\\bhigh school\\b|\\bvarsity\\b|\\b[a-z]{1,5}(hsaa|hsa|hsl)\\b", event.title, true);
    if (p.highSchool) {
      // "2026 KSHSAA 4A Girls Volleyball Championship 3 vs 6": the year goes,
      // and the numbers are seeds, not sides.
      p.title = rx::replace(p.title, "^\\s*20\\d\\d\\s+", "");
      if (auto m = rx::first("^(.*\\b(?:championships?|tournament|playoffs?|finals?|semifinals?|quarterfinals?))\\s+"
                             "(\\d{1,2})\\s+vs\\.?\\s+(\\d{1,2})$",
                             p.title, true)) {
        p.title = std::string(m->group(1)) + " _ Seeds #" + std::string(m->group(2)) + " and #" + std::string(m->group(3));
      }
    }
    if (event.startGuessed) {
      p.guessed = *event.startGuessed;
    } else if (event.start && !event.feeds.empty()) {
      // A list saved by an older build doesn't say whether the day was a
      // guess: read that from the slot's raw text.
      auto t = traits.find(event.feeds.front());
      if (t != traits.end() && t->second.eventText) {
        p.guessed = catalog::parseEventDetailed(*t->second.eventText, now, event.market).guessed;
      }
    }
    out.push_back(std::move(p));
  }
  return out;
}

// Feeds dated in the last two weeks that are over, which the channel list
// leaves out of its events: only the slots no event uses are read again.
std::vector<matcher::ProviderEvent> pastEvents(
    const std::vector<catalog::LiveEvent>& events,
    const std::unordered_map<std::string, catalog::ChannelTraits>& traits, Seconds now) {
  std::unordered_set<std::string> used;
  for (const catalog::LiveEvent& event : events)
    for (const std::string& feed : event.feeds) used.insert(feed);
  std::vector<matcher::ProviderEvent> out;
  for (const auto& [id, t] : traits) {
    if (!t.familyKey || !t.eventText || used.count(id)) continue;
    const std::string& text = *t.eventText;
    if (std::none_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) continue;
    catalog::ParsedEvent parsed = catalog::parseEventDetailed(text, now, catalog::slotMarket(t));
    if (!parsed.start || parsed.guessed || parsed.title.empty()) continue;
    if (*parsed.start > now - 8 * 3600 || *parsed.start < now - 14 * 86400) continue;
    matcher::ProviderEvent p;
    p.id = "past|" + id;
    p.title = parsed.title;
    p.start = parsed.start;
    out.push_back(std::move(p));
  }
  return out;
}

std::vector<matcher::RosterLeague> rosterLeagues(const std::vector<League>& active,
                                                 const std::map<std::string, std::vector<SportsTeam>>& rosters) {
  std::vector<matcher::RosterLeague> out;
  for (const League& league : active) {
    auto teams = rosters.find(league.shortName);
    if (teams == rosters.end() || teams->second.empty()) continue;
    out.push_back({league.shortName, league.group, league.college(), teams->second});
  }
  return out;
}

std::string channelIndexKey(int entriesRevision, const std::set<std::string>& lineup, const StationMarkets& markets,
                            const std::set<std::string>& hidden, const std::vector<matcher::RosterLeague>& rosters) {
  std::string hashed;
  for (const auto* set : {&lineup, &markets.home, &markets.others}) {
    for (const std::string& s : *set) hashed += s + "\n";
    hashed += "\x1F";
  }
  std::vector<std::string> hiddenList(hidden.begin(), hidden.end());
  std::vector<std::string> rosterParts;
  for (const auto& r : rosters) rosterParts.push_back(r.shortName + ":" + std::to_string(r.teams.size()));
  return std::to_string(entriesRevision) + "|" + str::hex64(str::fnv1a64(hashed)) + "|" + str::join(hiddenList, ",") +
         "|" + str::join(rosterParts, ",");
}

std::string fingerprint(const Fingerprint& f, Seconds now) {
  std::vector<std::string> hidden(f.hidden.begin(), f.hidden.end());
  return std::to_string(f.eventsRevision) + "|" + std::to_string(f.scheduleRevision) + "|" +
         std::to_string(f.entriesRevision) + "|" + std::to_string(f.listingsRevision) + "|" +
         std::to_string(now / 1800) + "|" + str::join(hidden, ",") + "|" +
         str::join(std::vector<std::string>(f.languages.begin(), f.languages.end()), ",") + "|" +
         (f.enabled ? "true" : "false") + "|" + std::to_string(f.schools);
}

uint64_t listingsDigest(const std::vector<matcher::GuideListing>& listings, Seconds now) {
  const Seconds from = now / 1800 * 1800, to = from + 1800;
  uint64_t h = 0xcbf29ce484222325ULL;
  auto mix = [&h](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
  auto text = [&mix](const std::optional<std::string>& s) { mix(s ? str::fnv1a64(*s) + 1 : 0); };
  for (const matcher::GuideListing& l : listings) {
    if (l.end <= to - kListingsBefore || l.start >= from + kListingsAhead) continue;
    mix(str::fnv1a64(l.entryID));
    mix(str::fnv1a64(l.title));
    mix(str::fnv1a64(l.lang));
    mix(static_cast<uint64_t>(l.start));
    mix(static_cast<uint64_t>(l.end));
    text(l.detail);
    text(l.venue);
    text(l.city);
    mix(l.rank ? static_cast<uint64_t>(*l.rank) + 1 : 0);
    mix((l.inLineup ? 1u : 0u) | (l.tennisCoverage ? 2u : 0u) | (l.cricket ? 4u : 0u) | (l.abroad ? 8u : 0u) |
        (l.replay ? 16u : 0u));
    mix(str::fnv1a64(l.series));
  }
  return h;
}

std::optional<RebuildResult> rebuild(const RebuildInput& in) {
  matcher::BuildInput b;
  const auto& events = in.sharedEvents ? *in.sharedEvents : in.events;
  const auto& traits = in.sharedTraits ? *in.sharedTraits : in.traits;
  b.events = providerEvents(events, traits, in.now);
  b.pastEvents = pastEvents(events, traits, in.now);
  network_map::NetworkSources network = network_map::networkSources(in.games, in.channelIndex);
  b.channelSources = network.sources;
  if (!in.f1Sources.empty()) {
    for (const SportsGame& game : in.games) {
      if (game.leagueShort != "F1" || game.completed) continue;
      std::vector<EventSource>& list = b.channelSources[game.id];
      for (const EventSource& source : in.f1Sources) {
        bool known = std::any_of(list.begin(), list.end(),
                                 [&](const EventSource& s) { return s.channelID == source.channelID; });
        if (!known) list.push_back(source);
      }
    }
  }
  b.games = in.games;
  b.listings = in.listings;
  b.languages = in.languages;
  b.now = in.now;
  b.zone = in.zone;
  for (const SportsEvent& event : in.previous) {
    if (event.game) b.previousIDs[event.game->id] = event.id;
  }
  b.schools = in.schools;
  b.rosters = in.rosters;
  b.shouldStop = in.shouldStop;
  b.isCameraName = in.isCameraName;
  b.seen = in.slotsSeen;
  auto built = matcher::build(b);
  if (!built) return std::nullopt;
  RebuildResult out;
  out.slotsSeen = std::move(built->seen);
  out.events = markEndedByFeeds(built->events, in.previous, !in.events.empty(), in.now, in.channelExists,
                                &out.endedByFeeds);
  out.stats = std::move(built->stats);
  out.sources = std::move(network.stats);
  for (const SportsEvent& event : out.events) {
    if (!event.sources.empty() && event.game) out.watchableLeagues.insert(event.leagueShort);
  }
  return out;
}

std::vector<SportsEvent> markEndedByFeeds(const std::vector<SportsEvent>& built,
                                          const std::vector<SportsEvent>& previous, bool haveRawEvents, Seconds now,
                                          const std::function<bool(const std::string&)>& channelExists, int* ended) {
  if (ended) *ended = 0;
  if (!haveRawEvents) return built;
  std::map<std::string, const SportsEvent*> old;
  for (const SportsEvent& event : previous) {
    if (event.game) old[event.id] = &event;
  }
  if (old.empty()) return built;
  std::vector<SportsEvent> result;
  result.reserve(built.size());
  for (const SportsEvent& event : built) {
    auto before = old.find(event.id);
    if (!event.game || before == old.end()) {
      result.push_back(event);
      continue;
    }
    SportsEvent copy = event;
    if (event.hasFeeds()) {
      copy.feedsEndedAt.reset();
      result.push_back(std::move(copy));
      continue;
    }
    if (before->second->feedsEndedAt) {
      copy.feedsEndedAt = before->second->feedsEndedAt;
      result.push_back(std::move(copy));
      continue;
    }
    std::vector<EventSource> beforeFeeds = before->second->feeds();
    bool onChannels = std::any_of(event.sources.begin(), event.sources.end(), [](const EventSource& s) {
      return s.kind != EventSource::Kind::provider && s.kind != EventSource::Kind::team;
    });
    bool allExist = std::all_of(beforeFeeds.begin(), beforeFeeds.end(), [&](const EventSource& s) {
      return channelExists && channelExists(s.channelID);
    });
    if (!event.start || now - *event.start <= 45 * 60 || beforeFeeds.empty() || onChannels || !allExist) {
      result.push_back(event);
      continue;
    }
    copy.feedsEndedAt = now;
    if (ended) *ended += 1;
    result.push_back(std::move(copy));
  }
  return result;
}

std::string coverageSummary(const std::vector<SportsEvent>& events, const network_map::SourceStats& sources,
                            Seconds now) {
  int games = 0, lineup = 0, local = 0, elsewhere = 0, team = 0, feedsOnly = 0, none = 0, folded = 0;
  for (const SportsEvent& event : events) {
    if (!event.game || event.game->completed || event.game->start <= now - 4 * 3600 ||
        event.game->start >= now + 48 * 3600) {
      continue;
    }
    games += 1;
    std::vector<EventSource> channels = event.channels();
    auto any = [&](const std::function<bool(const EventSource&)>& f) {
      return std::any_of(channels.begin(), channels.end(), f);
    };
    if (any([](const EventSource& s) { return s.kind == EventSource::Kind::lineup; })) lineup += 1;
    if (any([](const EventSource& s) {
          return s.kind == EventSource::Kind::guide && station_rank::isLocal(s.rank.value_or(station_rank::elsewhere));
        })) {
      local += 1;
    }
    if (any([](const EventSource& s) {
          return s.kind == EventSource::Kind::guide && s.rank.value_or(station_rank::elsewhere) == station_rank::elsewhere;
        })) {
      elsewhere += 1;
    }
    if (any([](const EventSource& s) { return s.kind == EventSource::Kind::team; })) team += 1;
    if (channels.empty()) {
      if (event.hasFeeds()) {
        feedsOnly += 1;
      } else {
        none += 1;
      }
    }
    folded += event.moreStations.value_or(0);
  }
  auto n = [](int v) { return std::to_string(v); };
  return n(games) + " games in 48 h: " + n(lineup) + " on My Lineup, " + n(local) + " on your local stations, " +
         n(elsewhere) + " on other stations, " + n(team) + " on team channels, " + n(feedsOnly) +
         " on provider feeds only, " + n(none) + " nowhere; " + n(folded) + " more stations folded; links: " +
         sources.summary();
}

bool RebuildScheduler::isUserReason(std::string_view reason) {
  return reason == "setting" || reason == "home city" || reason == "my sports" || reason == "hidden";
}

std::optional<double> RebuildScheduler::request(const std::string& reason, double delay, double now) {
  if (running_) {
    again_ = reason;
    return std::nullopt;
  }
  if (!first_) first_ = now;
  double wait = delay;
  if (now - *first_ < kLaunchWindow && last_ && !isUserReason(reason)) {
    double gap = kLaunchGap - (now - *last_);
    if (gap > wait) wait = gap;
  }
  double due = now + wait;
  if (due_ && *due_ <= due) return std::nullopt;
  due_ = due;
  dueReason_ = reason;
  return due;
}

std::string RebuildScheduler::begin() {
  due_.reset();
  running_ = true;
  return dueReason_;
}

std::optional<std::string> RebuildScheduler::end(double now) {
  running_ = false;
  last_ = now;
  std::optional<std::string> again = std::move(again_);
  again_.reset();
  return again;
}

// MARK: Sources of an event

std::map<std::string, std::vector<size_t>> backupIndex(const std::vector<SportsEvent>& events) {
  std::map<std::string, std::vector<size_t>> index;
  for (size_t position = 0; position < events.size(); ++position) {
    for (const EventSource& source : events[position].sources) {
      if (source.kind != EventSource::Kind::provider) index[source.channelID].push_back(position);
    }
  }
  return index;
}

std::vector<std::string> eventBackupFeeds(const std::string& id, const std::vector<SportsEvent>& events,
                                          const std::map<std::string, std::vector<size_t>>& index, Seconds now) {
  auto positions = index.find(id);
  if (positions == index.end()) return {};
  for (size_t position : positions->second) {
    if (position >= events.size()) continue;
    const SportsEvent& event = events[position];
    if (!event.start || *event.start >= now + 15 * 60) continue;
    SportsEvent::State state = event.state(now);
    if (state != SportsEvent::State::live && state != SportsEvent::State::mayHaveEnded &&
        state != SportsEvent::State::upcoming) {
      continue;
    }
    std::vector<std::string> feeds;
    for (const EventSource& s : event.feeds()) feeds.push_back(s.channelID);
    return feeds;
  }
  return {};
}

std::vector<std::string> orderedByReliability(const std::vector<std::string>& ids, const Reliability& reliability) {
  std::vector<std::pair<size_t, double>> items;
  for (size_t i = 0; i < ids.size(); ++i) items.emplace_back(i, reliability ? reliability(ids[i]) : 0.5);
  // Keep the catalog's order unless a source is clearly worse. The rule
  // isn't a strict ordering, so it runs as an insertion sort (what Swift's
  // sort does for lists this short) rather than std::sort.
  auto before = [](const std::pair<size_t, double>& a, const std::pair<size_t, double>& b) {
    if (std::fabs(a.second - b.second) > 0.25) return a.second > b.second;
    return a.first < b.first;
  };
  for (size_t i = 1; i < items.size(); ++i) {
    for (size_t j = i; j > 0 && before(items[j], items[j - 1]); --j) std::swap(items[j], items[j - 1]);
  }
  std::vector<std::string> out;
  for (const auto& item : items) out.push_back(ids[item.first]);
  return out;
}

std::vector<std::string> bestFeeds(const std::vector<std::string>& ids, const std::function<int(const std::string&)>& quality,
                                   const Reliability& reliability) {
  struct Item {
    size_t at;
    long tenths;
    int quality;
  };
  std::vector<Item> items;
  items.reserve(ids.size());
  for (size_t i = 0; i < ids.size(); ++i) {
    double r = reliability ? reliability(ids[i]) : 0.5;
    items.push_back({i, std::lround(std::clamp(r, 0.0, 1.0) * 10.0), quality ? quality(ids[i]) : 0});
  }
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    if (a.tenths != b.tenths) return a.tenths > b.tenths;
    return a.quality > b.quality;
  });
  std::vector<std::string> out;
  out.reserve(items.size());
  for (const Item& item : items) out.push_back(ids[item.at]);
  return out;
}

std::vector<std::string> baseVersions(const std::string& id, const catalog::EntryInfo* info,
                                      const std::optional<std::string>& pinned, const Reliability& reliability,
                                      const std::function<bool(const std::string&)>& exists) {
  if (!info) return {id};
  std::vector<std::string> members;
  for (const std::string& m : info->members) {
    if (!exists || exists(m)) members.push_back(m);
  }
  std::vector<std::string> list = orderedByReliability(members, reliability);
  if (pinned) {
    auto at = std::find(list.begin(), list.end(), *pinned);
    if (at != list.end() && at != list.begin()) std::rotate(list.begin(), at, at + 1);
  }
  return list;
}

void directVersionsFirst(std::vector<std::string>& list, size_t keep,
                         const std::function<bool(const std::string&)>& serverOpened) {
  if (!serverOpened || keep >= list.size()) return;
  std::stable_partition(list.begin() + static_cast<std::ptrdiff_t>(keep), list.end(),
                        [&](const std::string& id) { return !serverOpened(id); });
}

void leadServerVersions(std::vector<std::string>& list, const std::string& last,
                        const std::function<bool(const std::string&)>& serverOpened) {
  auto at = std::find(list.begin(), list.end(), last);
  auto firstServer = std::find_if(list.begin(), list.end(), serverOpened);
  if (at != list.end() && firstServer < at) std::rotate(firstServer, at, at + 1);
}

std::vector<std::string> eventCandidates(const SportsEvent& event, const std::optional<std::string>& preferring,
                                         const Resolver& resolver) {
  std::vector<std::string> list;
  auto add = [&](const std::vector<std::string>& ids) {
    for (const std::string& id : ids) {
      if (std::find(list.begin(), list.end(), id) == list.end()) list.push_back(id);
    }
  };
  auto resolve = [&](const EventSource& s) -> std::optional<ChannelRef> {
    return resolver.sourceChannel ? resolver.sourceChannel(s) : std::nullopt;
  };
  auto versions = [&](const std::string& id) {
    return resolver.baseVersions ? resolver.baseVersions(id) : std::vector<std::string>{id};
  };
  if (preferring) {
    auto source = std::find_if(event.sources.begin(), event.sources.end(),
                               [&](const EventSource& s) { return s.channelID == *preferring; });
    if (source != event.sources.end()) {
      if (auto channel = resolve(*source)) {
        add(source->kind == EventSource::Kind::provider ? std::vector<std::string>{channel->id} : versions(channel->id));
      }
    }
  }
  std::vector<EventSource> channels = event.channels();
  // Yours (My Lineup's, in your languages or not known) first: their
  // channels, then their feeds; then another language's.
  for (bool welcome : {true, false}) {
    auto fits = [&](const EventSource& s) { return languages::welcome(s, resolver.languages) == welcome; };
    for (const EventSource& source : channels) {
      if (source.kind == EventSource::Kind::team || !fits(source)) continue;
      if (auto channel = resolve(source)) add(versions(channel->id));
    }
    std::vector<std::string> feeds;
    for (const EventSource& source : event.feeds()) {
      if (!fits(source)) continue;
      if (auto channel = resolve(source)) feeds.push_back(channel->id);
    }
    add(resolver.bestFeeds ? resolver.bestFeeds(feeds) : feeds);
  }
  // A team's own channel last: likely, not certain, to carry the game.
  for (bool welcome : {true, false}) {
    for (const EventSource& source : channels) {
      if (source.kind != EventSource::Kind::team || languages::welcome(source, resolver.languages) != welcome) continue;
      if (auto channel = resolve(source)) add(versions(channel->id));
    }
  }
  return list;
}

WatchPlan watchPlan(const SportsEvent& event, const std::optional<std::string>& preferring,
                    const std::optional<std::string>& remembered, const Resolver& resolver) {
  WatchPlan plan;
  plan.candidates = eventCandidates(event, preferring, resolver);
  if (plan.candidates.empty()) {
    plan.empty = true;
    return plan;
  }
  if (!preferring && remembered) {
    auto at = std::find(plan.candidates.begin(), plan.candidates.end(), *remembered);
    if (at != plan.candidates.end()) {
      std::rotate(plan.candidates.begin(), at, at + 1);
      plan.reason = "the source you were on";
    }
  }
  if (preferring) {
    auto source = std::find_if(event.sources.begin(), event.sources.end(),
                               [&](const EventSource& s) { return s.channelID == *preferring; });
    if (source != event.sources.end() && source->kind != EventSource::Kind::provider && resolver.isEntry &&
        resolver.isEntry(source->channelID)) {
      plan.entry = source->channelID;
    }
  }
  return plan;
}

std::optional<LiveReminder> reminderTarget(const SportsEvent& event, const Resolver& resolver) {
  if (!event.start || !resolver.sourceChannel) return std::nullopt;
  std::vector<EventSource> channels = event.channels();
  std::vector<EventSource> feeds = event.feeds();
  // As Watch: a channel in your languages, else a feed in them, else any.
  auto welcome = [&](const EventSource& s) { return languages::welcome(s, resolver.languages); };
  auto mine = std::find_if(channels.begin(), channels.end(), welcome);
  auto mineFeed = std::find_if(feeds.begin(), feeds.end(), welcome);
  const EventSource* source = mine != channels.end()      ? &*mine
                              : mineFeed != feeds.end()   ? &*mineFeed
                              : !channels.empty()         ? &channels.front()
                              : !feeds.empty()            ? &feeds.front()
                                                          : nullptr;
  if (!source) return std::nullopt;
  auto channel = resolver.sourceChannel(*source);
  if (!channel) return std::nullopt;
  return LiveReminder{channel->id, channel->name, event.title, *event.start, *event.start + event.expectedLength()};
}

// MARK: Saved result

std::string toJson(const SavedResult& saved) {
  json::Value v = json::Value::object();
  v.set("version", saved.version);
  v.set("savedAt", static_cast<int64_t>(saved.savedAt));
  json::Value events = json::Value::array();
  for (const SportsEvent& e : saved.events) events.push(sports::toJson(e));
  v.set("events", events);
  json::Value keys = json::Value::object();
  for (const auto& [feed, key] : saved.feedKeys) keys.set(feed, key);
  v.set("feedKeys", keys);
  if (saved.listings) {
    json::Value listings = json::Value::array();
    for (const matcher::GuideListing& l : *saved.listings) listings.push(matcher::toJson(l));
    v.set("listings", listings);
  }
  if (saved.slotsSeen) v.set("slotsSeen", matcher::toJson(*saved.slotsSeen));
  return json::write(v);
}

std::optional<SavedResult> savedResultFromJson(std::string_view textBody) {
  auto doc = json::parse(textBody);
  if (!doc || !doc->isObject() || !(*doc)["version"].isNumber() || !(*doc)["savedAt"].isNumber() ||
      !(*doc)["events"].isArray()) {
    return std::nullopt;
  }
  const json::Value& v = *doc;
  SavedResult saved;
  saved.version = static_cast<int>(v["version"].asInt());
  saved.savedAt = v["savedAt"].asInt();
  for (const json::Value& item : v["events"].items()) {
    auto e = eventFromJson(item);
    if (!e) return std::nullopt;
    saved.events.push_back(std::move(*e));
  }
  for (const auto& [feed, key] : v["feedKeys"].fields()) {
    if (key.isString()) saved.feedKeys[feed] = key.asString();
  }
  if (v["listings"].isArray()) {
    std::vector<matcher::GuideListing> listings;
    for (const json::Value& item : v["listings"].items()) {
      if (auto l = matcher::listingFromJson(item)) listings.push_back(std::move(*l));
    }
    saved.listings = std::move(listings);
  }
  if (v["slotsSeen"].isObject())
    saved.slotsSeen = std::make_shared<const matcher::SlotsSeen>(matcher::slotsSeenFromJson(v["slotsSeen"]));
  return saved;
}

std::optional<SavedResult> resultToSave(const std::vector<SportsEvent>& events,
                                        const std::vector<catalog::LiveEvent>& rawEvents,
                                        const std::vector<matcher::GuideListing>& listings, bool listingsReady,
                                        Seconds now, std::string& lastFingerprint,
                                        std::shared_ptr<const matcher::SlotsSeen> slotsSeen) {
  std::vector<SportsEvent> games;
  for (const SportsEvent& e : events) {
    if (e.game && !e.sources.empty()) games.push_back(e);
  }
  if (games.empty() || !(listingsReady || !listings.empty())) return std::nullopt;
  std::vector<std::string> parts;
  for (const SportsEvent& e : games) {
    std::vector<std::string> ids;
    for (const EventSource& s : e.sources) ids.push_back(s.channelID);
    parts.push_back(e.id + ":" + str::join(ids, "+"));
  }
  std::string print = str::join(parts, ",");
  // A slot seen for the first time, or on a new day, is saved too.
  if (slotsSeen) {
    uint64_t seenPrint = slotsSeen->size();
    for (const auto& [key, seen] : *slotsSeen)
      seenPrint += key ^ (static_cast<uint64_t>(seen.first) * 31 + static_cast<uint64_t>(seen.last));
    print += "|" + str::hex64(seenPrint);
  }
  if (print == lastFingerprint) return std::nullopt;
  lastFingerprint = print;
  std::unordered_map<std::string, std::string> titleOfFeed;
  for (const catalog::LiveEvent& event : rawEvents) {
    for (const std::string& feed : event.feeds) titleOfFeed[feed] = event.title;
  }
  SavedResult saved;
  saved.savedAt = now;
  for (const SportsEvent& e : games) {
    for (const EventSource& source : e.feeds()) {
      auto title = titleOfFeed.find(source.channelID);
      if (title != titleOfFeed.end()) saved.feedKeys[source.channelID] = matcher::matchupKey(title->second);
    }
  }
  saved.events = std::move(games);
  saved.listings = listings;
  saved.slotsSeen = std::move(slotsSeen);
  return saved;
}

std::optional<Restored> restore(const SavedResult& saved, const std::vector<catalog::LiveEvent>& rawEvents,
                                Seconds now) {
  if (saved.version != 1) return std::nullopt;
  std::unordered_map<std::string, std::string> current;
  for (const catalog::LiveEvent& event : rawEvents) {
    std::string key = matcher::matchupKey(event.title);
    for (const std::string& feed : event.feeds) current[feed] = key;
  }
  Restored out;
  for (const SportsEvent& event : saved.events) {
    if (event.state(now) == SportsEvent::State::finished) continue;
    SportsEvent copy = event;
    copy.sources.clear();
    for (const EventSource& source : event.sources) {
      if (source.kind != EventSource::Kind::provider) {
        copy.sources.push_back(source);
        continue;
      }
      auto now_ = current.find(source.channelID);
      auto then = saved.feedKeys.find(source.channelID);
      bool keep = !rawEvents.empty() && now_ != current.end() && then != saved.feedKeys.end() &&
                  now_->second == then->second;
      if (keep) {
        copy.sources.push_back(source);
      } else {
        out.dropped += 1;
      }
    }
    out.events.push_back(std::move(copy));
  }
  if (saved.listings) {
    for (const matcher::GuideListing& l : *saved.listings) {
      if (l.end > now && l.start < now + 16 * 3600) out.listings.push_back(l);
    }
  }
  out.ageMinutes = static_cast<int>((now - saved.savedAt) / 60);
  return out;
}

std::optional<std::vector<SportsEvent>> fillIn(const std::vector<SportsEvent>& restored,
                                               const std::vector<SportsEvent>& current, bool channelsLoaded,
                                               const std::function<bool(const EventSource&)>& resolvable) {
  if (std::any_of(current.begin(), current.end(), [](const SportsEvent& e) { return e.game.has_value(); })) {
    return std::nullopt;
  }
  std::vector<SportsEvent> out;
  std::set<std::string> savedIDs;
  for (const SportsEvent& event : restored) {
    SportsEvent copy = event;
    if (channelsLoaded && resolvable) {
      copy.sources.clear();
      for (const EventSource& s : event.sources) {
        if (resolvable(s)) copy.sources.push_back(s);
      }
    }
    savedIDs.insert(copy.id);
    out.push_back(std::move(copy));
  }
  for (const SportsEvent& event : current) {
    if (!event.game && !savedIDs.count(event.id)) out.push_back(event);
  }
  return out;
}

// MARK: Last source per game

namespace {

struct Stamp {
  std::string feedID;
  double time = 0;
};

std::optional<Stamp> stamp(const std::string& value) {
  size_t bar = value.rfind('|');
  if (bar == std::string::npos) return std::nullopt;
  std::string number = value.substr(bar + 1);
  if (number.empty()) return std::nullopt;
  char* end = nullptr;
  double t = std::strtod(number.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(t)) return std::nullopt;
  return Stamp{value.substr(0, bar), t};
}

}  // namespace

std::string SourceMemory::key(const SportsEvent& event) { return event.game ? event.game->id : event.id; }

std::optional<std::string> SourceMemory::remembered(const SportsEvent& event, Seconds now) const {
  auto it = map_.find(key(event));
  if (it == map_.end()) return std::nullopt;
  auto s = stamp(it->second);
  if (!s || static_cast<double>(now) - s->time >= static_cast<double>(kLife)) return std::nullopt;
  return s->feedID;
}

bool SourceMemory::remember(const SportsEvent& event, const std::string& feedID, Seconds now) {
  const std::string k = key(event);
  const double t = static_cast<double>(now);
  auto existing = map_.find(k);
  if (existing != map_.end()) {
    auto s = stamp(existing->second);
    // Same source, saved in the last minute: nothing to write.
    if (s && s->feedID == feedID && t - s->time < 60) return false;
  }
  for (auto it = map_.begin(); it != map_.end();) {
    auto s = stamp(it->second);
    it = s && t - s->time < static_cast<double>(kLife) ? std::next(it) : map_.erase(it);
  }
  map_[k] = feedID + "|" + std::to_string(now);
  if (map_.size() > kCap) {
    std::vector<std::pair<double, std::string>> byTime;
    for (const auto& [name, value] : map_) {
      auto s = stamp(value);
      byTime.emplace_back(s ? s->time : 0, name);
    }
    std::stable_sort(byTime.begin(), byTime.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    size_t extra = map_.size() - kCap;
    for (size_t i = 0; i < extra; ++i) map_.erase(byTime[i].second);
  }
  return true;
}

std::string SourceMemory::toJson() const {
  json::Value v = json::Value::object();
  for (const auto& [name, value] : map_) v.set(name, value);
  return json::write(v);
}

std::map<std::string, std::string> SourceMemory::fromJson(std::string_view textBody) {
  std::map<std::string, std::string> out;
  auto doc = json::parse(textBody);
  if (!doc) return out;
  for (const auto& [name, value] : doc->fields()) {
    if (value.isString()) out[name] = value.asString();
  }
  return out;
}

std::optional<std::string> feedToRemember(bool sawFirstFrame, const std::optional<std::string>& playing,
                                          const std::optional<std::string>& manualFeedID,
                                          const std::vector<std::string>& candidates) {
  if (sawFirstFrame) return playing;
  if (manualFeedID && std::find(candidates.begin(), candidates.end(), *manualFeedID) != candidates.end()) {
    return manualFeedID;
  }
  return std::nullopt;
}

}  // namespace marigold::sports::store
