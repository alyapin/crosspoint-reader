#pragma once

#include <FiveLetters.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "5 букв" row, like sudokuListStatus()/openSudoku().
const char* fiveLettersListStatus();
std::unique_ptr<Activity> openFiveLetters(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Six rows of five cells over the word games' keyboard (LetterKeyboard.h): a placed letter is a black
// cell, a present one a thick frame, an absent one a thin frame and a
// regular-weight letter; keys carry the same marks, absent keys turn grey and stop typing. Ё types Е.
// The bar holds Стереть, Готово (sends the row) and Меню (Новая игра once over). A game opens at once,
// without a picker; dropping it after a guess (Новая игра, Завершить) counts as a loss.
class FiveLettersActivity final : public GameScreen {
 public:
  FiveLettersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  static constexpr int ERASE_SLOT = 0;
  static constexpr int DONE_SLOT = 1;
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

  void type(int key);
  void submit();
  void runSlot(int slot);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;
  void placeCursor();
  int cellSize() const;
  int keyboardTop() const;
  void drawCell(int row, int column, int x, int y, int size) const;
  void drawLetter(uint8_t letter, int x, int y, int width, int height, int capHeight, EpdFontFamily::Style style,
                  bool black) const;

  fiveletters::Game game;
  fiveletters::Stats stats;
  bool statsPending = false;
  bool unknownWord = false;  // the last row sent is not in the list; shown until the next input
  int keyCursor = -1;        // key focus on the keyboard, -1 hidden
  int barFocus = -1;
};
