// Flood, chord and first-dig rules follow ma-r-s/crossplay
// src/apps_local/minesweeper/MinesweeperCore.h (MIT, (c) 2026 Mario Ruiz);
// board sizes, the no-guess solver and the save are new.

#include "Minesweeper.h"

#include <cstdlib>
#include <cstring>

#include "GameText.h"

namespace minesweeper {
namespace {

constexpr uint32_t SAVE_VERSION = 1;
constexpr uint8_t MINE = 1 << 0;
constexpr uint8_t OPEN = 1 << 1;
constexpr uint8_t FLAG = 1 << 2;
constexpr uint8_t STATE_BITS = MINE | OPEN | FLAG;

// Solver knowledge per cell.
constexpr uint8_t UNKNOWN = 0;
constexpr uint8_t SAFE = 1;
constexpr uint8_t KNOWN_MINE = 2;

int countOf(const uint8_t c) { return c >> 4; }

}  // namespace

bool isValid(const Config& c) {
  const int cellCount = c.columns * c.rows;
  return c.columns >= MIN_SIDE && c.columns <= MAX_COLUMNS && c.rows >= MIN_SIDE && c.rows <= MAX_ROWS &&
         c.mines >= 1 && c.mines <= cellCount - 9;
}

Game::Game(const games::MillisFn millis) : now(millis) { newGame(BEGINNER, 1); }

bool Game::newGame(const Config& config, const uint32_t seed) {
  if (!isValid(config)) return false;
  cfg = config;
  seedValue = seed;
  state = Status::Ready;
  proved = false;
  memset(cells, 0, sizeof(cells));
  clock.reset();
  return true;
}

void Game::layMines(games::Rng& rng, const int safe) {
  const int sx = safe % cfg.columns;
  const int sy = safe / cfg.columns;
  int pool = 0;
  for (int i = 0; i < cellCount(); ++i) {
    cells[i] = 0;
    const int dx = i % cfg.columns - sx;
    const int dy = i / cfg.columns - sy;
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) scratch[pool++] = static_cast<uint16_t>(i);
  }
  // Partial Fisher-Yates: the first `mines` slots of the pool become mines.
  for (int i = 0; i < cfg.mines; ++i) {
    const int j = i + static_cast<int>(rng.below(pool - i));
    const uint16_t t = scratch[i];
    scratch[i] = scratch[j];
    scratch[j] = t;
    cells[scratch[i]] = MINE;
  }
  countAdjacent();
}

void Game::countAdjacent() {
  for (int y = 0; y < cfg.rows; ++y) {
    for (int x = 0; x < cfg.columns; ++x) {
      int n = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if ((dx != 0 || dy != 0) && inside(x + dx, y + dy) && (cells[index(x + dx, y + dy)] & MINE)) ++n;
        }
      }
      uint8_t& c = cells[index(x, y)];
      c = static_cast<uint8_t>((c & STATE_BITS) | (n << 4));
    }
  }
}

// Opens `start` and everything a zero touches. Cells are marked when queued, so
// each enters the queue once and MAX_CELLS slots suffice. Flags stop the flood.
void Game::flood(const int start) {
  if (cells[start] & (OPEN | FLAG)) return;
  int head = 0;
  int tail = 0;
  cells[start] |= OPEN;
  scratch[tail++] = static_cast<uint16_t>(start);
  while (head < tail) {
    const int i = scratch[head++];
    if (countOf(cells[i]) != 0) continue;
    const int x = i % cfg.columns;
    const int y = i / cfg.columns;
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (!inside(x + dx, y + dy)) continue;
        const int n = index(x + dx, y + dy);
        if (cells[n] & (OPEN | FLAG)) continue;
        cells[n] |= OPEN;
        scratch[tail++] = static_cast<uint16_t>(n);
      }
    }
  }
}

// The solver's own flood over `known`. False if a deduction opened a mine,
// which would be a solver bug; the board is then simply not accepted.
bool Game::solverOpen(const int start) {
  if (known[start] != UNKNOWN) return true;
  if (cells[start] & MINE) return false;
  int head = 0;
  int tail = 0;
  known[start] = SAFE;
  scratch[tail++] = static_cast<uint16_t>(start);
  while (head < tail) {
    const int i = scratch[head++];
    if (countOf(cells[i]) != 0) continue;
    const int x = i % cfg.columns;
    const int y = i / cfg.columns;
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (!inside(x + dx, y + dy)) continue;
        const int n = index(x + dx, y + dy);
        if (known[n] != UNKNOWN) continue;
        known[n] = SAFE;
        scratch[tail++] = static_cast<uint16_t>(n);
      }
    }
  }
  return true;
}

