#include "ReversiActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#include "gameIcons.h"
#include "games/GameSave.h"
#include "games/chess/ui/chessIcons.h"
#include "levelIcons.h"

namespace fui = freeink::ui;
using namespace setka;
using reversi::CELLS;
using reversi::Disc;
using reversi::SIZE;
using reversi::Status;

namespace {
constexpr int kMenuSlot = 2;
constexpr int kSliceMs = 20;
constexpr int kDiscInset = 7;
constexpr int kDot = 10;
constexpr int kMark = 12;
constexpr int kCursorInset = 4;
constexpr int kStatusGap = 22;
constexpr int kLineGap = 10;
constexpr int kCountDisc = 18;
constexpr int kCountGap = 8;
constexpr int kCountSpace = 22;
}  // namespace

const char* reversiListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Reversi) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openReversi(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<ReversiActivity>(renderer, mappedInput);
}

ReversiActivity::ReversiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("Reversi", StrId::STR_REVERSI, renderer, mappedInput) {}

// A finished game still loads for its score; the screen then opens the picker.
bool ReversiActivity::restore() {
  if (!gamesave::load(game) || !game.inProgress()) return false;
  syncFocus();
  return true;
}

void ReversiActivity::saveGame() { gamesave::save(game); }

void ReversiActivity::clearSave() { gamesave::clear(gamesave::Kind::Reversi); }

void ReversiActivity::openNewGame() {
  const GamePickerActivity::Row rows[] = {
      {tr(STR_SUDOKU_EASY), &icon_level_1_24},
      {tr(STR_SUDOKU_MEDIUM), &icon_level_2_24},
      {tr(STR_SUDOKU_HARD), &icon_level_3_24},
      {tr(STR_GAMES_TWO_PLAYERS), &icon_friend_24},
  };
  openPicker(tr(STR_GAMES_NEW_GAME), rows, static_cast<int>(std::size(rows)), static_cast<int>(game.mode()));
}

void ReversiActivity::pickerChosen(const int row, bool) {
  engine.cancel();
  game.newGame(static_cast<reversi::Mode>(std::clamp(row, 0, reversi::MODE_COUNT - 1)), esp_random());
  cursor = -1;
  barFocus = -1;
  syncFocus();
}

// --- loop task ---------------------------------------------------------------

void ReversiActivity::loop() {
  GameScreen::loop();
  if (!hasGame || paused || !game.computerToMove()) return;
  if (!engine.thinking() && !engine.ready()) {
    thinkStart = nowMs();
    engine.start(game.whiteDiscs(), game.blackDiscs(), reversi::limitsFor(game.mode()),
                 game.seed() ^ (game.movesMade() * 0x9E3779B9u), thinkStart);
  }
  if (!engine.step(&nowMs, kSliceMs)) return;
  RenderLock lock(RenderLock::Mode::Try);
  if (!lock.ownsLock()) return;
  LOG_DBG("GAMES", "Reversi: depth %d, %lu nodes, %lu ms", engine.depth(), static_cast<unsigned long>(engine.nodes()),
          static_cast<unsigned long>(nowMs() - thinkStart));
  const int square = engine.move();
  engine.cancel();
  if (game.play(square)) changed();
}

bool ReversiActivity::playerToMove() const {
  return !game.isOver() && (!game.vsComputer() || game.toMove() == Disc::Black);
}

void ReversiActivity::syncFocus() {
  if (mappedInput.hasTouch() || game.isOver()) return;
  const uint64_t moves = playerToMove() ? game.moves() : 0;
  if (moves != 0 && (cursor < 0 || !(moves >> cursor & 1))) cursor = __builtin_ctzll(moves);
}

void ReversiActivity::changed() {
  syncFocus();
  markChanged();
}

void ReversiActivity::play(const int square) {
  if (!playerToMove() || !game.play(square)) return;
  changed();
}

void ReversiActivity::runBar(const int slot) {
  if (slot == 0) {
    engine.cancel();
    if (game.undo()) changed();
  } else if (slot == kMenuSlot) {
    if (game.isOver()) {
      openNewGame();
    } else {
      openGameMenu();
    }
  }
}

bool ReversiActivity::barShown(const int slot) const {
  if (slot == 0) return game.canUndo();
  return slot == kMenuSlot;
}

