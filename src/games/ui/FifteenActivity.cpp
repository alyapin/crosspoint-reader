#include "FifteenActivity.h"

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

namespace fui = freeink::ui;
using namespace setka;

namespace {
// 104 px tiles with 8 px gaps: 440 px, the width inside the side padding.
constexpr int kTile = 104;
constexpr int kTileGap = 8;
constexpr int kTileRule = 2;
constexpr int kFrameInset = 4;
constexpr int kBoard = fifteen::SIDE * kTile + (fifteen::SIDE - 1) * kTileGap;
constexpr int kResultTop = GameScreen::BOARD_TOP + kBoard + 36;
constexpr int kResultGap = 10;
constexpr int kLastSlot = GameScreen::BAR_SLOTS - 1;

// "37 ходов": Russian picks the form by the number; English uses the last two the same.
void movesText(const int n, char* out, const size_t cap) {
  const int mod10 = n % 10;
  const int mod100 = n % 100;
  StrId id = StrId::STR_FIFTEEN_MOVES_MANY;
  if (mod10 == 1 && mod100 != 11) {
    id = StrId::STR_FIFTEEN_MOVES_ONE;
  } else if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) {
    id = StrId::STR_FIFTEEN_MOVES_FEW;
  }
  snprintf(out, cap, I18N.get(id), n);
}
}  // namespace

const char* fifteenListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Fifteen) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openFifteen(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<FifteenActivity>(renderer, mappedInput);
}

FifteenActivity::FifteenActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("Fifteen", StrId::STR_FIFTEEN, renderer, mappedInput), game(&GameScreen::nowMs) {}

// Without a save a new game starts right away.
bool FifteenActivity::restore() {
  gamesave::load(best);
  if (gamesave::load(game)) {
    cursor = mappedInput.hasTouch() ? -1 : game.gap();
  } else {
    startGame();
  }
  return true;
}

void FifteenActivity::saveGame() {
  gamesave::save(game);
  if (bestPending) {
    bestPending = false;
    gamesave::save(best);
  }
}

void FifteenActivity::clearSave() { gamesave::clear(gamesave::Kind::Fifteen); }

void FifteenActivity::startGame() {
  game.newGame(esp_random());
  newBest = false;
  barFocus = -1;
  cursor = mappedInput.hasTouch() ? -1 : game.gap();
}

void FifteenActivity::openNewGame() {
  startGame();
  game.resume();
  markChanged();
}

// --- input -------------------------------------------------------------------

void FifteenActivity::slideAt(const int cell) {
  if (!game.slide(cell)) return;
  if (game.isSolved()) {
    newBest = best.record(game.moveCount(), game.elapsedMs());
    if (newBest) bestPending = true;
    if (cursor >= 0) barFocus = kLastSlot;
  }
  markChanged();
}

void FifteenActivity::runBar() {
  if (game.isSolved()) {
    openNewGame();
  } else {
    openGameMenu();
  }
}

void FifteenActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  switch (event.action) {
    case ACTION_BOARD:
      if (const int cell = cellAt(x, y); cell >= 0) slideAt(cell);
      break;
    case ACTION_BAR:
      if (event.value == kLastSlot) runBar();
      break;
    default:
      break;
  }
}

void FifteenActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int before[] = {cursor, barFocus};
  if (barFocus >= 0) {
    if (confirm) {
      runBar();
    } else if (dy < 0 && !game.isSolved()) {
      barFocus = -1;
    }
  } else if (game.isSolved()) {
    barFocus = kLastSlot;
  } else if (cursor < 0) {
    cursor = game.gap();
  } else {
    const int x = cursor % fifteen::SIDE;
    const int y = cursor / fifteen::SIDE;
    if (confirm) {
      slideAt(cursor);
    } else if (dx != 0) {
      cursor = y * fifteen::SIDE + (x + dx + fifteen::SIDE) % fifteen::SIDE;
    } else if (dy < 0 && y > 0) {
      cursor -= fifteen::SIDE;
    } else if (dy > 0 && y < fifteen::SIDE - 1) {
      cursor += fifteen::SIDE;
    } else if (dy > 0) {
      barFocus = kLastSlot;
    }
  }
  const int after[] = {cursor, barFocus};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

