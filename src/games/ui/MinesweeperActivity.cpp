#include "MinesweeperActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "ElapsedText.h"
#include "activities/util/ConfirmationActivity.h"
#include "gameIcons.h"
#include "games/GameSave.h"
#include "games/chess/ui/chessIcons.h"

namespace fui = freeink::ui;
using namespace setka;

namespace {
constexpr int kBoardGap = 8;  // between the board and the bar rule
constexpr minesweeper::Config kFields[] = {minesweeper::BEGINNER, minesweeper::PORTRAIT};

// A pennant on a pole, sized to the cell.
void drawFlag(const GfxRenderer& renderer, const int x, const int y, const int size) {
  const int pole = x + size * 2 / 5;
  const int top = y + size / 5;
  const int bottom = y + size * 4 / 5;
  const int stroke = std::max(2, size / 16);
  renderer.fillRect(pole, top, stroke, bottom - top, true);
  renderer.fillRect(pole - size / 6, bottom - stroke, size / 3 + stroke, stroke, true);
  GameScreen::fillTriangle(renderer, pole + stroke, top, pole + stroke + size * 2 / 5, top + size / 6, pole + stroke,
                           top + size / 3);
}

// A round mine with four spikes, in black or (on the dug mine) white.
void drawMine(const GfxRenderer& renderer, const int x, const int y, const int size, const bool black) {
  const int cx = x + size / 2;
  const int cy = y + size / 2;
  const int r = size / 5;
  const int spike = size * 3 / 10;
  renderer.drawLine(cx - spike, cy, cx + spike, cy, 2, black);
  renderer.drawLine(cx, cy - spike, cx, cy + spike, 2, black);
  renderer.drawLine(cx - spike * 7 / 10, cy - spike * 7 / 10, cx + spike * 7 / 10, cy + spike * 7 / 10, 2, black);
  renderer.drawLine(cx - spike * 7 / 10, cy + spike * 7 / 10, cx + spike * 7 / 10, cy - spike * 7 / 10, 2, black);
  renderer.fillRoundedRect(cx - r, cy - r, 2 * r, 2 * r, r, black ? Color::Black : Color::White);
}
}  // namespace

const char* minesweeperListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Minesweeper) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openMinesweeper(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<MinesweeperActivity>(renderer, mappedInput);
}

MinesweeperActivity::MinesweeperActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("Minesweeper", StrId::STR_MINESWEEPER, renderer, mappedInput), game(&GameScreen::nowMs) {}

bool MinesweeperActivity::restore() {
  if (!gamesave::load(game)) return false;
  cursor = mappedInput.hasTouch() ? -1 : game.rows() / 2 * game.columns() + game.columns() / 2;
  return true;
}

void MinesweeperActivity::saveGame() { gamesave::save(game); }

void MinesweeperActivity::clearSave() { gamesave::clear(gamesave::Kind::Minesweeper); }

void MinesweeperActivity::openNewGame() {
  const GamePickerActivity::Row rows[] = {
      {tr(STR_MINES_SMALL), &icon_grid_small_24},
      {tr(STR_MINES_LARGE), &icon_grid_24},
  };
  int current = 0;
  if (hasGame) {
    for (int i = 0; i < static_cast<int>(std::size(kFields)); ++i) {
      if (kFields[i].columns == game.columns() && kFields[i].rows == game.rows()) current = i;
    }
  }
  openPicker(tr(STR_GAMES_NEW_GAME), rows, static_cast<int>(std::size(rows)), current);
}

void MinesweeperActivity::pickerChosen(const int row, bool) {
  game.newGame(kFields[std::clamp(row, 0, static_cast<int>(std::size(kFields)) - 1)], esp_random());
  flagMode = false;
  barFocus = -1;
  cursor = mappedInput.hasTouch() ? -1 : game.rows() / 2 * game.columns() + game.columns() / 2;
}

// --- input -------------------------------------------------------------------

void MinesweeperActivity::runAction(const Action action) {
  switch (action) {
    case Action::Flag:
      flagMode = !flagMode;
      requestRepaint(RepaintUpdate);
      break;
    case Action::New:
      confirmRestart();
      break;
    case Action::Menu:
      if (game.isOver()) {
        openNewGame();
      } else {
        openGameMenu();
      }
      break;
  }
}

