#include "TicTacToe.h"

#include <algorithm>
#include <iterator>

#include "GameCommon.h"
#include "GameText.h"

namespace tictactoe {

const uint8_t LINES[LINE_COUNT][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
                                      {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

namespace {

constexpr uint32_t SAVE_VERSION = 1;
// On Easy one computer move in EASY_SLIP goes to a random cell.
constexpr uint32_t EASY_SLIP = 3;
constexpr const char* SCORE_KEYS[2][3] = {{"cx", "co", "cd"}, {"px", "po", "pd"}};

Mark other(const Mark side) { return side == Mark::X ? Mark::O : Mark::X; }

int lineOf(const Mark (&board)[CELLS]) {
  for (int i = 0; i < LINE_COUNT; ++i) {
    const Mark m = board[LINES[i][0]];
    if (m != Mark::None && board[LINES[i][1]] == m && board[LINES[i][2]] == m) return i;
  }
  return -1;
}

int search(Mark (&board)[CELLS], Mark side, int empty);

// Negamax value of `side` taking `cell`: a win scores the empty cells left, so sooner is better.
int valueOf(Mark (&board)[CELLS], const int cell, const Mark side, const int empty) {
  board[cell] = side;
  const int value = lineOf(board) >= 0 ? empty : empty == 1 ? 0 : -search(board, other(side), empty - 1);
  board[cell] = Mark::None;
  return value;
}

int search(Mark (&board)[CELLS], const Mark side, const int empty) {
  int best = -CELLS - 1;
  for (int c = 0; c < CELLS && best < empty; ++c) {
    if (board[c] == Mark::None) best = std::max(best, valueOf(board, c, side, empty));
  }
  return best;
}

}  // namespace

int bestMove(const Mark (&board)[CELLS], const Mark side, const uint32_t salt) {
  Mark work[CELLS];
  int empty = 0;
  for (int c = 0; c < CELLS; ++c) {
    work[c] = board[c];
    empty += board[c] == Mark::None;
  }
  games::Rng rng(salt);
  int best = -CELLS - 1;
  int chosen = -1;
  uint32_t ties = 0;
  for (int c = 0; c < CELLS; ++c) {
    if (work[c] != Mark::None) continue;
    const int value = valueOf(work, c, side, empty);
    if (value > best) {
      best = value;
      chosen = c;
      ties = 1;
    } else if (value == best && rng.below(++ties) == 0) {
      chosen = c;
    }
  }
  return chosen;
}

void Game::newGame(const Mode mode, const uint32_t seed) {
  gameMode = mode;
  seedValue = seed;
  rebuild(0);
}

int Game::winLine() const { return lineOf(board); }

void Game::place(const int cell) {
  board[cell] = toMove();
  history[moveCount++] = static_cast<uint8_t>(cell);
  if (lineOf(board) >= 0) {
    state = board[cell] == Mark::X ? Status::XWon : Status::OWon;
  } else if (moveCount == CELLS) {
    state = Status::Draw;
  }
}

void Game::rebuild(const int count) {
  std::fill(std::begin(board), std::end(board), Mark::None);
  state = Status::Playing;
  moveCount = 0;
  for (int i = 0; i < count; ++i) place(history[i]);
}

int Game::computerMove() const {
  games::Rng rng(seedValue + moveCount * 0x9E3779B9u);
  if (gameMode == Mode::Easy && rng.below(EASY_SLIP) == 0) {
    int pick = static_cast<int>(rng.below(CELLS - moveCount));
    for (int c = 0; c < CELLS; ++c) {
      if (board[c] == Mark::None && pick-- == 0) return c;
    }
  }
  return bestMove(board, toMove(), rng.next());
}

bool Game::play(const int cell) {
  if (isOver() || cell < 0 || cell >= CELLS || board[cell] != Mark::None) return false;
  place(cell);
  if (vsComputer() && !isOver()) place(computerMove());
  if (isOver()) {
    Score& s = scores[vsComputer() ? 0 : 1];
    ++(state == Status::XWon ? s.x : state == Status::OWon ? s.o : s.draws);
  }
  return true;
}

bool Game::undo() {
  if (!canUndo()) return false;
  rebuild(moveCount - (vsComputer() ? 2 : 1));
  return true;
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("mode", static_cast<uint32_t>(gameMode));
  w.number("seed", seedValue);
  w.beginString("moves");
  for (int i = 0; i < moveCount; ++i) w.put(static_cast<char>('0' + history[i]));
  w.endString();
  for (int i = 0; i < 2; ++i) {
    w.number(SCORE_KEYS[i][0], scores[i].x);
    w.number(SCORE_KEYS[i][1], scores[i].o);
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
  size_t count = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("mode", mode) || mode >= MODE_COUNT ||
      !r.number("seed", seed) || !r.string("moves", moves, count) || count > CELLS) {
    return false;
  }
  Score loaded[2];
  for (int i = 0; i < 2; ++i) {
    uint32_t v[3];
    for (int k = 0; k < 3; ++k) {
      if (!r.number(SCORE_KEYS[i][k], v[k]) || v[k] > UINT16_MAX) return false;
    }
    loaded[i] = {static_cast<uint16_t>(v[0]), static_cast<uint16_t>(v[1]), static_cast<uint16_t>(v[2])};
  }

  Game g;
  g.newGame(static_cast<Mode>(mode), seed);
  for (size_t i = 0; i < count; ++i) {
    const int cell = moves[i] - '0';
    if (g.isOver() || cell < 0 || cell >= CELLS || g.board[cell] != Mark::None) return false;
    g.place(cell);
  }
  // Against the computer the player is always to move.
  if (g.vsComputer() && !g.isOver() && g.toMove() != Mark::X) return false;
  std::copy(std::begin(loaded), std::end(loaded), std::begin(g.scores));
  *this = g;
  return true;
}

}  // namespace tictactoe
