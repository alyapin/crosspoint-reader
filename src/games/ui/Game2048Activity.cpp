#include "Game2048Activity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>

#include "gameIcons.h"
#include "games/GameSave.h"
#include "games/chess/ui/chessIcons.h"

namespace fui = freeink::ui;
using namespace setka;
using g2048::Dir;

namespace {
// Four 104 px tiles with 8 px gaps between the side paddings; the board starts
// below the top-edge swipe band, so a swipe down on it stays a move.
constexpr int kTile = 104;
constexpr int kGap = 8;
constexpr int kGridSize = g2048::SIZE * kTile + (g2048::SIZE - 1) * kGap;
constexpr int kGridTop = GameScreen::BOARD_TOP;
constexpr int kFooterGap = 18;
constexpr int kTileRule = 2;        // as the 15 puzzle's tiles
constexpr uint8_t kThickFrom = 4;   // 16
constexpr uint8_t kFilledFrom = 7;  // 128
constexpr int kMenuSlot = 2;

// "2 340".
void formatScore(const uint32_t value, char* out, const size_t cap) {
  char digits[12];
  const int n = snprintf(digits, sizeof(digits), "%lu", static_cast<unsigned long>(value));
  size_t len = 0;
  for (int i = 0; i < n && len + 2 < cap; ++i) {
    if (i > 0 && (n - i) % 3 == 0) out[len++] = ' ';
    out[len++] = digits[i];
  }
  out[len] = '\0';
}

}  // namespace

const char* game2048ListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Game2048) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openGame2048(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<Game2048Activity>(renderer, mappedInput);
}

Game2048Activity::Game2048Activity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("2048", StrId::STR_2048, renderer, mappedInput) {}

// Without a save a new game starts right away.
bool Game2048Activity::restore() {
  best = gamesave::loadBest2048();
  savedBest = best;
  barFocus = -1;
  if (!gamesave::load(game)) game.newGame(esp_random());
  return true;
}

void Game2048Activity::saveGame() {
  gamesave::save(game);
  if (game.score() > savedBest) {
    savedBest = game.score();
    gamesave::saveBest2048(savedBest);
  }
}

void Game2048Activity::clearSave() { gamesave::clear(gamesave::Kind::Game2048); }

void Game2048Activity::openNewGame() {
  best = std::max(best, game.score());
  game.newGame(esp_random());
  barFocus = -1;
  markChanged();
  requestRepaint(RepaintEntry);
}

// --- input -------------------------------------------------------------------

void Game2048Activity::loop() {
  int y = 0;
  const auto swipe = hasGame ? mappedInput.wasSwipe(nullptr, &y) : MappedInputManager::SwipeDir::None;
  // A swipe on the board is a move, also where the left-edge Back swipe would start: the first column sits there.
  if (swipe != MappedInputManager::SwipeDir::None && y >= BOARD_TOP && y < barTop()) {
    RenderLock lock(*this);
    slide(swipe == MappedInputManager::SwipeDir::Left    ? Dir::Left
          : swipe == MappedInputManager::SwipeDir::Right ? Dir::Right
          : swipe == MappedInputManager::SwipeDir::Up    ? Dir::Up
                                                         : Dir::Down);
    return;
  }
  GameScreen::loop();
}

void Game2048Activity::slide(const Dir dir) {
  if (!game.move(dir)) return;
  markChanged();
}

void Game2048Activity::runBar(const int slot) {
  if (!barShown(slot)) return;
  if (slot == 0 && game.undo()) {
    markChanged();
  } else if (slot == kMenuSlot) {
    if (game.isOver()) {
      openNewGame();
    } else {
      openGameMenu();
    }
  }
}

bool Game2048Activity::barShown(const int slot) const { return slot == 0 ? game.canUndo() : slot == kMenuSlot; }

int Game2048Activity::nextBarSlot(const int from, const int step) const {
  for (int i = 1; i <= BAR_SLOTS; ++i) {
    const int slot = (from + step * i + 2 * BAR_SLOTS) % BAR_SLOTS;
    if (barShown(slot)) return slot;
  }
  return from;
}

void Game2048Activity::onTouch(const fui::ActionEvent& event, int, int) {
  if (event.action == ACTION_BAR) runBar(event.value);
}