// A player who never guesses: single-cell counts, then one number's unknowns
// contained in another's, then the global mine count. True when that clears
// every safe cell from `start`.
bool Game::solvable(const int start) {
  memset(known, UNKNOWN, cellCount());
  if (!solverOpen(start)) return false;
  const int w = cfg.columns;

  // Unknown neighbours and still-missing mines of an open cell.
  auto tally = [&](const int i, int& unknown, int& need) {
    const int x = i % w;
    const int y = i / w;
    unknown = 0;
    need = countOf(cells[i]);
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (!inside(x + dx, y + dy)) continue;
        const uint8_t k = known[index(x + dx, y + dy)];
        unknown += k == UNKNOWN;
        need -= k == KNOWN_MINE;
      }
    }
  };
  // Sets the unknown neighbours of `i` that are not neighbours of `skip` (-1: none).
  auto settleAround = [&](const int i, const int skip, const bool mine) {
    const int x = i % w;
    const int y = i / w;
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (!inside(x + dx, y + dy)) continue;
        const int n = index(x + dx, y + dy);
        if (known[n] != UNKNOWN) continue;
        if (skip >= 0 && abs(n % w - skip % w) <= 1 && abs(n / w - skip / w) <= 1) continue;
        if (mine) {
          known[n] = KNOWN_MINE;
        } else if (!solverOpen(n)) {
          return false;
        }
      }
    }
    return true;
  };

  while (true) {
    bool progress = false;
    for (int i = 0; i < cellCount(); ++i) {
      if (known[i] != SAFE) continue;
      int unknown = 0;
      int need = 0;
      tally(i, unknown, need);
      if (unknown == 0) continue;
      if (need == 0 || need == unknown) {
        if (!settleAround(i, -1, need != 0)) return false;
        progress = true;
      }
    }
    if (progress) continue;

    for (int a = 0; a < cellCount(); ++a) {
      if (known[a] != SAFE) continue;
      int unknownA = 0;
      int needA = 0;
      tally(a, unknownA, needA);
      if (unknownA == 0) continue;
      const int ax = a % w;
      const int ay = a / w;
      for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
          if ((dx == 0 && dy == 0) || !inside(ax + dx, ay + dy)) continue;
          const int b = index(ax + dx, ay + dy);
          if (known[b] != SAFE) continue;
          // Is every unknown neighbour of a also a neighbour of b?
          bool subset = true;
          for (int ey = -1; ey <= 1 && subset; ++ey) {
            for (int ex = -1; ex <= 1 && subset; ++ex) {
              if (!inside(ax + ex, ay + ey) || known[index(ax + ex, ay + ey)] != UNKNOWN) continue;
              subset = abs(dx - ex) <= 1 && abs(dy - ey) <= 1;
            }
          }
          if (!subset) continue;
          int unknownB = 0;
          int needB = 0;
          tally(b, unknownB, needB);
          const int extra = unknownB - unknownA;
          const int extraMines = needB - needA;
          if (extra == 0 || (extraMines != 0 && extraMines != extra)) continue;
          if (!settleAround(b, a, extraMines != 0)) return false;
          progress = true;
        }
      }
    }
    if (progress) continue;

    int unknown = 0;
    int minesFound = 0;
    for (int i = 0; i < cellCount(); ++i) {
      unknown += known[i] == UNKNOWN;
      minesFound += known[i] == KNOWN_MINE;
    }
    const int minesLeftHidden = cfg.mines - minesFound;
    if (unknown == 0 || (minesLeftHidden != 0 && minesLeftHidden != unknown)) break;
    for (int i = 0; i < cellCount(); ++i) {
      if (known[i] != UNKNOWN) continue;
      if (minesLeftHidden != 0) {
        known[i] = KNOWN_MINE;
      } else if (!solverOpen(i)) {
        return false;
      }
    }
  }

  for (int i = 0; i < cellCount(); ++i) {
    if (!(cells[i] & MINE) && known[i] != SAFE) return false;
  }
  return true;
}

View Game::view(const int x, const int y) const {
  if (!inside(x, y)) return View::Covered;
  const uint8_t c = cells[index(x, y)];
  if (state == Status::Lost) {
    if ((c & MINE) && (c & OPEN)) return View::Exploded;
    if ((c & FLAG) && !(c & MINE)) return View::WrongFlag;
    if ((c & MINE) && !(c & FLAG)) return View::Mine;
  }
  if (c & OPEN) return View::Open;
  return (c & FLAG) ? View::Flagged : View::Covered;
}

int Game::adjacent(const int x, const int y) const { return inside(x, y) ? countOf(cells[index(x, y)]) : 0; }

int Game::minesLeft() const {
  int flags = 0;
  for (int i = 0; i < cellCount(); ++i) flags += (cells[i] & FLAG) != 0;
  return cfg.mines - flags;
}

