#include "DayWindow.h"

#include <algorithm>
#include <cstring>

void DayWindow::fold(const uint16_t value) {
  if (value >= STREAK_MIN_SECONDS) {
    if (runBefore < UINT16_MAX) runBefore++;
    best = std::max(best, runBefore);
  } else {
    runBefore = 0;
  }
}

void DayWindow::slide(const uint32_t shift) {
  const uint32_t dropped = std::min<uint32_t>(shift, DAYS);
  for (uint32_t i = 0; i < dropped; i++) fold(seconds[i]);
  // Days between the old window and the new one were not read.
  if (shift > DAYS) runBefore = 0;
  std::copy(seconds.begin() + dropped, seconds.end(), seconds.begin());
  std::fill(seconds.end() - dropped, seconds.end(), 0);
  last = static_cast<uint16_t>(last + shift);
}

void DayWindow::add(const uint16_t day, const uint32_t value) {
  if (day < DAYS) return;  // NO_DAY, or a clock far in the past
  if (last == stats::NO_DAY) last = day;
  if (day > last) slide(day - last);
  if (day < firstDay()) return;
  uint16_t& slot = seconds[DAYS - 1 - (last - day)];
  slot = static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, slot + value));
}

uint32_t DayWindow::secondsOn(const uint16_t day) const {
  if (last == stats::NO_DAY || day > last || day < firstDay()) return 0;
  return seconds[DAYS - 1 - (last - day)];
}

uint32_t DayWindow::secondsBetween(const uint16_t from, const uint16_t to) const {
  uint32_t sum = 0;
  for (uint32_t day = from; day <= to; day++) sum += secondsOn(static_cast<uint16_t>(day));
  return sum;
}

uint16_t DayWindow::currentStreak(const uint16_t today) const {
  if (today < DAYS || last == stats::NO_DAY) return 0;
  uint16_t day = counts(today) ? today : static_cast<uint16_t>(today - 1);
  uint16_t run = 0;
  while (counts(day)) {
    run++;
    if (day == firstDay()) return static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, run + runBefore));
    day--;
  }
  return run;
}

uint16_t DayWindow::bestStreak() const {
  uint16_t run = runBefore;
  uint16_t longest = best;
  for (const uint16_t value : seconds) {
    run = value >= STREAK_MIN_SECONDS ? static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, run + 1u)) : 0;
    longest = std::max(longest, run);
  }
  return longest;
}

void DayWindow::encode(uint8_t* out) const {
  const uint16_t head[3] = {last, runBefore, best};
  std::memcpy(out, head, sizeof(head));
  std::memcpy(out + sizeof(head), seconds.data(), DAYS * sizeof(uint16_t));
}

bool DayWindow::decode(const uint8_t* data, const size_t size) {
  if (!data || size != ENCODED_SIZE) return false;
  uint16_t head[3];
  std::memcpy(head, data, sizeof(head));
  if (head[0] != stats::NO_DAY && head[0] < DAYS) return false;
  last = head[0];
  runBefore = head[1];
  best = head[2];
  std::memcpy(seconds.data(), data + sizeof(head), DAYS * sizeof(uint16_t));
  return true;
}
