#include <StatsCalendar.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>

#include "CardSchedule.h"

using namespace cards;

TEST(CardSchedule, BoxesMoveUpAndBack) {
  EXPECT_EQ(boxAfter(0, Grade::Good), 1);
  EXPECT_EQ(boxAfter(0, Grade::Easy), 2);
  EXPECT_EQ(boxAfter(3, Grade::Good), 4);
  EXPECT_EQ(boxAfter(6, Grade::Easy), TOP_BOX);
  EXPECT_EQ(boxAfter(TOP_BOX, Grade::Good), TOP_BOX);
  EXPECT_EQ(boxAfter(5, Grade::Again), 1 | AGAIN_FLAG);
  // After «Снова» the next answer counts as for a new card.
  EXPECT_EQ(boxAfter(1 | AGAIN_FLAG, Grade::Good), 1);
  EXPECT_EQ(boxAfter(1 | AGAIN_FLAG, Grade::Easy), 2);
  EXPECT_EQ(intervalAfter(0, Grade::Good), 1);
  EXPECT_EQ(intervalAfter(1, Grade::Good), 3);
  EXPECT_EQ(intervalAfter(1, Grade::Easy), 7);
  EXPECT_EQ(intervalAfter(4, Grade::Again), 0);
  EXPECT_EQ(intervalAfter(1 | AGAIN_FLAG, Grade::Easy), 3);
}

TEST(CardSchedule, AnswerSetsDueAndLapses) {
  const uint16_t today = 20000;
  CardState s;
  s = answer(s, Grade::Good, 42, today);
  EXPECT_EQ(s.box, 1);
  EXPECT_EQ(s.due, today + 1);
  EXPECT_EQ(s.lapses, 0);
  s = answer(s, Grade::Again, 42, today + 1);
  EXPECT_EQ(s.box, 1 | AGAIN_FLAG);
  EXPECT_EQ(s.due, today + 1);
  EXPECT_EQ(s.lapses, 1);
  EXPECT_TRUE(isDue(s, today + 1));
  s = answer(s, Grade::Good, 42, today + 1);
  EXPECT_EQ(s.box, 1);
  EXPECT_EQ(s.due, today + 2);
  // A new card's «Снова» is not a lapse.
  EXPECT_EQ(answer(CardState{}, Grade::Again, 1, today).lapses, 0);
}

TEST(CardSchedule, SpreadIsBoundedAndFixedByKey) {
  const uint16_t today = 20000;
  for (uint8_t box = 1; box <= TOP_BOX; box++) {
    const int interval = INTERVAL_DAYS[box];
    const int spread = interval >= 7 ? (interval + 5) / 10 : 0;
    int lo = 1 << 30;
    int hi = 0;
    for (uint32_t key = 0; key < 500; key++) {
      const int due = dueDay(box, key * 2654435761u, today) - today;
      EXPECT_EQ(due, dueDay(box, key * 2654435761u, today) - today);
      lo = std::min(lo, due);
      hi = std::max(hi, due);
    }
    EXPECT_GE(lo, interval - spread) << int(box);
    EXPECT_LE(hi, interval + spread) << int(box);
    if (spread > 0) EXPECT_LT(lo, hi) << int(box);
  }
}

TEST(CardSchedule, DayStartsAtFour) {
  setenv("TZ", "UTC0", 1);
  tzset();
  const int64_t midnight = static_cast<int64_t>(stats::daysFromCivil(2026, 10, 29)) * 86400;
  const uint16_t oct28 = static_cast<uint16_t>(stats::daysFromCivil(2026, 10, 28));
  EXPECT_EQ(stats::localDay(cardEpoch(midnight + 3 * 3600 + 59 * 60)), oct28);
  EXPECT_EQ(stats::localDay(cardEpoch(midnight + 4 * 3600)), oct28 + 1);
  EXPECT_EQ(stats::localDay(cardEpoch(0)), stats::NO_DAY);
}

TEST(CardSchedule, UnknownClock) {
  int32_t shift = 0;
  DeckDays fresh;
  EXPECT_EQ(studyDay(NO_DAY, fresh, shift), 1);
  EXPECT_TRUE(fresh.virtualDays);
  fresh.lastDay = 1;
  EXPECT_EQ(studyDay(NO_DAY, fresh, shift), 1);
  EXPECT_EQ(studyDay(20500, fresh, shift), 20500);
  EXPECT_EQ(shift, 20499);
  EXPECT_FALSE(fresh.virtualDays);

  DeckDays studied{20400, false};
  EXPECT_EQ(studyDay(NO_DAY, studied, shift), 20400);
  EXPECT_EQ(shift, 0);
  EXPECT_FALSE(studied.virtualDays);
  EXPECT_EQ(studyDay(20410, studied, shift), 20410);
  EXPECT_EQ(shift, 0);
}
