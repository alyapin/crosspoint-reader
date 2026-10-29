#pragma once

#include <Sudoku.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "Судоку" row: sudokuListStatus() is the row status
// ("игра идёт" while a saved game exists) or nullptr; openSudoku() builds the
// screen, which resumes the saved game or opens the level picker.
const char* sudokuListStatus();
std::unique_ptr<Activity> openSudoku(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Full-width 9x9 grid, a row of nine digit keys, and Заметки / Стереть / Отменить.
// The level and the timer on the top line open the menu (Новая игра, Завершить).
class SudokuActivity final : public GameScreen {
 public:
  SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  uint32_t idleSleepBudgetMs() const override;

 private:
  // Key focus: the grid cursor is the selected cell; the pad and bar have their own.
  // Status is the level and timer on the top line, where Confirm opens the menu.
  enum class Area : uint8_t { Status, Grid, Pad, Bar };
  enum class Action : uint8_t { None, Notes, Erase, Undo, NewGame };
  static constexpr freeink::ui::ActionId ACTION_PAD = ACTION_USER;  // value = digit

  bool restore() override;
  void saveGame() override;
  void pauseGame() override { game.pause(); }
  void resumeGame() override { game.resume(); }
  void openNewGame() override;
  void pickerChosen(int row, bool toggle) override;
  void clearSave() override;
  void openStatusMenu();
  void build(UiScreen& screen) override;
  void onTouch(const freeink::ui::ActionEvent& event, int x, int y) override;
  void onKey(int dx, int dy, bool confirm) override;
  bool onBack() override;
  int32_t shownMinutes() const override;

  void barActions(Action (&out)[BAR_SLOTS]) const;
  void runAction(Action action);
  void enterDigit(int digit);
  bool padKeyShown(int digit) const;
  void drawGrid() const;
  void drawPad(UiScreen& screen) const;
  int cellAt(int x, int y) const;

  sudoku::Game game;
  int selected = 0;
  int padFocus = 0;
  int barFocus = 0;
  Area area = Area::Grid;
  bool notesMode = false;
};
