#include "Codeword.h"

#include <cstring>

#include "GameText.h"

namespace codeword {
namespace {

constexpr uint32_t SAVE_VERSION = 1;
constexpr uint32_t CP_A = 0x410;
constexpr uint32_t CP_YO = 0x401;
constexpr uint8_t LETTER_E = 6;

struct Lines {
  const char* text;
  size_t len;
  size_t pos = 0;

  bool next(const char*& line, size_t& n) {
    if (pos >= len) return false;
    line = text + pos;
    const void* end = memchr(line, '\n', len - pos);
    n = end != nullptr ? static_cast<const char*>(end) - line : len - pos;
    pos += n + 1;
    if (n > 0 && line[n - 1] == '\r') --n;
    return true;
  }
};

bool is(const char* line, const size_t n, const char* word) {
  const size_t w = strlen(word);
  return n >= w && memcmp(line, word, w) == 0 && (n == w || line[w] == ' ');
}

// Decimal numbers after the keyword; their count, or -1 on anything else.
int parseNumbers(const char* line, const size_t n, const size_t from, uint32_t* out, const int cap) {
  int count = 0;
  size_t i = from;
  while (i < n) {
    if (line[i] == ' ') {
      ++i;
      continue;
    }
    if (line[i] < '0' || line[i] > '9' || count == cap) return -1;
    uint32_t value = 0;
    while (i < n && line[i] >= '0' && line[i] <= '9') {
      value = value * 10 + static_cast<uint32_t>(line[i] - '0');
      if (value > 0xFFFF) return -1;
      ++i;
    }
    out[count++] = value;
  }
  return count;
}

// One upper-case letter at text[i]: its code (Ё folds into Е) and byte length, else 0.
uint8_t letterAt(const char* text, const size_t n, const size_t i, size_t& bytes) {
  if (i + 1 >= n || static_cast<uint8_t>(text[i]) != 0xD0) return 0;
  const uint32_t cp = 0x400 | (static_cast<uint8_t>(text[i + 1]) & 0x3F);
  if (static_cast<uint8_t>(text[i + 1]) < 0x80 || static_cast<uint8_t>(text[i + 1]) > 0xBF) return 0;
  bytes = 2;
  if (cp == CP_YO) return LETTER_E;
  return cp >= CP_A && cp < CP_A + LETTER_COUNT ? static_cast<uint8_t>(cp - CP_A + 1) : 0;
}

uint32_t fnv(uint32_t h, const uint8_t byte) { return (h ^ byte) * 16777619u; }

int popcount(uint64_t v) {
  int n = 0;
  for (; v != 0; v &= v - 1) ++n;
  return n;
}

}  // namespace

uint32_t letterCodepoint(const uint8_t letter) { return letter >= 1 && letter <= LETTER_COUNT ? CP_A + letter - 1 : 0; }

bool Puzzle::parse(const char* text, const size_t len) {
  Lines lines{text, len};
  const char* line = nullptr;
  size_t n = 0;
  uint32_t size[2] = {};
  if (!lines.next(line, n) || n != 10 || memcmp(line, "CODEWORD 1", 10) != 0) return false;
  if (!lines.next(line, n) || !is(line, n, "size") || parseNumbers(line, n, 4, size, 2) != 2 || size[0] < 3 ||
      size[0] > MAX_SIDE || size[1] < 3 || size[1] > MAX_SIDE) {
    return false;
  }
  if (!lines.next(line, n) || n != 4 || memcmp(line, "grid", 4) != 0) return false;

  Puzzle out;
  out.cols = static_cast<uint8_t>(size[0]);
  out.rows = static_cast<uint8_t>(size[1]);
  uint8_t letters[MAX_CELLS] = {};
  for (uint8_t r = 0; r < out.rows; ++r) {
    if (!lines.next(line, n)) return false;
    size_t i = 0;
    for (uint8_t c = 0; c < out.cols; ++c) {
      size_t bytes = 1;
      if (i < n && line[i] == '.') {
        letters[r * out.cols + c] = 0;
      } else if (const uint8_t l = letterAt(line, n, i, bytes)) {
        letters[r * out.cols + c] = l;
      } else {
        return false;
      }
      i += bytes;
    }
    if (i != n) return false;
  }

  if (!lines.next(line, n) || !is(line, n, "code")) return false;
  uint8_t numberOf[LETTER_COUNT + 1] = {};
  for (size_t i = 5; i < n;) {
    size_t bytes = 0;
    const uint8_t l = letterAt(line, n, i, bytes);
    if (l == 0 || numberOf[l] != 0 || out.numbers == MAX_NUMBERS) return false;
    numberOf[l] = ++out.numbers;
    out.answers[out.numbers] = l;
    i += bytes;
  }
  if (out.numbers == 0) return false;

  uint64_t seen = 0;
  for (uint8_t cell = 0; cell < out.cellCount(); ++cell) {
    if (letters[cell] == 0) continue;
    const uint8_t number = numberOf[letters[cell]];
    if (number == 0) return false;
    out.cells[cell] = number;
    seen |= uint64_t{1} << number;
  }
  if (popcount(seen) != out.numbers) return false;

  uint32_t given[MAX_NUMBERS];
  if (!lines.next(line, n) || !is(line, n, "given")) return false;
  const int givenCount = parseNumbers(line, n, 5, given, MAX_NUMBERS);
  if (givenCount < 0) return false;
  for (int i = 0; i < givenCount; ++i) {
    if (given[i] < 1 || given[i] > out.numbers) return false;
    out.givenMask |= uint64_t{1} << given[i];
  }
  if (!lines.next(line, n) || n != 3 || memcmp(line, "end", 3) != 0) return false;

  uint32_t h = 2166136261u;
  h = fnv(fnv(h, out.cols), out.rows);
  for (uint8_t cell = 0; cell < out.cellCount(); ++cell) h = fnv(h, out.cells[cell]);
  for (uint8_t i = 1; i <= out.numbers; ++i) h = fnv(h, out.answers[i]);
  for (uint8_t i = 1; i <= out.numbers; ++i) h = fnv(h, out.given(i) ? 1 : 0);
  out.sum = h;
  *this = out;
  return true;
}

uint8_t Puzzle::givenCount() const { return static_cast<uint8_t>(popcount(givenMask)); }

int parseIndex(const char* text, const size_t len, uint16_t* out, const int cap) {
  Lines lines{text, len};
  const char* line = nullptr;
  size_t n = 0;
  if (!lines.next(line, n) || n != 11 || memcmp(line, "CODEWORDS 1", 11) != 0) return -1;
  int count = 0;
  while (lines.next(line, n)) {
    if (n == 0) continue;
    uint32_t id = 0;
    if (parseNumbers(line, n, 0, &id, 1) != 1 || id < 1 || id > MAX_ID) return -1;
    if (count < cap) out[count++] = static_cast<uint16_t>(id);
  }
  return count;
}

void Solved::add(const uint16_t id) {
  if (id <= MAX_ID) bits[id / 32] |= uint32_t{1} << (id % 32);
}

size_t Solved::serialize(char* out, const size_t cap) const {
  int last = static_cast<int>(sizeof(bits) / sizeof(bits[0])) - 1;
  while (last >= 0 && bits[last] == 0) --last;
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.beginString("ids");
  for (int i = 0; i <= last; ++i) {
    for (int shift = 28; shift >= 0; shift -= 4) w.put(games::hexDigit((bits[i] >> shift) & 0xF));
  }
  w.endString();
  return w.finish();
}

bool Solved::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  const char* hex = nullptr;
  size_t n = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.string("ids", hex, n) || n % 8 != 0 ||
      n / 8 > sizeof(bits) / sizeof(bits[0])) {
    return false;
  }
  uint32_t read[sizeof(bits) / sizeof(bits[0])] = {};
  for (size_t i = 0; i < n; ++i) {
    const int digit = games::hexValue(hex[i]);
    if (digit < 0) return false;
    read[i / 8] = read[i / 8] << 4 | static_cast<uint32_t>(digit);
  }
  memcpy(bits, read, sizeof(bits));
  return true;
}

