#pragma once

// SD persistence for the games, one file each under /.crosspoint/games. A save
// exists only while its game is in progress, so its presence is the "game in
// progress" flag. Some games keep a second file that outlives the game: the
// last finished tic-tac-toe game (for the score it carries) and the 15 puzzle
// best.

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
namespace fifteen {
class Game;
struct Best;
}  // namespace fifteen

namespace gamesave {

enum class Kind { Sudoku, Minesweeper, Klondike, TicTacToe, Fifteen };

bool hasGameInProgress(Kind kind);
void clear(Kind kind);

// Write an unfinished game (pause() it first so its clock is charged); a
// finished one removes the save instead. Crash-safe: tmp file + rename.
bool save(const sudoku::Game& game);
bool save(const minesweeper::Game& game);
bool save(const klondike::Game& game);
bool save(const tictactoe::Game& game);
bool save(const fifteen::Game& game);

// Restore the saved game, paused. False on failure; a file that reads but does
// not parse is removed.
bool load(sudoku::Game& game);
bool load(minesweeper::Game& game);
bool load(klondike::Game& game);
// The game in progress, else the last finished one.
bool load(tictactoe::Game& game);
bool load(fifteen::Game& game);

// The best finished 15 puzzle, kept apart from the save.
bool save(const fifteen::Best& best);
bool load(fifteen::Best& best);

}  // namespace gamesave
