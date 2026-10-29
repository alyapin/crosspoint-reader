#include "CodewordActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <string>

#include "gameIcons.h"
#include "games/GameSave.h"
#include "games/chess/ui/chessIcons.h"

namespace fui = freeink::ui;
using namespace setka;

namespace {
constexpr const char* kIndexPath = "/codewords/index.txt";
constexpr size_t kIndexMax = 8192;
constexpr size_t kPuzzleMax = 1024;
constexpr int kCell = 41;
constexpr int kLegendRows = 2;
constexpr int kLegendRow = 36;
constexpr int kStripCells = 7;
constexpr int kStripCell = 48;
constexpr int kNumberInset = 5;
constexpr int kLetterTop = 18;  // below the cell number
constexpr int kGridCap = 16;
constexpr int kLegendCap = 12;
constexpr int kMarkSize = 6;
constexpr int kPairFrame = 2;  // the other cells of the selected number

void puzzlePath(const uint16_t id, char (&out)[24]) { snprintf(out, sizeof(out), "/codewords/%04u.cwd", id); }

// The text y that puts the top of a dense-font digit at `top`.
int digitsY(const GfxRenderer& renderer, const int top) {
  int32_t advance = 0;
  int digitTop = 0;
  if (!renderer.getCodepointMetrics(kDenseFont, '0', EpdFontFamily::REGULAR, advance, digitTop)) return top;
  return top - renderer.getFontAscenderSize(kDenseFont) + digitTop;
}

bool readText(const char* path, const size_t cap, std::string& text) {
  return Storage.exists(path) && Storage.readFileToString("GAMES", path, cap, text);
}
}  // namespace

const char* codewordListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Codeword) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openCodeword(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<CodewordActivity>(renderer, mappedInput);
}

CodewordActivity::CodewordActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("Codeword", StrId::STR_CODEWORD, renderer, mappedInput), game(&GameScreen::nowMs) {}

bool CodewordActivity::restore() {
  gamesave::load(solved);
  if (!gamesave::load(game)) return false;
  char path[24];
  puzzlePath(game.id(), path);
  std::string text;
  if (!readText(path, kPuzzleMax, text) || !game.attach(text.data(), text.size())) {
    keepSave = true;
    notice = Notice::SaveKept;
    return true;
  }
  resetView();
  return true;
}

void CodewordActivity::saveGame() {
  if (!game.attached()) return;
  if (!keepSave) gamesave::save(game);
  if (solvedPending) {
    solvedPending = false;
    solved.add(game.id());
    gamesave::save(solved);
  }
}

void CodewordActivity::clearSave() { gamesave::clear(gamesave::Kind::Codeword); }

// Entry calls this without the render lock; the menu and the bar hold it.
void CodewordActivity::openNewGame() {
  if (hasGame) {
    startNext();
    return;
  }
  RenderLock lock(*this);
  startNext();
  hasGame = true;
}

void CodewordActivity::startNext() {
  std::string text;
  auto ids = makeUniqueNoThrow<uint16_t[]>(codeword::MAX_ID);
  int count = 0;
  if (ids && readText(kIndexPath, kIndexMax, text)) {
    count = std::max(0, codeword::parseIndex(text.data(), text.size(), ids.get(), codeword::MAX_ID));
  }
  uint16_t current = game.id();
  bool missing = true;
  for (int tries = 0; tries < count && missing; ++tries) {
    const uint16_t id = codeword::nextPuzzle(ids.get(), count, solved, current);
    char path[24];
    puzzlePath(id, path);
    missing = !readText(path, kPuzzleMax, text) || !game.start(id, text.data(), text.size());
    current = id;
  }
  if (missing) {
    game.pause();
    notice = Notice::NoPack;
    requestRepaint(RepaintEntry);
    return;
  }
  notice = Notice::None;
  resetView();
  game.resume();
  markChanged();
}