uint16_t nextPuzzle(const uint16_t* ids, const int count, const Solved& solved, const uint16_t current) {
  if (count <= 0) return 0;
  int at = -1;
  for (int i = 0; i < count && at < 0; ++i) {
    if (ids[i] == current) at = i;
  }
  for (int step = 1; step <= count; ++step) {
    const uint16_t id = ids[(at + step + count) % count];
    if (id != current && !solved.has(id)) return id;
  }
  return ids[(at + 1 + count) % count];
}

// --- game ---------------------------------------------------------------------

bool Game::start(const uint16_t id, const char* text, const size_t len) {
  Puzzle next;
  if (!next.parse(text, len)) return false;
  p = next;
  ready = true;
  puzzleId = id;
  savedSum = p.checksum();
  memset(guesses, 0, sizeof(guesses));
  for (uint8_t n = 1; n <= p.count(); ++n) {
    if (p.given(n)) guesses[n] = p.answer(n);
  }
  hintMask = 0;
  wrongMask = 0;
  moves = 0;
  hintCount = 0;
  historyCount = 0;
  clock.reset();
  return true;
}

bool Game::attach(const char* text, const size_t len) {
  Puzzle saved;
  if (!saved.parse(text, len) || saved.checksum() != savedSum) return false;
  uint64_t held = 0;
  for (uint8_t n = 1; n <= MAX_NUMBERS; ++n) {
    const uint8_t l = guesses[n];
    if (n > saved.count()) {
      if (l != 0) return false;
      continue;
    }
    if ((saved.given(n) || ((hintMask >> n) & 1)) && l != saved.answer(n)) return false;
    if (l == 0) continue;
    if ((held >> l) & 1) return false;
    held |= uint64_t{1} << l;
  }
  for (uint8_t i = 0; i < historyCount; ++i) {
    if (history[i].number < 1 || history[i].number > saved.count()) return false;
  }
  if (((hintMask | wrongMask) >> (saved.count() + 1)) != 0) return false;
  p = saved;
  ready = true;
  return true;
}

