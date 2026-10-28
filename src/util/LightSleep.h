#pragma once

// Light sleep between inputs: once the main loop is idle, the chip sleeps until a
// button, the touch controller or the next deadline wakes it, instead of polling
// in 10 ms slices. Decision logic only, so host tests can drive it; main.cpp
// gathers the state and HalPowerManager::lightSleep() sleeps.

#include <algorithm>
#include <cstdint>

// X4 Pro only: its wake lines (GPIO buttons, the GT911 INT) are known.
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)
#define CROSSPOINT_LIGHT_SLEEP 1
#else
#define CROSSPOINT_LIGHT_SLEEP 0
#endif

namespace light_sleep {

// Activity::idleSleepBudgetMs() when only input can need the loop.
constexpr uint32_t kNoDeadline = UINT32_MAX;
// Shorter sleeps are not worth entering; the loop polls as before.
constexpr uint32_t kMinSleepMs = 20;
// Until a touch is seen to wake the chip, sleeps are no longer than the idle
// poll they replace, so touch is never slower than without light sleep.
constexpr uint32_t kUnprovenTouchWakeMs = 50;
// Backstop for what has no wake line (USB plug-in, the charge state) and for a
// touch the INT line might still miss.
constexpr uint32_t kMaxSleepMs = 2000;
// With the crystal kept up for the frontlight, sleeps stay below the 300 ms
// interrupt watchdog in case a timer it clocks keeps counting through them.
constexpr uint32_t kCrystalSleepMaxMs = 250;

// Milliseconds from nowMs to deadlineMs (millis() values, wrap-safe); 0 once due.
constexpr uint32_t msUntil(const uint32_t deadlineMs, const uint32_t nowMs) {
  return static_cast<int32_t>(deadlineMs - nowMs) > 0 ? deadlineMs - nowMs : 0;
}

struct State {
  bool radioOn = false;       // Wi-Fi or Bluetooth
  bool usbConnected = false;  // charging or a USB host: sleep drops the USB serial link
  bool workPending = false;   // a render queued, the panel refreshing, loop-owned setup
  bool inputHeld = false;     // a button or the screen is down, or a press is still debouncing
  bool frontlightOn = false;
  bool frontlightOnCrystal = false;  // its PWM is clocked from the crystal, which sleep can keep up
  uint32_t idleMs = 0;               // since the last input
  uint32_t autoSleepMs = 0;          // deep-sleep timeout, 0 = never
  uint32_t activityBudgetMs = kNoDeadline;
  bool touchWakeProven = false;
};

// How long the idle loop may sleep; 0 = poll as without light sleep.
constexpr uint32_t sleepMs(const State& s) {
  if (s.radioOn || s.usbConnected || s.workPending || s.inputHeld) return 0;
  // Any other PWM clock stops in sleep and freezes the LEDs fully on or off.
  if (s.frontlightOn && !s.frontlightOnCrystal) return 0;
  uint32_t ms = std::min(s.touchWakeProven ? kMaxSleepMs : kUnprovenTouchWakeMs, s.activityBudgetMs);
  if (s.frontlightOn) ms = std::min(ms, kCrystalSleepMaxMs);
  if (s.autoSleepMs != 0) ms = std::min(ms, s.autoSleepMs > s.idleMs ? s.autoSleepMs - s.idleMs : 0);
  return ms >= kMinSleepMs ? ms : 0;
}

// The first touch after a sleep shows whether the touch INT line woke the chip:
// seen after a wake-line wake, it did; after a timer wake, it slept through it.
constexpr bool touchWakeProven(const bool proven, const bool wokeByLine, const bool touchSeen) {
  return touchSeen ? wokeByLine : proven;
}

}  // namespace light_sleep
