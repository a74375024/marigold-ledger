// Build 181, "This feed is wrong…": a channel's copy (a feed) the viewer
// set aside for that channel, with why. Set aside for that channel only,
// never for the copy everywhere:
//
//   every reason but Listings  not played, not replayed, and its listings
//                              never used for the channel (the guide's
//                              choice, Rewind's past, Live Events' replay)
//   Listings                   still played and replayed; only its listings
//                              are never used for the channel
//
// "It's a different channel" may name the channel it is: the copy is then
// a source of that channel (Store's copy moves, catalog::moveCopies).
//
// Kept with the lists (lists.json "setAside"), with the provider's own
// identity of the copy (its account, stream id, name and group) so a list
// reload that renumbers it finds it again (rematch). Pure: no store, no
// screen. Never a link, a host or a login in anything here.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/model/channel.hpp"
#include "core/util/json.hpp"
#include "core/util/time.hpp"

namespace marigold::engine::setaside {

enum class Reason : uint8_t { OtherChannel, ReplayOther, WrongCity, WrongLanguage, Listings, DoesntWork, Other };

// In the order the reason panel lists them.
const std::vector<Reason>& reasons();
// "It's a different channel" (the panel's row), its second line ("Shows
// another channel's programmes"; "" for Something else), and in plain
// words for the log ("it's a different channel").
std::string title(Reason r);
std::string detail(Reason r);
std::string plain(Reason r);
// Saved as ("other-channel", "replay", "city", "language", "listings",
// "broken", "other"); nullopt for a word not known.
std::string key(Reason r);
std::optional<Reason> fromKey(std::string_view k);
// Still played (and replayed) on its channel: only Listings.
inline bool playable(Reason r) { return r == Reason::Listings; }

struct Record {
  std::string entryId;  // the channel it is set aside for
  std::string copyId;   // the raw channel ("<server>|<item>")
  Reason reason = Reason::Other;
  std::string named;      // "It's a different channel": the entry it is ("" none)
  std::string namedName;  // that channel's name when named
  std::string entryName;  // the channel's name when set aside
  int feed = 0;           // its number in Sources when set aside ("Feed 3")
  time::Seconds at = 0;
  // The provider's own identity of the copy: its account ("xt:1"), its
  // stream id, its name and group in the provider's list.
  std::string server, item, name, group;
};

json::Value toJson(const Record& r);
std::optional<Record> fromJson(const json::Value& v);

// "Feed 3".
std::string feedName(int number);
// The numbers Sources gives `count` feeds in order: 1, 2, 3... past the
// numbers its feeds set aside keep (`taken`), so Feed 3 set aside leaves
// Feed 1, Feed 2, Feed 4 and no second Feed 3.
std::vector<int> feedNumbers(size_t count, const std::vector<int>& taken);
// The reason as its rows say it, in amber: "It's NBC Sports Bay Area" when
// a channel was named, else the reason's title.
std::string reasonLine(const Record& r);
// "It's NBC Sports Bay Area · Dream · today" (Sources' SET ASIDE row and
// Settings › Help › Feeds you set aside); `day` from sourcetest::testedDay.
std::string rowLine(const Record& r, std::string_view provider, std::string_view day);

// The playback log's "set aside" detail: the reason in plain words (with
// the channel named), the provider's own name and group of the copy, what
// the copy's own listing has on now, and what the channel's guide shows
// now; parts not known are left out:
//   it's a different channel: NBC Sports Bay Area · its name "US: NBC BAY
//   AREA (KNTV)" in "|US| ✪ NFL TEAMS" · on now: "49ers Now" · guide:
//   "Jazz at Clippers"
std::string logDetail(const Record& r, std::string_view onNow, std::string_view guideNow);
// "used again": what it had been set aside as ("it's a different channel:
// NBC Sports Bay Area, set aside Nov 16").
std::string usedAgainDetail(const Record& r, std::string_view day);

// Finds the copy again after a list reload: its id when it is still there,
// else a copy of the same account with the same name and group (the
// provider renumbered it). `find(id)` gives a raw channel by id (null when
// none). Returns the id found ("" none).
template <class Find, class Each>
std::string rematch(const Record& r, const Find& find, const Each& each) {
  if (find(r.copyId)) return r.copyId;
  if (r.name.empty()) return "";
  std::string found;
  each([&](const Channel& c) {
    if (!found.empty() || c.serverId != r.server || c.name != r.name) return;
    if (c.group.value_or("") != r.group) return;
    found = c.id;
  });
  return found;
}

}  // namespace marigold::engine::setaside
