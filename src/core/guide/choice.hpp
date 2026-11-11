// Which schedule a guide row shows (Build 154, listings by channel
// identity). A row's copies bring several schedules: the provider's guide
// by each copy's guide id (topped up by its short guide) and outside
// listings matched by name or call sign. Some belong to another channel
// (a guide id pointing at a local station), another country (a UK channel
// of the same name) or the other coast (a West schedule on an East row).
//
// For each row:
// 1. Identity. Each schedule's guide channel is read through the same
//    reference data the lineup uses (reference channel, station call sign,
//    country). One that is another known channel, station or country is
//    set aside; one that says nothing either way is kept (as weaker).
// 2. Coast. Outside listings' labels ("West", "Pacific", an East listing
//    moved 3 h for a West feed) are taken as true; the provider's are only
//    claims. A provider schedule is lined up with a trusted one at 0 and
//    ±3 h, and the shift that matches tells its real coast. Each schedule
//    is then moved to the row's coast (+3 h East to West, −3 h West to
//    East).
// 3. Distinct schedules. Schedules that agree (most start times within 5
//    minutes with the same title over the next 6 hours) are one schedule;
//    a schedule counts its independent sources (each provider's guide,
//    each outside file), so many copies on one wrong schedule don't outvote
//    one right one. The best one's fullest list is shown, its gaps filled
//    from the schedules that agree with it only.
// 4. The viewer's pick ("Listings look wrong?") wins while that schedule is
//    still there.
//
// Pure: the store gathers each row's copies (MemberGuide) and calls these on
// a worker. Bounded: at most kMaxCandidates schedules per row and
// kMaxMarks programmes per comparison; titles are reduced once per schedule.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/guide/outside.hpp"
#include "core/model/channel.hpp"
#include "core/util/time.hpp"

