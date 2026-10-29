#pragma once

// The computer opponent: iterative-deepening alpha-beta with a capture
// extension. step() runs it in slices of whole root moves, so the UI loop
// keeps control; a node budget and a wall-clock cap bound every move. All
// storage is in the object (~16 KB), nothing on the heap.

#include "Draughts.h"
#include "GameCommon.h"

namespace draughts {

struct Level {
  uint8_t depth;
  int16_t margin;  // plays a random move scoring within this of the best
  bool extend;     // goes on past the depth while captures are forced
  uint32_t maxNodes;
};

// Easy, Medium, Hard; TwoPlayers maps to Hard.
const Level& levelOf(Mode mode);

class Engine {
 public:
  static constexpr uint32_t SOFT_MS = 700;   // no deeper iteration starts after this
  static constexpr uint32_t HARD_MS = 1500;  // the search stops here

  void start(const Position& rootPosition, Mode mode, uint32_t seed, games::MillisFn now);
  // Searches until `sliceMs` passes or the move is ready; true when ready.
  bool step(uint32_t sliceMs);
  void cancel() { state = State::Idle; }
  bool thinking() const { return state == State::Searching; }
  bool ready() const { return state == State::Done; }
  // Valid once ready(); from == to == 0 only when the root had no move.
  const Move& bestMove() const { return chosen; }
  int depth() const { return doneDepth; }
  uint32_t nodes() const { return nodeCount; }

 private:
  enum class State : uint8_t { Idle, Searching, Done };
  static constexpr int ARENA = 640;
  static constexpr int MAX_PLY = 40;

  void searchRootMove();
  void iterationDone();
  void finish();
  int search(const Position& p, int depth, int alpha, int beta, int ply);
  bool outOfTime();

  Move root[MAX_MOVES];
  int16_t scores[MAX_MOVES];
  bool exact[MAX_MOVES];
  int16_t doneScores[MAX_MOVES];
  bool doneExact[MAX_MOVES];
  Move arena[ARENA];
  uint16_t killers[MAX_PLY];
  Move chosen;
  Position rootPos;
  Level level{};
  games::Rng rng{1};
  games::MillisFn clock = nullptr;
  uint32_t startMs = 0;
  uint32_t nodeCount = 0;
  int rootCount = 0;
  int rootIndex = 0;
  int iterDepth = 0;
  int doneDepth = 0;
  int iterBest = 0;
  int top = 0;
  bool aborted = false;
  State state = State::Idle;
};

}  // namespace draughts
