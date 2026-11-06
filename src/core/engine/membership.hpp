// Provider memberships (Build 144): the end date an Xtream panel gives at
// sign-in and with every channel list read (also for an M3U playlist whose
// link is an Xtream panel's), in words for Settings › Providers and the
// guide's status line. Days are counted on the TV's own calendar. An
// account with no end date (or 0: unlimited) shows nothing; Emby / Jellyfin
// has none.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/util/time.hpp"

namespace marigold::engine::membership {

struct Membership {
  std::optional<time::Seconds> ends;  // none: no end date
  bool trial = false;
  std::optional<int> streams;  // at once (max_connections); none or 0: not said
  bool endedStatus = false;    // the provider says it has ended ("Expired")
};

// The last week: the row's line turns amber and the guide gives a heads-up.
constexpr int kSoonDays = 7;
// The guide's heads-up stays this long, once a day.
constexpr uint32_t kHeadsUpMs = 8000;

// The calendar day (days since 1970-01-01) an instant falls on in `zone`.
int64_t localDay(time::Seconds t, const time::Zone& zone);
// Calendar days from today to the end's day, both in `zone`: 0 ends today,
// 1 tomorrow (negative once that day has passed).
int daysLeft(time::Seconds ends, time::Seconds now, const time::Zone& zone);

enum class Stage { None, Far, Soon, Ended };
Stage stage(const Membership& m, time::Seconds now, const time::Zone& zone);
// Soon or Ended: said in amber.
bool warns(const Membership& m, time::Seconds now, const time::Zone& zone);

// The part of a provider row's second line: "Ends Mar 3, 2027", "Ends in 5
// days · Nov 10", "Ends tomorrow · Nov 10", "Ends today", "Ended Nov 10 ·
// renew it with your provider"; a trial "Trial · ends Mar 3, 2027". Empty
// with no end date.
std::string rowText(const Membership& m, time::Seconds now, const time::Zone& zone);
// The whole line: "Xtream · Signed in · Ends Mar 3, 2027". Once it has
// ended the end takes the sign-in state's place ("Xtream · Ended Nov 10 ·
// renew it with your provider", not a bare "Couldn't sign in").
std::string accountLine(const std::string& kind, const std::string& state, const Membership& m, time::Seconds now,
                        const time::Zone& zone);

struct Text {
  std::string title, detail;
};
// The provider page's Membership note: "Membership ends in 5 days" over
// "Monday, November 10, 2026. Renew it with your provider to keep its
// channels.", "Membership" over "Ends Tuesday, March 3, 2027 · in 118
// days", "Membership ended on November 10" over what to do. Empty title:
// no note.
Text pageNote(const Membership& m, time::Seconds now, const time::Zone& zone);
// "Streams at once": "2 at a time on this account. Another TV watching it
// counts too." Empty when not said.
std::string streamsNote(const Membership& m);
// The right column for a provider row: a heading ("Membership ends March 3,
// 2027") and words ("118 days left. Up to 2 streams at once."). Both empty
// with nothing to say. `signInFailed`: the provider refused it just now.
Text info(const Membership& m, bool signInFailed, time::Seconds now, const time::Zone& zone);
// The guide's heads-up in the last week and after the end: "Backup TV ends
// in 5 days", "… ends tomorrow", "… ends today", "… ended Nov 10". Empty
// otherwise.
std::string headsUp(const std::string& name, const Membership& m, time::Seconds now, const time::Zone& zone);
// Once a day: not yet shown on today's date (`lastDay` from localDay; 0:
// never).
bool headsUpDue(int64_t lastDay, time::Seconds now, const time::Zone& zone);

}  // namespace marigold::engine::membership
