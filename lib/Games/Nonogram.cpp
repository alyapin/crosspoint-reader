// generate() follows the generator of Pattern from Simon Tatham's Portable
// Puzzle Collection (MIT licence, see tools/nonogram/ATTRIBUTION.txt): smoothed
// noise cut at the median, kept once it is soluble by line logic. The noise is
// integer here and the line solver is a placement DP.

#include "Nonogram.h"

#include <algorithm>
#include <cstring>

#include "GameText.h"

namespace nonogram {
namespace {

constexpr uint32_t SAVE_VERSION = 1;

bool sameClue(const Clue& a, const Clue& b) {
  return a.count == b.count && std::equal(a.runs, a.runs + a.count, b.runs);
}

bool validName(const char* text, const size_t len) {
  if (len > MAX_NAME_BYTES) return false;
  for (size_t i = 0; i < len; ++i) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c < 0x20 || c == '"' || c == '\\') return false;
  }
  return true;
}

int sideIndex(const int side) { return side <= 10 ? 0 : 1; }

// Neither all black nor all white: such a line gives too much away.
bool mixedLines(const uint8_t* bits, const int side) {
  for (int i = 0; i < side; ++i) {
    int rowBlack = 0;
    int columnBlack = 0;
    for (int j = 0; j < side; ++j) {
      rowBlack += bits[i * side + j];
      columnBlack += bits[j * side + i];
    }
    if (rowBlack == 0 || rowBlack == side || columnBlack == 0 || columnBlack == side) return false;
  }
  return true;
}

bool parseNumber(const char*& p, const char* end, uint32_t& out) {
  if (p == end || *p < '0' || *p > '9') return false;
  out = 0;
  while (p != end && *p >= '0' && *p <= '9') {
    out = out * 10 + static_cast<uint32_t>(*p++ - '0');
    if (out > 1000) return false;
  }
  return true;
}

}  // namespace

Clue clueOf(const uint8_t* cells, const int n, const int stride) {
  Clue clue;
  int run = 0;
  for (int i = 0; i <= n; ++i) {
    if (i < n && cells[i * stride] == 1) {
      ++run;
    } else if (run > 0) {
      if (clue.count < MAX_RUNS) clue.runs[clue.count++] = static_cast<uint8_t>(run);
      run = 0;
    }
  }
  return clue;
}

// head[j][i]: runs 0..j-1 fit in cells [0, i) with cell i-1 white (or i == 0).
// tail[j][i]: runs j.. fit in cells [i, n).
bool solveLine(const Clue& clue, uint8_t* line, const int n) {
  const int k = clue.count;
  uint8_t whites[MAX_SIDE + 1];
  whites[0] = 0;
  for (int i = 0; i < n; ++i) whites[i + 1] = static_cast<uint8_t>(whites[i] + (line[i] == White));
  const auto canBlack = [&](const int from, const int len) { return whites[from + len] == whites[from]; };
  const auto canWhite = [&](const int i) { return line[i] != Black; };

  bool head[MAX_RUNS + 1][MAX_SIDE + 2] = {};
  bool tail[MAX_RUNS + 1][MAX_SIDE + 2] = {};
  head[0][0] = true;
  for (int i = 0; i <= n; ++i) {
    for (int j = 0; j <= k; ++j) {
      if (!head[j][i]) continue;
      if (i < n && canWhite(i)) head[j][i + 1] = true;
      if (j == k) continue;
      const int end = i + clue.runs[j];
      if (end > n || !canBlack(i, clue.runs[j])) continue;
      if (end < n && canWhite(end)) head[j + 1][end + 1] = true;
    }
  }
  tail[k][n] = true;
  for (int i = n - 1; i >= 0; --i) {
    for (int j = k; j >= 0; --j) {
      bool fits = canWhite(i) && tail[j][i + 1];
      if (!fits && j < k) {
        const int end = i + clue.runs[j];
        if (end <= n && canBlack(i, clue.runs[j])) {
          fits = end == n ? j + 1 == k : canWhite(end) && tail[j + 1][end + 1];
        }
      }
      tail[j][i] = fits;
    }
  }
  if (!tail[0][0]) return false;

  bool black[MAX_SIDE] = {};
  bool white[MAX_SIDE] = {};
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j <= k && !white[i]; ++j) white[i] = head[j][i + 1] && tail[j][i + 1];
  }
  for (int j = 0; j < k; ++j) {
    const int len = clue.runs[j];
    for (int s = 0; s + len <= n; ++s) {
      if (!head[j][s] || !canBlack(s, len)) continue;
      const int end = s + len;
      if (end == n ? j + 1 != k : !canWhite(end) || !tail[j + 1][end + 1]) continue;
      for (int i = s; i < end; ++i) black[i] = true;
    }
  }
  for (int i = 0; i < n; ++i) {
    if (!black[i] && !white[i]) return false;
    if (!white[i]) line[i] = Black;
    if (!black[i]) line[i] = White;
  }
  return true;
}

