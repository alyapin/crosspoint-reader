#include "HangmanActivity.h"

#include <KeyboardGames.h>
#if SOVA_KEYBOARD_GAMES
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HangmanWords.h>
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

namespace {
using games::ui::kKeyboardColumns;
using games::ui::kKeyboardRows;
constexpr int kWordHeight = 48;  // the letters above their underlines
constexpr int kGallowsMax = 250;
constexpr int kGapMin = 16;
constexpr int kSlotPitchMax = 60;
constexpr int kLetterCap = 28;
constexpr int kUnderline = 3;
constexpr int kStroke = 4;
constexpr int kFigureStroke = 3;
constexpr int kClueGap = 28;
constexpr int kClueLines = 4;
constexpr int kStartKey = games::ui::kKeyboardStartKey;

// Обычные слова, and С крепкими словами: the same words with the rude ones after them.
const hangman::WordList kLists[] = {
    {hangman::WORDS, hangman::WORD_COUNT},
    {hangman::WORDS, static_cast<uint16_t>(hangman::WORD_COUNT + hangman::RUDE_WORD_COUNT)},
};
constexpr int kListCount = static_cast<int>(std::size(kLists));
// The list last picked outlives the game, so it is kept apart from the save.
constexpr const char* kListDir = "/.crosspoint/games";
constexpr const char* kListPath = "/.crosspoint/games/hangman-list.txt";

uint8_t loadList() {
  if (!Storage.exists(kListPath)) return 0;
  const String text = Storage.readFile(kListPath);
  return text.length() > 0 && text[0] == '1' ? 1 : 0;
}

void saveList(const uint8_t list) {
  if (Storage.ensureDirectoryExists(kListDir)) Storage.writeFile(kListPath, String(list == 1 ? "1" : "0"));
}

int measureText(void* ctx, const char* text) {
  const auto* renderer = static_cast<const GfxRenderer*>(ctx);
  return renderer->getTextWidth(kText12Font, text);
}

// Untried keys plain, right ones black, wrong ones grey without the letter.
games::ui::KeyLook keyLook(const void* ctx, const int key) {
  switch (static_cast<const hangman::Game*>(ctx)->key(games::ui::keyboardLetter(key))) {
    case hangman::Key::Right:
      return games::ui::KeyLook::Placed;
    case hangman::Key::Wrong:
      return games::ui::KeyLook::Spent;
    case hangman::Key::Untried:
      break;
  }
  return games::ui::KeyLook::Plain;
}
}  // namespace

const char* hangmanListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Hangman) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openHangman(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<HangmanActivity>(renderer, mappedInput);
}

HangmanActivity::HangmanActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("Hangman", StrId::STR_HANGMAN, renderer, mappedInput), game(kLists, kListCount) {}

bool HangmanActivity::restore() {
  chosenList = loadList();
  if (!gamesave::load(game)) return false;
  cursor = mappedInput.hasTouch() ? -1 : kStartKey;
  barFocus = -1;
  return true;
}

void HangmanActivity::saveGame() {
  gamesave::save(game);
  if (listPending) {
    listPending = false;
    saveList(chosenList);
  }
}

void HangmanActivity::clearSave() { gamesave::clear(gamesave::Kind::Hangman); }

void HangmanActivity::openNewGame() {
  const GamePickerActivity::Row rows[] = {
      {tr(STR_HANGMAN_NORMAL), &icon_hangman_24},
      {tr(STR_HANGMAN_RUDE), &icon_hangman_rude_24},
  };
  openPicker(tr(STR_GAMES_NEW_GAME), rows, static_cast<int>(std::size(rows)), chosenList);
}

void HangmanActivity::pickerChosen(const int row, bool) {
  const auto list = static_cast<uint8_t>(std::clamp(row, 0, kListCount - 1));
  listPending = listPending || list != chosenList;
  chosenList = list;
  game.newGame(list, esp_random());
  cursor = mappedInput.hasTouch() ? -1 : kStartKey;
  barFocus = -1;
}

// --- input -------------------------------------------------------------------

void HangmanActivity::runAction(const Action action) {
  switch (action) {
    case Action::Hint:
      if (game.hint()) markChanged();
      break;
    case Action::Menu:
      if (game.isOver()) {
        openNewGame();
      } else {
        openGameMenu();
      }
      break;
    case Action::None:
      break;
  }
}

void HangmanActivity::guessKey(const int key) {
  if (key < 0) return;
  const hangman::Guess result = game.guess(games::ui::keyboardLetter(key));
  if (result != hangman::Guess::Hit && result != hangman::Guess::Miss) return;
  markChanged();
}

void HangmanActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  switch (event.action) {
    case ACTION_BOARD:
      guessKey(games::ui::keyboardKeyAt(renderer, layout().keysTop, x, y));
      break;
    case ACTION_BAR:
      if (event.value >= 0 && event.value < BAR_SLOTS && barShown(event.value)) {
        runAction(static_cast<Action>(event.value));
      }
      break;
    default:
      break;
  }
}

bool HangmanActivity::barShown(const int slot) const {
  if (slot == static_cast<int>(Action::Hint)) return game.canHint();
  return slot == static_cast<int>(Action::Menu);
}

int HangmanActivity::nextBarSlot(const int from, const int step) const {
  for (int i = 1; i <= BAR_SLOTS; ++i) {
    const int slot = (from + step * i + 2 * BAR_SLOTS) % BAR_SLOTS;
    if (barShown(slot)) return slot;
  }
  return from;
}

void HangmanActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int before[] = {cursor, barFocus};
  if (barFocus >= 0) {
    if (confirm) {
      if (barShown(barFocus)) runAction(static_cast<Action>(barFocus));
      if (barFocus >= 0 && !barShown(barFocus)) barFocus = nextBarSlot(barFocus, 1);
    } else if (dx != 0) {
      barFocus = nextBarSlot(barFocus, dx);
    } else if (dy < 0 && !game.isOver()) {
      barFocus = -1;
    }
  } else if (cursor < 0) {
    cursor = kStartKey;
  } else {
    const int column = cursor % kKeyboardColumns;
    const int row = cursor / kKeyboardColumns;
    if (confirm) {
      guessKey(cursor);
    } else if (dx != 0) {
      cursor = row * kKeyboardColumns + (column + dx + kKeyboardColumns) % kKeyboardColumns;
    } else if (dy < 0 && row > 0) {
      cursor -= kKeyboardColumns;
    } else if (dy > 0 && row < kKeyboardRows - 1) {
      cursor += kKeyboardColumns;
    } else if (dy > 0 && hasBar()) {
      barFocus = nextBarSlot(BAR_SLOTS - 1, 1);
    }
  }
  const int after[] = {cursor, barFocus};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

bool HangmanActivity::onBack() {
  if (barFocus < 0 || game.isOver()) return false;
  barFocus = -1;
  requestRepaint(RepaintUpdate);
  return true;
}

// The keyboard on the bar; above it the gallows and the word with even gaps, and room for the result under
// the word. The gallows give way on a short screen.
HangmanActivity::Layout HangmanActivity::layout() const {
  Layout l;
  l.keysTop = barTop() - games::ui::kKeyboardHeight;
  const int band = kUnderline + games::ui::kCaptionGap + resultHeight(false);
  const int room = l.keysTop - kRuleWidth - BOARD_TOP - kWordHeight - band;
  const int gallows = std::min(kGallowsMax, room - 3 * kGapMin);
  const int gap = (room - gallows) / 3;
  l.gallowsTop = BOARD_TOP + gap;
  l.gallowsBottom = l.gallowsTop + gallows;
  l.wordBaseline = l.gallowsBottom + gap + kWordHeight;
  return l;
}

// --- render task -------------------------------------------------------------

void HangmanActivity::build(UiScreen& screen) {
  char value[32];
  snprintf(value, sizeof(value), tr(STR_GAMES_OF), game.mistakes(), hangman::MAX_MISTAKES);
  drawTopLine(screen, {{tr(STR_HANGMAN_MISTAKES), value}}, false);

  const Layout l = layout();
  drawGallows(l.gallowsTop, l.gallowsBottom);
  drawWord(l.wordBaseline);
  const bool over = game.isOver();
  games::ui::drawKeyboard(renderer, l.keysTop, keyLook, &game, barFocus < 0 && !over ? cursor : -1);
  screen.frame().hit(fui::makeRect(0, l.keysTop, renderer.getScreenWidth(), games::ui::kKeyboardHeight), ACTION_BOARD,
                     0, shellInput());
  if (over) {
    drawResult(games::ui::captionTop(renderer, l.wordBaseline + kUnderline, kText12Font),
               game.status() == hangman::Status::Won ? tr(STR_HANGMAN_WON) : tr(STR_HANGMAN_LOST));
  }

  BarItem items[BAR_SLOTS];
  if (barShown(0)) items[0] = {&icon_bar_hint_24, tr(STR_HANGMAN_HINT), false, BarItem::Action};
  items[2] = {over ? &icon_bar_new_game_24 : &icon_menu_24, over ? tr(STR_GAMES_NEW_GAME) : tr(STR_GAMES_MENU), false,
              over ? BarItem::NewGame : BarItem::Menu};
  drawBar(screen, items, barFocus);
}