int32_t FifteenActivity::shownMinutes() const { return games::ui::shownMinute(!game.isSolved(), game.elapsedMs()); }

// GameScreen::loop() repaints the timer when its minute turns; a stopped clock never turns.
uint32_t FifteenActivity::idleSleepBudgetMs() const {
  return hasGame && !game.paused() ? games::ui::msUntilNextMinute(game.elapsedMs()) : light_sleep::kNoDeadline;
}

bool FifteenActivity::onBack() {
  if (barFocus < 0) return false;
  barFocus = -1;
  requestRepaint(RepaintUpdate);
  return true;
}

int FifteenActivity::boardLeft() const { return (renderer.getScreenWidth() - kBoard) / 2; }

int FifteenActivity::cellAt(const int x, const int y) const {
  const int column = (x - boardLeft()) / (kTile + kTileGap);
  const int row = (y - BOARD_TOP) / (kTile + kTileGap);
  if (x < boardLeft() || y < BOARD_TOP || column >= fifteen::SIDE || row >= fifteen::SIDE) return -1;
  return row * fifteen::SIDE + column;
}

// --- render task -------------------------------------------------------------

void FifteenActivity::build(UiScreen& screen) {
  const int width = renderer.getScreenWidth();
  char clock[24];
  formatElapsed(game.elapsedMs(), clock, sizeof(clock));
  char moves[24];
  movesText(game.moveCount(), moves, sizeof(moves));
  char value[56];
  snprintf(value, sizeof(value), "%s \xC2\xB7 %s", moves, clock);
  drawTopLine(screen, nullptr, value, false);

  const int left = boardLeft();
  for (int cell = 0; cell < fifteen::CELLS; ++cell) drawTile(cell, left);
  screen.frame().hit(fui::makeRect(left, BOARD_TOP, kBoard, kBoard), ACTION_BOARD, 0, fui::InputTouch);

  if (game.isSolved()) {
    char line[80];
    snprintf(line, sizeof(line), tr(STR_FIFTEEN_SOLVED), moves, clock);
    drawCentered(kText12Font, 0, width, kResultTop, line, kSemiBold);
    if (newBest) {
      snprintf(line, sizeof(line), "%s", tr(STR_FIFTEEN_NEW_BEST));
    } else {
      movesText(best.moves, moves, sizeof(moves));
      formatElapsed(best.ms, clock, sizeof(clock));
      snprintf(line, sizeof(line), tr(STR_FIFTEEN_BEST), moves, clock);
    }
    drawCentered(kText10Font, 0, width, kResultTop + renderer.getLineHeight(kText12Font) + kResultGap, line);
  }

  BarItem items[BAR_SLOTS];
  items[kLastSlot] = game.isSolved() ? BarItem{&icon_bar_new_game_24, tr(STR_GAMES_NEW_GAME), false, BarItem::NewGame}
                                     : BarItem{&icon_menu_24, tr(STR_GAMES_MENU), false, BarItem::Menu};
  drawBar(screen, items, barFocus);
}

void FifteenActivity::drawTile(const int cell, const int left) const {
  const int x = left + cell % fifteen::SIDE * (kTile + kTileGap);
  const int y = BOARD_TOP + cell / fifteen::SIDE * (kTile + kTileGap);
  if (const int tile = game.tile(cell); tile == 0) {
    renderer.fillRectDither(x, y, kTile, kTile, Color::LightGray);
  } else {
    renderer.drawRect(x, y, kTile, kTile, kTileRule, true);
    char text[4];
    snprintf(text, sizeof(text), "%d", tile);
    const int cap = heroCapHeight(renderer, kDigitsFont);
    renderer.drawText(kDigitsFont, x + (kTile - renderer.getTextWidth(kDigitsFont, text, kSemiBold)) / 2,
                      y + (kTile + cap) / 2 - renderer.getFontAscenderSize(kDigitsFont), text, true, kSemiBold);
  }
  if (cell == cursor && barFocus < 0 && !game.isSolved()) {
    drawCellFrame(renderer, x + kFrameInset, y + kFrameInset, kTile - 2 * kFrameInset, kTile - 2 * kFrameInset);
  }
}
