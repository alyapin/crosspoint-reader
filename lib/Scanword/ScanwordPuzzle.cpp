#include "ScanwordPuzzle.h"

#include <KeyboardGames.h>

#include <cstring>

namespace scanword {
namespace {

constexpr uint32_t CP_A = 0x410;
constexpr uint32_t CP_A_LOWER = 0x430;
constexpr uint32_t CP_YO = 0x401;
constexpr uint32_t CP_YO_LOWER = 0x451;

#if SOVA_KEYBOARD_GAMES
bool isLine(const char* text, const size_t len, const char* expected) {
  return len == strlen(expected) && memcmp(text, expected, len) == 0;
}

// Reads a decimal number at text[pos] followed by a space or the end.
bool readNumber(const char* text, const size_t len, size_t& pos, uint32_t& out) {
  if (pos >= len || text[pos] < '0' || text[pos] > '9') return false;
  uint32_t v = 0;
  while (pos < len && text[pos] >= '0' && text[pos] <= '9') {
    v = v * 10 + static_cast<uint32_t>(text[pos] - '0');
    if (v > 0xFFFF) return false;
    ++pos;
  }
  if (pos < len && text[pos] != ' ') return false;
  out = v;
  return true;
}

bool skipSpace(const char* text, const size_t len, size_t& pos) {
  if (pos >= len || text[pos] != ' ') return false;
  ++pos;
  return true;
}

uint32_t fnv(uint32_t h, const uint8_t byte) { return (h ^ byte) * 16777619u; }
#endif

}  // namespace

uint8_t letterFromCodepoint(const uint32_t cp) {
  if (cp == CP_YO || cp == CP_YO_LOWER) return LETTER_YO;
  uint32_t index;
  if (cp >= CP_A && cp < CP_A + 32) {
    index = cp - CP_A;
  } else if (cp >= CP_A_LOWER && cp < CP_A_LOWER + 32) {
    index = cp - CP_A_LOWER;
  } else {
    return NO_LETTER;
  }
  // Ё sits between Е and Ж in the alphabet but outside the А..Я block.
  return static_cast<uint8_t>(index + 1 + (index >= LETTER_E ? 1 : 0));
}

uint32_t letterCodepoint(const uint8_t letter) {
  if (letter == NO_LETTER || letter > LETTER_COUNT) return 0;
  if (letter == LETTER_YO) return CP_YO;
  return CP_A + letter - (letter > LETTER_YO ? 2 : 1);
}

// The rest is the game itself (SOVA_KEYBOARD_GAMES); the letter helpers above serve the clue layout too.
#if SOVA_KEYBOARD_GAMES
std::string_view Puzzle::clue(const uint8_t index) const {
  const Word& w = list[index];
  return {pool + w.textOffset, w.textLength};
}

uint8_t Puzzle::cluesAt(const uint16_t cell, uint8_t out[2]) const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < words && n < 2; ++i) {
    if (list[i].clueCell != cell) continue;
    out[list[i].half] = i;
    ++n;
  }
  return n;
}

uint16_t Puzzle::cellOf(const Word& w, const uint8_t pos) const {
  return static_cast<uint16_t>(w.start + pos * (w.dir == Dir::Across ? 1 : cols));
}

PuzzleParser::PuzzleParser(Puzzle& out) : p(out) {
  p.cols = p.rows = p.difficulty = p.words = 0;
  p.letterCells = p.poolUsed = 0;
  p.sum = 0;
}

bool PuzzleParser::fail(const char* why) {
  if (failure == nullptr) failure = why;
  return false;
}

bool PuzzleParser::feed(const char* data, const size_t len) {
  for (size_t i = 0; i < len && failure == nullptr; ++i) {
    const char c = data[i];
    if (c == '\n') {
      if (used > 0 && buf[used - 1] == '\r') --used;
      line(buf, used);
      used = 0;
    } else if (used + 1 >= LINE_CAP) {
      return fail("line too long");
    } else {
      buf[used++] = c;
    }
  }
  return failure == nullptr;
}

bool PuzzleParser::finish() {
  if (failure == nullptr && used > 0) {
    if (buf[used - 1] == '\r') --used;
    line(buf, used);
    used = 0;
  }
  if (failure != nullptr) return false;
  if (stage != Stage::Done) return fail("truncated");
  return link();
}

