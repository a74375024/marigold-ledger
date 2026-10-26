// "Remind Me" on a future show or game (LiveReminders.swift, without the
// UI): when it starts, a card appears in the corner with one click to
// watch. The reminders are kept on this TV only; the caller stores the JSON
// and runs check() on a timer.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/util/time.hpp"

namespace marigold::sports {

struct LiveReminder {
  std::string channelID;
  std::string channelName;
  std::string title;
  time::Seconds start = 0;
  time::Seconds end = 0;

  // "<channel id>|<start seconds>".
  std::string id() const;
  bool operator==(const LiveReminder& o) const {
    return channelID == o.channelID && channelName == o.channelName && title == o.title &&
           start == o.start && end == o.end;
  }
};

class Reminders {
 public:
  // How often the caller runs check().
  static constexpr int kCheckSeconds = 15;
  // A reminder is due this long before its start.
  static constexpr time::Seconds kLeadSeconds = 30;
  // The card hides itself after this long.
  static constexpr int kCardSeconds = 15;
  // Two reminders on one channel within a minute are the same one.
  static constexpr time::Seconds kSameStart = 60;

  Reminders() = default;
  // Saved reminders; ones already over are dropped.
  Reminders(std::vector<LiveReminder> saved, time::Seconds now);

  bool isSet(std::string_view channelID, time::Seconds start) const;
  // Adds a reminder (kept by start time), or removes the one already set.
  void toggle(const LiveReminder& reminder);

  struct Check {
    bool changed = false;                // the list changed: save it
    std::optional<LiveReminder> due;     // just became due: show its card
  };
  // Drops reminders that are over; with no card showing, the first one due
  // (starting within kLeadSeconds) is taken off the list and shown.
  Check check(time::Seconds now);
  // The card was closed (or kCardSeconds passed).
  void dismiss() { due_.reset(); }

  const std::vector<LiveReminder>& items() const { return items_; }
  const std::optional<LiveReminder>& due() const { return due_; }

  std::string toJson() const;
  // nullopt when the text isn't a reminder list.
  static std::optional<std::vector<LiveReminder>> fromJson(std::string_view text);

 private:
  std::vector<LiveReminder> items_;
  std::optional<LiveReminder> due_;
};

}  // namespace marigold::sports
