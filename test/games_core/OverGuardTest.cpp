#include <OverGuard.h>
#include <gtest/gtest.h>

using games::OverGuard;

TEST(OverGuard, HoldsForAMomentAfterTheGameEnds) {
  OverGuard g;
  g.track(false, 100);
  EXPECT_FALSE(g.holds(false, 200));  // still playing
  EXPECT_TRUE(g.holds(true, 300));    // the press that ended the game, then its double
  EXPECT_TRUE(g.holds(true, 300 + OverGuard::kHoldMs - 1));
  EXPECT_FALSE(g.holds(true, 300 + OverGuard::kHoldMs));
}

TEST(OverGuard, TheEndCountsWhenTracked) {
  OverGuard g;
  g.track(false, 0);
  g.track(true, 5000);  // the computer's move ended it in the loop pass
  EXPECT_TRUE(g.holds(true, 5500));
  EXPECT_FALSE(g.holds(true, 6000));
}

TEST(OverGuard, AGameShownAlreadyOverDoesNotHold) {
  OverGuard g;
  g.reset(true);
  EXPECT_FALSE(g.holds(true, 10));
}

TEST(OverGuard, ANewGameEndsAgain) {
  OverGuard g;
  g.track(false, 0);
  g.track(true, 1000);
  g.track(false, 5000);  // Новая игра
  EXPECT_FALSE(g.holds(false, 5100));
  EXPECT_TRUE(g.holds(true, 9000));
  EXPECT_FALSE(g.holds(true, 10000));
}

TEST(OverGuard, ClockWrap) {
  OverGuard g;
  g.track(false, 0xFFFFFF00u);
  EXPECT_TRUE(g.holds(true, 0xFFFFFF00u));
  EXPECT_TRUE(g.holds(true, 0x100u));  // 512 ms later, across the wrap
  EXPECT_FALSE(g.holds(true, 0x400u));
}