bool PuzzleParser::line(const char* text, const size_t len) {
  if (len == 0) return true;
  size_t pos = 0;
  uint32_t a = 0;
  uint32_t b = 0;
  switch (stage) {
    case Stage::Header:
      if (!isLine(text, len, "SCANWORD 1")) return fail("not a scanword v1 file");
      stage = Stage::Size;
      return true;
    case Stage::Size:
      pos = 5;
      if (len < 5 || memcmp(text, "size ", 5) != 0 || !readNumber(text, len, pos, a) || !skipSpace(text, len, pos) ||
          !readNumber(text, len, pos, b) || pos != len) {
        return fail("bad size");
      }
      if (a < 3 || a > MAX_GRID_WIDTH || b < 3 || b > MAX_GRID_HEIGHT) return fail("size out of range");
      p.cols = static_cast<uint8_t>(a);
      p.rows = static_cast<uint8_t>(b);
      stage = Stage::Level;
      return true;
    case Stage::Level:
      pos = 6;
      if (len < 6 || memcmp(text, "level ", 6) != 0 || !readNumber(text, len, pos, a) || pos != len || a < 1 || a > 9) {
        return fail("bad level");
      }
      p.difficulty = static_cast<uint8_t>(a);
      stage = Stage::GridTag;
      return true;
    case Stage::GridTag:
      if (!isLine(text, len, "grid")) return fail("expected grid");
      stage = Stage::Grid;
      return true;
    case Stage::Grid:
      if (!gridLine(text, len)) return false;
      if (++gridRow == p.rows) stage = Stage::CluesTag;
      return true;
    case Stage::CluesTag:
      if (!isLine(text, len, "clues")) return fail("expected clues");
      stage = Stage::Clues;
      return true;
    case Stage::Clues:
      if (isLine(text, len, "end")) {
        stage = Stage::Done;
        return true;
      }
      return clueLine(text, len);
    case Stage::Done:
      return fail("data after end");
  }
  return fail("bad state");
}

bool PuzzleParser::gridLine(const char* text, const size_t len) {
  uint16_t cell = static_cast<uint16_t>(gridRow) * p.cols;
  const uint16_t end = cell + p.cols;
  size_t i = 0;
  while (i < len) {
    if (cell == end) return fail("grid row too long");
    const auto c = static_cast<uint8_t>(text[i]);
    uint8_t letter = NO_LETTER;
    CellKind kind = CellKind::Letter;
    if (c == '#') {
      kind = CellKind::Clue;
      ++i;
    } else if (c == '.') {
      kind = CellKind::Blank;
      ++i;
    } else if ((c == 0xD0 || c == 0xD1) && i + 1 < len && (static_cast<uint8_t>(text[i + 1]) & 0xC0) == 0x80) {
      const uint32_t cp = ((c & 0x1Fu) << 6) | (static_cast<uint8_t>(text[i + 1]) & 0x3Fu);
      letter = letterFromCodepoint(cp);
      if (letter == NO_LETTER) return fail("bad grid letter");
      i += 2;
    } else {
      return fail("bad grid cell");
    }
    p.kinds[cell] = kind;
    p.letters[cell] = letter;
    p.across[cell] = NO_WORD;
    p.down[cell] = NO_WORD;
    if (kind == CellKind::Letter) ++p.letterCells;
    ++cell;
  }
  if (cell != end) return fail("grid row too short");
  return true;
}

bool PuzzleParser::clueLine(const char* text, const size_t len) {
  size_t pos = 0;
  uint32_t row = 0;
  uint32_t col = 0;
  if (!readNumber(text, len, pos, row) || !skipSpace(text, len, pos) || !readNumber(text, len, pos, col) ||
      !skipSpace(text, len, pos)) {
    return fail("bad clue position");
  }
  if (row >= p.rows || col >= p.cols) return fail("clue outside grid");
  const size_t arrowAt = pos;
  while (pos < len && text[pos] != ' ') ++pos;
  const std::string_view token(text + arrowAt, pos - arrowAt);
  Arrow arrow;
  if (token == "R") {
    arrow = Arrow::Right;
  } else if (token == "D") {
    arrow = Arrow::Down;
  } else if (token == "DR") {
    arrow = Arrow::DownRight;
  } else if (token == "RD") {
    arrow = Arrow::RightDown;
  } else {
    return fail("bad arrow");
  }
  if (!skipSpace(text, len, pos) || pos >= len) return fail("empty clue");
  const size_t textLen = len - pos;
  if (textLen > MAX_CLUE_BYTES) return fail("clue too long");
  for (size_t i = pos; i < len; ++i) {
    if (static_cast<uint8_t>(text[i]) < 0x20) return fail("control character in clue");
  }
  if (p.words == MAX_WORDS) return fail("too many clues");
  if (p.poolUsed + textLen > CLUE_POOL_BYTES) return fail("clue text too large");

  const auto clueCell = static_cast<uint16_t>(row * p.cols + col);
  if (p.kinds[clueCell] != CellKind::Clue) return fail("clue on a non-clue cell");
  uint8_t existing[2];
  const uint8_t half = p.cluesAt(clueCell, existing);
  if (half == 2) return fail("more than two clues in a cell");

  const bool startsRight = arrow == Arrow::Right || arrow == Arrow::RightDown;
  const Dir dir = (arrow == Arrow::Right || arrow == Arrow::DownRight) ? Dir::Across : Dir::Down;
  const uint32_t startRow = startsRight ? row : row + 1;
  const uint32_t startCol = startsRight ? col + 1 : col;
  if (startRow >= p.rows || startCol >= p.cols) return fail("answer outside grid");
  const auto start = static_cast<uint16_t>(startRow * p.cols + startCol);
  const uint32_t prevRow = dir == Dir::Across ? startRow : startRow - 1;
  const uint32_t prevCol = dir == Dir::Across ? startCol - 1 : startCol;
  const bool hasPrev = dir == Dir::Across ? startCol > 0 : startRow > 0;
  if (p.kinds[start] != CellKind::Letter) return fail("answer starts on a non-letter");
  if (hasPrev && p.kinds[prevRow * p.cols + prevCol] == CellKind::Letter) return fail("answer does not start a run");

  const uint8_t index = p.words;
  uint8_t* owner = dir == Dir::Across ? p.across : p.down;
  uint8_t length = 0;
  uint32_t r = startRow;
  uint32_t c = startCol;
  while (r < p.rows && c < p.cols && p.kinds[r * p.cols + c] == CellKind::Letter) {
    uint8_t& slot = owner[r * p.cols + c];
    if (slot != NO_WORD) return fail("two clues for one answer");
    slot = index;
    ++length;
    if (dir == Dir::Across) {
      ++c;
    } else {
      ++r;
    }
  }
  if (length < MIN_ANSWER) return fail("answer too short");

  memcpy(p.pool + p.poolUsed, text + pos, textLen);
  Word& w = p.list[index];
  w.clueCell = static_cast<uint8_t>(clueCell);
  w.start = static_cast<uint8_t>(start);
  w.length = length;
  w.dir = dir;
  w.arrow = arrow;
  w.half = half;
  w.textOffset = p.poolUsed;
  w.textLength = static_cast<uint8_t>(textLen);
  p.poolUsed = static_cast<uint16_t>(p.poolUsed + textLen);
  ++p.words;
  return true;
}

