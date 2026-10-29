#pragma once

// A reversi (Othello) session: the surface a reversi screen drives.
//   newGame(mode, seed)   black moves first; against the computer the player is black
//   play(square)          the move of the side to move; a side without a move passes by itself
//   undo()                takes back the last move, or the player's move and the answers; not once over
//   at(), toMove(), moves(), status(), lastMove(), passed(), score()  for drawing
//   serialize() / deserialize()
// The computer's move comes from Engine, run in slices by the screen. Squares
// run 0..63 row by row from the top left. The score of each mode carries over
// from game to game. No heap.

#include <cstddef>
#include <cstdint>

#include "GameCommon.h"

namespace reversi {

constexpr int SIZE = 8;
constexpr int CELLS = 64;
constexpr int MAX_MOVES = 60;
constexpr size_t SAVE_MAX = 320;

enum class Disc : uint8_t { None, Black, White };
enum class Mode : uint8_t { Easy, Medium, Hard, TwoPlayers };
constexpr int MODE_COUNT = 4;
enum class Status : uint8_t { Playing, BlackWon, WhiteWon, Draw };

// Against the computer: won = the player's wins. Two players: won = black's.
struct Score {
  uint16_t won = 0;
  uint16_t lost = 0;
  uint16_t draws = 0;
};

uint64_t legalMoves(uint64_t me, uint64_t opp);
// The discs `me` turns by playing `square`; 0 when the move is illegal.
uint64_t flips(uint64_t me, uint64_t opp, int square);
int popcount(uint64_t bits);

struct Limits {
  uint8_t depth;
  uint8_t exactEmpties;  // solve to the end from this many empty squares
  bool mobility;
  uint8_t randomPercent;  // chance of a random legal move instead
  uint32_t maxNodes;
  uint32_t moveTimeMs;
};

const Limits& limitsFor(Mode mode);

// Iterative-deepening alpha-beta on an explicit stack, so the screen can run
// it in short slices via step().
class Engine {
 public:
  void start(uint64_t me, uint64_t opp, const Limits& limits, uint32_t seed, uint32_t nowMs);
  // Searches until `sliceMs` passes or the move is ready. True when ready.
  bool step(games::MillisFn now, uint32_t sliceMs);
  void cancel() { state = State::Idle; }

  bool thinking() const { return state == State::Searching; }
  bool ready() const { return state == State::Done; }
  int move() const { return chosen; }
  int depth() const { return completedDepth; }
  uint32_t nodes() const { return nodeCount; }

 private:
  enum class State : uint8_t { Idle, Searching, Done };
  static constexpr int MAX_PLY = 72;

  struct Frame {
    uint64_t me;
    uint64_t opp;
    uint64_t todo;
    int32_t alpha;
    int32_t beta;
    int32_t best;
    int8_t depth;
    uint8_t next;  // index into the move order (root: into rootMoves)
    bool pass;
  };

  void beginIteration();
  // Pushes a node; false when it resolves at once, with its value in `result`.
  bool enter(uint64_t me, uint64_t opp, int depth, int32_t alpha, int32_t beta);
  bool descend(Frame& f);
  void finishIteration();
  int32_t evaluate(uint64_t me, uint64_t opp) const;

  Frame stack[MAX_PLY];
  int top = -1;
  int32_t result = 0;
  uint8_t rootMoves[32];
  int rootCount = 0;
  int rootBest = -1;
  int iterationDepth = 0;
  int completedDepth = 0;
  int chosen = -1;
  uint64_t rootMe = 0;
  uint64_t rootOpp = 0;
  Limits limits{};
  uint32_t nodeCount = 0;
  uint32_t startMs = 0;
  State state = State::Idle;
};

class Game {
 public:
  void newGame(Mode mode, uint32_t seed);

  Mode mode() const { return gameMode; }
  bool vsComputer() const { return gameMode != Mode::TwoPlayers; }
  Disc at(int square) const;
  Disc toMove() const { return side; }
  // The legal moves of the side to move.
  uint64_t moves() const;
  int count(Disc disc) const;
  Status status() const { return state; }
  bool isOver() const { return state != Status::Playing; }
  bool inProgress() const { return moveCount > 0 && !isOver(); }
  bool computerToMove() const { return vsComputer() && !isOver() && side == Disc::White; }
  int movesMade() const { return moveCount; }
  // The last move and who made it; -1 before the first move.
  int lastMove() const { return moveCount > 0 ? history[moveCount - 1] : -1; }
  Disc lastMover() const { return lastBy; }
  // The side that passed and has not moved since, or None.
  Disc passed() const { return passedSide; }
  const Score& score() const { return scores[static_cast<int>(gameMode)]; }
  uint32_t seed() const { return seedValue; }
  uint64_t blackDiscs() const { return black; }
  uint64_t whiteDiscs() const { return white; }

  // False for an illegal move or a finished game.
  bool play(int square);
  bool canUndo() const;
  bool undo();

  size_t serialize(char* out, size_t cap) const;
  // False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  void place(int square);
  void rebuild(int count);
  void tally();

  Mode gameMode = Mode::Easy;
  Status state = Status::Playing;
  Disc side = Disc::Black;
  Disc lastBy = Disc::None;
  Disc passedSide = Disc::None;
  uint32_t seedValue = 0;
  uint64_t black = 0;
  uint64_t white = 0;
  uint64_t whiteMoves = 0;  // bit i: move i was white's
  uint8_t history[MAX_MOVES] = {};
  uint8_t moveCount = 0;
  Score scores[MODE_COUNT];
};

}  // namespace reversi
