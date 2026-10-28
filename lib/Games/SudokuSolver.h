#pragma once

// Sudoku rules, grading and generation. Ported from ma-r-s/crossplay
// (src/apps_local/sudoku/SudokuCore, MIT, (c) 2026 Mario Ruiz).
//
// Difficulty is the hardest technique a puzzle requires: solve() always applies
// the cheapest technique that fires, so the hardest one it used is a
// measurement. A puzzle the ladder finishes was forced at every step, so it has
// exactly one solution; countSolutions() checks that independently.

#include <cstdint>

namespace sudoku {

constexpr int SIZE = 9;
constexpr int CELLS = SIZE * SIZE;
constexpr int BOX = 3;
constexpr int UNITS = 27;  // rows 0-8, columns 9-17, boxes 18-26

// Bit 0 is the digit 1.
using Mask = uint16_t;
constexpr Mask ALL_DIGITS = 0x1FF;

constexpr Mask bitFor(const int digit) { return static_cast<Mask>(1u << (digit - 1)); }
inline int popcount(const Mask mask) { return __builtin_popcount(mask); }
// Lowest digit in `mask`, or 0.
inline int lowestDigit(const Mask mask) { return mask == 0 ? 0 : __builtin_ctz(mask) + 1; }

constexpr int rowOf(const int cell) { return cell / SIZE; }
constexpr int columnOf(const int cell) { return cell % SIZE; }
constexpr int boxOf(const int cell) { return (rowOf(cell) / BOX) * BOX + columnOf(cell) / BOX; }
constexpr int cellAt(const int row, const int column) { return row * SIZE + column; }
constexpr int unitCell(const int unit, const int slot) {
  if (unit < SIZE) return cellAt(unit, slot);
  if (unit < 2 * SIZE) return cellAt(slot, unit - SIZE);
  const int box = unit - 2 * SIZE;
  return cellAt((box / BOX) * BOX + slot / BOX, (box % BOX) * BOX + slot % BOX);
}
constexpr bool arePeers(const int a, const int b) {
  return a != b && (rowOf(a) == rowOf(b) || columnOf(a) == columnOf(b) || boxOf(a) == boxOf(b));
}

constexpr int PEERS = 20;
struct PeerTable {
  uint8_t peer[CELLS][PEERS];
};
constexpr PeerTable makePeerTable() {
  PeerTable table{};
  for (int cell = 0; cell < CELLS; ++cell) {
    int found = 0;
    for (int other = 0; other < CELLS; ++other) {
      if (arePeers(cell, other)) table.peer[cell][found++] = static_cast<uint8_t>(other);
    }
  }
  return table;
}
// The 20 peers of every cell, built at compile time (1620 bytes of flash).
inline constexpr PeerTable PEER = makePeerTable();

// In difficulty order; the order is the scale.
enum class Technique : uint8_t {
  None = 0,
  NakedSingle,
  HiddenSingle,
  LockedCandidates,
  NakedPair,
  HiddenPair,
  NakedTriple,
  HiddenTriple,
  XWing,
  XYWing,
  Swordfish,
};

enum class Level : uint8_t { Easy = 0, Medium, Hard, Expert };
constexpr int LEVEL_COUNT = 4;

// The hardest technique each level may need: singles; + locked candidates;
// + pairs; + triples, X-wing, XY-wing, swordfish.
constexpr Technique ceilingFor(const Level level) {
  switch (level) {
    case Level::Easy:
      return Technique::HiddenSingle;
    case Level::Medium:
      return Technique::LockedCandidates;
    case Level::Hard:
      return Technique::HiddenPair;
    case Level::Expert:
      return Technique::Swordfish;
  }
  return Technique::HiddenSingle;
}

constexpr Level levelOf(const Technique t) {
  if (t <= Technique::HiddenSingle) return Level::Easy;
  if (t == Technique::LockedCandidates) return Level::Medium;
  if (t <= Technique::HiddenPair) return Level::Hard;
  return Level::Expert;
}

// candidate[cell] is 0 wherever value[cell] is set.
struct Grid {
  uint8_t value[CELLS];
  Mask candidate[CELLS];
};

struct SolveReport {
  bool solved = false;
  bool broken = false;  // the position contradicts itself
  Technique hardest = Technique::None;
  uint16_t steps = 0;
};

// Places `digit` and strikes it from the peers; false on a contradiction.
bool assign(Grid& grid, int cell, int digit);
// Position from clues (0 = empty); false when the clues clash.
bool load(Grid& grid, const uint8_t given[CELLS]);
// Drives the position with techniques up to `ceiling`, cheapest first.
SolveReport solve(Grid& grid, Technique ceiling);
// Grades a puzzle: hardest technique needed, or None when the ladder cannot finish it.
Technique grade(const uint8_t given[CELLS]);

// Plain backtracking, independent of the ladder. Counts up to `limit` solutions
// and copies the first into `solution` when it is not null.
int countSolutions(const uint8_t given[CELLS], int limit, uint8_t* solution);

// Generator scratch (~0.9 KB), owned by the caller.
struct Workspace {
  uint8_t order[CELLS][SIZE];
  uint8_t next[CELLS];
  uint8_t value[CELLS];
  Grid grid;
};

struct Puzzle {
  uint8_t given[CELLS] = {};
  uint8_t solution[CELLS] = {};
  Technique hardest = Technique::None;
  Level level = Level::Easy;
  uint8_t clues = 0;
};

// Random full grid; the rng state advances.
bool fillComplete(uint8_t solution[CELLS], Workspace& work, uint32_t& rng);
// Removes 180-degree symmetric clue pairs while the ladder still finishes the
// grid, then grades the result. The level is an output.
bool carve(Puzzle& out, const uint8_t solution[CELLS], Workspace& work, uint32_t& rng);
// fillComplete + carve until a puzzle grades as `level`, at most `attempts` tries.
bool generate(Puzzle& out, Level level, Workspace& work, uint32_t& rng, int attempts);

struct Hint {
  int cell = -1;
  int digit = 0;
  Technique technique = Technique::None;
};

// The next placement a solver would make from `values` (clues plus correct
// entries); cell -1 when there is none.
Hint nextHint(const uint8_t values[CELLS], Technique ceiling);

}  // namespace sudoku
