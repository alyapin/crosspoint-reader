#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Klondike.h"
#include "KlondikeSolver.h"

using namespace klondike;

namespace {

uint32_t fakeNow = 0;
uint32_t fakeMillis() { return fakeNow; }

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

bool same(const Board& a, const Board& b) {
  for (int i = 0; i < PILES; ++i) {
    if (a.pile[i].count != b.pile[i].count || memcmp(a.pile[i].cards, b.pile[i].cards, a.pile[i].count) != 0) {
      return false;
    }
  }
  return true;
}

uint8_t up(const int suit, const int rank) { return static_cast<uint8_t>(makeCard(suit, rank) | FACE_UP); }

void put(Board& b, const int pile, const uint8_t card) { b.pile[pile].cards[b.pile[pile].count++] = card; }

// Plays a pseudo-random legal move; false when there is none.
bool randomMove(Game& g, games::Rng& rng) {
  struct Option {
    int from, to, count;
  };
  Option options[400];
  int n = 0;
  for (int from = WASTE; from < PILES; ++from) {
    const Pile& p = g.pile(from);
    for (int i = 0; i < p.count; ++i) {
      const int count = g.runLength(from, i);
      if (count == 0) continue;
      for (int to = FOUNDATION; to < PILES; ++to) {
        if (g.canMove(from, to, count)) options[n++] = {from, to, count};
      }
    }
  }
  const int pick = static_cast<int>(rng.below(n + 2));
  if (pick >= n) return g.draw();
  return g.move(options[pick].from, options[pick].to, options[pick].count);
}

void replay(Game& g, const std::vector<SolverStep>& line) {
  for (const SolverStep& s : line) ASSERT_TRUE(s.draw ? g.draw() : g.move(s.from, s.to, s.count));
}

}  // namespace

TEST(Klondike, ShuffleIsASeededPermutation) {
  uint8_t a[DECK];
  uint8_t b[DECK];
  shuffledDeck(5, a);
  shuffledDeck(5, b);
  EXPECT_EQ(memcmp(a, b, DECK), 0);
  shuffledDeck(6, b);
  EXPECT_NE(memcmp(a, b, DECK), 0);
  bool seen[DECK] = {};
  for (const uint8_t c : a) seen[suitOf(c) * RANKS + rankOf(c)] = true;
  for (const bool s : seen) EXPECT_TRUE(s);
}

TEST(Klondike, DealShape) {
  Game g(fakeMillis);
  g.newGame(12, false, false);
  EXPECT_TRUE(isConsistent(g.board()));
  EXPECT_EQ(g.pile(STOCK).count, 24);
  EXPECT_EQ(g.pile(WASTE).count, 0);
  for (int t = 0; t < TABLEAU_PILES; ++t) {
    const Pile& p = g.pile(TABLEAU + t);
    ASSERT_EQ(p.count, t + 1);
    for (int c = 0; c < p.count; ++c) EXPECT_EQ(isFaceUp(p.cards[c]), c == t);
  }
  EXPECT_EQ(g.pile(-1).count, 0);
  EXPECT_EQ(g.pile(PILES).count, 0);
}

