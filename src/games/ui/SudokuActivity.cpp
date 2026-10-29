#include "SudokuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "ElapsedText.h"
#include "gameIcons.h"
#include "games/GameSave.h"
#include "games/chess/ui/chessIcons.h"
#include "levelIcons.h"

namespace fui = freeink::ui;
using namespace setka;

namespace {
// 50 px cells, 1 px rules, 3 px rules around the boxes: 468 px, full width.
constexpr int kCell = 50;
constexpr int kThin = 1;
constexpr int kThick = 3;
constexpr int kGridSize = 9 * kCell + 6 * kThin + 4 * kThick;
constexpr int kNoteCell = kCell / 3;
// Digit keys under the grid.
constexpr int kKeyWidth = 48;
constexpr int kKeyHeight = 60;
constexpr int kKeyGap = 4;
constexpr int kKeyRule = 2;
constexpr int kPadGap = 24;
constexpr int kPadTop = GameScreen::BOARD_TOP + kGridSize + kPadGap;
constexpr int kConflictWidth = 20;
constexpr int kConflictHeight = 3;

int cellOffset(const int index) { return kThick * (index / 3 + 1) + kThin * (index - index / 3) + kCell * index; }

int firstOpenCell(const sudoku::Game& game) {
  for (int cell = 0; cell < sudoku::CELLS; ++cell) {
    if (!game.isLocked(cell) && game.value(cell) == 0) return cell;
  }
  return 0;
}

const char* levelStatus(const sudoku::Level level) {
  switch (level) {
    case sudoku::Level::Easy:
      return tr(STR_SUDOKU_EASY_LOWER);
    case sudoku::Level::Medium:
      return tr(STR_SUDOKU_MEDIUM_LOWER);
    case sudoku::Level::Hard:
      return tr(STR_SUDOKU_HARD_LOWER);
    case sudoku::Level::Expert:
      return tr(STR_SUDOKU_EXPERT_LOWER);
  }
  return "";
}
}  // namespace

const char* sudokuListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Sudoku) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openSudoku(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SudokuActivity>(renderer, mappedInput);
}

SudokuActivity::SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("Sudoku", StrId::STR_SUDOKU, renderer, mappedInput), game(&GameScreen::nowMs) {}

bool SudokuActivity::restore() {
  if (!gamesave::load(game)) return false;
  selected = firstOpenCell(game);
  return true;
}

void SudokuActivity::saveGame() { gamesave::save(game); }

void SudokuActivity::clearSave() { gamesave::clear(gamesave::Kind::Sudoku); }

void SudokuActivity::openNewGame() {
  const GamePickerActivity::Row rows[] = {
      {tr(STR_SUDOKU_EASY), &icon_level_1_24},
      {tr(STR_SUDOKU_MEDIUM), &icon_level_2_24},
      {tr(STR_SUDOKU_HARD), &icon_level_3_24},
      {tr(STR_SUDOKU_EXPERT), &icon_level_4_24},
  };
  const int current = hasGame ? static_cast<int>(game.level()) : 1;
  openPicker(tr(STR_GAMES_NEW_GAME), rows, static_cast<int>(std::size(rows)), current);
}

void SudokuActivity::pickerChosen(const int row, bool) {
  const int level = std::clamp(row, 0, sudoku::LEVEL_COUNT - 1);
  game.newGame(static_cast<sudoku::Level>(level), esp_random());
  selected = firstOpenCell(game);
  area = Area::Grid;
  notesMode = false;
}

// --- input -------------------------------------------------------------------

void SudokuActivity::barActions(Action (&out)[SLOTS]) const {
  for (Action& a : out) a = Action::None;
  if (game.isSolved()) {
    out[3] = Action::NewGame;
    return;
  }
  out[0] = Action::Notes;
  const bool erasable = !game.isLocked(selected) && (game.value(selected) != 0 || game.notes(selected) != 0);
  if (erasable) out[1] = Action::Erase;
  if (game.canUndo()) out[2] = Action::Undo;
  out[3] = Action::Menu;
}

