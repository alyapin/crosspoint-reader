#include "Sudoku.h"

#include <cstring>

#include "GameText.h"
#include "SudokuBank.h"

namespace sudoku {
namespace {

constexpr uint32_t SAVE_VERSION = 1;

// Cell text: '0' empty, '1'-'9' a digit, 'a'-'i' a revealed digit.
char cellChar(const uint8_t value, const bool hinted) {
  if (value == 0) return '0';
  return static_cast<char>(hinted ? 'a' + value - 1 : '0' + value);
}

bool parseCell(const char c, uint8_t& value, bool& hinted) {
  hinted = c >= 'a' && c <= 'i';
  if (hinted) {
    value = static_cast<uint8_t>(c - 'a' + 1);
  } else if (c >= '0' && c <= '9') {
    value = static_cast<uint8_t>(c - '0');
  } else {
    return false;
  }
  return true;
}

bool parseNotes(const char* p, Mask& out) {
  int v = 0;
  for (int i = 0; i < 3; ++i) {
    const int h = games::hexValue(p[i]);
    if (h < 0) return false;
    v = v * 16 + h;
  }
  if (v > ALL_DIGITS) return false;
  out = static_cast<Mask>(v);
  return true;
}

void putNotes(games::TextWriter& w, const Mask m) {
  w.put(games::hexDigit(m >> 8));
  w.put(games::hexDigit(m >> 4));
  w.put(games::hexDigit(m));
}

// A row (or column) permutation that keeps bands and commutes with the 180-degree
// turn r -> 8-r, so the clue pattern stays symmetric: 24 choices.
void symmetricLines(games::Rng& rng, uint8_t map[SIZE]) {
  uint8_t outer[BOX] = {0, 1, 2};
  for (int i = BOX - 1; i > 0; --i) {
    const int j = static_cast<int>(rng.below(i + 1));
    const uint8_t t = outer[i];
    outer[i] = outer[j];
    outer[j] = t;
  }
  const bool flipOuter = rng.next() & 1;
  const bool swapMiddle = rng.next() & 1;
  for (int r = 0; r < BOX; ++r) {
    const int to = flipOuter ? SIZE - 1 - outer[r] : outer[r];
    map[r] = static_cast<uint8_t>(to);
    map[SIZE - 1 - r] = static_cast<uint8_t>(SIZE - 1 - to);
  }
  map[3] = swapMiddle ? 5 : 3;
  map[4] = 4;
  map[5] = swapMiddle ? 3 : 5;
}

}  // namespace

int bankSize() { return BANK_PER_LEVEL; }

void bankPuzzle(const Level level, const int index, const uint32_t seed, uint8_t given[CELLS]) {
  const int lv = static_cast<int>(level) < LEVEL_COUNT ? static_cast<int>(level) : 0;
  const char* text = BANK[lv][index >= 0 && index < BANK_PER_LEVEL ? index : 0];
  games::Rng rng(seed);
  uint8_t digit[SIZE + 1] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = SIZE; i > 1; --i) {
    const int j = 1 + static_cast<int>(rng.below(i));
    const uint8_t t = digit[i];
    digit[i] = digit[j];
    digit[j] = t;
  }
  uint8_t rowMap[SIZE];
  uint8_t columnMap[SIZE];
  symmetricLines(rng, rowMap);
  symmetricLines(rng, columnMap);
  const bool transpose = rng.next() & 1;
  for (int cell = 0; cell < CELLS; ++cell) {
    const int r = rowMap[rowOf(cell)];
    const int c = columnMap[columnOf(cell)];
    given[transpose ? cellAt(c, r) : cellAt(r, c)] = digit[text[cell] - '0'];
  }
}

Game::Game(const games::MillisFn millis) : now(millis) { newGame(Level::Easy, 1); }

void Game::start(const uint8_t given[CELLS], const uint8_t solution[CELLS], const Level level, const uint32_t seed) {
  lvl = level;
  seedValue = seed;
  memcpy(givens, given, CELLS);
  memcpy(answer, solution, CELLS);
  memcpy(values, given, CELLS);
  memset(hinted, 0, sizeof(hinted));
  memset(marks, 0, sizeof(marks));
  undoTop = 0;
  undoCount = 0;
  hints = 0;
  solved = false;
  clock.reset();
  clock.start(now());
}

