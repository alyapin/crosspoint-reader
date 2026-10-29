#include <HalStorage.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "Nonogram.h"
#include "src/games/GameSave.h"

using namespace nonogram;

namespace {

uint32_t fakeNow = 0;
uint32_t fakeMillis() { return fakeNow; }

std::vector<uint8_t> bitsOf(const char* rows) {
  std::vector<uint8_t> bits;
  for (const char* p = rows; *p != '\0'; ++p) {
    if (*p == '#' || *p == '.') bits.push_back(*p == '#' ? 1 : 0);
  }
  return bits;
}

Clue clue(std::initializer_list<int> runs) {
  Clue c;
  for (const int r : runs) c.runs[c.count++] = static_cast<uint8_t>(r);
  return c;
}

// A 5x5 cup with a line-solvable answer.
constexpr char kCup[] =
    "#####"
    "#####"
    "####."
    "####."
    "###..";

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

// Solutions of the clues of `bits`, counted up to `cap` by brute force over rows.
int countSolutions(const uint8_t* bits, const int side, const int cap) {
  std::vector<Clue> rows(side);
  std::vector<Clue> columns(side);
  for (int i = 0; i < side; ++i) {
    rows[i] = clueOf(bits + i * side, side, 1);
    columns[i] = clueOf(bits + i, side, side);
  }
  std::vector<std::vector<uint32_t>> candidates(side);
  for (uint32_t m = 0; m < (1u << side); ++m) {
    uint8_t line[MAX_SIDE];
    for (int j = 0; j < side; ++j) line[j] = (m >> j) & 1;
    const Clue c = clueOf(line, side, 1);
    for (int i = 0; i < side; ++i) {
      if (c.count == rows[i].count && std::equal(c.runs, c.runs + c.count, rows[i].runs)) candidates[i].push_back(m);
    }
  }
  std::vector<uint32_t> pick(side);
  int found = 0;
  std::function<void(int)> walk = [&](const int row) {
    if (found >= cap) return;
    if (row == side) {
      for (int x = 0; x < side; ++x) {
        uint8_t line[MAX_SIDE];
        for (int y = 0; y < side; ++y) line[y] = (pick[y] >> x) & 1;
        const Clue c = clueOf(line, side, 1);
        if (c.count != columns[x].count || !std::equal(c.runs, c.runs + c.count, columns[x].runs)) return;
      }
      ++found;
      return;
    }
    for (const uint32_t m : candidates[row]) {
      pick[row] = m;
      walk(row + 1);
    }
  };
  walk(0);
  return found;
}

}  // namespace

TEST(Nonogram, CluesAreTheBlackRuns) {
  const auto line = bitsOf("##.#..###.");
  const Clue c = clueOf(line.data(), 10, 1);
  ASSERT_EQ(c.count, 3);
  EXPECT_EQ(c.runs[0], 2);
  EXPECT_EQ(c.runs[1], 1);
  EXPECT_EQ(c.runs[2], 3);
  EXPECT_EQ(clueOf(bitsOf("..........").data(), 10, 1).count, 0);
  const auto column = bitsOf("#.#..");
  EXPECT_EQ(clueOf(column.data(), 3, 2).count, 1);
  EXPECT_EQ(clueOf(column.data(), 3, 2).runs[0], 2);
  EXPECT_EQ(clueOf(bitsOf("#.#.#.#.#.#.#.#").data(), 15, 1).count, MAX_RUNS);
}

TEST(Nonogram, LineSolverOverlapsAndFails) {
  uint8_t line[10] = {};
  ASSERT_TRUE(solveLine(clue({8}), line, 10));
  for (int i = 0; i < 10; ++i) EXPECT_EQ(line[i], i >= 2 && i < 8 ? Black : Unknown) << i;

  uint8_t empty[5] = {};
  ASSERT_TRUE(solveLine(clue({}), empty, 5));
  for (const uint8_t k : empty) EXPECT_EQ(k, White);

  uint8_t exact[5] = {};
  ASSERT_TRUE(solveLine(clue({2, 2}), exact, 5));
  EXPECT_EQ(exact[2], White);
  EXPECT_EQ(exact[0], Black);

  uint8_t blocked[5] = {Unknown, Unknown, White, Unknown, Unknown};
  EXPECT_FALSE(solveLine(clue({3}), blocked, 5));
}