void SudokuActivity::runAction(const Action action) {
  switch (action) {
    case Action::Notes:
      notesMode = !notesMode;
      requestRepaint(RepaintUpdate);
      break;
    case Action::Erase:
      if (game.erase(selected)) markChanged();
      break;
    case Action::Undo:
      if (game.undo()) markChanged();
      break;
    case Action::Menu:
      openGameMenu();
      break;
    case Action::NewGame:
      openNewGame();
      break;
    case Action::None:
      break;
  }
}

void SudokuActivity::enterDigit(const int digit) {
  if (game.isLocked(selected)) return;
  const bool changed =
      notesMode ? game.toggleNote(selected, digit) : game.place(selected, game.value(selected) == digit ? 0 : digit);
  if (changed) markChanged();
}

bool SudokuActivity::padKeyShown(const int digit) const { return game.digitCount(digit) < sudoku::SIZE; }

void SudokuActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  switch (event.action) {
    case ACTION_BOARD:
      if (const int cell = cellAt(x, y); cell >= 0) {
        selected = cell;
        area = Area::Grid;
        requestRepaint(RepaintUpdate);
      }
      break;
    case ACTION_PAD:
      enterDigit(event.value);
      break;
    case ACTION_BAR: {
      Action actions[SLOTS];
      barActions(actions);
      if (event.value >= 0 && event.value < SLOTS) runAction(actions[event.value]);
      break;
    }
    default:
      break;
  }
}

void SudokuActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int before[] = {static_cast<int>(area), selected, padFocus, barFocus, notesMode ? 1 : 0};
  Action actions[SLOTS];
  barActions(actions);
  // Next shown pad key / bar slot from `from` in direction `step`; `from` itself when none.
  auto nextKey = [this](const int from, const int step) {
    for (int i = 1; i <= sudoku::SIZE; ++i) {
      const int k = (from + step * i + sudoku::SIZE * 2) % sudoku::SIZE;
      if (padKeyShown(k + 1)) return k;
    }
    return from;
  };
  auto nextSlot = [&actions](const int from, const int step) {
    for (int i = 1; i <= SLOTS; ++i) {
      const int s = (from + step * i + SLOTS * 2) % SLOTS;
      if (actions[s] != Action::None) return s;
    }
    return from;
  };
  auto toPad = [&](const int key) {
    padFocus = key;
    if (!padKeyShown(padFocus + 1)) padFocus = nextKey(padFocus, 1);
    if (padKeyShown(padFocus + 1)) area = Area::Pad;
  };
  auto toBar = [&] {
    if (!hasBar()) return;
    area = Area::Bar;
    if (actions[barFocus] == Action::None) barFocus = nextSlot(barFocus, 1);
  };

  switch (area) {
    case Area::Grid: {
      const int row = sudoku::rowOf(selected);
      if (confirm) {
        const int value = game.value(selected);
        if (!game.isLocked(selected) && !game.isSolved()) toPad(value != 0 ? value - 1 : padFocus);
      } else if (dx != 0) {
        selected = (selected + dx + sudoku::CELLS) % sudoku::CELLS;
      } else if (dy < 0 && row > 0) {
        selected -= sudoku::SIZE;
      } else if (dy > 0 && row < sudoku::SIZE - 1) {
        selected += sudoku::SIZE;
      } else if (dy > 0) {
        if (game.isSolved()) {
          toBar();
        } else {
          toPad(sudoku::columnOf(selected));
          if (area != Area::Pad) toBar();
        }
      }
      break;
    }
    case Area::Pad:
      if (confirm) {
        if (padKeyShown(padFocus + 1)) enterDigit(padFocus + 1);
        area = Area::Grid;
      } else if (dx != 0) {
        padFocus = nextKey(padFocus, dx);
      } else if (dy != 0 && !hasBar()) {
        // Button boards: Up/Down step three keys with wrap, so any digit is two presses away; Back leaves.
        padFocus = (padFocus + 3 * dy + sudoku::SIZE) % sudoku::SIZE;
        if (!padKeyShown(padFocus + 1)) padFocus = nextKey(padFocus, dy);
      } else if (dy < 0) {
        area = Area::Grid;
      } else if (dy > 0) {
        toBar();
      }
      break;
    case Area::Bar:
      if (confirm) {
        runAction(actions[barFocus]);
        // The action may hide its own slot (Стереть on a cleared cell, the last undo).
        barActions(actions);
        if (area == Area::Bar && actions[barFocus] == Action::None) barFocus = nextSlot(barFocus, 1);
      } else if (dx != 0) {
        barFocus = nextSlot(barFocus, dx);
      } else if (dy < 0) {
        area = Area::Grid;
        if (!game.isSolved()) toPad(padFocus);
      }
      break;
  }
  const int after[] = {static_cast<int>(area), selected, padFocus, barFocus, notesMode ? 1 : 0};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

