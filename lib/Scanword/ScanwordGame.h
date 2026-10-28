#pragma once

// Play state over a loaded Puzzle: entered letters, the selected answer and
// cursor, checks, hints, undo and the save text. This is the surface a screen
// drives:
//   tapCell() / selectWord() / nextWord() / moveCursor() / toggleDirection()
//   enter() / erase() / undo()
//   checkWord() / checkAll() / revealLetter() / revealWord()
//   addSeconds() from the screen's clock while the puzzle is open
//   serialize() / deserialize() for the SD layer
// The game keeps a reference to the puzzle, which must outlive it.

#include <cstddef>
#include <cstdint>

#include "ScanwordPuzzle.h"

namespace scanword {

enum CellFlag : uint8_t {
  FLAG_REVEALED = 1,  // filled by a hint; locked
  FLAG_WRONG = 2,     // marked by a check until the letter changes
};

enum class EntryResult : uint8_t { Ignored, Entered, Solved };

struct Progress {
  uint32_t seconds = 0;
  uint16_t hints = 0;   // revealed letters
  uint16_t checks = 0;  // check actions
  bool solved = false;
};

// Totals over all solved puzzles.
struct Stats {
  uint32_t solved = 0;
  uint32_t seconds = 0;
  uint32_t bestSeconds = 0;  // 0 until a puzzle is solved without hints
  uint32_t hints = 0;

  void record(const Progress& p);
  size_t serialize(char* out, size_t cap) const;
  bool deserialize(const char* text, size_t len);
};

// Upper bounds of serialize() output, terminator included.
constexpr size_t SAVE_MAX = 160 + 2 * MAX_CELLS;
constexpr size_t STATS_MAX = 96;

class Game {
 public:
  static constexpr uint8_t UNDO_MAX = 64;

  explicit Game(const Puzzle& puzzle) : p(puzzle) { reset(); }

  // Clears every entry, flag and counter; selects the first answer.
  void reset();
  const Puzzle& puzzle() const { return p; }

  uint8_t entry(uint16_t cell) const { return entries[cell]; }
  uint8_t flags(uint16_t cell) const { return marks[cell]; }
  uint8_t selectedWord() const { return selected; }
  uint16_t cursor() const { return cursorCell; }
  bool inSelection(uint16_t cell) const;

  // A clue cell selects its answer (a second tap on a two-clue cell takes the
  // other); a letter cell selects the answer through it, and with `toggle` a tap
  // on the cursor cell switches direction at a crossing. False for blank cells.
  bool tapCell(uint16_t cell, bool toggle = true);
  // Selects an answer with the cursor on its first empty cell.
  bool selectWord(uint8_t word);
  // Next/previous answer in clue order, wrapping.
  void nextWord(int delta);
  // Steps the cursor to the nearest letter cell in a direction (button play),
  // keeping the answer direction where the new cell allows. False at the border.
  bool moveCursor(int dRow, int dCol);
  bool toggleDirection();

  // Writes a letter (1..33) at the cursor and advances within the answer.
  EntryResult enter(uint8_t letter);
  // Clears the cursor cell, or the previous one when it is already empty.
  bool erase();
  // Reverts the last enter/erase. Cells revealed since are skipped.
  bool undo();
  bool canUndo() const;

  // Marks wrong letters of the selected answer / the grid; returns how many.
  uint16_t checkWord();
  uint16_t checkAll();
  EntryResult revealLetter();
  EntryResult revealWord();

  bool wordFilled(uint8_t word) const;
  uint16_t filledCount() const;
  bool solved() const { return state.solved; }
  const Progress& progress() const { return state; }
  // Play time is added only while unsolved.
  void addSeconds(uint32_t s);

  // Writes the save text (flat JSON) into `out`; returns its length, or 0 if `cap` is too small.
  size_t serialize(char* out, size_t cap) const;
  // Restores a save of this puzzle. On failure the game is reset and false returned.
  bool deserialize(const char* text, size_t len);

 private:
  struct UndoRecord {
    uint8_t cell;
    uint8_t before;
    uint8_t flags;
    uint8_t cursorBefore;
  };

  bool locked(uint16_t cell) const { return (marks[cell] & FLAG_REVEALED) != 0; }
  uint8_t positionOf(uint8_t word, uint16_t cell) const;
  uint16_t firstOpenCell(uint8_t word) const;
  void place(uint16_t cell, uint8_t letter);
  void pushUndo(uint16_t cell);
  EntryResult afterChange();
  bool correct(uint16_t cell) const { return sameLetter(entries[cell], p.solution(cell)); }
  uint16_t markWrong(uint8_t word);

  const Puzzle& p;
  uint8_t entries[MAX_CELLS];
  uint8_t marks[MAX_CELLS];
  uint8_t selected = NO_WORD;
  uint16_t cursorCell = 0;
  Progress state;
  UndoRecord history[UNDO_MAX];
  uint8_t undoHead = 0;
  uint8_t undoCount = 0;
};

}  // namespace scanword