bool PuzzleParser::link() {
  if (p.words == 0) return fail("no clues");
  uint32_t h = 2166136261u;
  h = fnv(h, p.cols);
  h = fnv(h, p.rows);
  for (uint16_t cell = 0; cell < p.cellCount(); ++cell) {
    h = fnv(h, static_cast<uint8_t>(p.kinds[cell]));
    h = fnv(h, p.letters[cell]);
    uint8_t ids[2];
    if (p.kinds[cell] == CellKind::Clue && p.cluesAt(cell, ids) == 0) return fail("clue cell without a clue");
    if (p.kinds[cell] != CellKind::Letter) continue;
    const uint8_t col = cell % p.cols;
    const bool letterRight = col + 1 < p.cols && p.kinds[cell + 1] == CellKind::Letter;
    const bool letterBelow = cell + p.cols < p.cellCount() && p.kinds[cell + p.cols] == CellKind::Letter;
    if (p.across[cell] == NO_WORD && p.down[cell] == NO_WORD) return fail("letter in no answer");
    // Every run of two or more letters must be an answer, or the grid misleads.
    if ((letterRight && p.across[cell] == NO_WORD) || (letterBelow && p.down[cell] == NO_WORD)) {
      return fail("letter run without a clue");
    }
  }
  p.sum = h;
  return true;
}

bool validId(const char* id) {
  size_t n = 0;
  for (; id != nullptr && id[n] != '\0'; ++n) {
    const char ch = id[n];
    const bool ok =
        (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' || ch == '-';
    if (!ok || n == ID_MAX) return false;
  }
  return n > 0;
}

bool IndexParser::feed(const char* data, const size_t len) {
  for (size_t i = 0; i < len && !failed && !halted; ++i) {
    if (data[i] == '\n') {
      if (used > 0 && buf[used - 1] == '\r') --used;
      failed = !line(buf, used);
      used = 0;
    } else if (used + 1 >= LINE_CAP) {
      failed = true;
    } else {
      buf[used++] = data[i];
    }
  }
  return !failed && !halted;
}

bool IndexParser::finish() {
  if (!failed && !halted && used > 0) {
    if (buf[used - 1] == '\r') --used;
    failed = !line(buf, used);
    used = 0;
  }
  return !failed && header;
}

bool IndexParser::line(const char* text, const size_t len) {
  if (len == 0) return true;
  if (!header) {
    header = isLine(text, len, "SCANWORDS 1");
    return header;
  }
  size_t space = 0;
  while (space < len && text[space] != ' ') ++space;
  size_t pos = space;
  uint32_t level = 0;
  if (space == 0 || space > ID_MAX || !skipSpace(text, len, pos) || !readNumber(text, len, pos, level) || pos != len ||
      level < 1 || level > 9) {
    return false;
  }
  IndexEntry entry{};
  memcpy(entry.id, text, space);
  entry.id[space] = '\0';
  entry.level = static_cast<uint8_t>(level);
  if (!validId(entry.id)) return false;
  halted = !fn(context, entry);
  return true;
}

#endif  // SOVA_KEYBOARD_GAMES

}  // namespace scanword
