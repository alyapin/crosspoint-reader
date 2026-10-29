#pragma once

#include <I18n.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "ActionBar.h"
#include "BoardKeys.h"
#include "GamePickerActivity.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/setka/SetkaStyle.h"

// The shell of the Sudoku, Minesweeper and Klondike boards (ui-style Setka):
// the shared clock/battery line (setka::drawStatusLine), a top line with "‹ Выйти" and a status on the right,
// the board, and a bottom bar of up to three actions (four for Судоку); button boards keep only the status
// and the board, with a mode that is on at the left of the top line.
// HALF on entry, cadence
// FAST after each change, and a plain FAST when the shown minute of the timer
// turns; nothing else repaints while the player is idle. The game is saved
// after each change, on exit and before sleep. A screen without a saved game opens
// the new-game picker first; cancelling it leaves.
//
// Keys (the X4 has no touch): the subclass walks its own cursor with
// Left/Right and the side buttons, Confirm acts; Back first drops what the
// subclass holds (a pick, a pad focus), then leaves, or on button boards opens
// the game menu (openGameMenu). Once a game is over there, Confirm runs the bar's Новая игра.
//
// The loop task alone mutates the game and the view state, under the
// RenderLock; the render task only reads them.
class GameScreen : public Activity, protected UiAppHost {
 public:
  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
  void loop() override;
  void render(RenderLock&&) override;

  static constexpr int TOP_LINE_Y = setka::kGameTopLineY;
  static constexpr int BOARD_TOP = setka::kGameBoardTop;
  static constexpr int BAR_SLOTS = 3;
  static constexpr int MAX_BAR_SLOTS = 4;
  static constexpr int BAR_HEIGHT = games::ui::kActionBarHeight;  // the rule and the action bar, above the button hints

  // Filled triangle without the heap (GfxRenderer::fillPolygon allocates per call).
  static void fillTriangle(const GfxRenderer& renderer, int x0, int y0, int x1, int y1, int x2, int y2,
                           bool black = true);

 protected:
  // Pending repaint, strongest wins: Entry = HALF, Update = cadence FAST,
  // Clock = plain FAST that does not count toward the cadence.
  enum Repaint : uint8_t { RepaintNone, RepaintClock, RepaintUpdate, RepaintEntry };

  static constexpr freeink::ui::ActionId ACTION_EXIT = 1;
  static constexpr freeink::ui::ActionId ACTION_STATUS = 2;  // right side of the top line
  static constexpr freeink::ui::ActionId ACTION_BOARD = 3;
  static constexpr freeink::ui::ActionId ACTION_BAR = 4;  // value = bar slot
  static constexpr freeink::ui::ActionId ACTION_USER = 5;

  struct BarItem {
    // Mode: a toggle; Repeat: a harmless action worth repeating (undo, hint, flip), which the game menu
    // reopens on; Menu, NewGame (the game is over): the slot the game menu leaves out.
    enum Kind : uint8_t { Action, Mode, Repeat, Menu, NewGame };
    const freeink::Icon* icon = nullptr;
    const char* label = nullptr;  // nullptr hides the slot
    bool active = false;          // a mode that is on: inset frame
    Kind kind = Action;
  };

  GameScreen(const char* name, StrId title, GfxRenderer& renderer, MappedInputManager& mappedInput);

  // Restores the saved game; false when there is none.
  virtual bool restore() = 0;
  virtual void saveGame() = 0;
  virtual void pauseGame() = 0;
  virtual void resumeGame() = 0;
  // Opens the picker for a new game (see openPicker).
  virtual void openNewGame() = 0;
  // Removes the saved game.
  virtual void clearSave() = 0;
  virtual void pickerChosen(int row, bool toggle) = 0;
  virtual void build(UiScreen& screen) = 0;
  virtual void onTouch(const freeink::ui::ActionEvent& event, int x, int y) = 0;
  virtual void onKey(int dx, int dy, bool confirm) = 0;
  // Back with something to drop first; true when consumed.
  virtual bool onBack() { return false; }
  // Button boards: whether a held Confirm means something now; it then runs onConfirmHold() instead of onKey().
  virtual bool holdsConfirm() const { return false; }
  virtual void onConfirmHold() {}
  // Shown play time in whole minutes (ElapsedText.h), or -1 when no timer is shown.
  virtual int32_t shownMinutes() const { return -1; }
  // Board hits carry InputLongPress (event.longPress on a held tap).
  virtual bool wantsLongPress() const { return false; }
  // The game's own menu rows (at most MENU_ROWS) and the choice of one, by its
  // index there: after the bar's actions, before Новая игра.
  static constexpr int MENU_ROWS = 3;
  virtual int menuRows(GamePickerActivity::Row (& /*rows*/)[MENU_ROWS]) const { return 0; }
  virtual void menuChosen(int /*row*/) {}
  // A game against an opponent ends with Сдаться (resign) where a solo game drops its save and leaves.
  virtual bool resigns() const { return false; }
  virtual void resign() {}
  // Button hints while the game runs, naming what Back and Confirm do now: nullptr keeps Меню and Выбрать, "" hides.
  virtual const char* backHint() const { return nullptr; }
  virtual const char* confirmHint() const { return nullptr; }