void Game::settle() {
  int open = 0;
  for (int i = 0; i < cellCount(); ++i) {
    if ((cells[i] & (MINE | OPEN)) == (MINE | OPEN)) state = Status::Lost;
    open += (cells[i] & (MINE | OPEN)) == OPEN;
  }
  if (state != Status::Lost && open == cellCount() - cfg.mines) {
    state = Status::Won;
    for (int i = 0; i < cellCount(); ++i) {
      if (cells[i] & MINE) cells[i] |= FLAG;
    }
  }
  if (isOver()) clock.stop(now());
}

bool Game::reveal(const int x, const int y) {
  if (!inside(x, y) || isOver()) return false;
  const int i = index(x, y);
  if (cells[i] & (OPEN | FLAG)) return false;
  if (state == Status::Ready) {
    games::Rng rng(seedValue);
    for (int attempt = 0; attempt < NO_GUESS_ATTEMPTS; ++attempt) {
      layMines(rng, i);
      proved = cfg.noGuess && solvable(i);
      if (proved || !cfg.noGuess) break;
    }
    state = Status::Playing;
    clock.start(now());
  }
  if (cells[i] & MINE) {
    cells[i] |= OPEN;
  } else {
    flood(i);
  }
  settle();
  return true;
}

bool Game::chord(const int x, const int y) {
  if (!inside(x, y) || state != Status::Playing) return false;
  const uint8_t c = cells[index(x, y)];
  if (!(c & OPEN) || countOf(c) == 0) return false;
  int flags = 0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (inside(x + dx, y + dy) && (cells[index(x + dx, y + dy)] & FLAG)) ++flags;
    }
  }
  if (flags != countOf(c)) return false;
  // Same result as revealing each neighbour in turn; a wrong flag loses here.
  bool changed = false;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) changed |= reveal(x + dx, y + dy);
  }
  return changed;
}

bool Game::dig(const int x, const int y) { return reveal(x, y) || chord(x, y); }

bool Game::toggleFlag(const int x, const int y) {
  // Nothing to flag before the mines exist.
  if (!inside(x, y) || state != Status::Playing) return false;
  uint8_t& c = cells[index(x, y)];
  if (c & OPEN) return false;
  c ^= FLAG;
  return true;
}

void Game::pause() { clock.stop(now()); }

void Game::resume() {
  if (state == Status::Playing) clock.start(now());
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("cols", cfg.columns);
  w.number("rows", cfg.rows);
  w.number("mines", cfg.mines);
  w.number("ng", cfg.noGuess ? 1 : 0);
  w.number("proved", proved ? 1 : 0);
  w.number("seed", seedValue);
  w.number("ms", elapsedMs());
  w.beginString("cells");
  for (int i = 0; i < cellCount(); ++i) w.put(static_cast<char>('0' + (cells[i] & STATE_BITS)));
  w.endString();
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t columnCount = 0;
  uint32_t rowCount = 0;
  uint32_t mineCount = 0;
  uint32_t noGuess = 0;
  uint32_t wasProved = 0;
  uint32_t seed = 0;
  uint32_t ms = 0;
  const char* board = nullptr;
  size_t boardLen = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("cols", columnCount) ||
      columnCount > MAX_COLUMNS || !r.number("rows", rowCount) || rowCount > MAX_ROWS ||
      !r.number("mines", mineCount) || mineCount > MAX_CELLS || !r.number("ng", noGuess) || noGuess > 1 ||
      !r.number("proved", wasProved) || wasProved > 1 || !r.number("seed", seed) || !r.number("ms", ms) ||
      !r.string("cells", board, boardLen)) {
    return false;
  }
  const Config config{static_cast<uint8_t>(columnCount), static_cast<uint8_t>(rowCount),
                      static_cast<uint16_t>(mineCount), noGuess == 1};
  if (!isValid(config) || boardLen != columnCount * rowCount) return false;

  int mines = 0;
  int opened = 0;
  int exploded = 0;
  int flagged = 0;
  for (size_t i = 0; i < boardLen; ++i) {
    if (board[i] < '0' || board[i] > '7') return false;
    const uint8_t c = static_cast<uint8_t>(board[i] - '0');
    if ((c & OPEN) && (c & FLAG)) return false;
    mines += (c & MINE) != 0;
    opened += (c & OPEN) != 0;
    exploded += (c & (MINE | OPEN)) == (MINE | OPEN);
    flagged += (c & FLAG) != 0;
  }
  // Either an untouched board, or every mine laid by a first dig and at most one gone off.
  if (mines == 0 ? opened != 0 || flagged != 0 : mines != static_cast<int>(mineCount) || opened == 0 || exploded > 1) {
    return false;
  }

  newGame(config, seed);
  for (size_t i = 0; i < boardLen; ++i) cells[i] = static_cast<uint8_t>(board[i] - '0');
  proved = wasProved == 1;
  if (mines != 0) {
    countAdjacent();
    state = Status::Playing;
    settle();
  }
  clock.reset(ms);
  return true;
}

}  // namespace minesweeper
