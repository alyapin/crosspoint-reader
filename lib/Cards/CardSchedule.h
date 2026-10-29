#pragma once

#include <cstdint>

// Leitner boxes: a new card is in box 0; «Хорошо» moves it one box up, «Легко»
// two (box 7 at most), «Снова» back to box 1 and into the same session again.
// The answer after «Снова» counts as for a new card. Intervals from a week up
// get a ±10% spread fixed by the card key, so cards learnt together do not
// all come due on one day.
//
// Days are local days since 1970 (StatsCalendar), counted from 04:00 so late
// night study belongs to the evening before; 0 is no day.
namespace cards {

inline constexpr uint8_t TOP_BOX = 7;
inline constexpr uint8_t BOX_MASK = 0x07;
inline constexpr uint8_t AGAIN_FLAG = 0x80;
inline constexpr uint16_t INTERVAL_DAYS[TOP_BOX + 1] = {0, 1, 3, 7, 14, 30, 60, 120};
inline constexpr uint16_t NO_DAY = 0;
inline constexpr int64_t DAY_START_SECONDS = 4 * 3600;

enum class Grade : uint8_t { Again, Good, Easy };

struct CardState {
  uint16_t due = NO_DAY;
  uint8_t box = 0;  // box and AGAIN_FLAG
  uint8_t lapses = 0;
};

inline bool isNew(const CardState& s) { return s.box == 0; }
inline bool isDue(const CardState& s, const uint16_t today) { return s.box != 0 && s.due <= today; }
// Learning: boxes 1-3; known: 4 and up (two weeks or more).
inline bool isKnown(const CardState& s) { return (s.box & BOX_MASK) >= 4; }

uint8_t boxAfter(uint8_t box, Grade grade);
// Days until the card comes back after `grade`, without the spread.
uint16_t intervalAfter(uint8_t box, Grade grade);
uint16_t dueDay(uint8_t box, uint32_t key, uint16_t today);
CardState answer(const CardState& state, Grade grade, uint32_t key, uint16_t today);

// The epoch whose local day is the card day: the day starts at 04:00. 0 stays unknown.
inline int64_t cardEpoch(const int64_t epoch) { return epoch > DAY_START_SECONDS ? epoch - DAY_START_SECONDS : 0; }

// The day a deck is studied on. With the clock unknown (realDay == NO_DAY)
// that is the last day the deck was studied, so nothing new comes due; a deck
// never studied starts a virtual calendar at day 1. Once the clock is known,
// `shift` moves a virtual calendar onto the real one: add it to every due day.
struct DeckDays {
  uint16_t lastDay = NO_DAY;
  bool virtualDays = false;
};
uint16_t studyDay(uint16_t realDay, DeckDays& days, int32_t& shift);

}  // namespace cards
