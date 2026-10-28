#include "ScanwordGame.h"

#include <cstring>

namespace scanword {
namespace {

constexpr uint32_t SAVE_VERSION = 1;
constexpr uint32_t MAX_SECONDS = 100u * 3600u * 24u;  // keeps sums far from overflow
constexpr uint8_t NOWHERE = 0xFF;
// Save alphabet for letter codes 1..33.
constexpr char LETTER_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefg";

uint8_t letterFromSaveChar(const char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<uint8_t>(c - 'A' + 1);
  if (c >= 'a' && c <= 'g') return static_cast<uint8_t>(c - 'a' + 27);
  return NO_LETTER;
}

struct Writer {
  char* out;
  size_t cap;
  size_t len = 0;
  bool ok = true;

  void put(const char c) {
    if (len + 1 < cap) {
      out[len++] = c;
    } else {
      ok = false;
    }
  }
  void text(const char* s) {
    while (*s != '\0') put(*s++);
  }
  void number(uint32_t v) {
    char digits[10];
    int n = 0;
    do {
      digits[n++] = static_cast<char>('0' + v % 10);
      v /= 10;
    } while (v != 0);
    while (n > 0) put(digits[--n]);
  }
  void field(const char* key, const uint32_t v, const bool last = false) {
    put('"');
    text(key);
    text("\":");
    number(v);
    if (!last) put(',');
  }
  void key(const char* k) {
    put('"');
    text(k);
    text("\":\"");
  }
};

// Reader for the flat objects serialize() writes. Values never contain quotes.
struct Reader {
  const char* text;
  size_t len;

  const char* value(const char* key) const {
    const size_t keyLen = strlen(key);
    for (size_t i = 0; i + keyLen + 2 <= len; ++i) {
      if (text[i] != '"' || strncmp(text + i + 1, key, keyLen) != 0 || text[i + 1 + keyLen] != '"') continue;
      size_t j = i + keyLen + 2;
      while (j < len && text[j] == ' ') ++j;
      if (j >= len || text[j] != ':') continue;
      ++j;
      while (j < len && text[j] == ' ') ++j;
      return j < len ? text + j : nullptr;
    }
    return nullptr;
  }

  bool number(const char* key, uint32_t& out, const uint32_t max) const {
    const char* p = value(key);
    if (p == nullptr || *p < '0' || *p > '9') return false;
    uint64_t v = 0;
    while (p < text + len && *p >= '0' && *p <= '9') {
      v = v * 10 + static_cast<uint64_t>(*p - '0');
      if (v > max) return false;
      ++p;
    }
    out = static_cast<uint32_t>(v);
    return true;
  }