// The gallows stand from the start; each mistake adds a part of the figure.
void HangmanActivity::drawGallows(const int top, const int bottom) const {
  const int height = bottom - top;
  const int width = std::min(height * 3 / 5, 170);
  const int left = kSidePadding + 8;
  const int post = left + width / 5;
  const int rope = left + width * 17 / 20;
  renderer.fillRect(left, bottom - kStroke, width * 3 / 5, kStroke, true);
  renderer.fillRect(post, top, kStroke, height, true);
  renderer.fillRect(post, top, rope - post + kStroke / 2, kStroke, true);
  const int ropeEnd = top + height * 3 / 20;
  renderer.fillRect(rope - 1, top, kFigureStroke, ropeEnd - top, true);

  const int mistakes = game.mistakes();
  const int r = height / 11;
  const int neck = ropeEnd + 2 * r;
  const int hip = neck + height * 3 / 10;
  const int armY = neck + height / 20;
  const int arm = width / 5;
  if (mistakes >= 1) {
    renderer.fillRoundedRect(rope - r, ropeEnd, 2 * r, 2 * r, r, Color::Black);
    renderer.fillRoundedRect(rope - r + kFigureStroke, ropeEnd + kFigureStroke, 2 * (r - kFigureStroke),
                             2 * (r - kFigureStroke), r - kFigureStroke, Color::White);
  }
  if (mistakes >= 2) renderer.fillRect(rope - 1, neck, kFigureStroke, hip - neck, true);
  if (mistakes >= 3) renderer.drawLine(rope, armY, rope - arm, armY + arm, kFigureStroke, true);
  if (mistakes >= 4) renderer.drawLine(rope, armY, rope + arm, armY + arm, kFigureStroke, true);
  if (mistakes >= 5) renderer.drawLine(rope, hip, rope - arm, hip + arm * 6 / 5, kFigureStroke, true);
  if (mistakes >= 6) renderer.drawLine(rope, hip, rope + arm, hip + arm * 6 / 5, kFigureStroke, true);

  drawClue(left + width + kClueGap, top);
}

void HangmanActivity::drawClue(const int left, const int top) const {
  char caption[24];
  snprintf(caption, sizeof(caption), tr(STR_HANGMAN_LETTERS), game.length());
  renderer.drawText(kText10Font, left, top, caption);
  char clue[hangman::MAX_CLUE_BYTES];
  kLists[game.list()].clue(game.wordIndex(), clue, sizeof(clue));
  scanword::ClueLines lines;
  scanword::layoutClue(clue, renderer.getScreenWidth() - kSidePadding - left, kClueLines, measureText, &renderer,
                       lines);
  const int lineTop = top + renderer.getLineHeight(kText10Font) + 4;
  for (uint8_t i = 0; i < lines.count; ++i) {
    renderer.drawText(kText12Font, left, lineTop + i * renderer.getLineHeight(kText12Font), lines.line[i]);
  }
}

// Letter slots on underlines; a lost round shows the missed letters framed, like the untried keys.
void HangmanActivity::drawWord(const int baseline) const {
  const int count = game.length();
  const int pitch = std::min(kSlotPitchMax, (renderer.getScreenWidth() - 2 * kSidePadding) / std::max(count, 1));
  const int slot = pitch * 4 / 5;
  const int cap = std::min(kLetterCap, pitch / 2);
  const int left = (renderer.getScreenWidth() - pitch * count + pitch - slot) / 2;
  int32_t refAdvance = 0;
  int refTop = 0;
  const bool metrics = renderer.getCodepointMetrics(kText12Font, 0x41D, kSemiBold, refAdvance, refTop) && refTop > 0;
  const bool lost = game.status() == hangman::Status::Lost;
  for (int i = 0; i < count; ++i) {
    const int x = left + i * pitch;
    renderer.fillRect(x, baseline, slot, kUnderline, true);
    if (!game.shown(i)) {
      if (!lost) continue;
      renderer.drawRect(x, baseline - kWordHeight, slot, kWordHeight, true);
    }
    const uint32_t cp = hangman::letterCodepoint(game.letter(i));
    int32_t advance = 0;
    int glyphTop = 0;
    if (!metrics || !renderer.getCodepointMetrics(kText12Font, cp, kSemiBold, advance, glyphTop)) continue;
    const int scale = cap * 256 / refTop;
    const int glyphWidth = static_cast<int>(advance * scale / (256 * 16));
    renderer.drawScaledCodepoint(kText12Font, cp, kSemiBold, x + (slot - glyphWidth) / 2, baseline - 10, scale);
  }
}

#endif  // SOVA_KEYBOARD_GAMES
