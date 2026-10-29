#include "FiveLettersActivity.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "gameIcons.h"
#include "games/GameSave.h"
#include "games/chess/ui/chessIcons.h"

namespace fui = freeink::ui;
using namespace setka;
using fiveletters::ATTEMPTS;
using fiveletters::LETTERS;
using fiveletters::Mark;

namespace {
constexpr int kCellGap = 6;
constexpr int kMaxCell = 64;
constexpr int kCursorFrame = 2;  // the cell the next letter goes to
constexpr int kGridKeysGap = 16;
constexpr int kKeyRows = 3;
constexpr int kKeyHeight = 44;
constexpr int kKeyGap = 6;
constexpr int kKeySide = 8;
constexpr int kKeyUnits = 12;     // the widest row; keys sit on half units
constexpr int kKeyCursorOut = 3;  // key focus: corner brackets just outside the key
constexpr int kCaptionBand = 32;  // under the keyboard: the totals once over
constexpr int kIconSize = 24;

constexpr uint8_t kDelete = 0xFE;
constexpr uint8_t kEnter = 0xFD;
constexpr int kRowKeys[kKeyRows] = {12, 11, 11};
// ЙЦУКЕНГШЩЗХЪ / ФЫВАПРОЛДЖЭ / ⌫ЯЧСМИТЬБЮ↵ as letters (А=0 … Я=31).
constexpr uint8_t kKeys[kKeyRows][kKeyUnits] = {
    {9, 22, 19, 10, 5, 13, 3, 24, 25, 7, 21, 26},
    {20, 27, 2, 0, 15, 16, 14, 11, 4, 6, 29},
    {kDelete, 31, 23, 17, 12, 8, 18, 28, 1, 30, kEnter},
};
constexpr int kStartRow = 1;
constexpr int kStartIndex = 5;
}  // namespace

const char* fiveLettersListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::FiveLetters) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openFiveLetters(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<FiveLettersActivity>(renderer, mappedInput);
}

FiveLettersActivity::FiveLettersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("FiveLetters", renderer, mappedInput) {}

// No setup to pick: without a save a new game starts here.
bool FiveLettersActivity::restore() {
  gamesave::loadStats(stats);
  if (!gamesave::load(game)) game.newGame(esp_random());
  placeCursor();
  return true;
}

void FiveLettersActivity::saveGame() {
  gamesave::save(game);
  if (statsPending) {
    statsPending = false;
    gamesave::saveStats(stats);
  }
}

void FiveLettersActivity::clearSave() {
  if (game.inProgress()) {
    stats.record(false);
    gamesave::saveStats(stats);
  }
  gamesave::clear(gamesave::Kind::FiveLetters);
}

void FiveLettersActivity::openNewGame() {
  if (game.inProgress()) {
    stats.record(false);
    statsPending = true;
  }
  game.newGame(esp_random());
  unknownWord = false;
  placeCursor();
  markChanged();
}

void FiveLettersActivity::placeCursor() {
  barFocus = -1;
  keyRow = mappedInput.hasTouch() ? -1 : kStartRow;
  keyIndex = kStartIndex;
}

// --- input -------------------------------------------------------------------

void FiveLettersActivity::press(const uint8_t key) {
  if (key == kDelete) {
    if (game.erase()) requestRepaint(RepaintUpdate);
    return;
  }
  if (key != kEnter) {
    if (game.keyMark(key) == Mark::Absent || !game.type(key)) return;
    if (keyRow >= 0 && game.typed() == LETTERS) {
      keyRow = kKeyRows - 1;
      keyIndex = kRowKeys[keyRow] - 1;
    }
    requestRepaint(RepaintUpdate);
    return;
  }
  switch (game.submit()) {
    case fiveletters::Submit::Scored:
      if (game.isOver()) {
        stats.record(game.status() == fiveletters::Status::Won);
        statsPending = true;
        if (keyRow >= 0) barFocus = MENU_SLOT;
      }
      markChanged();
      break;
    case fiveletters::Submit::Unknown:
      unknownWord = true;
      requestRepaint(RepaintUpdate);
      break;
    default:
      break;
  }
}

void FiveLettersActivity::runMenu() {
  if (game.isOver()) {
    openNewGame();
  } else {
    openGameMenu();
  }
}

void FiveLettersActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  const bool hadNotice = unknownWord;
  unknownWord = false;
  if (event.action == ACTION_BOARD) {
    const int row = std::clamp((y - keyboardTop()) / (kKeyHeight + kKeyGap), 0, kKeyRows - 1);
    press(kKeys[row][keyAt(row, x)]);
  } else if (event.action == ACTION_BAR && event.value == MENU_SLOT) {
    runMenu();
  }
  if (hadNotice && !unknownWord) requestRepaint(RepaintUpdate);
}

