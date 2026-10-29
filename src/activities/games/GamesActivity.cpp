#include "GamesActivity.h"

#include <I18n.h>

#include <iterator>
#include <memory>

#include "games/chess/ui/ChessActivity.h"
#include "games/chess/ui/chessIcons.h"
#include "games/ui/DraughtsActivity.h"
#include "games/ui/FifteenActivity.h"
#include "games/ui/Game2048Activity.h"
#include "games/ui/KlondikeActivity.h"
#include "games/ui/MinesweeperActivity.h"
#include "games/ui/NonogramActivity.h"
#include "games/ui/ReversiActivity.h"
#include "games/ui/SudokuActivity.h"
#include "games/ui/gameIcons.h"
#if SOVA_KEYBOARD_GAMES
#include "games/scanword/ui/ScanwordActivity.h"
#include "games/scanword/ui/scanwordIcons.h"
#include "games/ui/FiveLettersActivity.h"
#include "games/ui/HangmanActivity.h"
#endif

namespace {
struct Game {
  StrId name;
  const freeink::Icon* icon;  // 24 px Lucide icon
  std::unique_ptr<Activity> (*open)(GfxRenderer&, MappedInputManager&);
  // Right-aligned row status (e.g. an unfinished game); nullptr or "" for none.
  const char* (*status)();
};

template <typename T>
std::unique_ptr<Activity> openGame(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<T>(renderer, mappedInput);
}

// A game registers with one line:
//   {StrId::STR_<NAME>, &icon_<name>_24, &openGame<GameActivity>, &statusFn or nullptr},
// The trailing empty entry keeps the array valid while no game is registered.
constexpr Game kGames[] = {
    {StrId::STR_CHESS, &icon_chess_24, &openChess, &chessListStatus},
    {StrId::STR_SUDOKU, &icon_grid_24, &openSudoku, &sudokuListStatus},
    {StrId::STR_MINESWEEPER, &icon_bomb_24, &openMinesweeper, &minesweeperListStatus},
    {StrId::STR_KLONDIKE, &icon_spade_24, &openKlondike, &klondikeListStatus},
#if SOVA_KEYBOARD_GAMES
    {StrId::STR_SCANWORDS, &icon_scanword_24, &openScanwords, &scanwordListStatus},
#endif
    {StrId::STR_2048, &icon_tiles_24, &openGame2048, &game2048ListStatus},
    {StrId::STR_FIFTEEN, &icon_fifteen_24, &openFifteen, &fifteenListStatus},
#if SOVA_KEYBOARD_GAMES
    {StrId::STR_HANGMAN, &icon_hangman_24, &openHangman, &hangmanListStatus},
    {StrId::STR_FIVE_LETTERS, &icon_five_letters_24, &openFiveLetters, &fiveLettersListStatus},
#endif
    {StrId::STR_REVERSI, &icon_reversi_24, &openReversi, &reversiListStatus},
    {StrId::STR_DRAUGHTS, &icon_draughts_24, &openDraughts, &draughtsListStatus},
    {StrId::STR_NONOGRAM, &icon_nonogram_24, &openNonogram, &nonogramListStatus},
    {},
};
constexpr int kGameCount = static_cast<int>(std::size(kGames)) - 1;
}  // namespace

int GamesActivity::listCount() const { return kGameCount; }

const char* GamesActivity::headerTitle() const { return tr(STR_GAMES); }

const char* GamesActivity::rowLabel(const int index) const { return I18N.get(kGames[index].name); }

const freeink::Icon* GamesActivity::rowIcon(const int index) const { return kGames[index].icon; }

const char* GamesActivity::rowStatus(const int index) const {
  const auto status = kGames[index].status;
  return status ? status() : nullptr;
}

const char* GamesActivity::emptyText() const { return tr(STR_NO_GAMES); }

// The X4 Pro's twelve games fit one page at 52 px; the X4's nine keep the Setka pitch.
int GamesActivity::compactRowHeight() const { return 52; }

void GamesActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (auto game = kGames[index].open(renderer, mappedInput)) openChild(std::move(game));
}