// The cursor on the first number still without a letter.
void CodewordActivity::resetView() {
  const codeword::Puzzle& p = game.puzzle();
  cursor = 0;
  for (uint8_t cell = 0; cell < p.cellCount(); ++cell) {
    const uint8_t number = p.number(cell);
    if (number == 0) continue;
    if (p.number(cursor) == 0) cursor = cell;
    if (game.letter(number) == 0) {
      cursor = cell;
      break;
    }
  }
  select(cursor);
  stripOpen = false;
  barFocus = -1;
  mistakes = -1;
  solvedPending = false;
}

// --- play ----------------------------------------------------------------------

void CodewordActivity::select(const uint8_t cell) {
  cursor = cell;
  if (const uint8_t held = game.letter(selected())) {
    pick = held;
  } else if (!stripFree(pick)) {
    pick = stripStep(pick, 1);
  }
}

void CodewordActivity::place(const uint8_t letter) {
  if (!game.set(selected(), letter)) return;
  pick = letter;
  afterChange();
}

void CodewordActivity::afterChange() {
  mistakes = -1;
  stripOpen = false;
  if (game.solved()) {
    solvedPending = true;
    barFocus = -1;
  }
  if (!stripFree(pick)) pick = stripStep(pick, 1);
  markChanged();
}

void CodewordActivity::runAction(const Action action) {
  switch (action) {
    case Action::Hint:
      if (game.hint(selected())) {
        pick = game.letter(selected());
        afterChange();
      }
      break;
    case Action::Undo:
      if (game.undo()) afterChange();
      break;
    case Action::Menu:
      if (game.solved() || notice != Notice::None) {
        openNewGame();
      } else {
        openGameMenu();
      }
      break;
  }
}

int CodewordActivity::menuRows(GamePickerActivity::Row (&rows)[MENU_ROWS]) const {
  if (notice != Notice::None || game.solved()) return 0;
  int count = 0;
  if (game.opened() > game.puzzle().givenCount()) {
    menuItems[count] = MenuItem::Check;
    rows[count++] = {tr(STR_CODEWORD_CHECK), &icon_circle_check_24};
  }
  if (game.letter(selected()) != 0 && !game.locked(selected())) {
    menuItems[count] = MenuItem::Erase;
    rows[count++] = {tr(STR_CODEWORD_ERASE), &icon_bar_erase_24};
  }
  return count;
}

void CodewordActivity::menuChosen(const int row) {
  if (row < 0 || row >= static_cast<int>(std::size(menuItems))) return;
  switch (menuItems[row]) {
    case MenuItem::Check:
      mistakes = game.check();
      markChanged();
      break;
    case MenuItem::Erase:
      if (game.set(selected(), 0)) afterChange();
      break;
  }
}

bool CodewordActivity::barShown(const int slot) const {
  if (notice != Notice::None) return notice == Notice::SaveKept && slot == static_cast<int>(Action::Menu);
  switch (static_cast<Action>(slot)) {
    case Action::Hint:
      return game.canHint(selected());
    case Action::Undo:
      return game.canUndo();
    case Action::Menu:
      return true;
  }
  return false;
}

int CodewordActivity::nextBarSlot(const int from, const int step) const {
  for (int i = 1; i <= BAR_SLOTS; ++i) {
    const int slot = (from + step * i + 2 * BAR_SLOTS) % BAR_SLOTS;
    if (barShown(slot)) return slot;
  }
  return from;
}

// A letter held by another number is not in the strip.
bool CodewordActivity::stripFree(const uint8_t letter) const {
  const uint8_t holder = game.holder(letter);
  return letter >= 1 && letter <= codeword::LETTER_COUNT && (holder == 0 || holder == selected());
}

int CodewordActivity::stripCount() const {
  int count = 0;
  for (uint8_t l = 1; l <= codeword::LETTER_COUNT; ++l) count += stripFree(l);
  return count;
}

