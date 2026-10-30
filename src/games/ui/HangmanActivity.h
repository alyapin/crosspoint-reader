#pragma once

#include <Hangman.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "Виселица" row, like minesweeperListStatus()/openMinesweeper().
const char* hangmanListStatus();
std::unique_ptr<Activity> openHangman(GfxRenderer& renderer, MappedInputManager& mappedInput);

// The gallows with the word's definition beside it, the word as letter slots,
// and the word games' keyboard (LetterKeyboard.h): a tap (or Confirm on the key cursor) guesses. Подсказка
// opens a letter for a mistake; Меню holds Новая игра and Завершить, and once
// the round is over the middle slot opens Новая игра: Обычные слова, or С крепкими
// словами, which mixes the rude words in. The last pick is remembered.
class HangmanActivity final : public GameScreen {
 public:
  HangmanActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  enum class Action : uint8_t { Hint, NewGame, Menu };
  struct Layout {
    int gallowsTop;
    int gallowsBottom;
    int wordBaseline;  // the underlines
    int keysTop;
  };

  bool restore() override;
  bool isOver() const override { return game.isOver(); }
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

  void runAction(Action action);
  void guessKey(int key);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;
  Layout layout() const;
  void drawGallows(int top, int bottom) const;
  void drawClue(int left, int top) const;
  void drawWord(int baseline) const;

  hangman::Game game;
  int cursor = -1;  // key focus: keyboard key, -1 hidden
  int barFocus = -1;
  uint8_t chosenList = 0;
  bool listPending = false;  // chosenList to write on the next save
};
