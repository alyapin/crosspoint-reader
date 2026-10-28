#pragma once

// Two-sided game clock with Fischer increment. Times are millis() values;
// unsigned subtraction keeps it correct across the 49-day wrap.

#include <cstdint>

#include "ChessBoard.h"

namespace chess {

struct TimeControl {
  uint32_t baseMs = 0;  // 0 = unlimited
  uint32_t incrementMs = 0;

  bool unlimited() const { return baseMs == 0; }
};

constexpr TimeControl TIME_UNLIMITED{0, 0};
constexpr TimeControl TIME_5_3{5 * 60 * 1000, 3 * 1000};
constexpr TimeControl TIME_10_5{10 * 60 * 1000, 5 * 1000};
constexpr TimeControl TIME_15_10{15 * 60 * 1000, 10 * 1000};

// Custom controls are clamped to these.
constexpr uint32_t MAX_BASE_MS = 3 * 60 * 60 * 1000;
constexpr uint32_t MAX_INCREMENT_MS = 60 * 1000;

// Remaining time never grows past base + this many increments. Undo keeps earned
// increments, so without a ceiling move/undo cycles could bank time forever; the
// same ceiling bounds what a save may restore.
constexpr uint32_t MAX_INCREMENTS = 512;
uint32_t maxRemainingMs(const TimeControl& control);

class Clock {
 public:
  // Starts `toMove`'s clock at `nowMs` with full base time on both sides.
  void reset(const TimeControl& control, Color toMove, uint32_t nowMs);
  // Restores saved times; the clock comes back paused.
  void restore(const TimeControl& control, Color toMove, uint32_t whiteMs, uint32_t blackMs);

  void pause(uint32_t nowMs);
  void resume(uint32_t nowMs);
  // Freezes both sides for good (game over).
  void stop(uint32_t nowMs);
  bool paused() const { return isPaused; }

  // Charges the mover, adds the increment, and starts the opponent's clock.
  void moveMade(Color mover, uint32_t nowMs);
  // Hands the clock to `toMove` without increment and reopens a stopped clock (after an undo).
  void setTurn(Color toMove, uint32_t nowMs);

  // Remaining time including the running side's current think. 0 for unlimited.
  uint32_t remainingMs(Color side, uint32_t nowMs) const;
  bool flagged(Color side, uint32_t nowMs) const;
  Color turn() const { return toMove; }
  const TimeControl& control() const { return tc; }

 private:
  void charge(uint32_t nowMs);
  bool running() const { return !tc.unlimited() && !isPaused && !isStopped; }

  TimeControl tc;
  uint32_t remaining[2] = {0, 0};
  Color toMove = WHITE;
  uint32_t since = 0;  // when the running side's current charge started
  bool isPaused = false;
  bool isStopped = false;
};

}  // namespace chess
