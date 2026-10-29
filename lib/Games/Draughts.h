#pragma once

// Russian draughts: the rules, a game with undo and the draw rules, its save.
// The computer opponent lives in DraughtsEngine.h.
//   Squares 0..31 are the dark cells row by row from the top (Black's side);
//   White sits at the bottom, moves up and moves first.
//   Men move forward and capture both ways; kings fly. A capture is mandatory,
//   any one may be chosen, and it goes on while it can. Captured pieces stay
//   on the board until the move ends (Turkish strike). A man reaching the far
//   row mid-capture goes on as a king.
//   Draws: threefold repetition, KING_DRAW_PLIES plies of king moves without a
//   capture or a man move, or agreement.
// No heap.

#include <cstddef>
#include <cstdint>

namespace draughts {

constexpr int SQUARES = 32;
constexpr int MAX_JUMPS = 12;
constexpr int MAX_MOVES = 128;
constexpr int MAX_PLIES = 200;       // undo reaches back this far; older plies fold into the start position
constexpr int KING_DRAW_PLIES = 30;  // 15 moves each
constexpr size_t SAVE_MAX = 2560;

enum Side : uint8_t { White, Black };

int rowOf(int square);
int colOf(int square);
// The square of a dark cell, else -1.
int squareAt(int row, int col);

struct Position {
  uint32_t white = 0;
  uint32_t black = 0;
  uint32_t kings = 0;
  Side side = White;
  uint8_t quiet = 0;  // plies of king moves since the last capture or man move

  static Position initial();
  bool samePlacement(const Position& o) const {
    return white == o.white && black == o.black && kings == o.kings && side == o.side;
  }
};

// What a move changes; enough to replay it.
struct Step {
  uint32_t captured = 0;
  uint8_t from = 0;
  uint8_t to = 0;
  bool promotes = false;
};

struct Move {
  uint32_t captured = 0;
  uint8_t from = 0;
  uint8_t to = 0;
  bool promotes = false;
  uint8_t jumps = 0;  // landing squares in path; 0 for a plain move
  uint8_t path[MAX_JUMPS] = {};

  Step step() const { return {captured, from, to, promotes}; }
  // The squares the piece stops on: the landings of a capture, else just `to`.
  int stops() const { return jumps > 0 ? jumps : 1; }
  int stopAt(const int i) const { return jumps > 0 ? path[i] : to; }
};

// The legal moves of the side to move, at most `cap`.
int legalMoves(const Position& pos, Move* out, int cap);
void apply(Position& pos, const Step& step);
// Static score for the side to move; a man is 100.
int evaluate(const Position& pos);

enum class Mode : uint8_t { Easy, Medium, Hard, TwoPlayers };
constexpr int MODE_COUNT = 4;
enum class Status : uint8_t { Playing, WhiteWon, BlackWon, Draw };
enum class End : uint8_t { None, NoMoves, Resigned, Repetition, KingMoves, Agreed };

// Against the computer the player is White.
class Game {
 public:
  void newGame(Mode mode, uint32_t seed, const Position& start = Position::initial());

  Mode mode() const { return gameMode; }
  bool vsComputer() const { return gameMode != Mode::TwoPlayers; }
  const Position& position() const { return pos; }
  Status status() const { return state; }
  End end() const { return reason; }
  bool isOver() const { return state != Status::Playing; }
  bool inProgress() const { return plies > 0 && !isOver(); }
  bool computerToMove() const { return vsComputer() && !isOver() && pos.side == Black; }
  int plyCount() const { return plies; }
  bool lastMove(Step& out) const;

  // None once the game is over.
  int moves(Move* out, int cap) const { return isOver() ? 0 : legalMoves(pos, out, cap); }
  // False unless legal.
  bool play(const Move& move);
  // Against the computer: the player's last move and the answer to it.
  bool canUndo() const { return !isOver() && plies >= undoPlies(); }
  bool undo();
  // The side to move gives up; against the computer, the player.
  void resign();
  // True when accepted: always between two players; the computer takes it
  // unless it stands better.
  bool offerDraw();

  size_t serialize(char* out, size_t cap) const;
  // False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  int undoPlies() const { return vsComputer() && pos.side == White ? 2 : 1; }
  void replay(int count);
  void judge();

  Mode gameMode = Mode::Easy;
  Status state = Status::Playing;
  End reason = End::None;
  uint32_t seedValue = 0;
  Position base;
  Position pos;
  Step history[MAX_PLIES];
  int plies = 0;
};

}  // namespace draughts
