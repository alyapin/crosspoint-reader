#pragma once

#include <esp_ota_ops.h>
#include <esp_system.h>

#include <cstdint>

// Decisions behind BootRescue, kept free of hardware access for host tests.
namespace bootrescue {

constexpr uint32_t COMBO_HOLD_MS = 3000;
// A contact gap shorter than this does not end the hold.
constexpr uint32_t COMBO_GAP_MS = 50;
// Left is GPIO0, the ROM's download-mode strap: held through Reset or a power-on it stops the
// chip before this firmware runs. So after those boots the combo may still begin this long after
// the first sample, once Reset is released.
constexpr uint32_t COMBO_START_WINDOW_MS = 1500;
constexpr uint32_t CRASH_BOOTS_BEFORE_ROLLBACK = 3;
constexpr uint32_t HEALTHY_UPTIME_MS = 120000;
constexpr uint32_t CRASH_COUNT_MAGIC = 0x52534355;

enum class ComboState : uint8_t { Holding, Fired, Released };

// Fed one sample per poll from the first instant of setup(). The combo must begin within
// startWindowMs of the first sample; with no window it must already be held then, so an
// ordinary wake ends after one read.
class ComboHold {
 public:
  explicit ComboHold(uint32_t startWindowMs = 0) : startWindowMs(startWindowMs) {}

  ComboState update(uint32_t nowMs, bool held) {
    if (state != ComboState::Holding) return state;
    if (!started) {
      if (!sampled) {
        sampled = true;
        firstMs = nowMs;
      }
      if (!held) {
        if (nowMs - firstMs >= startWindowMs) state = ComboState::Released;
        return state;
      }
      started = true;
      startMs = nowMs;
      lastHeldMs = nowMs;
    }
    if (!held) {
      if (nowMs - lastHeldMs >= COMBO_GAP_MS) state = ComboState::Released;
      return state;
    }
    lastHeldMs = nowMs;
    if (nowMs - startMs >= COMBO_HOLD_MS) state = ComboState::Fired;
    return state;
  }

 private:
  uint32_t startWindowMs;
  ComboState state = ComboState::Holding;
  bool sampled = false;
  bool started = false;
  uint32_t firstMs = 0;
  uint32_t startMs = 0;
  uint32_t lastHeldMs = 0;
};

// A deep-sleep wake stays instant, and so does the software restart behind silentRestart(): the
// firmware's own heap-defrag reboot, with nobody at the buttons. Any other boot may follow Reset or Power.
constexpr uint32_t comboStartWindowMs(esp_reset_reason_t reason, bool silentRestart) {
  return reason == ESP_RST_DEEPSLEEP || (silentRestart && reason == ESP_RST_SW) ? 0 : COMBO_START_WINDOW_MS;
}

// Panics and watchdog resets. Brownout, power-on, deep-sleep wake and software restarts are not crashes.
constexpr bool isCrashReset(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_CPU_LOCKUP:
      return true;
    default:
      return false;
  }
}

// Consecutive crash boots including this one, saturating at the rollback threshold. A record
// without the magic, or with an impossible count, is uninitialized RTC memory and counts as zero.
constexpr uint32_t nextCrashCount(uint32_t magic, uint32_t storedCount, bool crashed) {
  if (!crashed) return 0;
  const uint32_t previous = magic == CRASH_COUNT_MAGIC && storedCount <= CRASH_BOOTS_BEFORE_ROLLBACK ? storedCount : 0;
  return previous < CRASH_BOOTS_BEFORE_ROLLBACK ? previous + 1 : CRASH_BOOTS_BEFORE_ROLLBACK;
}

constexpr bool shouldRollBack(uint32_t crashCount) { return crashCount >= CRASH_BOOTS_BEFORE_ROLLBACK; }

// The other slot is worth booting when it holds an app image the bootloader has not recorded as
// failed. With no otadata entry (stateKnown false) nothing was recorded against it either; the
// bootloader still verifies the image and falls back to this slot if it does not load.
constexpr bool slotUsable(bool hasAppDescription, bool stateKnown, esp_ota_img_states_t state) {
  // PENDING_VERIFY there means that image never proved itself; switching to it would also
  // overwrite this slot's rollback state, so leave it alone.
  return hasAppDescription && !(stateKnown && (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED ||
                                               state == ESP_OTA_IMG_PENDING_VERIFY));
}

}  // namespace bootrescue
