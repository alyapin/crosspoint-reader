#include "ChessClock.h"

namespace chess {

uint32_t maxRemainingMs(const TimeControl& control) {
  const uint64_t ceiling = control.baseMs + static_cast<uint64_t>(MAX_INCREMENTS) * control.incrementMs;
  return ceiling > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(ceiling);
}

void Clock::reset(const TimeControl& control, const Color side, const uint32_t nowMs) {
  tc = control;
  remaining[WHITE] = remaining[BLACK] = control.baseMs;
  toMove = side;
  since = nowMs;
  isPaused = false;
  isStopped = false;
}

void Clock::restore(const TimeControl& control, const Color side, const uint32_t whiteMs, const uint32_t blackMs) {
  tc = control;
  remaining[WHITE] = whiteMs;
  remaining[BLACK] = blackMs;
  toMove = side;
  since = 0;
  isPaused = true;
  isStopped = false;
}

void Clock::charge(const uint32_t nowMs) {
  if (!running()) return;
  const uint32_t elapsed = nowMs - since;
  remaining[toMove] = elapsed >= remaining[toMove] ? 0 : remaining[toMove] - elapsed;
  since = nowMs;
}

void Clock::pause(const uint32_t nowMs) {
  charge(nowMs);
  isPaused = true;
}

void Clock::resume(const uint32_t nowMs) {
  if (!isPaused) return;
  isPaused = false;
  since = nowMs;
}

void Clock::stop(const uint32_t nowMs) {
  charge(nowMs);
  isStopped = true;
}

void Clock::moveMade(const Color mover, const uint32_t nowMs) {
  charge(nowMs);
  if (!tc.unlimited() && remaining[mover] > 0) {
    const uint32_t ceiling = maxRemainingMs(tc);
    const uint32_t room = remaining[mover] < ceiling ? ceiling - remaining[mover] : 0;
    remaining[mover] += tc.incrementMs < room ? tc.incrementMs : room;
  }
  toMove = opposite(mover);
  since = nowMs;
}

void Clock::setTurn(const Color side, const uint32_t nowMs) {
  charge(nowMs);
  isStopped = false;
  toMove = side;
  since = nowMs;
}

uint32_t Clock::remainingMs(const Color side, const uint32_t nowMs) const {
  if (tc.unlimited()) return 0;
  if (side != toMove || !running()) return remaining[side];
  const uint32_t elapsed = nowMs - since;
  return elapsed >= remaining[side] ? 0 : remaining[side] - elapsed;
}

bool Clock::flagged(const Color side, const uint32_t nowMs) const {
  return !tc.unlimited() && remainingMs(side, nowMs) == 0;
}

}  // namespace chess