bool lineSolvable(const uint8_t* bits, const int side) {
  if (side < 1 || side > MAX_SIDE) return false;
  Clue rows[MAX_SIDE];
  Clue columns[MAX_SIDE];
  for (int i = 0; i < side; ++i) {
    rows[i] = clueOf(bits + i * side, side, 1);
    columns[i] = clueOf(bits + i, side, side);
  }
  uint8_t known[MAX_CELLS] = {};
  uint16_t rowDirty = static_cast<uint16_t>((1u << side) - 1);
  uint16_t columnDirty = rowDirty;
  uint8_t line[MAX_SIDE];
  while (rowDirty != 0 || columnDirty != 0) {
    for (int pass = 0; pass < 2; ++pass) {
      uint16_t& dirty = pass == 0 ? rowDirty : columnDirty;
      uint16_t& other = pass == 0 ? columnDirty : rowDirty;
      for (int i = 0; i < side; ++i) {
        if (!(dirty & (1u << i))) continue;
        dirty &= static_cast<uint16_t>(~(1u << i));
        const int first = pass == 0 ? i * side : i;
        const int stride = pass == 0 ? 1 : side;
        for (int j = 0; j < side; ++j) line[j] = known[first + j * stride];
        if (!solveLine(pass == 0 ? rows[i] : columns[i], line, side)) return false;
        for (int j = 0; j < side; ++j) {
          if (line[j] == known[first + j * stride]) continue;
          known[first + j * stride] = line[j];
          other |= static_cast<uint16_t>(1u << j);
        }
      }
    }
  }
  for (int i = 0; i < side * side; ++i) {
    if (known[i] != (bits[i] != 0 ? Black : White)) return false;
  }
  return true;
}

int generate(const int side, games::Rng& rng, uint8_t* bits) {
  const int count = side * side;
  uint16_t noise[MAX_CELLS];
  uint16_t smooth[MAX_CELLS];
  for (int attempt = 1; attempt <= GENERATE_ATTEMPTS; ++attempt) {
    for (int i = 0; i < count; ++i) noise[i] = static_cast<uint16_t>(rng.below(1024));
    // The mean of the 3x3 neighbourhood, scaled by 36 so corners and edges stay integer.
    for (int y = 0; y < side; ++y) {
      for (int x = 0; x < side; ++x) {
        int sum = 0;
        int cellsAround = 0;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            if (x + dx < 0 || x + dx >= side || y + dy < 0 || y + dy >= side) continue;
            sum += noise[(y + dy) * side + x + dx];
            ++cellsAround;
          }
        }
        smooth[y * side + x] = static_cast<uint16_t>(sum * (36 / cellsAround));
      }
    }
    std::copy(smooth, smooth + count, noise);
    int median = count / 2;
    if (side & 1) median += static_cast<int>(rng.below(2));
    std::nth_element(noise, noise + median, noise + count);
    const uint16_t threshold = noise[median];
    for (int i = 0; i < count; ++i) bits[i] = smooth[i] >= threshold ? 1 : 0;
    if (mixedLines(bits, side) && lineSolvable(bits, side)) return attempt;
  }
  return 0;
}

// --- Game --------------------------------------------------------------------

Game::Game(const games::MillisFn millis) : now(millis) {}

void Game::start(const int side, const Source source, const uint8_t pictureId, const char* name) {
  n = static_cast<uint8_t>(side);
  src = source;
  id = pictureId;
  const size_t len = name != nullptr ? std::min(strlen(name), static_cast<size_t>(MAX_NAME_BYTES)) : 0;
  memcpy(title, name, len);
  title[len] = '\0';
  for (int i = 0; i < side; ++i) {
    rows[i] = clueOf(solution + i * side, side, 1);
    columns[i] = clueOf(solution + i, side, side);
  }
  memset(cells, Empty, sizeof(cells));
  undoTop = 0;
  undoCount = 0;
  solved = false;
  clock.reset();
}