int32_t SudokuActivity::shownMinutes() const { return games::ui::shownMinute(!game.isSolved(), game.elapsedMs()); }

// GameScreen::loop() repaints the timer when its minute turns; a stopped clock never turns.
uint32_t SudokuActivity::idleSleepBudgetMs() const {
  return hasGame && !game.paused() ? games::ui::msUntilNextMinute(game.elapsedMs()) : light_sleep::kNoDeadline;
}

const char* SudokuActivity::confirmHint() const {
  if (area == Area::Bar) return nullptr;
  if (game.isLocked(selected)) return "";
  // Verbs, as the other boards' Confirm hints; «Пометить» does not fit the 72 px label.
  return notesMode ? tr(STR_SUDOKU_MARK_KEY) : tr(STR_SUDOKU_WRITE_KEY);
}

bool SudokuActivity::onBack() {
  if (area == Area::Grid) return false;
  area = Area::Grid;
  requestRepaint(RepaintUpdate);
  return true;
}

int SudokuActivity::cellAt(const int x, const int y) const {
  const int left = (renderer.getScreenWidth() - kGridSize) / 2;
  int column = -1;
  int row = -1;
  for (int i = 0; i < sudoku::SIZE; ++i) {
    if (x >= left + cellOffset(i) && x < left + cellOffset(i) + kCell) column = i;
    if (y >= BOARD_TOP + cellOffset(i) && y < BOARD_TOP + cellOffset(i) + kCell) row = i;
  }
  return column >= 0 && row >= 0 ? sudoku::cellAt(row, column) : -1;
}

// --- render task -------------------------------------------------------------

void SudokuActivity::build(UiScreen& screen) {
  char clock[24];
  formatElapsed(game.elapsedMs(), clock, sizeof(clock));
  drawTopLine(screen, {{levelStatus(game.level()), nullptr}, {nullptr, clock}}, false);

  drawGrid();
  const int left = (renderer.getScreenWidth() - kGridSize) / 2;
  screen.frame().hit(fui::makeRect(left, BOARD_TOP, kGridSize, kGridSize), ACTION_BOARD, 0, fui::InputTouch);

  if (game.isSolved()) {
    char line[48];
    snprintf(line, sizeof(line), tr(STR_SUDOKU_SOLVED_IN), clock);
    drawResult(kPadTop, line);
  } else {
    drawPad(screen);
  }

  Action actions[SLOTS];
  barActions(actions);
  BarItem items[SLOTS];
  for (int i = 0; i < SLOTS; ++i) {
    switch (actions[i]) {
      case Action::Notes:
        items[i] = {&icon_bar_notes_24, tr(STR_SUDOKU_NOTES), notesMode, BarItem::Mode};
        break;
      case Action::Erase:
        items[i] = {&icon_bar_erase_24, tr(STR_SUDOKU_ERASE), false};
        break;
      case Action::Undo:
        items[i] = {&icon_bar_undo_24, tr(STR_GAMES_UNDO), false, BarItem::Repeat};
        break;
      case Action::Menu:
        items[i] = {&icon_menu_24, tr(STR_GAMES_MENU), false, BarItem::Menu};
        break;
      case Action::NewGame:
        items[i] = {&icon_bar_new_game_24, tr(STR_GAMES_NEW_GAME), false, BarItem::NewGame};
        break;
      case Action::None:
        break;
    }
  }
  drawBar(screen, items, area == Area::Bar ? barFocus : -1);
}

