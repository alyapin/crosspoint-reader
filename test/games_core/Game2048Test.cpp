#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "Game2048.h"

using namespace g2048;

namespace {

// Rows top to bottom, one letter per cell: 'a' empty, 'b' = 2, 'c' = 4, ...
Game fromCells(const char* cells, const uint32_t score = 0, const uint32_t seed = 7) {
  Game g;
  char text[160];
  snprintf(text, sizeof(text), R"({"v":1,"seed":%u,"draws":0,"score":%u,"cells":"%s","undo":0})", seed, score, cells);
  EXPECT_TRUE(g.deserialize(text, strlen(text))) << text;
  return g;
}

std::string cells(const Game& g) {
  std::string s;
  for (int i = 0; i < CELLS; ++i) s += static_cast<char>('a' + g.exponent(i));
  return s;
}

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

int tiles(const Game& g) {
  int n = 0;
  for (int i = 0; i < CELLS; ++i) n += g.exponent(i) != 0;
  return n;
}

// `after` equals `expected` except for one spawned 2 or 4 on a cell `expected` leaves empty.
void expectSlidAndSpawned(const Game& g, const std::string& expected) {
  const std::string got = cells(g);
  int spawned = 0;
  for (int i = 0; i < CELLS; ++i) {
    if (got[i] == expected[i]) continue;
    EXPECT_EQ(expected[i], 'a') << "cell " << i << " in " << got;
    EXPECT_TRUE(got[i] == 'b' || got[i] == 'c') << "cell " << i << " in " << got;
    ++spawned;
  }
  EXPECT_EQ(spawned, 1) << got;
}

}  // namespace

TEST(Game2048, NewGameDealsTwoSmallTiles) {
  Game g;
  g.newGame(42);
  EXPECT_EQ(tiles(g), 2);
  for (int i = 0; i < CELLS; ++i) EXPECT_LE(g.exponent(i), 2);
  EXPECT_EQ(g.score(), 0u);
  EXPECT_FALSE(g.canUndo());
  EXPECT_TRUE(g.inProgress());

  Game same;
  same.newGame(42);
  EXPECT_EQ(cells(same), cells(g));
}

TEST(Game2048, SeedsDealDifferentBoards) {
  Game a;
  a.newGame(1);
  int differ = 0;
  for (uint32_t seed = 2; seed < 20; ++seed) {
    Game b;
    b.newGame(seed);
    differ += cells(a) != cells(b);
  }
  EXPECT_GT(differ, 10);
}

TEST(Game2048, MovesLeft) {
  Game g = fromCells("bbccaaaaaaaaaaaa");
  ASSERT_TRUE(g.move(Dir::Left));
  expectSlidAndSpawned(g, "cdaaaaaaaaaaaaaa");
  EXPECT_EQ(g.score(), 4u + 8u);
}

TEST(Game2048, FourEqualTilesMergeIntoTwoPairs) {
  Game g = fromCells("bbbbaaaaaaaaaaaa");
  ASSERT_TRUE(g.move(Dir::Left));
  expectSlidAndSpawned(g, "ccaaaaaaaaaaaaaa");
  EXPECT_EQ(g.score(), 8u);
}

TEST(Game2048, MergedTileDoesNotMergeAgain) {
  Game g = fromCells("cbbaaaaaaaaaaaaa");
  ASSERT_TRUE(g.move(Dir::Left));
  expectSlidAndSpawned(g, "ccaaaaaaaaaaaaaa");
  EXPECT_EQ(g.score(), 4u);
}

TEST(Game2048, ThreeEqualTilesMergeTheOnesNearTheWall) {
  Game left = fromCells("bbbaaaaaaaaaaaaa");
  ASSERT_TRUE(left.move(Dir::Left));
  expectSlidAndSpawned(left, "cbaaaaaaaaaaaaaa");

  Game right = fromCells("bbbaaaaaaaaaaaaa");
  ASSERT_TRUE(right.move(Dir::Right));
  expectSlidAndSpawned(right, "aabcaaaaaaaaaaaa");
}

TEST(Game2048, MovesRight) {
  Game g = fromCells("abacaaaaddaaaaaa");
  ASSERT_TRUE(g.move(Dir::Right));
  expectSlidAndSpawned(g, "aabcaaaaaaaeaaaa");
  EXPECT_EQ(g.score(), 16u);
}