bool Game::newPicture(const int side, const uint8_t* bits, const uint8_t pictureId, const char* name) {
  if (side < MIN_SIDE || side > MAX_SIDE || bits == nullptr || name == nullptr || !validName(name, strlen(name))) {
    return false;
  }
  for (int i = 0; i < side * side; ++i) solution[i] = bits[i] != 0 ? 1 : 0;
  start(side, Source::Picture, pictureId, name);
  return true;
}

bool Game::newRandom(const int side, const uint32_t seed) {
  if (side < MIN_SIDE || side > MAX_SIDE) return false;
  games::Rng rng(seed);
  uint8_t bits[MAX_CELLS];
  if (generate(side, rng, bits) == 0) return false;
  std::copy(bits, bits + side * side, solution);
  start(side, Source::Random, 0, "");
  return true;
}

void Game::restart() {
  if (!hasPuzzle()) return;
  const bool running = clock.isRunning();
  memset(cells, Empty, sizeof(cells));
  undoTop = 0;
  undoCount = 0;
  solved = false;
  clock.reset();
  if (running) clock.start(now());
}

bool Game::lineMatches(const Clue& clue, const int first, const int stride) const {
  return sameClue(clue, clueOf(cells + first, n, stride));
}

// The whole line when its filled runs equal the clue. Otherwise the runs
// closed by a cross or the edge from the start, up to the first empty cell or
// wrong run; then from the end, short of where the start scan stopped and of
// the runs it took, so no run is struck twice.
uint8_t Game::doneOf(const Clue& clue, const int first, const int stride) const {
  const int k = clue.count;
  if (lineMatches(clue, first, stride)) return k == 0 ? 1 : static_cast<uint8_t>((1u << k) - 1);
  const auto at = [&](const int i) { return i < n ? cells[first + i * stride] : static_cast<uint8_t>(Crossed); };
  uint8_t done = 0;
  int next = 0;
  int run = 0;
  int stop = 0;
  for (; stop <= n; ++stop) {
    const uint8_t m = at(stop);
    if (m == Filled) {
      ++run;
      continue;
    }
    if (m == Empty || (run > 0 && (next >= k || clue.runs[next] != run))) break;
    if (run > 0) done |= static_cast<uint8_t>(1u << next++);
    run = 0;
  }
  int last = k - 1;
  run = 0;
  for (int i = n - 1; i >= stop && last >= next; --i) {
    const uint8_t m = at(i);
    if (m == Filled) {
      ++run;
      continue;
    }
    if (m == Empty || (run > 0 && clue.runs[last] != run)) break;
    if (run > 0) done |= static_cast<uint8_t>(1u << last--);
    run = 0;
  }
  return done;
}

uint8_t Game::rowDone(const int row) const { return doneOf(rows[row], row * n, 1); }

uint8_t Game::columnDone(const int column) const { return doneOf(columns[column], column, n); }

void Game::settle() {
  for (int i = 0; i < n; ++i) {
    if (!lineMatches(rows[i], i * n, 1) || !lineMatches(columns[i], i, n)) return;
  }
  solved = true;
  clock.stop(now());
}

bool Game::set(const int cell, const Mark value) {
  if (!inProgress() || cell < 0 || cell >= n * n || value > Crossed || cells[cell] == value) return false;
  undoCells[undoTop] = static_cast<uint8_t>(cell);
  undoMarks[undoTop] = cells[cell];
  undoTop = static_cast<uint8_t>((undoTop + 1) % UNDO_DEPTH);
  if (undoCount < UNDO_DEPTH) ++undoCount;
  cells[cell] = value;
  settle();
  return true;
}

bool Game::undo() {
  if (!canUndo()) return false;
  undoTop = static_cast<uint8_t>((undoTop + UNDO_DEPTH - 1) % UNDO_DEPTH);
  --undoCount;
  cells[undoCells[undoTop]] = undoMarks[undoTop];
  return true;
}

bool Game::isWrong(const int cell) const {
  return (cells[cell] == Filled && solution[cell] == 0) || (cells[cell] == Crossed && solution[cell] != 0);
}

