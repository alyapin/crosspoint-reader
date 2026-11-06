#include <gtest/gtest.h>

#include "activities/reader/QuietWait.h"

constexpr uint32_t kQuietMs = 2500;

TEST(QuietWait, ElapsesAfterTheQuietTime) {
  QuietWait wait;
  wait.start(1000);
  EXPECT_FALSE(wait.elapsed(1000, false, kQuietMs));
  EXPECT_FALSE(wait.elapsed(3499, false, kQuietMs));
  EXPECT_TRUE(wait.elapsed(3500, false, kQuietMs));
}

// A press right after the page appears pushes the bookkeeping past it.
TEST(QuietWait, BusyFrameStartsOver) {
  QuietWait wait;
  wait.start(0);
  EXPECT_FALSE(wait.elapsed(300, true, kQuietMs));
  EXPECT_FALSE(wait.elapsed(2600, false, kQuietMs));
  EXPECT_TRUE(wait.elapsed(2800, false, kQuietMs));
}

TEST(QuietWait, NeverDuringABusyFrame) {
  QuietWait wait;
  wait.start(0);
  EXPECT_FALSE(wait.elapsed(10000, true, kQuietMs));
  EXPECT_TRUE(wait.elapsed(12500, false, kQuietMs));
}

TEST(QuietWait, SurvivesTheClockWrap) {
  QuietWait wait;
  wait.start(0xFFFFFF00u);
  EXPECT_FALSE(wait.elapsed(0x00000100u, false, kQuietMs));
  EXPECT_TRUE(wait.elapsed(0x00000A00u, false, kQuietMs));
}
