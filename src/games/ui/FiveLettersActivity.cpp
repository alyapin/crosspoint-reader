#include "FiveLettersActivity.h"

#include <KeyboardGames.h>
#if SOVA_KEYBOARD_GAMES
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "LetterKeyboard.h"
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
constexpr int kResultBand = 88;  // between the grid and the keyboard: the result and the totals once over

uint8_t letterOfKey(const int key) { return fiveletters::letterOf(games::ui::keyboardCodepoint(key)); }

games::ui::KeyLook keyLook(const void* ctx, const int key) {
  switch (static_cast<const fiveletters::Game*>(ctx)->keyMark(letterOfKey(key))) {
    case Mark::Placed:
      return games::ui::KeyLook::Placed;
    case Mark::Present:
      return games::ui::KeyLook::Present;
    case Mark::Absent:
      return games::ui::KeyLook::Spent;
    case Mark::None:
      break;
  }
  return games::ui::KeyLook::Plain;
}
}  // namespace

const char* fiveLettersListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::FiveLetters) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openFiveLetters(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<FiveLettersActivity>(renderer, mappedInput);
}

FiveLettersActivity::FiveLettersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("FiveLetters", StrId::STR_FIVE_LETTERS, renderer, mappedInput) {}

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
  keyCursor = mappedInput.hasTouch() ? -1 : games::ui::kKeyboardStartKey;
}

// --- input -------------------------------------------------------------------

void FiveLettersActivity::type(const int key) {
  const uint8_t letter = letterOfKey(key);
  if (game.keyMark(letter) == Mark::Absent || !game.type(letter)) return;
  if (keyCursor >= 0 && game.typed() == LETTERS && hasBar()) barFocus = DONE_SLOT;
  requestRepaint(RepaintUpdate);
}