TEST(Klondike, MoveRules) {
  Board b{};
  put(b, TABLEAU + 0, makeCard(SPADES, 5));
  put(b, TABLEAU + 0, up(HEARTS, 8));
  put(b, TABLEAU + 0, up(CLUBS, 7));
  put(b, TABLEAU + 0, up(DIAMONDS, 6));
  put(b, TABLEAU + 1, up(SPADES, 9));
  put(b, TABLEAU + 2, up(HEARTS, 0));
  put(b, TABLEAU + 3, up(CLUBS, 12));
  put(b, WASTE, up(DIAMONDS, 7));
  put(b, FOUNDATION, up(SPADES, 0));
  put(b, STOCK, makeCard(CLUBS, 1));

  EXPECT_EQ(runLength(b, TABLEAU, 0), 0);  // face down
  EXPECT_EQ(runLength(b, TABLEAU, 1), 3);
  EXPECT_EQ(runLength(b, TABLEAU, 3), 1);
  EXPECT_EQ(runLength(b, WASTE, 0), 1);
  EXPECT_TRUE(canMove(b, TABLEAU, TABLEAU + 1, 3));         // red 8 on black 9
  EXPECT_FALSE(canMove(b, TABLEAU, TABLEAU + 1, 2));        // black 7 on black 9
  EXPECT_FALSE(canMove(b, TABLEAU, TABLEAU + 1, 4));        // includes a face-down card
  EXPECT_FALSE(canMove(b, WASTE, TABLEAU + 1, 1));          // 7 on 9
  EXPECT_TRUE(canMove(b, TABLEAU + 2, FOUNDATION + 1, 1));  // ace to an empty foundation
  EXPECT_FALSE(canMove(b, TABLEAU + 2, FOUNDATION, 1));     // heart on spades
  EXPECT_FALSE(canMove(b, TABLEAU, FOUNDATION + 1, 3));     // one card at a time
  EXPECT_FALSE(canMove(b, TABLEAU + 1, TABLEAU + 4, 1));    // empty column takes a king only
  EXPECT_TRUE(canMove(b, TABLEAU + 3, TABLEAU + 4, 1));
  EXPECT_FALSE(canMove(b, TABLEAU + 3, WASTE, 1));
  EXPECT_FALSE(canMove(b, TABLEAU + 3, STOCK, 1));
  EXPECT_FALSE(canMove(b, STOCK, TABLEAU + 1, 1));
  EXPECT_FALSE(canMove(b, TABLEAU, TABLEAU, 1));
  EXPECT_FALSE(canMove(b, TABLEAU + 3, TABLEAU + 4, 0));

  Move m;
  Board before = b;
  ASSERT_TRUE(applyMove(b, TABLEAU, TABLEAU + 1, 3, m));
  EXPECT_EQ(m.flags, Move::FLIPPED);
  EXPECT_TRUE(isFaceUp(b.pile[TABLEAU].top()));
  ASSERT_TRUE(revert(b, m));
  EXPECT_TRUE(same(b, before));
  EXPECT_FALSE(isFaceUp(b.pile[TABLEAU].cards[0]));
  EXPECT_FALSE(applyMove(b, TABLEAU, TABLEAU + 1, 2, m));
  EXPECT_TRUE(same(b, before));
}

TEST(Klondike, StockAndWasteHoldAtMostTheDealtStock) {
  Game g(fakeMillis);
  g.newGame(4, true, false);
  Board b = g.board();
  // The bottom card of the last column moved to the waste: 25 cards in stock + waste.
  Pile& column = b.pile[PILES - 1];
  put(b, WASTE, static_cast<uint8_t>(column.cards[0] | FACE_UP));
  memmove(column.cards, column.cards + 1, --column.count);
  EXPECT_FALSE(isConsistent(b));

  // A full waste never overflows on a draw.
  Board full{};
  for (int i = 0; i < PILE_CAPACITY; ++i) put(full, WASTE, up(i / RANKS, i % RANKS));
  put(full, STOCK, makeCard(CLUBS, 12));
  Move m;
  EXPECT_FALSE(applyDraw(full, true, m));
  EXPECT_EQ(full.pile[WASTE].count, PILE_CAPACITY);
}

TEST(Klondike, DrawOneAndThreeWithRecycle) {
  for (const bool three : {false, true}) {
    Game g(fakeMillis);
    g.newGame(31, three, false);
    const Board start = g.board();
    int draws = 0;
    while (g.pile(STOCK).count > 0) {
      const int before = g.pile(WASTE).count;
      ASSERT_TRUE(g.draw());
      EXPECT_EQ(g.pile(WASTE).count - before, three ? 3 : 1);
      EXPECT_TRUE(isFaceUp(g.pile(WASTE).top()));
      ++draws;
    }
    EXPECT_EQ(draws, three ? 8 : 24);
    EXPECT_EQ(g.pile(WASTE).count, 24);
    ASSERT_TRUE(g.draw());  // recycle
    EXPECT_EQ(g.pile(WASTE).count, 0);
    EXPECT_TRUE(same(g.board(), start));  // the same order comes round again
    EXPECT_EQ(g.moveCount(), draws + 1);
    while (g.undo()) {
    }
    EXPECT_TRUE(same(g.board(), start));
    EXPECT_EQ(g.moveCount(), 0);
  }
  Board empty{};
  Move m;
  EXPECT_FALSE(applyDraw(empty, false, m));
}

TEST(Klondike, RandomPlayUndoesBackToTheDeal) {
  for (uint32_t seed = 1; seed <= 30; ++seed) {
    Game g(fakeMillis);
    g.newGame(seed, seed % 2 == 0, false);
    const Board start = g.board();
    games::Rng rng(seed);
    int played = 0;
    for (int i = 0; i < 150 && !g.isWon(); ++i) {
      played += randomMove(g, rng);
      ASSERT_TRUE(isConsistent(g.board()));
    }
    ASSERT_LE(played, UNDO_DEPTH);
    int undone = 0;
    while (g.undo()) {
      ASSERT_TRUE(isConsistent(g.board()));
      ++undone;
    }
    EXPECT_EQ(undone, played);
    EXPECT_TRUE(same(g.board(), start));
  }
}

