#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "Sudoku.h"
#include "SudokuBank.h"

using namespace sudoku;

namespace {

uint32_t fakeNow = 0;
uint32_t fakeMillis() { return fakeNow; }

bool symmetric(const uint8_t given[CELLS]) {
  for (int c = 0; c < CELLS; ++c) {
    if ((given[c] != 0) != (given[CELLS - 1 - c] != 0)) return false;
  }
  return true;
}

void bankGiven(const int level, const int index, uint8_t out[CELLS]) {
  for (int c = 0; c < CELLS; ++c) out[c] = static_cast<uint8_t>(BANK[level][index][c] - '0');
}

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

int firstOpen(const Game& g) {
  for (int c = 0; c < CELLS; ++c) {
    if (!g.isLocked(c) && g.value(c) == 0) return c;
  }
  return -1;
}

// `text` with the value of `key` replaced (`value` carries its own quotes).
std::string withField(const std::string& text, const char* key, const std::string& value) {
  std::string t = text;
  const size_t start = t.find(std::string("\"") + key + "\":") + strlen(key) + 3;
  const size_t end = value[0] == '"' ? t.find('"', start + 1) + 1 : t.find_first_of(",}", start);
  return t.replace(start, end - start, value);
}

}  // namespace

TEST(SudokuSolver, BankPuzzlesAreUniqueSymmetricAndGradedAsLabelled) {
  for (int level = 0; level < LEVEL_COUNT; ++level) {
    for (int i = 0; i < BANK_PER_LEVEL; ++i) {
      uint8_t given[CELLS];
      bankGiven(level, i, given);
      EXPECT_EQ(countSolutions(given, 2, nullptr), 1) << level << "/" << i;
      EXPECT_TRUE(symmetric(given));
      const Technique t = grade(given);
      ASSERT_NE(t, Technique::None);
      EXPECT_EQ(static_cast<int>(levelOf(t)), level) << level << "/" << i;
    }
  }
}

TEST(SudokuSolver, ReshapedPuzzlesKeepUniquenessGradeAndSymmetry) {
  for (int level = 0; level < LEVEL_COUNT; ++level) {
    for (uint32_t seed = 1; seed <= 40; ++seed) {
      uint8_t base[CELLS];
      uint8_t given[CELLS];
      const int index = static_cast<int>(seed % BANK_PER_LEVEL);
      bankGiven(level, index, base);
      bankPuzzle(static_cast<Level>(level), index, seed * 2654435761u, given);
      EXPECT_EQ(countSolutions(given, 2, nullptr), 1);
      EXPECT_TRUE(symmetric(given));
      EXPECT_EQ(grade(given), grade(base));
      int clues = 0;
      int baseClues = 0;
      for (int c = 0; c < CELLS; ++c) {
        clues += given[c] != 0;
        baseClues += base[c] != 0;
      }
      EXPECT_EQ(clues, baseClues);
    }
  }
}

TEST(SudokuSolver, LevelIsTheLowestCeilingThatSolves) {
  for (int level = 1; level < LEVEL_COUNT; ++level) {
    for (int i = 0; i < BANK_PER_LEVEL; ++i) {
      uint8_t given[CELLS];
      bankGiven(level, i, given);
      Grid grid;
      ASSERT_TRUE(load(grid, given));
      EXPECT_FALSE(solve(grid, ceilingFor(static_cast<Level>(level - 1))).solved);
      ASSERT_TRUE(load(grid, given));
      EXPECT_TRUE(solve(grid, ceilingFor(static_cast<Level>(level))).solved);
    }
  }
}

TEST(SudokuSolver, CountSolutionsSeesNoneOneAndMany) {
  uint8_t empty[CELLS] = {};
  EXPECT_EQ(countSolutions(empty, 2, nullptr), 2);
  uint8_t clash[CELLS] = {};
  clash[0] = 5;
  clash[1] = 5;
  EXPECT_EQ(countSolutions(clash, 2, nullptr), 0);
  uint8_t given[CELLS];
  bankGiven(3, 0, given);
  uint8_t solution[CELLS];
  ASSERT_EQ(countSolutions(given, 2, solution), 1);
  EXPECT_EQ(countSolutions(solution, 2, nullptr), 1);
  for (int c = 0; c < CELLS; ++c) {
    if (given[c] != 0) EXPECT_EQ(solution[c], given[c]);
  }
}