namespace marigold::guide::choice {

constexpr size_t kMaxCandidates = 20;
constexpr size_t kMaxMarks = 30;
// Agreement: this share of the programmes compared, at least kMinCompared
// of them, over the next kCompareMinutes.
constexpr double kAgree = 0.6;
constexpr int kMinCompared = 3;
constexpr int kCompareMinutes = 360;
constexpr int kStartSlack = 5;  // minutes
constexpr int kCoastShift = 180;

enum class Coast : uint8_t { unknown, east, west };

// What a guide channel's name or id stands for. Empty fields: not known.
struct Identity {
  std::string ref;       // reference channel id ("discovery-channel")
  std::string callsign;  // a station's call sign, bare ("WCBS")
  std::string country;   // "US", "CA", "UK"...
  Coast coast = Coast::unknown;
};

// A name ("US: DISCOVERY WEST HD", "WCBS-DT", "UK: Discovery HD") read
// through the reference data and its station list. Cached (thread safe).
Identity identifyName(std::string_view name);
// A provider guide id ("DiscoveryChannel.us", "WCBS.us", "nbc30wvit.us",
// "TheWeatherNetwork.ca"): its country suffix, a station call sign it is
// or ends with, and the channel its words name. Cached (thread safe).
Identity identifyId(std::string_view guideId);

// The row the schedules are for.
struct Row {
  std::string id;        // "e|r:discovery-channel~west"
  std::string ref;       // from "e|r:<id>"
  std::string callsign;  // from "e|cs:<SIGN>|<network>"
  std::string country;   // "US"... (empty: not known)
  bool west = false;
  // A reference channel or station: its identity and country are certain.
  bool known = false;
};
Row rowOf(const std::string& entryId, const std::optional<std::string>& country);

// One copy of a row with what the guides gave it (the store's snapshot).
struct MemberGuide {
  std::string id;        // raw channel id
  std::string serverId;  // "xt:..."
  std::string epgId;     // the provider's guide id
  Schedule provider;     // its full guide, by guide id
  std::string providerNote;  // "Provider guide: <name> (by guide id)"
  Schedule outside;          // outside listings (one source, gaps from agreeing ones)
  std::string outsideNote;   // "US national: <name> (by name); UK: ... [other country, not used]"
  // Outside listings kept apart from `outside` (another schedule), each
  // with its note part ("US national: <name> (by network CBS News)").
  std::vector<std::pair<Schedule, std::string>> others;
  // The provider's short guide (Xtream) or the server's own (Emby).
  std::shared_ptr<const std::vector<Program>> server;
  bool xtream = false;
};

// One outside note part: "US national: Discovery Channel HD (by name, West
// feed 3 h later)". Empty source when it isn't one.
struct NotePart {
  std::string source;  // "US national"
  std::string name;    // "Discovery Channel HD"
  std::string how;     // "by name, West feed 3 h later"
  bool unused = false;  // "[... not used]" / "[... kept apart]"
};
std::vector<NotePart> noteParts(std::string_view note);
// "Provider guide: <name> (by guide id)" -> name.
std::string providerGuideName(std::string_view note);

// One schedule a row could show.
struct Candidate {
  // Stable while the schedule is there: "pg:<server>|<guide id>",
  // "og:<source id>|<guide channel name>", "sv:<server>|<channel id>".
  std::string key;
  std::string source;  // independent source: "pg:<server>", "og:<source id>"
  std::string label;   // for the report: `provider guide "US: HISTORY" [historychannel.us]`
  std::string kind;    // "provider guide", "outside US national", "server guide"
  std::string name;    // its guide channel's name (may be empty)
  std::string guideId;
  std::string country;  // its source's country (outside), else from name / id
  bool outside = false;
  int shifted = 0;  // minutes its listing was already moved (180: West made from East)
  std::string channelId;  // a copy it came with (programme ids)
  Schedule raw;           // minutes since 1970
  std::shared_ptr<const std::vector<Program>> extra;  // short guide, for gaps
};

// A row's schedules, one per distinct list: copies sharing a guide id (or
// an outside guide channel) are one. At most kMaxCandidates, provider and
// server guides first.
std::vector<Candidate> candidates(const std::vector<MemberGuide>& members);

struct Option {
  size_t index = 0;  // into the candidates
  int shift = 0;     // minutes, to the row's coast
  bool rejected = false;
};

struct Decision {
  int chosen = -1;  // candidate index; -1: nothing to show
  int automatic = -1;  // what would be shown without the viewer's pick
  int shift = 0;    // minutes added to the chosen schedule
  // Schedules that agree with it (index, shift): its gaps come from these.
  std::vector<std::pair<size_t, int>> fill;
  bool picked = false;    // the viewer's pick
  bool pickGone = false;  // a pick that is no longer among the schedules
  std::string why;        // the report's guide_choice
  std::string rejected;   // the report's guide_rejected
  // The distinct schedules for "Listings look wrong?": the chosen one
  // first, then the others, then those set aside.
  std::vector<Option> options;
};

// `now`: minutes since 1970. `windowStart`: the guide grid's start
// (seconds), for coverage. `pick`: the viewer's choice (a candidate key).
Decision decide(const Row& row, const std::vector<Candidate>& candidates, int now,
                time::Seconds windowStart, const std::string& pick = std::string());

// A candidate's programmes moved by `shift` minutes, for the guide window
// around `windowStart` (an hour before to 13 h after, as catalog::listing).
std::vector<Program> programsOf(const Candidate& c, int shift, time::Seconds windowStart);
// The row's listing: the chosen schedule, its gaps from `fill`.
std::vector<Program> listing(const Decision& d, const std::vector<Candidate>& candidates,
                             time::Seconds windowStart);

// MARK: Agreement (exposed for the outside build and tests)

// A list's start times and reduced titles, `from` to `to` (minutes).
struct Mark {
  int s = 0;
  uint32_t key = 0;
};
std::vector<Mark> marks(const std::vector<RawProgram>* raw, const std::vector<Program>* extra, int from,
                        int to);
uint32_t titleKey(std::string_view title);

struct Agreement {
  int matched = 0;
  int compared = 0;
  bool agrees() const;     // enough of enough
  bool differs() const;    // compared enough, and too few matched
};
// `a` moved by `da` against `b` moved by `db`, over [from, from + minutes).
Agreement agreement(const std::vector<Mark>& a, int da, const std::vector<Mark>& b, int db, int from,
                    int minutes = kCompareMinutes);

}  // namespace marigold::guide::choice
