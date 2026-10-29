#pragma once

// A 2048 session: the surface the 2048 screen drives.
//   newGame(seed)           two tiles on an empty board
//   move(dir)               slides and merges, then spawns a 2 (or a 4 in ten)
//   undo()                  one step back, the spawned tile included
//   exponent(cell), score(), isOver(), justWon()  for drawing
//   serialize() / deserialize()
// Cells hold exponents: 0 empty, 1 = 2, 11 = 2048. No heap.

#include <cstddef>
#include <cstdint>

#include "GameCommon.h"

namespace g2048 {

constexpr int SIZE = 4;
constexpr int CELLS = SIZE * SIZE;
constexpr uint8_t WIN_EXPONENT = 11;
constexpr uint8_t MAX_EXPONENT = 20;
constexpr size_t SAVE_MAX = 256;

enum class Dir : uint8_t { Left, Right, Up, Down };

class Game {
 public:
  Game() : rng(0), undoRng(0) {}

  void newGame(uint32_t seed);

  uint8_t exponent(const int cell) const { return board[cell]; }
  uint32_t score() const { return points; }
  // False (no change, nothing spawned) when no tile can slide that way.
  bool move(Dir dir);
  bool canUndo() const { return hasUndo; }
  bool undo();
  bool canMove() const;
  bool isOver() const { return !canMove(); }
  bool inProgress() const;
  // The last move made the first 2048.
  bool justWon() const { return winMove; }

  size_t serialize(char* out, size_t cap) const;
  // False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  bool hasWinTile() const;
  void spawn();

  uint8_t board[CELLS] = {};
  uint8_t undoBoard[CELLS] = {};
  uint32_t points = 0;
  uint32_t undoPoints = 0;
  uint32_t seedValue = 0;
  uint32_t draws = 0;  // rng steps since the seed, so a save replays the same spawns
  uint32_t undoDraws = 0;
  games::Rng rng;
  games::Rng undoRng;
  bool hasUndo = false;
  bool winMove = false;
};

}  // namespace g2048
