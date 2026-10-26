// Time: Unix seconds, civil dates and the few time zones the app parses in.
//
// Event titles and guides give clock times in US Eastern, UK and other
// provider zones. Those rules are built in (current era only) instead of read
// from the TV's zoneinfo, which old firmware may lack or have out of date.
// The device's own zone (for showing times) comes from libc.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace marigold::time {

using Seconds = int64_t;  // since 1970-01-01T00:00:00Z

struct Civil {
  int year = 1970, month = 1, day = 1;
  int hour = 0, minute = 0, second = 0;
};

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant).
int64_t daysFromCivil(int year, int month, int day);
Civil civilFromSeconds(Seconds t);   // as UTC
Seconds secondsFromCivil(const Civil& c);  // c read as UTC
// 0 = Sunday ... 6 = Saturday.
int weekday(int year, int month, int day);
int daysInMonth(int year, int month);

class Zone {
 public:
  enum class Rule {
    Fixed,
    US,         // second Sunday of March to first Sunday of November
    EU,         // last Sunday of March to last Sunday of October, at 01:00 UTC
    Sydney,     // first Sunday of October to first Sunday of April
    Auckland,   // last Sunday of September to first Sunday of April
    Device,     // libc localtime
  };

  static Zone utc() { return Zone(Rule::Fixed, 0); }
  static Zone fixed(int offsetSeconds) { return Zone(Rule::Fixed, offsetSeconds); }
  static Zone device() { return Zone(Rule::Device, 0); }
  // IANA names the app uses ("America/New_York", "Europe/London", ...).
  static std::optional<Zone> named(std::string_view iana);
  static Zone usEastern();
  static Zone london();

  // Offset from UTC in seconds at the instant `utc`.
  int offsetAt(Seconds utc) const;
  Civil toLocal(Seconds utc) const;
  // Local wall-clock time to an instant. In a DST gap the later reading wins;
  // in an overlap the earlier instant.
  Seconds fromLocal(const Civil& local) const;

  Rule rule() const { return rule_; }
  int standardOffset() const { return standard_; }

 private:
  Zone(Rule r, int standard) : rule_(r), standard_(standard) {}
  bool daylightAt(Seconds utc) const;

  Rule rule_;
  int standard_;
};

Seconds now();

}  // namespace marigold::time
