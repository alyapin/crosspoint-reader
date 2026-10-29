#include "Reversi.h"

#include <algorithm>

#include "GameText.h"

namespace reversi {

namespace {

constexpr uint32_t SAVE_VERSION = 1;
constexpr uint64_t NOT_LEFT = 0xFEFEFEFEFEFEFEFEull;
constexpr uint64_t NOT_RIGHT = 0x7F7F7F7F7F7F7F7Full;
constexpr uint64_t START_BLACK = (1ull << 28) | (1ull << 35);
constexpr uint64_t START_WHITE = (1ull << 27) | (1ull << 36);
constexpr int32_t INF = 1000000;
constexpr int32_t WIN = 100000;
constexpr int32_t MOBILITY_WEIGHT = 8;
constexpr uint32_t CHECK_EVERY = 64;
constexpr const char* SCORE_KEYS[MODE_COUNT][3] = {
    {"w0", "l0", "d0"}, {"w1", "l1", "d1"}, {"w2", "l2", "d2"}, {"w3", "l3", "d3"}};

constexpr Limits LIMITS[MODE_COUNT - 1] = {
    {1, 0, false, 30, 2000, 1500},
    {3, 0, false, 0, 20000, 1500},
    {6, 10, true, 0, 120000, 1500},
};

constexpr int8_t WEIGHTS[CELLS] = {
    100, -20, 10, 5,  5,  10, -20, 100,  //
    -20, -50, -2, -2, -2, -2, -50, -20,  //
    10,  -2,  -1, -1, -1, -1, -2,  10,   //
    5,   -2,  -1, -1, -1, -1, -2,  5,    //
    5,   -2,  -1, -1, -1, -1, -2,  5,    //
    10,  -2,  -1, -1, -1, -1, -2,  10,   //
    -20, -50, -2, -2, -2, -2, -50, -20,  //
    100, -20, 10, 5,  5,  10, -20, 100,
};

struct Order {
  uint8_t square[CELLS];
};

constexpr Order makeOrder() {
  Order order{};
  int n = 0;
  for (int w = 100; w >= -50; --w) {
    for (int s = 0; s < CELLS; ++s) {
      if (WEIGHTS[s] == w) order.square[n++] = static_cast<uint8_t>(s);
    }
  }
  return order;
}

constexpr Order ORDER = makeOrder();

template <int D>
uint64_t shift(const uint64_t b) {
  if constexpr (D == 1) return (b << 1) & NOT_LEFT;
  if constexpr (D == -1) return (b >> 1) & NOT_RIGHT;
  if constexpr (D == 8) return b << 8;
  if constexpr (D == -8) return b >> 8;
  if constexpr (D == 9) return (b << 9) & NOT_LEFT;
  if constexpr (D == 7) return (b << 7) & NOT_RIGHT;
  if constexpr (D == -7) return (b >> 7) & NOT_LEFT;
  if constexpr (D == -9) return (b >> 9) & NOT_RIGHT;
  return 0;
}

template <int D>
uint64_t movesAlong(const uint64_t me, const uint64_t opp, const uint64_t empty) {
  uint64_t x = shift<D>(me) & opp;
  for (int i = 0; i < 5; ++i) x |= shift<D>(x) & opp;
  return shift<D>(x) & empty;
}

template <int D>
uint64_t flipsAlong(const uint64_t me, const uint64_t opp, const uint64_t from) {
  uint64_t run = 0;
  uint64_t x = shift<D>(from);
  while (x & opp) {
    run |= x;
    x = shift<D>(x);
  }
  return (x & me) ? run : 0;
}

int32_t finalValue(const uint64_t me, const uint64_t opp) {
  const int diff = popcount(me) - popcount(opp);
  return diff > 0 ? WIN + diff : diff < 0 ? -WIN + diff : 0;
}

Disc opposite(const Disc d) { return d == Disc::Black ? Disc::White : Disc::Black; }

}  // namespace

int popcount(const uint64_t bits) { return __builtin_popcountll(bits); }

uint64_t legalMoves(const uint64_t me, const uint64_t opp) {
  const uint64_t empty = ~(me | opp);
  return movesAlong<1>(me, opp, empty) | movesAlong<-1>(me, opp, empty) | movesAlong<8>(me, opp, empty) |
         movesAlong<-8>(me, opp, empty) | movesAlong<9>(me, opp, empty) | movesAlong<7>(me, opp, empty) |
         movesAlong<-7>(me, opp, empty) | movesAlong<-9>(me, opp, empty);
}

uint64_t flips(const uint64_t me, const uint64_t opp, const int square) {
  if (square < 0 || square >= CELLS) return 0;
  const uint64_t from = 1ull << square;
  if ((me | opp) & from) return 0;
  return flipsAlong<1>(me, opp, from) | flipsAlong<-1>(me, opp, from) | flipsAlong<8>(me, opp, from) |
         flipsAlong<-8>(me, opp, from) | flipsAlong<9>(me, opp, from) | flipsAlong<7>(me, opp, from) |
         flipsAlong<-7>(me, opp, from) | flipsAlong<-9>(me, opp, from);
}

const Limits& limitsFor(const Mode mode) {
  return LIMITS[std::min(static_cast<int>(mode), static_cast<int>(Mode::Hard))];
}

// --- engine ------------------------------------------------------------------

void Engine::start(const uint64_t me, const uint64_t opp, const Limits& lim, const uint32_t seed,
                   const uint32_t nowMs) {
  rootMe = me;
  rootOpp = opp;
  limits = lim;
  nodeCount = 0;
  startMs = nowMs;
  chosen = -1;
  completedDepth = 0;
  rootCount = 0;
  const uint64_t moves = legalMoves(me, opp);
  for (const uint8_t s : ORDER.square) {
    if (moves >> s & 1) rootMoves[rootCount++] = s;
  }
  games::Rng rng(seed);
  // Shuffle within each weight, so equal moves vary from game to game.
  for (int i = rootCount - 1; i > 0; --i) {
    const int j = static_cast<int>(rng.below(static_cast<uint32_t>(i + 1)));
    if (WEIGHTS[rootMoves[i]] == WEIGHTS[rootMoves[j]]) std::swap(rootMoves[i], rootMoves[j]);
  }
  state = State::Done;
  if (rootCount == 0) return;
  chosen = rootMoves[0];
  if (rootCount == 1) return;
  if (rng.below(100) < limits.randomPercent) {
    chosen = rootMoves[rng.below(static_cast<uint32_t>(rootCount))];
    return;
  }
  const int empties = CELLS - popcount(me | opp);
  if (empties <= limits.exactEmpties) limits.depth = static_cast<uint8_t>(std::max<int>(limits.depth, empties));
  limits.depth = static_cast<uint8_t>(std::min<int>(limits.depth, empties));
  iterationDepth = 1;
  beginIteration();
  state = State::Searching;
}

void Engine::beginIteration() {
  top = 0;
  stack[0] = {rootMe, rootOpp, 0, -INF, INF, -INF, static_cast<int8_t>(iterationDepth), 0, false};
  rootBest = -1;
}

bool Engine::enter(const uint64_t me, const uint64_t opp, const int depth, const int32_t alpha, const int32_t beta) {
  ++nodeCount;
  const uint64_t moves = legalMoves(me, opp);
  if (depth <= 0 || moves == 0 || top + 1 >= MAX_PLY) {
    const uint64_t replies = legalMoves(opp, me);
    if (moves == 0 && replies == 0) {
      result = finalValue(me, opp);
      return false;
    }
    if (depth <= 0 || top + 1 >= MAX_PLY) {
      result = evaluate(me, opp);
      if (limits.mobility) result += MOBILITY_WEIGHT * (popcount(moves) - popcount(replies));
      return false;
    }
  }
  stack[++top] = {me, opp, moves, alpha, beta, -INF, static_cast<int8_t>(depth), 0, moves == 0};
  return true;
}

bool Engine::descend(Frame& f) {
  if (f.best >= f.beta) return false;
  int square = -1;
  if (f.pass) {
    if (f.next++ > 0) return false;
  } else if (top == 0) {
    if (f.next >= rootCount) return false;
    square = rootMoves[f.next++];
  } else {
    while (f.next < CELLS && !(f.todo >> ORDER.square[f.next] & 1)) ++f.next;
    if (f.next >= CELLS) return false;
    square = ORDER.square[f.next++];
  }
  const int32_t alpha = std::max(f.alpha, f.best);
  bool pushed;
  if (square < 0) {
    pushed = enter(f.opp, f.me, f.depth, -f.beta, -alpha);
  } else {
    const uint64_t turned = flips(f.me, f.opp, square);
    pushed = enter(f.opp ^ turned, f.me | turned | (1ull << square), f.depth - 1, -f.beta, -alpha);
  }
  if (!pushed) {
    const int32_t value = -result;
    if (value > f.best) {
      f.best = value;
      if (&f == &stack[0]) rootBest = square;
    }
  }
  return true;
}

void Engine::finishIteration() {
  completedDepth = iterationDepth;
  chosen = rootBest;
  const auto at = std::find(rootMoves, rootMoves + rootCount, static_cast<uint8_t>(rootBest));
  std::rotate(rootMoves, at, at + 1);
  if (iterationDepth >= limits.depth) {
    state = State::Done;
    return;
  }
  ++iterationDepth;
  beginIteration();
}

bool Engine::step(const games::MillisFn now, const uint32_t sliceMs) {
  if (state != State::Searching) return state == State::Done;
  const uint32_t sliceStart = now();
  uint32_t checked = nodeCount;
  for (;;) {
    if (nodeCount - checked >= CHECK_EVERY) {
      checked = nodeCount;
      const uint32_t t = now();
      if (completedDepth > 0 && (nodeCount >= limits.maxNodes || t - startMs >= limits.moveTimeMs)) {
        if (rootBest >= 0) chosen = rootBest;
        state = State::Done;
        return true;
      }
      if (t - sliceStart >= sliceMs) return false;
    }
    Frame& f = stack[top];
    if (descend(f)) continue;
    const int32_t value = -f.best;
    const bool root = top == 0;
    --top;
    if (root) {
      finishIteration();
      if (state == State::Done) return true;
      continue;
    }
    Frame& parent = stack[top];
    if (value > parent.best) {
      parent.best = value;
      if (top == 0) rootBest = rootMoves[parent.next - 1];
    }
  }
}

int32_t Engine::evaluate(const uint64_t me, const uint64_t opp) const {
  const uint64_t occupied = me | opp;
  int32_t score = 0;
  for (uint64_t b = occupied; b != 0; b &= b - 1) {
    const int s = __builtin_ctzll(b);
    int w = WEIGHTS[s];
    // Next to a taken corner a square is no longer a liability.
    if (w <= -20) {
      const int corner = (s < 32 ? 0 : 56) + (s % 8 < 4 ? 0 : 7);
      if (occupied >> corner & 1) w = 0;
    }
    score += (me >> s & 1) ? w : -w;
  }
  return score;
}

// --- game --------------------------------------------------------------------

void Game::newGame(const Mode mode, const uint32_t seed) {
  gameMode = mode;
  seedValue = seed;
  rebuild(0);
}

Disc Game::at(const int square) const {
  if (black >> square & 1) return Disc::Black;
  if (white >> square & 1) return Disc::White;
  return Disc::None;
}

uint64_t Game::moves() const {
  if (isOver()) return 0;
  return side == Disc::Black ? legalMoves(black, white) : legalMoves(white, black);
}

int Game::count(const Disc disc) const { return popcount(disc == Disc::Black ? black : white); }

void Game::place(const int square) {
  const bool isBlack = side == Disc::Black;
  uint64_t& me = isBlack ? black : white;
  uint64_t& opp = isBlack ? white : black;
  const uint64_t turned = flips(me, opp, square);
  me |= turned | (1ull << square);
  opp ^= turned;
  if (!isBlack) whiteMoves |= 1ull << moveCount;
  history[moveCount++] = static_cast<uint8_t>(square);
  lastBy = side;
  if (passedSide == side) passedSide = Disc::None;
  if (legalMoves(opp, me) != 0) {
    side = opposite(side);
  } else if (legalMoves(me, opp) != 0) {
    passedSide = opposite(side);
  } else {
    const int b = popcount(black);
    const int w = popcount(white);
    state = b > w ? Status::BlackWon : b < w ? Status::WhiteWon : Status::Draw;
  }
}

void Game::rebuild(const int count) {
  black = START_BLACK;
  white = START_WHITE;
  side = Disc::Black;
  lastBy = Disc::None;
  passedSide = Disc::None;
  resignedSide = Disc::None;
  state = Status::Playing;
  whiteMoves = 0;
  moveCount = 0;
  for (int i = 0; i < count; ++i) place(history[i]);
}

void Game::tally() {
  Score& s = scores[static_cast<int>(gameMode)];
  uint16_t& n = state == Status::BlackWon ? s.won : state == Status::WhiteWon ? s.lost : s.draws;
  ++n;
}

bool Game::play(const int square) {
  if (square < 0 || square >= CELLS || (moves() >> square & 1) == 0) return false;
  place(square);
  if (isOver()) tally();
  return true;
}

bool Game::canUndo() const { return !isOver() && moveCount > 0; }

bool Game::resign() {
  if (isOver()) return false;
  resignedSide = vsComputer() ? Disc::Black : side;
  state = resignedSide == Disc::Black ? Status::WhiteWon : Status::BlackWon;
  tally();
  return true;
}

bool Game::undo() {
  if (!canUndo()) return false;
  int n = moveCount - 1;
  if (vsComputer()) {
    while (n > 0 && (whiteMoves >> n & 1)) --n;
  }
  rebuild(n);
  return true;
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("mode", static_cast<uint32_t>(gameMode));
  w.number("seed", seedValue);
  w.beginString("moves");
  for (int i = 0; i < moveCount; ++i) {
    w.put(static_cast<char>('a' + history[i] % SIZE));
    w.put(static_cast<char>('1' + history[i] / SIZE));
  }
  w.endString();
  if (resigned()) w.number("resign", static_cast<uint32_t>(resignedSide));
  for (int i = 0; i < MODE_COUNT; ++i) {
    w.number(SCORE_KEYS[i][0], scores[i].won);
    w.number(SCORE_KEYS[i][1], scores[i].lost);
    w.number(SCORE_KEYS[i][2], scores[i].draws);
  }
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t mode = 0;
  uint32_t seed = 0;
  const char* moves = nullptr;
  size_t n = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("mode", mode) || mode >= MODE_COUNT ||
      !r.number("seed", seed) || !r.string("moves", moves, n) || n % 2 != 0 || n > 2 * MAX_MOVES) {
    return false;
  }
  Game g;
  for (int i = 0; i < MODE_COUNT; ++i) {
    uint32_t v[3];
    for (int k = 0; k < 3; ++k) {
      if (!r.number(SCORE_KEYS[i][k], v[k]) || v[k] > UINT16_MAX) return false;
    }
    g.scores[i] = {static_cast<uint16_t>(v[0]), static_cast<uint16_t>(v[1]), static_cast<uint16_t>(v[2])};
  }
  g.newGame(static_cast<Mode>(mode), seed);
  for (size_t i = 0; i < n; i += 2) {
    const int col = moves[i] - 'a';
    const int row = moves[i + 1] - '1';
    if (col < 0 || col >= SIZE || row < 0 || row >= SIZE) return false;
    const int square = row * SIZE + col;
    if (g.isOver() || (g.moves() >> square & 1) == 0) return false;
    g.place(square);
  }
  // A resigned game replays to a running position; the score already counts it.
  uint32_t resign = 0;
  if (r.number("resign", resign)) {
    if (g.isOver() || (resign != static_cast<uint32_t>(Disc::Black) && resign != static_cast<uint32_t>(Disc::White))) {
      return false;
    }
    g.resignedSide = static_cast<Disc>(resign);
    g.state = g.resignedSide == Disc::Black ? Status::WhiteWon : Status::BlackWon;
  }
  *this = g;
  return true;
}

}  // namespace reversi
