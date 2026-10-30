#pragma once

#include <Icon.h>
#include <OverGuard.h>
#include <ScanwordGame.h>
#include <ScanwordPuzzle.h>
#include <ScanwordShelf.h>

#include <atomic>
#include <memory>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/setka/SetkaStyle.h"
#include "util/ButtonNavigator.h"

// Games list hook for the "Сканворды" row: "идёт" while a started puzzle is
// unsolved, and the screen itself.
const char* scanwordListStatus();
std::unique_ptr<Activity> openScanwords(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Scanwords in the Setka style: the puzzle list, the board and the action menu
// (the board's Меню on touch boards) are views of one activity, so the puzzle,
// the play state and the list summary are allocated once.
//
// The board follows the approved input model: on touch boards the grid never
// moves; selecting an answer opens an answer strip (clue + letter boxes) and a
// keyboard over the lower rows, which close on "Готово" or after the answer's
// last letter. Without touch the whole grid stays visible, side buttons pick
// the answer, Left/Right the cell, and Confirm opens a letter strip.
//
// The loop task alone mutates the state; whatever the render task reads changes
// under the RenderLock. Play time and the saves run outside it: no view draws them.

class ScanwordActivity final : public Activity, private UiAppHost {
 public:
  ScanwordActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  void prepareForSleep() override;

 private:
  enum class View : uint8_t { List, Board, Menu };
  enum Repaint : uint8_t { RepaintNone, RepaintUpdate, RepaintEntry };
  enum class MenuItem : uint8_t { CheckWord, CheckAll, RevealLetter, RevealWord, Erase, Undo, NewGame, Finish };
  static constexpr uint16_t kMaxPuzzles = 256;
  static constexpr int kMaxMenuItems = 8;

  struct ListItem {
    char id[scanword::ID_MAX + 1];
    uint8_t level;
  };
  enum class Status : uint8_t { New, Started, Solved };

  static void screenFn(UiScreen& screen, void* user);
  void build(UiScreen& screen);
  void buildList(UiScreen& screen);
  void buildMenu(UiScreen& screen);
  void buildBoard(UiScreen& screen);
  void drawTopLine(UiScreen& screen) const;
  void drawGrid(UiScreen& screen, int visibleBottom) const;
  void drawCell(uint16_t cell, int x, int y) const;
  void drawClueCell(uint16_t cell, int x, int y) const;
  void drawArrows(uint16_t cell, int x, int y) const;
  void drawSelection() const;
  void drawKeyboard(UiScreen& screen) const;
  void drawAnswerStrip(UiScreen& screen, int top) const;
  void drawBand(UiScreen& screen) const;
  void drawLetterStrip(int top) const;
  void drawRow(UiScreen& screen, int top, int index, const freeink::Icon* icon, const char* label,
               const char* status) const;
  void drawLetter(uint8_t letter, int x, int y, int width, int height, int capHeight) const;
  void drawWordLine(int top, int height) const;

  void handleAction(const freeink::ui::ActionEvent& event, int x, int y);
  void handleListKeys();
  void handleBoardKeys();
  void handleMenuKeys();
  void goBack();
  void setView(View next);
  void requestRepaint(Repaint kind);

  void loadList();
  int menuItems(MenuItem* out) const;
  bool wordHasEntry() const;
  void sortList();
  Status statusOf(const ListItem& item) const;
  int rowsPerPage() const;
  void openPuzzle(uint16_t item);
  void closePuzzle();
  void runMenu(MenuItem item);
  void confirmFinish();
  void tapGrid(int x, int y);
  void typeLetter(uint8_t letter);
  void afterEntry(scanword::EntryResult result, bool wordWasLast);
  void showCheck(uint16_t wrong, bool wholeGrid);
  void closeInput();
  void tickTime();
  void saveProgress();

  bool touchBoard() const;
  int cellWidth() const;
  int cellHeight() const;
  // Bottom of the content, above the button hints on button boards.
  int bottom() const;
  int gridLeft() const;
  int gridTop() const;
  int gridBottom() const;
  int keyboardTop() const;
  uint16_t cellAt(int x, int y) const;
  void cellOrigin(uint16_t cell, int& x, int& y) const;

  std::unique_ptr<scanword::Puzzle> puzzle;
  std::unique_ptr<scanword::Game> game;
  std::unique_ptr<scanword::Shelf> shelf;
  std::unique_ptr<ListItem[]> items;
  std::unique_ptr<uint16_t[]> order;  // items in list order: started, new, solved
  scanword::Stats stats;
  uint16_t itemCount = 0;
  bool loadFailed = false;    // the last puzzle tapped did not open
  bool shelfWritable = true;  // false after a read error: keep the file as it is
  bool statsWritable = true;

  View view = View::List;
  int focus = 0;      // list row or menu item
  int openItem = -1;  // index into items of the puzzle on the board
  bool keyboardOpen = false;
  bool stripOpen = false;    // buttons: the letter strip
  uint8_t pickLetter = 1;    // buttons: letter under the strip cursor
  int16_t checkResult = -1;  // wrong letters after the last check, shown once; -1 none
  bool checkedGrid = false;  // checkResult counts the whole grid, not the answer
  uint32_t lastTickMs = 0;
  uint32_t carryMs = 0;
  bool savePending = false;
  bool dirty = false;           // letters or marks changed since the last save
  bool solveUncounted = false;  // solved, the save that records it not yet written
  bool shelfPending = false;
  bool statsPending = false;
  uint32_t savedSeconds = 0;

  ButtonNavigator buttonNavigator;
  games::OverGuard overGuard;     // the open puzzle's solve; loop task only
  setka::RefreshCadence cadence;  // render task only
  std::atomic<uint8_t> repaint{RepaintEntry};
};
