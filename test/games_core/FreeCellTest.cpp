#include <FreeCell.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/games/GameSave.h"

using namespace freecell;
using cards::makeCard;

namespace {

constexpr uint8_t S = cards::SPADES;
constexpr uint8_t H = cards::HEARTS;
constexpr uint8_t D = cards::DIAMONDS;
constexpr uint8_t C = cards::CLUBS;

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

bool same(const Board& a, const Board& b) {
  for (int i = 0; i < PLACES; ++i) {
    if (a.pile[i].count != b.pile[i].count || memcmp(a.pile[i].cards, b.pile[i].cards, a.pile[i].count) != 0) {
      return false;
    }
  }
  return true;
}

void put(Board& b, const int place, const uint8_t card) { b.pile[place].cards[b.pile[place].count++] = card; }

// Depth-first over boards: foundations, columns, empty columns, then cells.
bool solve(const Board& b, std::set<std::string>& seen, std::vector<Step>& line) {
  if (isWon(b)) return true;
  std::string key;
  for (const Pile& p : b.pile) key.append(reinterpret_cast<const char*>(p.cards), p.count).push_back('/');
  if (line.size() > 300 || seen.size() > 200000 || !seen.insert(key).second) return false;
  for (int pass = 0; pass < 4; ++pass) {
    for (int from = 0; from < PLACES; ++from) {
      const int count = b.pile[from].count;
      for (int n = count; n > 0; --n) {
        if (runLength(b, from, count - n) != n) continue;
        for (int to = 0; to < PLACES; ++to) {
          const bool wanted = pass == 0   ? isFoundation(to)
                              : pass == 1 ? isColumn(to) && b.pile[to].count > 0
                              : pass == 2 ? isColumn(to) && b.pile[to].count == 0 && n < count
                                          : isCell(to) && isColumn(from);
          Board next = b;
          if (!wanted || !applyMove(next, from, to, n)) continue;
          autoPlay(next);
          line.push_back(Step{static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint8_t>(n)});
          if (solve(next, seen, line)) return true;
          line.pop_back();
        }
      }
    }
  }
  return false;
}

}  // namespace

TEST(FreeCell, DealOneMatchesMicrosoft) {
  Board b;
  deal(1, b);
  // Row one: JD 2D 9H JC 5D 7H 7C 5H; columns 1-4 get seven cards, the rest six.
  const uint8_t first[COLUMNS] = {makeCard(D, 10), makeCard(D, 1), makeCard(H, 8), makeCard(C, 10),
                                  makeCard(D, 4),  makeCard(H, 6), makeCard(C, 6), makeCard(H, 4)};
  for (int c = 0; c < COLUMNS; ++c) {
    EXPECT_EQ(b.pile[COLUMN + c].cards[0], first[c]) << c;
    EXPECT_EQ(b.pile[COLUMN + c].count, c < 4 ? 7 : 6);
  }
  EXPECT_EQ(b.pile[COLUMN].top(), makeCard(S, 5));
  EXPECT_EQ(b.pile[COLUMN + 7].top(), makeCard(C, 9));
}

TEST(FreeCell, EveryDealHoldsEachCardOnce) {
  for (const uint32_t n : {2u, 617u, UNSOLVABLE_DEAL, DEALS}) {
    Board b;
    deal(n, b);
    std::set<uint8_t> seen;
    for (int c = COLUMN; c < PLACES; ++c) {
      for (int i = 0; i < b.pile[c].count; ++i) seen.insert(b.pile[c].cards[i]);
    }
    EXPECT_EQ(seen.size(), 52u) << n;
  }
}