void Game::newGame(const Level level, const uint32_t seed) {
  games::Rng rng(seed);
  const int index = static_cast<int>(rng.below(BANK_PER_LEVEL));
  uint8_t given[CELLS];
  bankPuzzle(level, index, rng.next(), given);
  uint8_t solution[CELLS];
  // Bank puzzles are unique (host-tested), so this only derives the answer.
  countSolutions(given, 1, solution);
  start(given, solution, level, seed);
}

bool Game::newGame(const uint8_t given[CELLS], const Level level, const uint32_t seed) {
  uint8_t solution[CELLS];
  if (countSolutions(given, 2, solution) != 1) return false;
  start(given, solution, level, seed);
  return true;
}

Mask Game::visibleNotes(const int cell) const {
  if (!valid(cell) || values[cell] != 0) return 0;
  Mask seen = 0;
  for (int i = 0; i < PEERS; ++i) {
    const uint8_t v = values[PEER.peer[cell][i]];
    if (v != 0) seen = static_cast<Mask>(seen | bitFor(v));
  }
  return static_cast<Mask>(marks[cell] & ~seen);
}

bool Game::hasConflict(const int cell) const {
  if (!valid(cell) || values[cell] == 0) return false;
  for (int i = 0; i < PEERS; ++i) {
    if (values[PEER.peer[cell][i]] == values[cell]) return true;
  }
  return false;
}

int Game::digitCount(const int digit) const {
  int n = 0;
  for (int cell = 0; cell < CELLS; ++cell) n += values[cell] == digit;
  return n;
}

void Game::record(const int cell) {
  undoRing[undoTop] = Step{static_cast<uint8_t>(cell), values[cell], static_cast<uint8_t>(hinted[cell]), marks[cell]};
  undoTop = static_cast<uint16_t>((undoTop + 1) % UNDO_DEPTH);
  if (undoCount < UNDO_DEPTH) ++undoCount;  // a full ring drops the oldest step
}

void Game::settle() {
  if (memcmp(values, answer, CELLS) != 0) return;
  solved = true;
  clock.stop(now());
}

bool Game::place(const int cell, const int digit) {
  if (solved || !valid(cell) || isLocked(cell) || digit < 0 || digit > SIZE) return false;
  if (values[cell] == digit && marks[cell] == 0) return false;
  record(cell);
  values[cell] = static_cast<uint8_t>(digit);
  marks[cell] = 0;
  settle();
  return true;
}

bool Game::toggleNote(const int cell, const int digit) {
  if (solved || !valid(cell) || values[cell] != 0 || digit < 1 || digit > SIZE) return false;
  record(cell);
  marks[cell] = static_cast<Mask>(marks[cell] ^ bitFor(digit));
  return true;
}

bool Game::erase(const int cell) {
  if (solved || !valid(cell) || isLocked(cell) || (values[cell] == 0 && marks[cell] == 0)) return false;
  record(cell);
  if (values[cell] != 0) {
    values[cell] = 0;
  } else {
    marks[cell] = 0;
  }
  return true;
}

bool Game::undo() {
  if (!canUndo()) return false;
  undoTop = static_cast<uint16_t>((undoTop + UNDO_DEPTH - 1) % UNDO_DEPTH);
  --undoCount;
  const Step& step = undoRing[undoTop];
  values[step.cell] = step.value;
  hinted[step.cell] = step.hinted != 0;
  marks[step.cell] = step.notes;
  return true;
}

int Game::revealHint(const int preferred) {
  if (solved) return -1;
  int cell = -1;
  // Locked cells always hold the answer.
  if (valid(preferred) && values[preferred] != answer[preferred]) {
    cell = preferred;
  }
  for (int c = 0; c < CELLS && cell < 0; ++c) {
    if (isWrong(c)) cell = c;
  }
  if (cell < 0) {
    const Hint hint = nextHint(values, ceilingFor(Level::Expert));
    cell = hint.cell;
  }
  for (int c = 0; c < CELLS && cell < 0; ++c) {
    if (values[c] == 0) cell = c;
  }
  if (cell < 0) return -1;
  record(cell);
  values[cell] = answer[cell];
  hinted[cell] = true;
  marks[cell] = 0;
  if (hints < UINT16_MAX) ++hints;
  settle();
  return cell;
}

void Game::pause() { clock.stop(now()); }

