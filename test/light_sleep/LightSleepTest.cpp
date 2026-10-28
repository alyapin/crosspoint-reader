#include <gtest/gtest.h>

#include "util/LightSleep.h"

using namespace light_sleep;

namespace {
// Idle, nothing pending, touch wake already proven, no auto-sleep.
State idle() {
  State s;
  s.idleMs = 5000;
  s.touchWakeProven = true;
  return s;
}
}  // namespace

TEST(LightSleep, SleepsUntilTheBackstopWhenOnlyInputMatters) { EXPECT_EQ(sleepMs(idle()), kMaxSleepMs); }

TEST(LightSleep, EveryBlockerKeepsTheLoopPolling) {
  for (bool State::* blocker : {&State::radioOn, &State::usbConnected, &State::workPending, &State::inputHeld}) {
    State s = idle();
    s.*blocker = true;
    EXPECT_EQ(sleepMs(s), 0u);
  }
}

TEST(LightSleep, UnprovenTouchWakeKeepsTheOldPollPeriod) {
  State s = idle();
  s.touchWakeProven = false;
  EXPECT_EQ(sleepMs(s), kUnprovenTouchWakeMs);
}

TEST(LightSleep, ActivityDeadlineShortensTheSleep) {
  State s = idle();
  s.activityBudgetMs = 1234;
  EXPECT_EQ(sleepMs(s), 1234u);
  s.activityBudgetMs = 0;  // background work wants every pass
  EXPECT_EQ(sleepMs(s), 0u);
  s.activityBudgetMs = kMinSleepMs - 1;  // too short to be worth a sleep
  EXPECT_EQ(sleepMs(s), 0u);
  s.activityBudgetMs = kMinSleepMs;
  EXPECT_EQ(sleepMs(s), kMinSleepMs);
}

TEST(LightSleep, WakesForTheAutoSleepTimeout) {
  State s = idle();
  s.autoSleepMs = 10 * 60 * 1000;
  s.idleMs = s.autoSleepMs - 1500;
  EXPECT_EQ(sleepMs(s), 1500u);
  s.idleMs = s.autoSleepMs;  // due: the loop enters deep sleep on this pass
  EXPECT_EQ(sleepMs(s), 0u);
  s.idleMs = s.autoSleepMs + 1;
  EXPECT_EQ(sleepMs(s), 0u);
}

TEST(LightSleep, FrontlightSleepsOnlyOnTheCrystalClockAndBriefly) {
  State s = idle();
  s.frontlightOn = true;
  EXPECT_EQ(sleepMs(s), 0u);
  s.frontlightOnCrystal = true;
  EXPECT_EQ(sleepMs(s), kCrystalSleepMaxMs);
  s.frontlightOn = false;  // the clock source does not matter with the light off
  s.frontlightOnCrystal = false;
  EXPECT_EQ(sleepMs(s), kMaxSleepMs);
}

TEST(LightSleep, MsUntilIsWrapSafe) {
  EXPECT_EQ(msUntil(1500, 1000), 500u);
  EXPECT_EQ(msUntil(1000, 1000), 0u);
  EXPECT_EQ(msUntil(900, 1000), 0u);
  EXPECT_EQ(msUntil(100, UINT32_MAX - 99), 200u);  // across the millis() wrap
  EXPECT_EQ(msUntil(UINT32_MAX - 99, 100), 0u);
}

TEST(LightSleep, FirstTouchAfterASleepProvesOrDisprovesTouchWake) {
  EXPECT_TRUE(touchWakeProven(false, true, true));    // the INT line woke the chip for it
  EXPECT_FALSE(touchWakeProven(true, false, true));   // the timer found it: INT slept through
  EXPECT_TRUE(touchWakeProven(true, false, false));   // no touch: nothing learned
  EXPECT_FALSE(touchWakeProven(false, true, false));  // a button woke it
}
