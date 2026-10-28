#pragma once

// A Russian scanword: a grid of clue cells and letter cells. Each answer starts
// next to the clue cell that announces it and runs to the next non-letter cell or
// the border. Puzzles come from tools/scanword/generator.py as .scw text:
//
//   SCANWORD 1
//   size <width> <height>
//   level <1..3>
//   grid
//   <height lines of width cells: '#' clue, '.' blank, a Russian letter>
//   clues
//   <row> <col> <R|D|DR|RD> <clue text>     (first line of a cell = top half)
//   end
//
// The puzzle list is an index file: "SCANWORDS 1", then one "<id> <level>" line
// per puzzle; ids are 1-16 characters of [0-9A-Za-z_-]. Both parsers take the
// file in chunks of any size, so loading needs no buffer for the whole file.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace scanword {

constexpr uint8_t MAX_GRID_WIDTH = 12;
constexpr uint8_t MAX_GRID_HEIGHT = 16;
constexpr uint16_t MAX_CELLS = MAX_GRID_WIDTH * MAX_GRID_HEIGHT;
constexpr uint8_t MAX_WORDS = 96;
constexpr uint8_t MIN_ANSWER = 2;
constexpr size_t MAX_CLUE_BYTES = 80;
constexpr size_t CLUE_POOL_BYTES = 4096;
constexpr uint8_t NO_WORD = 0xFF;
constexpr uint8_t NO_LETTER = 0;
constexpr uint8_t LETTER_COUNT = 33;
constexpr uint8_t LETTER_E = 6;
constexpr uint8_t LETTER_YO = 7;

// Letters are codes 1..33 in alphabet order (А=1, Е=6, Ё=7, Я=33); 0 = none.
uint8_t letterFromCodepoint(uint32_t cp);  // either case; 0 for anything else
uint32_t letterCodepoint(uint8_t letter);  // upper case; 0 for an invalid code
// Ё and Е are one letter for matching; the display keeps Ё where the answer has it.
inline uint8_t foldLetter(const uint8_t letter) { return letter == LETTER_YO ? LETTER_E : letter; }
inline bool sameLetter(const uint8_t a, const uint8_t b) { return a != NO_LETTER && foldLetter(a) == foldLetter(b); }

enum class CellKind : uint8_t { Blank, Clue, Letter };
enum class Dir : uint8_t { Across, Down };
// Where the answer starts relative to its clue cell, and which way it runs.
enum class Arrow : uint8_t {
  Right,      // starts right of the clue, runs across
  Down,       // starts below the clue, runs down
  DownRight,  // starts below the clue, runs across
  RightDown,  // starts right of the clue, runs down
};

struct Word {
  uint8_t clueCell;
  uint8_t start;
  uint8_t length;
  Dir dir;
  Arrow arrow;
  uint8_t half;  // 0: whole or top half of the clue cell, 1: bottom half
  uint16_t textOffset;
  uint8_t textLength;
};

class Puzzle {
 public:
  uint8_t width() const { return cols; }
  uint8_t height() const { return rows; }
  uint16_t cellCount() const { return static_cast<uint16_t>(cols) * rows; }
  uint8_t level() const { return difficulty; }
  CellKind kind(uint16_t cell) const { return kinds[cell]; }
  // Display letter of a letter cell: Ё only where every answer through it has Ё.
  uint8_t solution(uint16_t cell) const { return letters[cell]; }
  uint8_t wordCount() const { return words; }
  const Word& wordAt(uint8_t index) const { return list[index]; }
  std::string_view clue(uint8_t index) const;
  // Answer through a letter cell in `dir`, NO_WORD if none.
  uint8_t wordAt(uint16_t cell, Dir dir) const { return dir == Dir::Across ? across[cell] : down[cell]; }
  // Words announced by a clue cell (top half first); returns the count, 0..2.
  uint8_t cluesAt(uint16_t cell, uint8_t out[2]) const;
  uint16_t cellOf(const Word& w, uint8_t pos) const;
  uint16_t letterCount() const { return letterCells; }
  // Identifies the grid (not the clue wording) so saved progress can be matched to it.
  uint32_t checksum() const { return sum; }

 private:
  friend class PuzzleParser;

  uint8_t cols = 0;
  uint8_t rows = 0;
  uint8_t difficulty = 0;
  uint8_t words = 0;
  uint16_t letterCells = 0;
  uint16_t poolUsed = 0;
  uint32_t sum = 0;
  CellKind kinds[MAX_CELLS];
  uint8_t letters[MAX_CELLS];
  uint8_t across[MAX_CELLS];
  uint8_t down[MAX_CELLS];
  Word list[MAX_WORDS];
  char pool[CLUE_POOL_BYTES];
};

// Line-oriented loader. feed() any chunks, then finish(); the puzzle is usable
// only when finish() returns true. Errors are sticky.
class PuzzleParser {
 public:
  static constexpr size_t LINE_CAP = 128;

  explicit PuzzleParser(Puzzle& out);
  bool feed(const char* data, size_t len);
  bool finish();
  const char* error() const { return failure; }

 private:
  enum class Stage : uint8_t { Header, Size, Level, GridTag, Grid, CluesTag, Clues, Done };

  bool line(const char* text, size_t len);
  bool gridLine(const char* text, size_t len);
  bool clueLine(const char* text, size_t len);
  bool link();
  bool fail(const char* why);

  Puzzle& p;
  const char* failure = nullptr;
  Stage stage = Stage::Header;
  uint8_t gridRow = 0;
  size_t used = 0;
  char buf[LINE_CAP];
};

constexpr size_t ID_MAX = 16;

struct IndexEntry {
  char id[ID_MAX + 1];
  uint8_t level;
};

bool validId(const char* id);

// Calls `onEntry` per index line until it returns false, which stops parsing
// without an error. feed() returns false once failed or stopped; finish() tells
// them apart.
class IndexParser {
 public:
  using EntryFn = bool (*)(void* ctx, const IndexEntry& entry);
  static constexpr size_t LINE_CAP = 32;

  IndexParser(EntryFn onEntry, void* ctx) : fn(onEntry), context(ctx) {}
  bool feed(const char* data, size_t len);
  bool finish();

 private:
  bool line(const char* text, size_t len);

  EntryFn fn;
  void* context;
  bool header = false;
  bool failed = false;
  bool halted = false;
  size_t used = 0;
  char buf[LINE_CAP];
};

}  // namespace scanword