int Game::mistakes() const {
  int count = 0;
  for (int i = 0; i < n * n; ++i) count += isWrong(i) ? 1 : 0;
  return count;
}

void Game::pause() { clock.stop(now()); }

void Game::resume() {
  if (inProgress()) clock.start(now());
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("n", n);
  w.number("src", static_cast<uint32_t>(src));
  w.number("id", id);
  w.number("ms", elapsedMs());
  w.beginString("name");
  for (const char* p = title; *p != '\0'; ++p) w.put(*p);
  w.endString();
  w.beginString("sol");
  const int count = n * n;
  for (int i = 0; i < count; i += 4) {
    int nibble = 0;
    for (int b = 0; b < 4; ++b) nibble |= (i + b < count && solution[i + b] != 0 ? 1 : 0) << b;
    w.put(games::hexDigit(nibble));
  }
  w.endString();
  w.beginString("cells");
  for (int i = 0; i < count; ++i) w.put(static_cast<char>('0' + cells[i]));
  w.endString();
  // Oldest first: the cell in two hex digits, then the mark it had.
  w.beginString("undo");
  for (int i = 0; i < undoCount; ++i) {
    const int slot = (undoTop + UNDO_DEPTH - undoCount + i) % UNDO_DEPTH;
    w.put(games::hexDigit(undoCells[slot] >> 4));
    w.put(games::hexDigit(undoCells[slot] & 0xF));
    w.put(static_cast<char>('0' + undoMarks[slot]));
  }
  w.endString();
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t side = 0;
  uint32_t source = 0;
  uint32_t pictureId = 0;
  uint32_t ms = 0;
  const char* name = nullptr;
  size_t nameLen = 0;
  const char* sol = nullptr;
  size_t solLen = 0;
  const char* marks = nullptr;
  size_t marksLen = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("n", side) || side < MIN_SIDE ||
      side > MAX_SIDE || !r.number("src", source) || source > 1 || !r.number("id", pictureId) || pictureId > 255 ||
      !r.number("ms", ms) || !r.string("name", name, nameLen) || !validName(name, nameLen) ||
      !r.string("sol", sol, solLen) || !r.string("cells", marks, marksLen)) {
    return false;
  }
  const int count = static_cast<int>(side * side);
  if (solLen != static_cast<size_t>((count + 3) / 4) || marksLen != static_cast<size_t>(count)) return false;
  uint8_t bits[MAX_CELLS];
  for (int i = 0; i < count; ++i) {
    const int nibble = games::hexValue(sol[i / 4]);
    if (nibble < 0 || marks[i] < '0' || marks[i] > '2') return false;
    bits[i] = static_cast<uint8_t>((nibble >> (i % 4)) & 1);
  }
  // Older saves have no undo history.
  const char* steps = nullptr;
  size_t stepsLen = 0;
  if (!r.string("undo", steps, stepsLen)) stepsLen = 0;
  const int stepCount = static_cast<int>(stepsLen / 3);
  if (stepsLen % 3 != 0 || stepCount > UNDO_DEPTH) return false;
  uint8_t stepCells[UNDO_DEPTH];
  uint8_t stepMarks[UNDO_DEPTH];
  for (int i = 0; i < stepCount; ++i) {
    const int high = games::hexValue(steps[3 * i]);
    const int low = games::hexValue(steps[3 * i + 1]);
    const char mark = steps[3 * i + 2];
    if (high < 0 || low < 0 || (high << 4 | low) >= count || mark < '0' || mark > '2') return false;
    stepCells[i] = static_cast<uint8_t>(high << 4 | low);
    stepMarks[i] = static_cast<uint8_t>(mark - '0');
  }
  char shown[MAX_NAME_BYTES + 1];
  memcpy(shown, name, nameLen);
  shown[nameLen] = '\0';
  std::copy(bits, bits + count, solution);
  start(static_cast<int>(side), static_cast<Source>(source), static_cast<uint8_t>(pictureId), shown);
  for (int i = 0; i < count; ++i) cells[i] = static_cast<uint8_t>(marks[i] - '0');
  std::copy(stepCells, stepCells + stepCount, undoCells);
  std::copy(stepMarks, stepMarks + stepCount, undoMarks);
  undoTop = static_cast<uint8_t>(stepCount % UNDO_DEPTH);
  undoCount = static_cast<uint8_t>(stepCount);
  settle();
  clock.reset(ms);
  return true;
}

