#pragma once

#include <Cards.h>
#include <Spider.h>

#include <memory>

#include "GameScreen.h"

const char* spiderListStatus();
std::unique_ptr<Activity> openSpider(GfxRenderer& renderer, MappedInputManager& mappedInput);

// The stock top left, collected suits top right, ten columns below. Taps and
// keys as in Klondike; a tap on the stock deals. Отменить / Совет / Меню.
class SpiderActivity final : public GameScreen {
 public:
  SpiderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  static constexpr freeink::ui::ActionId ACTION_COLUMN = ACTION_USER;  // value = column
  static constexpr freeink::ui::ActionId ACTION_STOCK = ACTION_USER + 1;
  static constexpr int kNone = -1;

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

  void activate(int column, int index);
  void dealStock();
  void pick(int column, int index);
  void clearPick();
  void afterMove();
  void showHint();
  void runBar(int slot);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;

  int columnX(int column) const;
  int cardY(int column, int index) const;
  int cardAt(int column, int y) const;
  void drawColumn(int column) const;
  void drawTop() const;

  spider::Game game;
  cards::Best best;
  int pickColumn = kNone;
  int pickIndex = kNone;
  int hintPlace = kNone;  // a column or spider::STOCK
  bool noMoves = false;
  bool stockBlocked = false;
  bool newBest = false;
  bool bestPending = false;
  bool solvableChoice = true;
  int focus = kNone;  // key focus: a column, spider::STOCK, or kBarFocus + slot
};
