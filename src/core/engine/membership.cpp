#include "core/engine/membership.hpp"

namespace marigold::engine::membership {
namespace {

const char* const kShortMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kLongMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                   "July",    "August",   "September", "October", "November", "December"};
const char* const kWeekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kDot = " \xc2\xb7 ";
const char* const kRenew = "Renew it with your provider to keep its channels.";

int monthIndex(const time::Civil& c) { return c.month >= 1 && c.month <= 12 ? c.month - 1 : 0; }

// "Mar 3, 2027", or "Nov 10" without the year.
std::string shortDate(const time::Civil& c, bool year) {
  std::string out = std::string(kShortMonths[monthIndex(c)]) + " " + std::to_string(c.day);
  return year ? out + ", " + std::to_string(c.year) : out;
}
// "March 3, 2027", or "November 10".
std::string longDate(const time::Civil& c, bool year) {
  std::string out = std::string(kLongMonths[monthIndex(c)]) + " " + std::to_string(c.day);
  return year ? out + ", " + std::to_string(c.year) : out;
}
// "Monday, November 10, 2026".
std::string fullDate(const time::Civil& c) {
  return std::string(kWeekdays[time::weekday(c.year, c.month, c.day)]) + ", " + longDate(c, true);
}

// "in 5 days", "tomorrow", "today".
std::string when(int days) {
  if (days <= 0) return "today";
  if (days == 1) return "tomorrow";
  return "in " + std::to_string(days) + " days";
}

std::string streamsLine(const Membership& m) {
  if (!m.streams || *m.streams <= 0) return "";
  if (*m.streams == 1) return "One stream at a time.";
  return "Up to " + std::to_string(*m.streams) + " streams at once.";
}

std::string lowerFirst(std::string s) {
  if (!s.empty() && s[0] >= 'A' && s[0] <= 'Z') s[0] = static_cast<char>(s[0] - 'A' + 'a');
  return s;
}

// An end date that has passed (a status of ended with a later date is
// shown without one).
bool datedEnd(const Membership& m, time::Seconds now) { return m.ends && *m.ends <= now; }

}  // namespace

int64_t localDay(time::Seconds t, const time::Zone& zone) {
  time::Civil c = zone.toLocal(t);
  return time::daysFromCivil(c.year, c.month, c.day);
}

int daysLeft(time::Seconds ends, time::Seconds now, const time::Zone& zone) {
  return static_cast<int>(localDay(ends, zone) - localDay(now, zone));
}

Stage stage(const Membership& m, time::Seconds now, const time::Zone& zone) {
  if (m.endedStatus || (m.ends && *m.ends <= now)) return Stage::Ended;
  if (!m.ends) return Stage::None;
  return daysLeft(*m.ends, now, zone) < kSoonDays ? Stage::Soon : Stage::Far;
}

bool warns(const Membership& m, time::Seconds now, const time::Zone& zone) {
  Stage s = stage(m, now, zone);
  return s == Stage::Soon || s == Stage::Ended;
}

std::string rowText(const Membership& m, time::Seconds now, const time::Zone& zone) {
  std::string out;
  switch (stage(m, now, zone)) {
    case Stage::None: return "";
    case Stage::Far: out = "Ends " + shortDate(zone.toLocal(*m.ends), true); break;
    case Stage::Soon: {
      int days = daysLeft(*m.ends, now, zone);
      out = days <= 0 ? std::string("Ends today")
                      : "Ends " + when(days) + kDot + shortDate(zone.toLocal(*m.ends), false);
      break;
    }
    case Stage::Ended: {
      std::string date;
      if (datedEnd(m, now)) {
        time::Civil end = zone.toLocal(*m.ends);
        date = " " + shortDate(end, end.year != zone.toLocal(now).year);
      }
      out = "Ended" + date + kDot + "renew it with your provider";
      break;
    }
  }
  return m.trial ? "Trial" + std::string(kDot) + lowerFirst(out) : out;
}

