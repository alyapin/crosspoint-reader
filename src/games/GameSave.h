#pragma once

// SD persistence for the unfinished Sudoku, Minesweeper, Klondike and
// tic-tac-toe games, one file each under /.crosspoint/games. A file exists only
// while its game is in progress, so its presence is the "game in progress" flag.
// A finished tic-tac-toe game moves to a second file for the score it carries.

namespace sudoku {
class Game;
}
namespace minesweeper {
class Game;
}
namespace klondike {
class Game;
}
namespace tictactoe {
class Game;
}

namespace gamesave {

enum class Kind { Sudoku, Minesweeper, Klondike, TicTacToe };

bool hasGameInProgress(Kind kind);
void clear(Kind kind);

// Write an unfinished game (pause() it first so its clock is charged); a
// finished one removes the save instead. Crash-safe: tmp file + rename.
bool save(const sudoku::Game& game);
bool save(const minesweeper::Game& game);
bool save(const klondike::Game& game);
bool save(const tictactoe::Game& game);

// Restore the saved game, paused. False on failure; a file that reads but does
// not parse is removed.
bool load(sudoku::Game& game);
bool load(minesweeper::Game& game);
bool load(klondike::Game& game);
// The game in progress, else the last finished one.
bool load(tictactoe::Game& game);

}  // namespace gamesave