TEST(FreeCell, MovesFollowTheRules) {
  Board b{};
  put(b, COLUMN, makeCard(S, 7));
  put(b, COLUMN, makeCard(H, 6));
  put(b, COLUMN, makeCard(C, 5));
  put(b, COLUMN + 1, makeCard(D, 6));
  put(b, COLUMN + 2, makeCard(S, 0));
  put(b, CELL, makeCard(H, 8));
  put(b, FOUNDATION, makeCard(H, 0));
  EXPECT_EQ(runLength(b, COLUMN, 0), 3);
  EXPECT_EQ(runLength(b, FOUNDATION, 0), 0);
  EXPECT_TRUE(canMove(b, COLUMN, COLUMN + 1, 1));
  EXPECT_FALSE(canMove(b, COLUMN + 1, COLUMN, 1));
  EXPECT_TRUE(canMove(b, COLUMN + 2, FOUNDATION + 2, 1));
  EXPECT_FALSE(canMove(b, COLUMN + 1, FOUNDATION + 1, 1));
  EXPECT_TRUE(canMove(b, COLUMN + 1, CELL + 1, 1));
  EXPECT_FALSE(canMove(b, COLUMN, CELL + 1, 2));
  EXPECT_FALSE(canMove(b, COLUMN + 1, CELL, 1));
  EXPECT_FALSE(canMove(b, FOUNDATION, COLUMN + 3, 1));
  EXPECT_FALSE(canMove(b, CELL, COLUMN, 1));
  EXPECT_TRUE(canMove(b, COLUMN, COLUMN + 3, 3));
}

TEST(FreeCell, SupermoveIsLimitedByCellsAndEmptyColumns) {
  Board b{};
  put(b, COLUMN, makeCard(S, 9));
  for (int rank = 8; rank >= 4; --rank) put(b, COLUMN + 1, makeCard(rank % 2 == 0 ? H : C, rank));
  for (int c = COLUMN + 2; c < PLACES; ++c) put(b, c, makeCard(D, 12));
  for (int c = CELL; c < FOUNDATION; ++c) put(b, c, makeCard(D, c));
  EXPECT_EQ(moveLimit(b, COLUMN), 1);
  EXPECT_FALSE(canMove(b, COLUMN + 1, COLUMN, 5));
  b.pile[CELL].count = 0;
  b.pile[CELL + 1].count = 0;
  EXPECT_EQ(moveLimit(b, COLUMN), 3);
  EXPECT_FALSE(canMove(b, COLUMN + 1, COLUMN, 5));
  b.pile[COLUMN + 7].count = 0;
  EXPECT_EQ(moveLimit(b, COLUMN), 6);
  EXPECT_TRUE(canMove(b, COLUMN + 1, COLUMN, 5));
  // An empty target does not count toward its own limit.
  EXPECT_EQ(moveLimit(b, COLUMN + 7), 3);
  EXPECT_FALSE(canMove(b, COLUMN + 1, COLUMN + 7, 4));
  EXPECT_TRUE(canMove(b, COLUMN + 1, COLUMN + 7, 3));
}

TEST(FreeCell, SafeCardsGoUpAndOrderedColumnsFinish) {
  Board b{};
  put(b, FOUNDATION, makeCard(S, 0));
  put(b, FOUNDATION, makeCard(S, 1));
  put(b, FOUNDATION + 1, makeCard(H, 0));
  put(b, COLUMN, makeCard(D, 1));
  put(b, COLUMN, makeCard(S, 2));
  put(b, COLUMN + 1, makeCard(D, 0));
  autoPlay(b);
  // The ace goes up; 3S waits for both red twos.
  EXPECT_EQ(b.pile[COLUMN].count, 2);
  EXPECT_EQ(b.pile[COLUMN + 1].count, 0);

  Board full{};
  for (int suit = 0; suit < 4; ++suit) {
    for (int rank = 0; rank < cards::RANKS - 3; ++rank) put(full, FOUNDATION + suit, makeCard(suit, rank));
    for (int rank = cards::RANKS - 1; rank >= cards::RANKS - 3; --rank) put(full, COLUMN + suit, makeCard(suit, rank));
  }
  EXPECT_FALSE(isWon(full));
  autoPlay(full);
  EXPECT_TRUE(isWon(full));
}

TEST(FreeCell, MoveTakesThePartThatFits) {
  Game g;
  g.newGame(1);
  ASSERT_TRUE(g.move(COLUMN, CELL, 7));
  EXPECT_EQ(g.pile(CELL).count, 1);
  EXPECT_EQ(g.pile(COLUMN).count, 6);
  EXPECT_EQ(g.moveCount(), 1);
  EXPECT_FALSE(g.move(COLUMN + 1, CELL, 1));
}

