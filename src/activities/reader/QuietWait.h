#pragma once

#include <cstdint>

// Waits for a stretch of loop frames with nothing going on: no input, no closed contact, no queued
// page turn, no render. A busy frame starts the wait over. Safe across the millis() wrap.
class QuietWait {
 public:
  void start(const uint32_t nowMs) { since = nowMs; }
  bool elapsed(const uint32_t nowMs, const bool busy, const uint32_t quietMs) {
    if (busy) since = nowMs;
    return !busy && nowMs - since >= quietMs;
  }

 private:
  uint32_t since = 0;
};
