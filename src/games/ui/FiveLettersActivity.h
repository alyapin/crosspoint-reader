#pragma once

#include <FiveLetters.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "5 букв" row, like sudokuListStatus()/openSudoku().
const char* fiveLettersListStatus();
std::unique_ptr<Activity> openFiveLetters(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Six rows of five cells over a ЙЦУКЕН keyboard: a placed letter is a black
// cell, a present one a thick frame, an absent one a thin frame and a
// regular-weight letter; keys carry the same marks, absent keys turn grey and stop typing.
// Меню (Новая игра once over) is the only bar action. A game opens at once,
// without a picker; dropping it after a guess (Новая игра, Завершить) counts as a loss.
class FiveLettersActivity final : public GameScreen {
 public:
  FiveLettersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  static constexpr int MENU_SLOT = 2;

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

  void press(uint8_t key);
  void runMenu();
  void placeCursor();
  void moveRow(int step);
  int keyAt(int row, int x) const;
  void keyBounds(int row, int index, int& x, int& width) const;
  int cellSize() const;
  int keyboardTop() const;
  void drawCell(int row, int column, int x, int y, int size) const;
  void drawKey(UiScreen& screen, int row, int index) const;
  void drawLetter(uint8_t letter, int x, int y, int width, int height, int capHeight, EpdFontFamily::Style style,
                  bool black) const;

  fiveletters::Game game;
  fiveletters::Stats stats;
  bool statsPending = false;
  bool unknownWord = false;  // the last row sent is not in the list; shown until the next input
  int keyRow = -1;           // key focus on the keyboard, -1 hidden
  int keyIndex = 0;
  int barFocus = -1;
};
