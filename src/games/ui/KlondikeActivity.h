#pragma once

#include <Klondike.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "Косынка" row, like sudokuListStatus()/openSudoku().
const char* klondikeListStatus();
std::unique_ptr<Activity> openKlondike(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Stock, waste and four foundations on top, seven columns below. A tap picks a
// card (and the run on it); a tap on another pile moves it there, a second tap
// on the picked card sends it where it fits. Отменить / Подсказка / Меню.
class KlondikeActivity final : public GameScreen {
 public:
  KlondikeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  static constexpr freeink::ui::ActionId ACTION_PILE = ACTION_USER;  // value = pile
  static constexpr int kNone = -1;

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
  const char* backHint() const override {
    return pickPile != kNone || hintPile != kNone || noMoves ? tr(STR_CANCEL) : nullptr;
  }

  // Acts on `pile` at card `index` (kNone: the pile's natural card).
  void activate(int pile, int index);
  void pick(int pile, int index);
  void clearPick();
  void afterMove();
  void showHint();
  void runBar(int slot);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;

  int pileX(int pile) const;
  int wasteTopX() const;
  int cardY(int pile, int index) const;
  int cardAt(int pile, int y) const;
  void drawPile(int pile) const;
  void drawCard(int x, int y, uint8_t card) const;

  klondike::Game game;
  int pickPile = kNone;
  int pickIndex = kNone;
  int hintPile = kNone;
  bool noMoves = false;
  int focus = kNone;  // key focus: a pile (0..12), or BAR_FOCUS + slot
  bool solvableChoice = true;
};
