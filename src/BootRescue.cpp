#include "BootRescue.h"

#include <BoardConfig.h>

#if FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)
#include <Arduino.h>
#include <Logging.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "network/OtaBootSwitch.h"
#include "util/BootRescuePolicy.h"

namespace bootrescue {

namespace {

// RTC_NOINIT survives panic, watchdog and software resets and deep sleep, and a
// crash loop costs no flash writes. Power loss leaves garbage, rejected by the magic.
RTC_NOINIT_ATTR uint32_t crashCountMagic;
RTC_NOINIT_ATTR uint32_t crashCount;

constexpr unsigned long RELEASE_TIMEOUT_MS = 30000;

void storeCrashCount(const uint32_t count) {
  crashCount = count;
  crashCountMagic = CRASH_COUNT_MAGIC;
}

// X4 Pro Left is GPIO0 (profile "up") and Right is GPIO7 ("down"): plain active-low
// inputs that need neither the touch controller nor a power rail.
bool comboHeld() {
  return digitalRead(BoardConfig::ACTIVE.input.up) == LOW && digitalRead(BoardConfig::ACTIVE.input.down) == LOW;
}

// The bootloader already rolls a PENDING_VERIFY image back if it crashes or resets, and a
// switch made from it would leave its entry for the bootloader to ABORT on the next boot.
bool runningPendingVerify() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  return running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
}

bool comboFired() {
  ComboHold hold;
  for (;;) {
    switch (hold.update(millis(), comboHeld())) {
      case ComboState::Fired:
        return true;
      case ComboState::Released:
        return false;
      case ComboState::Holding:
        delay(10);
        break;
    }
  }
}

const esp_partition_t* usableOtherSlot() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if (!running || !other || other->address == running->address) {
    LOG_ERR("RESCUE", "No other OTA slot");
    return nullptr;
  }
  esp_app_desc_t desc;
  const bool hasApp = esp_ota_get_partition_description(other, &desc) == ESP_OK;
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const bool stateKnown = esp_ota_get_state_partition(other, &state) == ESP_OK;
  if (!slotUsable(hasApp, stateKnown, state)) {
    LOG_ERR("RESCUE", "Slot %s unusable (app=%d stateKnown=%d state=0x%lx)", other->label, hasApp, stateKnown,
            static_cast<unsigned long>(state));
    return nullptr;
  }
  LOG_INF("RESCUE", "Slot %s holds %.32s", other->label, desc.version);
  return other;
}

// ota_boot::switchTo is the otadata write SD updates use. Unlike esp_ota_set_boot_partition
// it skips esp_image_verify, which rejects patched Xteink images (OtaBootSwitch.h). Its new
// entry outranks the running one even when that image is confirmed, and the bootloader
// still verifies the target and falls back to this slot if it does not load.
void restartInto(const esp_partition_t* slot, const char* why) {
  if (!ota_boot::switchTo(slot)) {
    LOG_ERR("RESCUE", "%s: switching to %s failed", why, slot->label);
    return;
  }
  LOG_INF("RESCUE", "%s: restarting into %s", why, slot->label);
  storeCrashCount(0);
  esp_restart();
}

}  // namespace

void runAtBoot() {
  const esp_reset_reason_t reason = esp_reset_reason();
  const uint32_t count = nextCrashCount(crashCountMagic, crashCount, isCrashReset(reason));
  storeCrashCount(count);
  if (count > 0) {
    LOG_ERR("RESCUE", "Crash reset %lu in a row (reason %d)", static_cast<unsigned long>(count),
            static_cast<int>(reason));
  }
  // Rescue only from a confirmed image: this also keeps a just-switched-to image from
  // switching straight back while the combo is still held.
  if (runningPendingVerify()) return;
  pinMode(BoardConfig::ACTIVE.input.up, INPUT_PULLUP);
  pinMode(BoardConfig::ACTIVE.input.down, INPUT_PULLUP);
  delay(1);  // pull-up settling before the first sample
  if (shouldRollBack(count)) {
    if (const esp_partition_t* other = usableOtherSlot()) restartInto(other, "Crash loop");
  }

  if (!comboFired()) return;
  LOG_INF("RESCUE", "Rescue combo held");
  const esp_partition_t* other = usableOtherSlot();
  if (!other) return;
  // A restart under a still-held combo would let the other image switch straight back.
  const unsigned long waitStart = millis();
  while (comboHeld()) {
    if (millis() - waitStart >= RELEASE_TIMEOUT_MS) {
      LOG_ERR("RESCUE", "Combo still held after %lu ms, ignoring it", RELEASE_TIMEOUT_MS);
      return;
    }
    delay(10);
  }
  restartInto(other, "Rescue combo");
}

void clearIfHealthy() {
  static bool cleared = false;
  if (cleared || millis() < HEALTHY_UPTIME_MS) return;
  cleared = true;
  clearCrashCount();
}

void clearCrashCount() { storeCrashCount(0); }

}  // namespace bootrescue

#else

namespace bootrescue {
void runAtBoot() {}
void clearIfHealthy() {}
void clearCrashCount() {}
}  // namespace bootrescue

#endif
