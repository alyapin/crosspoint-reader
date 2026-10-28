#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>

#include "util/BootRescuePolicy.h"

using namespace bootrescue;

TEST(BootRescueCombo, NotHeldAtFirstSampleReleasesAtOnce) {
  ComboHold hold;
  EXPECT_EQ(hold.update(500, false), ComboState::Released);
  // Pressing later in the same boot does not start a hold.
  EXPECT_EQ(hold.update(510, true), ComboState::Released);
  EXPECT_EQ(hold.update(5000, true), ComboState::Released);
}

TEST(BootRescueCombo, FiresAfterContinuousHold) {
  ComboHold hold;
  for (uint32_t t = 500; t < 500 + COMBO_HOLD_MS; t += 10) {
    ASSERT_EQ(hold.update(t, true), ComboState::Holding) << t;
  }
  EXPECT_EQ(hold.update(500 + COMBO_HOLD_MS, true), ComboState::Fired);
  EXPECT_EQ(hold.update(500 + COMBO_HOLD_MS + 10, false), ComboState::Fired);
}

TEST(BootRescueCombo, ReleaseBeforeHoldTimeAborts) {
  ComboHold hold;
  EXPECT_EQ(hold.update(0, true), ComboState::Holding);
  EXPECT_EQ(hold.update(2000, true), ComboState::Holding);
  EXPECT_EQ(hold.update(2010, false), ComboState::Holding);
  EXPECT_EQ(hold.update(2000 + COMBO_GAP_MS - 1, false), ComboState::Holding);
  EXPECT_EQ(hold.update(2000 + COMBO_GAP_MS, false), ComboState::Released);
  EXPECT_EQ(hold.update(COMBO_HOLD_MS, true), ComboState::Released);
}

TEST(BootRescueCombo, ShortContactGapKeepsTheHold) {
  ComboHold hold;
  EXPECT_EQ(hold.update(0, true), ComboState::Holding);
  EXPECT_EQ(hold.update(1000, true), ComboState::Holding);
  EXPECT_EQ(hold.update(1010, false), ComboState::Holding);
  EXPECT_EQ(hold.update(1040, false), ComboState::Holding);
  EXPECT_EQ(hold.update(1050, true), ComboState::Holding);
  EXPECT_EQ(hold.update(COMBO_HOLD_MS, true), ComboState::Fired);
}

TEST(BootRescueCombo, FiresOnlyWhileHeld) {
  ComboHold hold;
  EXPECT_EQ(hold.update(0, true), ComboState::Holding);
  EXPECT_EQ(hold.update(COMBO_HOLD_MS - 10, true), ComboState::Holding);
  // Inside a contact gap at the deadline: wait for the contact to return.
  EXPECT_EQ(hold.update(COMBO_HOLD_MS + 10, false), ComboState::Holding);
  EXPECT_EQ(hold.update(COMBO_HOLD_MS + 20, true), ComboState::Fired);
}

TEST(BootRescueCombo, SurvivesMillisWrap) {
  ComboHold hold;
  const uint32_t start = UINT32_MAX - 1000;
  EXPECT_EQ(hold.update(start, true), ComboState::Holding);
  EXPECT_EQ(hold.update(start + COMBO_HOLD_MS - 1, true), ComboState::Holding);
  EXPECT_EQ(hold.update(start + COMBO_HOLD_MS, true), ComboState::Fired);
}

TEST(BootRescueResetReason, OnlyPanicsAndWatchdogsAreCrashes) {
  EXPECT_TRUE(isCrashReset(ESP_RST_PANIC));
  EXPECT_TRUE(isCrashReset(ESP_RST_INT_WDT));
  EXPECT_TRUE(isCrashReset(ESP_RST_TASK_WDT));
  EXPECT_TRUE(isCrashReset(ESP_RST_WDT));
  EXPECT_TRUE(isCrashReset(ESP_RST_CPU_LOCKUP));

  EXPECT_FALSE(isCrashReset(ESP_RST_POWERON));
  EXPECT_FALSE(isCrashReset(ESP_RST_SW));
  EXPECT_FALSE(isCrashReset(ESP_RST_DEEPSLEEP));
  EXPECT_FALSE(isCrashReset(ESP_RST_BROWNOUT));
  EXPECT_FALSE(isCrashReset(ESP_RST_UNKNOWN));
  EXPECT_FALSE(isCrashReset(ESP_RST_EXT));
  EXPECT_FALSE(isCrashReset(ESP_RST_SDIO));
  EXPECT_FALSE(isCrashReset(ESP_RST_USB));
  EXPECT_FALSE(isCrashReset(ESP_RST_JTAG));
  EXPECT_FALSE(isCrashReset(ESP_RST_EFUSE));
  EXPECT_FALSE(isCrashReset(ESP_RST_PWR_GLITCH));
}