TEST(SudokuSolver, GeneratorHitsEveryLevelWithUniquePuzzles) {
  Workspace work;
  for (int level = 0; level < LEVEL_COUNT; ++level) {
    uint32_t rng = 1234u + static_cast<uint32_t>(level);
    int attempts = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int n = 0; n < 4;) {
      Puzzle p;
      ++attempts;
      ASSERT_LT(attempts, 2000);
      if (!generate(p, static_cast<Level>(level), work, rng, 1)) continue;
      EXPECT_EQ(static_cast<int>(p.level), level);
      EXPECT_EQ(countSolutions(p.given, 2, nullptr), 1);
      EXPECT_EQ(grade(p.given), p.hardest);
      EXPECT_TRUE(symmetric(p.given));
      ++n;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("sudoku generate level %d: %.1f ms per puzzle (%d attempts for 4)\n", level, ms / 4, attempts);
  }
}

TEST(SudokuGame, SameSeedSamePuzzle) {
  Game a(fakeMillis);
  Game b(fakeMillis);
  a.newGame(Level::Hard, 77);
  b.newGame(Level::Hard, 77);
  for (int c = 0; c < CELLS; ++c) EXPECT_EQ(a.value(c), b.value(c));
  b.newGame(Level::Hard, 78);
  int diff = 0;
  for (int c = 0; c < CELLS; ++c) diff += a.value(c) != b.value(c);
  EXPECT_GT(diff, 0);
  EXPECT_EQ(a.level(), Level::Hard);
  EXPECT_EQ(a.seed(), 77u);
}

TEST(SudokuGame, NewGameTakesBoundedTime) {
  Game g(fakeMillis);
  const auto t0 = std::chrono::steady_clock::now();
  for (uint32_t seed = 1; seed <= 400; ++seed) g.newGame(static_cast<Level>(seed % LEVEL_COUNT), seed);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  printf("sudoku newGame from bank: %.3f ms\n", ms / 400);
}

TEST(SudokuGame, EditingRulesUndoAndConflicts) {
  Game g(fakeMillis);
  g.newGame(Level::Easy, 5);
  int given = -1;
  for (int c = 0; c < CELLS && given < 0; ++c) {
    if (g.isGiven(c)) given = c;
  }
  ASSERT_GE(given, 0);
  EXPECT_FALSE(g.place(given, 1));
  EXPECT_FALSE(g.erase(given));
  EXPECT_FALSE(g.canUndo());

  // An open peer of the given: placing the given's digit there clashes.
  int peer = -1;
  for (int i = 0; i < PEERS && peer < 0; ++i) {
    const int c = PEER.peer[given][i];
    if (!g.isLocked(c)) peer = c;
  }
  ASSERT_GE(peer, 0);
  const int digit = g.value(given);
  const int placed = g.digitCount(digit);
  EXPECT_TRUE(g.place(peer, digit));
  EXPECT_EQ(g.digitCount(digit), placed + 1);
  EXPECT_TRUE(g.hasConflict(peer));
  EXPECT_TRUE(g.hasConflict(given));
  EXPECT_TRUE(g.isWrong(peer));
  EXPECT_FALSE(g.place(peer, digit));  // no change
  EXPECT_TRUE(g.undo());
  EXPECT_EQ(g.value(peer), 0);
  EXPECT_FALSE(g.hasConflict(given));

  // Notes: peer digits are hidden from view but kept; erase and undo.
  const int other = digit == 3 ? 4 : 3;
  EXPECT_TRUE(g.toggleNote(peer, other));
  EXPECT_TRUE(g.toggleNote(peer, digit));
  EXPECT_EQ(g.notes(peer), static_cast<Mask>(bitFor(other) | bitFor(digit)));
  EXPECT_EQ(g.visibleNotes(peer) & bitFor(digit), 0);
  EXPECT_TRUE(g.erase(peer));
  EXPECT_EQ(g.notes(peer), 0);
  EXPECT_FALSE(g.erase(peer));
  EXPECT_TRUE(g.undo());
  EXPECT_EQ(g.notes(peer), static_cast<Mask>(bitFor(other) | bitFor(digit)));
  // A digit clears the notes; undo brings them back.
  EXPECT_TRUE(g.place(peer, other));
  EXPECT_EQ(g.notes(peer), 0);
  EXPECT_FALSE(g.toggleNote(peer, 1));
  EXPECT_TRUE(g.undo());
  EXPECT_EQ(g.value(peer), 0);
  EXPECT_NE(g.notes(peer), 0);
  EXPECT_FALSE(g.toggleNote(peer, 0));
  EXPECT_FALSE(g.toggleNote(peer, 10));
  EXPECT_FALSE(g.place(-1, 1));
  EXPECT_FALSE(g.place(CELLS, 1));
  EXPECT_FALSE(g.place(peer, 10));
}

