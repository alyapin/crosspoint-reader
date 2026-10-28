// Ported from ma-r-s/crossplay src/apps_local/sudoku/SudokuCore.cpp (MIT,
// (c) 2026 Mario Ruiz). countSolutions() is new and iterative.

#include "SudokuSolver.h"

#include <cstring>

namespace sudoku {
namespace {

// Every technique removes a candidate or places a digit, so the ladder ends by
// itself; this only bounds what a corrupt position can cost.
constexpr int STEP_BUDGET = 4096;
// Backtracking probes allowed for one full-grid fill.
constexpr uint32_t FILL_BUDGET = 200000;

int lowestIndex(const Mask mask) { return mask == 0 ? -1 : __builtin_ctz(mask); }

uint32_t nextRandom(uint32_t& state) {
  if (state == 0) state = 0x9E3779B9u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// Slots of `unit` that could still hold `digit`.
Mask positionsOf(const Grid& grid, const int unit, const int digit) {
  Mask positions = 0;
  for (int slot = 0; slot < SIZE; ++slot) {
    const int cell = unitCell(unit, slot);
    if (grid.value[cell] == 0 && (grid.candidate[cell] & bitFor(digit))) {
      positions = static_cast<Mask>(positions | (1u << slot));
    }
  }
  return positions;
}

bool digitPlacedIn(const Grid& grid, const int unit, const int digit) {
  for (int slot = 0; slot < SIZE; ++slot) {
    if (grid.value[unitCell(unit, slot)] == digit) return true;
  }
  return false;
}

// Strikes `bits` from a cell; true when something went.
bool eliminate(Grid& grid, const int cell, const Mask bits, bool& broken) {
  if (grid.value[cell] != 0) return false;
  const Mask before = grid.candidate[cell];
  const Mask after = static_cast<Mask>(before & ~bits);
  if (after == before) return false;
  grid.candidate[cell] = after;
  if (after == 0) broken = true;
  return true;
}

// Next k-combination of [0, n) in place; `index` starts as {0, 1, ... k-1}.
bool nextCombination(int* index, const int k, const int n) {
  int i = k - 1;
  while (i >= 0 && index[i] == n - k + i) --i;
  if (i < 0) return false;
  ++index[i];
  for (int j = i + 1; j < k; ++j) index[j] = index[j - 1] + 1;
  return true;
}

bool findNakedSingle(const Grid& grid, int& cell, int& digit) {
  for (int c = 0; c < CELLS; ++c) {
    if (grid.value[c] != 0 || popcount(grid.candidate[c]) != 1) continue;
    cell = c;
    digit = lowestDigit(grid.candidate[c]);
    return true;
  }
  return false;
}

bool findHiddenSingle(const Grid& grid, int& cell, int& digit) {
  for (int unit = 0; unit < UNITS; ++unit) {
    for (int d = 1; d <= SIZE; ++d) {
      if (digitPlacedIn(grid, unit, d)) continue;
      const Mask positions = positionsOf(grid, unit, d);
      if (popcount(positions) != 1) continue;
      const int only = unitCell(unit, lowestIndex(positions));
      // A cell with one candidate is a naked single; calling it hidden would mis-grade.
      if (popcount(grid.candidate[only]) == 1) continue;
      cell = only;
      digit = d;
      return true;
    }
  }
  return false;
}

bool applyNakedSingle(Grid& grid, bool& broken) {
  bool progress = false;
  int cell = -1;
  int digit = 0;
  while (findNakedSingle(grid, cell, digit)) {
    if (!assign(grid, cell, digit)) {
      broken = true;
      return true;
    }
    progress = true;
  }
  return progress;
}

bool applyHiddenSingle(Grid& grid, bool& broken) {
  int cell = -1;
  int digit = 0;
  if (!findHiddenSingle(grid, cell, digit)) return false;
  if (!assign(grid, cell, digit)) broken = true;
  return true;
}

// Pointing and claiming: a digit confined to a box-line intersection leaves the
// rest of the other unit.
bool applyLockedCandidates(Grid& grid, bool& broken) {
  for (int box = 0; box < SIZE; ++box) {
    const int unit = 2 * SIZE + box;
    for (int digit = 1; digit <= SIZE; ++digit) {
      if (digitPlacedIn(grid, unit, digit)) continue;
      const Mask positions = positionsOf(grid, unit, digit);
      if (popcount(positions) < 2) continue;
      int row = -1;
      int column = -1;
      bool sameRow = true;
      bool sameColumn = true;
      for (int slot = 0; slot < SIZE; ++slot) {
        if (!(positions & static_cast<Mask>(1u << slot))) continue;
        const int cell = unitCell(unit, slot);
        if (row < 0) {
          row = rowOf(cell);
        } else if (rowOf(cell) != row) {
          sameRow = false;
        }
        if (column < 0) {
          column = columnOf(cell);
        } else if (columnOf(cell) != column) {
          sameColumn = false;
        }
      }
      bool progress = false;
      if (sameRow) {
        for (int c = 0; c < SIZE; ++c) {
          const int cell = cellAt(row, c);
          if (boxOf(cell) != box) progress |= eliminate(grid, cell, bitFor(digit), broken);
        }
      }
      if (sameColumn) {
        for (int r = 0; r < SIZE; ++r) {
          const int cell = cellAt(r, column);
          if (boxOf(cell) != box) progress |= eliminate(grid, cell, bitFor(digit), broken);
        }
      }
      if (progress) return true;
    }
  }

  for (int line = 0; line < 2 * SIZE; ++line) {
    const bool isRow = line < SIZE;
    for (int digit = 1; digit <= SIZE; ++digit) {
      if (digitPlacedIn(grid, line, digit)) continue;
      const Mask positions = positionsOf(grid, line, digit);
      if (popcount(positions) < 2) continue;
      int box = -1;
      bool sameBox = true;
      for (int slot = 0; slot < SIZE; ++slot) {
        if (!(positions & static_cast<Mask>(1u << slot))) continue;
        const int cell = unitCell(line, slot);
        if (box < 0) {
          box = boxOf(cell);
        } else if (boxOf(cell) != box) {
          sameBox = false;
        }
      }
      if (!sameBox || box < 0) continue;
      bool progress = false;
      for (int slot = 0; slot < SIZE; ++slot) {
        const int cell = unitCell(2 * SIZE + box, slot);
        const bool onLine = isRow ? rowOf(cell) == line : columnOf(cell) == line - SIZE;
        if (!onLine) progress |= eliminate(grid, cell, bitFor(digit), broken);
      }
      if (progress) return true;
    }
  }
  return false;
}

// `size` cells of a unit whose candidates span exactly `size` digits.
bool applyNakedSubset(Grid& grid, const int size, bool& broken) {
  for (int unit = 0; unit < UNITS; ++unit) {
    int open[SIZE];
    int openCount = 0;
    for (int slot = 0; slot < SIZE; ++slot) {
      const int cell = unitCell(unit, slot);
      if (grid.value[cell] == 0) open[openCount++] = cell;
    }
    if (openCount <= size) continue;

    int index[3] = {0, 1, 2};
    do {
      Mask spanned = 0;
      bool usable = true;
      for (int i = 0; i < size; ++i) {
        const Mask candidates = grid.candidate[open[index[i]]];
        const int count = popcount(candidates);
        if (count < 2 || count > size) {
          usable = false;
          break;
        }
        spanned = static_cast<Mask>(spanned | candidates);
      }
      if (!usable || popcount(spanned) != size) continue;

      bool progress = false;
      for (int other = 0; other < openCount; ++other) {
        bool inSubset = false;
        for (int i = 0; i < size; ++i) inSubset |= index[i] == other;
        if (!inSubset) progress |= eliminate(grid, open[other], spanned, broken);
      }
      if (progress) return true;
    } while (nextCombination(index, size, openCount));
  }
  return false;
}

// `size` digits of a unit confined to exactly `size` cells.
bool applyHiddenSubset(Grid& grid, const int size, bool& broken) {
  for (int unit = 0; unit < UNITS; ++unit) {
    int digits[SIZE];
    Mask where[SIZE];
    int digitCount = 0;
    for (int digit = 1; digit <= SIZE; ++digit) {
      if (digitPlacedIn(grid, unit, digit)) continue;
      const Mask positions = positionsOf(grid, unit, digit);
      const int count = popcount(positions);
      if (count < 2 || count > size) continue;
      digits[digitCount] = digit;
      where[digitCount] = positions;
      ++digitCount;
    }
    if (digitCount < size) continue;

    int index[3] = {0, 1, 2};
    do {
      Mask covered = 0;
      Mask wanted = 0;
      for (int i = 0; i < size; ++i) {
        covered = static_cast<Mask>(covered | where[index[i]]);
        wanted = static_cast<Mask>(wanted | bitFor(digits[index[i]]));
      }
      if (popcount(covered) != size) continue;

      bool progress = false;
      for (int slot = 0; slot < SIZE; ++slot) {
        if (!(covered & static_cast<Mask>(1u << slot))) continue;
        progress |= eliminate(grid, unitCell(unit, slot), static_cast<Mask>(ALL_DIGITS & ~wanted), broken);
      }
      if (progress) return true;
    } while (nextCombination(index, size, digitCount));
  }
  return false;
}

// X-wing (size 2) and swordfish (size 3): a digit's homes on `size` lines span
// `size` crossing lines, so it leaves those crossing lines elsewhere.
bool applyFish(Grid& grid, const int size, bool& broken) {
  for (int orientation = 0; orientation < 2; ++orientation) {
    const bool byRow = orientation == 0;
    for (int digit = 1; digit <= SIZE; ++digit) {
      int line[SIZE];
      Mask cross[SIZE];
      int found = 0;
      for (int i = 0; i < SIZE; ++i) {
        const int unit = byRow ? i : SIZE + i;
        if (digitPlacedIn(grid, unit, digit)) continue;
        const Mask positions = positionsOf(grid, unit, digit);
        const int count = popcount(positions);
        if (count < 2 || count > size) continue;
        line[found] = i;
        cross[found] = positions;
        ++found;
      }
      if (found < size) continue;

      int index[3] = {0, 1, 2};
      do {
        Mask covered = 0;
        for (int i = 0; i < size; ++i) covered = static_cast<Mask>(covered | cross[index[i]]);
        if (popcount(covered) != size) continue;

        bool progress = false;
        for (int slot = 0; slot < SIZE; ++slot) {
          if (!(covered & static_cast<Mask>(1u << slot))) continue;
          for (int i = 0; i < SIZE; ++i) {
            bool inFish = false;
            for (int k = 0; k < size; ++k) inFish |= line[index[k]] == i;
            if (inFish) continue;
            const int cell = byRow ? cellAt(i, slot) : cellAt(slot, i);
            progress |= eliminate(grid, cell, bitFor(digit), broken);
          }
        }
        if (progress) return true;
      } while (nextCombination(index, size, found));
    }
  }
  return false;
}

// Pivot {a,b} with pincers {a,c} and {b,c}: c leaves every cell seeing both pincers.
bool applyXYWing(Grid& grid, bool& broken) {
  for (int pivot = 0; pivot < CELLS; ++pivot) {
    if (grid.value[pivot] != 0 || popcount(grid.candidate[pivot]) != 2) continue;
    const int a = lowestDigit(grid.candidate[pivot]);
    const int b = lowestDigit(static_cast<Mask>(grid.candidate[pivot] & ~bitFor(a)));

    for (int i = 0; i < PEERS; ++i) {
      const int first = PEER.peer[pivot][i];
      if (grid.value[first] != 0 || popcount(grid.candidate[first]) != 2) continue;
      const bool hasA = (grid.candidate[first] & bitFor(a)) != 0;
      const bool hasB = (grid.candidate[first] & bitFor(b)) != 0;
      if (hasA == hasB) continue;
      const int shared = hasA ? a : b;
      const int other = hasA ? b : a;
      const int third = lowestDigit(static_cast<Mask>(grid.candidate[first] & ~bitFor(shared)));
      if (third == other || third == 0) continue;
      const Mask wanted = static_cast<Mask>(bitFor(other) | bitFor(third));

      for (int j = 0; j < PEERS; ++j) {
        const int second = PEER.peer[pivot][j];
        if (second == first || grid.value[second] != 0 || grid.candidate[second] != wanted) continue;
        bool progress = false;
        for (int cell = 0; cell < CELLS; ++cell) {
          if (cell == pivot || cell == first || cell == second) continue;
          if (!arePeers(cell, first) || !arePeers(cell, second)) continue;
          progress |= eliminate(grid, cell, bitFor(third), broken);
        }
        if (progress) return true;
      }
    }
  }
  return false;
}

bool apply(Grid& grid, const Technique technique, bool& broken) {
  switch (technique) {
    case Technique::None:
      return false;
    case Technique::NakedSingle:
      return applyNakedSingle(grid, broken);
    case Technique::HiddenSingle:
      return applyHiddenSingle(grid, broken);
    case Technique::LockedCandidates:
      return applyLockedCandidates(grid, broken);
    case Technique::NakedPair:
      return applyNakedSubset(grid, 2, broken);
    case Technique::HiddenPair:
      return applyHiddenSubset(grid, 2, broken);
    case Technique::NakedTriple:
      return applyNakedSubset(grid, 3, broken);
    case Technique::HiddenTriple:
      return applyHiddenSubset(grid, 3, broken);
    case Technique::XWing:
      return applyFish(grid, 2, broken);
    case Technique::XYWing:
      return applyXYWing(grid, broken);
    case Technique::Swordfish:
      return applyFish(grid, 3, broken);
  }
  return false;
}

bool fits(const uint8_t value[CELLS], const int cell, const int digit) {
  for (int i = 0; i < PEERS; ++i) {
    if (value[PEER.peer[cell][i]] == digit) return false;
  }
  return true;
}

bool isComplete(const Grid& grid) {
  for (int cell = 0; cell < CELLS; ++cell) {
    if (grid.value[cell] == 0) return false;
  }
  return true;
}

bool solvableWith(const uint8_t given[CELLS], const Technique ceiling, Grid& probe) {
  if (!load(probe, given)) return false;
  const SolveReport report = solve(probe, ceiling);
  return report.solved && !report.broken;
}

}  // namespace

bool assign(Grid& grid, const int cell, const int digit) {
  if (cell < 0 || cell >= CELLS || digit < 1 || digit > SIZE) return false;
  if (grid.value[cell] == 0 && !(grid.candidate[cell] & bitFor(digit))) return false;
  grid.value[cell] = static_cast<uint8_t>(digit);
  grid.candidate[cell] = 0;
  const Mask bit = bitFor(digit);
  bool ok = true;
  for (int i = 0; i < PEERS; ++i) {
    const int peer = PEER.peer[cell][i];
    if (grid.value[peer] == digit) {
      ok = false;
      continue;
    }
    if (grid.value[peer] != 0) continue;
    grid.candidate[peer] = static_cast<Mask>(grid.candidate[peer] & ~bit);
    if (grid.candidate[peer] == 0) ok = false;
  }
  return ok;
}

bool load(Grid& grid, const uint8_t given[CELLS]) {
  for (int cell = 0; cell < CELLS; ++cell) {
    grid.value[cell] = 0;
    grid.candidate[cell] = ALL_DIGITS;
  }
  bool ok = true;
  for (int cell = 0; cell < CELLS; ++cell) {
    if (given[cell] != 0 && !assign(grid, cell, given[cell])) ok = false;
  }
  return ok;
}

SolveReport solve(Grid& grid, const Technique ceiling) {
  SolveReport report;
  while (report.steps < STEP_BUDGET) {
    if (isComplete(grid)) {
      report.solved = true;
      return report;
    }
    bool progress = false;
    for (int rung = 1; rung <= static_cast<int>(ceiling); ++rung) {
      bool broken = false;
      if (!apply(grid, static_cast<Technique>(rung), broken)) continue;
      ++report.steps;
      if (rung > static_cast<int>(report.hardest)) report.hardest = static_cast<Technique>(rung);
      if (broken) {
        report.broken = true;
        return report;
      }
      progress = true;
      break;
    }
    if (!progress) return report;
  }
  return report;
}

Technique grade(const uint8_t given[CELLS]) {
  Grid grid;
  if (!load(grid, given)) return Technique::None;
  const SolveReport report = solve(grid, ceilingFor(Level::Expert));
  return report.solved && !report.broken ? report.hardest : Technique::None;
}

int countSolutions(const uint8_t given[CELLS], const int limit, uint8_t* solution) {
  uint8_t value[CELLS];
  Mask used[3][SIZE] = {};  // rows, columns, boxes
  for (int cell = 0; cell < CELLS; ++cell) {
    const int d = given[cell];
    value[cell] = static_cast<uint8_t>(d);
    if (d == 0) continue;
    if (d > SIZE) return 0;
    const Mask b = bitFor(d);
    if ((used[0][rowOf(cell)] | used[1][columnOf(cell)] | used[2][boxOf(cell)]) & b) return 0;
    used[0][rowOf(cell)] |= b;
    used[1][columnOf(cell)] |= b;
    used[2][boxOf(cell)] |= b;
  }
  auto toggle = [&](const int cell, const int d) {
    const Mask b = bitFor(d);
    used[0][rowOf(cell)] ^= b;
    used[1][columnOf(cell)] ^= b;
    used[2][boxOf(cell)] ^= b;
  };

  // Iterative depth-first search on the most constrained cell: no recursion on
  // small device stacks.
  uint8_t stackCell[CELLS];
  Mask stackLeft[CELLS];
  int depth = 0;
  int found = 0;
  bool descend = true;
  while (true) {
    if (descend) {
      int best = -1;
      Mask bestMask = 0;
      int bestCount = SIZE + 1;
      bool dead = false;
      for (int cell = 0; cell < CELLS && bestCount > 1; ++cell) {
        if (value[cell] != 0) continue;
        const Mask open =
            static_cast<Mask>(ALL_DIGITS & ~(used[0][rowOf(cell)] | used[1][columnOf(cell)] | used[2][boxOf(cell)]));
        const int n = popcount(open);
        if (n == 0) {
          dead = true;
          break;
        }
        if (n < bestCount) {
          best = cell;
          bestMask = open;
          bestCount = n;
        }
      }
      if (!dead && best < 0) {
        if (found == 0 && solution != nullptr) memcpy(solution, value, CELLS);
        if (++found >= limit) return found;
      } else if (!dead) {
        stackCell[depth] = static_cast<uint8_t>(best);
        stackLeft[depth] = bestMask;
        ++depth;
      }
    }
    descend = false;
    while (depth > 0) {
      const int cell = stackCell[depth - 1];
      if (value[cell] != 0) {
        toggle(cell, value[cell]);
        value[cell] = 0;
      }
      const Mask left = stackLeft[depth - 1];
      if (left == 0) {
        --depth;
        continue;
      }
      const int d = lowestDigit(left);
      stackLeft[depth - 1] = static_cast<Mask>(left & ~bitFor(d));
      value[cell] = static_cast<uint8_t>(d);
      toggle(cell, d);
      descend = true;
      break;
    }
    if (!descend) return found;
  }
}

bool fillComplete(uint8_t solution[CELLS], Workspace& work, uint32_t& rng) {
  for (int cell = 0; cell < CELLS; ++cell) {
    for (int i = 0; i < SIZE; ++i) work.order[cell][i] = static_cast<uint8_t>(i + 1);
    for (int i = SIZE - 1; i > 0; --i) {
      const int j = static_cast<int>(nextRandom(rng) % static_cast<uint32_t>(i + 1));
      const uint8_t swap = work.order[cell][i];
      work.order[cell][i] = work.order[cell][j];
      work.order[cell][j] = swap;
    }
    work.next[cell] = 0;
    work.value[cell] = 0;
  }

  int cell = 0;
  uint32_t guard = 0;
  while (cell < CELLS) {
    if (++guard > FILL_BUDGET) return false;
    bool placed = false;
    while (work.next[cell] < SIZE) {
      const int digit = work.order[cell][work.next[cell]++];
      if (fits(work.value, cell, digit)) {
        work.value[cell] = static_cast<uint8_t>(digit);
        placed = true;
        break;
      }
    }
    if (placed) {
      ++cell;
      continue;
    }
    work.next[cell] = 0;
    work.value[cell] = 0;
    if (cell == 0) return false;
    --cell;
    work.value[cell] = 0;
  }
  memcpy(solution, work.value, CELLS);
  return true;
}

bool carve(Puzzle& out, const uint8_t solution[CELLS], Workspace& work, uint32_t& rng) {
  memcpy(out.given, solution, CELLS);
  memcpy(out.solution, solution, CELLS);

  int leader[(CELLS + 1) / 2];
  int leaderCount = 0;
  for (int cell = 0; cell <= CELLS - 1 - cell; ++cell) leader[leaderCount++] = cell;
  for (int i = leaderCount - 1; i > 0; --i) {
    const int j = static_cast<int>(nextRandom(rng) % static_cast<uint32_t>(i + 1));
    const int swap = leader[i];
    leader[i] = leader[j];
    leader[j] = swap;
  }

  const Technique ceiling = ceilingFor(Level::Expert);
  for (int i = 0; i < leaderCount; ++i) {
    const int a = leader[i];
    const int b = CELLS - 1 - a;
    const uint8_t keptA = out.given[a];
    const uint8_t keptB = out.given[b];
    out.given[a] = 0;
    out.given[b] = 0;
    if (!solvableWith(out.given, ceiling, work.grid)) {
      out.given[a] = keptA;
      out.given[b] = keptB;
    }
  }

  if (!load(work.grid, out.given)) return false;
  const SolveReport report = solve(work.grid, ceiling);
  if (!report.solved || report.broken) return false;
  out.hardest = report.hardest;
  out.level = levelOf(report.hardest);
  out.clues = 0;
  for (int cell = 0; cell < CELLS; ++cell) out.clues += out.given[cell] != 0;
  return true;
}

bool generate(Puzzle& out, const Level level, Workspace& work, uint32_t& rng, const int attempts) {
  for (int attempt = 0; attempt < attempts; ++attempt) {
    uint8_t solution[CELLS];
    if (!fillComplete(solution, work, rng)) continue;
    if (!carve(out, solution, work, rng)) continue;
    if (out.level == level) return true;
  }
  return false;
}

Hint nextHint(const uint8_t values[CELLS], const Technique ceiling) {
  Hint hint;
  Grid grid;
  if (!load(grid, values)) return hint;
  // Eliminations run silently; the hint is the first placement, labelled with
  // the hardest rule needed to reach it.
  Technique hardest = Technique::None;
  for (int guard = 0; guard < STEP_BUDGET; ++guard) {
    if (isComplete(grid)) return hint;
    int cell = -1;
    int digit = 0;
    Technique via = Technique::None;
    if (findNakedSingle(grid, cell, digit)) {
      via = Technique::NakedSingle;
    } else if (findHiddenSingle(grid, cell, digit)) {
      via = Technique::HiddenSingle;
    }
    if (via != Technique::None) {
      hint.cell = cell;
      hint.digit = digit;
      hint.technique = hardest > via ? hardest : via;
      return hint;
    }
    bool progress = false;
    for (int rung = static_cast<int>(Technique::LockedCandidates); rung <= static_cast<int>(ceiling); ++rung) {
      bool broken = false;
      if (!apply(grid, static_cast<Technique>(rung), broken)) continue;
      if (broken) return hint;
      if (rung > static_cast<int>(hardest)) hardest = static_cast<Technique>(rung);
      progress = true;
      break;
    }
    if (!progress) return hint;
  }
  return hint;
}

}  // namespace sudoku