bool Game::locked(const uint8_t number) const { return p.given(number) || hinted(number); }

uint8_t Game::holder(const uint8_t letter) const {
  if (letter == 0) return 0;
  for (uint8_t n = 1; n <= p.count(); ++n) {
    if (guesses[n] == letter) return n;
  }
  return 0;
}

uint8_t Game::opened() const {
  uint8_t count = 0;
  for (uint8_t n = 1; n <= p.count(); ++n) count += guesses[n] != 0;
  return count;
}

bool Game::solved() const {
  if (!ready) return false;
  for (uint8_t n = 1; n <= p.count(); ++n) {
    if (guesses[n] != p.answer(n)) return false;
  }
  return true;
}

void Game::resume() {
  if (ready && !solved()) clock.start(now());
}

void Game::push(const uint8_t number, const uint8_t before) {
  if (historyCount == UNDO_DEPTH) {
    memmove(history, history + 1, sizeof(Step) * (UNDO_DEPTH - 1));
    --historyCount;
  }
  history[historyCount++] = {number, before};
}

void Game::changed(const uint8_t number) {
  wrongMask &= ~(uint64_t{1} << number);
  if (moves < UINT16_MAX) ++moves;
  if (solved()) clock.stop(now());
}

bool Game::set(const uint8_t number, const uint8_t letter) {
  if (!ready || !valid(number) || locked(number) || letter > LETTER_COUNT || guesses[number] == letter || solved()) {
    return false;
  }
  const uint8_t other = holder(letter);
  if (other != 0 && other != number) return false;
  push(number, guesses[number]);
  guesses[number] = letter;
  changed(number);
  return true;
}

// Steps a hint has overtaken or made moot are skipped.
bool Game::undoable(const Step& step) const {
  if (locked(step.number) || guesses[step.number] == step.before) return false;
  const uint8_t other = holder(step.before);
  return other == 0 || other == step.number;
}

bool Game::canUndo() const {
  if (solved()) return false;
  for (uint8_t i = 0; i < historyCount; ++i) {
    if (undoable(history[i])) return true;
  }
  return false;
}

bool Game::undo() {
  if (solved()) return false;
  while (historyCount > 0) {
    const Step step = history[--historyCount];
    if (!undoable(step)) continue;
    guesses[step.number] = step.before;
    changed(step.number);
    return true;
  }
  return false;
}

bool Game::canHint(const uint8_t number) const { return ready && valid(number) && !locked(number) && !solved(); }