void FiveLettersActivity::moveRow(const int step) {
  int x = 0;
  int width = 0;
  keyBounds(keyRow, keyIndex, x, width);
  keyRow += step;
  keyIndex = keyAt(keyRow, x + width / 2);
}

void FiveLettersActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int before[] = {keyRow, keyIndex, barFocus, unknownWord};
  unknownWord = false;
  if (barFocus >= 0) {
    if (confirm) {
      runMenu();
    } else if (dy < 0 && !game.isOver()) {
      barFocus = -1;
    }
  } else if (keyRow < 0) {
    keyRow = kStartRow;
    keyIndex = kStartIndex;
  } else if (confirm) {
    press(kKeys[keyRow][keyIndex]);
  } else if (dx != 0) {
    keyIndex = (keyIndex + dx + kRowKeys[keyRow]) % kRowKeys[keyRow];
  } else if (dy < 0 && keyRow > 0) {
    moveRow(-1);
  } else if (dy > 0 && keyRow < kKeyRows - 1) {
    moveRow(1);
  } else if (dy > 0) {
    barFocus = MENU_SLOT;
  }
  const int after[] = {keyRow, keyIndex, barFocus, unknownWord};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

bool FiveLettersActivity::onBack() {
  if (barFocus < 0 || game.isOver()) return false;
  barFocus = -1;
  requestRepaint(RepaintUpdate);
  return true;
}

// --- layout ------------------------------------------------------------------

int FiveLettersActivity::cellSize() const {
  const int keyboard = kKeyRows * kKeyHeight + (kKeyRows - 1) * kKeyGap;
  const int room = barTop() - kCaptionBand - keyboard - kGridKeysGap - BOARD_TOP;
  return std::min(kMaxCell, (room - (ATTEMPTS - 1) * kCellGap) / ATTEMPTS);
}

int FiveLettersActivity::keyboardTop() const {
  return BOARD_TOP + ATTEMPTS * cellSize() + (ATTEMPTS - 1) * kCellGap + kGridKeysGap;
}

// The top row holds kKeyUnits keys; the others start half a key in, and ⌫ and ↵ are a key and a half wide.
void FiveLettersActivity::keyBounds(const int row, const int index, int& x, int& width) const {
  const int unit = (renderer.getScreenWidth() - 2 * kKeySide + kKeyGap) / kKeyUnits;
  const int left = (renderer.getScreenWidth() - kKeyUnits * unit + kKeyGap) / 2;
  int at = 2 * index + (row > 0 ? 1 : 0);
  int span = 2;
  if (row == kKeyRows - 1 && (index == 0 || index == kRowKeys[row] - 1)) {
    at = index == 0 ? 0 : at;
    span = 3;
  }
  x = left + at * unit / 2;
  width = left + (at + span) * unit / 2 - kKeyGap - x;
}

// The key under x, the gaps split between neighbours and the row ends running to the edges.
int FiveLettersActivity::keyAt(const int row, const int x) const {
  for (int i = 0; i < kRowKeys[row] - 1; ++i) {
    int left = 0;
    int width = 0;
    keyBounds(row, i, left, width);
    if (x < left + width + kKeyGap / 2) return i;
  }
  return kRowKeys[row] - 1;
}

// --- render task -------------------------------------------------------------

void FiveLettersActivity::build(UiScreen& screen) {
  char value[48];
  if (game.status() == fiveletters::Status::Won) {
    snprintf(value, sizeof(value), tr(STR_FIVE_WON), game.guesses());
  } else if (game.status() == fiveletters::Status::Lost) {
    char word[2 * LETTERS + 1];
    game.answerText(word, sizeof(word));
    snprintf(value, sizeof(value), tr(STR_FIVE_LOST), word);
  } else if (unknownWord) {
    snprintf(value, sizeof(value), "%s", tr(STR_FIVE_UNKNOWN));
  } else {
    snprintf(value, sizeof(value), tr(STR_FIVE_TRY), game.guesses() + 1, ATTEMPTS);
  }
  drawTopLine(screen, nullptr, value, false);

  const int size = cellSize();
  const int left = (renderer.getScreenWidth() - LETTERS * size - (LETTERS - 1) * kCellGap) / 2;
  for (int r = 0; r < ATTEMPTS; ++r) {
    for (int c = 0; c < LETTERS; ++c) {
      drawCell(r, c, left + c * (size + kCellGap), BOARD_TOP + r * (size + kCellGap), size);
    }
  }

  const int keysTop = keyboardTop();
  for (int r = 0; r < kKeyRows; ++r) {
    for (int i = 0; i < kRowKeys[r]; ++i) drawKey(screen, r, i);
  }
  const int keysHeight = kKeyRows * kKeyHeight + (kKeyRows - 1) * kKeyGap;
  if (!game.isOver()) {
    screen.frame().hit(fui::makeRect(0, keysTop, renderer.getScreenWidth(), keysHeight), ACTION_BOARD, 0, shellInput());
  } else {
    char totals[64];
    snprintf(totals, sizeof(totals), tr(STR_FIVE_STATS), static_cast<unsigned>(stats.played),
             static_cast<unsigned>(stats.wins), static_cast<unsigned>(stats.streak));
    const int bandTop = keysTop + keysHeight;
    renderer.drawText(kCaptionFont, kSidePadding,
                      bandTop + (barTop() - bandTop - renderer.getLineHeight(kCaptionFont)) / 2, totals);
  }

  BarItem items[BAR_SLOTS];
  items[MENU_SLOT] = game.isOver() ? BarItem{&icon_bar_new_game_24, tr(STR_GAMES_NEW_GAME)}
                                   : BarItem{&icon_menu_24, tr(STR_GAMES_MENU)};
  drawBar(screen, items, barFocus);
}

