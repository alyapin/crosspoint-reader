#include "Spider.h"

#include <cstring>

#include "GameCommon.h"
#include "SpiderDeals.h"

namespace spider {
namespace {

using namespace cards;

constexpr uint32_t SAVE_VERSION = 1;

uint8_t faceUp(const uint8_t card) { return static_cast<uint8_t>(card | FACE_UP); }

void turnTop(Pile& p) {
  if (p.count > 0) p.cards[p.count - 1] = faceUp(p.top());
}

// A finished king-to-ace run of one suit leaves the column.
void collect(Board& board, const int column) {
  Pile& p = board.column[column];
  const int base = p.count - RANKS;
  if (base < 0 || rankOf(p.cards[base]) != RANKS - 1 || runLength(board, column, base) != RANKS) return;
  board.done[board.doneCount++] = static_cast<uint8_t>(suitOf(p.cards[base]));
  p.count = static_cast<uint8_t>(base);
  turnTop(p);
}

bool play(Board& board, const Step& step) {
  return step.from == STOCK ? applyDeal(board) : applyMove(board, step.from, step.to, step.count);
}

void dealSeed(Board& board, const uint32_t seed, const int suits) {
  uint8_t deck[DECK];
  shuffledDeck(seed, suits, deck);
  deal(board, deck);
}

}  // namespace

void shuffledDeck(const uint32_t seed, const int suits, uint8_t out[DECK]) {
  for (int set = 0; set < SETS; ++set) {
    const int suit = suits == 1 ? SPADES : suits == 2 ? (set % 2 == 0 ? SPADES : HEARTS) : set % 4;
    for (int rank = 0; rank < RANKS; ++rank) out[set * RANKS + rank] = makeCard(suit, rank);
  }
  games::Rng rng(seed);
  for (int i = DECK - 1; i > 0; --i) {
    const int j = static_cast<int>(rng.below(i + 1));
    const uint8_t t = out[i];
    out[i] = out[j];
    out[j] = t;
  }
}

void deal(Board& board, const uint8_t deck[DECK]) {
  for (Pile& p : board.column) p.count = 0;
  for (int i = 0; i < DEALT; ++i) {
    Pile& p = board.column[i % COLUMNS];
    p.cards[p.count++] = i + COLUMNS >= DEALT ? faceUp(deck[i]) : deck[i];
  }
  memcpy(board.stock, deck + DEALT, STOCK_SIZE);
  board.stockCount = STOCK_SIZE;
  board.doneCount = 0;
}

int runLength(const Board& board, const int column, const int index) {
  if (column < 0 || column >= COLUMNS) return 0;
  const Pile& p = board.column[column];
  if (index < 0 || index >= p.count || !isFaceUp(p.cards[index])) return 0;
  for (int i = index; i + 1 < p.count; ++i) {
    if (suitOf(p.cards[i]) != suitOf(p.cards[i + 1]) || rankOf(p.cards[i + 1]) != rankOf(p.cards[i]) - 1) return 0;
  }
  return p.count - index;
}

bool canMove(const Board& board, const int from, const int to, const int count) {
  if (from < 0 || from >= COLUMNS || to < 0 || to >= COLUMNS || from == to || count <= 0) return false;
  const Pile& source = board.column[from];
  const Pile& target = board.column[to];
  if (count > source.count || runLength(board, from, source.count - count) != count) return false;
  if (target.count + count > PILE_CAPACITY) return false;
  return target.count == 0 || rankOf(target.top()) == rankOf(source.cards[source.count - count]) + 1;
}

bool canDeal(const Board& board) {
  if (board.stockCount < COLUMNS) return false;
  for (const Pile& p : board.column) {
    if (p.count == 0 || p.count >= PILE_CAPACITY) return false;
  }
  return true;
}

bool applyMove(Board& board, const int from, const int to, const int count) {
  if (!canMove(board, from, to, count)) return false;
  Pile& source = board.column[from];
  Pile& target = board.column[to];
  source.count = static_cast<uint8_t>(source.count - count);
  memcpy(target.cards + target.count, source.cards + source.count, count);
  target.count = static_cast<uint8_t>(target.count + count);
  turnTop(source);
  collect(board, to);
  return true;
}

bool applyDeal(Board& board) {
  if (!canDeal(board)) return false;
  for (Pile& p : board.column) p.cards[p.count++] = faceUp(board.stock[--board.stockCount]);
  for (int c = 0; c < COLUMNS; ++c) collect(board, c);
  return true;
}

bool isWon(const Board& board) { return board.doneCount == SETS; }

int solvableDealCount(const int suits) {
  return suits == 1 ? SOLVABLE_1SUIT_COUNT : suits == 2 ? SOLVABLE_2SUIT_COUNT : 0;
}

uint32_t solvableSeed(const int suits, const int index) {
  const int count = solvableDealCount(suits);
  if (count == 0) return 1;
  return (suits == 1 ? SOLVABLE_1SUIT : SOLVABLE_2SUIT)[index >= 0 && index < count ? index : 0];
}

void Game::newGame(const uint32_t seed, const int suits, const bool solvableOnly) {
  suitCount = static_cast<uint8_t>(suits == 2 || suits == 4 ? suits : 1);
  const int bank = solvableDealCount(suitCount);
  solvableDeal = solvableOnly && bank > 0;
  if (solvableDeal) {
    games::Rng rng(seed);
    seedValue = solvableSeed(suitCount, static_cast<int>(rng.below(bank)));
  } else {
    seedValue = seed;
  }
  dealSeed(state, seedValue, suitCount);
  historyCount = 0;
  won = false;
}

const Pile& Game::column(const int index) const {
  static const Pile EMPTY{};
  return index >= 0 && index < COLUMNS ? state.column[index] : EMPTY;
}

bool Game::play(const Step& step) {
  if (won || historyCount >= HISTORY || !spider::play(state, step)) return false;
  history[historyCount++] = step;
  won = spider::isWon(state);
  return true;
}

bool Game::move(const int from, const int to, const int count) {
  for (int n = count; n > 0; --n) {
    if (canMove(state, from, to, n)) {
      return play(Step{static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint8_t>(n)});
    }
  }
  return false;
}

bool Game::deal() { return play(Step{STOCK, 0, 0}); }

bool Game::undo() {
  if (!canUndo()) return false;
  --historyCount;
  dealSeed(state, seedValue, suitCount);
  for (int i = 0; i < historyCount; ++i) spider::play(state, history[i]);
  return true;
}

int Game::autoTarget(const int from, const int count) const {
  int any = -1;
  int empty = -1;
  const uint8_t card = column(from).cards[column(from).count - count];
  for (int c = 0; c < COLUMNS; ++c) {
    if (!canMove(state, from, c, count)) continue;
    if (state.column[c].count == 0) {
      if (empty < 0) empty = c;
    } else if (suitOf(state.column[c].top()) == suitOf(card)) {
      return c;
    } else if (any < 0) {
      any = c;
    }
  }
  if (any >= 0) return any;
  // A whole column gains nothing from another empty one.
  return column(from).count == count ? -1 : empty;
}

bool Game::hint(Step& out) const {
  if (won) return false;
  int best = -1;
  for (int from = 0; from < COLUMNS; ++from) {
    const Pile& p = state.column[from];
    for (int n = 1; runLength(from, p.count - n) > 0; ++n) {
      const int base = p.count - n;
      const uint8_t card = p.cards[base];
      const uint8_t below = base > 0 ? p.cards[base - 1] : NO_CARD;
      const bool onParent = base > 0 && isFaceUp(below) && rankOf(below) == rankOf(card) + 1;
      for (int to = 0; to < COLUMNS; ++to) {
        if (!canMove(state, from, to, n)) continue;
        int score;
        if (state.column[to].count == 0) {
          // A whole column gains nothing; a run on its parent only frees a column for the stock.
          if (base == 0) continue;
          score = onParent ? 0 : 1;
        } else {
          const bool sameSuit = suitOf(state.column[to].top()) == suitOf(card);
          if (onParent && (!sameSuit || suitOf(below) == suitOf(card))) continue;
          score = (sameSuit ? 8 : 2) + (base > 0 && !isFaceUp(below) ? 4 : 0) + (base == 0 ? 3 : 0);
        }
        if (score > best) {
          best = score;
          out = Step{static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint8_t>(n)};
        }
      }
    }
  }
  if (best > 0) return true;
  if (canDeal()) {
    out = Step{STOCK, 0, 0};
    return true;
  }
  return best == 0 && state.stockCount > 0;
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("seed", seedValue);
  w.number("suits", suitCount);
  w.number("solvable", solvableDeal ? 1 : 0);
  putSteps(w, "moves", history, historyCount);
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t seed = 0;
  uint32_t suits = 0;
  uint32_t solvable = 0;
  const char* steps = nullptr;
  size_t stepsLen = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("seed", seed) || !r.number("suits", suits) ||
      (suits != 1 && suits != 2 && suits != 4) || !r.number("solvable", solvable) || solvable > 1 ||
      !r.string("moves", steps, stepsLen) || stepsLen % 3 != 0 || stepsLen > 3 * HISTORY) {
    return false;
  }
  // Every stored move must replay from the deal; a won game is never saved.
  const int count = static_cast<int>(stepsLen / 3);
  Board board;
  dealSeed(board, seed, static_cast<int>(suits));
  for (int i = 0; i < count; ++i) {
    Step step;
    if (!readStep(steps + 3 * i, step) || !spider::play(board, step)) return false;
  }
  if (spider::isWon(board)) return false;
  for (int i = 0; i < count; ++i) readStep(steps + 3 * i, history[i]);
  historyCount = static_cast<uint16_t>(count);
  seedValue = seed;
  suitCount = static_cast<uint8_t>(suits);
  solvableDeal = solvable == 1;
  state = board;
  won = false;
  return true;
}

}  // namespace spider