std::string accountLine(const std::string& kind, const std::string& state, const Membership& m, time::Seconds now,
                        const time::Zone& zone) {
  std::string row = rowText(m, now, zone);
  std::string out = kind;
  if (stage(m, now, zone) != Stage::Ended) out += kDot + (state.empty() ? std::string("Signing in\xe2\x80\xa6") : state);
  if (!row.empty()) out += kDot + row;
  return out;
}

Text pageNote(const Membership& m, time::Seconds now, const time::Zone& zone) {
  const char* what = m.trial ? "Trial" : "Membership";
  switch (stage(m, now, zone)) {
    case Stage::None: return {};
    case Stage::Far: {
      time::Civil end = zone.toLocal(*m.ends);
      return {what, "Ends " + fullDate(end) + kDot + when(daysLeft(*m.ends, now, zone))};
    }
    case Stage::Soon: {
      time::Civil end = zone.toLocal(*m.ends);
      return {std::string(what) + " ends " + when(daysLeft(*m.ends, now, zone)), fullDate(end) + ". " + kRenew};
    }
    case Stage::Ended: {
      std::string title = std::string(what) + " has ended";
      if (datedEnd(m, now)) {
        time::Civil end = zone.toLocal(*m.ends);
        title = std::string(what) + " ended on " + longDate(end, end.year != zone.toLocal(now).year);
      }
      return {title, "Renew it with your provider; its channels come back by themselves."};
    }
  }
  return {};
}

std::string streamsNote(const Membership& m) {
  if (!m.streams || *m.streams <= 0) return "";
  return std::to_string(*m.streams) + " at a time on this account. Another TV watching it counts too.";
}

Text info(const Membership& m, bool signInFailed, time::Seconds now, const time::Zone& zone) {
  std::string streams = streamsLine(m);
  auto withStreams = [&streams](std::string text) { return streams.empty() ? text : text + " " + streams; };
  const char* what = m.trial ? "Trial" : "Membership";
  switch (stage(m, now, zone)) {
    case Stage::None: return {"", streams};
    case Stage::Far: {
      int days = daysLeft(*m.ends, now, zone);
      return {std::string(what) + " ends " + longDate(zone.toLocal(*m.ends), true),
              withStreams(std::to_string(days) + " days left.")};
    }
    case Stage::Soon: {
      int days = daysLeft(*m.ends, now, zone);
      return {std::string(what) + " ends " + when(days),
              withStreams(fullDate(zone.toLocal(*m.ends)) + ". " + kRenew)};
    }
    case Stage::Ended: {
      std::string heading = m.trial ? "Its trial has ended" : "Its membership has ended";
      if (datedEnd(m, now)) {
        time::Civil end = zone.toLocal(*m.ends);
        heading = std::string(m.trial ? "Its trial" : "Its membership") + " ended on " +
                  longDate(end, end.year != zone.toLocal(now).year);
      }
      std::string text = signInFailed ? "The provider no longer lets this TV sign in. Renew it with your provider "
                                        "and its channels come back by themselves."
                                      : std::string(kRenew);
      return {heading, text + "\nChannels from your other providers keep playing."};
    }
  }
  return {};
}

std::string headsUp(const std::string& name, const Membership& m, time::Seconds now, const time::Zone& zone) {
  switch (stage(m, now, zone)) {
    case Stage::None:
    case Stage::Far: return "";
    case Stage::Soon: return name + " ends " + when(daysLeft(*m.ends, now, zone));
    case Stage::Ended: {
      if (!datedEnd(m, now)) return name + " has ended";
      time::Civil end = zone.toLocal(*m.ends);
      return name + " ended " + shortDate(end, end.year != zone.toLocal(now).year);
    }
  }
  return "";
}

bool headsUpDue(int64_t lastDay, time::Seconds now, const time::Zone& zone) { return localDay(now, zone) != lastDay; }

}  // namespace marigold::engine::membership
