#include "ChessEngine.h"

#include <cstring>

namespace chess {
namespace {

// Low levels cap depth and pick randomly among near-best moves, so they miss
// tactics and drift positionally like a weaker human rather than playing
// perfect moves interleaved with random ones.
// clang-format off
constexpr EngineLevel LEVELS[MAX_LEVEL] = {
    // depth             margin blunder% blunderMargin nodes    ms
    {1,                  150,   25,      400,          20000,   1500},
    {2,                  80,    12,      250,          60000,   1500},
    {3,                  40,    6,       150,          200000,  2000},
    {5,                  15,    0,       0,            600000,  2500},
    {Engine::MAX_DEPTH,  0,     0,       0,            0,       4000},
};
// clang-format on

constexpr int16_t MG_VALUE[7] = {0, 100, 320, 330, 500, 950, 0};
constexpr int16_t EG_VALUE[7] = {0, 120, 300, 320, 530, 950, 0};
constexpr int PHASE_WEIGHT[7] = {0, 0, 1, 1, 2, 4, 0};
constexpr int PHASE_TOTAL = 24;
constexpr int TEMPO = 10;

// White's point of view, rank 1 first (index == square); Black reads square ^ 56.
// clang-format off
constexpr int8_t PAWN_PST[64] = {
   0,  0,  0,  0,  0,  0,  0,  0,
   5, 10, 10,-20,-20, 10, 10,  5,
   5, -5,-10,  0,  0,-10, -5,  5,
   0,  0,  0, 20, 20,  0,  0,  0,
   5,  5, 10, 25, 25, 10,  5,  5,
  10, 10, 20, 30, 30, 20, 10, 10,
  50, 50, 50, 50, 50, 50, 50, 50,
   0,  0,  0,  0,  0,  0,  0,  0,
};
constexpr int8_t KNIGHT_PST[64] = {
 -50,-40,-30,-30,-30,-30,-40,-50,
 -40,-20,  0,  5,  5,  0,-20,-40,
 -30,  5, 10, 15, 15, 10,  5,-30,
 -30,  0, 15, 20, 20, 15,  0,-30,
 -30,  5, 15, 20, 20, 15,  5,-30,
 -30,  0, 10, 15, 15, 10,  0,-30,
 -40,-20,  0,  0,  0,  0,-20,-40,
 -50,-40,-30,-30,-30,-30,-40,-50,
};
constexpr int8_t BISHOP_PST[64] = {
 -20,-10,-10,-10,-10,-10,-10,-20,
 -10,  5,  0,  0,  0,  0,  5,-10,
 -10, 10, 10, 10, 10, 10, 10,-10,
 -10,  0, 10, 10, 10, 10,  0,-10,
 -10,  5,  5, 10, 10,  5,  5,-10,
 -10,  0,  5, 10, 10,  5,  0,-10,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -20,-10,-10,-10,-10,-10,-10,-20,
};
constexpr int8_t ROOK_PST[64] = {
   0,  0,  0,  5,  5,  0,  0,  0,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
  -5,  0,  0,  0,  0,  0,  0, -5,
   5, 10, 10, 10, 10, 10, 10,  5,
   0,  0,  0,  0,  0,  0,  0,  0,
};
constexpr int8_t QUEEN_PST[64] = {
 -20,-10,-10, -5, -5,-10,-10,-20,
 -10,  0,  5,  0,  0,  0,  0,-10,
 -10,  5,  5,  5,  5,  5,  0,-10,
   0,  0,  5,  5,  5,  5,  0, -5,
  -5,  0,  5,  5,  5,  5,  0, -5,
 -10,  0,  5,  5,  5,  5,  0,-10,
 -10,  0,  0,  0,  0,  0,  0,-10,
 -20,-10,-10, -5, -5,-10,-10,-20,
};
constexpr int8_t KING_MG_PST[64] = {
  20, 30, 10,  0,  0, 10, 30, 20,
  20, 20,  0,  0,  0,  0, 20, 20,
 -10,-20,-20,-20,-20,-20,-20,-10,
 -20,-30,-30,-40,-40,-30,-30,-20,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
 -30,-40,-40,-50,-50,-40,-40,-30,
};
constexpr int8_t KING_EG_PST[64] = {
 -50,-30,-30,-30,-30,-30,-30,-50,
 -30,-30,  0,  0,  0,  0,-30,-30,
 -30,-10, 20, 30, 30, 20,-10,-30,
 -30,-10, 30, 40, 40, 30,-10,-30,
 -30,-10, 30, 40, 40, 30,-10,-30,
 -30,-10, 20, 30, 30, 20,-10,-30,
 -30,-20,-10,  0,  0,-10,-20,-30,
 -50,-40,-30,-20,-20,-30,-40,-50,
};
// clang-format on

constexpr const int8_t* PST_MG[7] = {PAWN_PST, PAWN_PST, KNIGHT_PST, BISHOP_PST, ROOK_PST, QUEEN_PST, KING_MG_PST};
constexpr const int8_t* PST_EG[7] = {PAWN_PST, PAWN_PST, KNIGHT_PST, BISHOP_PST, ROOK_PST, QUEEN_PST, KING_EG_PST};

// Indexed by rank from the pawn owner's side.
constexpr int16_t PASSED_MG[8] = {0, 5, 10, 15, 25, 40, 60, 0};
constexpr int16_t PASSED_EG[8] = {0, 10, 20, 35, 55, 85, 120, 0};

enum : uint8_t { PHASE_ENTER, PHASE_GENERATE, PHASE_MOVES };
// Which search of the current move is in flight, for PVS / LMR re-searches.
enum : uint8_t { CHILD_FULL, CHILD_ZERO, CHILD_REDUCED, CHILD_NULL };
enum : uint8_t { BOUND_NONE, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT };

constexpr int16_t ORDER_TT = 30000;
constexpr int16_t ORDER_CAPTURE = 20000;
constexpr int16_t ORDER_KILLER = 15000;
constexpr int16_t HISTORY_MAX = 14000;

int centerDistance(const int sq) {
  const int f = fileOf(static_cast<Square>(sq));
  const int r = rankOf(static_cast<Square>(sq));
  return (f < 4 ? 3 - f : f - 4) + (r < 4 ? 3 - r : r - 4);
}

// Drives a bare king to the edge and brings the winning king closer, so the
// search can finish K+R v K and similar endings it cannot see to the end.
int mopUp(const int winner88, const int loser88) {
  const int w = from88(winner88);
  const int l = from88(loser88);
  const int fileGap = fileOf(static_cast<Square>(w)) - fileOf(static_cast<Square>(l));
  const int rankGap = rankOf(static_cast<Square>(w)) - rankOf(static_cast<Square>(l));
  const int distance = (fileGap < 0 ? -fileGap : fileGap) + (rankGap < 0 ? -rankGap : rankGap);
  return 10 * centerDistance(l) + 4 * (14 - distance);
}

uint16_t encodeMove(const Move& m) { return static_cast<uint16_t>(m.from | (m.to << 6) | (m.promo << 12)); }

Move decodeMove(const uint16_t v) {
  Move m;
  m.from = static_cast<Square>(v & 63);
  m.to = static_cast<Square>((v >> 6) & 63);
  m.promo = static_cast<PieceType>((v >> 12) & 7);
  return m;
}

// Mate scores are stored relative to the node so they stay valid at any ply.
int16_t toTt(const int16_t s, const int ply) {
  if (s >= MATE_BOUND) return static_cast<int16_t>(s + ply);
  if (s <= -MATE_BOUND) return static_cast<int16_t>(s - ply);
  return s;
}

int16_t fromTt(const int16_t s, const int ply) {
  if (s >= MATE_BOUND) return static_cast<int16_t>(s - ply);
  if (s <= -MATE_BOUND) return static_cast<int16_t>(s + ply);
  return s;
}

int16_t clampScore(const int s) {
  return static_cast<int16_t>(s > SCORE_INFINITY ? SCORE_INFINITY : (s < -SCORE_INFINITY ? -SCORE_INFINITY : s));
}

}  // namespace

const EngineLevel& engineLevel(const int level) {
  const int clamped = level < MIN_LEVEL ? MIN_LEVEL : (level > MAX_LEVEL ? MAX_LEVEL : level);
  return LEVELS[clamped - 1];
}

int evaluate(const Position& pos) {
  int mg[2] = {0, 0};
  int eg[2] = {0, 0};
  int phase = 0;
  int bishops[2] = {0, 0};
  int pawns[2] = {0, 0};
  int knights = 0;
  int heavy = 0;
  int bishopColors = 0;
  uint8_t pawnFiles[2][8] = {};
  uint8_t rookFiles[2][8] = {};
  uint8_t pawnSquares[2][8];
  int8_t whiteMinRank[8];
  int8_t blackMaxRank[8];
  memset(whiteMinRank, 8, sizeof(whiteMinRank));
  memset(blackMaxRank, -1, sizeof(blackMaxRank));

  for (int sq = 0; sq < 128; ++sq) {
    if (sq & 0x88) {
      sq += 7;
      continue;
    }
    const Piece p = pos.board[sq];
    if (p == 0) continue;
    const Color c = colorOf(p);
    const PieceType t = typeOf(p);
    const int s = from88(sq);
    const int idx = c == WHITE ? s : (s ^ 56);
    mg[c] += MG_VALUE[t] + PST_MG[t][idx];
    eg[c] += EG_VALUE[t] + PST_EG[t][idx];
    phase += PHASE_WEIGHT[t];
    const int file = fileOf(static_cast<Square>(s));
    const int rank = rankOf(static_cast<Square>(s));
    switch (t) {
      case PAWN:
        ++pawnFiles[c][file];
        pawnSquares[c][pawns[c]++] = static_cast<uint8_t>(s);
        if (c == WHITE && rank < whiteMinRank[file]) whiteMinRank[file] = static_cast<int8_t>(rank);
        if (c == BLACK && rank > blackMaxRank[file]) blackMaxRank[file] = static_cast<int8_t>(rank);
        break;
      case KNIGHT:
        ++knights;
        break;
      case BISHOP:
        ++bishops[c];
        bishopColors |= 1 << ((file + rank) & 1);
        break;
      case ROOK:
        ++heavy;
        ++rookFiles[c][file];
        break;
      case QUEEN:
        ++heavy;
        break;
      default:
        break;
    }
  }

  if (pawns[WHITE] + pawns[BLACK] == 0 && heavy == 0 &&
      ((knights == 0 && bishopColors != 3) || (knights == 1 && bishopColors == 0))) {
    return 0;
  }

  for (int c = 0; c < 2; ++c) {
    const int them = c ^ 1;
    for (int i = 0; i < pawns[c]; ++i) {
      const int s = pawnSquares[c][i];
      const int file = s & 7;
      const int rank = s >> 3;
      if ((file == 0 || pawnFiles[c][file - 1] == 0) && (file == 7 || pawnFiles[c][file + 1] == 0)) {
        mg[c] -= 10;
        eg[c] -= 15;
      }
      bool passed = true;
      for (int f = file > 0 ? file - 1 : 0; f <= (file < 7 ? file + 1 : 7); ++f) {
        if (c == WHITE ? blackMaxRank[f] > rank : whiteMinRank[f] < rank) passed = false;
      }
      if (passed) {
        const int relative = c == WHITE ? rank : 7 - rank;
        mg[c] += PASSED_MG[relative];
        eg[c] += PASSED_EG[relative];
      }
    }
    for (int f = 0; f < 8; ++f) {
      if (pawnFiles[c][f] > 1) {
        mg[c] -= 10 * (pawnFiles[c][f] - 1);
        eg[c] -= 20 * (pawnFiles[c][f] - 1);
      }
      if (rookFiles[c][f] != 0 && pawnFiles[c][f] == 0) {
        const int bonus = pawnFiles[them][f] == 0 ? 20 : 10;
        mg[c] += bonus * rookFiles[c][f];
        eg[c] += bonus / 2 * rookFiles[c][f];
      }
    }
    if (bishops[c] >= 2) {
      mg[c] += 30;
      eg[c] += 50;
    }
  }

  if (phase > PHASE_TOTAL) phase = PHASE_TOTAL;
  int score = ((mg[WHITE] - mg[BLACK]) * phase + (eg[WHITE] - eg[BLACK]) * (PHASE_TOTAL - phase)) / PHASE_TOTAL;
  if (score > 200 && pawns[BLACK] == 0) {
    score += mopUp(pos.king[WHITE], pos.king[BLACK]);
  } else if (score < -200 && pawns[WHITE] == 0) {
    score -= mopUp(pos.king[BLACK], pos.king[WHITE]);
  }
  return (pos.side == WHITE ? score : -score) + TEMPO;
}

Engine::Engine() { newGame(); }

void Engine::newGame() {
  memset(tt, 0, sizeof(tt));
  memset(history, 0, sizeof(history));
  memset(killers, 0, sizeof(killers));
  generation = 0;
  state = State::Idle;
}

void Engine::seed(const uint32_t value) { rng = value != 0 ? value : 0x9E3779B9u; }

uint32_t Engine::nextRandom() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

void Engine::start(const Position& root, const uint64_t* keys, const int keyCount, const SearchLimits& searchLimits,
                   const uint32_t nowMs) {
  limits = searchLimits;
  if (limits.maxDepth < 1) limits.maxDepth = 1;
  if (limits.maxDepth > MAX_DEPTH) limits.maxDepth = MAX_DEPTH;
  searchInfo = SearchInfo{};
  nodes = 0;
  ticks = 0;
  startMs = nowMs;
  chosen = Move{};
  ++generation;
  memset(killers, 0, sizeof(killers));
  for (auto& row : history) {
    for (auto& h : row) h = static_cast<int16_t>(h / 2);
  }

  const int keep = keys != nullptr && keyCount > 0 ? (keyCount < HISTORY_KEYS ? keyCount : HISTORY_KEYS) : 1;
  if (keys != nullptr && keyCount > 0) memcpy(path, keys + keyCount - keep, sizeof(uint64_t) * keep);
  pathBase = keep - 1;
  path[pathBase] = root.key;

  Frame& rootFrame = frames[0];
  rootFrame.pos = root;
  rootFrame.moveBegin = rootFrame.moveEnd = rootFrame.moveNext = 0;
  const TtEntry* entry = probeTt(root.key);
  rootFrame.ttMove = entry != nullptr ? decodeMove(entry->move) : Move{};

  // The arena is free between searches; use it to filter the root moves.
  const int n = generatePseudo(root, arena, false);
  rootCount = 0;
  for (int i = 0; i < n; ++i) {
    if (makeMove(root, arena[i], frames[1].pos)) {
      rootMoves[rootCount] = arena[i];
      rootScores[rootCount] = scoreMove(rootFrame, arena[i], 0);
      ++rootCount;
    }
  }
  for (int i = 1; i < rootCount; ++i) {
    const Move m = rootMoves[i];
    const int16_t s = rootScores[i];
    int j = i;
    for (; j > 0 && rootScores[j - 1] < s; --j) {
      rootMoves[j] = rootMoves[j - 1];
      rootScores[j] = rootScores[j - 1];
    }
    rootMoves[j] = m;
    rootScores[j] = s;
  }

  sp = 0;
  hasReturn = false;
  completedDepth = 0;
  iterationDepth = 1;
  searchMargin = limits.marginCp;
  if (limits.blunderPercent != 0 && nextRandom() % 100 < limits.blunderPercent) {
    searchMargin = limits.blunderMarginCp;
  }
  if (rootCount == 0) {
    state = State::Done;
    return;
  }
  state = State::Searching;
  startIteration();
}

bool Engine::step(const MillisFn now, const uint32_t sliceMs) {
  if (state != State::Searching) return state == State::Done;
  clock = now;
  const uint32_t sliceStart = now();
  for (;;) {
    advance();
    if (state != State::Searching) return true;
    if (limits.maxNodes != 0 && nodes >= limits.maxNodes) {
      finish(true);
      return true;
    }
    if ((++ticks & 63) == 0) {
      const uint32_t t = now();
      if (limits.hardMs != 0 && t - startMs >= limits.hardMs) {
        finish(true);
        return true;
      }
      if (t - sliceStart >= sliceMs) return false;
    }
  }
}

void Engine::cancel() {
  state = State::Idle;
  sp = 0;
  hasReturn = false;
}

void Engine::advance() {
  if (hasReturn) {
    hasReturn = false;
    if (sp == 0) {
      rootChildReturned(returnValue);
    } else {
      childReturned(frames[sp], returnValue);
    }
    return;
  }
  if (sp == 0) {
    advanceRoot();
    return;
  }
  Frame& f = frames[sp];
  switch (f.phase) {
    case PHASE_ENTER:
      enter(f);
      break;
    case PHASE_GENERATE:
      generate(f);
      break;
    default:
      nextMove(f);
      break;
  }
}

void Engine::startIteration() {
  rootIndex = 0;
  rootBestIndex = -1;
  rootBest = -SCORE_INFINITY;
}

void Engine::advanceRoot() {
  if (rootIndex >= rootCount) {
    iterationDone();
    return;
  }
  makeMove(frames[0].pos, rootMoves[rootIndex], frames[1].pos);
  const int depth = iterationDepth - 1;
  if (rootIndex == 0) {
    rootWindow = -SCORE_INFINITY;
    rootFullWindow = true;
    push(depth, -SCORE_INFINITY, SCORE_INFINITY, false);
    return;
  }
  // With a margin, scores inside it must be exact so the random pick can use them.
  int window = searchMargin > 0 ? rootBest - searchMargin : rootBest;
  if (window < -SCORE_INFINITY + 1) window = -SCORE_INFINITY + 1;
  if (window > SCORE_INFINITY - 1) window = SCORE_INFINITY - 1;
  rootWindow = static_cast<int16_t>(window);
  rootFullWindow = false;
  push(depth, static_cast<int16_t>(-(window + 1)), static_cast<int16_t>(-window), false);
}

void Engine::rootChildReturned(const int16_t value) {
  const int16_t score = static_cast<int16_t>(-value);
  if (!rootFullWindow && score > rootWindow) {
    rootFullWindow = true;
    push(iterationDepth - 1, -SCORE_INFINITY, static_cast<int16_t>(-rootWindow), false);
    return;
  }
  rootScores[rootIndex] = score;
  rootExact[rootIndex] = rootFullWindow && score > rootWindow ? 1 : 0;
  if (rootBestIndex < 0 || score > rootBest) {
    rootBest = score;
    rootBestIndex = rootIndex;
  }
  ++rootIndex;
}

void Engine::iterationDone() {
  completedDepth = iterationDepth;
  for (int i = 1; i < rootCount; ++i) {
    const Move m = rootMoves[i];
    const int16_t s = rootScores[i];
    const uint8_t e = rootExact[i];
    int j = i;
    for (; j > 0 && rootScores[j - 1] < s; --j) {
      rootMoves[j] = rootMoves[j - 1];
      rootScores[j] = rootScores[j - 1];
      rootExact[j] = rootExact[j - 1];
    }
    rootMoves[j] = m;
    rootScores[j] = s;
    rootExact[j] = e;
  }
  memcpy(completedScores, rootScores, sizeof(int16_t) * rootCount);
  memcpy(completedExact, rootExact, rootCount);

  const int16_t best = rootScores[0];
  const int magnitude = best < 0 ? -best : best;
  const bool mateSeen = magnitude >= MATE_BOUND && SCORE_MATE - magnitude <= iterationDepth;
  const uint32_t elapsed = clock != nullptr ? clock() - startMs : 0;
  if (iterationDepth >= limits.maxDepth || rootCount == 1 || mateSeen ||
      (limits.softMs != 0 && elapsed >= limits.softMs)) {
    finish(false);
    return;
  }
  ++iterationDepth;
  startIteration();
}

Move Engine::pickMove(const int16_t* scores, const uint8_t* exact, const int count) {
  int best = -SCORE_INFINITY - 1;
  for (int i = 0; i < count; ++i) {
    if (exact[i] && scores[i] > best) best = scores[i];
  }
  if (best < -SCORE_INFINITY) return rootMoves[0];
  const int floor = best - searchMargin;
  uint32_t total = 0;
  for (int i = 0; i < count; ++i) {
    if (exact[i] && scores[i] >= floor) total += static_cast<uint32_t>(scores[i] - floor + 1);
  }
  uint32_t r = nextRandom() % total;
  for (int i = 0; i < count; ++i) {
    if (!exact[i] || scores[i] < floor) continue;
    const uint32_t weight = static_cast<uint32_t>(scores[i] - floor + 1);
    if (r < weight) return rootMoves[i];
    r -= weight;
  }
  return rootMoves[0];
}

void Engine::finish(const bool aborted) {
  int16_t score = completedDepth > 0 ? completedScores[0] : rootBest;
  if (searchMargin > 0) {
    if (completedDepth > 0) {
      chosen = pickMove(completedScores, completedExact, rootCount);
    } else if (rootIndex > 0) {
      chosen = pickMove(rootScores, rootExact, rootIndex);
    } else {
      chosen = rootMoves[0];
    }
  } else if (aborted && rootIndex > 0 && rootBestIndex >= 0) {
    // The previous best was searched first at the new depth, so the partial
    // iteration's best is at least as well founded.
    chosen = rootMoves[rootBestIndex];
    score = rootBest;
  } else {
    chosen = rootMoves[0];
  }
  searchInfo.depth = static_cast<uint8_t>(completedDepth);
  searchInfo.score = score;
  searchInfo.nodes = nodes;
  searchInfo.elapsedMs = clock != nullptr ? clock() - startMs : 0;
  state = State::Done;
  sp = 0;
  hasReturn = false;
}

void Engine::push(const int depth, const int16_t alpha, const int16_t beta, const bool afterNull) {
  Frame& child = frames[sp + 1];
  child.depth = static_cast<int8_t>(depth < -100 ? -100 : depth);
  child.alpha = alpha;
  child.beta = beta;
  child.pv = beta - alpha > 1;
  child.afterNull = afterNull;
  child.phase = PHASE_ENTER;
  const uint16_t begin = sp == 0 ? 0 : frames[sp].moveEnd;
  child.moveBegin = child.moveEnd = child.moveNext = begin;
  ++sp;
}

void Engine::leave(const int16_t score) {
  --sp;
  hasReturn = true;
  returnValue = score;
}

bool Engine::isRepetition(const int ply) const {
  const int idx = pathBase + ply;
  const int oldest = idx - frames[ply].pos.halfmove;
  for (int i = idx - 2; i >= 0 && i >= oldest; i -= 2) {
    if (path[i] == path[idx]) return true;
  }
  return false;
}

void Engine::enter(Frame& f) {
  const int ply = sp;
  ++nodes;
  path[pathBase + ply] = f.pos.key;
  if (ply >= MAX_PLY - 1) {
    leave(clampScore(evaluate(f.pos)));
    return;
  }
  if (f.pos.halfmove >= 100 || isRepetition(ply)) {
    leave(0);
    return;
  }
  f.inCheck = chess::inCheck(f.pos);
  if (f.inCheck && f.depth > 0) ++f.depth;
  f.quiesce = f.depth <= 0;
  f.alphaOrig = f.alpha;
  f.best = -SCORE_INFINITY;
  f.legal = 0;
  f.bestMove = Move{};
  f.ttMove = Move{};

  if (const TtEntry* e = probeTt(f.pos.key)) {
    f.ttMove = decodeMove(e->move);
    if (!f.pv && e->depth >= f.depth) {
      const int16_t s = fromTt(e->score, ply);
      if (e->bound == BOUND_EXACT || (e->bound == BOUND_LOWER && s >= f.beta) ||
          (e->bound == BOUND_UPPER && s <= f.alpha)) {
        leave(s);
        return;
      }
    }
  }

  if (f.quiesce) {
    if (!f.inCheck) {
      const int16_t stand = clampScore(evaluate(f.pos));
      if (stand >= f.beta) {
        leave(stand);
        return;
      }
      f.best = f.staticEval = stand;
      if (stand > f.alpha) f.alpha = stand;
    }
    generate(f);
    return;
  }

  f.staticEval = f.inCheck ? -SCORE_INFINITY : clampScore(evaluate(f.pos));
  if (!f.pv && !f.inCheck && f.beta < MATE_BOUND && f.beta > -MATE_BOUND) {
    // Reverse futility: far above beta near the leaves, a quiet move will not change that.
    if (f.depth <= 3 && f.staticEval - 120 * f.depth >= f.beta) {
      leave(f.staticEval);
      return;
    }
    if (f.depth >= 3 && !f.afterNull && f.staticEval >= f.beta && hasNonPawnMaterial(f.pos, f.pos.side)) {
      makeNullMove(f.pos, frames[ply + 1].pos);
      f.child = CHILD_NULL;
      f.phase = PHASE_GENERATE;
      push(f.depth - 1 - (f.depth >= 7 ? 3 : 2), static_cast<int16_t>(-f.beta), static_cast<int16_t>(-f.beta + 1),
           true);
      return;
    }
  }
  generate(f);
}

void Engine::generate(Frame& f) {
  if (f.moveBegin + MAX_MOVES > ARENA) {
    leave(f.quiesce && !f.inCheck ? f.best : clampScore(evaluate(f.pos)));
    return;
  }
  const int n = generatePseudo(f.pos, arena + f.moveBegin, f.quiesce && !f.inCheck);
  f.moveEnd = static_cast<uint16_t>(f.moveBegin + n);
  f.moveNext = f.moveBegin;
  const int ply = sp;
  for (int i = f.moveBegin; i < f.moveEnd; ++i) arenaScores[i] = scoreMove(f, arena[i], ply);
  f.phase = PHASE_MOVES;
}

int16_t Engine::scoreMove(const Frame& f, const Move& m, const int ply) const {
  if (!f.ttMove.isNull() && m.sameAs(f.ttMove)) return ORDER_TT;
  if (m.isCapture() || m.promo == QUEEN) {
    const int victim = (m.flags & MOVE_EN_PASSANT) ? PAWN : typeOf(f.pos.at(m.to));
    const int attacker = typeOf(f.pos.at(m.from));
    return static_cast<int16_t>(ORDER_CAPTURE + victim * 16 - attacker + (m.promo == QUEEN ? 2000 : 0));
  }
  if (m.isPromotion()) return static_cast<int16_t>(-1000 + m.promo);
  if (killers[ply][0].sameAs(m)) return ORDER_KILLER;
  if (killers[ply][1].sameAs(m)) return ORDER_KILLER - 1;
  return history[f.pos.at(m.from)][m.to];
}

void Engine::nextMove(Frame& f) {
  const int ply = sp;
  while (f.moveNext < f.moveEnd) {
    int pick = f.moveNext;
    for (int i = f.moveNext + 1; i < f.moveEnd; ++i) {
      if (arenaScores[i] > arenaScores[pick]) pick = i;
    }
    const Move m = arena[pick];
    arena[pick] = arena[f.moveNext];
    arenaScores[pick] = arenaScores[f.moveNext];
    arena[f.moveNext] = m;
    ++f.moveNext;

    if (f.quiesce && !f.inCheck && !m.isPromotion()) {
      // Delta pruning: even winning this piece outright cannot reach alpha. The
      // skipped move's optimistic value still bounds the fail-soft result.
      const int victim = (m.flags & MOVE_EN_PASSANT) ? PAWN : typeOf(f.pos.at(m.to));
      const int optimistic = f.staticEval + MG_VALUE[victim] + 200;
      if (optimistic <= f.alpha) {
        if (optimistic > f.best) f.best = static_cast<int16_t>(optimistic);
        continue;
      }
    }

    Frame& child = frames[ply + 1];
    if (!makeMove(f.pos, m, child.pos)) continue;
    ++f.legal;
    f.current = m;

    if (f.quiesce) {
      f.child = CHILD_FULL;
      push(f.depth - 1, static_cast<int16_t>(-f.beta), static_cast<int16_t>(-f.alpha), false);
      return;
    }

    const bool quiet = !m.isCapture() && !m.isPromotion();
    const bool givesCheck = chess::inCheck(child.pos);
    // Futility: a quiet move at the frontier will not lift a hopeless eval.
    const int optimistic = f.staticEval + 150;
    if (f.depth == 1 && !f.pv && !f.inCheck && quiet && !givesCheck && f.legal > 1 && optimistic <= f.alpha) {
      if (optimistic > f.best) f.best = static_cast<int16_t>(optimistic);
      continue;
    }
    if (f.legal == 1) {
      f.child = CHILD_FULL;
      push(f.depth - 1, static_cast<int16_t>(-f.beta), static_cast<int16_t>(-f.alpha), false);
      return;
    }
    int reduction = 0;
    if (f.depth >= 3 && f.legal > 3 && quiet && !f.inCheck && !givesCheck && !killers[ply][0].sameAs(m) &&
        !killers[ply][1].sameAs(m)) {
      reduction = f.legal > 8 && f.depth >= 5 ? 2 : 1;
    }
    f.child = reduction > 0 ? CHILD_REDUCED : CHILD_ZERO;
    push(f.depth - 1 - reduction, static_cast<int16_t>(-f.alpha - 1), static_cast<int16_t>(-f.alpha), false);
    return;
  }

  if (f.legal == 0) {
    if (f.inCheck) {
      leave(static_cast<int16_t>(-SCORE_MATE + ply));
    } else {
      leave(f.quiesce ? f.best : 0);
    }
    return;
  }
  if (!f.quiesce) {
    storeTt(f.pos.key, f.depth, f.best, f.best > f.alphaOrig ? BOUND_EXACT : BOUND_UPPER, f.bestMove, ply);
  }
  leave(f.best);
}

void Engine::childReturned(Frame& f, const int16_t value) {
  const int16_t score = static_cast<int16_t>(-value);
  const int ply = sp;
  switch (f.child) {
    case CHILD_NULL:
      if (score >= f.beta) {
        leave(score >= MATE_BOUND ? f.beta : score);
      } else {
        generate(f);
      }
      return;
    case CHILD_REDUCED:
      if (score > f.alpha) {
        f.child = CHILD_ZERO;
        push(f.depth - 1, static_cast<int16_t>(-f.alpha - 1), static_cast<int16_t>(-f.alpha), false);
        return;
      }
      break;
    case CHILD_ZERO:
      if (score > f.alpha && score < f.beta) {
        f.child = CHILD_FULL;
        push(f.depth - 1, static_cast<int16_t>(-f.beta), static_cast<int16_t>(-f.alpha), false);
        return;
      }
      break;
    default:
      break;
  }

  if (score <= f.best) return;
  f.best = score;
  if (score <= f.alpha) return;
  f.alpha = score;
  f.bestMove = f.current;
  if (score < f.beta) return;

  if (!f.quiesce) {
    const Move& m = f.current;
    if (!m.isCapture() && !m.isPromotion()) {
      if (!killers[ply][0].sameAs(m)) {
        killers[ply][1] = killers[ply][0];
        killers[ply][0] = m;
      }
      int16_t& h = history[f.pos.at(m.from)][m.to];
      h = static_cast<int16_t>(h + f.depth * f.depth);
      if (h > HISTORY_MAX) {
        for (auto& row : history) {
          for (auto& v : row) v = static_cast<int16_t>(v / 2);
        }
      }
    }
    storeTt(f.pos.key, f.depth, score, BOUND_LOWER, m, ply);
  }
  leave(score);
}

const Engine::TtEntry* Engine::probeTt(const uint64_t key) const {
  const TtEntry& e = tt[key & (TT_SIZE - 1)];
  return e.bound != BOUND_NONE && e.lock == static_cast<uint32_t>(key >> 32) ? &e : nullptr;
}

void Engine::storeTt(const uint64_t key, const int depth, const int16_t score, const uint8_t bound, const Move& move,
                     const int ply) {
  TtEntry& e = tt[key & (TT_SIZE - 1)];
  const uint32_t lock = static_cast<uint32_t>(key >> 32);
  // Depth-preferred within one search; anything from an earlier search may go.
  if (e.bound != BOUND_NONE && e.generation == generation && depth < e.depth && bound != BOUND_EXACT) return;
  if (!move.isNull() || e.lock != lock) e.move = encodeMove(move);
  e.lock = lock;
  e.score = toTt(score, ply);
  e.depth = static_cast<int8_t>(depth);
  e.bound = bound;
  e.generation = generation;
}

}  // namespace chess
