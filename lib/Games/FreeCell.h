#pragma once

// FreeCell: four free cells, four foundations, eight columns, every card open.
// A run moves as one while (free cells + 1) x 2^(empty columns) allows it; safe
// cards go up by themselves. Deals are numbered as in Microsoft FreeCell. The
// history from the deal is both the undo stack and the save: undo replays it.
//   newGame(deal), move(from, to, count), undo(), autoTarget(), hint()
//   serialize() / deserialize()

#include "Cards.h"

namespace freecell {

using cards::Step;

constexpr int CELLS = 4;
constexpr int FOUNDATIONS = 4;
constexpr int COLUMNS = 8;
// One index space for every place: cells, foundations, then columns.
constexpr int CELL = 0;
constexpr int FOUNDATION = 4;
constexpr int COLUMN = 8;
constexpr int PLACES = 16;
constexpr int DECK = 52;
constexpr int PILE_CAPACITY = 20;  // seven dealt cards and a run down from a king
constexpr uint32_t DEALS = 32000;
constexpr uint32_t UNSOLVABLE_DEAL = 11982;
constexpr int HISTORY = 1000;
constexpr size_t SAVE_MAX = 64 + 3 * HISTORY;

constexpr bool isCell(const int place) { return place >= CELL && place < FOUNDATION; }
constexpr bool isFoundation(const int place) { return place >= FOUNDATION && place < COLUMN; }
constexpr bool isColumn(const int place) { return place >= COLUMN && place < PLACES; }

struct Pile {
  uint8_t cards[PILE_CAPACITY];
  uint8_t count;

  uint8_t top() const { return count == 0 ? cards::NO_CARD : cards[count - 1]; }
};

struct Board {
  Pile pile[PLACES];
};

// Microsoft deal `deal` (1..32000), dealt row by row.
void deal(uint32_t deal, Board& board);
// Cards from `index` to the top of `place` that move together; 0 when none.
int runLength(const Board& board, int place, int index);
// The most cards one move to `to` can carry.
int moveLimit(const Board& board, int to);
bool canMove(const Board& board, int from, int to, int count);
bool applyMove(Board& board, int from, int to, int count);
// Sends safe cards up, and every card once the columns are in order.
void autoPlay(Board& board);
bool isWon(const Board& board);

class Game {
 public:
  Game() { newGame(1); }

  void newGame(uint32_t dealNumber);

  const Pile& pile(int place) const;
  const Board& board() const { return state; }
  uint32_t dealNumber() const { return number; }
  int moveCount() const { return historyCount; }
  bool isWon() const { return won; }
  bool inProgress() const { return !won; }

  int runLength(const int place, const int index) const { return freecell::runLength(state, place, index); }
  // Moves the last `count` cards of `from`, or as many of them as `to` takes.
  bool move(int from, int to, int count);
  bool canUndo() const { return historyCount > 0 && !won; }
  bool undo();
  // HISTORY moves are kept; past them no move is taken until one is undone.
  bool historyFull() const { return historyCount >= HISTORY; }

  // Where `count` cards from `from` go on a second tap: a foundation, a
  // column, an empty column, a free cell. -1 when nowhere.
  int autoTarget(int from, int count) const;
  // A useful next move; false when nothing helps.
  bool hint(Step& out) const;

  size_t serialize(char* out, size_t cap) const;
  // Restores a serialize()d game. False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  bool replay(uint32_t dealNumber, const Step* steps, int count);

  Board state{};
  Step history[HISTORY] = {};
  uint16_t historyCount = 0;
  uint32_t number = 1;
  bool won = false;
};

}  // namespace freecell
