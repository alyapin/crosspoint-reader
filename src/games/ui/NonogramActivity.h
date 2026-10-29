#pragma once

#include <Nonogram.h>

#include <memory>

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
  struct Layout {
    int font;
    int pitch;  // clue line height
    int gap;    // between row clue numbers
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
  int menuExtras(GamePickerActivity::Row (&rows)[MENU_EXTRAS]) const override;
  void menuExtraChosen(int index) override;
  void build(UiScreen& screen) override;
  void onTouch(const freeink::ui::ActionEvent& event, int x, int y) override;
  void onKey(int dx, int dy, bool confirm) override;
  bool onBack() override;
  int32_t shownMinutes() const override;
  bool wantsLongPress() const override { return true; }

  bool loadPicture(int side);
  bool boardShown() const { return game.hasPuzzle() && !noPictures; }
  void apply(int cell, nonogram::Mark mark);
  void runBar(int slot);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;
  Layout layout() const;
  int rowClueWidth(const nonogram::Clue& clue, int font, int gap) const;
  void drawClues(const Layout& l) const;
  void drawGrid(const Layout& l) const;
  void drawCaption(int top) const;

  nonogram::Game game;
  nonogram::Record record;
  int cursor = -1;  // board cell, -1 hidden
  bool keyCursor = false;
  int barFocus = -1;
  bool crossMode = false;
  bool checkShown = false;
  bool noPictures = false;
  bool newBest = false;
  bool recordPending = false;
};
