#pragma once

// SD persistence for the unfinished Sudoku, Minesweeper, Klondike and Hangman games, one
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
namespace hangman {
class Game;
}

namespace gamesave {

enum class Kind { Sudoku, Minesweeper, Klondike, Hangman };

bool hasGameInProgress(Kind kind);
void clear(Kind kind);

// Write an unfinished game (pause() it first so its clock is charged); a
// finished one removes the save instead. Crash-safe: tmp file + rename.
bool save(const sudoku::Game& game);
bool save(const minesweeper::Game& game);
bool save(const klondike::Game& game);
bool save(const hangman::Game& game);

// Restore the saved game, paused. False on failure; a file that reads but does
// not parse is removed.
bool load(sudoku::Game& game);
bool load(minesweeper::Game& game);
bool load(klondike::Game& game);
bool load(hangman::Game& game);

}  // namespace gamesave