  void openPicker(const char* title, const GamePickerActivity::Row* rows, int count, int selected, int toggleRow = -1,
                  bool toggleOn = false);
  // The game menu, titled with the game: on button boards Продолжить and the bar's
  // actions first; the game's own rows, Новая игра, then while the game runs
  // Завершить (or Сдаться), which asks first; button boards end with Выйти.
  void openGameMenu();
  void requestRepaint(Repaint kind);
  // Touch boards: "‹ Выйти" and the action bar; button boards play by keys alone.
  bool hasBar() const;
  int barTop() const;
  uint16_t shellInput() const;
  void markChanged() {
    savePending = true;
    requestRepaint(RepaintUpdate);
  }
  void leave();

  // The status parts as games::ui::drawStatus; `focused` frames the status for key focus.
  void drawTopLine(UiScreen& screen, std::initializer_list<games::ui::StatusItem> status, bool tappable,
                   bool focused = false) const;
  template <size_t N>
  void drawBar(UiScreen& screen, const BarItem (&items)[N], const int focused) const {
    static_assert(N <= MAX_BAR_SLOTS, "the bar holds four slots at most");
    drawBar(screen, items, static_cast<int>(N), focused);
  }
  void drawBar(UiScreen& screen, const BarItem* items, int count, int focused) const;
  // A finished game's result under the board (games::ui::drawResult), with the
  // Confirm cue on button boards; returns the y below it.
  int drawResult(int top, const char* result, const char* detail = nullptr) const;
  int resultHeight(bool detail) const;
  void drawCentered(int font, int x, int width, int y, const char* text,
                    EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  // The key cursor is the 4 px inset frame (drawCellFrame) on a cell, a card or a pad key. What is picked and
  // waits for its target (a piece, a run of cards, the Sudoku cell while the keys are on the pad) takes corner
  // brackets inside the box; under the cursor's frame they step inside it.
  void drawPick(int x, int y, int width, int height, bool underCursor = false) const;
  // Key cursor on a keyboard key, which may carry a 4 px frame of its own (5 букв): corner brackets just
  // outside it.
  void drawKeyCursor(int x, int y, int width, int height) const;
  // "4 мин" / "<1 мин".
  static void formatElapsed(uint32_t ms, char* out, size_t cap);
  // The games' clock source.
  static uint32_t nowMs();

  bool hasGame = false;

 private:
  static void screenFn(UiScreen& screen, void* user);
  void confirmFinish();
  bool applyKeys();
  void drawMode(UiScreen& screen, const BarItem* items, int count) const;
  void addMenuRow(GamePickerActivity::Row* rows, int& count, int8_t id, const GamePickerActivity::Row& row);
  void addOwnRows(GamePickerActivity::Row* rows, int& count);
  void showMenu(const char* title, const GamePickerActivity::Row* rows, int count);
  void runBarSlot(int slot);
  int newGameSlot() const;

  StrId title;
  mutable BarItem shownBar[MAX_BAR_SLOTS];  // the last drawBar(), for the game menu
  mutable int shownCount = 0;
  int8_t menuIds[GamePickerActivity::MAX_ROWS] = {};  // the open menu's rows (GameScreen.cpp MenuRow)
  int8_t lastMenuRow = -1;                            // the last mode or Repeat action chosen there
  setka::RefreshCadence cadence;                      // render task only
  std::atomic<uint8_t> repaint{RepaintEntry};
  games::ui::BoardKeys keys;  // loop task only
  int32_t clockShown = -1;
  bool savePending = false;
};
