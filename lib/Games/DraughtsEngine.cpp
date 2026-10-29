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

void Engine::start(const Position& rootPosition, const Mode mode, const uint32_t seed, const games::MillisFn now,
                   const bool timeLimits) {
  rootPos = rootPosition;
  level = levelOf(mode);
  rng = games::Rng(seed);
  clock = now;
  timed = timeLimits;
  startMs = clock();
  nodeCount = 0;
  checked = 0;
  doneDepth = 0;
  aborted = false;
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
  sp = -1;
  state = State::Searching;
}

bool Engine::step(const uint32_t sliceMs) {
  if (state != State::Searching) return state == State::Done;
  sliceStart = clock();
  this->sliceMs = sliceMs;
  while (state == State::Searching) {
    if (sliceUsed()) return false;
    if (aborted) {
      finish();
    } else if (sp < 0) {
      openRootMove();
    } else if (Frame& f = stack[sp]; f.next < f.count && f.alpha < f.beta) {
      Position child = f.pos;
      apply(child, arena[f.first + f.next].step());
      ++f.next;
      if (!enter(child, f.depth - 1, -f.beta, -f.alpha) && !aborted) childReturned(-result);
    } else {
      result = f.best;
      top = f.first;
      --sp;
      if (sp >= 0) {
        childReturned(-result);
      } else {
        rootMoveDone(-result);
      }
    }
  }
  return true;
}

// Every CHECK_NODES nodes: true once the slice is used up; past the hard cap the search stops.
bool Engine::sliceUsed() {
  if (nodeCount - checked < CHECK_NODES) return false;
  checked = nodeCount;
  const uint32_t now = clock();
  if (timed && now - startMs >= HARD_MS) {
    aborted = true;
    return false;
  }
  return now - sliceStart >= sliceMs;
}

// Moves after the first are searched against the best so far less the margin:
// a score above that floor is exact, one below is only a bound.
void Engine::openRootMove() {
  floor = rootIndex == 0 ? -INF : iterBest - level.margin - 1;
  Position child = rootPos;
  apply(child, root[rootIndex].step());
  top = 0;
  if (!enter(child, iterDepth - 1, -INF, -floor) && !aborted) rootMoveDone(-result);
}

void Engine::rootMoveDone(const int score) {
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
      (timed && clock() - startMs >= SOFT_MS)) {
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
  sp = -1;
  state = State::Done;
}

bool Engine::enter(const Position& p, const int depth, const int alpha, const int beta) {
  if (++nodeCount >= level.maxNodes) {
    aborted = true;
    return false;
  }
  const int ply = sp + 2;
  if (p.quiet >= KING_DRAW_PLIES) {
    result = 0;
    return false;
  }
  if (ply >= MAX_PLY || ARENA - top < MAX_MOVES / 2) {
    result = evaluate(p);
    return false;
  }
  Move* list = arena + top;
  const int n = legalMoves(p, list, std::min(ARENA - top, MAX_MOVES));
  if (n == 0) {
    result = ply - WIN;
    return false;
  }
  const bool capture = list[0].captured != 0;
  if (depth <= 0 && !(capture && level.extend)) {
    result = evaluate(p);
    return false;
  }
  if (!capture) {
    for (int i = 1; i < n; ++i) {
      if (keyOf(list[i]) == killers[ply]) {
        std::swap(list[0], list[i]);
        break;
      }
    }
  }
  stack[++sp] = {p,
                 static_cast<int16_t>(alpha),
                 static_cast<int16_t>(beta),
                 static_cast<int16_t>(-INF),
                 static_cast<int8_t>(depth),
                 capture,
                 static_cast<uint16_t>(top),
                 static_cast<uint8_t>(n),
                 0};
  top += n;
  return true;
}

void Engine::childReturned(const int score) {
  Frame& f = stack[sp];
  f.best = static_cast<int16_t>(std::max<int>(f.best, score));
  f.alpha = static_cast<int16_t>(std::max<int>(f.alpha, score));
  if (f.alpha >= f.beta && !f.capture) killers[sp + 1] = keyOf(arena[f.first + f.next - 1]);
}

}  // namespace draughts