void FiveLettersActivity::submit() {
  switch (game.submit()) {
    case fiveletters::Submit::Scored:
      if (game.isOver()) {
        stats.record(game.status() == fiveletters::Status::Won);
        statsPending = true;
      }
      if (barFocus >= 0) barFocus = game.isOver() ? MENU_SLOT : -1;
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

void FiveLettersActivity::runSlot(const int slot) {
  if (!barShown(slot)) return;
  switch (slot) {
    case ERASE_SLOT:
      if (game.erase()) requestRepaint(RepaintUpdate);
      break;
    case DONE_SLOT:
      submit();
      break;
    case MENU_SLOT:
      if (game.isOver()) {
        openNewGame();
      } else {
        openGameMenu();
      }
      break;
    default:
      break;
  }
}

bool FiveLettersActivity::barShown(const int slot) const { return slot == MENU_SLOT || !game.isOver(); }

int FiveLettersActivity::nextBarSlot(const int from, const int step) const {
  for (int i = 1; i <= BAR_SLOTS; ++i) {
    const int slot = (from + step * i + 2 * BAR_SLOTS) % BAR_SLOTS;
    if (barShown(slot)) return slot;
  }
  return from;
}

void FiveLettersActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  const bool hadNotice = unknownWord;
  unknownWord = false;
  if (event.action == ACTION_BOARD) {
    const int key = games::ui::keyboardKeyAt(renderer, keyboardTop(), x, y);
    if (key >= 0) type(key);
  } else if (event.action == ACTION_BAR) {
    runSlot(event.value);
  }
  if (hadNotice && !unknownWord) requestRepaint(RepaintUpdate);
}

void FiveLettersActivity::onKey(const int dx, const int dy, const bool confirm) {
  using games::ui::kKeyboardColumns;
  const int before[] = {keyCursor, barFocus, unknownWord};
  unknownWord = false;
  const int row = keyCursor / kKeyboardColumns;
  if (barFocus >= 0) {
    if (confirm) {
      runSlot(barFocus);
    } else if (dx != 0) {
      barFocus = nextBarSlot(barFocus, dx);
    } else if (dy < 0 && !game.isOver()) {
      barFocus = -1;
    }
  } else if (keyCursor < 0) {
    keyCursor = games::ui::kKeyboardStartKey;
  } else if (confirm) {
    type(keyCursor);
  } else if (dx != 0) {
    keyCursor = row * kKeyboardColumns + (keyCursor % kKeyboardColumns + dx + kKeyboardColumns) % kKeyboardColumns;
  } else if (dy < 0 && row > 0) {
    keyCursor -= kKeyboardColumns;
  } else if (dy > 0 && row < games::ui::kKeyboardRows - 1) {
    keyCursor += kKeyboardColumns;
  } else if (dy > 0 && hasBar()) {
    barFocus = DONE_SLOT;
  }
  const int after[] = {keyCursor, barFocus, unknownWord};
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
  const int room = keyboardTop() - kRuleWidth - kResultBand - BOARD_TOP;
  return std::min(kMaxCell, (room - (ATTEMPTS - 1) * kCellGap) / ATTEMPTS);
}

// The keyboard sits on the bar.
int FiveLettersActivity::keyboardTop() const { return barTop() - games::ui::kKeyboardHeight; }

// --- render task -------------------------------------------------------------

void FiveLettersActivity::build(UiScreen& screen) {
  char value[48] = "";
  if (game.status() == fiveletters::Status::Won) {
    snprintf(value, sizeof(value), tr(STR_FIVE_WON), game.guesses());
  } else if (game.status() == fiveletters::Status::Lost) {
    char word[2 * LETTERS + 1];
    game.answerText(word, sizeof(word));
    snprintf(value, sizeof(value), tr(STR_FIVE_LOST), word);
  }
  char attempt[16];
  snprintf(attempt, sizeof(attempt), tr(STR_GAMES_OF), game.guesses() + 1, ATTEMPTS);
  if (game.isOver()) {
    drawTopLine(screen, {}, false);
  } else if (unknownWord) {
    drawTopLine(screen, {{tr(STR_FIVE_UNKNOWN), nullptr}}, false);
  } else {
    drawTopLine(screen, {{tr(STR_FIVE_TRY), attempt}}, false);
  }

  const int size = cellSize();
  const int left = (renderer.getScreenWidth() - LETTERS * size - (LETTERS - 1) * kCellGap) / 2;
  for (int r = 0; r < ATTEMPTS; ++r) {
    for (int c = 0; c < LETTERS; ++c) {
      drawCell(r, c, left + c * (size + kCellGap), BOARD_TOP + r * (size + kCellGap), size);
    }
  }

  const int keysTop = keyboardTop();
  const bool over = game.isOver();
  games::ui::drawKeyboard(renderer, keysTop, keyLook, &game, barFocus < 0 && !over ? keyCursor : -1);
  if (!over) {
    screen.frame().hit(fui::makeRect(0, keysTop, renderer.getScreenWidth(), games::ui::kKeyboardHeight), ACTION_BOARD,
                       0, shellInput());
  } else {
    char totals[64];
    snprintf(totals, sizeof(totals), tr(STR_FIVE_STATS), static_cast<unsigned>(stats.played),
             static_cast<unsigned>(stats.wins), static_cast<unsigned>(stats.streak));
    drawResult(games::ui::captionTop(renderer, BOARD_TOP + ATTEMPTS * size + (ATTEMPTS - 1) * kCellGap, kText12Font),
               value, totals);
  }

  BarItem items[BAR_SLOTS];
  if (!over) {
    items[ERASE_SLOT] = {&icon_key_delete_24, tr(STR_SCANWORD_ERASE), false, BarItem::Action};
    items[DONE_SLOT] = {&icon_done_24, tr(STR_SCANWORD_DONE), false, BarItem::Action};
  }
  items[MENU_SLOT] = over ? BarItem{&icon_bar_new_game_24, tr(STR_GAMES_NEW_GAME), false, BarItem::NewGame}
                          : BarItem{&icon_menu_24, tr(STR_GAMES_MENU), false, BarItem::Menu};
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
  renderer.drawRect(x, y, size, size, next ? kCellFrameWidth : 1, true);
  if (letter != fiveletters::NO_LETTER) drawLetter(letter, x, y, size, size, cap, kSemiBold, true);
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

#endif  // SOVA_KEYBOARD_GAMES
