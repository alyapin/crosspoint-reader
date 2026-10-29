#pragma once

#include <Minesweeper.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "Сапёр" row, like sudokuListStatus()/openSudoku().
const char* minesweeperListStatus();
std::unique_ptr<Activity> openMinesweeper(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Full-width minefield: covered cells dithered, open cells white with their
// count, flags. A tap digs (or flags in Флажок mode; a held tap always flags);
// Начать заново deals the same field again, Меню holds Новая игра and Завершить.
class MinesweeperActivity final : public GameScreen {
 public:
  MinesweeperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  uint32_t idleSleepBudgetMs() const override;

 private:
  enum class Action : uint8_t { Flag, New, Menu };

  bool restore() override;
  void saveGame() override;
  void pauseGame() override { game.pause(); }
  void resumeGame() override { game.resume(); }
  void openNewGame() override;
  void pickerChosen(int row, bool toggle) override;
  void clearSave() override;
  void build(UiScreen& screen) override;
  void onTouch(const freeink::ui::ActionEvent& event, int x, int y) override;
  void onKey(int dx, int dy, bool confirm) override;
  bool onBack() override;
  int32_t shownMinutes() const override;
  bool wantsLongPress() const override { return true; }
  // Button boards: a held Confirm flags (the bar's Флажок mode is touch only).
  bool holdsConfirm() const override { return !hasBar() && barFocus < 0 && cursor >= 0 && !game.isOver(); }
  void onConfirmHold() override { act(cursor % game.columns(), cursor / game.columns(), true); }
  const char* confirmHint() const override { return barFocus < 0 && cursor >= 0 ? tr(STR_GAMES_DIG) : nullptr; }

  void runAction(Action action);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;
  void act(int x, int y, bool flag);
  int cellSize() const;
  int boardLeft() const;
  void drawCell(int x, int y, int left, int size) const;

  minesweeper::Game game;
  int cursor = -1;  // key focus: board cell, -1 hidden
  int barFocus = -1;
  bool flagMode = false;
};
