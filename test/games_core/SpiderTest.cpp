#include <HalStorage.h>
#include <Spider.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "SpiderSolver.h"
#include "src/games/GameSave.h"

using namespace spider;
using cards::FACE_UP;
using cards::makeCard;

namespace {

constexpr uint8_t S = cards::SPADES;
constexpr uint8_t H = cards::HEARTS;

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

bool same(const Board& a, const Board& b) {
  for (int i = 0; i < COLUMNS; ++i) {
    if (a.column[i].count != b.column[i].count ||
        memcmp(a.column[i].cards, b.column[i].cards, a.column[i].count) != 0) {
      return false;
    }
  }
  return a.stockCount == b.stockCount && a.doneCount == b.doneCount;
}

uint8_t up(const int suit, const int rank) { return static_cast<uint8_t>(makeCard(suit, rank) | FACE_UP); }

void put(Board& b, const int column, const uint8_t card) { b.column[column].cards[b.column[column].count++] = card; }

// Every column holds a card, the stock is full.
Board filled() {
  Board b{};
  for (int c = 0; c < COLUMNS; ++c) put(b, c, up(H, 12));
  for (int i = 0; i < STOCK_SIZE; ++i) b.stock[i] = makeCard(S, i % cards::RANKS);
  b.stockCount = STOCK_SIZE;
  return b;
}

void play(Game& g, const std::vector<Step>& line) {
  for (const Step& s : line) ASSERT_TRUE(s.from == STOCK ? g.deal() : g.move(s.from, s.to, s.count));
}

}  // namespace

TEST(Spider, DealShapeAndSuits) {
  for (const int suits : {1, 2, 4}) {
    uint8_t deck[DECK];
    shuffledDeck(77, suits, deck);
    int perSuit[4] = {};
    for (const uint8_t card : deck) ++perSuit[cards::suitOf(card)];
    for (int s = 0; s < 4; ++s) EXPECT_EQ(perSuit[s], s < suits ? DECK / suits : 0) << suits;
    Board b;
    deal(b, deck);
    for (int c = 0; c < COLUMNS; ++c) {
      const Pile& p = b.column[c];
      EXPECT_EQ(p.count, c < 4 ? 6 : 5);
      for (int i = 0; i < p.count; ++i) EXPECT_EQ(cards::isFaceUp(p.cards[i]), i == p.count - 1);
    }
    EXPECT_EQ(b.stockCount, STOCK_SIZE);
  }
}

TEST(Spider, OnlySameSuitRunsMove) {
  Board b = filled();
  b.column[0].count = 0;
  put(b, 0, makeCard(S, 3));
  put(b, 0, up(S, 7));
  put(b, 0, up(S, 6));
  put(b, 0, up(H, 5));
  put(b, 1, up(H, 7));
  put(b, 2, up(S, 8));
  EXPECT_EQ(runLength(b, 0, 1), 0);
  EXPECT_EQ(runLength(b, 0, 2), 0);
  EXPECT_EQ(runLength(b, 0, 3), 1);
  EXPECT_EQ(runLength(b, 0, 0), 0);
  b.column[0].cards[3] = up(S, 5);
  EXPECT_EQ(runLength(b, 0, 1), 3);
  // Any suit takes a run one rank lower.
  EXPECT_TRUE(canMove(b, 0, 2, 3));
  EXPECT_FALSE(canMove(b, 0, 1, 3));
  EXPECT_TRUE(canMove(b, 0, 1, 2));
  ASSERT_TRUE(applyMove(b, 0, 2, 3));
  EXPECT_EQ(b.column[0].count, 1);
  EXPECT_TRUE(cards::isFaceUp(b.column[0].top()));
}

TEST(Spider, FinishedRunLeavesTheColumn) {
  Board b = filled();
  b.column[0].count = 0;
  put(b, 0, makeCard(H, 4));
  for (int rank = 12; rank >= 1; --rank) put(b, 0, up(S, rank));
  put(b, 1, up(S, 0));
  ASSERT_TRUE(applyMove(b, 1, 0, 1));
  EXPECT_EQ(b.doneCount, 1);
  EXPECT_EQ(b.done[0], S);
  EXPECT_EQ(b.column[0].count, 1);
  EXPECT_EQ(b.column[0].top(), up(H, 4));
  EXPECT_EQ(b.column[1].count, 1);
}

TEST(Spider, StockWaitsForEmptyColumns) {
  Board b = filled();
  EXPECT_TRUE(canDeal(b));
  b.column[3].count = 0;
  EXPECT_FALSE(canDeal(b));
  EXPECT_FALSE(applyDeal(b));
  EXPECT_EQ(b.stockCount, STOCK_SIZE);
  put(b, 3, up(S, 2));
  ASSERT_TRUE(applyDeal(b));
  EXPECT_EQ(b.stockCount, STOCK_SIZE - COLUMNS);
  for (const Pile& p : b.column) EXPECT_EQ(p.count, 2);
  b.stockCount = 0;
  EXPECT_FALSE(canDeal(b));
}

TEST(Spider, UndoReplaysAndWinEndsTheGame) {
  Game g;
  g.newGame(solvableSeed(1, 0), 1, false);
  std::vector<Step> line;
  ASSERT_TRUE(solve(g.board(), 300000, line));
  ASSERT_GT(line.size(), 20u);
  const Board start = g.board();
  play(g, std::vector<Step>(line.begin(), line.begin() + 12));
  const Board mid = g.board();
  const int midMoves = g.moveCount();
  play(g, std::vector<Step>(line.begin() + 12, line.begin() + 20));
  for (int i = 0; i < 8; ++i) ASSERT_TRUE(g.undo());
  EXPECT_TRUE(same(g.board(), mid));
  EXPECT_EQ(g.moveCount(), midMoves);
  while (g.canUndo()) g.undo();
  EXPECT_TRUE(same(g.board(), start));

  play(g, line);
  EXPECT_TRUE(g.isWon());
  EXPECT_FALSE(g.inProgress());
  EXPECT_FALSE(g.canUndo());
  EXPECT_EQ(g.board().doneCount, SETS);
  Step step;
  EXPECT_FALSE(g.hint(step));
}

