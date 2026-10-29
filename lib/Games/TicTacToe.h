#pragma once

// A tic-tac-toe session: the surface a tic-tac-toe screen drives.
//   newGame(mode, seed)   X moves first; against the computer the player is X
//   play(cell)            the move of the side to move; the computer answers at once
//   undo()                takes back the last move, or the player's move and the answer
//   at(cell), toMove(), status(), winLine(), score()  for drawing
//   serialize() / deserialize()
// Cells run 0..8 row by row. The score of each opponent carries over from game
// to game. No heap.

#include <cstddef>
#include <cstdint>

namespace tictactoe {

constexpr int CELLS = 9;
constexpr int LINE_COUNT = 8;
constexpr size_t SAVE_MAX = 160;

extern const uint8_t LINES[LINE_COUNT][3];

enum class Mark : uint8_t { None, X, O };
enum class Mode : uint8_t { Easy, Hard, TwoPlayers };
constexpr int MODE_COUNT = 3;
enum class Status : uint8_t { Playing, XWon, OWon, Draw };

// Against the computer: x = the player, o = the computer.
struct Score {
  uint16_t x = 0;
  uint16_t o = 0;
  uint16_t draws = 0;
};

// The perfect move for `side`: the quickest win, else a draw, else the slowest
// loss; ties broken by `salt`. -1 on a full board.
int bestMove(const Mark (&board)[CELLS], Mark side, uint32_t salt);

class Game {
 public:
  void newGame(Mode mode, uint32_t seed);

  Mode mode() const { return gameMode; }
  bool vsComputer() const { return gameMode != Mode::TwoPlayers; }
  Mark at(const int cell) const { return board[cell]; }
  Mark toMove() const { return moveCount % 2 == 0 ? Mark::X : Mark::O; }
  Status status() const { return state; }
  bool isOver() const { return state != Status::Playing; }
  bool inProgress() const { return moveCount > 0 && !isOver(); }
  int moves() const { return moveCount; }
  // Index into LINES of the completed line, or -1.
  int winLine() const;
  const Score& score() const { return scores[vsComputer() ? 0 : 1]; }

  // False for a taken cell or a finished game.
  bool play(int cell);
  bool canUndo() const { return !isOver() && moveCount >= (vsComputer() ? 2 : 1); }
  bool undo();

  size_t serialize(char* out, size_t cap) const;
  // False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  void place(int cell);
  int computerMove() const;
  void rebuild(int count);

  Mode gameMode = Mode::Easy;
  Status state = Status::Playing;
  uint32_t seedValue = 0;
  Mark board[CELLS] = {};
  uint8_t history[CELLS] = {};
  uint8_t moveCount = 0;
  Score scores[2];
};

}  // namespace tictactoe