// Every deduction agrees with brute force over all lines that fit.
TEST(Nonogram, LineSolverMatchesBruteForce) {
  constexpr int n = 8;
  games::Rng rng(11);
  for (int trial = 0; trial < 3000; ++trial) {
    uint8_t answer[n];
    for (uint8_t& b : answer) b = rng.below(2);
    const Clue c = clueOf(answer, n, 1);
    uint8_t line[n];
    for (int i = 0; i < n; ++i) {
      const uint32_t r = rng.below(4);
      line[i] = r == 0 ? (answer[i] ? Black : White) : r == 1 && trial % 7 == 0 ? (answer[i] ? White : Black) : Unknown;
    }
    bool canBlack[n] = {};
    bool canWhite[n] = {};
    bool any = false;
    for (uint32_t m = 0; m < (1u << n); ++m) {
      uint8_t cand[n];
      bool fits = true;
      for (int i = 0; i < n; ++i) {
        cand[i] = (m >> i) & 1;
        if ((line[i] == Black && !cand[i]) || (line[i] == White && cand[i])) fits = false;
      }
      const Clue cc = clueOf(cand, n, 1);
      if (!fits || cc.count != c.count || !std::equal(cc.runs, cc.runs + cc.count, c.runs)) continue;
      any = true;
      for (int i = 0; i < n; ++i) (cand[i] ? canBlack : canWhite)[i] = true;
    }
    uint8_t solved[n];
    memcpy(solved, line, n);
    ASSERT_EQ(solveLine(c, solved, n), any) << trial;
    if (!any) continue;
    for (int i = 0; i < n; ++i) {
      const uint8_t expected = canBlack[i] && canWhite[i] ? line[i] : canBlack[i] ? Black : White;
      ASSERT_EQ(solved[i], expected) << trial << " cell " << i;
    }
  }
}

TEST(Nonogram, LineSolvableNeedsAUniqueAnswer) {
  EXPECT_TRUE(lineSolvable(bitsOf(kCup).data(), 5));
  // Two diagonals swap freely.
  const auto ambiguous = bitsOf(
      "#...."
      ".#..."
      "....."
      "....."
      ".....");
  EXPECT_FALSE(lineSolvable(ambiguous.data(), 5));
  EXPECT_EQ(countSolutions(ambiguous.data(), 5, 3), 2);
}

TEST(Nonogram, GeneratedGridsAreUnique) {
  for (const int side : {6, 10, 15}) {
    for (uint32_t seed = 1; seed <= 12; ++seed) {
      games::Rng rng(seed);
      uint8_t bits[MAX_CELLS];
      ASSERT_GT(generate(side, rng, bits), 0) << side << " " << seed;
      EXPECT_TRUE(lineSolvable(bits, side));
      int black = 0;
      for (int i = 0; i < side * side; ++i) black += bits[i];
      EXPECT_NEAR(black, side * side / 2, side);
      if (side == 6) EXPECT_EQ(countSolutions(bits, side, 2), 1) << seed;
    }
  }
  games::Rng a(5);
  games::Rng b(5);
  uint8_t first[MAX_CELLS];
  uint8_t second[MAX_CELLS];
  generate(10, a, first);
  generate(10, b, second);
  EXPECT_EQ(memcmp(first, second, 100), 0);
}

TEST(Nonogram, FillingTheAnswerWins) {
  Game g(&fakeMillis);
  const auto cup = bitsOf(kCup);
  ASSERT_TRUE(g.newPicture(5, cup.data(), 7, "Чашка"));
  fakeNow = 1000;
  g.resume();
  EXPECT_TRUE(g.inProgress());
  for (int i = 0; i < 25; ++i) {
    if (cup[i]) EXPECT_TRUE(g.set(i, Filled));
  }
  EXPECT_TRUE(g.isSolved());
  EXPECT_FALSE(g.inProgress());
  fakeNow = 5000;
  EXPECT_EQ(g.elapsedMs(), 0u);
  EXPECT_FALSE(g.set(24, Filled));
  EXPECT_FALSE(g.canUndo());
}

TEST(Nonogram, AnyAnswerToTheCluesWins) {
  const auto bits = bitsOf(
      "#...."
      ".#..."
      "....."
      "....."
      ".....");
  Game g(&fakeMillis);
  ASSERT_TRUE(g.newPicture(5, bits.data(), 1, "x"));
  g.set(1, Filled);
  g.set(5, Filled);
  EXPECT_TRUE(g.isSolved());
}

