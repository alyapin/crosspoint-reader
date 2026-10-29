#include "FreeCell.h"

#include <cstring>

namespace freecell {
namespace {

using namespace cards;

constexpr uint32_t SAVE_VERSION = 1;

bool parentOf(const uint8_t lower, const uint8_t upper) {
  return isRed(lower) != isRed(upper) && rankOf(lower) == rankOf(upper) + 1;
}

int foundationRank(const Board& board, const int suit) {
  for (int f = FOUNDATION; f < COLUMN; ++f) {
    const Pile& p = board.pile[f];
    if (p.count > 0 && suitOf(p.top()) == suit) return rankOf(p.top());
  }
  return -1;
}

bool safeToFoundation(const Board& board, const uint8_t card) {
  const int rank = rankOf(card);
  if (rank <= 1) return true;
  const int a = isRed(card) ? SPADES : HEARTS;
  const int b = isRed(card) ? CLUBS : DIAMONDS;
  return foundationRank(board, a) >= rank - 1 && foundationRank(board, b) >= rank - 1;
}

bool columnsInOrder(const Board& board) {
  for (int c = COLUMN; c < PLACES; ++c) {
    const Pile& p = board.pile[c];
    for (int i = 1; i < p.count; ++i) {
      if (rankOf(p.cards[i]) > rankOf(p.cards[i - 1])) return false;
    }
  }
  return true;
}

bool replayInto(Board& board, const uint32_t dealNumber, const Step* steps, const int count) {
  deal(dealNumber, board);
  for (int i = 0; i < count; ++i) {
    if (!applyMove(board, steps[i].from, steps[i].to, steps[i].count)) return false;
    autoPlay(board);
  }
  return true;
}

}  // namespace

void deal(const uint32_t dealNumber, Board& board) {
  // Microsoft card ids: rank * 4 + suit, suits clubs, diamonds, hearts, spades.
  uint8_t deck[DECK];
  for (int i = 0; i < DECK; ++i) deck[i] = static_cast<uint8_t>(DECK - 1 - i);
  uint32_t seed = dealNumber;
  for (int i = 0; i < DECK - 1; ++i) {
    seed = (seed * 214013u + 2531011u) & 0x7FFFFFFFu;
    const int j = DECK - 1 - static_cast<int>((seed >> 16) % (DECK - i));
    const uint8_t t = deck[i];
    deck[i] = deck[j];
    deck[j] = t;
  }
  for (Pile& p : board.pile) p.count = 0;
  for (int i = 0; i < DECK; ++i) {
    Pile& p = board.pile[COLUMN + i % COLUMNS];
    p.cards[p.count++] = makeCard(3 - deck[i] % 4, deck[i] / 4);
  }
}

int runLength(const Board& board, const int place, const int index) {
  if (place < 0 || place >= PLACES || isFoundation(place)) return 0;
  const Pile& p = board.pile[place];
  if (index < 0 || index >= p.count) return 0;
  for (int i = index; i + 1 < p.count; ++i) {
    if (!parentOf(p.cards[i], p.cards[i + 1])) return 0;
  }
  return p.count - index;
}

int moveLimit(const Board& board, const int to) {
  int free = 0;
  int empty = 0;
  for (int c = CELL; c < FOUNDATION; ++c) free += board.pile[c].count == 0 ? 1 : 0;
  for (int c = COLUMN; c < PLACES; ++c) empty += c != to && board.pile[c].count == 0 ? 1 : 0;
  return (free + 1) << empty;
}

bool canMove(const Board& board, const int from, const int to, const int count) {
  if (from < 0 || from >= PLACES || to < 0 || to >= PLACES || from == to || count <= 0) return false;
  const Pile& source = board.pile[from];
  if (count > source.count || runLength(board, from, source.count - count) != count) return false;
  const uint8_t moving = source.cards[source.count - count];
  const Pile& target = board.pile[to];
  if (isCell(to)) return count == 1 && target.count == 0;
  if (isFoundation(to)) {
    return count == 1 &&
           (target.count == 0 ? rankOf(moving) == 0
                              : suitOf(target.top()) == suitOf(moving) && rankOf(moving) == rankOf(target.top()) + 1);
  }
  if (target.count + count > PILE_CAPACITY || count > moveLimit(board, to)) return false;
  return target.count == 0 || parentOf(target.top(), moving);
}

bool applyMove(Board& board, const int from, const int to, const int count) {
  if (!canMove(board, from, to, count)) return false;
  Pile& source = board.pile[from];
  Pile& target = board.pile[to];
  source.count = static_cast<uint8_t>(source.count - count);
  memcpy(target.cards + target.count, source.cards + source.count, count);
  target.count = static_cast<uint8_t>(target.count + count);
  return true;
}

void autoPlay(Board& board) {
  const bool all = columnsInOrder(board);
  bool progress = true;
  while (progress) {
    progress = false;
    for (int from = CELL; from < PLACES && !progress; ++from) {
      if (isFoundation(from) || board.pile[from].count == 0) continue;
      if (!all && !safeToFoundation(board, board.pile[from].top())) continue;
      for (int f = FOUNDATION; f < COLUMN && !progress; ++f) progress = applyMove(board, from, f, 1);
    }
  }
}

bool isWon(const Board& board) {
  for (int f = FOUNDATION; f < COLUMN; ++f) {
    if (board.pile[f].count != RANKS) return false;
  }
  return true;
}

void Game::newGame(const uint32_t dealNumber) {
  number = dealNumber;
  historyCount = 0;
  replayInto(state, number, history, 0);
  won = false;
}

const Pile& Game::pile(const int place) const {
  static const Pile EMPTY{};
  return place >= 0 && place < PLACES ? state.pile[place] : EMPTY;
}

bool Game::move(const int from, const int to, const int count) {
  if (won || historyCount >= HISTORY) return false;
  for (int n = count; n > 0; --n) {
    if (!applyMove(state, from, to, n)) continue;
    history[historyCount++] = Step{static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint8_t>(n)};
    autoPlay(state);
    won = freecell::isWon(state);
    return true;
  }
  return false;
}

bool Game::undo() {
  if (!canUndo()) return false;
  --historyCount;
  replayInto(state, number, history, historyCount);
  return true;
}

int Game::autoTarget(const int from, const int count) const {
  if (count == 1) {
    for (int f = FOUNDATION; f < COLUMN; ++f) {
      if (canMove(state, from, f, 1)) return f;
    }
  }
  int empty = -1;
  for (int c = COLUMN; c < PLACES; ++c) {
    if (!canMove(state, from, c, count)) continue;
    if (state.pile[c].count > 0) return c;
    if (empty < 0) empty = c;
  }
  // A whole column gains nothing from another empty one.
  if (empty >= 0 && !(isColumn(from) && state.pile[from].count == count)) return empty;
  if (count == 1 && isColumn(from)) {
    for (int c = CELL; c < FOUNDATION; ++c) {
      if (canMove(state, from, c, 1)) return c;
    }
  }
  return -1;
}

bool Game::hint(Step& out) const {
  if (won) return false;
  auto suggest = [&](const int from, const int to, const int count) {
    out = Step{static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint8_t>(count)};
    return true;
  };
  // A run that already sits on its own parent gains nothing by moving.
  auto settled = [&](const int from, const int count) {
    const Pile& p = state.pile[from];
    const int base = p.count - count;
    return isColumn(from) && base > 0 && parentOf(p.cards[base - 1], p.cards[base]);
  };
  for (int from = CELL; from < PLACES; ++from) {
    for (int f = FOUNDATION; f < COLUMN && !isFoundation(from); ++f) {
      if (canMove(state, from, f, 1)) return suggest(from, f, 1);
    }
  }
  for (int pass = 0; pass < 2; ++pass) {
    const bool toEmpty = pass == 1;
    for (int from = CELL; from < PLACES; ++from) {
      if (isFoundation(from)) continue;
      int longest = 0;
      while (runLength(from, state.pile[from].count - longest - 1) > 0) ++longest;
      for (int c = COLUMN; c < PLACES; ++c) {
        if ((state.pile[c].count == 0) != toEmpty) continue;
        for (int n = longest; n > 0; --n) {
          if (!canMove(state, from, c, n) || settled(from, n)) continue;
          if (toEmpty && (!isColumn(from) || n == state.pile[from].count)) continue;
          return suggest(from, c, n);
        }
      }
    }
  }
  for (int from = COLUMN; from < PLACES; ++from) {
    if (state.pile[from].count == 0 || settled(from, 1)) continue;
    for (int c = CELL; c < FOUNDATION; ++c) {
      if (canMove(state, from, c, 1)) return suggest(from, c, 1);
    }
  }
  return false;
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("deal", number);
  putSteps(w, "moves", history, historyCount);
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t dealNumber = 0;
  const char* steps = nullptr;
  size_t stepsLen = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("deal", dealNumber) || dealNumber == 0 ||
      dealNumber > DEALS || !r.string("moves", steps, stepsLen) || stepsLen % 3 != 0 || stepsLen > 3 * HISTORY) {
    return false;
  }
  // Every stored move must replay from the deal; a won game is never saved.
  const int count = static_cast<int>(stepsLen / 3);
  Board board;
  deal(dealNumber, board);
  for (int i = 0; i < count; ++i) {
    Step step;
    if (!readStep(steps + 3 * i, step) || !applyMove(board, step.from, step.to, step.count)) return false;
    autoPlay(board);
  }
  if (freecell::isWon(board)) return false;
  for (int i = 0; i < count; ++i) readStep(steps + 3 * i, history[i]);
  historyCount = static_cast<uint16_t>(count);
  number = dealNumber;
  state = board;
  won = false;
  return true;
}

}  // namespace freecell