  bool string(const char* key, const char*& begin, size_t& n) const {
    const char* p = value(key);
    if (p == nullptr || *p != '"') return false;
    begin = ++p;
    while (p < text + len && *p != '"') {
      if (*p == '\\') return false;
      ++p;
    }
    if (p >= text + len) return false;
    n = static_cast<size_t>(p - begin);
    return true;
  }
};

uint32_t addCapped(const uint32_t a, const uint32_t b, const uint32_t cap) { return b >= cap - a ? cap : a + b; }

}  // namespace

void Stats::record(const Progress& p) {
  if (!p.solved) return;
  solved = addCapped(solved, 1, UINT32_MAX);
  seconds = addCapped(seconds, p.seconds, UINT32_MAX);
  hints = addCapped(hints, p.hints, UINT32_MAX);
  if (p.hints == 0 && p.seconds > 0 && (bestSeconds == 0 || p.seconds < bestSeconds)) bestSeconds = p.seconds;
}

size_t Stats::serialize(char* out, const size_t cap) const {
  if (out == nullptr || cap == 0) return 0;
  Writer w{out, cap};
  w.put('{');
  w.field("v", SAVE_VERSION);
  w.field("solved", solved);
  w.field("time", seconds);
  w.field("best", bestSeconds);
  w.field("hints", hints, true);
  w.put('}');
  if (!w.ok) return 0;
  out[w.len] = '\0';
  return w.len;
}

bool Stats::deserialize(const char* text, const size_t len) {
  const Reader r{text, text != nullptr ? len : 0};
  uint32_t version = 0;
  Stats s;
  if (!r.number("v", version, UINT32_MAX) || version != SAVE_VERSION || !r.number("solved", s.solved, UINT32_MAX) ||
      !r.number("time", s.seconds, UINT32_MAX) || !r.number("best", s.bestSeconds, UINT32_MAX) ||
      !r.number("hints", s.hints, UINT32_MAX)) {
    *this = Stats{};
    return false;
  }
  *this = s;
  return true;
}

void Game::reset() {
  memset(entries, 0, sizeof(entries));
  memset(marks, 0, sizeof(marks));
  state = Progress{};
  undoHead = undoCount = 0;
  selected = NO_WORD;
  cursorCell = 0;
  if (p.wordCount() > 0) selectWord(0);
}

uint8_t Game::positionOf(const uint8_t word, const uint16_t cell) const {
  const Word& w = p.wordAt(word);
  for (uint8_t i = 0; i < w.length; ++i) {
    if (p.cellOf(w, i) == cell) return i;
  }
  return NOWHERE;
}

bool Game::inSelection(const uint16_t cell) const {
  return selected != NO_WORD && positionOf(selected, cell) != NOWHERE;
}

uint16_t Game::firstOpenCell(const uint8_t word) const {
  const Word& w = p.wordAt(word);
  for (uint8_t i = 0; i < w.length; ++i) {
    const uint16_t cell = p.cellOf(w, i);
    if (entries[cell] == NO_LETTER && !locked(cell)) return cell;
  }
  return w.start;
}

bool Game::tapCell(const uint16_t cell, const bool toggle) {
  if (cell >= p.cellCount()) return false;
  if (p.kind(cell) == CellKind::Clue) {
    uint8_t ids[2];
    const uint8_t n = p.cluesAt(cell, ids);
    if (n == 0) return false;
    return selectWord(n == 2 && selected == ids[0] ? ids[1] : ids[0]);
  }
  if (p.kind(cell) != CellKind::Letter) return false;
  const uint8_t across = p.wordAt(cell, Dir::Across);
  const uint8_t down = p.wordAt(cell, Dir::Down);
  if (toggle && selected != NO_WORD && cell == cursorCell) {
    const uint8_t other = selected == across ? down : across;
    if (other != NO_WORD) selected = other;
    return true;
  }
  if (!inSelection(cell)) {
    const bool wantAcross = selected == NO_WORD || p.wordAt(selected).dir == Dir::Across;
    const uint8_t preferred = wantAcross ? across : down;
    selected = preferred != NO_WORD ? preferred : (wantAcross ? down : across);
  }
  cursorCell = cell;
  return true;
}

bool Game::selectWord(const uint8_t word) {
  if (word >= p.wordCount()) return false;
  selected = word;
  cursorCell = firstOpenCell(word);
  return true;
}

void Game::nextWord(const int delta) {
  const int n = p.wordCount();
  if (n == 0) return;
  const int current = selected == NO_WORD ? (delta > 0 ? n - 1 : 0) : selected;
  selectWord(static_cast<uint8_t>(((current + delta) % n + n) % n));
}

bool Game::moveCursor(const int dRow, const int dCol) {
  if (dRow == 0 && dCol == 0) return false;
  int row = cursorCell / p.width();
  int col = cursorCell % p.width();
  while (true) {
    row += dRow;
    col += dCol;
    if (row < 0 || col < 0 || row >= p.height() || col >= p.width()) return false;
    const auto cell = static_cast<uint16_t>(row * p.width() + col);
    if (p.kind(cell) != CellKind::Letter) continue;
    const Dir keep = selected == NO_WORD ? (dCol != 0 ? Dir::Across : Dir::Down) : p.wordAt(selected).dir;
    const uint8_t same = p.wordAt(cell, keep);
    selected = same != NO_WORD ? same : p.wordAt(cell, keep == Dir::Across ? Dir::Down : Dir::Across);
    cursorCell = cell;
    return true;
  }
}

bool Game::toggleDirection() {
  if (selected == NO_WORD) return false;
  const Dir other = p.wordAt(selected).dir == Dir::Across ? Dir::Down : Dir::Across;
  const uint8_t word = p.wordAt(cursorCell, other);
  if (word == NO_WORD) return false;
  selected = word;
  return true;
}

void Game::pushUndo(const uint16_t cell) {
  history[undoHead] = {static_cast<uint8_t>(cell), entries[cell], marks[cell], static_cast<uint8_t>(cursorCell)};
  undoHead = static_cast<uint8_t>((undoHead + 1) % UNDO_MAX);
  if (undoCount < UNDO_MAX) ++undoCount;
}

void Game::place(const uint16_t cell, const uint8_t letter) {
  entries[cell] = letter;
  marks[cell] = static_cast<uint8_t>(marks[cell] & ~FLAG_WRONG);
}

EntryResult Game::afterChange() {
  if (state.solved) return EntryResult::Entered;
  for (uint16_t cell = 0; cell < p.cellCount(); ++cell) {
    if (p.kind(cell) == CellKind::Letter && !correct(cell)) return EntryResult::Entered;
  }
  state.solved = true;
  undoCount = 0;
  return EntryResult::Solved;
}

EntryResult Game::enter(const uint8_t letter) {
  if (letter == NO_LETTER || letter > LETTER_COUNT || selected == NO_WORD || state.solved) return EntryResult::Ignored;
  const Word& w = p.wordAt(selected);
  uint8_t pos = positionOf(selected, cursorCell);
  while (pos < w.length && locked(p.cellOf(w, pos))) ++pos;
  if (pos >= w.length) return EntryResult::Ignored;
  const uint16_t cell = p.cellOf(w, pos);
  cursorCell = cell;
  pushUndo(cell);
  place(cell, letter);
  for (uint8_t next = pos + 1; next < w.length; ++next) {
    if (!locked(p.cellOf(w, next))) {
      cursorCell = p.cellOf(w, next);
      break;
    }
  }
  return afterChange();
}

bool Game::erase() {
  if (selected == NO_WORD || state.solved) return false;
  if (entries[cursorCell] != NO_LETTER && !locked(cursorCell)) {
    pushUndo(cursorCell);
    place(cursorCell, NO_LETTER);
    return true;
  }
  const uint8_t at = positionOf(selected, cursorCell);
  if (at == NOWHERE) return false;
  const Word& w = p.wordAt(selected);
  for (int pos = at - 1; pos >= 0; --pos) {
    const uint16_t cell = p.cellOf(w, static_cast<uint8_t>(pos));
    if (locked(cell)) continue;
    pushUndo(cell);
    cursorCell = cell;
    place(cell, NO_LETTER);
    return true;
  }
  return false;
}

bool Game::canUndo() const {
  if (state.solved) return false;
  for (uint8_t i = 1; i <= undoCount; ++i) {
    if (!locked(history[(undoHead + UNDO_MAX - i) % UNDO_MAX].cell)) return true;
  }
  return false;
}

bool Game::undo() {
  while (undoCount > 0 && !state.solved) {
    undoHead = static_cast<uint8_t>((undoHead + UNDO_MAX - 1) % UNDO_MAX);
    --undoCount;
    const UndoRecord& r = history[undoHead];
    if (locked(r.cell)) continue;
    entries[r.cell] = r.before;
    marks[r.cell] = r.flags;
    cursorCell = r.cursorBefore;
    if (!inSelection(cursorCell)) {
      const Dir keep = selected == NO_WORD ? Dir::Across : p.wordAt(selected).dir;
      const uint8_t same = p.wordAt(cursorCell, keep);
      selected = same != NO_WORD ? same : p.wordAt(cursorCell, keep == Dir::Across ? Dir::Down : Dir::Across);
    }
    return true;
  }
  return false;
}

uint16_t Game::markWrong(const uint8_t word) {
  const Word& w = p.wordAt(word);
  uint16_t wrong = 0;
  for (uint8_t i = 0; i < w.length; ++i) {
    const uint16_t cell = p.cellOf(w, i);
    if (entries[cell] != NO_LETTER && !correct(cell)) {
      marks[cell] |= FLAG_WRONG;
      ++wrong;
    }
  }
  return wrong;
}

uint16_t Game::checkWord() {
  if (selected == NO_WORD || state.solved) return 0;
  if (state.checks < UINT16_MAX) ++state.checks;
  return markWrong(selected);
}

uint16_t Game::checkAll() {
  if (state.solved) return 0;
  if (state.checks < UINT16_MAX) ++state.checks;
  uint16_t wrong = 0;
  for (uint16_t cell = 0; cell < p.cellCount(); ++cell) {
    if (p.kind(cell) == CellKind::Letter && entries[cell] != NO_LETTER && !correct(cell)) {
      marks[cell] |= FLAG_WRONG;
      ++wrong;
    }
  }
  return wrong;
}

EntryResult Game::revealLetter() {
  if (selected == NO_WORD || state.solved) return EntryResult::Ignored;
  uint16_t target = cursorCell;
  if (correct(target)) {
    const Word& w = p.wordAt(selected);
    target = MAX_CELLS;
    for (uint8_t i = 0; i < w.length && target == MAX_CELLS; ++i) {
      if (!correct(p.cellOf(w, i))) target = p.cellOf(w, i);
    }
    if (target == MAX_CELLS) return EntryResult::Ignored;
  }
  entries[target] = p.solution(target);
  marks[target] = FLAG_REVEALED;
  if (state.hints < UINT16_MAX) ++state.hints;
  cursorCell = firstOpenCell(selected);
  return afterChange();
}

EntryResult Game::revealWord() {
  if (selected == NO_WORD || state.solved) return EntryResult::Ignored;
  const Word& w = p.wordAt(selected);
  bool any = false;
  for (uint8_t i = 0; i < w.length; ++i) {
    const uint16_t cell = p.cellOf(w, i);
    if (correct(cell)) continue;
    entries[cell] = p.solution(cell);
    marks[cell] = FLAG_REVEALED;
    if (state.hints < UINT16_MAX) ++state.hints;
    any = true;
  }
  return any ? afterChange() : EntryResult::Ignored;
}

bool Game::wordFilled(const uint8_t word) const {
  const Word& w = p.wordAt(word);
  for (uint8_t i = 0; i < w.length; ++i) {
    if (entries[p.cellOf(w, i)] == NO_LETTER) return false;
  }
  return true;
}

uint16_t Game::filledCount() const {
  uint16_t n = 0;
  for (uint16_t cell = 0; cell < p.cellCount(); ++cell) n += entries[cell] != NO_LETTER;
  return n;
}

void Game::addSeconds(const uint32_t s) {
  if (!state.solved) state.seconds = addCapped(state.seconds, s, MAX_SECONDS);
}

size_t Game::serialize(char* out, const size_t cap) const {
  if (out == nullptr || cap == 0) return 0;
  Writer w{out, cap};
  w.put('{');
  w.field("v", SAVE_VERSION);
  w.field("sum", p.checksum());
  w.key("cells");
  for (uint16_t cell = 0; cell < p.cellCount(); ++cell) {
    if (p.kind(cell) != CellKind::Letter) {
      w.put('#');
    } else {
      w.put(entries[cell] == NO_LETTER ? '.' : LETTER_CHARS[entries[cell] - 1]);
    }
  }
  w.text("\",");
  w.key("flags");
  for (uint16_t cell = 0; cell < p.cellCount(); ++cell) w.put(static_cast<char>('0' + marks[cell]));
  w.text("\",");
  w.field("sel", selected);
  w.field("cur", cursorCell);
  w.field("time", state.seconds);
  w.field("hints", state.hints);
  w.field("checks", state.checks);
  w.field("solved", state.solved ? 1 : 0, true);
  w.put('}');
  if (!w.ok) return 0;
  out[w.len] = '\0';
  return w.len;
}

bool Game::deserialize(const char* text, const size_t len) {
  reset();
  const Reader r{text, text != nullptr ? len : 0};
  uint32_t version = 0;
  uint32_t sum = 0;
  uint32_t sel = 0;
  uint32_t cur = 0;
  uint32_t seconds = 0;
  uint32_t hints = 0;
  uint32_t checks = 0;
  uint32_t solvedFlag = 0;
  const char* cells = nullptr;
  size_t cellsLen = 0;
  const char* flagText = nullptr;
  size_t flagsLen = 0;
  const uint16_t count = p.cellCount();
  bool ok = r.number("v", version, UINT32_MAX) && version == SAVE_VERSION && r.number("sum", sum, UINT32_MAX) &&
            sum == p.checksum() && r.string("cells", cells, cellsLen) && cellsLen == count &&
            r.string("flags", flagText, flagsLen) && flagsLen == count && r.number("sel", sel, NO_WORD) &&
            sel < p.wordCount() && r.number("cur", cur, count - 1u) && r.number("time", seconds, MAX_SECONDS) &&
            r.number("hints", hints, UINT16_MAX) && r.number("checks", checks, UINT16_MAX) &&
            r.number("solved", solvedFlag, 1);
  bool allCorrect = true;
  for (uint16_t cell = 0; ok && cell < count; ++cell) {
    const char e = cells[cell];
    const char f = flagText[cell];
    if (p.kind(cell) != CellKind::Letter) {
      ok = e == '#' && f == '0';
      continue;
    }
    const uint8_t letter = e == '.' ? NO_LETTER : letterFromSaveChar(e);
    const uint8_t mark = static_cast<uint8_t>(f - '0');
    ok = (e == '.' || letter != NO_LETTER) && f >= '0' && f <= '3' &&
         ((mark & FLAG_WRONG) == 0 || (letter != NO_LETTER && !sameLetter(letter, p.solution(cell)))) &&
         ((mark & FLAG_REVEALED) == 0 || (sameLetter(letter, p.solution(cell)) && (mark & FLAG_WRONG) == 0));
    entries[cell] = letter;
    marks[cell] = mark;
    allCorrect = allCorrect && correct(cell);
  }
  ok = ok && (solvedFlag == 1) == allCorrect && positionOf(static_cast<uint8_t>(sel), cur) != NOWHERE;
  if (!ok) {
    reset();
    return false;
  }
  selected = static_cast<uint8_t>(sel);
  cursorCell = static_cast<uint16_t>(cur);
  state.seconds = seconds;
  state.hints = static_cast<uint16_t>(hints);
  state.checks = static_cast<uint16_t>(checks);
  state.solved = allCorrect;
  return true;
}

}  // namespace scanword