TEST(Nonogram, UndoRestoresMarks) {
  Game g(&fakeMillis);
  const auto cup = bitsOf(kCup);
  ASSERT_TRUE(g.newPicture(5, cup.data(), 7, "Чашка"));
  EXPECT_FALSE(g.canUndo());
  EXPECT_TRUE(g.set(4, Crossed));
  EXPECT_TRUE(g.set(4, Filled));
  EXPECT_FALSE(g.set(4, Filled));
  EXPECT_TRUE(g.undo());
  EXPECT_EQ(g.mark(4), Crossed);
  EXPECT_TRUE(g.undo());
  EXPECT_EQ(g.mark(4), Empty);
  EXPECT_FALSE(g.undo());
  for (int i = 0; i < UNDO_DEPTH + 10; ++i) g.set(24, i % 2 == 0 ? Crossed : Empty);
  int undone = 0;
  while (g.undo()) ++undone;
  EXPECT_EQ(undone, UNDO_DEPTH);
}

TEST(Nonogram, WrongMarksCount) {
  Game g(&fakeMillis);
  const auto cup = bitsOf(kCup);
  ASSERT_TRUE(g.newPicture(5, cup.data(), 7, "Чашка"));
  g.set(0, Crossed);
  g.set(24, Filled);
  g.set(1, Filled);
  g.set(23, Crossed);
  EXPECT_TRUE(g.isWrong(0));
  EXPECT_TRUE(g.isWrong(24));
  EXPECT_FALSE(g.isWrong(1));
  EXPECT_EQ(g.mistakes(), 2);
  g.restart();
  EXPECT_EQ(g.mistakes(), 0);
  EXPECT_EQ(g.mark(1), Empty);
  EXPECT_FALSE(g.canUndo());
}

TEST(Nonogram, DoneRunsAreStruckFromEitherEnd) {
  const auto bits = bitsOf(
      "##.#.###.."
      "##########"
      ".........."
      ".........."
      ".........."
      ".........."
      ".........."
      ".........."
      ".........."
      "..........");
  Game g(&fakeMillis);
  ASSERT_TRUE(g.newPicture(10, bits.data(), 2, "x"));
  ASSERT_EQ(g.rowClue(0).count, 3);
  EXPECT_EQ(g.rowDone(0), 0);
  g.set(0, Filled);
  g.set(1, Filled);
  EXPECT_EQ(g.rowDone(0), 0);  // not closed yet
  g.set(2, Crossed);
  EXPECT_EQ(g.rowDone(0), 0b001);
  g.set(9, Crossed);
  g.set(8, Crossed);
  g.set(7, Filled);
  g.set(6, Filled);
  g.set(5, Filled);
  g.set(4, Crossed);
  EXPECT_EQ(g.rowDone(0), 0b101);
  g.set(3, Filled);
  EXPECT_EQ(g.rowDone(0), 0b111);
  // A whole match needs no crosses; a clue of no runs is done while the line is clear.
  EXPECT_EQ(g.rowDone(2), 1);
  g.set(20, Filled);
  EXPECT_EQ(g.rowDone(2), 0);
  g.undo();
  // A run with no clue left stops the count.
  g.set(30, Filled);
  g.set(31, Crossed);
  EXPECT_EQ(g.rowDone(3), 0);
  g.undo();
  g.undo();
  for (int x = 0; x < 10; ++x) g.set(10 + x, Filled);
  EXPECT_EQ(g.rowDone(1), 1);
  EXPECT_TRUE(g.isSolved());
}

TEST(Nonogram, SaveRoundTrip) {
  Game g(&fakeMillis);
  const auto cup = bitsOf(kCup);
  ASSERT_TRUE(g.newPicture(5, cup.data(), 7, "Чашка"));
  fakeNow = 0;
  g.resume();
  g.set(0, Filled);
  g.set(3, Crossed);
  fakeNow = 125000;
  g.pause();
  const std::string text = save(g);
  ASSERT_FALSE(text.empty());

  Game loaded(&fakeMillis);
  ASSERT_TRUE(loaded.deserialize(text.data(), text.size()));
  EXPECT_EQ(loaded.side(), 5);
  EXPECT_EQ(loaded.pictureId(), 7);
  EXPECT_STREQ(loaded.name(), "Чашка");
  EXPECT_EQ(loaded.source(), Source::Picture);
  EXPECT_EQ(loaded.elapsedMs(), 125000u);
  EXPECT_TRUE(loaded.paused());
  for (int i = 0; i < 25; ++i) {
    EXPECT_EQ(loaded.mark(i), g.mark(i));
    EXPECT_EQ(loaded.answer(i), cup[i] != 0);
  }

  Game random(&fakeMillis);
  ASSERT_TRUE(random.newRandom(15, 42));
  random.set(100, Crossed);
  const std::string randomText = save(random);
  Game back(&fakeMillis);
  ASSERT_TRUE(back.deserialize(randomText.data(), randomText.size()));
  EXPECT_EQ(back.source(), Source::Random);
  EXPECT_EQ(back.mark(100), Crossed);
  for (int i = 0; i < 225; ++i) EXPECT_EQ(back.answer(i), random.answer(i));

  std::string bad = text;
  bad.replace(bad.find("\"cells\":\"") + 9, 1, "3");
  EXPECT_FALSE(back.deserialize(bad.data(), bad.size()));
  EXPECT_EQ(back.source(), Source::Random);
}

