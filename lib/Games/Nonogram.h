#pragma once

// A nonogram (Japanese crossword) session: the surface a Nonogram screen drives.
//   newPicture(side, bits, id, name)  a picture from the SD pack
//   newRandom(side, seed)  a generated grid with a unique, line-solvable answer
//   set(cell, mark), undo(), restart()
//   rowClue(), columnClue(), rowDone(), columnDone()  for drawing; a done group is struck
//   isSolved(), isWrong(cell)  the win check and «Проверить»
//   pause() / resume()     play time; a restored game comes back paused
//   serialize() / deserialize()
// Cells are row-major. No heap.

#include <cstddef>
#include <cstdint>

#include "GameCommon.h"

namespace nonogram {

constexpr int MIN_SIDE = 5;
constexpr int MAX_SIDE = 15;
constexpr int MAX_CELLS = MAX_SIDE * MAX_SIDE;
constexpr int MAX_RUNS = (MAX_SIDE + 1) / 2;
constexpr int MAX_NAME_BYTES = 40;
constexpr int UNDO_DEPTH = 96;
constexpr int GENERATE_ATTEMPTS = 200;
constexpr size_t SAVE_MAX = 160 + MAX_NAME_BYTES + MAX_CELLS / 4 + 1 + MAX_CELLS;

enum Mark : uint8_t { Empty, Filled, Crossed };
enum class Source : uint8_t { Picture, Random };

struct Clue {
  uint8_t count = 0;
  uint8_t runs[MAX_RUNS] = {};
};

// Runs of cells equal to 1 (a black bit, or Filled) along `n` cells, `stride` apart.
Clue clueOf(const uint8_t* cells, int n, int stride);

// Line-solver knowledge per cell.
enum Known : uint8_t { Unknown, Black, White };

// Every cell that all placements of `clue` consistent with `line` agree on is
// set; false when no placement fits.
bool solveLine(const Clue& clue, uint8_t* line, int n);

// True when the clues of `bits` (side x side, 1 = black) pin down every cell
// by line logic alone, which also proves the answer unique.
bool lineSolvable(const uint8_t* bits, int side);

// Simon Tatham's Pattern generator (MIT): random noise smoothed over each
// 3x3 neighbourhood, cut at the median, rerolled until lineSolvable and no
// line is all black or all white. Returns the attempts used, 0 when
// GENERATE_ATTEMPTS ran out (`bits` then holds the last try).
int generate(int side, games::Rng& rng, uint8_t* bits);

class Game {
 public:
  explicit Game(games::MillisFn millis);

  // False (game unchanged) for a bad side or name.
  bool newPicture(int side, const uint8_t* bits, uint8_t id, const char* name);
  bool newRandom(int side, uint32_t seed);
  // Clears the marks of the same puzzle.
  void restart();

  bool hasPuzzle() const { return n != 0; }
  int side() const { return n; }
  Source source() const { return src; }
  uint8_t pictureId() const { return id; }
  const char* name() const { return title; }

  Mark mark(const int cell) const { return static_cast<Mark>(cells[cell]); }
  bool answer(const int cell) const { return solution[cell] != 0; }
  const Clue& rowClue(const int row) const { return rows[row]; }
  const Clue& columnClue(const int column) const { return columns[column]; }
  // Bit i set: run i is done.
  uint8_t rowDone(int row) const;
  uint8_t columnDone(int column) const;

  // Records the change for undo(); false when nothing changes or the game is over.
  bool set(int cell, Mark value);
  bool canUndo() const { return undoCount > 0 && !solved; }
  bool undo();

  bool isSolved() const { return solved; }
  bool inProgress() const { return hasPuzzle() && !solved; }
  // A filled white cell or a crossed black one.
  bool isWrong(int cell) const;
  int mistakes() const;

  uint32_t elapsedMs() const { return clock.elapsed(now()); }
  void pause();
  void resume();
  bool paused() const { return !clock.isRunning(); }

  size_t serialize(char* out, size_t cap) const;
  // Restores a serialize()d game, paused. False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  void start(int side, Source source, uint8_t pictureId, const char* name);
  uint8_t doneOf(const Clue& clue, int first, int stride) const;
  bool lineMatches(const Clue& clue, int first, int stride) const;
  void settle();

  games::MillisFn now;
  uint8_t n = 0;
  Source src = Source::Picture;
  uint8_t id = 0;
  bool solved = false;
  char title[MAX_NAME_BYTES + 1] = {};
  uint8_t solution[MAX_CELLS] = {};
  uint8_t cells[MAX_CELLS] = {};
  Clue rows[MAX_SIDE];
  Clue columns[MAX_SIDE];
  // Ring of (cell, previous mark).
  uint8_t undoCells[UNDO_DEPTH] = {};
  uint8_t undoMarks[UNDO_DEPTH] = {};
  uint8_t undoTop = 0;
  uint8_t undoCount = 0;
  games::Stopwatch clock;
};

// Kept apart from the game: best times per source and side, and the pictures solved.
struct Record {
  uint32_t best[2][2] = {};  // [source][0: 10x10 and smaller, 1: larger]
  uint8_t solved[32] = {};   // bit per picture id

  // True when the time beats the kept one and replaces it.
  bool recordTime(Source source, int side, uint32_t ms);
  uint32_t bestMs(Source source, int side) const;
  void markSolved(uint8_t pictureId);
  bool isSolved(uint8_t pictureId) const;
  size_t serialize(char* out, size_t cap) const;
  bool deserialize(const char* text, size_t len);
};
constexpr size_t RECORD_MAX = 192;

// The SD picture pack, from tools/nonogram/generator.py:
//   NONOGRAMS 1
//   <id 1..255> <side> <name>
//   <side lines of '#' black and '.' white>
//   ...
// Blank lines may separate pictures. Takes the file in chunks of any size.
struct Picture {
  uint8_t id = 0;
  uint8_t side = 0;
  char name[MAX_NAME_BYTES + 1] = {};
  uint8_t bits[MAX_CELLS] = {};
};

class PackParser {
 public:
  // Return false to stop.
  using PictureFn = bool (*)(void* ctx, const Picture& picture);
  static constexpr size_t LINE_CAP = 64;

  PackParser(PictureFn onPicture, void* ctx) : fn(onPicture), context(ctx) {}
  // False once failed or stopped.
  bool feed(const char* data, size_t len);
  // True when the whole pack parsed or the callback stopped it.
  bool finish();

 private:
  bool line(const char* text, size_t len);

  PictureFn fn;
  void* context;
  Picture current;
  uint8_t gridRow = 0;
  bool header = false;
  bool inGrid = false;
  bool failed = false;
  bool halted = false;
  size_t used = 0;
  char buf[LINE_CAP];
};

}  // namespace nonogram