TEST(Game2048, MovesUp) {
  Game g = fromCells("baaaaaaabaaacaaa");
  ASSERT_TRUE(g.move(Dir::Up));
  expectSlidAndSpawned(g, "caaacaaaaaaaaaaa");
  EXPECT_EQ(g.score(), 4u);
}

TEST(Game2048, MovesDown) {
  Game g = fromCells("abaaacaaacaaadaa");
  ASSERT_TRUE(g.move(Dir::Down));
  expectSlidAndSpawned(g, "aaaaabaaadaaadaa");
  EXPECT_EQ(g.score(), 8u);
}

TEST(Game2048, BlockedMoveChangesNothing) {
  Game g = fromCells("bcaaaaaaaaaaaaaa", 10);
  EXPECT_FALSE(g.move(Dir::Left));
  EXPECT_FALSE(g.move(Dir::Up));
  EXPECT_EQ(cells(g), "bcaaaaaaaaaaaaaa");
  EXPECT_EQ(g.score(), 10u);
  EXPECT_FALSE(g.canUndo());
}

TEST(Game2048, SpawnsAboutOneFourInTen) {
  int fours = 0;
  constexpr int kRuns = 2000;
  for (int seed = 1; seed <= kRuns; ++seed) {
    Game g = fromCells("baaaaaaaaaaaaaaa", 0, static_cast<uint32_t>(seed));
    ASSERT_TRUE(g.move(Dir::Right));
    for (int i = 0; i < CELLS; ++i) fours += g.exponent(i) == 2;
  }
  EXPECT_GT(fours, kRuns / 20);
  EXPECT_LT(fours, kRuns / 5);
}

TEST(Game2048, UndoRestoresTheBoardBeforeTheSpawn) {
  Game g = fromCells("bbaaaaaaaaaaaaaa", 30);
  ASSERT_TRUE(g.move(Dir::Left));
  const std::string afterMove = cells(g);
  ASSERT_TRUE(g.canUndo());
  ASSERT_TRUE(g.undo());
  EXPECT_EQ(cells(g), "bbaaaaaaaaaaaaaa");
  EXPECT_EQ(g.score(), 30u);
  EXPECT_FALSE(g.canUndo());
  EXPECT_FALSE(g.undo());

  // Undo cannot re-roll the spawn.
  ASSERT_TRUE(g.move(Dir::Left));
  EXPECT_EQ(cells(g), afterMove);
}

TEST(Game2048, UndoIsOneStep) {
  Game g = fromCells("bbaaccaaaaaaaaaa");
  ASSERT_TRUE(g.move(Dir::Left));
  const std::string first = cells(g);
  ASSERT_TRUE(g.move(Dir::Right));
  ASSERT_TRUE(g.undo());
  EXPECT_EQ(cells(g), first);
  EXPECT_FALSE(g.undo());
}

TEST(Game2048, ReachingTwoThousandFortyEightWinsOnceAndPlayGoesOn) {
  Game g = fromCells("kkaaaaaaaaaaaaaa");
  EXPECT_FALSE(g.justWon());
  ASSERT_TRUE(g.move(Dir::Left));
  EXPECT_EQ(g.exponent(0), WIN_EXPONENT);
  EXPECT_EQ(g.score(), 2048u);
  EXPECT_TRUE(g.justWon());
  EXPECT_TRUE(g.inProgress());

  ASSERT_TRUE(g.undo());
  EXPECT_FALSE(g.justWon());
  ASSERT_TRUE(g.move(Dir::Left));
  EXPECT_TRUE(g.justWon());

  ASSERT_TRUE(g.move(Dir::Down) || g.move(Dir::Right));
  EXPECT_FALSE(g.justWon());
}

TEST(Game2048, SecondTwoThousandFortyEightIsNoNewWin) {
  Game g = fromCells("lkkaaaaaaaaaaaaa");
  ASSERT_TRUE(g.move(Dir::Left));
  EXPECT_FALSE(g.justWon());
}

