#include "CardSchedule.h"

#include <algorithm>

namespace cards {

uint8_t boxAfter(const uint8_t box, const Grade grade) {
  if (grade == Grade::Again) return 1 | AGAIN_FLAG;
  const int base = (box & AGAIN_FLAG) ? 0 : box & BOX_MASK;
  return static_cast<uint8_t>(std::min<int>(TOP_BOX, base + (grade == Grade::Easy ? 2 : 1)));
}

uint16_t intervalAfter(const uint8_t box, const Grade grade) {
  return grade == Grade::Again ? 0 : INTERVAL_DAYS[boxAfter(box, grade) & BOX_MASK];
}

uint16_t dueDay(const uint8_t box, const uint32_t key, const uint16_t today) {
  if (box & AGAIN_FLAG) return today;
  const int interval = INTERVAL_DAYS[box & BOX_MASK];
  int offset = 0;
  if (interval >= 7) {
    const int spread = (interval + 5) / 10;
    uint32_t h = (key ^ (box * 0x9E3779B9u)) * 0x85EBCA6Bu;
    h ^= h >> 13;
    offset = static_cast<int>(h % static_cast<uint32_t>(2 * spread + 1)) - spread;
  }
  return static_cast<uint16_t>(std::clamp<int>(today + interval + offset, today + 1, UINT16_MAX));
}

CardState answer(const CardState& state, const Grade grade, const uint32_t key, const uint16_t today) {
  CardState next = state;
  next.box = boxAfter(state.box, grade);
  next.due = dueDay(next.box, key, today);
  if (grade == Grade::Again && !isNew(state) && next.lapses < UINT8_MAX) next.lapses++;
  return next;
}

uint16_t studyDay(const uint16_t realDay, DeckDays& days, int32_t& shift) {
  shift = 0;
  if (realDay == NO_DAY) {
    if (days.lastDay != NO_DAY) return days.lastDay;
    days.virtualDays = true;
    return 1;
  }
  if (days.virtualDays) {
    shift = static_cast<int32_t>(realDay) - std::max<int32_t>(days.lastDay, 1);
    days.virtualDays = false;
  }
  return realDay;
}

}  // namespace cards
