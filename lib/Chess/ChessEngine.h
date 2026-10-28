#pragma once

// Computer opponent: iterative-deepening alpha-beta (PVS, quiescence, TT,
// null move, LMR, killers/history) driven as an explicit-stack state machine,
// so the UI loop can run it in short slices via step(). All storage is inside
// the object (~125 KB); no allocation during search.
//
// Piece-square tables follow Tomasz Michniewski's "Simplified Evaluation
// Function" in the rank-1-first layout used by ma-r-s/crossplay (MIT),
// src/apps_local/chess/ChessEngine.cpp.

#include <cstdint>

#include "ChessBoard.h"

namespace chess {

constexpr int MIN_LEVEL = 1;
constexpr int MAX_LEVEL = 5;

struct EngineLevel {
  uint8_t maxDepth;
  int16_t marginCp;        // plays a random move scoring within this of the best
  uint8_t blunderPercent;  // per-move chance to use blunderMarginCp instead
  int16_t blunderMarginCp;
  uint32_t maxNodes;    // 0 = no cap
  uint32_t moveTimeMs;  // wall-time cap per move
};

// Settings for level 1..5 (clamped).
const EngineLevel& engineLevel(int level);

struct SearchLimits {
  uint8_t maxDepth = 1;
  uint32_t maxNodes = 0;  // 0 = no cap
  uint32_t softMs = 0;    // no new iteration starts after this; 0 = no limit
  uint32_t hardMs = 0;    // search stops at this; 0 = no limit
  int16_t marginCp = 0;
  uint8_t blunderPercent = 0;
  int16_t blunderMarginCp = 0;
};

struct SearchInfo {
  uint8_t depth = 0;  // last completed iteration
  int16_t score = 0;  // centipawns for the side to move; |score| > MATE_BOUND is a mate
  uint32_t nodes = 0;
  uint32_t elapsedMs = 0;
};

constexpr int16_t SCORE_INFINITY = 32000;
constexpr int16_t SCORE_MATE = 31000;
constexpr int16_t MATE_BOUND = SCORE_MATE - 1000;

// Static evaluation in centipawns for the side to move.
int evaluate(const Position& pos);

class Engine {
 public:
  using MillisFn = uint32_t (*)();

  static constexpr int MAX_PLY = 64;
  static constexpr int MAX_DEPTH = 48;

  Engine();

  // Forgets hash and move-ordering knowledge from earlier games.
  void newGame();
  void seed(uint32_t value);

  // Starts thinking about `root`. `history` holds the game's position keys,
  // oldest first, ending with root's own key; it is copied.
  void start(const Position& root, const uint64_t* history, int historyCount, const SearchLimits& limits,
             uint32_t nowMs);
  // Searches until `sliceMs` passes or the move is ready. Returns true when ready.
  bool step(MillisFn now, uint32_t sliceMs);
  void cancel();

  bool thinking() const { return state == State::Searching; }
  bool ready() const { return state == State::Done; }
  // Valid once ready(); a null move only when the root had no legal move.
  Move bestMove() const { return chosen; }
  const SearchInfo& info() const { return searchInfo; }

 private:
  enum class State : uint8_t { Idle, Searching, Done };

  struct Frame {
    Position pos;
    int16_t alpha;
    int16_t beta;
    int16_t alphaOrig;
    int16_t best;
    int16_t staticEval;
    int8_t depth;
    uint8_t phase;
    uint8_t child;
    uint8_t legal;
    bool inCheck;
    bool pv;
    bool quiesce;
    bool afterNull;
    uint16_t moveBegin;
    uint16_t moveEnd;
    uint16_t moveNext;
    Move ttMove;
    Move bestMove;
    Move current;
  };

  struct TtEntry {
    uint32_t lock;
    uint16_t move;
    int16_t score;
    int8_t depth;
    uint8_t bound;
    uint8_t generation;
  };

  // Boards without PSRAM (ESP32-C3) get an 8x smaller table: ~65 KB per Match instead of ~150 KB.
#if defined(ARDUINO_ARCH_ESP32) && !defined(BOARD_HAS_PSRAM)
  static constexpr int TT_BITS = 10;
#else
  static constexpr int TT_BITS = 13;
#endif
  static constexpr int TT_SIZE = 1 << TT_BITS;
  static constexpr int ARENA = 2048;
  static constexpr int HISTORY_KEYS = 128;

  void advance();
  void advanceRoot();
  void startIteration();
  void rootChildReturned(int16_t score);
  void iterationDone();
  void finish(bool aborted);
  Move pickMove(const int16_t* scores, const uint8_t* exact, int count);

  void enter(Frame& f);
  void generate(Frame& f);
  void nextMove(Frame& f);
  void childReturned(Frame& f, int16_t score);
  void push(int depth, int16_t alpha, int16_t beta, bool afterNull);
  void leave(int16_t score);

  bool isRepetition(int ply) const;
  int16_t scoreMove(const Frame& f, const Move& m, int ply) const;
  void storeTt(uint64_t key, int depth, int16_t score, uint8_t bound, const Move& move, int ply);
  const TtEntry* probeTt(uint64_t key) const;
  uint32_t nextRandom();

  Frame frames[MAX_PLY];
  Move arena[ARENA];
  int16_t arenaScores[ARENA];
  TtEntry tt[TT_SIZE];
  int16_t history[16][64];
  Move killers[MAX_PLY][2];
  uint64_t path[HISTORY_KEYS + MAX_PLY];
  int pathBase = 0;

  Move rootMoves[MAX_MOVES];
  int16_t rootScores[MAX_MOVES];
  uint8_t rootExact[MAX_MOVES];
  int16_t completedScores[MAX_MOVES];
  uint8_t completedExact[MAX_MOVES];
  int rootCount = 0;
  int rootIndex = 0;
  int rootBestIndex = -1;
  int16_t rootBest = 0;
  int16_t rootWindow = 0;
  bool rootFullWindow = false;
  int iterationDepth = 0;
  int completedDepth = 0;
  int16_t searchMargin = 0;

  int sp = 0;
  bool hasReturn = false;
  int16_t returnValue = 0;

  SearchLimits limits;
  SearchInfo searchInfo;
  State state = State::Idle;
  Move chosen;
  uint32_t startMs = 0;
  uint32_t nodes = 0;
  uint32_t ticks = 0;
  MillisFn clock = nullptr;
  uint32_t rng = 0x9E3779B9u;
  uint8_t generation = 0;
};

}  // namespace chess
