#include <gtest/gtest.h>

#include <string>

#include "games/chess/ui/ClockText.h"

using chess::ui::clockText;

namespace {
constexpr uint32_t kSecond = 1000;
constexpr uint32_t kMinute = 60 * kSecond;

std::string text(const uint32_t ms) { return clockText(ms).text; }
}  // namespace

TEST(ChessClockText, WholeMinutesRoundedUpAboveOneMinute) {
  EXPECT_EQ(text(10 * kMinute), "10");
  EXPECT_EQ(text(9 * kMinute + 1), "10");
  EXPECT_EQ(text(9 * kMinute), "9");
  EXPECT_EQ(text(kMinute + 1), "2");
  EXPECT_EQ(text(180 * kMinute), "180");
  EXPECT_TRUE(clockText(kMinute + 1).minutes);
}

TEST(ChessClockText, LastMinuteInTenSecondSteps) {
  EXPECT_EQ(text(kMinute), "1:00");
  EXPECT_EQ(text(50 * kSecond + 1), "1:00");
  EXPECT_EQ(text(50 * kSecond), "0:50");
  EXPECT_EQ(text(41 * kSecond), "0:50");
  EXPECT_EQ(text(10 * kSecond), "0:10");
  EXPECT_EQ(text(1), "0:10");
  EXPECT_EQ(text(0), "0:00");
  EXPECT_FALSE(clockText(kMinute).minutes);
}

TEST(ChessClockText, LargeValuesDoNotWrap) {
  EXPECT_EQ(text(UINT32_MAX), "71583");
  EXPECT_EQ(text(UINT32_MAX - 999), "71583");
}

// A clock running down from 10:00 repaints at each minute boundary down to two
// minutes (8), into the last minute (1), then every 10 s (6): 15 repaints.
TEST(ChessClockText, RepaintsOnlyWhenTheShownValueChanges) {
  uint32_t shown = clockText(10 * kMinute).shownSeconds;
  int repaints = 0;
  for (uint32_t ms = 10 * kMinute; ms > 0;) {
    ms = ms > 100 ? ms - 100 : 0;
    const uint32_t now = clockText(ms).shownSeconds;
    if (now != shown) {
      ++repaints;
      shown = now;
    }
  }
  EXPECT_EQ(repaints, 15);
}

// A light-sleeping game screen wakes exactly when the running clock's text changes.
TEST(ChessClockText, MsUntilTextChangeLandsOnTheNextText) {
  using chess::ui::msUntilClockTextChange;
  for (const uint32_t ms : {10 * kMinute, 9 * kMinute + 1, kMinute + 500, kMinute, 50 * kSecond + 1, 5 * kSecond, 1u}) {
    const uint32_t wait = msUntilClockTextChange(ms);
    ASSERT_GT(wait, 0u) << ms;
    EXPECT_EQ(text(ms - wait + 1), text(ms)) << ms;
    EXPECT_NE(text(ms - wait), text(ms)) << ms;
  }
  EXPECT_EQ(msUntilClockTextChange(10 * kMinute), kMinute);
  EXPECT_EQ(msUntilClockTextChange(kMinute + 500), 500u);
  EXPECT_EQ(msUntilClockTextChange(5 * kSecond), 5 * kSecond);  // the flag falls
  EXPECT_EQ(msUntilClockTextChange(0), 0u);
  EXPECT_GT(msUntilClockTextChange(UINT32_MAX), 0u);
}

// After a move the increment can push a clock back across a boundary; the
// shown value follows it both ways.
TEST(ChessClockText, IncrementCrossesBackIntoMinutes) {
  EXPECT_EQ(text(55 * kSecond), "1:00");
  EXPECT_EQ(text(55 * kSecond + 10 * kSecond), "2");
  EXPECT_NE(clockText(55 * kSecond).shownSeconds, clockText(65 * kSecond).shownSeconds);
}