void Game2048Activity::onKey(const int dx, const int dy, const bool confirm) {
  const int before = barFocus;
  if (barFocus < 0) {
    if (confirm) {
      if (hasBar()) {
        barFocus = nextBarSlot(BAR_SLOTS - 1, 1);
      } else if (game.undo()) {
        markChanged();  // button boards: Confirm undoes
      }
    } else if (dx != 0) {
      slide(dx < 0 ? Dir::Left : Dir::Right);
    } else if (dy != 0) {
      slide(dy < 0 ? Dir::Up : Dir::Down);
    }
  } else if (confirm) {
    runBar(barFocus);
    // The action may hide its own slot (the undo).
    if (barFocus >= 0 && !barShown(barFocus)) barFocus = nextBarSlot(barFocus, 1);
  } else if (dx != 0) {
    barFocus = nextBarSlot(barFocus, dx);
  } else if (dy < 0) {
    barFocus = -1;
  }
  if (barFocus != before) requestRepaint(RepaintUpdate);
}

bool Game2048Activity::onBack() {
  if (barFocus < 0) return false;
  barFocus = -1;
  requestRepaint(RepaintUpdate);
  return true;
}

// --- render task -------------------------------------------------------------

void Game2048Activity::build(UiScreen& screen) {
  char score[16];
  formatScore(game.score(), score, sizeof(score));
  drawTopLine(screen, tr(STR_2048_SCORE), score, false);

  const int left = (renderer.getScreenWidth() - kGridSize) / 2;
  for (int cell = 0; cell < g2048::CELLS; ++cell) {
    drawTile(left + cell % g2048::SIZE * (kTile + kGap), kGridTop + cell / g2048::SIZE * (kTile + kGap),
             game.exponent(cell));
  }

  int y = kGridTop + kGridSize + kFooterGap;
  char footer[96] = "";
  int len = 0;
  const uint32_t record = std::max(best, game.score());
  if (record > 0) {
    char digits[16];
    formatScore(record, digits, sizeof(digits));
    len = std::max(0, snprintf(footer, sizeof(footer), "%s %s", tr(STR_2048_BEST), digits));
  }
  if (game.isOver()) {
    drawResult(y, tr(STR_2048_NO_MOVES), footer);
  } else {
    if (game.justWon()) {
      renderer.drawText(kText12Font, kSidePadding, y, tr(STR_2048_WON), true, kSemiBold);
      y += renderer.getLineHeight(kText12Font) + kFooterGap / 2;
    }
    if (len < static_cast<int>(sizeof(footer))) {
      snprintf(footer + len, sizeof(footer) - len, "%s%s", len > 0 ? "  \xC2\xB7  " : "",
               mappedInput.hasTouch() ? tr(STR_2048_SWIPE_HINT) : tr(STR_2048_SIDE_HINT));
    }
    renderer.drawText(kCaptionFont, kSidePadding, y, footer);
  }

  BarItem items[BAR_SLOTS];
  if (barShown(0)) items[0] = {&icon_bar_undo_24, tr(STR_GAMES_UNDO), false};
  items[kMenuSlot] = game.isOver() ? BarItem{&icon_bar_new_game_24, tr(STR_GAMES_NEW_GAME), false, BarItem::NewGame}
                                   : BarItem{&icon_menu_24, tr(STR_GAMES_MENU), false, BarItem::Menu};
  drawBar(screen, items, barFocus);
}

void Game2048Activity::drawTile(const int x, const int y, const uint8_t exponent) const {
  const bool filled = exponent >= kFilledFrom;
  if (filled) {
    renderer.fillRect(x, y, kTile, kTile, true);
  } else {
    renderer.drawRect(x, y, kTile, kTile, exponent >= kThickFrom ? kCellFrameWidth : kTileRule, true);
  }
  if (exponent == 0) return;
  char text[12];
  snprintf(text, sizeof(text), "%lu", 1ul << exponent);
  const int textY = y + (kTile - renderer.getLineHeight(kText12Font)) / 2;
  renderer.drawText(kText12Font, x + (kTile - renderer.getTextWidth(kText12Font, text, kSemiBold)) / 2, textY, text,
                    !filled, kSemiBold);
}