TEST(Spider, FullHistoryTakesNoMoreMoves) {
  Game g;
  g.newGame(solvableSeed(1, 0), 1, false);
  std::vector<Step> line;
  ASSERT_TRUE(solve(g.board(), 300000, line));
  // Play until a single card can shuttle between its column and an empty one.
  int empty = -1;
  int single = -1;
  for (size_t i = 0; i < line.size() && (empty < 0 || single < 0); ++i) {
    const Step& s = line[i];
    ASSERT_TRUE(s.from == STOCK ? g.deal() : g.move(s.from, s.to, s.count));
    empty = single = -1;
    for (int c = 0; c < COLUMNS; ++c) {
      if (g.column(c).count == 0) empty = c;
      if (g.column(c).count == 1) single = c;
    }
  }
  ASSERT_GE(empty, 0);
  ASSERT_GE(single, 0);
  while (g.moveCount() < HISTORY) {
    ASSERT_TRUE(g.move(single, empty, 1));
    std::swap(single, empty);
  }
  EXPECT_TRUE(g.historyFull());
  EXPECT_FALSE(g.move(single, empty, 1));
  EXPECT_FALSE(g.deal());
  Step step;
  EXPECT_FALSE(g.hint(step));
  const std::string text = save(g);
  Game loaded;
  ASSERT_TRUE(loaded.deserialize(text.data(), text.size()));
  EXPECT_TRUE(loaded.historyFull());
  ASSERT_TRUE(g.undo());
  EXPECT_FALSE(g.historyFull());
  EXPECT_TRUE(g.move(empty, single, 1));
}

TEST(Spider, SolvableBankDealsAreWonThroughTheGame) {
  const auto t0 = std::chrono::steady_clock::now();
  int solved = 0;
  for (const int suits : {1, 2}) {
    ASSERT_GE(solvableDealCount(suits), 1);
    // Every 16th seed; the generator replayed them all through Game.
    for (int index = 0; index < solvableDealCount(suits); index += 16) {
      Game g;
      g.newGame(solvableSeed(suits, index), suits, false);
      std::vector<Step> line;
      ASSERT_TRUE(solve(g.board(), 300000, line)) << suits << " " << index;
      play(g, line);
      EXPECT_TRUE(g.isWon()) << suits << " " << index;
      ++solved;
    }
  }
  EXPECT_EQ(solvableDealCount(4), 0);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  printf("spider: %d bank deals re-solved, %.0f ms\n", solved, ms);
}

TEST(Spider, SolvableOnlyDealsFromTheBank) {
  Game g;
  g.newGame(12345, 2, true);
  EXPECT_TRUE(g.solvableOnly());
  bool found = false;
  for (int i = 0; i < solvableDealCount(2); ++i) found = found || solvableSeed(2, i) == g.seed();
  EXPECT_TRUE(found);
  g.newGame(12345, 4, true);
  EXPECT_FALSE(g.solvableOnly());
  EXPECT_EQ(g.seed(), 12345u);
}

TEST(Spider, HintsPlayAndSaveRoundTrips) {
  Game g;
  g.newGame(4242, 2, false);
  Step step;
  for (int i = 0; i < 30 && g.hint(step); ++i) {
    ASSERT_TRUE(step.from == STOCK ? g.deal() : g.move(step.from, step.to, step.count));
  }
  EXPECT_GT(g.moveCount(), 0);
  const std::string text = save(g);
  Game loaded;
  ASSERT_TRUE(loaded.deserialize(text.data(), text.size()));
  EXPECT_TRUE(same(loaded.board(), g.board()));
  EXPECT_EQ(loaded.suits(), 2);
  EXPECT_EQ(loaded.moveCount(), g.moveCount());

  std::string bad = text;
  bad.replace(bad.find("\"suits\":2"), 9, "\"suits\":3");
  EXPECT_FALSE(loaded.deserialize(bad.data(), bad.size()));
  bad = text;
  bad.replace(bad.find("\"v\":2"), 5, "\"v\":1");
  EXPECT_FALSE(loaded.deserialize(bad.data(), bad.size()));
  bad = text;
  bad[bad.find("\"moves\":\"") + 9] = 'b';  // from past the stock
  EXPECT_FALSE(loaded.deserialize(bad.data(), bad.size()));
  EXPECT_TRUE(same(loaded.board(), g.board()));

  fake::reset();
  ASSERT_TRUE(gamesave::save(g));
  Game fromFile;
  ASSERT_TRUE(gamesave::load(fromFile));
  EXPECT_TRUE(same(fromFile.board(), g.board()));
  fake::add("/.crosspoint/games/spider.json", bad);
  EXPECT_FALSE(gamesave::load(fromFile));
  EXPECT_FALSE(gamesave::hasGameInProgress(gamesave::Kind::Spider));

  cards::Best best;
  EXPECT_TRUE(best.record(2, 300));
  ASSERT_TRUE(gamesave::saveBest(gamesave::Kind::Spider, best));
  cards::Best read;
  ASSERT_TRUE(gamesave::loadBest(gamesave::Kind::Spider, read));
  EXPECT_EQ(read.moves[2], 300);
  EXPECT_EQ(read.moves[0], 0);
}