// As Японские: Начать заново asks first.
void MinesweeperActivity::confirmRestart() {
  auto confirm =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_GAMES_RESTART_CONFIRM),
                                              tr(STR_MINES_RESTART_BODY), tr(STR_CANCEL), tr(STR_GAMES_RESTART));
  if (!confirm) {
    LOG_ERR("MINES", "OOM: restart confirmation, the board stays");
    return;
  }
  app.clearTapFlash();
  pauseGame();
  startActivityForResult(std::move(confirm), [this](const ActivityResult& result) {
    RenderLock lock(*this);
    if (!result.isCancelled) {
      game.newGame(game.config(), esp_random());
      flagMode = false;
      markChanged();
    }
    resumeGame();
    requestRepaint(RepaintEntry);
  });
}

void MinesweeperActivity::act(const int x, const int y, const bool flag) {
  if (flag ? game.toggleFlag(x, y) : game.dig(x, y)) markChanged();
}

void MinesweeperActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  switch (event.action) {
    case ACTION_BOARD: {
      const int size = cellSize();
      const int column = (x - boardLeft()) / size;
      const int row = (y - BOARD_TOP) / size;
      if (x >= boardLeft() && y >= BOARD_TOP) act(column, row, flagMode || event.longPress);
      break;
    }
    case ACTION_BAR:
      if (event.value >= 0 && event.value < BAR_SLOTS && barShown(event.value)) {
        runAction(static_cast<Action>(event.value));
      }
      break;
    default:
      break;
  }
}

bool MinesweeperActivity::barShown(const int slot) const {
  // Флажок while playing on touch, Начать заново once there is something to deal again,
  // Меню (Новая игра once over) always.
  if (slot == 0) return hasBar() && !game.isOver();
  if (slot == 1) return game.status() != minesweeper::Status::Ready;
  return slot == 2;
}

int MinesweeperActivity::nextBarSlot(const int from, const int step) const {
  for (int i = 1; i <= BAR_SLOTS; ++i) {
    const int slot = (from + step * i + 2 * BAR_SLOTS) % BAR_SLOTS;
    if (barShown(slot)) return slot;
  }
  return from;
}

void MinesweeperActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int columns = game.columns();
  const int rows = game.rows();
  const int before[] = {cursor, barFocus, flagMode ? 1 : 0};
  if (barFocus >= 0) {
    if (confirm) {
      if (barShown(barFocus)) runAction(static_cast<Action>(barFocus));
      if (barFocus >= 0 && !barShown(barFocus)) barFocus = nextBarSlot(barFocus, 1);
    } else if (dx != 0) {
      barFocus = nextBarSlot(barFocus, dx);
    } else if (dy < 0) {
      barFocus = -1;
    }
  } else if (cursor < 0) {
    cursor = rows / 2 * columns + columns / 2;
  } else {
    const int x = cursor % columns;
    const int y = cursor / columns;
    if (confirm) {
      act(x, y, flagMode);
    } else if (dx != 0) {
      cursor = y * columns + (x + dx + columns) % columns;
    } else if (dy < 0 && y > 0) {
      cursor -= columns;
    } else if (dy > 0 && y < rows - 1) {
      cursor += columns;
    } else if (dy > 0 && hasBar()) {
      barFocus = nextBarSlot(BAR_SLOTS - 1, 1);
    }
  }
  const int after[] = {cursor, barFocus, flagMode ? 1 : 0};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

int32_t MinesweeperActivity::shownMinutes() const { return games::ui::shownMinute(true, game.elapsedMs()); }

// GameScreen::loop() repaints the timer when its minute turns; a stopped clock never turns.
uint32_t MinesweeperActivity::idleSleepBudgetMs() const {
  return hasGame && !game.paused() ? games::ui::msUntilNextMinute(game.elapsedMs()) : light_sleep::kNoDeadline;
}

bool MinesweeperActivity::onBack() {
  if (barFocus < 0) return false;
  barFocus = -1;
  requestRepaint(RepaintUpdate);
  return true;
}

int MinesweeperActivity::cellSize() const {
  const int room = barTop() - kBoardGap - games::ui::kCaptionGap - resultHeight(false) - BOARD_TOP;
  return std::min(renderer.getScreenWidth() / game.columns(), room / game.rows());
}

int MinesweeperActivity::boardLeft() const { return (renderer.getScreenWidth() - cellSize() * game.columns()) / 2; }

// --- render task -------------------------------------------------------------

