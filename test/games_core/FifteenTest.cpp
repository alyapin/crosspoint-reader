#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "Fifteen.h"

using namespace fifteen;

namespace {

uint32_t fakeNow = 0;
uint32_t fakeMillis() { return fakeNow; }

constexpr uint8_t kSolved[CELLS] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0};

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

void board(const Game& g, uint8_t out[CELLS]) {
  for (int cell = 0; cell < CELLS; ++cell) out[cell] = g.tile(cell);
}

}  // namespace

TEST(Fifteen, SolvabilityFollowsParity) {
  EXPECT_TRUE(isSolvable(kSolved));
  uint8_t swapped[CELLS];
  memcpy(swapped, kSolved, CELLS);
  swapped[13] = 15;
  swapped[14] = 14;
  EXPECT_FALSE(isSolvable(swapped));
  Game g(&fakeMillis);
  EXPECT_FALSE(g.newGame(swapped));
  uint8_t duplicate[CELLS];
  memcpy(duplicate, kSolved, CELLS);
  duplicate[0] = 2;
  EXPECT_FALSE(g.newGame(duplicate));
}

TEST(Fifteen, RandomWalksStaySolvable) {
  Game g(&fakeMillis);
  g.newGame(7);
  games::Rng rng(7);
  int slides = 0;
  for (int i = 0; i < 20000; ++i) {
    if (!g.slide(static_cast<int>(rng.below(CELLS)))) continue;
    ++slides;
    uint8_t tiles[CELLS];
    board(g, tiles);
    ASSERT_TRUE(isSolvable(tiles)) << i;
    if (g.isSolved()) g.newGame(static_cast<uint32_t>(i));
  }
  EXPECT_GT(slides, 5000);
}

TEST(Fifteen, ShufflesAreSolvablePermutationsAndNeverSolved) {
  Game g(&fakeMillis);
  for (uint32_t seed = 1; seed <= 2000; ++seed) {
    g.newGame(seed * 2654435761u);
    uint8_t tiles[CELLS];
    board(g, tiles);
    bool seen[CELLS] = {};
    for (const uint8_t t : tiles) {
      ASSERT_LT(t, CELLS);
      ASSERT_FALSE(seen[t]);
      seen[t] = true;
    }
    EXPECT_TRUE(isSolvable(tiles)) << seed;
    EXPECT_FALSE(g.isSolved());
    EXPECT_EQ(g.moveCount(), 0);
    EXPECT_EQ(g.tile(g.gap()), 0);
  }
  Game a(&fakeMillis);
  Game b(&fakeMillis);
  a.newGame(42);
  b.newGame(42);
  EXPECT_EQ(save(a), save(b));
}

TEST(Fifteen, SlidesOnlyAlongTheGapRowOrColumn) {
  const uint8_t start[CELLS] = {1, 2, 3, 4, 5, 0, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  Game g(&fakeMillis);
  ASSERT_TRUE(g.newGame(start));
  ASSERT_EQ(g.gap(), 5);
  EXPECT_FALSE(g.canSlide(5));
  EXPECT_FALSE(g.canSlide(0));
  EXPECT_FALSE(g.canSlide(10));
  EXPECT_FALSE(g.canSlide(-1));
  EXPECT_FALSE(g.canSlide(CELLS));
  for (const int cell : {1, 9, 13, 4, 6, 7}) EXPECT_TRUE(g.canSlide(cell)) << cell;
  EXPECT_FALSE(g.slide(10));
  EXPECT_EQ(g.moveCount(), 0);

  // Row: 6 and 7 shift left, the gap lands on 7.
  ASSERT_TRUE(g.slide(7));
  EXPECT_EQ(g.gap(), 7);
  EXPECT_EQ(g.tile(5), start[6]);
  EXPECT_EQ(g.tile(6), start[7]);
  EXPECT_EQ(g.moveCount(), 1);

  // Column: 11 and 15 shift up, the gap lands on 15.
  ASSERT_TRUE(g.slide(15));
  EXPECT_EQ(g.gap(), 15);
  EXPECT_EQ(g.tile(7), start[11]);
  EXPECT_EQ(g.tile(11), start[15]);
  EXPECT_EQ(g.moveCount(), 2);

  // And back down in one move.
  ASSERT_TRUE(g.slide(3));
  EXPECT_EQ(g.gap(), 3);
  EXPECT_EQ(g.tile(7), start[3]);
  EXPECT_EQ(g.tile(11), start[11]);
  EXPECT_EQ(g.tile(15), start[15]);
  EXPECT_EQ(g.moveCount(), 3);
}

TEST(Fifteen, LastSlideWinsAndStopsTheClock) {
  const uint8_t tiles[CELLS] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 13, 14, 15};
  Game g(&fakeMillis);
  ASSERT_TRUE(g.newGame(tiles));
  fakeNow = 1000;
  g.resume();
  fakeNow = 61000;
  EXPECT_FALSE(g.isSolved());
  ASSERT_TRUE(g.slide(15));
  EXPECT_TRUE(g.isSolved());
  EXPECT_FALSE(g.inProgress());
  EXPECT_EQ(g.moveCount(), 1);
  fakeNow = 200000;
  EXPECT_EQ(g.elapsedMs(), 60000u);
  EXPECT_FALSE(g.canSlide(14));
  EXPECT_FALSE(g.slide(14));
  g.resume();
  EXPECT_TRUE(g.paused());
  EXPECT_EQ(g.moveCount(), 1);
}

TEST(Fifteen, SaveRoundTripsPaused) {
  Game g(&fakeMillis);
  g.newGame(99);
  fakeNow = 5000;
  g.resume();
  for (int cell = 0; cell < CELLS; ++cell) g.slide(cell);
  fakeNow = 95000;
  g.pause();
  const std::string text = save(g);
  ASSERT_FALSE(text.empty());

  Game r(&fakeMillis);
  ASSERT_TRUE(r.deserialize(text.data(), text.size()));
  EXPECT_TRUE(r.paused());
  EXPECT_EQ(r.elapsedMs(), 90000u);
  EXPECT_EQ(r.moveCount(), g.moveCount());
  EXPECT_EQ(r.gap(), g.gap());
  for (int cell = 0; cell < CELLS; ++cell) EXPECT_EQ(r.tile(cell), g.tile(cell));

  const size_t at = text.find("\"tiles\":\"") + 9;
  for (const char* tiles : {"123456789abcdfe0", "1123456789abcde0", "123456789abcdeg0"}) {
    const std::string bad = std::string(text).replace(at, CELLS, tiles);
    EXPECT_FALSE(r.deserialize(bad.data(), bad.size())) << tiles;
  }
  EXPECT_FALSE(r.deserialize("{}", 2));
  EXPECT_EQ(save(r), save(g));
}

TEST(Fifteen, BestKeepsFewestMovesThenShortestTime) {
  Best best;
  EXPECT_FALSE(best.record(0, 1000));
  EXPECT_TRUE(best.record(80, 300000));
  EXPECT_FALSE(best.record(90, 1000));
  EXPECT_FALSE(best.record(80, 300000));
  EXPECT_TRUE(best.record(80, 200000));
  EXPECT_TRUE(best.record(60, 900000));
  EXPECT_EQ(best.moves, 60);
  EXPECT_EQ(best.ms, 900000u);

  char text[64];
  const size_t len = best.serialize(text, sizeof(text));
  Best restored;
  ASSERT_TRUE(restored.deserialize(text, len));
  EXPECT_EQ(restored.moves, 60);
  EXPECT_EQ(restored.ms, 900000u);
}