uint8_t CodewordActivity::stripStep(uint8_t from, const int step) const {
  const int dir = step < 0 ? -1 : 1;
  for (int moved = 0; moved < std::abs(step);) {
    from = static_cast<uint8_t>((from - 1 + dir + codeword::LETTER_COUNT) % codeword::LETTER_COUNT + 1);
    if (stripFree(from)) ++moved;
  }
  return from;
}

void CodewordActivity::moveCursor(const int dx, const int dy) {
  const codeword::Puzzle& p = game.puzzle();
  const int width = p.width();
  const int cells = p.cellCount();
  if (dx != 0) {
    int cell = cursor;
    do {
      cell = (cell + dx + cells) % cells;
    } while (p.number(static_cast<uint8_t>(cell)) == 0);
    select(static_cast<uint8_t>(cell));
    return;
  }
  const int column = cursor % width;
  for (int row = cursor / width + dy; row >= 0 && row < p.height(); row += dy) {
    int best = -1;
    for (int c = 0; c < width; ++c) {
      const int cell = row * width + c;
      if (p.number(static_cast<uint8_t>(cell)) != 0 &&
          (best < 0 || std::abs(c - column) < std::abs(best % width - column))) {
        best = cell;
      }
    }
    if (best >= 0) {
      select(static_cast<uint8_t>(best));
      return;
    }
  }
  if (dy > 0 && hasBar()) barFocus = nextBarSlot(BAR_SLOTS - 1, 1);
}

// --- input -------------------------------------------------------------------

void CodewordActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  if (notice != Notice::None && event.action != ACTION_BAR) return;
  const codeword::Puzzle& p = game.puzzle();
  switch (event.action) {
    case ACTION_BOARD: {
      const Layout l = layout();
      const int column = std::clamp((x - l.left) / kCell, 0, p.width() - 1);
      const int row = std::clamp((y - BOARD_TOP) / kCell, 0, p.height() - 1);
      const auto cell = static_cast<uint8_t>(row * p.width() + column);
      if (p.number(cell) == 0) return;
      select(cell);
      requestRepaint(RepaintUpdate);
      break;
    }
    case ACTION_LEGEND: {
      const Layout l = layout();
      const int column = std::clamp((x - l.legendLeft) / l.legendPitch, 0, l.legendColumns - 1);
      const int row = std::clamp((y - l.legendTop) / kLegendRow, 0, kLegendRows - 1);
      const int number = row * l.legendColumns + column + 1;
      for (uint8_t cell = 0; cell < p.cellCount(); ++cell) {
        if (p.number(cell) != number) continue;
        select(cell);
        requestRepaint(RepaintUpdate);
        break;
      }
      break;
    }
    case ACTION_STRIP:
      tapStrip((x - layout().stripLeft) / kStripCell);
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

// Slot 0 and the last page the strip; the others are its letters.
void CodewordActivity::tapStrip(const int slot) {
  const int shown = std::min(kStripCells, stripCount());
  if (slot <= 0 || slot > kStripCells) {
    pick = stripStep(pick, slot <= 0 ? -shown : shown);
    requestRepaint(RepaintUpdate);
    return;
  }
  const int offset = slot - 1 - kStripCells / 2;
  if (offset >= -(shown - 1) / 2 && offset <= shown / 2 && !game.solved()) place(stripStep(pick, offset));
}

void CodewordActivity::onKey(const int dx, const int dy, const bool confirm) {
  if (notice != Notice::None) return;
  const int before[] = {cursor, pick, stripOpen, barFocus};
  if (barFocus >= 0) {
    if (confirm) {
      if (barShown(barFocus)) runAction(static_cast<Action>(barFocus));
      if (barFocus >= 0 && !barShown(barFocus)) barFocus = nextBarSlot(barFocus, 1);
    } else if (dx != 0) {
      barFocus = nextBarSlot(barFocus, dx);
    } else if (dy < 0 && !game.solved()) {
      barFocus = -1;
    }
  } else if (stripOpen) {
    if (confirm) {
      place(pick);
      stripOpen = false;
    } else if (dx != 0) {
      pick = stripStep(pick, dx);
    }
  } else if (confirm) {
    stripOpen = !game.locked(selected()) && !game.solved();
  } else {
    moveCursor(dx, dy);
  }
  const int after[] = {cursor, pick, stripOpen, barFocus};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

bool CodewordActivity::onBack() {
  if (stripOpen) {
    stripOpen = false;
  } else if (barFocus >= 0 && !game.solved()) {
    barFocus = -1;
  } else {
    return false;
  }
  requestRepaint(RepaintUpdate);
  return true;
}

// Grid, legend, a rule and the strip with even gaps down to the bar.
CodewordActivity::Layout CodewordActivity::layout() const {
  const codeword::Puzzle& p = game.puzzle();
  const int width = p.width() * kCell;
  Layout l;
  l.left = (renderer.getScreenWidth() - width) / 2;
  l.gridBottom = BOARD_TOP + p.height() * kCell;
  l.legendColumns = (p.count() + kLegendRows - 1) / kLegendRows;
  l.legendPitch = std::min(kCell, width / l.legendColumns);
  l.legendLeft = (renderer.getScreenWidth() - l.legendColumns * l.legendPitch) / 2;
  l.stripLeft = (renderer.getScreenWidth() - (kStripCells + 2) * kStripCell) / 2;
  const int gap = std::max(0, (barTop() - l.gridBottom - kLegendRows * kLegendRow - kRuleWidth - kStripCell) / 4);
  l.legendTop = l.gridBottom + gap;
  l.ruleY = l.legendTop + kLegendRows * kLegendRow + gap;
  l.stripTop = l.ruleY + kRuleWidth + gap;
  return l;
}

// --- render task -------------------------------------------------------------

void CodewordActivity::build(UiScreen& screen) {
  if (notice != Notice::None) {
    drawTopLine(screen, nullptr, nullptr, false);
    drawNotice();
    if (notice == Notice::SaveKept) {
      BarItem items[BAR_SLOTS];
      items[2] = {&icon_bar_new_game_24, tr(STR_GAMES_NEW_GAME), false, BarItem::NewGame};
      drawBar(screen, items, -1);
    }
    return;
  }
  char value[32];
  const char* label = nullptr;
  if (game.solved()) {
    label = tr(STR_CODEWORD_SOLVED_IN);
    formatElapsed(game.elapsedMs(), value, sizeof(value));
  } else if (mistakes == 0) {
    snprintf(value, sizeof(value), "%s", tr(STR_CODEWORD_ALL_RIGHT));
  } else if (mistakes > 0) {
    label = tr(STR_CODEWORD_MISTAKES);
    snprintf(value, sizeof(value), "%d", mistakes);
  } else {
    label = tr(STR_CODEWORD_OPENED);
    snprintf(value, sizeof(value), tr(STR_CODEWORD_OF), game.opened(), game.puzzle().count());
  }
  drawTopLine(screen, label, value, false);

  const Layout l = layout();
  drawGrid(screen, l);
  drawLegend(screen, l);
  if (!game.solved()) {
    drawRule(renderer, 0, l.ruleY, renderer.getScreenWidth());
    drawStrip(screen, l);
  }

  BarItem items[BAR_SLOTS];
  if (barShown(0)) items[0] = {&icon_bar_hint_24, tr(STR_CODEWORD_HINT)};
  if (barShown(1)) items[1] = {&icon_bar_undo_24, tr(STR_GAMES_UNDO)};
  items[2] = game.solved() ? BarItem{&icon_bar_new_game_24, tr(STR_GAMES_NEW_GAME), false, BarItem::NewGame}
                           : BarItem{&icon_menu_24, tr(STR_GAMES_MENU), false, BarItem::Menu};
  drawBar(screen, items, barFocus);
}

void CodewordActivity::drawGrid(UiScreen& screen, const Layout& l) const {
  const codeword::Puzzle& p = game.puzzle();
  for (uint8_t cell = 0; cell < p.cellCount(); ++cell) {
    const int x = l.left + cell % p.width() * kCell;
    const int y = BOARD_TOP + cell / p.width() * kCell;
    const uint8_t n = p.number(cell);
    if (n == 0) {
      renderer.fillRectDither(x + 1, y + 1, kCell - 1, kCell - 1, Color::LightGray);
      continue;
    }
    char text[4];
    snprintf(text, sizeof(text), "%u", n);
    renderer.drawText(kDenseFont, x + kNumberInset, digitsY(renderer, y + kNumberInset), text);
    if (const uint8_t letter = game.letter(n)) {
      drawLetter(letter, x, y + kLetterTop, kCell, kCell - kLetterTop - 2, kGridCap);
    }
    drawMarks(n, x, y, kCell, kCell);
  }
  const int right = l.left + p.width() * kCell;
  for (int c = 0; c <= p.width(); ++c) {
    renderer.drawLine(l.left + c * kCell, BOARD_TOP, l.left + c * kCell, l.gridBottom);
  }
  for (int r = 0; r <= p.height(); ++r) renderer.drawLine(l.left, BOARD_TOP + r * kCell, right, BOARD_TOP + r * kCell);
  screen.frame().hit(fui::makeRect(l.left, BOARD_TOP, right - l.left, l.gridBottom - BOARD_TOP), ACTION_BOARD, 0,
                     shellInput());

  // The selected number's cells, framed over the grid lines so the frame clears the number and the letter.
  if (game.solved()) return;
  const bool keys = !mappedInput.hasTouch() && barFocus < 0 && !stripOpen;
  for (uint8_t cell = 0; cell < p.cellCount(); ++cell) {
    if (p.number(cell) != selected()) continue;
    const int x = l.left + cell % p.width() * kCell - 1;
    const int y = BOARD_TOP + cell / p.width() * kCell - 1;
    if (cell != cursor) {
      renderer.drawRect(x, y, kCell + 3, kCell + 3, kPairFrame, true);
    } else if (keys) {
      drawCursor(x, y, kCell + 3, kCell + 3);
    } else {
      drawCellFrame(renderer, x, y, kCell + 3, kCell + 3);
    }
  }
}

// Numbers in two rows, each over its letter.
void CodewordActivity::drawLegend(UiScreen& screen, const Layout& l) const {
  const int count = game.puzzle().count();
  for (int i = 0; i < count; ++i) {
    const auto n = static_cast<uint8_t>(i + 1);
    const int x = l.legendLeft + i % l.legendColumns * l.legendPitch;
    const int y = l.legendTop + i / l.legendColumns * kLegendRow;
    renderer.drawRect(x, y, l.legendPitch + 1, kLegendRow + 1, true);
    char text[4];
    snprintf(text, sizeof(text), "%u", n);
    drawCentered(kDenseFont, x, l.legendPitch, digitsY(renderer, y + kNumberInset), text);
    if (const uint8_t letter = game.letter(n)) {
      drawLetter(letter, x, y + kLetterTop, l.legendPitch, kLegendRow - kLetterTop - 2, kLegendCap);
    }
    drawMarks(n, x, y, l.legendPitch, kLegendRow);
  }
  if (!game.solved()) {
    const int i = selected() - 1;
    drawCellFrame(renderer, l.legendLeft + i % l.legendColumns * l.legendPitch - 1,
                  l.legendTop + i / l.legendColumns * kLegendRow - 1, l.legendPitch + 3, kLegendRow + 3);
  }
  screen.frame().hit(
      fui::makeRect(l.legendLeft, l.legendTop, l.legendColumns * l.legendPitch, kLegendRows * kLegendRow),
      ACTION_LEGEND, 0, shellInput());
}

// The free letters around `pick`; the centre one framed.
void CodewordActivity::drawStrip(UiScreen& screen, const Layout& l) const {
  const int left = l.stripLeft + kStripCell;
  const int shown = std::min(kStripCells, stripCount());
  const bool framed = mappedInput.hasTouch() || stripOpen;
  for (int offset = -(shown - 1) / 2; offset <= shown / 2; ++offset) {
    const int x = left + (offset + kStripCells / 2) * kStripCell;
    drawLetter(stripStep(pick, offset), x, l.stripTop, kStripCell, kStripCell, offset == 0 ? 22 : 18);
    if (offset == 0 && framed) drawCellFrame(renderer, x, l.stripTop, kStripCell, kStripCell);
  }
  const int arrowTop = l.stripTop + (kStripCell - renderer.getLineHeight(kText12Font)) / 2;
  renderer.drawText(kText12Font, left - 24, arrowTop, "\xE2\x80\xB9");
  renderer.drawText(kText12Font, left + kStripCells * kStripCell + 12, arrowTop, "\xE2\x80\xBA");
  screen.frame().hit(fui::makeRect(l.stripLeft, l.stripTop, (kStripCells + 2) * kStripCell, kStripCell), ACTION_STRIP,
                     0, shellInput());
}

void CodewordActivity::drawNotice() const {
  const bool kept = notice == Notice::SaveKept;
  const int width = renderer.getScreenWidth() - 2 * kSidePadding;
  int y = BOARD_TOP + 40;
  renderer.drawText(kText12Font, kSidePadding, y, kept ? tr(STR_CODEWORD_SAVE_KEPT) : tr(STR_CODEWORD_NO_PACK), true,
                    kSemiBold);
  y += 2 * renderer.getLineHeight(kText12Font);
  const char* body = kept ? tr(STR_CODEWORD_SAVE_KEPT_BODY) : tr(STR_CODEWORD_NO_PACK_BODY);
  const char* key = kept && !hasBar() ? tr(STR_CODEWORD_NEXT_KEY) : nullptr;
  for (const char* text : {body, key}) {
    if (text == nullptr) continue;
    for (const auto& line : renderer.wrappedText(kText12Font, text, width, 4)) {
      renderer.drawText(kText12Font, kSidePadding, y, line.c_str());
      y += renderer.getLineHeight(kText12Font);
    }
    y += renderer.getLineHeight(kText12Font);
  }
}

// Letters are the text face's glyphs scaled to a cap height, as in the scanwords.
void CodewordActivity::drawLetter(const uint8_t letter, const int x, const int y, const int width, const int height,
                                  const int capHeight) const {
  const uint32_t cp = codeword::letterCodepoint(letter);
  int32_t advance = 0;
  int top = 0;
  int32_t refAdvance = 0;
  int refTop = 0;
  if (cp == 0 || !renderer.getCodepointMetrics(kText12Font, cp, kSemiBold, advance, top) ||
      !renderer.getCodepointMetrics(kText12Font, 0x41D, kSemiBold, refAdvance, refTop) || refTop <= 0) {
    return;
  }
  const int scale = capHeight * 256 / refTop;
  const int glyphWidth = static_cast<int>(advance * scale / (256 * 16));
  renderer.drawScaledCodepoint(kText12Font, cp, kSemiBold, x + (width - glyphWidth) / 2, y + (height + capHeight) / 2,
                               scale);
}

// A checked wrong letter is struck through; a hint leaves a square in the corner.
void CodewordActivity::drawMarks(const uint8_t number, const int x, const int y, const int width,
                                 const int height) const {
  if (game.wrong(number)) renderer.drawLine(x + 6, y + height - 6, x + width - 6, y + 6, 2, true);
  if (game.hinted(number)) {
    renderer.fillRect(x + width - kMarkSize - 4, y + height - kMarkSize - 4, kMarkSize, kMarkSize, true);
  }
}