TEST(FreeCell, UndoRestoresAndWinEndsTheGame) {
  Game h;
  h.newGame(1);
  const Board start = h.board();
  Step step;
  ASSERT_TRUE(h.hint(step));
  ASSERT_TRUE(h.move(step.from, step.to, step.count));
  ASSERT_TRUE(h.hint(step));
  ASSERT_TRUE(h.move(step.from, step.to, step.count));
  EXPECT_EQ(h.moveCount(), 2);
  ASSERT_TRUE(h.undo());
  ASSERT_TRUE(h.undo());
  EXPECT_TRUE(same(h.board(), start));
  EXPECT_FALSE(h.undo());

  Game g;
  g.newGame(1);
  std::set<std::string> seen;
  std::vector<Step> line;
  ASSERT_TRUE(solve(g.board(), seen, line));
  for (const Step& s : line) ASSERT_TRUE(g.move(s.from, s.to, s.count));
  EXPECT_TRUE(g.isWon());
  EXPECT_FALSE(g.inProgress());
  EXPECT_FALSE(g.canUndo());
  EXPECT_FALSE(g.hint(step));
}

TEST(FreeCell, FullHistoryTakesNoMoreMoves) {
  Game g;
  g.newGame(1);
  ASSERT_TRUE(g.move(COLUMN, CELL, 1));
  int from = CELL;
  int to = CELL + 1;
  while (g.moveCount() < HISTORY) {
    ASSERT_TRUE(g.move(from, to, 1));
    std::swap(from, to);
  }
  EXPECT_TRUE(g.historyFull());
  EXPECT_FALSE(g.move(from, to, 1));
  Step step;
  EXPECT_FALSE(g.hint(step));
  const std::string text = save(g);
  Game loaded;
  ASSERT_TRUE(loaded.deserialize(text.data(), text.size()));
  EXPECT_TRUE(loaded.historyFull());
  ASSERT_TRUE(g.undo());
  EXPECT_FALSE(g.historyFull());
  EXPECT_TRUE(g.hint(step));
}

TEST(FreeCell, SaveRoundTripAndTamperedSaves) {
  Game g;
  g.newGame(617);
  Step step;
  for (int i = 0; i < 6 && g.hint(step); ++i) ASSERT_TRUE(g.move(step.from, step.to, step.count));
  const std::string text = save(g);
  Game loaded;
  ASSERT_TRUE(loaded.deserialize(text.data(), text.size()));
  EXPECT_TRUE(same(loaded.board(), g.board()));
  EXPECT_EQ(loaded.dealNumber(), 617u);
  EXPECT_EQ(loaded.moveCount(), g.moveCount());

  const size_t moves = text.find("\"moves\":\"") + 9;
  std::string bad = text;
  bad[moves] = '4';  // from a foundation
  EXPECT_FALSE(loaded.deserialize(bad.data(), bad.size()));
  bad[moves] = 'z';
  EXPECT_FALSE(loaded.deserialize(bad.data(), bad.size()));
  std::string older = text;
  older.replace(older.find("\"v\":2"), 5, "\"v\":1");
  EXPECT_FALSE(loaded.deserialize(older.data(), older.size()));
  std::string zero = text;
  zero.replace(zero.find("\"deal\":617"), 10, "\"deal\":0");
  EXPECT_FALSE(loaded.deserialize(zero.data(), zero.size()));
  EXPECT_TRUE(same(loaded.board(), g.board()));

  fake::reset();
  ASSERT_TRUE(gamesave::save(g));
  EXPECT_TRUE(gamesave::hasGameInProgress(gamesave::Kind::FreeCell));
  Game fromFile;
  ASSERT_TRUE(gamesave::load(fromFile));
  EXPECT_TRUE(same(fromFile.board(), g.board()));
  fake::add("/.crosspoint/games/freecell.json", bad);
  EXPECT_FALSE(gamesave::load(fromFile));
  EXPECT_FALSE(gamesave::hasGameInProgress(gamesave::Kind::FreeCell));

  cards::Best best;
  EXPECT_TRUE(best.record(0, 90));
  EXPECT_FALSE(best.record(0, 95));
  EXPECT_TRUE(best.record(0, 80));
  ASSERT_TRUE(gamesave::saveBest(gamesave::Kind::FreeCell, best));
  cards::Best read;
  ASSERT_TRUE(gamesave::loadBest(gamesave::Kind::FreeCell, read));
  EXPECT_EQ(read.moves[0], 80);
  fake::add("/.crosspoint/games/freecell-best.json", "{\"best0\":");
  ASSERT_TRUE(gamesave::loadBest(gamesave::Kind::FreeCell, read));
  EXPECT_EQ(read.moves[0], 0);
}