// Keys walk the legal moves, then the bar where it is shown, and wrap.
void ReversiActivity::stepFocus(const int step) {
  const uint64_t moves = playerToMove() ? game.moves() : 0;
  int8_t ring[CELLS + BAR_SLOTS];  // a square, or -1 - bar slot
  int count = 0;
  int at = -1;
  for (int s = 0; s < CELLS; ++s) {
    if (!(moves >> s & 1)) continue;
    if (barFocus < 0 && s == cursor) at = count;
    ring[count++] = static_cast<int8_t>(s);
  }
  for (int slot = 0; hasBar() && slot < BAR_SLOTS; ++slot) {
    if (!barShown(slot)) continue;
    if (slot == barFocus) at = count;
    ring[count++] = static_cast<int8_t>(-1 - slot);
  }
  if (count == 0) return;
  const int next = at < 0 ? (step > 0 ? 0 : count - 1) : (at + step + count) % count;
  if (ring[next] >= 0) {
    cursor = ring[next];
    barFocus = -1;
  } else {
    barFocus = -1 - ring[next];
  }
}

void ReversiActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  if (event.action == ACTION_BOARD) {
    const int cell = renderer.getScreenWidth() / SIZE;
    const int left = (renderer.getScreenWidth() - cell * SIZE) / 2;
    const int column = (x - left) / cell;
    const int row = (y - BOARD_TOP) / cell;
    if (x >= left && y >= BOARD_TOP && column < SIZE && row < SIZE) play(row * SIZE + column);
  } else if (event.action == ACTION_BAR && barShown(event.value)) {
    runBar(event.value);
  }
}

void ReversiActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int before[] = {cursor, barFocus};
  if (confirm && barFocus >= 0) {
    runBar(barFocus);
    if (barFocus >= 0 && !barShown(barFocus)) barFocus = kMenuSlot;
  } else if (confirm && cursor >= 0 && playerToMove()) {
    play(cursor);
  } else if (dy != 0 && dx == 0 && !hasBar() && cursor >= 0) {
    // Button boards: Up/Down jump to the nearest move on a row above or below.
    const uint64_t moves = playerToMove() ? game.moves() : 0;
    int best = INT_MAX;
    for (int s = 0; s < CELLS; ++s) {
      const int rows = (s / SIZE - cursor / SIZE) * dy;
      const int score = (moves >> s & 1) && rows > 0 ? rows * SIZE + std::abs(s % SIZE - cursor % SIZE) : INT_MAX;
      if (score < best) {
        best = score;
        cursor = s;
      }
    }
  } else if (dx != 0 || dy != 0) {
    stepFocus(dx != 0 ? dx : dy);
  }
  const int after[] = {cursor, barFocus};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

bool ReversiActivity::onBack() {
  if (barFocus < 0 || game.isOver()) return false;
  barFocus = -1;
  syncFocus();
  requestRepaint(RepaintUpdate);
  return true;
}

// --- render task -------------------------------------------------------------

void ReversiActivity::build(UiScreen& screen) {
  char level[12];
  const bool vs = game.vsComputer();
  snprintf(level, sizeof(level), "%d", static_cast<int>(game.mode()) + 1);
  if (vs) {
    drawTopLine(screen, {{tr(STR_GAMES_VS_LEVEL), level}}, false);
  } else {
    drawTopLine(screen, {{tr(STR_GAMES_TWO_PLAYERS), nullptr}}, false);
  }

  const int width = renderer.getScreenWidth();
  const int cell = width / SIZE;
  const int side = cell * SIZE;
  const int left = (width - side) / 2;
  for (int i = 0; i <= SIZE; ++i) {
    const int offset = std::clamp(i * cell - kRuleWidth / 2, 0, side - kRuleWidth);
    renderer.fillRect(left + offset, BOARD_TOP, kRuleWidth, side, true);
    renderer.fillRect(left, BOARD_TOP + offset, side, kRuleWidth, true);
  }
  const uint64_t moves = playerToMove() ? game.moves() : 0;
  const int last = !vs || game.lastMover() == Disc::White ? game.lastMove() : -1;
  for (int s = 0; s < CELLS; ++s) {
    const int x = left + s % SIZE * cell;
    const int y = BOARD_TOP + s / SIZE * cell;
    const Disc disc = game.at(s);
    if (disc != Disc::None) {
      drawDisc(disc, x + kDiscInset, y + kDiscInset, cell - 2 * kDiscInset);
      if (s == last) {
        renderer.fillRect(x + (cell - kMark) / 2, y + (cell - kMark) / 2, kMark, kMark, disc == Disc::White);
      }
    } else if (moves >> s & 1) {
      renderer.fillRoundedRect(x + (cell - kDot) / 2, y + (cell - kDot) / 2, kDot, kDot, kDot / 2, Color::Black);
    }
  }
  if (cursor >= 0 && barFocus < 0 && (moves >> cursor & 1)) {
    drawCellFrame(renderer, left + cursor % SIZE * cell + kCursorInset, BOARD_TOP + cursor / SIZE * cell + kCursorInset,
                  cell - 2 * kCursorInset, cell - 2 * kCursorInset);
  }
  screen.frame().hit(fui::makeRect(left, BOARD_TOP, side, side), ACTION_BOARD, 0, shellInput());

  drawStatus(BOARD_TOP + side + kStatusGap);

  BarItem items[BAR_SLOTS];
  if (barShown(0)) items[0] = {&icon_bar_undo_24, tr(STR_GAMES_UNDO), false, BarItem::Repeat};
  const bool over = game.isOver();
  items[kMenuSlot] = {over ? &icon_bar_new_game_24 : &icon_menu_24, over ? tr(STR_GAMES_NEW_GAME) : tr(STR_GAMES_MENU),
                      false, over ? BarItem::NewGame : BarItem::Menu};
  drawBar(screen, items, barFocus);
}

