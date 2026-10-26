#include "core/util/time.hpp"

#include <ctime>

namespace marigold::time {
namespace {

constexpr int kHour = 3600;

// The `n`th (1-based) `weekday` of a month; n = -1 means the last one.
int nthWeekday(int year, int month, int wday, int n) {
  if (n > 0) {
    int first = weekday(year, month, 1);
    return 1 + (wday - first + 7) % 7 + (n - 1) * 7;
  }
  int lastDay = daysInMonth(year, month);
  int last = weekday(year, month, lastDay);
  return lastDay - (last - wday + 7) % 7;
}

Seconds at(int year, int month, int day, int hour) {
  return secondsFromCivil({year, month, day, hour, 0, 0});
}

}  // namespace

int64_t daysFromCivil(int year, int month, int day) {
  int64_t y = year - (month <= 2 ? 1 : 0);
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t mp = (month + 9) % 12;
  int64_t doy = (153 * mp + 2) / 5 + day - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

Civil civilFromSeconds(Seconds t) {
  int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
  int64_t secs = t - days * 86400;
  int64_t z = days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  int64_t d = doy - (153 * mp + 2) / 5 + 1;
  int64_t m = mp < 10 ? mp + 3 : mp - 9;
  Civil c;
  c.year = static_cast<int>(y + (m <= 2 ? 1 : 0));
  c.month = static_cast<int>(m);
  c.day = static_cast<int>(d);
  c.hour = static_cast<int>(secs / 3600);
  c.minute = static_cast<int>((secs % 3600) / 60);
  c.second = static_cast<int>(secs % 60);
  return c;
}

Seconds secondsFromCivil(const Civil& c) {
  return daysFromCivil(c.year, c.month, c.day) * 86400 + c.hour * 3600 + c.minute * 60 + c.second;
}

int weekday(int year, int month, int day) {
  int64_t d = daysFromCivil(year, month, day);
  return static_cast<int>(((d % 7) + 11) % 7);  // 1970-01-01 was a Thursday
}

int daysInMonth(int year, int month) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
  }
  return kDays[(month - 1) % 12];
}

std::optional<Zone> Zone::named(std::string_view n) {
  if (n == "UTC" || n == "GMT" || n == "Etc/UTC") return utc();
  if (n == "America/New_York" || n == "America/Toronto" || n == "US/Eastern") return usEastern();
  if (n == "America/Chicago") return Zone(Rule::US, -6 * kHour);
  if (n == "America/Denver") return Zone(Rule::US, -7 * kHour);
  if (n == "America/Los_Angeles") return Zone(Rule::US, -8 * kHour);
  if (n == "Europe/London" || n == "Europe/Dublin" || n == "Europe/Lisbon") return london();
  if (n == "Europe/Madrid" || n == "Europe/Paris" || n == "Europe/Berlin" || n == "Europe/Rome" ||
      n == "Europe/Amsterdam" || n == "Europe/Brussels" || n == "Europe/Vienna" ||
      n == "Europe/Zurich" || n == "Europe/Stockholm" || n == "Europe/Oslo" ||
      n == "Europe/Copenhagen" || n == "Europe/Warsaw" || n == "Europe/Prague") {
    return Zone(Rule::EU, 1 * kHour);
  }
  if (n == "Australia/Sydney" || n == "Australia/Melbourne") return Zone(Rule::Sydney, 10 * kHour);
  if (n == "Pacific/Auckland") return Zone(Rule::Auckland, 12 * kHour);
  // No daylight time since October 2022 and February 2019.
  if (n == "America/Mexico_City") return fixed(-6 * kHour);
  if (n == "America/Sao_Paulo") return fixed(-3 * kHour);
  return std::nullopt;
}

Zone Zone::usEastern() { return Zone(Rule::US, -5 * kHour); }
Zone Zone::london() { return Zone(Rule::EU, 0); }

bool Zone::daylightAt(Seconds utc) const {
  int year = civilFromSeconds(utc + standard_).year;
  switch (rule_) {
    case Rule::US: {
      // 02:00 local standard time to 02:00 local daylight time.
      Seconds start = at(year, 3, nthWeekday(year, 3, 0, 2), 2) - standard_;
      Seconds end = at(year, 11, nthWeekday(year, 11, 0, 1), 2) - (standard_ + kHour);
      return utc >= start && utc < end;
    }
    case Rule::EU: {
      Seconds start = at(year, 3, nthWeekday(year, 3, 0, -1), 1);
      Seconds end = at(year, 10, nthWeekday(year, 10, 0, -1), 1);
      return utc >= start && utc < end;
    }
    case Rule::Sydney: {
      // Southern hemisphere: daylight time spans the new year.
      Seconds end = at(year, 4, nthWeekday(year, 4, 0, 1), 3) - (standard_ + kHour);
      Seconds start = at(year, 10, nthWeekday(year, 10, 0, 1), 2) - standard_;
      return utc < end || utc >= start;
    }
    case Rule::Auckland: {
      Seconds end = at(year, 4, nthWeekday(year, 4, 0, 1), 3) - (standard_ + kHour);
      Seconds start = at(year, 9, nthWeekday(year, 9, 0, -1), 2) - standard_;
      return utc < end || utc >= start;
    }
    case Rule::Fixed:
    case Rule::Device:
      return false;
  }
  return false;
}

int Zone::offsetAt(Seconds utc) const {
  if (rule_ == Rule::Device) {
    std::time_t t = static_cast<std::time_t>(utc);
    std::tm tm{};
    localtime_r(&t, &tm);
    return static_cast<int>(tm.tm_gmtoff);
  }
  return standard_ + (daylightAt(utc) ? kHour : 0);
}

Civil Zone::toLocal(Seconds utc) const { return civilFromSeconds(utc + offsetAt(utc)); }

Seconds Zone::fromLocal(const Civil& local) const {
  Seconds wall = secondsFromCivil(local);
  if (rule_ == Rule::Fixed) return wall - standard_;
  if (rule_ == Rule::Device) {
    std::tm tm{};
    tm.tm_year = local.year - 1900;
    tm.tm_mon = local.month - 1;
    tm.tm_mday = local.day;
    tm.tm_hour = local.hour;
    tm.tm_min = local.minute;
    tm.tm_sec = local.second;
    tm.tm_isdst = -1;
    return static_cast<Seconds>(std::mktime(&tm));
  }
  // Try both possible offsets; keep one that maps back to the same wall time.
  Seconds a = wall - offsetAt(wall - standard_);
  if (a + offsetAt(a) == wall) {
    Seconds b = wall - offsetAt(wall - standard_ - kHour);
    if (b != a && b + offsetAt(b) == wall) return a < b ? a : b;  // overlap: earlier
    return a;
  }
  Seconds b = wall - offsetAt(wall - standard_ - kHour);
  if (b + offsetAt(b) == wall) return b;
  // Gap (spring forward): read the wall time with the standard offset.
  return wall - standard_;
}

Seconds now() { return static_cast<Seconds>(std::time(nullptr)); }

}  // namespace marigold::time
