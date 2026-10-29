#include "GamesActivity.h"

#include <I18n.h>

#include <iterator>
#include <memory>

#include "games/chess/ui/ChessActivity.h"
#include "games/chess/ui/chessIcons.h"
#include "games/scanword/ui/ScanwordActivity.h"
#include "games/scanword/ui/scanwordIcons.h"
#include "games/ui/FiveLettersActivity.h"
#include "games/ui/KlondikeActivity.h"
#include "games/ui/MinesweeperActivity.h"
#include "games/ui/SudokuActivity.h"
#include "games/ui/gameIcons.h"

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
    {StrId::STR_SCANWORDS, &icon_scanword_24, &openScanwords, &scanwordListStatus},
    {StrId::STR_FIVE_LETTERS, &icon_five_letters_24, &openFiveLetters, &fiveLettersListStatus},
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

void GamesActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (auto game = kGames[index].open(renderer, mappedInput)) openChild(std::move(game));
}