void SudokuActivity::drawGrid() const {
  const int left = (renderer.getScreenWidth() - kGridSize) / 2;
  const int lineHeight = renderer.getLineHeight(kText12Font);
  const int noteHeight = renderer.getLineHeight(kDenseFont);
  renderer.fillRect(left, BOARD_TOP, kGridSize, kGridSize, true);
  for (int cell = 0; cell < sudoku::CELLS; ++cell) {
    const int x = left + cellOffset(sudoku::columnOf(cell));
    const int y = BOARD_TOP + cellOffset(sudoku::rowOf(cell));
    renderer.fillRect(x, y, kCell, kCell, false);
    const int value = game.value(cell);
    if (value != 0) {
      const char digit[2] = {static_cast<char>('0' + value), '\0'};
      // Fixed values SemiBold, the player's own Regular.
      drawCentered(kText12Font, x, kCell, y + (kCell - lineHeight) / 2, digit,
                   game.isLocked(cell) ? kSemiBold : EpdFontFamily::REGULAR);
      if (!game.isLocked(cell) && game.hasConflict(cell)) {
        renderer.fillRect(x + (kCell - kConflictWidth) / 2, y + kCell - 2 * kCellFrameWidth - kConflictHeight,
                          kConflictWidth, kConflictHeight, true);
      }
    } else if (const sudoku::Mask notes = game.visibleNotes(cell); notes != 0) {
      for (int d = 1; d <= sudoku::SIZE; ++d) {
        if (!(notes & sudoku::bitFor(d))) continue;
        const char digit[2] = {static_cast<char>('0' + d), '\0'};
        drawCentered(kDenseFont, x + 1 + (d - 1) % 3 * kNoteCell, kNoteCell,
                     y + 1 + (d - 1) / 3 * kNoteCell + (kNoteCell - noteHeight) / 2, digit);
      }
    }
    // Keys on the pad or the bar: the cell waits for its digit. A solved grid shows neither.
    if (cell == selected && !game.isSolved()) {
      if (area != Area::Grid) {
        drawPick(x, y, kCell, kCell);
      } else {
        drawCellFrame(renderer, x, y, kCell, kCell);
      }
    }
  }
}

void SudokuActivity::drawPad(UiScreen& screen) const {
  const int padWidth = sudoku::SIZE * kKeyWidth + (sudoku::SIZE - 1) * kKeyGap;
  const int left = (renderer.getScreenWidth() - padWidth) / 2;
  const int lineHeight = renderer.getLineHeight(kText12Font);
  for (int k = 0; k < sudoku::SIZE; ++k) {
    // A digit that is on the board nine times has no key.
    if (!padKeyShown(k + 1)) continue;
    const int x = left + k * (kKeyWidth + kKeyGap);
    renderer.drawRect(x, kPadTop, kKeyWidth, kKeyHeight, kKeyRule, true);
    const char digit[2] = {static_cast<char>('1' + k), '\0'};
    drawCentered(kText12Font, x, kKeyWidth, kPadTop + (kKeyHeight - lineHeight) / 2, digit, kSemiBold);
    if (area == Area::Pad && k == padFocus) drawCellFrame(renderer, x, kPadTop, kKeyWidth, kKeyHeight);
    screen.frame().hit(fui::makeRect(x, kPadTop, kKeyWidth, kKeyHeight), ACTION_PAD, static_cast<int16_t>(k + 1),
                       fui::InputTouch);
  }
}
