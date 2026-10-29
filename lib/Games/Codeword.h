#pragma once

// A codeword: every letter cell carries a number, one letter per number. The
// puzzles come from tools/codeword as .cwd text on the SD card:
//
//   CODEWORD 1
//   size <width> <height>
//   grid
//   <height lines of width cells: '.' block, or the answer's letter>
//   code <the letter of number 1, 2, ...>
//   given <numbers open from the start>
//   end
//
// The pack index is "CODEWORDS 1", then one decimal id per line; puzzle <id>
// is the file %04u.cwd. Letters are codes 1..32, А..Я without Ё (Ё is Е).

#include <cstddef>
#include <cstdint>

#include "GameCommon.h"

namespace codeword {

constexpr uint8_t MAX_SIDE = 11;
constexpr uint8_t MAX_CELLS = MAX_SIDE * MAX_SIDE;
constexpr uint8_t LETTER_COUNT = 32;
constexpr uint8_t MAX_NUMBERS = LETTER_COUNT;
constexpr uint16_t MAX_ID = 1023;
constexpr int UNDO_DEPTH = 64;
constexpr size_t SAVE_MAX = 512;
constexpr size_t SOLVED_MAX = 300;

// Upper case; 0 for an invalid code.
uint32_t letterCodepoint(uint8_t letter);

class Puzzle {
 public:
  // False (puzzle unchanged) when `text` is not a valid puzzle.
  bool parse(const char* text, size_t len);

  uint8_t width() const { return cols; }
  uint8_t height() const { return rows; }
  uint8_t cellCount() const { return static_cast<uint8_t>(cols * rows); }
  // 0 for a block.
  uint8_t number(const uint8_t cell) const { return cells[cell]; }
  // Numbers in use: 1..count().
  uint8_t count() const { return numbers; }
  uint8_t answer(const uint8_t number) const { return answers[number]; }
  bool given(const uint8_t number) const { return (givenMask >> number) & 1; }
  uint8_t givenCount() const;
  uint32_t checksum() const { return sum; }

 private:
  uint8_t cols = 0;
  uint8_t rows = 0;
  uint8_t numbers = 0;
  uint64_t givenMask = 0;
  uint32_t sum = 0;
  uint8_t cells[MAX_CELLS] = {};
  uint8_t answers[MAX_NUMBERS + 1] = {};
};

// The puzzle ids of an index, in order; their count, or -1 when `text` is not an index.
int parseIndex(const char* text, size_t len, uint16_t* out, int cap);

// Ids of the solved puzzles, kept across games.
class Solved {
 public:
  bool has(uint16_t id) const { return id <= MAX_ID && ((bits[id / 32] >> (id % 32)) & 1); }
  void add(uint16_t id);
  size_t serialize(char* out, size_t cap) const;
  bool deserialize(const char* text, size_t len);

 private:
  uint32_t bits[(MAX_ID + 1) / 32] = {};
};

// The first unsolved id after `current` in index order, wrapping; when all are
// solved, the next one. 0 for an empty index.
uint16_t nextPuzzle(const uint16_t* ids, int count, const Solved& solved, uint16_t current);

class Game {
 public:
  explicit Game(games::MillisFn millis) : now(millis) {}

  // A fresh game on puzzle `id`; false (game unchanged) when the text does not parse.
  bool start(uint16_t id, const char* text, size_t len);
  // After deserialize(): the text of puzzle id(); false when it is not the saved one.
  bool attach(const char* text, size_t len);
  bool attached() const { return ready; }

  uint16_t id() const { return puzzleId; }
  const Puzzle& puzzle() const { return p; }
  uint8_t letter(const uint8_t number) const { return guesses[number]; }
  bool locked(uint8_t number) const;
  bool hinted(const uint8_t number) const { return (hintMask >> number) & 1; }
  // Holding a wrong letter at the last check().
  bool wrong(const uint8_t number) const { return (wrongMask >> number) & 1; }
  // The number holding `letter`, 0 for none.
  uint8_t holder(uint8_t letter) const;
  // Numbers with a letter.
  uint8_t opened() const;
  bool solved() const;
  bool inProgress() const { return ready && moves > 0 && !solved(); }
  uint8_t hints() const { return hintCount; }

  // A letter for a number (0 erases); false when the number is locked or the letter is another number's.
  bool set(uint8_t number, uint8_t letter);
  // A step left that still changes something.
  bool canUndo() const;
  bool undo();
  bool canHint(uint8_t number) const;
  // Opens the number's letter, taking it from a number that held it.
  bool hint(uint8_t number);
  // Marks the numbers holding a wrong letter; returns their count.
  int check();

  uint32_t elapsedMs() const { return clock.elapsed(now()); }
  void pause() { clock.stop(now()); }
  void resume();

  size_t serialize(char* out, size_t cap) const;
  // Restores a serialize()d game, paused and not attached. False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  struct Step {
    uint8_t number;
    uint8_t before;
  };

  bool valid(const uint8_t number) const { return number >= 1 && number <= p.count(); }
  bool undoable(const Step& step) const;
  void push(uint8_t number, uint8_t before);
  void changed(uint8_t number);

  games::MillisFn now;
  games::Stopwatch clock;
  Puzzle p;
  bool ready = false;
  uint16_t puzzleId = 0;
  uint32_t savedSum = 0;
  uint8_t guesses[MAX_NUMBERS + 1] = {};
  uint64_t hintMask = 0;
  uint64_t wrongMask = 0;
  uint16_t moves = 0;
  uint8_t hintCount = 0;
  Step history[UNDO_DEPTH] = {};
  uint8_t historyCount = 0;
};

}  // namespace codeword