void Game::resume() {
  if (!solved) clock.start(now());
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("level", static_cast<uint32_t>(lvl));
  w.number("seed", seedValue);
  w.number("ms", elapsedMs());
  w.number("hints", hints);
  w.beginString("given");
  for (int cell = 0; cell < CELLS; ++cell) w.put(static_cast<char>('0' + givens[cell]));
  w.endString();
  w.beginString("cells");
  for (int cell = 0; cell < CELLS; ++cell) w.put(cellChar(values[cell], hinted[cell]));
  w.endString();
  w.beginString("notes");
  for (int cell = 0; cell < CELLS; ++cell) putNotes(w, marks[cell]);
  w.endString();
  // Oldest step first.
  w.beginString("undo");
  for (int i = 0; i < undoCount; ++i) {
    const Step& s = undoRing[(undoTop + UNDO_DEPTH - undoCount + i) % UNDO_DEPTH];
    w.put(games::hexDigit(s.cell >> 4));
    w.put(games::hexDigit(s.cell));
    w.put(cellChar(s.value, s.hinted != 0));
    putNotes(w, s.notes);
  }
  w.endString();
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t level = 0;
  uint32_t seed = 0;
  uint32_t ms = 0;
  uint32_t hintCount = 0;
  const char* given = nullptr;
  const char* cells = nullptr;
  const char* notes = nullptr;
  const char* steps = nullptr;
  size_t givenLen = 0;
  size_t cellsLen = 0;
  size_t notesLen = 0;
  size_t stepsLen = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("level", level) || level >= LEVEL_COUNT ||
      !r.number("seed", seed) || !r.number("ms", ms) || !r.number("hints", hintCount) || hintCount > UINT16_MAX ||
      !r.string("given", given, givenLen) || givenLen != CELLS || !r.string("cells", cells, cellsLen) ||
      cellsLen != CELLS || !r.string("notes", notes, notesLen) || notesLen != 3 * CELLS ||
      !r.string("undo", steps, stepsLen) || stepsLen % 6 != 0 || stepsLen > 6 * UNDO_DEPTH) {
    return false;
  }

  uint8_t newGiven[CELLS];
  for (int cell = 0; cell < CELLS; ++cell) {
    if (given[cell] < '0' || given[cell] > '9') return false;
    newGiven[cell] = static_cast<uint8_t>(given[cell] - '0');
  }
  uint8_t solution[CELLS];
  if (countSolutions(newGiven, 2, solution) != 1) return false;

  uint8_t newValues[CELLS];
  bool newHinted[CELLS];
  Mask newMarks[CELLS];
  for (int cell = 0; cell < CELLS; ++cell) {
    if (!parseCell(cells[cell], newValues[cell], newHinted[cell])) return false;
    if (newGiven[cell] != 0 && (newHinted[cell] || newValues[cell] != newGiven[cell])) return false;
    if (newHinted[cell] && newValues[cell] != solution[cell]) return false;
    if (!parseNotes(notes + 3 * cell, newMarks[cell])) return false;
  }
  // Steps are validated first and parsed into the ring only once the save is accepted.
  const int stepCount = static_cast<int>(stepsLen / 6);
  auto parseStep = [&](const int i, Step& s) {
    const char* p = steps + 6 * i;
    const int hi = games::hexValue(p[0]);
    const int lo = games::hexValue(p[1]);
    bool stepHinted = false;
    if (hi < 0 || lo < 0 || hi * 16 + lo >= CELLS || !parseCell(p[2], s.value, stepHinted) ||
        !parseNotes(p + 3, s.notes)) {
      return false;
    }
    s.cell = static_cast<uint8_t>(hi * 16 + lo);
    s.hinted = stepHinted;
    return newGiven[s.cell] == 0 && (!stepHinted || s.value == solution[s.cell]);
  };
  for (int i = 0; i < stepCount; ++i) {
    Step s{};
    if (!parseStep(i, s)) return false;
  }

  start(newGiven, solution, static_cast<Level>(level), seed);
  memcpy(values, newValues, CELLS);
  memcpy(hinted, newHinted, sizeof(hinted));
  memcpy(marks, newMarks, sizeof(marks));
  for (int i = 0; i < stepCount; ++i) parseStep(i, undoRing[i]);
  undoTop = static_cast<uint16_t>(stepCount % UNDO_DEPTH);
  undoCount = static_cast<uint16_t>(stepCount);
  hints = static_cast<uint16_t>(hintCount);
  solved = memcmp(values, answer, CELLS) == 0;
  clock.reset(ms);
  return true;
}

}  // namespace sudoku