TEST(SudokuGame, UndoRingKeepsTheNewestSteps) {
  Game g(fakeMillis);
  g.newGame(Level::Medium, 9);
  const int cell = firstOpen(g);
  for (int i = 0; i < UNDO_DEPTH + 40; ++i) ASSERT_TRUE(g.toggleNote(cell, 1 + i % 9));
  int undone = 0;
  while (g.undo()) ++undone;
  EXPECT_EQ(undone, UNDO_DEPTH);
}

TEST(SudokuGame, HintsFixMistakesFirstAndFinishThePuzzle) {
  Game g(fakeMillis);
  g.newGame(Level::Expert, 3);
  const int cell = firstOpen(g);
  int wrong = 1;
  while (!g.place(cell, wrong) || !g.isWrong(cell)) ++wrong;
  ASSERT_TRUE(g.isWrong(cell));
  EXPECT_EQ(g.revealHint(), cell);
  EXPECT_FALSE(g.isWrong(cell));
  EXPECT_TRUE(g.isHinted(cell));
  EXPECT_TRUE(g.isLocked(cell));
  EXPECT_FALSE(g.place(cell, 1));
  EXPECT_EQ(g.hintsUsed(), 1);

  const int preferred = firstOpen(g);
  EXPECT_EQ(g.revealHint(preferred), preferred);
  int guard = 0;
  while (!g.isSolved() && guard++ < CELLS) ASSERT_GE(g.revealHint(), 0);
  EXPECT_TRUE(g.isSolved());
  EXPECT_FALSE(g.inProgress());
  EXPECT_EQ(g.revealHint(), -1);
  EXPECT_FALSE(g.canUndo());
  for (int d = 1; d <= SIZE; ++d) EXPECT_EQ(g.digitCount(d), SIZE);
}

TEST(SudokuGame, SolvingByHandStopsTheClock) {
  fakeNow = 1000;
  Game g(fakeMillis);
  uint8_t given[CELLS];
  bankGiven(0, 4, given);
  ASSERT_TRUE(g.newGame(given, Level::Easy, 4));
  uint8_t solution[CELLS];
  countSolutions(given, 1, solution);
  fakeNow += 5000;
  EXPECT_EQ(g.elapsedMs(), 5000u);
  g.pause();
  fakeNow += 7000;
  EXPECT_EQ(g.elapsedMs(), 5000u);
  g.resume();
  fakeNow += 1000;
  for (int c = 0; c < CELLS; ++c) {
    if (!g.isGiven(c)) EXPECT_TRUE(g.place(c, solution[c]));
  }
  EXPECT_TRUE(g.isSolved());
  fakeNow += 9000;
  EXPECT_EQ(g.elapsedMs(), 6000u);
  g.resume();
  EXPECT_TRUE(g.paused());
  EXPECT_EQ(g.hintsUsed(), 0);
}

TEST(SudokuGame, CustomPuzzleMustBeUnique) {
  Game g(fakeMillis);
  g.newGame(Level::Easy, 1);
  const std::string before = save(g);
  uint8_t empty[CELLS] = {};
  EXPECT_FALSE(g.newGame(empty, Level::Easy, 2));
  EXPECT_EQ(save(g), before);
}

