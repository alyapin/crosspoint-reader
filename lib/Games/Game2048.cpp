#include "Game2048.h"

#include <cstring>

#include "GameText.h"

namespace g2048 {
namespace {

constexpr uint32_t SAVE_VERSION = 1;
constexpr uint32_t MAX_DRAWS = 1u << 22;  // bounds the replay of a damaged save

// Cell `pos` of line `line`, counted from the wall the tiles slide to.
int cellOf(const Dir dir, const int line, const int pos) {
  switch (dir) {
    case Dir::Left:
      return line * SIZE + pos;
    case Dir::Right:
      return line * SIZE + SIZE - 1 - pos;
    case Dir::Up:
      return pos * SIZE + line;
    case Dir::Down:
      break;
  }
  return (SIZE - 1 - pos) * SIZE + line;
}

void writeCells(games::TextWriter& w, const char* key, const uint8_t (&cells)[CELLS]) {
  w.beginString(key);
  for (const uint8_t e : cells) w.put(static_cast<char>('a' + e));
  w.endString();
}

bool readCells(const games::TextReader& r, const char* key, uint8_t (&cells)[CELLS]) {
  const char* text = nullptr;
  size_t n = 0;
  if (!r.string(key, text, n) || n != CELLS) return false;
  for (int i = 0; i < CELLS; ++i) {
    if (text[i] < 'a' || text[i] > 'a' + MAX_EXPONENT) return false;
    cells[i] = static_cast<uint8_t>(text[i] - 'a');
  }
  return true;
}

games::Rng replay(const uint32_t seed, const uint32_t draws) {
  games::Rng rng(seed);
  for (uint32_t i = 0; i < draws; ++i) rng.next();
  return rng;
}

}  // namespace

void Game::newGame(const uint32_t seed) {
  memset(board, 0, sizeof(board));
  points = 0;
  seedValue = seed;
  draws = 0;
  rng = games::Rng(seed);
  hasUndo = false;
  winMove = false;
  spawn();
  spawn();
}

void Game::spawn() {
  int empty = 0;
  for (const uint8_t e : board) empty += e == 0;
  if (empty == 0) return;
  int pick = static_cast<int>(rng.below(static_cast<uint32_t>(empty)));
  const uint8_t value = rng.below(10) == 0 ? 2 : 1;
  draws += 2;
  for (uint8_t& e : board) {
    if (e == 0 && pick-- == 0) {
      e = value;
      return;
    }
  }
}

bool Game::move(const Dir dir) {
  uint8_t next[CELLS] = {};
  uint32_t gained = 0;
  for (int line = 0; line < SIZE; ++line) {
    int out = 0;
    bool mergeable = false;
    for (int pos = 0; pos < SIZE; ++pos) {
      const uint8_t e = board[cellOf(dir, line, pos)];
      if (e == 0) continue;
      if (mergeable && next[cellOf(dir, line, out - 1)] == e && e < MAX_EXPONENT) {
        uint8_t& merged = next[cellOf(dir, line, out - 1)];
        ++merged;
        gained += 1u << merged;
        mergeable = false;
      } else {
        next[cellOf(dir, line, out++)] = e;
        mergeable = true;
      }
    }
  }
  if (memcmp(next, board, sizeof(board)) == 0) return false;

  const bool wonBefore = hasWinTile();
  memcpy(undoBoard, board, sizeof(board));
  undoPoints = points;
  undoRng = rng;
  undoDraws = draws;
  hasUndo = true;

  memcpy(board, next, sizeof(board));
  points += gained;
  spawn();
  winMove = !wonBefore && hasWinTile();
  return true;
}

bool Game::undo() {
  if (!hasUndo) return false;
  memcpy(board, undoBoard, sizeof(board));
  points = undoPoints;
  rng = undoRng;
  draws = undoDraws;
  hasUndo = false;
  winMove = false;
  return true;
}

bool Game::canMove() const {
  for (int cell = 0; cell < CELLS; ++cell) {
    const uint8_t e = board[cell];
    if (e == 0) return true;
    if (cell % SIZE < SIZE - 1 && board[cell + 1] == e) return true;
    if (cell + SIZE < CELLS && board[cell + SIZE] == e) return true;
  }
  return false;
}

bool Game::inProgress() const {
  for (const uint8_t e : board) {
    if (e != 0) return canMove();
  }
  return false;
}

bool Game::hasWinTile() const {
  for (const uint8_t e : board) {
    if (e >= WIN_EXPONENT) return true;
  }
  return false;
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("seed", seedValue);
  w.number("draws", draws);
  w.number("score", points);
  writeCells(w, "cells", board);
  w.number("undo", hasUndo ? 1 : 0);
  if (hasUndo) {
    w.number("uscore", undoPoints);
    w.number("udraws", undoDraws);
    writeCells(w, "ucells", undoBoard);
  }
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t seed = 0;
  uint32_t drawCount = 0;
  uint32_t score = 0;
  uint32_t undoFlag = 0;
  uint8_t cells[CELLS];
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("seed", seed) || !r.number("draws", drawCount) ||
      drawCount > MAX_DRAWS || !r.number("score", score) || !readCells(r, "cells", cells) ||
      !r.number("undo", undoFlag) || undoFlag > 1) {
    return false;
  }
  uint32_t uscore = 0;
  uint32_t udraws = 0;
  uint8_t ucells[CELLS] = {};
  if (undoFlag == 1 && (!r.number("uscore", uscore) || !r.number("udraws", udraws) || udraws > drawCount ||
                        !readCells(r, "ucells", ucells))) {
    return false;
  }

  memcpy(board, cells, sizeof(board));
  points = score;
  seedValue = seed;
  draws = drawCount;
  rng = replay(seed, drawCount);
  hasUndo = undoFlag == 1;
  memcpy(undoBoard, ucells, sizeof(undoBoard));
  undoPoints = uscore;
  undoDraws = udraws;
  undoRng = replay(seed, udraws);
  winMove = false;
  return true;
}

}  // namespace g2048
