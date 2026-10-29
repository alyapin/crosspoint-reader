#include "DraughtsEngine.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <utility>

namespace draughts {

namespace {

constexpr int INF = 32000;
constexpr int WIN = 10000;
constexpr int DECIDED = WIN - 100;
constexpr uint16_t NO_KILLER = 0xFFFF;

constexpr Level LEVELS[3] = {
    {2, 60, false, 20000},
    {4, 8, true, 60000},
    {8, 0, true, 400000},
};

uint16_t keyOf(const Move& m) { return static_cast<uint16_t>(m.from | m.to << 8); }

}  // namespace

const Level& levelOf(const Mode mode) { return LEVELS[std::min(static_cast<int>(mode), 2)]; }

void Engine::start(const Position& rootPosition, const Mode mode, const uint32_t seed, const games::MillisFn now) {
  rootPos = rootPosition;
  level = levelOf(mode);
  rng = games::Rng(seed);
  clock = now;
  startMs = clock();
  nodeCount = 0;
  doneDepth = 0;
  chosen = Move{};
  std::fill(std::begin(killers), std::end(killers), NO_KILLER);
  rootCount = legalMoves(rootPos, root, MAX_MOVES);
  if (rootCount <= 1) {
    if (rootCount == 1) chosen = root[0];
    state = State::Done;
    return;
  }
  for (int i = rootCount - 1; i > 0; --i) std::swap(root[i], root[rng.below(static_cast<uint32_t>(i + 1))]);
  iterDepth = 1;
  rootIndex = 0;
  iterBest = -INF;
  state = State::Searching;
}

bool Engine::step(const uint32_t sliceMs) {
  if (state != State::Searching) return state == State::Done;
  const uint32_t sliceStart = clock();
  while (state == State::Searching) {
    searchRootMove();
    if (state == State::Searching && clock() - sliceStart >= sliceMs) return false;
  }
  return true;
}

bool Engine::outOfTime() { return clock() - startMs >= HARD_MS; }

// Moves after the first are searched against the best so far less the margin:
// a score above that floor is exact, one below is only a bound.
void Engine::searchRootMove() {
  const int floor = rootIndex == 0 ? -INF : iterBest - level.margin - 1;
  Position child = rootPos;
  apply(child, root[rootIndex].step());
  aborted = false;
  top = 0;
  const int score = -search(child, iterDepth - 1, -INF, -floor, 1);
  if (aborted) {
    finish();
    return;
  }
  scores[rootIndex] = static_cast<int16_t>(score);
  exact[rootIndex] = score > floor;
  iterBest = std::max(iterBest, score);
  if (++rootIndex == rootCount) iterationDone();
}

void Engine::iterationDone() {
  // Stable insertion sort, best first: the next iteration searches it first.
  for (int i = 1; i < rootCount; ++i) {
    for (int k = i; k > 0 && scores[k] > scores[k - 1]; --k) {
      std::swap(root[k], root[k - 1]);
      std::swap(scores[k], scores[k - 1]);
      std::swap(exact[k], exact[k - 1]);
    }
  }
  std::copy(scores, scores + rootCount, doneScores);
  std::copy(exact, exact + rootCount, doneExact);
  doneDepth = iterDepth;
  if (iterDepth >= level.depth || std::abs(iterBest) >= DECIDED || nodeCount >= level.maxNodes ||
      clock() - startMs >= SOFT_MS) {
    finish();
    return;
  }
  ++iterDepth;
  rootIndex = 0;
  iterBest = -INF;
}

void Engine::finish() {
  chosen = root[0];
  if (doneDepth > 0) {
    int candidates = 0;
    for (int i = 0; i < rootCount; ++i) {
      if (doneExact[i] && doneScores[i] >= doneScores[0] - level.margin) ++candidates;
    }
    int pick = static_cast<int>(rng.below(static_cast<uint32_t>(std::max(candidates, 1))));
    for (int i = 0; i < rootCount; ++i) {
      if (doneExact[i] && doneScores[i] >= doneScores[0] - level.margin && pick-- == 0) {
        chosen = root[i];
        break;
      }
    }
  }
  state = State::Done;
}

int Engine::search(const Position& p, const int depth, int alpha, const int beta, const int ply) {
  if (++nodeCount >= level.maxNodes || ((nodeCount & 255) == 0 && outOfTime())) aborted = true;
  if (aborted) return 0;
  if (p.quiet >= KING_DRAW_PLIES) return 0;
  if (ply >= MAX_PLY || ARENA - top < MAX_MOVES / 2) return evaluate(p);
  Move* list = arena + top;
  const int n = legalMoves(p, list, ARENA - top);
  if (n == 0) return ply - WIN;
  const bool capture = list[0].captured != 0;
  if (depth <= 0 && !(capture && level.extend)) return evaluate(p);
  if (!capture) {
    for (int i = 1; i < n; ++i) {
      if (keyOf(list[i]) == killers[ply]) {
        std::swap(list[0], list[i]);
        break;
      }
    }
  }
  top += n;
  int best = -INF;
  for (int i = 0; i < n; ++i) {
    Position child = p;
    apply(child, list[i].step());
    const int score = -search(child, depth - 1, -beta, -alpha, ply + 1);
    if (aborted) break;
    best = std::max(best, score);
    alpha = std::max(alpha, score);
    if (alpha >= beta) {
      if (!capture) killers[ply] = keyOf(list[i]);
      break;
    }
  }
  top -= n;
  return best;
}

}  // namespace draughts
