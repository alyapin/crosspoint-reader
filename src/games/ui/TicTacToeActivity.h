#pragma once

#include <TicTacToe.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "Крестики-нолики" row, like sudokuListStatus()/openSudoku().
const char* tictactoeListStatus();
std::unique_ptr<Activity> openTicTacToe(GfxRenderer& renderer, MappedInputManager& mappedInput);

// The 3x3 board under the top line and the score under it. A tap on a free
// cell moves; against the computer its answer comes in the same refresh.
// Отменить takes the move back, Меню (Новая игра once over).
class TicTacToeActivity final : public GameScreen {
 public:
  TicTacToeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  bool restore() override;
  void saveGame() override;
  void pauseGame() override {}
  void resumeGame() override {}
  void openNewGame() override;
  void pickerChosen(int row, bool toggle) override;
  void clearSave() override;
  void build(UiScreen& screen) override;
  void onTouch(const freeink::ui::ActionEvent& event, int x, int y) override;
  void onKey(int dx, int dy, bool confirm) override;
  bool onBack() override;

  void play(int cell);
  void runBar(int slot);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;
  int cellSize() const;
  void drawMark(tictactoe::Mark mark, int x, int y, int size) const;

  tictactoe::Game game;
  int cursor = -1;  // key focus: board cell, -1 hidden
  int barFocus = -1;
};
