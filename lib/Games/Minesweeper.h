#pragma once

// A Minesweeper session: the surface a Minesweeper screen drives.
//   newGame(config, seed)   mines are laid on the first dig, around it
//   dig(x, y)               opens a covered cell or chords a satisfied number
//   toggleFlag(x, y)
//   view(x, y), adjacent(x, y), minesLeft(), status()  for drawing
//   pause() / resume()      play time runs from the first dig to the end
//   serialize() / deserialize()
// The first dig is always safe and opens an area: its cell and the 8 around it
// hold no mine. With noGuess the layout is re-rolled (bounded by
// NO_GUESS_ATTEMPTS) until a guess-free solver clears it from that dig.
// About 1.5 KB; no heap.

#include <cstddef>
#include <cstdint>

#include "GameCommon.h"

namespace minesweeper {

constexpr int MIN_SIDE = 5;
constexpr int MAX_COLUMNS = 16;
constexpr int MAX_ROWS = 30;
constexpr int MAX_CELLS = MAX_COLUMNS * MAX_ROWS;
constexpr int NO_GUESS_ATTEMPTS = 100;

struct Config {
  uint8_t columns = 9;
  uint8_t rows = 9;
  uint16_t mines = 10;
  bool noGuess = true;
};

constexpr Config BEGINNER{9, 9, 10, true};
// 40 px cells on the 480x800 portrait panel.
constexpr Config PORTRAIT{12, 16, 30, true};

// Sides within MIN_SIDE..MAX_*, at least one mine and room for the safe opening.
bool isValid(const Config& config);

enum class Status : uint8_t { Ready, Playing, Won, Lost };

enum class View : uint8_t {
  Covered,
  Flagged,
  Open,       // shows adjacent()
  Mine,       // shown once lost
  Exploded,   // the mine that was dug
  WrongFlag,  // a flag on a safe cell, shown once lost
};

constexpr size_t SAVE_MAX = 160 + MAX_CELLS;

class Game {
 public:
  explicit Game(games::MillisFn millis);

  // False (game unchanged) for an invalid config.
  bool newGame(const Config& config, uint32_t seed);

  const Config& config() const { return cfg; }
  int columns() const { return cfg.columns; }
  int rows() const { return cfg.rows; }
  uint32_t seed() const { return seedValue; }
  Status status() const { return state; }
  bool isOver() const { return state == Status::Won || state == Status::Lost; }
  bool inProgress() const { return state == Status::Playing; }
  // The laid board was proved solvable without guessing.
  bool provedNoGuess() const { return proved; }

  View view(int x, int y) const;
  // Mines around the cell.
  int adjacent(int x, int y) const;
  // Mines minus flags; negative when over-flagged.
  int minesLeft() const;

  bool reveal(int x, int y);
  // Opens the covered neighbours of a number that carries as many flags.
  bool chord(int x, int y);
  bool dig(int x, int y);
  bool toggleFlag(int x, int y);

  uint32_t elapsedMs() const { return clock.elapsed(now()); }
  void pause();
  void resume();
  bool paused() const { return !clock.isRunning(); }

  size_t serialize(char* out, size_t cap) const;
  // Restores a serialize()d game, paused. False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  bool inside(const int x, const int y) const { return x >= 0 && x < cfg.columns && y >= 0 && y < cfg.rows; }
  int index(const int x, const int y) const { return y * cfg.columns + x; }
  int cellCount() const { return cfg.columns * cfg.rows; }
  void layMines(games::Rng& rng, int safe);
  void countAdjacent();
  void flood(int start);
  bool solvable(int start);
  bool solverOpen(int start);
  void settle();

  games::MillisFn now;
  Config cfg;
  uint32_t seedValue = 0;
  Status state = Status::Ready;
  bool proved = false;
  // Bits: MINE, OPEN, FLAG; adjacent mines in the high nibble.
  uint8_t cells[MAX_CELLS] = {};
  // Solver knowledge, and a queue / pool shared by flood, solver and layout.
  uint8_t known[MAX_CELLS] = {};
  uint16_t scratch[MAX_CELLS] = {};
  games::Stopwatch clock;
};

}  // namespace minesweeper
