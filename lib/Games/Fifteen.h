#pragma once

// A 15 puzzle session: the surface a Fifteen screen drives.
//   newGame(seed)          a random solvable arrangement, never the solved one
//   canSlide(cell) / slide(cell)  the tile and those between it and the gap move one step
//   tile(cell), gap(), moveCount(), isSolved()  for drawing
//   pause() / resume()     play time; a restored game comes back paused
//   serialize() / deserialize()
// Cells are 0..15, row-major; tile 0 is the gap. One slide is one move, however
// many tiles it shifts. No heap.

#include <cstddef>
#include <cstdint>

#include "GameCommon.h"

namespace fifteen {

constexpr int SIDE = 4;
constexpr int CELLS = SIDE * SIDE;
constexpr size_t SAVE_MAX = 96;

// Reachable from the solved order by slides.
bool isSolvable(const uint8_t tiles[CELLS]);

class Game {
 public:
  explicit Game(games::MillisFn millis);

  void newGame(uint32_t seed);
  // A solvable permutation of 0..15; false (game unchanged) otherwise.
  bool newGame(const uint8_t tiles[CELLS]);

  uint8_t tile(const int cell) const { return cell >= 0 && cell < CELLS ? board[cell] : 0; }
  int gap() const { return gapCell; }
  int moveCount() const { return moves; }
  bool isSolved() const { return solved; }
  bool inProgress() const { return !solved; }

  bool canSlide(int cell) const;
  bool slide(int cell);
  // Slides the tile beside the gap one cell in direction (dx, dy): (-1, 0) moves the gap's right neighbour left.
  bool slideToward(int dx, int dy);

  uint32_t elapsedMs() const { return clock.elapsed(now()); }
  void pause();
  void resume();
  bool paused() const { return !clock.isRunning(); }

  size_t serialize(char* out, size_t cap) const;
  // Restores a serialize()d game, paused. False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  void start(const uint8_t tiles[CELLS]);

  games::MillisFn now;
  uint8_t board[CELLS] = {};
  uint8_t gapCell = 0;
  uint16_t moves = 0;
  bool solved = false;
  games::Stopwatch clock;
};

// The best finished game: fewest moves, then the shorter time.
struct Best {
  uint16_t moves = 0;  // 0 until a game is solved
  uint32_t ms = 0;

  // True when the result beats the kept one and replaces it.
  bool record(int moveCount, uint32_t elapsedMs);
  size_t serialize(char* out, size_t cap) const;
  bool deserialize(const char* text, size_t len);
};

}  // namespace fifteen
