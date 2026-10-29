#pragma once

#include <Draughts.h>
#include <DraughtsEngine.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "Шашки" row, like sudokuListStatus()/openSudoku().
const char* draughtsListStatus();
std::unique_ptr<Activity> openDraughts(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Russian draughts on the chess board look, White at the bottom. A tap on a
// piece that can move frames it and dots its landing squares; a capture is
// entered dot by dot, × marks the pieces it takes. Keys: Left/Right walk the
// pieces that can move, then the landing squares of the one taken; Back puts
// it down. Отменить · Перевернуть (two players) · Меню, Новая игра once over.
// The computer thinks in slices on the loop task (DraughtsEngine.h); its
// answer is applied under a try-lock, so a panel refresh never stalls the loop.
// Its ~18 KB are allocated only against the computer.
class DraughtsActivity final : public GameScreen {
 public:
  DraughtsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void loop() override;
  bool skipLoopDelay() override { return engine && (engine->thinking() || engine->ready()); }

 private:
  bool restore() override;
  void saveGame() override;
  void pauseGame() override;
  void resumeGame() override;
  void openNewGame() override;
  void pickerChosen(int row, bool toggle) override;
  void clearSave() override;
  void build(UiScreen& screen) override;
  void onTouch(const freeink::ui::ActionEvent& event, int x, int y) override;
  void onKey(int dx, int dy, bool confirm) override;
  bool onBack() override;
  int menuRows(GamePickerActivity::Row (&rows)[MENU_ROWS]) const override;
  void menuChosen(int row) override;
  bool resigns() const override { return true; }
  void resign() override;

  void reset();
  void refresh();
  void afterChange();
  void play(const draughts::Move& move);
  void select(int square);
  bool stepTo(int square);
  bool onPath(const draughts::Move& move, int length) const;
  bool soleMover() const;
  bool isMover(int square) const;
  int boardItems(int* out) const;
  void placeCursor();
  void runBar(int slot);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;

  int visualIndex(int square) const;
  int squareAtPoint(int x, int y) const;
  void cellOrigin(int square, int& x, int& y) const;
  void drawBoard() const;
  void drawPiece(int x, int y, bool white, bool king, bool marked) const;
  void drawStatus() const;

  draughts::Game game;
  std::unique_ptr<draughts::Engine> engine;  // made on the computer's first turn
  bool noEngine = false;                     // it did not fit in memory: the computer cannot move
  uint32_t engineTriedMs = 0;                // the last try to make it, retried once a second
  draughts::Move moves[draughts::MAX_MOVES];
  int moveCount = 0;
  int selected = -1;
  uint8_t chain[draughts::MAX_JUMPS] = {};
  int chainLen = 0;
  int cursor = -1;  // key focus: a board square, -1 hidden
  int barFocus = -1;
  bool flipped = false;
  bool drawDeclined = false;  // until the next move
};
