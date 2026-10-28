#include <gtest/gtest.h>

#include <string>

#include "src/games/ui/ElapsedText.h"

using games::ui::formatElapsed;

namespace {
constexpr uint32_t kSecond = 1000;
constexpr uint32_t kMinute = 60 * kSecond;

std::string text(const uint32_t ms) {
  char out[24];
  formatElapsed(out, sizeof(out), ms, "%u мин", "<1 мин");
  return out;
}
}  // namespace

TEST(GamesElapsedText, WholeMinutesRoundedDown) {
  EXPECT_EQ(text(0), "<1 мин");
  EXPECT_EQ(text(kMinute - 1), "<1 мин");
  EXPECT_EQ(text(kMinute), "1 мин");
  EXPECT_EQ(text(4 * kMinute + 59 * kSecond), "4 мин");
  EXPECT_EQ(text(125 * kMinute), "125 мин");
  EXPECT_EQ(text(UINT32_MAX), "71582 мин");
}

// A game played for ten minutes repaints its timer ten times: once per turned
// minute, through the same decision the screens use.
TEST(GamesElapsedText, RepaintsOnlyWhenTheMinuteTurns) {
  int32_t painted = games::ui::shownMinute(true, 0);
  int repaints = 0;
  for (uint32_t ms = 0; ms <= 10 * kMinute; ms += 100) {
    const int32_t shown = games::ui::shownMinute(true, ms);
    if (games::ui::timerStale(painted, shown)) {
      ++repaints;
      painted = shown;
    }
  }
  EXPECT_EQ(repaints, 10);
  EXPECT_FALSE(games::ui::timerStale(games::ui::shownMinute(true, 0), games::ui::shownMinute(true, 59 * kSecond)));
}

// Without a timer (Klondike, a solved puzzle) nothing ever repaints for time;
// the largest elapsed time stays clear of the -1 sentinel.
TEST(GamesElapsedText, NextMinuteDeadlineLandsOnTheTurn) {
  EXPECT_EQ(games::ui::msUntilNextMinute(0), kMinute);
  EXPECT_EQ(games::ui::msUntilNextMinute(59 * kSecond), kSecond);
  EXPECT_EQ(games::ui::msUntilNextMinute(kMinute), kMinute);
  for (uint32_t ms : {1u, 30 * kSecond, 4 * kMinute + 1, 71582u * kMinute - 1}) {
    EXPECT_EQ(games::ui::elapsedMinutes(ms + games::ui::msUntilNextMinute(ms)), games::ui::elapsedMinutes(ms) + 1)
        << ms;
    EXPECT_EQ(games::ui::elapsedMinutes(ms + games::ui::msUntilNextMinute(ms) - 1), games::ui::elapsedMinutes(ms))
        << ms;
  }
}

TEST(GamesElapsedText, NoTimerNeverRepaints) {
  for (uint32_t ms = 0; ms <= 10 * kMinute; ms += kSecond) {
    EXPECT_FALSE(games::ui::timerStale(-1, games::ui::shownMinute(false, ms)));
  }
  EXPECT_EQ(games::ui::shownMinute(true, UINT32_MAX), 71582);
  EXPECT_TRUE(games::ui::timerStale(-1, games::ui::shownMinute(true, 0)));
}
