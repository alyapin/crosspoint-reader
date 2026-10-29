#pragma once

#include <Hangman.h>

#include <memory>

#include "GameScreen.h"

// Games list hook for the "Виселица" row, like minesweeperListStatus()/openMinesweeper().
const char* hangmanListStatus();
std::unique_ptr<Activity> openHangman(GfxRenderer& renderer, MappedInputManager& mappedInput);

// The gallows with the word's definition beside it, the word as letter slots,
// and a 33-key keyboard: a tap (or Confirm on the key cursor) guesses. Подсказка
// opens a letter for a mistake; Меню holds Новая игра and Завершить, and once
// the round is over the slot starts the next word.
class HangmanActivity final : public GameScreen {
 public:
  HangmanActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 private:
  enum class Action : uint8_t { Hint, None, Menu };
  struct Layout {
    int gallowsTop;
    int gallowsBottom;
    int wordBaseline;  // the underlines
    int keysTop;
  };

  // The saved round, or a fresh one: the game has no setup to pick.
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

  void deal();
  void runAction(Action action);
  void guessKey(int key);
  bool barShown(int slot) const;
  int nextBarSlot(int from, int step) const;
  Layout layout() const;
  void drawGallows(int top, int bottom) const;
  void drawClue(int left, int top) const;
  void drawWord(int baseline) const;
  void drawKey(int key, int top) const;

  hangman::Game game;
  int cursor = -1;  // key focus: keyboard key, -1 hidden
  int barFocus = -1;
};