// --- Record ------------------------------------------------------------------

bool Record::recordTime(const Source source, const int side, const uint32_t ms) {
  uint32_t& kept = best[static_cast<int>(source)][sideIndex(side)];
  if (kept != 0 && ms >= kept) return false;
  kept = std::max<uint32_t>(ms, 1);
  return true;
}

uint32_t Record::bestMs(const Source source, const int side) const {
  return best[static_cast<int>(source)][sideIndex(side)];
}

void Record::markSolved(const uint8_t pictureId) { solved[pictureId / 8] |= static_cast<uint8_t>(1u << pictureId % 8); }

bool Record::isSolved(const uint8_t pictureId) const { return (solved[pictureId / 8] >> (pictureId % 8)) & 1; }

size_t Record::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("p0", best[0][0]);
  w.number("p1", best[0][1]);
  w.number("r0", best[1][0]);
  w.number("r1", best[1][1]);
  w.beginString("solved");
  for (const uint8_t byte : solved) {
    w.put(games::hexDigit(byte >> 4));
    w.put(games::hexDigit(byte & 0xF));
  }
  w.endString();
  return w.finish();
}

bool Record::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t times[4] = {};
  const char* bits = nullptr;
  size_t bitsLen = 0;
  if (!r.number("p0", times[0]) || !r.number("p1", times[1]) || !r.number("r0", times[2]) ||
      !r.number("r1", times[3]) || !r.string("solved", bits, bitsLen) || bitsLen != 2 * sizeof(solved)) {
    return false;
  }
  uint8_t marked[sizeof(solved)];
  for (size_t i = 0; i < sizeof(solved); ++i) {
    const int high = games::hexValue(bits[2 * i]);
    const int low = games::hexValue(bits[2 * i + 1]);
    if (high < 0 || low < 0) return false;
    marked[i] = static_cast<uint8_t>(high << 4 | low);
  }
  for (int i = 0; i < 4; ++i) best[i / 2][i % 2] = times[i];
  std::copy(marked, marked + sizeof(solved), solved);
  return true;
}

// --- PackParser --------------------------------------------------------------

bool PackParser::feed(const char* data, const size_t len) {
  for (size_t i = 0; i < len && !failed && !halted; ++i) {
    if (data[i] == '\n') {
      if (!line(buf, used)) failed = true;
      used = 0;
    } else if (used < LINE_CAP) {
      buf[used++] = data[i];
    } else {
      failed = true;
    }
  }
  return !failed && !halted;
}

bool PackParser::finish() {
  if (!failed && !halted && used > 0) {
    if (!line(buf, used)) failed = true;
    used = 0;
  }
  if (failed) return false;
  return halted || (header && !inGrid);
}

bool PackParser::line(const char* text, size_t len) {
  if (len > 0 && text[len - 1] == '\r') --len;
  if (!header) {
    static constexpr char HEADER[] = "NONOGRAMS 1";
    header = len == sizeof(HEADER) - 1 && memcmp(text, HEADER, len) == 0;
    return header;
  }
  if (inGrid) {
    if (len != current.side) return false;
    for (size_t i = 0; i < len; ++i) {
      if (text[i] != '#' && text[i] != '.') return false;
      current.bits[gridRow * current.side + i] = text[i] == '#' ? 1 : 0;
    }
    if (++gridRow == current.side) {
      inGrid = false;
      if (!fn(context, current)) halted = true;
    }
    return true;
  }
  if (len == 0) return true;
  const char* p = text;
  const char* end = text + len;
  uint32_t pictureId = 0;
  uint32_t side = 0;
  if (!parseNumber(p, end, pictureId) || pictureId < 1 || pictureId > 255 || p == end || *p++ != ' ' ||
      !parseNumber(p, end, side) || side < MIN_SIDE || side > MAX_SIDE || p == end || *p++ != ' ') {
    return false;
  }
  const size_t nameLen = static_cast<size_t>(end - p);
  if (nameLen == 0 || !validName(p, nameLen)) return false;
  current = Picture{};
  current.id = static_cast<uint8_t>(pictureId);
  current.side = static_cast<uint8_t>(side);
  memcpy(current.name, p, nameLen);
  gridRow = 0;
  inGrid = true;
  return true;
}

}  // namespace nonogram