bool Game::hint(const uint8_t number) {
  if (!canHint(number)) return false;
  const uint8_t answer = p.answer(number);
  const uint8_t other = holder(answer);
  if (other != 0 && other != number) {
    guesses[other] = 0;
    wrongMask &= ~(uint64_t{1} << other);
  }
  guesses[number] = answer;
  hintMask |= uint64_t{1} << number;
  if (hintCount < UINT8_MAX) ++hintCount;
  changed(number);
  return true;
}

int Game::check() {
  wrongMask = 0;
  for (uint8_t n = 1; n <= p.count(); ++n) {
    if (guesses[n] != 0 && guesses[n] != p.answer(n)) wrongMask |= uint64_t{1} << n;
  }
  return popcount(wrongMask);
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("id", puzzleId);
  w.number("sum", p.checksum());
  w.beginString("letters");
  for (uint8_t n = 1; n <= p.count(); ++n) {
    w.put(games::hexDigit(guesses[n] >> 4));
    w.put(games::hexDigit(guesses[n] & 0xF));
  }
  w.endString();
  w.number("hinted", static_cast<uint32_t>(hintMask >> 1));
  w.number("wrong", static_cast<uint32_t>(wrongMask >> 1));
  w.number("moves", moves);
  w.number("hints", hintCount);
  w.number("ms", elapsedMs());
  w.beginString("undo");
  for (uint8_t i = 0; i < historyCount; ++i) {
    const uint8_t pair[] = {history[i].number, history[i].before};
    for (const uint8_t b : pair) {
      w.put(games::hexDigit(b >> 4));
      w.put(games::hexDigit(b & 0xF));
    }
  }
  w.endString();
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t id = 0;
  uint32_t sum = 0;
  uint32_t hinted = 0;
  uint32_t wrongBits = 0;
  uint32_t moveCount = 0;
  uint32_t hintTotal = 0;
  uint32_t ms = 0;
  const char* letters = nullptr;
  size_t lettersLen = 0;
  const char* steps = nullptr;
  size_t stepsLen = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("id", id) || id < 1 || id > MAX_ID ||
      !r.number("sum", sum) || !r.string("letters", letters, lettersLen) || lettersLen % 2 != 0 ||
      lettersLen / 2 > MAX_NUMBERS || !r.number("hinted", hinted) || !r.number("wrong", wrongBits) ||
      !r.number("moves", moveCount) || moveCount > UINT16_MAX || !r.number("hints", hintTotal) ||
      hintTotal > UINT8_MAX || !r.number("ms", ms) || !r.string("undo", steps, stepsLen) || stepsLen % 4 != 0 ||
      stepsLen / 4 > UNDO_DEPTH) {
    return false;
  }
  uint8_t bytes[MAX_NUMBERS + 2 * UNDO_DEPTH];
  const size_t count = lettersLen / 2 + stepsLen / 2;
  for (size_t i = 0; i < count; ++i) {
    const char* hex = i < lettersLen / 2 ? letters + 2 * i : steps + 2 * (i - lettersLen / 2);
    const int hi = games::hexValue(hex[0]);
    const int lo = games::hexValue(hex[1]);
    if (hi < 0 || lo < 0) return false;
    bytes[i] = static_cast<uint8_t>(hi << 4 | lo);
    if (bytes[i] > LETTER_COUNT) return false;
  }

  ready = false;
  puzzleId = static_cast<uint16_t>(id);
  savedSum = sum;
  memset(guesses, 0, sizeof(guesses));
  memcpy(guesses + 1, bytes, lettersLen / 2);
  hintMask = static_cast<uint64_t>(hinted) << 1;
  wrongMask = static_cast<uint64_t>(wrongBits) << 1;
  moves = static_cast<uint16_t>(moveCount);
  hintCount = static_cast<uint8_t>(hintTotal);
  historyCount = static_cast<uint8_t>(stepsLen / 4);
  for (uint8_t i = 0; i < historyCount; ++i) {
    history[i] = {bytes[lettersLen / 2 + 2 * i], bytes[lettersLen / 2 + 2 * i + 1]};
  }
  clock.reset(ms);
  return true;
}

}  // namespace codeword
