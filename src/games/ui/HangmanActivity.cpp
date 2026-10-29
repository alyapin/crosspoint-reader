#include "HangmanActivity.h"

#include <GfxRenderer.h>
#include <HangmanWords.h>
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

namespace {
constexpr int kKeysPerRow = 11;
constexpr int kKeyRows = 3;
constexpr int kKeyCount = hangman::LETTER_COUNT;
constexpr int kKeyPitch = 40;  // 11 keys between the side paddings
constexpr int kKeyWidth = 36;
constexpr int kKeyHeight = 46;
constexpr int kKeyRowPitch = 54;
constexpr int kKeysHeight = kKeyRows * kKeyRowPitch - (kKeyRowPitch - kKeyHeight);
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
constexpr int kStartKey = 15;  // О

const hangman::WordList kWordList(hangman::WORDS, hangman::WORD_COUNT);

int measureText(void* ctx, const char* text) {
  const auto* renderer = static_cast<const GfxRenderer*>(ctx);
  return renderer->getTextWidth(kText12Font, text);
}

void letterText(const uint8_t letter, char (&out)[4]) {
  const int n = hangman::putUtf8(hangman::letterCodepoint(letter), out);
  out[n] = '\0';
}
}  // namespace

const char* hangmanListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Hangman) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openHangman(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<HangmanActivity>(renderer, mappedInput);
}

HangmanActivity::HangmanActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("Hangman", renderer, mappedInput), game(kWordList) {}

bool HangmanActivity::restore() {
  if (!gamesave::load(game)) game.newGame(esp_random());
  cursor = mappedInput.hasTouch() ? -1 : kStartKey;
  barFocus = -1;
  return true;
}

void HangmanActivity::saveGame() { gamesave::save(game); }

void HangmanActivity::clearSave() { gamesave::clear(gamesave::Kind::Hangman); }

void HangmanActivity::openNewGame() { deal(); }

void HangmanActivity::deal() {
  game.newGame(esp_random());
  cursor = mappedInput.hasTouch() ? -1 : kStartKey;
  barFocus = -1;
  hasGame = true;
  markChanged();
}

// --- input -------------------------------------------------------------------

void HangmanActivity::runAction(const Action action) {
  switch (action) {
    case Action::Hint:
      if (game.hint()) markChanged();
      break;
    case Action::Menu:
      if (game.isOver()) {
        deal();
      } else {
        openGameMenu();
      }
      break;
    case Action::None:
      break;
  }
}

void HangmanActivity::guessKey(const int key) {
  if (key < 0 || key >= kKeyCount) return;
  const hangman::Guess result = game.guess(static_cast<uint8_t>(key + 1));
  if (result != hangman::Guess::Hit && result != hangman::Guess::Miss) return;
  // Buttons: the next word is one Confirm away.
  if (game.isOver() && !mappedInput.hasTouch()) barFocus = static_cast<int>(Action::Menu);
  markChanged();
}

void HangmanActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  switch (event.action) {
    case ACTION_BOARD: {
      const int column = std::clamp((x - kSidePadding) / kKeyPitch, 0, kKeysPerRow - 1);
      const int row = std::clamp((y - layout().keysTop) / kKeyRowPitch, 0, kKeyRows - 1);
      guessKey(row * kKeysPerRow + column);
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
    const int column = cursor % kKeysPerRow;
    const int row = cursor / kKeysPerRow;
    if (confirm) {
      guessKey(cursor);
    } else if (dx != 0) {
      cursor = row * kKeysPerRow + (column + dx + kKeysPerRow) % kKeysPerRow;
    } else if (dy < 0 && row > 0) {
      cursor -= kKeysPerRow;
    } else if (dy > 0 && row < kKeyRows - 1) {
      cursor += kKeysPerRow;
    } else if (dy > 0) {
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

// Gallows, word and keyboard with even gaps down to the bar; the gallows give way on a short screen.
HangmanActivity::Layout HangmanActivity::layout() const {
  const int room = barTop() - BOARD_TOP - kWordHeight - kKeysHeight;
  const int gallows = std::min(kGallowsMax, room - 4 * kGapMin);
  const int gap = (room - gallows) * 2 / 7;
  Layout l;
  l.keysTop = barTop() - gap - kKeysHeight;
  l.wordBaseline = l.keysTop - gap;
  l.gallowsBottom = l.wordBaseline - kWordHeight - gap;
  l.gallowsTop = l.gallowsBottom - gallows;
  return l;
}

// --- render task -------------------------------------------------------------

void HangmanActivity::build(UiScreen& screen) {
  char value[32];
  const char* label = nullptr;
  if (game.status() == hangman::Status::Won) {
    snprintf(value, sizeof(value), "%s", tr(STR_HANGMAN_WON));
  } else if (game.status() == hangman::Status::Lost) {
    snprintf(value, sizeof(value), "%s", tr(STR_HANGMAN_LOST));
  } else {
    label = tr(STR_HANGMAN_MISTAKES);
    snprintf(value, sizeof(value), tr(STR_HANGMAN_OF), game.mistakes(), hangman::MAX_MISTAKES);
  }
  drawTopLine(screen, label, value, false);

  const Layout l = layout();
  drawGallows(l.gallowsTop, l.gallowsBottom);
  drawWord(l.wordBaseline);
  for (int key = 0; key < kKeyCount; ++key) drawKey(key, l.keysTop);
  screen.frame().hit(fui::makeRect(kSidePadding, l.keysTop, kKeysPerRow * kKeyPitch, kKeysHeight), ACTION_BOARD, 0,
                     shellInput());

  BarItem items[BAR_SLOTS];
  const bool over = game.isOver();
  if (barShown(0)) items[0] = {&icon_bar_hint_24, tr(STR_HANGMAN_HINT)};
  items[2] = {over ? &icon_bar_new_game_24 : &icon_menu_24, over ? tr(STR_GAMES_NEW_GAME) : tr(STR_GAMES_MENU)};
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
  renderer.drawText(kCaptionFont, left, top, caption);
  char clue[hangman::MAX_CLUE_BYTES];
  kWordList.clue(game.wordIndex(), clue, sizeof(clue));
  scanword::ClueLines lines;
  scanword::layoutClue(clue, renderer.getScreenWidth() - kSidePadding - left, kClueLines, measureText, &renderer,
                       lines);
  const int lineTop = top + renderer.getLineHeight(kCaptionFont) + 4;
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

// Untried keys framed, right ones black, wrong ones dithered without the letter.
void HangmanActivity::drawKey(const int key, const int top) const {
  const int x = kSidePadding + key % kKeysPerRow * kKeyPitch + (kKeyPitch - kKeyWidth) / 2;
  const int y = top + key / kKeysPerRow * kKeyRowPitch;
  const auto letter = static_cast<uint8_t>(key + 1);
  const hangman::Key state = game.key(letter);
  bool black = true;
  switch (state) {
    case hangman::Key::Right:
      renderer.fillRect(x, y, kKeyWidth, kKeyHeight, true);
      black = false;
      break;
    case hangman::Key::Wrong:
      renderer.fillRectDither(x, y, kKeyWidth, kKeyHeight, Color::LightGray);
      break;
    case hangman::Key::Untried:
      renderer.drawRect(x, y, kKeyWidth, kKeyHeight, true);
      break;
  }
  if (state != hangman::Key::Wrong) {
    char text[4];
    letterText(letter, text);
    int32_t advance = 0;
    int capTop = 0;
    if (!renderer.getCodepointMetrics(kText10Font, 0x41D, kSemiBold, advance, capTop)) capTop = 0;
    const int textY = y + (kKeyHeight + capTop) / 2 - renderer.getFontAscenderSize(kText10Font);
    renderer.drawText(kText10Font, x + (kKeyWidth - renderer.getTextWidth(kText10Font, text, kSemiBold)) / 2, textY,
                      text, black, kSemiBold);
  }
  if (key != cursor || barFocus >= 0 || game.isOver()) return;
  if (state == hangman::Key::Right) {
    renderer.drawRect(x + 3, y + 3, kKeyWidth - 6, kKeyHeight - 6, 3, false);
  } else {
    drawCellFrame(renderer, x, y, kKeyWidth, kKeyHeight);
  }
}
