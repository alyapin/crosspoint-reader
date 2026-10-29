#pragma once

#include <Cards.h>
#include <FreeCell.h>

#include <memory>

#include "GameScreen.h"

const char* freecellListStatus();
std::unique_ptr<Activity> openFreeCell(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Four free cells and four foundations on top, eight columns below. Taps and
// keys as in Klondike: a tap picks a card and the run on it, a tap on another
// place moves as much of it as fits there. Отменить / Совет / Меню.
class FreeCellActivity final : public GameScreen {
 public:
  FreeCellActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  static constexpr freeink::ui::ActionId ACTION_PLACE = ACTION_USER;  // value = place
  static constexpr int kNone = -1;

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

  void startGame();
  void activate(int place, int index);
  void pick(int place, int index);
  void clearPick();
  void afterMove();
  void showHint();
  void runBar(int slot);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;

  int placeX(int place) const;
  int cardY(int place, int index) const;
  int cardAt(int place, int y) const;
  void drawPlace(int place) const;

  freecell::Game game;
  cards::Best best;
  int pickPlace = kNone;
  int pickIndex = kNone;
  int hintPlace = kNone;
  bool noMoves = false;
  bool newBest = false;
  bool bestPending = false;
  int focus = kNone;  // key focus: a place (0..15), or kBarFocus + slot
};
