#pragma once

#include <Nonogram.h>

#include <memory>
#include <string>
#include <vector>

#include "GameScreen.h"

// Games list hook for the "Японские кроссворды" row, like sudokuListStatus()/openSudoku().
const char* nonogramListStatus();
std::unique_ptr<Activity> openNonogram(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Row clues left of the grid, column clues above it, a caption under it, and
// Крестик / Отменить / Меню. A tap fills or clears a cell; in Крестик mode, or
// on a held tap, it crosses. On the X4 Confirm cycles fill, cross, empty.
// Pictures come from the SD pack (Nonogram.h), random grids from the generator.
class NonogramActivity final : public GameScreen {
 public:
  NonogramActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  uint32_t idleSleepBudgetMs() const override;

 private:
  // What the screen shows instead of a board.
  enum class Notice : uint8_t { None, NoPack, NoSize, NoRandom };
  struct Layout {
    int font;
    int pitch;       // clue line height
    int numberCell;  // least width of a row clue number's cell, so "1 1" never reads as "11"
    int cell;
    int clueLeft;
    int clueTop;
    int gridLeft;
    int gridTop;
  };

  bool restore() override;
  void saveGame() override;
  void pauseGame() override { game.pause(); }
  void resumeGame() override;
  void openNewGame() override;
  void pickerChosen(int row, bool toggle) override;
  void clearSave() override;
  int menuRows(GamePickerActivity::Row (&rows)[MENU_ROWS]) const override;
  void menuChosen(int row) override;
  void confirmRestart();
  void build(UiScreen& screen) override;
  void onTouch(const freeink::ui::ActionEvent& event, int x, int y) override;
  void onKey(int dx, int dy, bool confirm) override;
  bool onBack() override;
  int32_t shownMinutes() const override;
  bool wantsLongPress() const override { return true; }
  // Button boards: a held Confirm crosses the cell (a filled one stays filled) and starts a stroke.
  bool holdsConfirm() const override;
  void onConfirmHold() override;
  // A stroke shows only "‹ Готово"; otherwise Confirm reads Метить, whatever it does to the cell.
  const char* backHint() const override { return stroking ? tr(STR_GAMES_DONE) : nullptr; }
  const char* confirmHint() const override;

  Notice loadPicture(int side);
  bool boardShown() const { return game.hasPuzzle() && notice == Notice::None; }
  void apply(int cell, nonogram::Mark mark);
  void runBar(int slot);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;
  Layout layout() const;
  int rowClueWidth(const nonogram::Clue& clue, int font, int numberCell) const;
  // A row clue number's cell: the least width, or the number and its padding when wider.
  int numberWidth(int font, const char* text, int numberCell) const;
  void drawClues(const Layout& l) const;
  void drawGrid(const Layout& l) const;
  void drawCaption(int top) const;

  nonogram::Game game;
  nonogram::Record record;
  int cursor = -1;  // board cell, -1 hidden
  bool keyCursor = false;
  int barFocus = -1;
  bool crossMode = false;
  bool stroking = false;  // button boards: the arrows paint strokeMark on the empty cells they reach
  nonogram::Mark strokeMark = nonogram::Crossed;
  mutable const char* captionFor = nullptr;  // render task only: the key caption, wrapped once
  mutable std::vector<std::string> captionLines;
  bool checkShown = false;
  Notice notice = Notice::None;
  int noticeSide = 0;
  bool newBest = false;
  bool recordPending = false;
};
