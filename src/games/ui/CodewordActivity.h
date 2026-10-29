#pragma once

#include <Codeword.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "Кейворды" row, like hangmanListStatus()/openHangman().
const char* codewordListStatus();
std::unique_ptr<Activity> openCodeword(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Кейворды: the numbered grid, the legend of numbers and a strip of the free
// letters. A tap on a cell or a legend number picks the number, a tap on a
// strip letter gives it to that number. Buttons: Left/Right walk the letter
// cells, the side buttons the rows, Confirm opens the strip, where Left/Right
// pick and Confirm places. Mistakes show only after Проверить in the menu.
// Puzzles are files from tools/codeword under /codewords on the SD card; Новая
// игра takes the next unsolved one.
class CodewordActivity final : public GameScreen {
 public:
  CodewordActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  static constexpr freeink::ui::ActionId ACTION_LEGEND = ACTION_USER;
  static constexpr freeink::ui::ActionId ACTION_STRIP = ACTION_USER + 1;

  enum class Action : uint8_t { Hint, Undo, Menu };
  enum class MenuItem : uint8_t { Check, Erase };
  // Instead of the board: no readable pack; a save that did not open, kept as it is.
  enum class Notice : uint8_t { None, NoPack, SaveKept };
  struct Layout {
    int left;
    int gridBottom;
    int legendLeft;
    int legendTop;
    int legendPitch;
    int legendColumns;
    int ruleY;
    int stripLeft;  // the left arrow
    int stripTop;
  };

  bool restore() override;
  void saveGame() override;
  void pauseGame() override { game.pause(); }
  void resumeGame() override {
    if (notice == Notice::None) game.resume();
  }
  void openNewGame() override;
  void pickerChosen(int, bool) override {}
  int menuRows(GamePickerActivity::Row (&rows)[MENU_ROWS]) const override;
  void menuChosen(int row) override;
  void clearSave() override;
  void build(UiScreen& screen) override;
  void onTouch(const freeink::ui::ActionEvent& event, int x, int y) override;
  void onKey(int dx, int dy, bool confirm) override;
  bool onBack() override;

  void startNext();
  void resetView();
  void runAction(Action action);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;
  uint8_t selected() const { return game.puzzle().number(cursor); }
  void select(uint8_t cell);
  void place(uint8_t letter);
  void afterChange();
  void moveCursor(int dx, int dy);
  void tapStrip(int slot);
  bool stripFree(uint8_t letter) const;
  int stripCount() const;
  uint8_t stripStep(uint8_t from, int step) const;
  Layout layout() const;

  void drawGrid(UiScreen& screen, const Layout& l) const;
  void drawLegend(UiScreen& screen, const Layout& l) const;
  void drawStrip(UiScreen& screen, const Layout& l) const;
  void drawNotice() const;
  void drawLetter(uint8_t letter, int x, int y, int width, int height, int capHeight) const;
  void drawMarks(uint8_t number, int x, int y, int width, int height) const;

  codeword::Game game;
  codeword::Solved solved;
  uint8_t cursor = 0;      // a letter cell; its number is the selected one
  uint8_t pick = 1;        // the strip's centre letter
  bool stripOpen = false;  // buttons: Left/Right pick a letter
  int barFocus = -1;
  int mistakes = -1;  // the last check, shown until the next change
  Notice notice = Notice::None;
  bool keepSave = false;  // this session never writes over the save that did not open
  bool solvedPending = false;
  mutable MenuItem menuItems[2] = {};  // the rows of the last menuRows()
};
