#include "TicTacToeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#include "gameIcons.h"
#include "games/GameSave.h"
#include "games/chess/ui/chessIcons.h"
#include "levelIcons.h"

namespace fui = freeink::ui;
using namespace setka;
using tictactoe::Mark;
using tictactoe::Status;

namespace {
constexpr int kBoardTop = GameScreen::BOARD_TOP + 16;
constexpr int kMaxCell = 140;
constexpr int kGridLine = 4;
constexpr int kStroke = 8;
constexpr int kTextGap = 28;    // between the board and the line under it
constexpr int kTextArea = 100;  // the result and score lines, kept clear of the bar
constexpr int kCenter = 4;
constexpr int kNewGameSlot = 2;

// A stroke `width` px across, swept along the shorter axis; the strokes here are straight or at 45 degrees.
void drawStroke(const GfxRenderer& renderer, const int x0, const int y0, const int x1, const int y1, const int width) {
  const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
  const int sweep = x0 != x1 && y0 != y1 ? width * 17 / 12 : width;
  for (int i = -sweep / 2; i < sweep - sweep / 2; ++i) {
    if (steep) {
      renderer.drawLine(x0 + i, y0, x1 + i, y1, true);
    } else {
      renderer.drawLine(x0, y0 + i, x1, y1 + i, true);
    }
  }
}

int sign(const int v) { return (v > 0) - (v < 0); }
}  // namespace

const char* tictactoeListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::TicTacToe) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openTicTacToe(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TicTacToeActivity>(renderer, mappedInput);
}

TicTacToeActivity::TicTacToeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("TicTacToe", StrId::STR_TICTACTOE, renderer, mappedInput) {}

// A finished game still loads for its score; the screen then opens the picker.
bool TicTacToeActivity::restore() {
  if (!gamesave::load(game) || !game.inProgress()) return false;
  cursor = mappedInput.hasTouch() ? -1 : kCenter;
  return true;
}

void TicTacToeActivity::saveGame() { gamesave::save(game); }

void TicTacToeActivity::clearSave() { gamesave::clear(gamesave::Kind::TicTacToe); }

void TicTacToeActivity::openNewGame() {
  const GamePickerActivity::Row rows[] = {
      {tr(STR_SUDOKU_EASY), &icon_level_1_24},
      {tr(STR_SUDOKU_HARD), &icon_level_3_24},
      {tr(STR_TTT_TWO_PLAYERS), &icon_friend_24},
  };
  openPicker(tr(STR_GAMES_NEW_GAME), rows, static_cast<int>(std::size(rows)), static_cast<int>(game.mode()));
}

void TicTacToeActivity::pickerChosen(const int row, bool) {
  game.newGame(static_cast<tictactoe::Mode>(std::clamp(row, 0, tictactoe::MODE_COUNT - 1)), esp_random());
  barFocus = -1;
  cursor = mappedInput.hasTouch() ? -1 : kCenter;
}

// --- input -------------------------------------------------------------------

void TicTacToeActivity::play(const int cell) {
  if (!game.play(cell)) return;
  markChanged();
}

void TicTacToeActivity::runBar(const int slot) {
  if (slot == 0) {
    if (game.undo()) markChanged();
  } else if (slot == kNewGameSlot) {
    if (game.isOver()) {
      openNewGame();
    } else {
      openGameMenu();
    }
  }
}

bool TicTacToeActivity::barShown(const int slot) const {
  if (slot == 0) return game.canUndo();
  return slot == kNewGameSlot;
}

int TicTacToeActivity::nextBarSlot(const int from, const int step) const {
  for (int i = 1; i <= BAR_SLOTS; ++i) {
    const int slot = (from + step * i + 2 * BAR_SLOTS) % BAR_SLOTS;
    if (barShown(slot)) return slot;
  }
  return from;
}

void TicTacToeActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  if (event.action == ACTION_BOARD) {
    const int size = cellSize();
    const int left = (renderer.getScreenWidth() - 3 * size) / 2;
    const int column = (x - left) / size;
    const int row = (y - kBoardTop) / size;
    if (x >= left && y >= kBoardTop && column < 3 && row < 3) play(row * 3 + column);
  } else if (event.action == ACTION_BAR && barShown(event.value)) {
    runBar(event.value);
  }
}

void TicTacToeActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int before[] = {cursor, barFocus};
  if (barFocus >= 0) {
    if (confirm) {
      runBar(barFocus);
      if (barFocus >= 0 && !barShown(barFocus)) barFocus = nextBarSlot(barFocus, 1);
    } else if (dx != 0) {
      barFocus = nextBarSlot(barFocus, dx);
    } else if (dy < 0) {
      barFocus = -1;
      if (cursor < 0) cursor = kCenter;
    }
  } else if (cursor < 0) {
    cursor = kCenter;
  } else if (confirm) {
    play(cursor);
  } else if (dx != 0) {
    cursor = cursor / 3 * 3 + (cursor % 3 + dx + 3) % 3;
  } else if (dy < 0 && cursor >= 3) {
    cursor -= 3;
  } else if (dy > 0 && cursor < 6) {
    cursor += 3;
  } else if (dy > 0 && hasBar()) {
    barFocus = nextBarSlot(BAR_SLOTS - 1, 1);
  }
  const int after[] = {cursor, barFocus};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