TEST(Game2048, FullBoardWithoutPairsIsOver) {
  Game g = fromCells("bcbccbcbbcbccbcb", 500);
  EXPECT_TRUE(g.isOver());
  EXPECT_FALSE(g.inProgress());
  for (const Dir d : {Dir::Left, Dir::Right, Dir::Up, Dir::Down}) EXPECT_FALSE(g.move(d));
  EXPECT_EQ(g.score(), 500u);
}

TEST(Game2048, FullBoardWithAPairCanMove) {
  EXPECT_FALSE(fromCells("bcbccbcbbcbbcbcb").isOver());
  EXPECT_FALSE(fromCells("bcbccbcbcbcbcbcb").isOver());
}

TEST(Game2048, LastSpawnCanEndTheGame) {
  bool sawOver = false;
  for (uint32_t seed = 1; seed < 200 && !sawOver; ++seed) {
    Game g = fromCells("bcbccbcbbcbcacbd", 0, seed);
    if (!g.move(Dir::Left)) continue;
    sawOver = g.isOver();
  }
  EXPECT_TRUE(sawOver);
}

TEST(Game2048, EmptyBoardIsNotInProgress) {
  Game g;
  EXPECT_FALSE(g.inProgress());
}

TEST(Game2048, SaveRoundTripReplaysTheSameSpawns) {
  Game g;
  g.newGame(1234);
  const Dir moves[] = {Dir::Left, Dir::Up, Dir::Right, Dir::Down, Dir::Left, Dir::Up};
  for (const Dir d : moves) g.move(d);
  const std::string text = save(g);
  ASSERT_FALSE(text.empty());

  Game restored;
  ASSERT_TRUE(restored.deserialize(text.data(), text.size()));
  EXPECT_EQ(cells(restored), cells(g));
  EXPECT_EQ(restored.score(), g.score());
  EXPECT_EQ(restored.canUndo(), g.canUndo());

  for (const Dir d : {Dir::Down, Dir::Left, Dir::Up, Dir::Right, Dir::Down}) {
    EXPECT_EQ(restored.move(d), g.move(d));
    EXPECT_EQ(cells(restored), cells(g));
  }
  ASSERT_TRUE(g.undo());
  ASSERT_TRUE(restored.undo());
  EXPECT_EQ(cells(restored), cells(g));
  EXPECT_EQ(restored.score(), g.score());
}

TEST(Game2048, SaveKeepsTheUndoStep) {
  Game g = fromCells("bbaaaaaaaaaaaaaa", 6);
  ASSERT_TRUE(g.move(Dir::Left));
  const std::string text = save(g);
  Game restored;
  ASSERT_TRUE(restored.deserialize(text.data(), text.size()));
  ASSERT_TRUE(restored.undo());
  EXPECT_EQ(cells(restored), "bbaaaaaaaaaaaaaa");
  EXPECT_EQ(restored.score(), 6u);
}

TEST(Game2048, RejectsBadSaves) {
  Game g = fromCells("bcaaaaaaaaaaaaaa", 4);
  const char* bad[] = {
      R"({"v":2,"seed":1,"draws":0,"score":0,"cells":"bcaaaaaaaaaaaaaa","undo":0})",
      R"({"v":1,"seed":1,"draws":0,"score":0,"cells":"bcaaaaaaaaaaaaa","undo":0})",
      R"({"v":1,"seed":1,"draws":0,"score":0,"cells":"bcaaaaaaaaaaaaaZ","undo":0})",
      R"({"v":1,"seed":1,"draws":0,"score":0,"cells":"bcaaaaaaaaaaaaaa","undo":1})",
      (R"({"v":1,"seed":1,"draws":2,"score":0,"cells":"bcaaaaaaaaaaaaaa","undo":1,"uscore":0,"udraws":4,)"
       R"("ucells":"bcaaaaaaaaaaaaaa"})"),
      R"({"v":1,"seed":1,"draws":99999999,"score":0,"cells":"bcaaaaaaaaaaaaaa","undo":0})",
      "",
  };
  for (const char* text : bad) {
    EXPECT_FALSE(g.deserialize(text, strlen(text))) << text;
    EXPECT_EQ(cells(g), "bcaaaaaaaaaaaaaa");
    EXPECT_EQ(g.score(), 4u);
  }
}