void ReversiActivity::drawStatus(const int top) const {
  const bool vs = game.vsComputer();
  const char* title;
  if (game.isOver()) {
    title = game.status() == Status::Draw       ? tr(STR_GAMES_DRAW)
            : game.status() == Status::BlackWon ? (vs ? tr(STR_GAMES_YOU_WON) : tr(STR_REVERSI_BLACK_WON))
                                                : (vs ? tr(STR_GAMES_YOU_LOST) : tr(STR_REVERSI_WHITE_WON));
  } else if (vs) {
    title = game.computerToMove() ? tr(STR_REVERSI_THINKING) : tr(STR_GAMES_YOUR_TURN);
  } else {
    title = game.toMove() == Disc::Black ? tr(STR_REVERSI_BLACK_MOVE) : tr(STR_REVERSI_WHITE_MOVE);
  }
  renderer.drawText(kText12Font, kSidePadding, top, title, true, kSemiBold);

  const int lineHeight = renderer.getLineHeight(kText12Font);
  const int discY = top + (lineHeight - kCountDisc) / 2;
  int x = renderer.getScreenWidth() - kSidePadding;
  for (const Disc disc : {Disc::White, Disc::Black}) {
    char count[4];
    snprintf(count, sizeof(count), "%d", game.count(disc));
    x -= renderer.getTextWidth(kText12Font, count, kSemiBold);
    renderer.drawText(kText12Font, x, top, count, true, kSemiBold);
    x -= kCountGap + kCountDisc;
    drawDisc(disc, x, discY, kCountDisc);
    x -= kCountSpace;
  }

  const char* lines[2] = {};
  char tally[64];
  if (game.isOver()) {
    const reversi::Score& score = game.score();
    snprintf(tally, sizeof(tally), vs ? tr(STR_GAMES_SCORE_VS) : tr(STR_REVERSI_SCORE_TWO), score.won, score.lost,
             score.draws);
    lines[0] = tally;
    if (!hasBar()) lines[1] = tr(STR_GAMES_NEW_GAME_KEY);
  } else {
    const Disc passed = game.passed();
    lines[0] = passed == Disc::Black   ? (vs ? tr(STR_REVERSI_PASS_YOU) : tr(STR_REVERSI_PASS_BLACK))
               : passed == Disc::White ? (vs ? tr(STR_REVERSI_PASS_COMPUTER) : tr(STR_REVERSI_PASS_WHITE))
                                       : tr(STR_REVERSI_HINT_DOTS);
    lines[1] = vs ? tr(STR_REVERSI_HINT_LAST) : tr(STR_REVERSI_HINT_LAST_TWO);
  }
  int y = top + lineHeight + kLineGap;
  for (const char* line : lines) {
    if (line == nullptr) continue;
    renderer.drawText(kText10Font, kSidePadding, y, line);
    y += renderer.getLineHeight(kText10Font);
  }
}

void ReversiActivity::drawDisc(const Disc disc, const int x, const int y, const int size) const {
  renderer.fillRoundedRect(x, y, size, size, size / 2, Color::Black);
  if (disc == Disc::White) {
    const int stroke = size / 16 + 1;
    renderer.fillRoundedRect(x + stroke, y + stroke, size - 2 * stroke, size - 2 * stroke, size / 2 - stroke,
                             Color::White);
  }
}