TEST(BootRescueCrashCount, CountsConsecutiveCrashesAndSaturates) {
  EXPECT_EQ(nextCrashCount(CRASH_COUNT_MAGIC, 0, true), 1u);
  EXPECT_EQ(nextCrashCount(CRASH_COUNT_MAGIC, 1, true), 2u);
  EXPECT_EQ(nextCrashCount(CRASH_COUNT_MAGIC, 2, true), 3u);
  EXPECT_EQ(nextCrashCount(CRASH_COUNT_MAGIC, 3, true), 3u);
}

TEST(BootRescueCrashCount, CleanBootClears) {
  EXPECT_EQ(nextCrashCount(CRASH_COUNT_MAGIC, 2, false), 0u);
  EXPECT_EQ(nextCrashCount(CRASH_COUNT_MAGIC, 3, false), 0u);
  EXPECT_EQ(nextCrashCount(0xDEADBEEF, 2, false), 0u);
}

TEST(BootRescueCrashCount, UninitializedRecordCountsAsZero) {
  EXPECT_EQ(nextCrashCount(0xDEADBEEF, 2, true), 1u);
  EXPECT_EQ(nextCrashCount(CRASH_COUNT_MAGIC, CRASH_BOOTS_BEFORE_ROLLBACK + 1, true), 1u);
  EXPECT_EQ(nextCrashCount(CRASH_COUNT_MAGIC, UINT32_MAX, true), 1u);
}

TEST(BootRescueCrashCount, RollsBackOnThirdConsecutiveCrashBoot) {
  EXPECT_FALSE(shouldRollBack(0));
  EXPECT_FALSE(shouldRollBack(CRASH_BOOTS_BEFORE_ROLLBACK - 1));
  EXPECT_TRUE(shouldRollBack(CRASH_BOOTS_BEFORE_ROLLBACK));

  // Replays boots against the RTC record the way runAtBoot() stores it.
  auto replay = [](std::initializer_list<esp_reset_reason_t> boots) {
    uint32_t magic = 0x12345678;  // cold-boot garbage
    uint32_t count = 77;
    bool rolledBack = false;
    for (const esp_reset_reason_t reason : boots) {
      count = nextCrashCount(magic, count, isCrashReset(reason));
      magic = CRASH_COUNT_MAGIC;
      rolledBack = shouldRollBack(count);
    }
    return rolledBack;
  };
  EXPECT_TRUE(replay({ESP_RST_POWERON, ESP_RST_PANIC, ESP_RST_TASK_WDT, ESP_RST_INT_WDT}));
  EXPECT_FALSE(replay({ESP_RST_POWERON, ESP_RST_PANIC, ESP_RST_PANIC}));
  EXPECT_FALSE(replay({ESP_RST_PANIC, ESP_RST_PANIC, ESP_RST_DEEPSLEEP, ESP_RST_PANIC, ESP_RST_PANIC}));
  EXPECT_FALSE(replay({ESP_RST_PANIC, ESP_RST_PANIC, ESP_RST_SW, ESP_RST_PANIC}));
  EXPECT_FALSE(replay({ESP_RST_PANIC, ESP_RST_BROWNOUT, ESP_RST_PANIC, ESP_RST_PANIC}));
}

TEST(BootRescueSlot, UsableUnlessEmptyOrMarkedFailed) {
  EXPECT_TRUE(slotUsable(true, true, ESP_OTA_IMG_NEW));
  EXPECT_FALSE(slotUsable(true, true, ESP_OTA_IMG_PENDING_VERIFY));
  EXPECT_TRUE(slotUsable(true, true, ESP_OTA_IMG_VALID));
  EXPECT_TRUE(slotUsable(true, true, ESP_OTA_IMG_UNDEFINED));
  EXPECT_FALSE(slotUsable(true, true, ESP_OTA_IMG_INVALID));
  EXPECT_FALSE(slotUsable(true, true, ESP_OTA_IMG_ABORTED));
  // No app image header: an erased or never-written slot.
  EXPECT_FALSE(slotUsable(false, true, ESP_OTA_IMG_VALID));
  EXPECT_FALSE(slotUsable(false, true, ESP_OTA_IMG_NEW));
}

TEST(BootRescueSlot, NoOtadataEntryStillUsable) {
  // The state value is meaningless when the lookup failed.
  EXPECT_TRUE(slotUsable(true, false, ESP_OTA_IMG_ABORTED));
  EXPECT_FALSE(slotUsable(false, false, ESP_OTA_IMG_UNDEFINED));
}
