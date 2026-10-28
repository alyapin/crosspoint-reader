#pragma once

// Pieces shared by the Sudoku, Minesweeper and Klondike cores: the clock
// source, a play-time stopwatch and the seeded generator. No Arduino, no heap.

#include <cstdint>

namespace games {

using MillisFn = uint32_t (*)();

// xorshift32: identical on host and device, so a seed always deals the same game.
class Rng {
 public:
  explicit Rng(const uint32_t seed) : state(seed != 0 ? seed : 0x9E3779B9u) {}
  uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  // Modulo bias is below 1e-7 for the n <= 480 used here.
  uint32_t below(const uint32_t n) { return next() % n; }

 private:
  uint32_t state;
};

// Accumulated play time; counts only while started.
class Stopwatch {
 public:
  void reset(const uint32_t elapsedMs = 0) {
    base = elapsedMs;
    running = false;
  }
  void start(const uint32_t now) {
    if (running) return;
    since = now;
    running = true;
  }
  void stop(const uint32_t now) {
    if (!running) return;
    base = elapsed(now);
    running = false;
  }
  uint32_t elapsed(const uint32_t now) const { return running ? base + (now - since) : base; }
  bool isRunning() const { return running; }

 private:
  uint32_t base = 0;
  uint32_t since = 0;
  bool running = false;
};

}  // namespace games
