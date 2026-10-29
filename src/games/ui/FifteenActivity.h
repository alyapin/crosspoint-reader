#pragma once

#include <Fifteen.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "Пятнашки" row, like sudokuListStatus()/openSudoku().
const char* fifteenListStatus();
std::unique_ptr<Activity> openFifteen(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Full-width 4x4 board of framed tiles, the gap dithered. A tap on a tile in the
// gap's row or column slides it and those between toward the gap. Меню while
// playing, Новая игра once solved; a new game starts without a picker.
class FifteenActivity final : public GameScreen {
 public:
  FifteenActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  uint32_t idleSleepBudgetMs() const override;

 private:
  bool restore() override;
  void saveGame() override;
  void pauseGame() override { game.pause(); }
  void resumeGame() override { game.resume(); }
  void openNewGame() override;
  void pickerChosen(int, bool) override {}
  void clearSave() override;
  void build(UiScreen& screen) override;
  void onTouch(const freeink::ui::ActionEvent& event, int x, int y) override;
  void onKey(int dx, int dy, bool confirm) override;
  bool onBack() override;
  const char* confirmHint() const override { return hasBar() ? nullptr : ""; }  // arrows slide on button boards
  int32_t shownMinutes() const override;

  void startGame();
  void slideAt(int cell);
  void afterSlide();
  void runBar(int slot);
  int cellAt(int x, int y) const;
  int boardLeft() const;
  void drawTile(int cell, int left) const;

  fifteen::Game game;
  fifteen::Best best;
  bool newBest = false;
  bool bestPending = false;
  int cursor = -1;  // key focus: board cell, -1 hidden
  int barFocus = -1;
};
