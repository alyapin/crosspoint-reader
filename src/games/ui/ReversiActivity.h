#pragma once

#include <Reversi.h>

#include <memory>

#include "GameScreen.h"

const char* reversiListStatus();
std::unique_ptr<Activity> openReversi(GfxRenderer& renderer, MappedInputManager& mappedInput);

// The 8x8 board edge to edge under the top line, the turn and the disc count
// under it. Dots mark the legal moves, a square the computer's last move. The
// computer thinks in slices on the loop task. Отменить takes back the player's
// move with the answers, Меню (Новая игра once over).
class ReversiActivity final : public GameScreen {
 public:
  ReversiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void loop() override;
  bool skipLoopDelay() override { return engine.thinking(); }

 private:
  bool restore() override;
  void saveGame() override;
  void pauseGame() override {
    engine.cancel();
    paused = true;
  }
  void resumeGame() override { paused = false; }
  void openNewGame() override;
  void pickerChosen(int row, bool toggle) override;
  void clearSave() override;
  void build(UiScreen& screen) override;
  void onTouch(const freeink::ui::ActionEvent& event, int x, int y) override;
  void onKey(int dx, int dy, bool confirm) override;
  bool onBack() override;

  bool playerToMove() const;
  void syncFocus();
  void play(int square);
  void changed();
  void runBar(int slot);
  bool barShown(int slot) const;
  void stepFocus(int step);
  void drawStatus(int top) const;
  void drawDisc(reversi::Disc disc, int x, int y, int size) const;

  reversi::Game game;
  reversi::Engine engine;
  int cursor = -1;  // key focus: a legal move, -1 hidden
  int barFocus = -1;
  uint32_t thinkStart = 0;
  bool paused = false;  // a menu or picker is open
};
