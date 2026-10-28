#include "Klondike.h"

#include <cstring>

#include "GameText.h"
#include "KlondikeDeals.h"

namespace klondike {
namespace {

constexpr uint32_t SAVE_VERSION = 1;
constexpr char RANK_CHARS[] = "A23456789TJQK";
constexpr char SUIT_UP[] = "SHDC";
constexpr char SUIT_DOWN[] = "shdc";

uint8_t faceUp(const uint8_t card) { return static_cast<uint8_t>(card | FACE_UP); }
uint8_t faceDown(const uint8_t card) { return static_cast<uint8_t>(card & ~FACE_UP); }

bool acceptsOnFoundation(const Pile& pile, const uint8_t card) {
  if (pile.count == 0) return rankOf(card) == 0;
  return suitOf(pile.top()) == suitOf(card) && rankOf(card) == rankOf(pile.top()) + 1;
}

bool acceptsOnTableau(const Pile& pile, const uint8_t card) {
  if (pile.count == 0) return rankOf(card) == RANKS - 1;
  const uint8_t top = pile.top();
  return isFaceUp(top) && isRed(top) != isRed(card) && rankOf(card) == rankOf(top) - 1;
}

// Rank on the foundation of `suit`, -1 when it has none.
int foundationRank(const Board& board, const int suit) {
  for (int f = FOUNDATION; f < FOUNDATION + FOUNDATIONS; ++f) {
    const Pile& p = board.pile[f];
    if (p.count > 0 && suitOf(p.top()) == suit) return rankOf(p.top());
  }
  return -1;
}

bool sameBoard(const Board& a, const Board& b) {
  for (int i = 0; i < PILES; ++i) {
    if (a.pile[i].count != b.pile[i].count || memcmp(a.pile[i].cards, b.pile[i].cards, a.pile[i].count) != 0) {
      return false;
    }
  }
  return true;
}

int parseCard(const char r, const char s) {
  const char* rank = strchr(RANK_CHARS, r);
  if (r == '\0' || rank == nullptr) return -1;
  const char* up = s != '\0' ? strchr(SUIT_UP, s) : nullptr;
  const char* down = s != '\0' ? strchr(SUIT_DOWN, s) : nullptr;
  if (up == nullptr && down == nullptr) return -1;
  const int card =
      makeCard(static_cast<int>(up != nullptr ? up - SUIT_UP : down - SUIT_DOWN), static_cast<int>(rank - RANK_CHARS));
  return up != nullptr ? faceUp(card) : card;
}

}  // namespace

void shuffledDeck(const uint32_t seed, uint8_t out[DECK]) {
  for (int suit = 0; suit < SUITS; ++suit) {
    for (int rank = 0; rank < RANKS; ++rank) out[suit * RANKS + rank] = makeCard(suit, rank);
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
  for (Pile& p : board.pile) p.count = 0;
  int next = 0;
  for (int column = 0; column < TABLEAU_PILES; ++column) {
    Pile& p = board.pile[TABLEAU + column];
    for (int row = 0; row <= column; ++row) {
      p.cards[p.count++] = row == column ? faceUp(deck[next]) : faceDown(deck[next]);
      ++next;
    }
  }
  Pile& stock = board.pile[STOCK];
  while (next < DECK) stock.cards[stock.count++] = faceDown(deck[next++]);
}

int runLength(const Board& board, const int pileIndex, const int index) {
  if (pileIndex < 0 || pileIndex >= PILES) return 0;
  const Pile& p = board.pile[pileIndex];
  if (index < 0 || index >= p.count || !isFaceUp(p.cards[index])) return 0;
  // Only a column moves more than its top card, and only an alternating descending run.
  if (!isTableau(pileIndex)) return index == p.count - 1 ? 1 : 0;
  for (int i = index; i + 1 < p.count; ++i) {
    const uint8_t upper = p.cards[i];
    const uint8_t lower = p.cards[i + 1];
    if (isRed(upper) == isRed(lower) || rankOf(lower) != rankOf(upper) - 1) return 0;
  }
  return p.count - index;
}

bool canMove(const Board& board, const int from, const int to, const int count) {
  if (from < 0 || from >= PILES || to < 0 || to >= PILES || from == to || count <= 0) return false;
  if (from == STOCK || to == STOCK || to == WASTE) return false;
  const Pile& source = board.pile[from];
  if (count > source.count) return false;
  const int first = source.count - count;
  if (runLength(board, from, first) != count) return false;
  const uint8_t moving = source.cards[first];
  if (isFoundation(to)) return count == 1 && acceptsOnFoundation(board.pile[to], moving);
  return board.pile[to].count + count <= PILE_CAPACITY && acceptsOnTableau(board.pile[to], moving);
}

bool applyMove(Board& board, const int from, const int to, const int count, Move& out) {
  if (!canMove(board, from, to, count)) return false;
  Pile& source = board.pile[from];
  Pile& target = board.pile[to];
  const int first = source.count - count;
  memcpy(target.cards + target.count, source.cards + first, count);
  target.count = static_cast<uint8_t>(target.count + count);
  source.count = static_cast<uint8_t>(first);
  out = Move{static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint8_t>(count), 0};
  if (isTableau(from) && source.count > 0 && !isFaceUp(source.top())) {
    source.cards[source.count - 1] = faceUp(source.top());
    out.flags = Move::FLIPPED;
  }
  return true;
}

bool applyDraw(Board& board, const bool drawThree, Move& out) {
  Pile& stock = board.pile[STOCK];
  Pile& waste = board.pile[WASTE];
  if (stock.count == 0) {
    if (waste.count == 0) return false;
    // Turning the waste over keeps the order, so the cards come round again in the same rotation.
    out = Move{WASTE, STOCK, waste.count, Move::RECYCLE};
    while (waste.count > 0) stock.cards[stock.count++] = faceDown(waste.cards[--waste.count]);
    return true;
  }
  const int wanted = drawThree ? 3 : 1;
  int dealt = 0;
  while (dealt < wanted && stock.count > 0 && waste.count < PILE_CAPACITY) {
    waste.cards[waste.count++] = faceUp(stock.cards[--stock.count]);
    ++dealt;
  }
  if (dealt == 0) return false;
  out = Move{STOCK, WASTE, static_cast<uint8_t>(dealt), Move::DRAW};
  return true;
}

bool revert(Board& board, const Move& m) {
  Pile& stock = board.pile[STOCK];
  Pile& waste = board.pile[WASTE];
  if (m.flags == Move::RECYCLE) {
    if (m.count == 0 || stock.count != m.count || waste.count != 0) return false;
    while (stock.count > 0) waste.cards[waste.count++] = faceUp(stock.cards[--stock.count]);
    return true;
  }
  if (m.flags == Move::DRAW) {
    if (m.count == 0 || m.count > 3 || waste.count < m.count || stock.count + m.count > PILE_CAPACITY) return false;
    for (int i = 0; i < m.count; ++i) stock.cards[stock.count++] = faceDown(waste.cards[--waste.count]);
    return true;
  }
  if ((m.flags & ~Move::FLIPPED) != 0 || m.from >= PILES || m.to >= PILES || m.from == m.to || m.count == 0) {
    return false;
  }
  Pile& source = board.pile[m.from];
  Pile& target = board.pile[m.to];
  if (target.count < m.count || source.count + m.count > PILE_CAPACITY) return false;
  if (m.flags & Move::FLIPPED) {
    if (source.count == 0 || !isFaceUp(source.top())) return false;
    source.cards[source.count - 1] = faceDown(source.top());
  }
  const int first = target.count - m.count;
  memcpy(source.cards + source.count, target.cards + first, m.count);
  source.count = static_cast<uint8_t>(source.count + m.count);
  target.count = static_cast<uint8_t>(first);
  return true;
}

bool isWon(const Board& board) {
  for (int f = FOUNDATION; f < FOUNDATION + FOUNDATIONS; ++f) {
    if (board.pile[f].count != RANKS) return false;
  }
  return true;
}

bool isSafeToFoundation(const Board& board, const uint8_t card) {
  const int rank = rankOf(card);
  if (rank <= 1) return true;
  const int a = isRed(card) ? SPADES : HEARTS;
  const int b = isRed(card) ? CLUBS : DIAMONDS;
  return foundationRank(board, a) >= rank - 1 && foundationRank(board, b) >= rank - 1;
}

bool isConsistent(const Board& board) {
  bool seen[DECK] = {};
  for (int i = 0; i < PILES; ++i) {
    const Pile& p = board.pile[i];
    if (p.count > PILE_CAPACITY) return false;
    for (int c = 0; c < p.count; ++c) {
      const uint8_t card = p.cards[c];
      if ((card & 0x40) != 0 || rankOf(card) >= RANKS) return false;
      const int id = suitOf(card) * RANKS + rankOf(card);
      if (seen[id]) return false;
      seen[id] = true;
      if (i == STOCK && isFaceUp(card)) return false;
      if ((i == WASTE || isFoundation(i)) && !isFaceUp(card)) return false;
      if (isFoundation(i) && (rankOf(card) != c || suitOf(card) != suitOf(p.cards[0]))) return false;
      if (isTableau(i) && c > 0 && isFaceUp(p.cards[c - 1]) && !isFaceUp(card)) return false;
    }
    if (isTableau(i) && p.count > 0) {
      int firstUp = p.count - 1;
      while (firstUp > 0 && isFaceUp(p.cards[firstUp - 1])) --firstUp;
      if (!isFaceUp(p.top()) || runLength(board, i, firstUp) != p.count - firstUp) return false;
    }
  }
  for (const bool s : seen) {
    if (!s) return false;
  }
  // Only the 24 cards dealt to the stock ever cycle through stock and waste.
  return board.pile[STOCK].count + board.pile[WASTE].count <= PILE_CAPACITY;
}

int solvableDealCount(const bool drawThree) { return drawThree ? SOLVABLE_DRAW3_COUNT : SOLVABLE_DRAW1_COUNT; }

void solvableDeck(const bool drawThree, const int index, const int variant, uint8_t out[DECK]) {
  const int count = solvableDealCount(drawThree);
  const uint32_t* seeds = drawThree ? SOLVABLE_DRAW3 : SOLVABLE_DRAW1;
  shuffledDeck(seeds[index >= 0 && index < count ? index : 0], out);
  // Suit swaps that keep colours (within black, within red, black for red) keep the deal winnable.
  uint8_t map[SUITS] = {SPADES, HEARTS, DIAMONDS, CLUBS};
  if (variant & 1) {
    map[SPADES] = CLUBS;
    map[CLUBS] = SPADES;
  }
  if (variant & 2) {
    map[HEARTS] = DIAMONDS;
    map[DIAMONDS] = HEARTS;
  }
  for (int i = 0; i < DECK; ++i) {
    int suit = map[suitOf(out[i])];
    if (variant & 4) {
      static constexpr uint8_t RECOLOUR[SUITS] = {HEARTS, SPADES, CLUBS, DIAMONDS};
      suit = RECOLOUR[suit];
    }
    out[i] = makeCard(suit, rankOf(out[i]));
  }
}

Game::Game(const games::MillisFn millis) : now(millis) { newGame(1, false, false); }

void Game::newGame(const uint32_t seed, const bool drawThree, const bool solvableOnly) {
  uint8_t deck[DECK];
  if (solvableOnly) {
    games::Rng rng(seed);
    const int index = static_cast<int>(rng.below(solvableDealCount(drawThree)));
    solvableDeck(drawThree, index, static_cast<int>(rng.below(8)), deck);
  } else {
    shuffledDeck(seed, deck);
  }
  deal(state, deck);
  undoTop = 0;
  undoCount = 0;
  moves = 0;
  seedValue = seed;
  threeCards = drawThree;
  solvableDeal = solvableOnly;
  won = false;
  clock.reset();
  clock.start(now());
}

const Pile& Game::pile(const int index) const {
  static const Pile EMPTY{};
  return index >= 0 && index < PILES ? state.pile[index] : EMPTY;
}

void Game::push(const Move& m) {
  undoRing[undoTop] = m;
  undoTop = static_cast<uint16_t>((undoTop + 1) % UNDO_DEPTH);
  if (undoCount < UNDO_DEPTH) ++undoCount;  // a full ring drops the oldest move
  if (moves < UINT16_MAX) ++moves;
}

void Game::settle() {
  won = klondike::isWon(state);
  if (won) clock.stop(now());
}

bool Game::move(const int from, const int to, const int count) {
  Move m;
  if (won || !applyMove(state, from, to, count, m)) return false;
  push(m);
  settle();
  return true;
}

bool Game::draw() {
  Move m;
  if (won || !applyDraw(state, threeCards, m)) return false;
  push(m);
  return true;
}

bool Game::undo() {
  if (!canUndo()) return false;
  const uint16_t slot = static_cast<uint16_t>((undoTop + UNDO_DEPTH - 1) % UNDO_DEPTH);
  if (!revert(state, undoRing[slot])) return false;
  undoTop = slot;
  --undoCount;
  if (moves > 0) --moves;
  return true;
}

int Game::autoTarget(const int from, const int count) const {
  if (count == 1) {
    for (int f = FOUNDATION; f < FOUNDATION + FOUNDATIONS; ++f) {
      if (canMove(from, f, 1)) return f;
    }
  }
  int empty = -1;
  for (int t = TABLEAU; t < PILES; ++t) {
    if (!canMove(from, t, count)) continue;
    if (state.pile[t].count > 0) return t;
    if (empty < 0) empty = t;
  }
  // A king already at the bottom of a column gains nothing from another empty one.
  if (empty >= 0 && isTableau(from) && state.pile[from].count == count) return -1;
  return empty;
}

int Game::autoMoveSafe() {
  int moved = 0;
  bool progress = true;
  while (progress && !won) {
    progress = false;
    for (int from = WASTE; from < PILES; ++from) {
      if (isFoundation(from) || state.pile[from].count == 0) continue;
      if (!isSafeToFoundation(state, state.pile[from].top())) continue;
      for (int f = FOUNDATION; f < FOUNDATION + FOUNDATIONS && !progress; ++f) progress = move(from, f, 1);
      if (progress) {
        ++moved;
        break;
      }
    }
  }
  return moved;
}

bool Game::canAutoFinish() const {
  if (won || state.pile[STOCK].count != 0 || state.pile[WASTE].count != 0) return false;
  for (int t = TABLEAU; t < PILES; ++t) {
    const Pile& p = state.pile[t];
    for (int c = 0; c < p.count; ++c) {
      if (!isFaceUp(p.cards[c])) return false;
    }
  }
  return true;
}

int Game::autoFinish() {
  if (!canAutoFinish()) return 0;
  int moved = 0;
  // Every column descends to its top, so the lowest card left is always on top
  // of some column: each sweep moves at least one card.
  bool progress = true;
  while (progress && !won) {
    progress = false;
    for (int from = TABLEAU; from < PILES; ++from) {
      for (int f = FOUNDATION; f < FOUNDATION + FOUNDATIONS; ++f) {
        if (move(from, f, 1)) {
          ++moved;
          progress = true;
          break;
        }
      }
    }
  }
  return moved;
}

bool Game::hint(Move& out) const {
  if (won) return false;
  auto suggest = [&](const int from, const int to, const int count) {
    out = Move{static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint8_t>(count), 0};
    return true;
  };
  // 1. A column's top card to a foundation when that uncovers a card or is safe.
  for (int from = TABLEAU; from < PILES; ++from) {
    const Pile& p = state.pile[from];
    if (p.count == 0) continue;
    const bool uncovers = p.count > 1 && !isFaceUp(p.cards[p.count - 2]);
    if (!uncovers && !isSafeToFoundation(state, p.top())) continue;
    for (int f = FOUNDATION; f < FOUNDATION + FOUNDATIONS; ++f) {
      if (canMove(from, f, 1)) return suggest(from, f, 1);
    }
  }
  // 2. A whole face-up run onto another column when it uncovers a face-down card.
  for (int from = TABLEAU; from < PILES; ++from) {
    const Pile& p = state.pile[from];
    int first = p.count;
    while (first > 0 && isFaceUp(p.cards[first - 1])) --first;
    if (first == 0 || first == p.count) continue;
    const int to = autoTarget(from, p.count - first);
    if (to >= 0 && !isFoundation(to)) return suggest(from, to, p.count - first);
  }
  // 3. The waste card to a foundation, then to a column.
  if (state.pile[WASTE].count > 0) {
    const int to = autoTarget(WASTE, 1);
    if (to >= 0) return suggest(WASTE, to, 1);
  }
  // 4. Any other card to a foundation.
  for (int from = TABLEAU; from < PILES; ++from) {
    for (int f = FOUNDATION; f < FOUNDATION + FOUNDATIONS; ++f) {
      if (canMove(from, f, 1)) return suggest(from, f, 1);
    }
  }
  // 5. The stock.
  if (state.pile[STOCK].count > 0 || state.pile[WASTE].count > 0) {
    out = Move{STOCK, WASTE, 0, Move::DRAW};
    return true;
  }
  return false;
}

void Game::pause() { clock.stop(now()); }

void Game::resume() {
  if (!won) clock.start(now());
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("seed", seedValue);
  w.number("draw", threeCards ? 3 : 1);
  w.number("solvable", solvableDeal ? 1 : 0);
  w.number("moves", moves);
  w.number("ms", elapsedMs());
  w.beginString("piles");
  for (int i = 0; i < PILES; ++i) {
    if (i > 0) w.put('/');
    const Pile& p = state.pile[i];
    for (int c = 0; c < p.count; ++c) {
      w.put(RANK_CHARS[rankOf(p.cards[c])]);
      w.put((isFaceUp(p.cards[c]) ? SUIT_UP : SUIT_DOWN)[suitOf(p.cards[c])]);
    }
  }
  w.endString();
  // Oldest move first: from, to, two hex digits of count, flags.
  w.beginString("undo");
  for (int i = 0; i < undoCount; ++i) {
    const Move& m = undoRing[(undoTop + UNDO_DEPTH - undoCount + i) % UNDO_DEPTH];
    w.put(games::hexDigit(m.from));
    w.put(games::hexDigit(m.to));
    w.put(games::hexDigit(m.count >> 4));
    w.put(games::hexDigit(m.count));
    w.put(games::hexDigit(m.flags));
  }
  w.endString();
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t seed = 0;
  uint32_t drawCount = 0;
  uint32_t solvable = 0;
  uint32_t moveTotal = 0;
  uint32_t ms = 0;
  const char* piles = nullptr;
  const char* history = nullptr;
  size_t pilesLen = 0;
  size_t historyLen = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("seed", seed) || !r.number("draw", drawCount) ||
      (drawCount != 1 && drawCount != 3) || !r.number("solvable", solvable) || solvable > 1 ||
      !r.number("moves", moveTotal) || moveTotal > UINT16_MAX || !r.number("ms", ms) ||
      !r.string("piles", piles, pilesLen) || !r.string("undo", history, historyLen) || historyLen % 5 != 0 ||
      historyLen > 5 * UNDO_DEPTH) {
    return false;
  }

  Board board{};
  int pileIndex = 0;
  for (size_t i = 0; i < pilesLen;) {
    if (piles[i] == '/') {
      if (++pileIndex >= PILES) return false;
      ++i;
      continue;
    }
    Pile& p = board.pile[pileIndex];
    const int card = i + 1 < pilesLen ? parseCard(piles[i], piles[i + 1]) : -1;
    if (card < 0 || p.count >= PILE_CAPACITY) return false;
    p.cards[p.count++] = static_cast<uint8_t>(card);
    i += 2;
  }
  if (pileIndex != PILES - 1 || !isConsistent(board)) return false;

  // Each stored move must undo cleanly and replay to the same board.
  const int count = static_cast<int>(historyLen / 5);
  Board current = board;
  for (int i = count - 1; i >= 0; --i) {
    const char* p = history + 5 * i;
    int digit[5];
    for (int k = 0; k < 5; ++k) {
      digit[k] = games::hexValue(p[k]);
      if (digit[k] < 0) return false;
    }
    const Move m{static_cast<uint8_t>(digit[0]), static_cast<uint8_t>(digit[1]),
                 static_cast<uint8_t>(digit[2] * 16 + digit[3]), static_cast<uint8_t>(digit[4])};
    Board before = current;
    if (!revert(before, m)) return false;
    Board replay = before;
    Move again;
    const bool replayed = (m.flags & (Move::DRAW | Move::RECYCLE)) != 0
                              ? applyDraw(replay, drawCount == 3, again)
                              : applyMove(replay, m.from, m.to, m.count, again);
    if (!replayed || again.from != m.from || again.to != m.to || again.count != m.count || again.flags != m.flags ||
        !sameBoard(replay, current)) {
      return false;
    }
    current = before;
  }

  state = board;
  for (int i = 0; i < count; ++i) {
    const char* p = history + 5 * i;
    undoRing[i] = Move{static_cast<uint8_t>(games::hexValue(p[0])), static_cast<uint8_t>(games::hexValue(p[1])),
                       static_cast<uint8_t>(games::hexValue(p[2]) * 16 + games::hexValue(p[3])),
                       static_cast<uint8_t>(games::hexValue(p[4]))};
  }
  undoTop = static_cast<uint16_t>(count % UNDO_DEPTH);
  undoCount = static_cast<uint16_t>(count);
  moves = static_cast<uint16_t>(moveTotal);
  seedValue = seed;
  threeCards = drawCount == 3;
  solvableDeal = solvable == 1;
  won = klondike::isWon(state);
  clock.reset(ms);
  return true;
}

}  // namespace klondike
