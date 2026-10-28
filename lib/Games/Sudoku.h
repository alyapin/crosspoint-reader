#pragma once

// A Sudoku session: the surface a Sudoku screen drives.
//   newGame(level, seed)        bank puzzle, reshaped by the seed (bounded time)
//   place / toggleNote / erase  player input; false when nothing changed
//   undo(), revealHint()
//   value / notes / visibleNotes / hasConflict / isLocked  for drawing
//   pause() / resume()          play time; a restored game comes back paused
//   serialize() / deserialize() save text for the SD layer
// Cells are 0..80, row-major. About 1.3 KB; no heap.

#include <cstddef>
#include <cstdint>

#include "GameCommon.h"
#include "SudokuSolver.h"

namespace sudoku {

constexpr int UNDO_DEPTH = 128;
// Upper bound of serialize() output, terminator included.
constexpr size_t SAVE_MAX = 160 + 3 * CELLS + 3 * CELLS + UNDO_DEPTH * 6;

// Puzzles in the flash bank per level.
int bankSize();
// Bank puzzle `index` of `level` reshaped by `seed` (digit relabelling, transposition
// and symmetry-keeping row/column swaps), which keeps its uniqueness and grade.
void bankPuzzle(Level level, int index, uint32_t seed, uint8_t given[CELLS]);

class Game {
 public:
  explicit Game(games::MillisFn millis);

  void newGame(Level level, uint32_t seed);
  // Any puzzle with exactly one solution; false (game unchanged) otherwise.
  bool newGame(const uint8_t given[CELLS], Level level, uint32_t seed);

  Level level() const { return lvl; }
  uint32_t seed() const { return seedValue; }

  // 0 when empty.
  uint8_t value(const int cell) const { return valid(cell) ? values[cell] : 0; }
  bool isGiven(const int cell) const { return valid(cell) && givens[cell] != 0; }
  bool isHinted(const int cell) const { return valid(cell) && hinted[cell]; }
  // Givens and revealed cells cannot be edited.
  bool isLocked(const int cell) const { return isGiven(cell) || isHinted(cell); }
  Mask notes(const int cell) const { return valid(cell) ? marks[cell] : 0; }
  // Pencil marks minus digits a peer already holds; stored marks stay untouched.
  Mask visibleNotes(int cell) const;
  // The cell's digit repeats in its row, column or box.
  bool hasConflict(int cell) const;
  // Filled with the wrong digit (for an optional mistakes display).
  bool isWrong(const int cell) const { return valid(cell) && values[cell] != 0 && values[cell] != answer[cell]; }
  // How many of `digit` are on the board, to dim a finished key on the pad.
  int digitCount(int digit) const;

  // `digit` 0 clears. Clears the cell's notes too.
  bool place(int cell, int digit);
  bool toggleNote(int cell, int digit);
  // Clears the digit, or the notes of an empty cell.
  bool erase(int cell);
  bool canUndo() const { return undoCount > 0 && !solved; }
  bool undo();
  // Reveals the answer in `preferred` when it is open or wrong; otherwise in the
  // first wrong cell, else the cell a solver would fill next. Returns the cell or -1.
  int revealHint(int preferred = -1);
  int hintsUsed() const { return hints; }

  bool isSolved() const { return solved; }
  bool inProgress() const { return !solved; }

  uint32_t elapsedMs() const { return clock.elapsed(now()); }
  void pause();
  void resume();
  bool paused() const { return !clock.isRunning(); }

  // Writes the save text; returns its length, or 0 if `cap` is too small.
  size_t serialize(char* out, size_t cap) const;
  // Restores a serialize()d game, paused. False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  struct Step {
    uint8_t cell;
    uint8_t value;  // before the action
    uint8_t hinted;
    Mask notes;
  };

  static bool valid(const int cell) { return cell >= 0 && cell < CELLS; }
  void start(const uint8_t given[CELLS], const uint8_t solution[CELLS], Level level, uint32_t seed);
  void record(int cell);
  void settle();

  games::MillisFn now;
  Level lvl = Level::Easy;
  uint32_t seedValue = 0;
  uint8_t givens[CELLS] = {};
  uint8_t answer[CELLS] = {};
  uint8_t values[CELLS] = {};
  bool hinted[CELLS] = {};
  Mask marks[CELLS] = {};
  Step undoRing[UNDO_DEPTH] = {};
  uint16_t undoTop = 0;  // next write slot
  uint16_t undoCount = 0;
  uint16_t hints = 0;
  bool solved = false;
  games::Stopwatch clock;
};

}  // namespace sudoku
