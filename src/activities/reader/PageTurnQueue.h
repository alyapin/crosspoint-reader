#pragma once

#include <cstdint>

// Page turns pressed while the reader cannot turn yet: during a panel refresh, a chapter layout, or
// right after a turn. Each press is one page; a press the other way takes one back.
class PageTurnQueue {
 public:
  // More presses than this in one wait are not reading.
  static constexpr int8_t kMaxTurns = 16;

  void push(const bool forward) {
    if (forward && turns < kMaxTurns) turns++;
    if (!forward && turns > -kMaxTurns) turns--;
  }
  bool pending() const { return turns != 0; }
  void clear() { turns = 0; }
  // The next turn: 1 forward, -1 back, 0 when none is queued.
  int8_t pop() {
    if (turns == 0) return 0;
    const int8_t step = turns > 0 ? 1 : -1;
    turns = static_cast<int8_t>(turns - step);
    return step;
  }

 private:
  int8_t turns = 0;
};