TEST(Nonogram, GameSaveKeepsGameAndRecord) {
  fake::reset();
  Game g(&fakeMillis);
  ASSERT_TRUE(g.newRandom(10, 3));
  g.set(12, Filled);
  ASSERT_TRUE(gamesave::save(g));
  EXPECT_TRUE(gamesave::hasGameInProgress(gamesave::Kind::Nonogram));
  Game loaded(&fakeMillis);
  ASSERT_TRUE(gamesave::load(loaded));
  EXPECT_EQ(loaded.mark(12), Filled);

  Record record;
  EXPECT_TRUE(record.recordTime(Source::Picture, 15, 90000));
  EXPECT_FALSE(record.recordTime(Source::Picture, 15, 95000));
  EXPECT_TRUE(record.recordTime(Source::Random, 10, 30000));
  record.markSolved(200);
  record.markSolved(1);
  ASSERT_TRUE(gamesave::save(record));
  Record back;
  ASSERT_TRUE(gamesave::load(back));
  EXPECT_EQ(back.bestMs(Source::Picture, 15), 90000u);
  EXPECT_EQ(back.bestMs(Source::Picture, 10), 0u);
  EXPECT_EQ(back.bestMs(Source::Random, 10), 30000u);
  EXPECT_TRUE(back.isSolved(200));
  EXPECT_TRUE(back.isSolved(1));
  EXPECT_FALSE(back.isSolved(2));
}

namespace {
struct Collected {
  std::vector<Picture> pictures;
  size_t stopAfter = 100;
};
bool collect(void* ctx, const Picture& p) {
  auto* c = static_cast<Collected*>(ctx);
  c->pictures.push_back(p);
  return c->pictures.size() < c->stopAfter;
}
constexpr char kPack[] =
    "NONOGRAMS 1\n"
    "7 5 Чашка\n"
    "#####\n#####\n####.\n####.\n###..\n"
    "\n"
    "9 5 Two\r\n"
    "#....\r\n.#...\r\n.....\r\n.....\r\n.....\r\n";
}  // namespace

TEST(Nonogram, PackParsesInAnyChunks) {
  Collected c;
  PackParser parser(&collect, &c);
  for (const char ch : std::string(kPack)) parser.feed(&ch, 1);
  ASSERT_TRUE(parser.finish());
  ASSERT_EQ(c.pictures.size(), 2u);
  EXPECT_EQ(c.pictures[0].id, 7);
  EXPECT_EQ(c.pictures[0].side, 5);
  EXPECT_STREQ(c.pictures[0].name, "Чашка");
  EXPECT_EQ(memcmp(c.pictures[0].bits, bitsOf(kCup).data(), 25), 0);
  EXPECT_STREQ(c.pictures[1].name, "Two");
  EXPECT_EQ(c.pictures[1].bits[6], 1);

  Collected first;
  first.stopAfter = 1;
  PackParser stopped(&collect, &first);
  EXPECT_FALSE(stopped.feed(kPack, sizeof(kPack) - 1));
  EXPECT_TRUE(stopped.finish());
  EXPECT_EQ(first.pictures.size(), 1u);
}

TEST(Nonogram, PackRejectsBadInput) {
  for (const char* text :
       {"NONOGRAM 1\n", "NONOGRAMS 1\n0 5 x\n", "NONOGRAMS 1\n1 5 a\"b\n", "NONOGRAMS 1\n1 5 x\n#####\n####\n",
        "NONOGRAMS 1\n1 16 x\n", "NONOGRAMS 1\n1 5 x\n#####\n#####\n"}) {
    Collected c;
    PackParser parser(&collect, &c);
    parser.feed(text, strlen(text));
    EXPECT_FALSE(parser.finish()) << text;
  }
}