bool TicTacToeActivity::onBack() {
  if (barFocus < 0) return false;
  barFocus = -1;
  requestRepaint(RepaintUpdate);
  return true;
}

int TicTacToeActivity::cellSize() const { return std::min(kMaxCell, (barTop() - kBoardTop - kTextArea) / 3); }

// --- render task -------------------------------------------------------------

void TicTacToeActivity::build(UiScreen& screen) {
  const bool over = game.isOver();
  const char* turn = nullptr;
  if (!over) {
    turn = game.vsComputer()          ? tr(STR_TTT_YOUR_TURN)
           : game.toMove() == Mark::X ? tr(STR_TTT_TURN_X)
                                      : tr(STR_TTT_TURN_O);
  }
  drawTopLine(screen, nullptr, turn, false);

  const int size = cellSize();
  const int side = 3 * size;
  const int left = (renderer.getScreenWidth() - side) / 2;
  for (int i = 1; i < 3; ++i) {
    renderer.fillRect(left + i * size - kGridLine / 2, kBoardTop, kGridLine, side, true);
    renderer.fillRect(left, kBoardTop + i * size - kGridLine / 2, side, kGridLine, true);
  }
  for (int c = 0; c < tictactoe::CELLS; ++c) drawMark(game.at(c), left + c % 3 * size, kBoardTop + c / 3 * size, size);
  if (const int line = game.winLine(); line >= 0) {
    const uint8_t* cells = tictactoe::LINES[line];
    const int x0 = left + cells[0] % 3 * size + size / 2;
    const int y0 = kBoardTop + cells[0] / 3 * size + size / 2;
    const int x1 = left + cells[2] % 3 * size + size / 2;
    const int y1 = kBoardTop + cells[2] / 3 * size + size / 2;
    const int ex = sign(x1 - x0) * size / 3;
    const int ey = sign(y1 - y0) * size / 3;
    drawStroke(renderer, x0 - ex, y0 - ey, x1 + ex, y1 + ey, kStroke);
  }
  if (cursor >= 0 && barFocus < 0) {
    constexpr int kInset = 8;
    drawCellFrame(renderer, left + cursor % 3 * size + kInset, kBoardTop + cursor / 3 * size + kInset,
                  size - 2 * kInset, size - 2 * kInset);
  }
  screen.frame().hit(fui::makeRect(left, kBoardTop, side, side), ACTION_BOARD, 0, shellInput());

  int y = kBoardTop + side + kTextGap;
  if (over) {
    const bool vs = game.vsComputer();
    const char* result = game.status() == Status::Draw   ? tr(STR_TTT_DRAW)
                         : game.status() == Status::XWon ? (vs ? tr(STR_TTT_YOU_WON) : tr(STR_TTT_X_WON))
                                                         : (vs ? tr(STR_TTT_YOU_LOST) : tr(STR_TTT_O_WON));
    renderer.drawText(kText12Font, left, y, result, true, kSemiBold);
    y += renderer.getLineHeight(kText12Font) + 8;
  }
  const tictactoe::Score& score = game.score();
  char text[64];
  snprintf(text, sizeof(text), game.vsComputer() ? tr(STR_TTT_SCORE_VS) : tr(STR_TTT_SCORE_TWO), score.x, score.o,
           score.draws);
  renderer.drawText(kText10Font, left, y, text);

  BarItem items[BAR_SLOTS];
  if (barShown(0)) items[0] = {&icon_bar_undo_24, tr(STR_GAMES_UNDO)};
  items[kNewGameSlot] = {over ? &icon_bar_new_game_24 : &icon_menu_24,
                         over ? tr(STR_GAMES_NEW_GAME) : tr(STR_GAMES_MENU), false,
                         over ? BarItem::NewGame : BarItem::Menu};
  drawBar(screen, items, barFocus);
}

void TicTacToeActivity::drawMark(const Mark mark, const int x, const int y, const int size) const {
  const int margin = size * 2 / 9;
  const int d = size - 2 * margin;
  if (mark == Mark::X) {
    drawStroke(renderer, x + margin, y + margin, x + size - margin, y + size - margin, kStroke);
    drawStroke(renderer, x + size - margin, y + margin, x + margin, y + size - margin, kStroke);
  } else if (mark == Mark::O) {
    renderer.fillRoundedRect(x + margin, y + margin, d, d, d / 2, Color::Black);
    renderer.fillRoundedRect(x + margin + kStroke, y + margin + kStroke, d - 2 * kStroke, d - 2 * kStroke,
                             d / 2 - kStroke, Color::White);
  }
}
