#pragma once

#include <cstdint>

// SD persistence for the games, one file each under /.crosspoint/games. A save
// exists only while its game is in progress, so its presence is the "game in
// progress" flag. Some games keep a second file that outlives the game: the
// last finished reversi game (for the score it carries), the 15 puzzle best,
// the «5 букв» totals, the 2048 record and the nonogram record.

namespace sudoku {
class Game;
}
namespace minesweeper {
class Game;
}
namespace klondike {
class Game;
}
namespace fifteen {
class Game;
struct Best;
}  // namespace fifteen
namespace fiveletters {
class Game;
struct Stats;
}  // namespace fiveletters
namespace g2048 {
class Game;
}
namespace hangman {
class Game;
}
namespace reversi {
class Game;
}

namespace nonogram {
class Game;
struct Record;
}  // namespace nonogram
namespace draughts {
class Game;
}

namespace gamesave {

enum class Kind {
  Sudoku,
  Minesweeper,
  Klondike,
  Fifteen,
  FiveLetters,
  Game2048,
  Hangman,
  Reversi,
  Nonogram,
  Draughts,
};

bool hasGameInProgress(Kind kind);
// False when a file stays, also for a save that a load could not read.
bool clear(Kind kind);

// Write an unfinished game (pause() it first so its clock is charged); a
// finished one removes the save instead. Crash-safe: tmp file + rename.
bool save(const sudoku::Game& game);
bool save(const minesweeper::Game& game);
bool save(const klondike::Game& game);
bool save(const fifteen::Game& game);
bool save(const fiveletters::Game& game);
bool save(const g2048::Game& game);
bool save(const hangman::Game& game);
bool save(const reversi::Game& game);
bool save(const nonogram::Game& game);
bool save(const draughts::Game& game);

// Restore the saved game, paused. False on failure; a file that reads but does
// not parse is removed. One that cannot be read is kept: until a load reads it,
// that game is not saved or cleared, so a new game in its place plays unsaved.
bool load(sudoku::Game& game);
bool load(minesweeper::Game& game);
bool load(klondike::Game& game);
bool load(fifteen::Game& game);
bool load(fiveletters::Game& game);
bool load(g2048::Game& game);
bool load(hangman::Game& game);
// The game in progress, else the last finished one.
bool load(reversi::Game& game);
bool load(nonogram::Game& game);
bool load(draughts::Game& game);

// Records outlive their games. A missing or corrupt one loads empty; one that
// cannot be read loads false (2048: 0) and is not written until a load reads it.

// The best finished 15 puzzle, kept apart from the save.
bool save(const fifteen::Best& best);
bool load(fifteen::Best& best);

// «5 букв» totals, kept across games.
bool saveStats(const fiveletters::Stats& stats);
bool loadStats(fiveletters::Stats& stats);

// Nonogram best times and solved pictures.
bool save(const nonogram::Record& record);
bool load(nonogram::Record& record);

// The 2048 record; 0 when none.
uint32_t loadBest2048();
void saveBest2048(uint32_t score);

}  // namespace gamesave