TEST(Klondike, HintIsAlwaysLegal) {
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    Game g(fakeMillis);
    g.newGame(seed, seed % 3 == 0, false);
    games::Rng rng(seed * 3);
    for (int i = 0; i < 120 && !g.isWon(); ++i) {
      Move m;
      ASSERT_TRUE(g.hint(m));
      if (m.flags == Move::DRAW) {
        EXPECT_TRUE(g.pile(STOCK).count > 0 || g.pile(WASTE).count > 0);
      } else {
        EXPECT_TRUE(g.canMove(m.from, m.to, m.count)) << int(m.from) << "->" << int(m.to) << " x" << int(m.count);
      }
      randomMove(g, rng);
    }
  }
}

TEST(Klondike, AutoMovesOnlySafeCardsAndFinishes) {
  Board b{};
  // Spades to 2, hearts to ace; the black 3 is not safe while red 2s are missing.
  put(b, FOUNDATION, up(SPADES, 0));
  put(b, FOUNDATION, up(SPADES, 1));
  put(b, FOUNDATION + 1, up(HEARTS, 0));
  put(b, TABLEAU, up(SPADES, 2));
  put(b, TABLEAU + 1, up(DIAMONDS, 0));
  EXPECT_FALSE(isSafeToFoundation(b, up(SPADES, 2)));
  EXPECT_TRUE(isSafeToFoundation(b, up(DIAMONDS, 0)));
  EXPECT_TRUE(isSafeToFoundation(b, up(CLUBS, 1)));

  // Solver lines into a position where every remaining card is face up in columns.
  Game g(fakeMillis);
  bool finished = false;
  for (uint32_t seed = 1; seed <= 40 && !finished; ++seed) {
    g.newGame(seed, false, true);
    std::vector<SolverStep> line;
    ASSERT_TRUE(solve(g.board(), false, 200000, line));
    // Play until the stock and waste are spent and nothing is hidden.
    size_t i = 0;
    while (i < line.size() && !g.canAutoFinish() && !g.isWon()) {
      const SolverStep& s = line[i++];
      ASSERT_TRUE(s.draw ? g.draw() : g.move(s.from, s.to, s.count));
    }
    if (g.isWon()) continue;
    ASSERT_TRUE(g.canAutoFinish()) << seed;
    EXPECT_GT(g.autoFinish(), 0);
    EXPECT_TRUE(g.isWon());
    EXPECT_FALSE(g.canUndo());
    EXPECT_FALSE(g.draw());
    finished = true;
  }
  EXPECT_TRUE(finished);
  g.newGame(77, false, false);
  const int moved = g.autoMoveSafe();
  for (int i = 0; i < moved; ++i) EXPECT_TRUE(g.undo());
  EXPECT_FALSE(g.canUndo());
}

TEST(Klondike, SolvableBankDealsAreWonThroughTheGame) {
  const auto t0 = std::chrono::steady_clock::now();
  int solved = 0;
  for (const bool three : {false, true}) {
    for (int index = 0; index < solvableDealCount(three); index += 17) {
      for (const int variant : {0, 5}) {
        uint8_t deck[DECK];
        solvableDeck(three, index, variant, deck);
        Board board;
        deal(board, deck);
        std::vector<SolverStep> line;
        ASSERT_TRUE(solve(board, three, 400000, line)) << three << " " << index << " " << variant;
        for (const SolverStep& step : line) {
          Move m;
          ASSERT_TRUE(step.draw ? applyDraw(board, three, m) : applyMove(board, step.from, step.to, step.count, m));
        }
        EXPECT_TRUE(isWon(board));
        ++solved;
      }
    }
  }
  // newGame(seed, solvable) plays its own deal to a win.
  for (const bool three : {false, true}) {
    for (uint32_t seed = 1; seed <= 12; ++seed) {
      fakeNow = 1000;
      Game g(fakeMillis);
      g.newGame(seed, three, true);
      std::vector<SolverStep> line;
      ASSERT_TRUE(solve(g.board(), three, 400000, line));
      replay(g, line);
      EXPECT_TRUE(g.isWon());
      fakeNow = 5000;
      EXPECT_EQ(g.elapsedMs(), 0u);
      EXPECT_FALSE(g.undo());
    }
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  printf("klondike: %d bank deals re-solved, %.0f ms\n", solved, ms);
}

TEST(KlondikeSave, RoundTripKeepsBoardAndUndo) {
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    fakeNow = 0;
    Game g(fakeMillis);
    g.newGame(seed, seed % 2 == 1, seed % 4 == 0);
    games::Rng rng(seed + 100);
    for (int i = 0; i < static_cast<int>(seed) * 12; ++i) randomMove(g, rng);
    fakeNow = 777;
    const std::string text = save(g);
    ASSERT_FALSE(text.empty());
    ASSERT_LT(text.size(), SAVE_MAX);
    Game r(fakeMillis);
    ASSERT_TRUE(r.deserialize(text.data(), text.size())) << text;
    EXPECT_TRUE(same(r.board(), g.board()));
    EXPECT_EQ(r.moveCount(), g.moveCount());
    EXPECT_EQ(r.drawThree(), g.drawThree());
    EXPECT_EQ(r.solvableOnly(), g.solvableOnly());
    EXPECT_EQ(r.seed(), g.seed());
    EXPECT_TRUE(r.paused());
    EXPECT_EQ(r.elapsedMs(), 777u);
    EXPECT_EQ(save(r), text);
    while (g.undo()) {
      ASSERT_TRUE(r.undo());
      ASSERT_TRUE(same(r.board(), g.board()));
    }
    EXPECT_FALSE(r.undo());
  }
}