void MinesweeperActivity::build(UiScreen& screen) {
  char clock[24];
  formatElapsed(game.elapsedMs(), clock, sizeof(clock));
  char mines[12];
  const bool over = game.isOver();
  snprintf(mines, sizeof(mines), "%d", game.minesLeft());
  if (over) {
    drawTopLine(screen, {{nullptr, clock}}, false);
  } else {
    drawTopLine(screen, {{tr(STR_MINES_LEFT), mines}, {nullptr, clock}}, false);
  }

  const int size = cellSize();
  const int left = boardLeft();
  for (int y = 0; y < game.rows(); ++y) {
    for (int x = 0; x < game.columns(); ++x) drawCell(x, y, left, size);
  }
  // 1 px rules between the cells, and the frame.
  const int width = size * game.columns();
  const int height = size * game.rows();
  for (int x = 0; x <= game.columns(); ++x) renderer.fillRect(left + x * size, BOARD_TOP, 1, height + 1, true);
  for (int y = 0; y <= game.rows(); ++y) renderer.fillRect(left, BOARD_TOP + y * size, width + 1, 1, true);
  if (cursor >= 0 && barFocus < 0 && !game.isOver()) {
    drawCellFrame(renderer, left + cursor % game.columns() * size, BOARD_TOP + cursor / game.columns() * size, size + 1,
                  size + 1);
  }
  screen.frame().hit(fui::makeRect(left, BOARD_TOP, width, height), ACTION_BOARD, 0,
                     static_cast<uint16_t>(fui::InputTouch | fui::InputLongPress));
  if (over) {
    drawResult(games::ui::captionTop(renderer, BOARD_TOP + height + 1, kText12Font),
               game.status() == minesweeper::Status::Won ? tr(STR_GAMES_WON) : tr(STR_MINES_LOST));
  } else if (!hasBar()) {
    // Button boards flag with a held Confirm (the bar's Флажок is touch only).
    char line[64];
    snprintf(line, sizeof(line), tr(STR_MINES_FLAG_HOLD), tr(STR_GAMES_DIG));
    renderer.drawText(kText10Font, kSidePadding, games::ui::captionTop(renderer, BOARD_TOP + height + 1, kText10Font),
                      line);
  }

  BarItem items[BAR_SLOTS];
  const char* labels[BAR_SLOTS] = {tr(STR_MINES_FLAG), tr(STR_GAMES_RESTART),
                                   over ? tr(STR_GAMES_NEW_GAME) : tr(STR_GAMES_MENU)};
  const freeink::Icon* icons[BAR_SLOTS] = {&icon_bar_flag_24, &icon_bar_restart_24,
                                           over ? &icon_bar_new_game_24 : &icon_menu_24};
  const BarItem::Kind kinds[BAR_SLOTS] = {BarItem::Mode, BarItem::Action, over ? BarItem::NewGame : BarItem::Menu};
  for (int i = 0; i < BAR_SLOTS; ++i) {
    if (barShown(i)) items[i] = {icons[i], labels[i], i == 0 && flagMode, kinds[i]};
  }
  drawBar(screen, items, barFocus);
}

void MinesweeperActivity::drawCell(const int x, const int y, const int left, const int size) const {
  const int px = left + x * size;
  const int py = BOARD_TOP + y * size;
  switch (game.view(x, y)) {
    case minesweeper::View::Covered:
      renderer.fillRectDither(px, py, size, size, Color::LightGray);
      break;
    case minesweeper::View::Flagged:
      drawFlag(renderer, px, py, size);
      break;
    case minesweeper::View::Open:
      if (const int n = game.adjacent(x, y); n > 0) {
        const char digit[2] = {static_cast<char>('0' + n), '\0'};
        drawCentered(kText12Font, px, size, py + (size - renderer.getLineHeight(kText12Font)) / 2, digit, kSemiBold);
      }
      break;
    case minesweeper::View::Mine:
      drawMine(renderer, px, py, size, true);
      break;
    case minesweeper::View::Exploded:
      renderer.fillRect(px, py, size, size, true);
      drawMine(renderer, px, py, size, false);
      break;
    case minesweeper::View::WrongFlag:
      drawFlag(renderer, px, py, size);
      renderer.drawLine(px + size / 6, py + size / 6, px + size * 5 / 6, py + size * 5 / 6, 3, true);
      renderer.drawLine(px + size / 6, py + size * 5 / 6, px + size * 5 / 6, py + size / 6, 3, true);
      break;
  }
}
