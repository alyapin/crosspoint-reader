#pragma once

#include <cstdint>

// Local calendar days for reading statistics. A day is the number of days
// since 1970-01-01 in local time, so it fits uint16_t until 2149. Local time
// comes from localtime_r() under the TZ rule HalClock::setTimezone() installs,
// so the day turns at local midnight, DST included.
namespace stats {

// No calendar day: the clock was not set when the reading happened.
inline constexpr uint16_t NO_DAY = 0;

int32_t daysFromCivil(int year, unsigned month, unsigned day);
void civilFromDays(int32_t days, int& year, unsigned& month, unsigned& day);

// Local day of a UTC epoch, or NO_DAY for epoch 0 (clock unknown).
uint16_t localDay(int64_t epoch);
// Seconds since local midnight, 0 for epoch 0.
uint32_t localSecondsOfDay(int64_t epoch);

// 0 = Monday ... 6 = Sunday.
unsigned weekday(uint16_t day);
// First day of the Monday-based week, month and year containing `day`.
uint16_t weekStart(uint16_t day);
uint16_t monthStart(uint16_t day);
uint16_t yearStart(uint16_t day);

}  // namespace stats
