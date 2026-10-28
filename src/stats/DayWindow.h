#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "StatsCalendar.h"

// Seconds read per local day over the last DAYS days, with the streak state
// of the days that already left the window. Enough for a 12-month heatmap and
// this year's totals; a streak or record of any length stays exact.
class DayWindow {
 public:
  static constexpr uint16_t DAYS = 400;
  // A day counts toward a streak from this much reading.
  static constexpr uint32_t STREAK_MIN_SECONDS = 5 * 60;

  // Days older than the window are dropped; a later day slides the window.
  void add(uint16_t day, uint32_t seconds);
  uint32_t secondsOn(uint16_t day) const;
  // Both ends inclusive.
  uint32_t secondsBetween(uint16_t from, uint16_t to) const;
  // Counting days in a row up to today. Until today counts, a run that ended
  // yesterday is still current.
  uint16_t currentStreak(uint16_t today) const;
  uint16_t bestStreak() const;
  uint16_t lastDay() const { return last; }

  static constexpr size_t ENCODED_SIZE = 3 * sizeof(uint16_t) + DAYS * sizeof(uint16_t);
  void encode(uint8_t* out) const;
  bool decode(const uint8_t* data, size_t size);

 private:
  bool counts(uint16_t day) const { return secondsOn(day) >= STREAK_MIN_SECONDS; }
  uint16_t firstDay() const { return static_cast<uint16_t>(last - (DAYS - 1)); }
  void slide(uint32_t shift);
  void fold(uint16_t value);

  uint16_t last = stats::NO_DAY;
  // Counting run that ends on the day before firstDay().
  uint16_t runBefore = 0;
  // Longest run that reached the days before the window.
  uint16_t best = 0;
  std::array<uint16_t, DAYS> seconds{};
};
