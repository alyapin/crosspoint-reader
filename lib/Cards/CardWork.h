#pragma once

#include <Arduino.h>

#include <cstdint>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_task_wdt.h>
#endif

namespace cards {

// Long passes (a 4 MB deck) keep the task watchdog and the idle task fed.
inline void serviceWork(uint32_t& units) {
  if ((++units & 0x1Fu) != 0) return;
#if defined(ARDUINO_ARCH_ESP32)
  if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
#endif
  delay(1);
}

}  // namespace cards