TEST(SudokuSave, RoundTripKeepsEverythingIncludingUndo) {
  fakeNow = 100;
  Game g(fakeMillis);
  g.newGame(Level::Hard, 42);
  const int a = firstOpen(g);
  ASSERT_TRUE(g.toggleNote(a, 2));
  ASSERT_TRUE(g.toggleNote(a, 7));
  ASSERT_GE(g.revealHint(), 0);
  ASSERT_TRUE(g.place(firstOpen(g), 9));
  fakeNow += 12345;
  const std::string text = save(g);
  ASSERT_FALSE(text.empty());

  Game r(fakeMillis);
  ASSERT_TRUE(r.deserialize(text.data(), text.size()));
  EXPECT_TRUE(r.paused());
  EXPECT_EQ(r.elapsedMs(), g.elapsedMs());
  EXPECT_EQ(r.level(), Level::Hard);
  EXPECT_EQ(r.seed(), 42u);
  EXPECT_EQ(r.hintsUsed(), 1);
  EXPECT_EQ(save(r), text);
  while (g.undo()) {
    ASSERT_TRUE(r.undo());
    for (int c = 0; c < CELLS; ++c) {
      ASSERT_EQ(r.value(c), g.value(c));
      ASSERT_EQ(r.notes(c), g.notes(c));
      ASSERT_EQ(r.isHinted(c), g.isHinted(c));
    }
  }
  EXPECT_FALSE(r.canUndo());
}

TEST(SudokuSave, FullUndoRingFitsSaveMax) {
  Game g(fakeMillis);
  g.newGame(Level::Expert, 8);
  for (int c = 0; c < CELLS; ++c) {
    if (!g.isLocked(c)) g.toggleNote(c, 9);
  }
  for (int i = 0; i < UNDO_DEPTH; ++i) ASSERT_TRUE(g.toggleNote(firstOpen(g), 1 + i % 8));
  g.revealHint();
  const std::string text = save(g);
  ASSERT_FALSE(text.empty());
  EXPECT_LT(text.size(), SAVE_MAX);
  Game r(fakeMillis);
  EXPECT_TRUE(r.deserialize(text.data(), text.size()));
  char tiny[64];
  EXPECT_EQ(g.serialize(tiny, sizeof(tiny)), 0u);
}

TEST(SudokuSave, CorruptSavesAreRejectedAndLeaveTheGameAlone) {
  Game g(fakeMillis);
  g.newGame(Level::Medium, 11);
  const int cell = firstOpen(g);
  g.place(cell, 1);
  const std::string good = save(g);

  Game r(fakeMillis);
  r.newGame(Level::Easy, 99);
  const std::string before = save(r);
  auto rejects = [&](const std::string& text) {
    EXPECT_FALSE(r.deserialize(text.data(), text.size())) << text;
    EXPECT_EQ(save(r), before);
  };
  rejects("");
  rejects("{}");
  rejects("garbage");
  rejects(good.substr(0, good.size() / 2));
  rejects(withField(good, "v", "2"));
  rejects(withField(good, "v", "1x"));
  rejects(withField(good, "level", "4"));
  rejects(withField(good, "hints", "65536"));
  rejects(withField(good, "given", "\"" + std::string(CELLS, '0') + "\""));  // many solutions

  const std::string cells = good.substr(good.find("\"cells\":\"") + 9, CELLS);
  int given = 0;
  while (!g.isGiven(given)) ++given;
  std::string bad = cells;
  bad[given] = static_cast<char>('0' + g.value(given) % 9 + 1);  // a given overwritten
  rejects(withField(good, "cells", "\"" + bad + "\""));
  bad = cells;
  bad[given] = static_cast<char>('a' + g.value(given) - 1);  // a given marked revealed
  rejects(withField(good, "cells", "\"" + bad + "\""));
  uint8_t solution[CELLS];
  uint8_t givens[CELLS];
  for (int c = 0; c < CELLS; ++c) givens[c] = g.isGiven(c) ? g.value(c) : 0;
  countSolutions(givens, 1, solution);
  bad = cells;
  bad[cell] = static_cast<char>('a' + solution[cell] % 9);  // revealed with a wrong digit
  rejects(withField(good, "cells", "\"" + bad + "\""));
  rejects(withField(good, "cells", "\"" + cells.substr(1) + "\""));
  rejects(withField(good, "notes", "\"" + std::string(3 * CELLS, 'f') + "\""));
  rejects(withField(good, "notes", "\"" + std::string(3 * CELLS - 1, '0') + "x\""));
  char step[7];
  snprintf(step, sizeof(step), "%02x0000", given);  // an undo step on a given
  rejects(withField(good, "undo", std::string("\"") + step + "\""));
  rejects(withField(good, "undo", "\"5100000\""));
  rejects(withField(good, "undo", "\"zz0000\""));

  EXPECT_TRUE(r.deserialize(good.data(), good.size()));
}
