#pragma once

// Klondike solitaire. Board rules are free functions (also used by the host
// solver); Game is the session a Klondike screen drives:
//   newGame(seed, drawThree, solvableOnly)
//   move(from, to, count), draw(), undo(), autoTarget(), hint()
//   autoMoveSafe(), canAutoFinish() / autoFinish()
//   pile(i), moveCount(), isWon(), elapsedMs(), pause() / resume()
//   serialize() / deserialize()
// Rules follow ma-r-s/crossplay src/apps_local/solitaire/SolitaireCore (MIT,
// (c) 2026 Mario Ruiz). About 1.2 KB; no heap.

#include <cstddef>
#include <cstdint>

#include "GameCommon.h"

namespace klondike {

constexpr int RANKS = 13;
constexpr int SUITS = 4;
constexpr int DECK = 52;

// Card byte: rank 0 (ace) .. 12 (king) in bits 0-3, suit in bits 4-5, face-up in bit 7.
enum Suit : uint8_t { SPADES, HEARTS, DIAMONDS, CLUBS };
constexpr uint8_t FACE_UP = 0x80;
constexpr uint8_t NO_CARD = 0xFF;
constexpr uint8_t makeCard(const int suit, const int rank) { return static_cast<uint8_t>((suit << 4) | rank); }
constexpr int rankOf(const uint8_t card) { return card & 0x0F; }
constexpr int suitOf(const uint8_t card) { return (card >> 4) & 0x03; }
constexpr bool isRed(const uint8_t card) { return suitOf(card) == HEARTS || suitOf(card) == DIAMONDS; }
constexpr bool isFaceUp(const uint8_t card) { return (card & FACE_UP) != 0; }

// One index space for all piles, so a move is (from, to, count).
constexpr int STOCK = 0;
constexpr int WASTE = 1;
constexpr int FOUNDATION = 2;  // 2..5
constexpr int TABLEAU = 6;     // 6..12
constexpr int FOUNDATIONS = 4;
constexpr int TABLEAU_PILES = 7;
constexpr int PILES = 13;
constexpr int PILE_CAPACITY = 24;  // the stock at the deal; a column holds at most 19

constexpr bool isFoundation(const int pile) { return pile >= FOUNDATION && pile < FOUNDATION + FOUNDATIONS; }
constexpr bool isTableau(const int pile) { return pile >= TABLEAU && pile < PILES; }

struct Pile {
  uint8_t cards[PILE_CAPACITY];
  uint8_t count;

  uint8_t top() const { return count == 0 ? NO_CARD : cards[count - 1]; }
};

struct Board {
  Pile pile[PILES];
};

// A reversible step. count = cards moved, dealt (DRAW) or turned back (RECYCLE).
struct Move {
  static constexpr uint8_t FLIPPED = 1;  // exposed a face-down tableau card
  static constexpr uint8_t DRAW = 2;
  static constexpr uint8_t RECYCLE = 4;
  uint8_t from = 0;
  uint8_t to = 0;
  uint8_t count = 0;
  uint8_t flags = 0;
};

// The shuffle: a seed always gives the same deck.
void shuffledDeck(uint32_t seed, uint8_t out[DECK]);
// Seven columns of 1..7 cards with the last face up, the rest to the stock.
void deal(Board& board, const uint8_t deck[DECK]);

// Cards from `index` to the top of `pile` that move together; 0 when none.
int runLength(const Board& board, int pile, int index);
bool canMove(const Board& board, int from, int to, int count);
// Plays a legal move and flips the newly exposed card; false (no change) when illegal.
bool applyMove(Board& board, int from, int to, int count, Move& out);
// Deals 1 or 3 cards to the waste, or turns the waste over into an empty stock.
bool applyDraw(Board& board, bool drawThree, Move& out);
// Undoes `move`; false (no change) when it does not fit the board.
bool revert(Board& board, const Move& move);
bool isWon(const Board& board);
// Safe to send up: no card still needs it as a base (both opposite-colour
// foundations reach rank - 1).
bool isSafeToFoundation(const Board& board, uint8_t card);
// Every card dealt once, piles in shapes play can reach.
bool isConsistent(const Board& board);

constexpr int UNDO_DEPTH = 200;
// Upper bound of serialize() output, terminator included.
constexpr size_t SAVE_MAX = 160 + DECK * 2 + PILES + UNDO_DEPTH * 5;

// Seeds in the solvable-deal bank per draw mode.
int solvableDealCount(bool drawThree);
// The deck of bank deal `index`, suits recoloured by `variant` (0..7).
void solvableDeck(bool drawThree, int index, int variant, uint8_t out[DECK]);

class Game {
 public:
  explicit Game(games::MillisFn millis);

  // `solvableOnly` picks a deal from the bank proved winnable by the host solver.
  void newGame(uint32_t seed, bool drawThree, bool solvableOnly);

  const Pile& pile(int index) const;
  const Board& board() const { return state; }
  uint32_t seed() const { return seedValue; }
  bool drawThree() const { return threeCards; }
  bool solvableOnly() const { return solvableDeal; }
  int moveCount() const { return moves; }
  bool isWon() const { return won; }
  bool inProgress() const { return !won; }

  int runLength(const int pileIndex, const int index) const { return klondike::runLength(state, pileIndex, index); }
  bool canMove(const int from, const int to, const int count) const {
    return klondike::canMove(state, from, to, count);
  }
  bool move(int from, int to, int count);
  // The stock tap: deals, or recycles the waste; false when both are empty.
  bool draw();
  bool canUndo() const { return undoCount > 0 && !won; }
  bool undo();

  // Where `count` cards from `from` would go on a tap: a foundation, else a
  // non-empty column, else an empty one. -1 when nowhere.
  int autoTarget(int from, int count) const;
  // Sends every safe card to the foundations; returns how many moved.
  int autoMoveSafe();
  // Stock and waste empty and every card face up: the rest plays itself.
  bool canAutoFinish() const;
  int autoFinish();
  // A useful next move (foundation, uncovering, waste, then the stock). A stock
  // tap comes back as Move::DRAW. False when nothing can move.
  bool hint(Move& out) const;

  uint32_t elapsedMs() const { return clock.elapsed(now()); }
  void pause();
  void resume();
  bool paused() const { return !clock.isRunning(); }

  size_t serialize(char* out, size_t cap) const;
  // Restores a serialize()d game, paused. False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  void push(const Move& m);
  void settle();

  games::MillisFn now;
  Board state{};
  Move undoRing[UNDO_DEPTH] = {};
  uint16_t undoTop = 0;
  uint16_t undoCount = 0;
  uint16_t moves = 0;
  uint32_t seedValue = 0;
  bool threeCards = false;
  bool solvableDeal = false;
  bool won = false;
  games::Stopwatch clock;
};

}  // namespace klondike
