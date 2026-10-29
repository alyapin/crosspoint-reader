#pragma once

// Spider: two decks in ten columns, 54 cards dealt and five deals of ten in the
// stock, in 1, 2 or 4 suits. A run of one suit moves as one; a king-to-ace run
// of one suit leaves by itself. The stock does not deal while a column is
// empty. The history from the deal is both the undo stack and the save: undo
// replays it.
//   newGame(seed, suits, solvableOnly), move(from, to, count), deal(), undo()
//   autoTarget(), hint(), serialize() / deserialize()

#include "Cards.h"

namespace spider {

using cards::Step;

constexpr int COLUMNS = 10;
constexpr int DECK = 104;
constexpr int DEALT = 54;
constexpr int STOCK_SIZE = DECK - DEALT;
constexpr int SETS = 8;
constexpr int STOCK = COLUMNS;  // a step from the stock is a deal
constexpr int PILE_CAPACITY = 48;
constexpr int HISTORY = 1000;
constexpr size_t SAVE_MAX = 96 + 3 * HISTORY;

struct Pile {
  uint8_t cards[PILE_CAPACITY];
  uint8_t count;

  uint8_t top() const { return count == 0 ? cards::NO_CARD : cards[count - 1]; }
};

struct Board {
  Pile column[COLUMNS];
  uint8_t stock[STOCK_SIZE];
  uint8_t stockCount;
  uint8_t done[SETS];  // the suit of each collected run
  uint8_t doneCount;
};

// The shuffle of 8 runs of 13 in `suits` (1, 2 or 4) suits.
void shuffledDeck(uint32_t seed, int suits, uint8_t out[DECK]);
void deal(Board& board, const uint8_t deck[DECK]);

// Cards from `index` to the top of `column` that move together; 0 when none.
int runLength(const Board& board, int column, int index);
bool canMove(const Board& board, int from, int to, int count);
bool canDeal(const Board& board);
// Plays a legal move or deal, turns the exposed cards and collects finished runs.
bool applyMove(Board& board, int from, int to, int count);
bool applyDeal(Board& board);
bool isWon(const Board& board);

// The bank of seeds the host solver won, for 1 and 2 suits; none for 4.
int solvableDealCount(int suits);
uint32_t solvableSeed(int suits, int index);

class Game {
 public:
  Game() { newGame(1, 1, false); }

  // `solvableOnly` deals from the bank (1 and 2 suits).
  void newGame(uint32_t seed, int suits, bool solvableOnly);

  const Pile& column(int index) const;
  const Board& board() const { return state; }
  uint32_t seed() const { return seedValue; }
  int suits() const { return suitCount; }
  bool solvableOnly() const { return solvableDeal; }
  int moveCount() const { return historyCount; }
  bool isWon() const { return won; }
  bool inProgress() const { return !won; }

  int runLength(const int column, const int index) const { return spider::runLength(state, column, index); }
  // Moves the last `count` cards of `from`, or as many of them as `to` takes.
  bool move(int from, int to, int count);
  bool canDeal() const { return spider::canDeal(state); }
  bool deal();
  bool canUndo() const { return historyCount > 0 && !won; }
  bool undo();
  // HISTORY moves are kept; past them no move is taken until one is undone.
  bool historyFull() const { return historyCount >= HISTORY; }

  // Where `count` cards from `from` go on a second tap: a card of the same
  // suit, any card, an empty column. -1 when nowhere.
  int autoTarget(int from, int count) const;
  // A useful next move; a deal comes back with from == STOCK. False when nothing helps.
  bool hint(Step& out) const;

  size_t serialize(char* out, size_t cap) const;
  // Restores a serialize()d game. False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  bool play(const Step& step);

  Board state{};
  Step history[HISTORY] = {};
  uint16_t historyCount = 0;
  uint32_t seedValue = 1;
  uint8_t suitCount = 1;
  bool solvableDeal = false;
  bool won = false;
};

}  // namespace spider
