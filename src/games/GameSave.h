#pragma once

// SD persistence for the unfinished Sudoku, Minesweeper, Klondike and «5 букв» games, one
// file each under /.crosspoint/games. A file exists only while its game is in
// progress, so its presence is the "game in progress" flag.

namespace sudoku {
class Game;
}
namespace minesweeper {
class Game;
}
namespace klondike {
class Game;
}
namespace fiveletters {
class Game;
struct Stats;
}  // namespace fiveletters

namespace gamesave {

enum class Kind { Sudoku, Minesweeper, Klondike, FiveLetters };

bool hasGameInProgress(Kind kind);
void clear(Kind kind);

// Write an unfinished game (pause() it first so its clock is charged); a
// finished one removes the save instead. Crash-safe: tmp file + rename.
bool save(const sudoku::Game& game);
bool save(const minesweeper::Game& game);
bool save(const klondike::Game& game);
bool save(const fiveletters::Game& game);

// Restore the saved game, paused. False on failure; a file that reads but does
// not parse is removed.
bool load(sudoku::Game& game);
bool load(minesweeper::Game& game);
bool load(klondike::Game& game);
bool load(fiveletters::Game& game);

// «5 букв» totals, kept across games. A missing file is zero totals.
bool saveStats(const fiveletters::Stats& stats);
bool loadStats(fiveletters::Stats& stats);

}  // namespace gamesave
