#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ActionBar.h"
#include "GamePickerActivity.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/setka/SetkaStyle.h"

// The shell of the Sudoku, Minesweeper and Klondike boards (ui-style Setka):
// the shared clock/battery line (setka::drawStatusLine), a top line with "‹ Выйти" and a status on the right,
// the board, and a bottom bar of up to three actions. HALF on entry, cadence
// FAST after each change, and a plain FAST when the shown minute of the timer
// turns; nothing else repaints while the player is idle. The game is saved
// after each change, on exit and before sleep. A screen without a saved game opens
// the new-game picker first; cancelling it leaves.
//
// Keys (the X4 has no touch): the subclass walks its own cursor with
// Left/Right and the side buttons, Confirm acts; Back first drops what the
// subclass holds (a pick, a pad focus), then leaves.
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
  static constexpr int BAR_HEIGHT = games::ui::kActionBarHeight;  // the rule and the action bar, above the button hints

  // Filled triangle without the heap (GfxRenderer::fillPolygon allocates per call).
  static void fillTriangle(const GfxRenderer& renderer, int x0, int y0, int x1, int y1, int x2, int y2);

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
    const freeink::Icon* icon = nullptr;
    const char* label = nullptr;  // nullptr hides the slot
    bool active = false;          // a mode that is on: inset frame
  };

  GameScreen(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);

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
  // Shown play time in whole minutes (ElapsedText.h), or -1 when no timer is shown.
  virtual int32_t shownMinutes() const { return -1; }
  // Board hits carry InputLongPress (event.longPress on a held tap).
  virtual bool wantsLongPress() const { return false; }

  void openPicker(const char* title, const GamePickerActivity::Row* rows, int count, int selected, int toggleRow = -1,
                  bool toggleOn = false);
  // The menu of a game in progress: the game's own rows first (menuChosen()),
  // then Новая игра, or Завершить, which after a confirmation drops the save and leaves.
  void openGameMenu(const GamePickerActivity::Row* own = nullptr, int ownCount = 0);
  virtual void menuChosen(int /*row*/) {}
  void requestRepaint(Repaint kind);
  int barTop() const;
  uint16_t shellInput() const;
  void markChanged() {
    savePending = true;
    requestRepaint(RepaintUpdate);
  }
  void leave();

  // `focused` frames the status for key focus.
  void drawTopLine(UiScreen& screen, const char* label, const char* value, bool tappable, bool focused = false) const;
  void drawBar(UiScreen& screen, const BarItem (&items)[BAR_SLOTS], int focused) const;
  void drawCentered(int font, int x, int width, int y, const char* text,
                    EpdFontFamily::Style style = EpdFontFamily::REGULAR) const;
  // Key cursor: corner brackets inside the box.
  void drawCursor(int x, int y, int width, int height) const;
  // "4 мин" / "<1 мин".
  static void formatElapsed(uint32_t ms, char* out, size_t cap);
  // The games' clock source.
  static uint32_t nowMs();

  bool hasGame = false;

 private:
  static void screenFn(UiScreen& screen, void* user);
  void confirmFinish();

  setka::RefreshCadence cadence;  // render task only
  std::atomic<uint8_t> repaint{RepaintEntry};
  int32_t clockShown = -1;
  bool savePending = false;
};