void FiveLettersActivity::drawCell(const int row, const int column, const int x, const int y, const int size) const {
  const uint8_t letter = game.letter(row, column);
  const int cap = size * 2 / 5;
  switch (game.mark(row, column)) {
    case Mark::Placed:
      renderer.fillRect(x, y, size, size, true);
      drawLetter(letter, x, y, size, size, cap, kSemiBold, false);
      return;
    case Mark::Present:
      renderer.drawRect(x, y, size, size, kCellFrameWidth, true);
      drawLetter(letter, x, y, size, size, cap, kSemiBold, true);
      return;
    case Mark::Absent:
      renderer.drawRect(x, y, size, size, true);
      drawLetter(letter, x, y, size, size, cap, EpdFontFamily::REGULAR, true);
      return;
    case Mark::None:
      break;
  }
  const bool next = !game.isOver() && row == game.guesses() && column == game.typed();
  renderer.drawRect(x, y, size, size, next ? kCursorFrame : 1, true);
  if (letter != fiveletters::NO_LETTER) drawLetter(letter, x, y, size, size, cap, kSemiBold, true);
}

void FiveLettersActivity::drawKey(UiScreen& screen, const int row, const int index) const {
  int x = 0;
  int width = 0;
  keyBounds(row, index, x, width);
  const int y = keyboardTop() + row * (kKeyHeight + kKeyGap);
  const uint8_t key = kKeys[row][index];
  constexpr int kKeyCap = 14;
  if (key == kDelete || key == kEnter) {
    renderer.drawRect(x, y, width, kKeyHeight, true);
    screen.target().bitmap(
        fui::makeRect(x + (width - kIconSize) / 2, y + (kKeyHeight - kIconSize) / 2, kIconSize, kIconSize),
        fui::bitmapFromIcon(key == kDelete ? icon_key_delete_24 : icon_key_enter_24), fui::BitmapMode::Center);
  } else {
    switch (game.keyMark(key)) {
      case Mark::Placed:
        renderer.fillRect(x, y, width, kKeyHeight, true);
        drawLetter(key, x, y, width, kKeyHeight, kKeyCap, kSemiBold, false);
        break;
      case Mark::Present:
        renderer.drawRect(x, y, width, kKeyHeight, kCellFrameWidth, true);
        drawLetter(key, x, y, width, kKeyHeight, kKeyCap, kSemiBold, true);
        break;
      case Mark::Absent:
        renderer.fillRectDither(x, y, width, kKeyHeight, Color::LightGray);
        break;
      case Mark::None:
        renderer.drawRect(x, y, width, kKeyHeight, true);
        drawLetter(key, x, y, width, kKeyHeight, kKeyCap, EpdFontFamily::REGULAR, true);
        break;
    }
  }
  if (row == keyRow && index == keyIndex && barFocus < 0 && !game.isOver()) {
    drawCursor(x - kKeyCursorOut, y - kKeyCursorOut, width + 2 * kKeyCursorOut, kKeyHeight + 2 * kKeyCursorOut);
  }
}

// A letter scaled to capHeight, centred in the box.
void FiveLettersActivity::drawLetter(const uint8_t letter, const int x, const int y, const int width, const int height,
                                     const int capHeight, const EpdFontFamily::Style style, const bool black) const {
  const uint32_t cp = fiveletters::codepointOf(letter);
  int32_t advance = 0;
  int top = 0;
  int32_t refAdvance = 0;
  int refTop = 0;
  if (!renderer.getCodepointMetrics(kText12Font, cp, style, advance, top) ||
      !renderer.getCodepointMetrics(kText12Font, 0x41D, style, refAdvance, refTop) || refTop <= 0) {
    return;
  }
  const int scale = capHeight * 256 / refTop;
  const int glyphWidth = static_cast<int>(advance * scale / (256 * 16));
  renderer.drawScaledCodepoint(kText12Font, cp, style, x + (width - glyphWidth) / 2, y + (height + capHeight) / 2,
                               scale, black);
}
