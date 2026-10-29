#include "Fifteen.h"

#include <cstring>

#include "GameText.h"

namespace fifteen {
namespace {

constexpr uint32_t SAVE_VERSION = 1;

int distance(const int a, const int b) {
  const int dx = a % SIDE - b % SIDE;
  const int dy = a / SIDE - b / SIDE;
  return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
}

bool isPermutation(const uint8_t tiles[CELLS]) {
  bool seen[CELLS] = {};
  for (int cell = 0; cell < CELLS; ++cell) {
    if (tiles[cell] >= CELLS || seen[tiles[cell]]) return false;
    seen[tiles[cell]] = true;
  }
  return true;
}

bool inOrder(const uint8_t tiles[CELLS]) {
  for (int cell = 0; cell < CELLS - 1; ++cell) {
    if (tiles[cell] != cell + 1) return false;
  }
  return true;
}

}  // namespace

// A slide swaps the gap with a neighbour: one transposition and one step of the
// gap, so the permutation parity always matches the gap's distance from home.
bool isSolvable(const uint8_t tiles[CELLS]) {
  int inversions = 0;
  int gapCell = 0;
  for (int i = 0; i < CELLS; ++i) {
    const int a = tiles[i] == 0 ? CELLS : tiles[i];
    if (tiles[i] == 0) gapCell = i;
    for (int j = i + 1; j < CELLS; ++j) inversions += a > (tiles[j] == 0 ? CELLS : tiles[j]);
  }
  return (inversions + distance(gapCell, CELLS - 1)) % 2 == 0;
}

Game::Game(const games::MillisFn millis) : now(millis) {}

void Game::start(const uint8_t tiles[CELLS]) {
  memcpy(board, tiles, CELLS);
  for (int cell = 0; cell < CELLS; ++cell) {
    if (board[cell] == 0) gapCell = static_cast<uint8_t>(cell);
  }
  moves = 0;
  solved = inOrder(board);
  clock.reset();
}

void Game::newGame(const uint32_t seed) {
  games::Rng rng(seed);
  uint8_t tiles[CELLS];
  do {
    for (int cell = 0; cell < CELLS; ++cell) tiles[cell] = static_cast<uint8_t>(cell);
    for (int i = CELLS - 1; i > 0; --i) {
      const int j = static_cast<int>(rng.below(static_cast<uint32_t>(i + 1)));
      const uint8_t t = tiles[i];
      tiles[i] = tiles[j];
      tiles[j] = t;
    }
    // Swapping two tiles flips the parity.
    if (!isSolvable(tiles)) {
      const int a = tiles[0] == 0 ? 1 : 0;
      const int b = tiles[a + 1] == 0 ? a + 2 : a + 1;
      const uint8_t t = tiles[a];
      tiles[a] = tiles[b];
      tiles[b] = t;
    }
  } while (inOrder(tiles));
  start(tiles);
}

bool Game::newGame(const uint8_t tiles[CELLS]) {
  if (!isPermutation(tiles) || !isSolvable(tiles)) return false;
  start(tiles);
  return true;
}

bool Game::canSlide(const int cell) const {
  if (solved || cell < 0 || cell >= CELLS || cell == gapCell) return false;
  return cell / SIDE == gapCell / SIDE || cell % SIDE == gapCell % SIDE;
}

bool Game::slide(const int cell) {
  if (!canSlide(cell)) return false;
  const int step = cell / SIDE == gapCell / SIDE ? (cell > gapCell ? 1 : -1) : (cell > gapCell ? SIDE : -SIDE);
  while (gapCell != cell) {
    const int next = gapCell + step;
    board[gapCell] = board[next];
    gapCell = static_cast<uint8_t>(next);
  }
  board[cell] = 0;
  if (moves < UINT16_MAX) ++moves;
  if (inOrder(board)) {
    solved = true;
    clock.stop(now());
  }
  return true;
}

void Game::pause() { clock.stop(now()); }

void Game::resume() {
  if (!solved) clock.start(now());
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("ms", elapsedMs());
  w.number("moves", moves);
  w.beginString("tiles");
  for (int cell = 0; cell < CELLS; ++cell) w.put(games::hexDigit(board[cell]));
  w.endString();
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t ms = 0;
  uint32_t moveCount = 0;
  const char* hex = nullptr;
  size_t hexLen = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("ms", ms) || !r.number("moves", moveCount) ||
      moveCount > UINT16_MAX || !r.string("tiles", hex, hexLen) || hexLen != CELLS) {
    return false;
  }
  uint8_t tiles[CELLS];
  for (int cell = 0; cell < CELLS; ++cell) {
    const int value = games::hexValue(hex[cell]);
    if (value < 0) return false;
    tiles[cell] = static_cast<uint8_t>(value);
  }
  if (!isPermutation(tiles) || !isSolvable(tiles)) return false;
  start(tiles);
  moves = static_cast<uint16_t>(moveCount);
  clock.reset(ms);
  return true;
}

bool Best::record(const int moveCount, const uint32_t elapsedMs) {
  if (moveCount <= 0 || moveCount > UINT16_MAX) return false;
  if (moves != 0 && (moveCount > moves || (moveCount == moves && elapsedMs >= ms))) return false;
  moves = static_cast<uint16_t>(moveCount);
  ms = elapsedMs;
  return true;
}

size_t Best::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("moves", moves);
  w.number("ms", ms);
  return w.finish();
}

bool Best::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t moveCount = 0;
  uint32_t elapsed = 0;
  if (!r.number("moves", moveCount) || moveCount > UINT16_MAX || !r.number("ms", elapsed)) return false;
  moves = static_cast<uint16_t>(moveCount);
  ms = elapsed;
  return true;
}

}  // namespace fifteen
