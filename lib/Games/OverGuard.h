#pragma once

#include <cstdint>

namespace games {

// A game that has just ended ignores its new-game control for a moment, so a doubled press on the control
// that ended it cannot skip the result. Fed the game's over state on every loop pass; the turn to over
// starts the hold.
class OverGuard {
 public:
  // Longer than a half refresh (1720 ms): the hold starts when the loop sees the end, about when that refresh starts.
  static constexpr uint32_t kHoldMs = 2500;

  // A game shown as it already is (restored, opened): no hold.
  void reset(const bool over) {
    wasOver = over;
    ended = false;
  }

  void track(const bool over, const uint32_t nowMs) {
    if (over && !wasOver) {
      ended = true;
      endedAt = nowMs;
    }
    wasOver = over;
  }

  // True while the new-game control should ignore a press; tracks first, so an end in this pass counts.
  bool holds(const bool over, const uint32_t nowMs) {
    track(over, nowMs);
    return over && ended && nowMs - endedAt < kHoldMs;
  }

 private:
  uint32_t endedAt = 0;
  bool wasOver = false;
  bool ended = false;
};

}  // namespace games
