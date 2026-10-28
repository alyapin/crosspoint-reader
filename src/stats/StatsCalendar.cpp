#include "StatsCalendar.h"

#include <ctime>

namespace stats {

// Howard Hinnant's days_from_civil / civil_from_days.
int32_t daysFromCivil(int year, const unsigned month, const unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153u * (month > 2 ? month - 3 : month + 9) + 2u) / 5u + day - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

void civilFromDays(int32_t days, int& year, unsigned& month, unsigned& day) {
  days += 719468;
  const int era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
  const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
  const unsigned mp = (5u * doy + 2u) / 153u;
  day = doy - (153u * mp + 2u) / 5u + 1u;
  month = mp < 10 ? mp + 3 : mp - 9;
  year = static_cast<int>(yoe) + era * 400 + (month <= 2);
}

uint16_t localDay(const int64_t epoch) {
  if (epoch <= 0) return NO_DAY;
  const time_t t = static_cast<time_t>(epoch);
  struct tm local{};
  if (!localtime_r(&t, &local)) return NO_DAY;
  const int32_t days = daysFromCivil(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
                                     static_cast<unsigned>(local.tm_mday));
  return days > 0 && days <= UINT16_MAX ? static_cast<uint16_t>(days) : NO_DAY;
}

uint32_t localSecondsOfDay(const int64_t epoch) {
  if (epoch <= 0) return 0;
  const time_t t = static_cast<time_t>(epoch);
  struct tm local{};
  if (!localtime_r(&t, &local)) return 0;
  return static_cast<uint32_t>(local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec);
}

// 1970-01-01 was a Thursday.
unsigned weekday(const uint16_t day) { return (day + 3u) % 7u; }

uint16_t weekStart(const uint16_t day) { return static_cast<uint16_t>(day - weekday(day)); }

uint16_t monthStart(const uint16_t day) {
  int y;
  unsigned m, d;
  civilFromDays(day, y, m, d);
  return static_cast<uint16_t>(day - (d - 1));
}

uint16_t yearStart(const uint16_t day) {
  int y;
  unsigned m, d;
  civilFromDays(day, y, m, d);
  return static_cast<uint16_t>(daysFromCivil(y, 1, 1));
}

}  // namespace stats