TEST(KlondikeSave, FullUndoRingFitsSaveMax) {
  Game g(fakeMillis);
  g.newGame(3, false, false);
  for (int i = 0; i < UNDO_DEPTH + 30; ++i) g.draw();
  const std::string text = save(g);
  ASSERT_FALSE(text.empty());
  EXPECT_LT(text.size(), SAVE_MAX);
  Game r(fakeMillis);
  EXPECT_TRUE(r.deserialize(text.data(), text.size()));
  int undone = 0;
  while (r.undo()) ++undone;
  EXPECT_EQ(undone, UNDO_DEPTH);
}

TEST(KlondikeSave, CorruptSavesAreRejected) {
  Game g(fakeMillis);
  g.newGame(9, false, false);
  games::Rng rng(4);
  for (int i = 0; i < 30; ++i) randomMove(g, rng);
  const std::string good = save(g);

  Game r(fakeMillis);
  const std::string before = save(r);
  auto rejects = [&](const std::string& text) {
    EXPECT_FALSE(r.deserialize(text.data(), text.size())) << text;
    EXPECT_EQ(save(r), before);
  };
  const size_t piles = good.find("\"piles\":\"") + 9;
  const size_t history = good.find("\"undo\":\"") + 8;
  rejects("");
  rejects(good.substr(0, good.size() - 3));
  {
    std::string t = good;
    rejects(t.replace(t.find("\"draw\":1"), 8, "\"draw\":2"));
  }
  {
    std::string t = good;
    rejects(t.replace(t.find("\"v\":1"), 5, "\"v\":3"));
  }
  {
    std::string t = good;  // a card twice, another missing
    t[piles] = t[piles + 2];
    t[piles + 1] = t[piles + 3];
    rejects(t);
  }
  {
    std::string t = good;  // the stock card turned face up
    t[piles + 1] = static_cast<char>(toupper(t[piles + 1]));
    rejects(t);
  }
  {
    std::string t = good;  // a pile separator missing
    t.erase(t.find('/', piles), 1);
    rejects(t);
  }
  {
    std::string t = good;  // the last move points elsewhere
    const size_t last = t.find('"', history) - 5;
    t[last + 1] = t[last + 1] == 'c' ? 'b' : 'c';
    rejects(t);
  }
  {
    std::string t = good;
    t.insert(history, "x");
    rejects(t);
  }
  {
    // A move that undoes cleanly but was never legal: waste onto column 1's face-down card.
    Game probe(fakeMillis);
    probe.newGame(9, false, false);
    std::string t = save(probe);
    rejects(t.insert(t.find("\"undo\":\"") + 8, "17010"));
  }
  {
    // A face-down card above a face-up one: column 1 of a fresh deal, flipped.
    Game probe(fakeMillis);
    probe.newGame(9, false, false);
    std::string t = save(probe);
    size_t col = t.find("\"piles\":\"") + 8;
    for (int i = 0; i < TABLEAU + 1; ++i) col = t.find('/', col + 1);
    std::swap(t[col + 1], t[col + 3]);
    std::swap(t[col + 2], t[col + 4]);
    rejects(t);
  }
  EXPECT_TRUE(r.deserialize(good.data(), good.size()));
}
