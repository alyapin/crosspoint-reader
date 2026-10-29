#pragma once

#include <Game2048.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "2048" row, like klondikeListStatus()/openKlondike().
const char* game2048ListStatus();
std::unique_ptr<Activity> openGame2048(GfxRenderer& renderer, MappedInputManager& mappedInput);

// A 4x4 board of tiles with the score on top and the record under it. A swipe
// on the board (the arrows on button boards) slides the tiles. Отменить / Меню.
// Keys with the bar shown: Confirm moves the focus to it, Up or Back returns it to the board.
class Game2048Activity final : public GameScreen {
 public:
  Game2048Activity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void loop() override;

 private:
  bool restore() override;
  void saveGame() override;
  void pauseGame() override {}
  void resumeGame() override {}
  void openNewGame() override;
  void pickerChosen(int, bool) override {}
  void clearSave() override;
  void build(UiScreen& screen) override;
  void onTouch(const freeink::ui::ActionEvent& event, int x, int y) override;
  void onKey(int dx, int dy, bool confirm) override;
  bool onBack() override;
  bool confirmActs() const override { return game.isOver(); }

  void slide(g2048::Dir dir);
  void runBar(int slot);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;
  void drawTile(int x, int y, uint8_t exponent) const;

  g2048::Game game;
  uint32_t best = 0;       // the record shown; the render task reads it
  uint32_t savedBest = 0;  // the record on the card; the loop task alone
  int barFocus = -1;
};
