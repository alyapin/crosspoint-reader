#include "NonogramActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "ElapsedText.h"
#include "activities/util/ConfirmationActivity.h"
#include "gameIcons.h"
#include "games/GameSave.h"
#include "games/chess/ui/chessIcons.h"

namespace fui = freeink::ui;
using namespace setka;
using nonogram::Mark;

namespace {
constexpr const char* PACK_PATH = "/nonograms/pictures.txt";
constexpr int kSmallCell = 38;  // 10x10
constexpr int kLargeCell = 25;  // 15x15
constexpr int kClueGap = 6;     // between the clues and the grid
constexpr int kNumberPad = 6;   // a row clue number's cell beyond its widest digit
constexpr int kEdge = 8;        // the board is full-bleed
constexpr int kNoticeGap = 10;
constexpr int kBoardGap = 8;  // above the bar rule
constexpr int kClueFrame = 2;
constexpr int kStroke = 2;

// Ids run 1..255, so a valid pack of 15x15 pictures stays under 75 KB.
constexpr size_t kPackMax = 96 * 1024;

// One pass over the pack for one side: the first unsolved picture after
// `after`, else the first unsolved, else the first after `after`, else the
// first. It stops at the first picture of the best kind.
struct PackScan {
  int side = 0;
  uint8_t after = 0;
  const nonogram::Record* record = nullptr;
  int rank = 4;  // 4: none found
  nonogram::Picture pick;
  nonogram::PackParser parser{&PackScan::onPicture, this};
  char chunk[512];

  static bool onPicture(void* ctx, const nonogram::Picture& p) {
    auto* s = static_cast<PackScan*>(ctx);
    if (p.side != s->side) return true;
    const bool later = p.id > s->after;
    const int rank = (s->record->isSolved(p.id) ? 2 : 0) + (later ? 0 : 1);
    if (rank < s->rank) {
      s->rank = rank;
      s->pick = p;
    }
    return rank != 0;
  }
};
}  // namespace

const char* nonogramListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Nonogram) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openNonogram(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<NonogramActivity>(renderer, mappedInput);
}

NonogramActivity::NonogramActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("Nonogram", StrId::STR_NONOGRAM, renderer, mappedInput), game(&GameScreen::nowMs) {}

bool NonogramActivity::restore() {
  gamesave::load(record);
  if (!gamesave::load(game)) return false;
  keyCursor = !mappedInput.hasTouch();
  cursor = keyCursor ? game.side() / 2 * game.side() + game.side() / 2 : -1;
  return true;
}

void NonogramActivity::saveGame() {
  gamesave::save(game);
  if (recordPending) {
    recordPending = false;
    gamesave::save(record);
  }
}

void NonogramActivity::resumeGame() {
  if (notice == Notice::None) game.resume();
}

void NonogramActivity::clearSave() { gamesave::clear(gamesave::Kind::Nonogram); }

void NonogramActivity::openNewGame() {
  const GamePickerActivity::Row rows[] = {
      {tr(STR_NONOGRAM_PICTURES_10), &icon_pictures_24},
      {tr(STR_NONOGRAM_PICTURES_15), &icon_pictures_24},
      {tr(STR_NONOGRAM_RANDOM_10), &icon_random_24},
      {tr(STR_NONOGRAM_RANDOM_15), &icon_random_24},
  };
  int current = 0;
  if (game.hasPuzzle()) current = (game.source() == nonogram::Source::Random ? 2 : 0) + (game.side() > 10 ? 1 : 0);
  openPicker(tr(STR_GAMES_NEW_GAME), rows, static_cast<int>(std::size(rows)), current);
}

void NonogramActivity::pickerChosen(const int row, bool) {
  const int side = row % 2 == 0 ? 10 : 15;
  noticeSide = side;
  if (row < 2) {
    notice = loadPicture(side);
  } else {
    notice = Notice::NoRandom;
    for (int i = 0; i < 3 && notice != Notice::None; ++i) {
      if (game.newRandom(side, esp_random())) notice = Notice::None;
    }
  }
  crossMode = false;
  stroking = false;
  checkShown = false;
  newBest = false;
  keyCursor = !mappedInput.hasTouch();
  cursor = keyCursor && game.hasPuzzle() ? game.side() / 2 * game.side() + game.side() / 2 : -1;
  barFocus = keyCursor && !boardShown() && hasBar() ? BAR_SLOTS - 1 : -1;
}

NonogramActivity::Notice NonogramActivity::loadPicture(const int side) {
  HalFile file;
  if (!Storage.exists(PACK_PATH) || !Storage.openFileForRead("NONO", PACK_PATH, file)) return Notice::NoPack;
  if (file.fileSize() > kPackMax) {
    LOG_ERR("NONO", "Pack too large: %u bytes", static_cast<unsigned>(file.fileSize()));
    return Notice::NoPack;
  }
  auto scan = makeUniqueNoThrow<PackScan>();
  if (!scan) {
    LOG_ERR("NONO", "OOM: %u bytes for the pack scan", static_cast<unsigned>(sizeof(PackScan)));
    return Notice::NoPack;
  }
  scan->side = side;
  scan->record = &record;
  if (game.hasPuzzle() && game.source() == nonogram::Source::Picture && game.side() == side) {
    scan->after = game.pictureId();
  }
  while (true) {
    const int n = file.read(scan->chunk, sizeof(scan->chunk));
    if (n < 0) return Notice::NoPack;
    if (n == 0 || !scan->parser.feed(scan->chunk, static_cast<size_t>(n))) break;
  }
  if (!scan->parser.finish()) {
    LOG_ERR("NONO", "Bad pack %s", PACK_PATH);
    return Notice::NoPack;
  }
  if (scan->rank == 4) return Notice::NoSize;
  const nonogram::Picture& p = scan->pick;
  return game.newPicture(p.side, p.bits, p.id, p.name) ? Notice::None : Notice::NoPack;
}

int NonogramActivity::menuRows(GamePickerActivity::Row (&rows)[MENU_ROWS]) const {
  if (!boardShown() || !game.inProgress()) return 0;
  rows[0] = {tr(STR_NONOGRAM_CHECK), &icon_circle_check_24};
  rows[1] = {tr(STR_GAMES_RESTART), &icon_bar_restart_24};
  return 2;
}

void NonogramActivity::menuChosen(const int row) {
  if (row == 0) {
    checkShown = true;
    requestRepaint(RepaintUpdate);
  } else if (row == 1) {
    confirmRestart();
  }
}

void NonogramActivity::confirmRestart() {
  auto confirm =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_GAMES_RESTART_CONFIRM),
                                              tr(STR_NONOGRAM_RESTART_BODY), tr(STR_CANCEL), tr(STR_GAMES_RESTART));
  if (!confirm) {
    LOG_ERR("NONOGRAM", "OOM: restart confirmation, the board stays");
    return;
  }
  app.clearTapFlash();
  pauseGame();
  startActivityForResult(std::move(confirm), [this](const ActivityResult& result) {
    RenderLock lock(*this);
    if (!result.isCancelled) {
      game.restart();
      checkShown = false;
      stroking = false;
      markChanged();
    }
    resumeGame();
    requestRepaint(RepaintEntry);
  });
}

// --- input -------------------------------------------------------------------

void NonogramActivity::apply(const int cell, const Mark mark) {
  if (!game.set(cell, mark)) return;
  checkShown = false;
  if (game.isSolved()) {
    newBest = record.recordTime(game.source(), game.side(), game.elapsedMs());
    if (game.source() == nonogram::Source::Picture) record.markSolved(game.pictureId());
    recordPending = true;
    if (keyCursor && hasBar()) barFocus = BAR_SLOTS - 1;
  }
  markChanged();
}

bool NonogramActivity::barShown(const int slot) const {
  // Крестик (touch) and Отменить while playing, Меню (Новая игра once solved or without a board) always.
  if (slot == 0) return hasBar() && boardShown() && game.inProgress();
  if (slot == 1) return boardShown() && game.canUndo();
  return slot == 2;
}

int NonogramActivity::nextBarSlot(const int from, const int step) const {
  for (int i = 1; i <= BAR_SLOTS; ++i) {
    const int slot = (from + step * i + 2 * BAR_SLOTS) % BAR_SLOTS;
    if (barShown(slot)) return slot;
  }
  return from;
}

void NonogramActivity::runBar(const int slot) {
  if (!barShown(slot)) return;
  if (slot == 0) {
    crossMode = !crossMode;
    requestRepaint(RepaintUpdate);
  } else if (slot == 1) {
    if (game.undo()) {
      checkShown = false;
      markChanged();
    }
  } else if (boardShown() && game.inProgress()) {
    openGameMenu();
  } else {
    openNewGame();
  }
}

void NonogramActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  if (event.action == ACTION_BAR) {
    if (event.value >= 0 && event.value < BAR_SLOTS) runBar(event.value);
    return;
  }
  if (event.action != ACTION_BOARD || !boardShown()) return;
  const Layout l = layout();
  const int n = game.side();
  const int column = (x - l.gridLeft) / l.cell;
  const int row = (y - l.gridTop) / l.cell;
  if (x < l.gridLeft || y < l.gridTop || column >= n || row >= n) return;
  const int cell = row * n + column;
  const int before[] = {cursor, keyCursor ? 1 : 0, barFocus};
  cursor = cell;
  keyCursor = false;
  barFocus = -1;
  const Mark current = game.mark(cell);
  if (crossMode || event.longPress) {
    apply(cell, current == nonogram::Crossed ? nonogram::Empty : nonogram::Crossed);
  } else {
    apply(cell, current == nonogram::Filled ? nonogram::Empty : nonogram::Filled);
  }
  const int after[] = {cursor, keyCursor ? 1 : 0, barFocus};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

void NonogramActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int n = game.side();
  const int before[] = {cursor, keyCursor ? 1 : 0, barFocus};
  if (!boardShown()) {
    if (!hasBar()) return;  // Confirm runs Новая игра (GameScreen)
    barFocus = BAR_SLOTS - 1;
    if (confirm) runBar(barFocus);
  } else if (barFocus >= 0) {
    if (confirm) {
      runBar(barFocus);
      if (barFocus >= 0 && !barShown(barFocus)) barFocus = nextBarSlot(barFocus, 1);
    } else if (dx != 0) {
      barFocus = nextBarSlot(barFocus, dx);
    } else if (dy < 0) {
      barFocus = -1;
    }
  } else if (cursor < 0 || !keyCursor) {
    if (cursor < 0) cursor = n / 2 * n + n / 2;
    keyCursor = true;
  } else if (stroking) {
    if (confirm) {
      stroking = false;
      requestRepaint(RepaintUpdate);
    } else if (dx != 0 || dy != 0) {
      const int y = cursor / n + dy;
      if (dx != 0) {
        cursor = (cursor + dx + n * n) % (n * n);
      } else if (y >= 0 && y < n) {
        cursor += dy * n;
      }
      if (game.mark(cursor) == nonogram::Empty) apply(cursor, strokeMark);
      stroking = game.inProgress();
    }
  } else {
    const int y = cursor / n;
    if (confirm) {
      const Mark m = game.mark(cursor);
      if (crossMode) {
        apply(cursor, m == nonogram::Crossed ? nonogram::Empty : nonogram::Crossed);
      } else {
        apply(cursor, m == nonogram::Empty    ? nonogram::Filled
                      : m == nonogram::Filled ? nonogram::Crossed
                                              : nonogram::Empty);
      }
    } else if (dx != 0) {
      cursor = (cursor + dx + n * n) % (n * n);
    } else if (dy < 0 && y > 0) {
      cursor -= n;
    } else if (dy > 0 && y < n - 1) {
      cursor += n;
    } else if (dy > 0 && hasBar()) {
      barFocus = nextBarSlot(BAR_SLOTS - 1, 1);
    }
  }
  const int after[] = {cursor, keyCursor ? 1 : 0, barFocus};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

const char* NonogramActivity::confirmHint() const {
  if (stroking) return "";
  if (!boardShown() || !game.inProgress() || barFocus >= 0 || cursor < 0) return nullptr;
  return tr(STR_NONOGRAM_MARK);
}

bool NonogramActivity::holdsConfirm() const {
  return !hasBar() && boardShown() && game.inProgress() && keyCursor && cursor >= 0 && barFocus < 0 && !stroking;
}

// The line puts what a press puts on an empty first cell (a fill, a cross in Крестик mode) and carries on
// the mark of a marked one.
void NonogramActivity::onConfirmHold() {
  const Mark first = game.mark(cursor);
  if (first == nonogram::Empty) {
    strokeMark = crossMode ? nonogram::Crossed : nonogram::Filled;
  } else {
    strokeMark = first;
  }
  stroking = true;
  if (game.mark(cursor) != strokeMark) {
    apply(cursor, strokeMark);
  } else {
    requestRepaint(RepaintUpdate);
  }
}

bool NonogramActivity::onBack() {
  if (stroking) {
    stroking = false;
    requestRepaint(RepaintUpdate);
    return true;
  }
  if (barFocus < 0 || !boardShown()) return false;
  barFocus = -1;
  requestRepaint(RepaintUpdate);
  return true;
}

int32_t NonogramActivity::shownMinutes() const {
  return games::ui::shownMinute(boardShown() && game.inProgress(), game.elapsedMs());
}

// GameScreen::loop() repaints the timer when its minute turns; a stopped clock never turns.
uint32_t NonogramActivity::idleSleepBudgetMs() const {
  return hasGame && !game.paused() ? games::ui::msUntilNextMinute(game.elapsedMs()) : light_sleep::kNoDeadline;
}

// --- layout ------------------------------------------------------------------

int NonogramActivity::numberWidth(const int font, const char* text, const int numberCell) const {
  return std::max(numberCell, renderer.getTextWidth(font, text) + kNumberPad);
}

int NonogramActivity::rowClueWidth(const nonogram::Clue& clue, const int font, const int numberCell) const {
  if (clue.count == 0) return numberWidth(font, "0", numberCell);
  int width = 0;
  for (int i = 0; i < clue.count; ++i) {
    char text[4];
    snprintf(text, sizeof(text), "%u", clue.runs[i]);
    width += numberWidth(font, text, numberCell);
  }
  return width;
}

NonogramActivity::Layout NonogramActivity::layout() const {
  const int n = game.side();
  Layout l{};
  l.font = n <= 10 ? kText10Font : kDenseFont;
  l.pitch = renderer.getLineHeight(l.font);
  for (char digit = '0'; digit <= '9'; ++digit) {
    const char text[2] = {digit, '\0'};
    l.numberCell = std::max(l.numberCell, renderer.getTextWidth(l.font, text) + kNumberPad);
  }
  int clueWidth = 0;
  int clueLines = 1;
  for (int i = 0; i < n; ++i) {
    clueWidth = std::max(clueWidth, rowClueWidth(game.rowClue(i), l.font, l.numberCell));
    clueLines = std::max<int>(clueLines, game.columnClue(i).count);
  }
  const int width = renderer.getScreenWidth();
  const int captionHeight =
      games::ui::kCaptionGap + renderer.getLineHeight(kText12Font) + renderer.getLineHeight(kText10Font);
  const int roomX = width - 2 * kEdge - clueWidth - kClueGap;
  const int roomY = barTop() - kBoardGap - captionHeight - BOARD_TOP - clueLines * l.pitch - kClueGap;
  l.cell = std::max(8, std::min({n <= 10 ? kSmallCell : kLargeCell, roomX / n, roomY / n}));
  l.clueLeft = (width - clueWidth - kClueGap - n * l.cell) / 2;
  l.gridLeft = l.clueLeft + clueWidth + kClueGap;
  l.clueTop = BOARD_TOP;
  l.gridTop = BOARD_TOP + clueLines * l.pitch + kClueGap;
  return l;
}

// --- render task -------------------------------------------------------------

void NonogramActivity::build(UiScreen& screen) {
  if (!boardShown()) {
    drawTopLine(screen, {}, false);
    const int width = renderer.getScreenWidth();
    char text[96];
    if (notice == Notice::NoSize) {
      snprintf(text, sizeof(text), tr(STR_NONOGRAM_NO_SIZE), noticeSide, noticeSide);
    } else {
      snprintf(text, sizeof(text), "%s",
               notice == Notice::NoRandom ? tr(STR_NONOGRAM_NO_RANDOM) : tr(STR_NONOGRAM_NO_PACK));
    }
    int y = BOARD_TOP + kNoticeGap;
    for (const auto& line : renderer.wrappedText(kText12Font, text, width - 2 * kSidePadding, 4)) {
      renderer.drawText(kText12Font, kSidePadding, y, line.c_str());
      y += renderer.getLineHeight(kText12Font);
    }
  } else {
    char size[16];
    snprintf(size, sizeof(size), "%d \xC3\x97 %d", game.side(), game.side());
    char clock[24];
    formatElapsed(game.elapsedMs(), clock, sizeof(clock));
    drawTopLine(screen, {{size, nullptr}, {nullptr, clock}}, false);

    const Layout l = layout();
    drawClues(l);
    drawGrid(l);
    const int span = game.side() * l.cell;
    screen.frame().hit(fui::makeRect(l.gridLeft, l.gridTop, span, span), ACTION_BOARD, 0,
                       static_cast<uint16_t>(fui::InputTouch | fui::InputLongPress));
    drawCaption(l.gridTop + span + 1);  // the grid's last rule ends on its bottom row
  }

  BarItem items[BAR_SLOTS];
  if (barShown(0)) items[0] = {&icon_bar_cross_24, tr(STR_NONOGRAM_CROSS), crossMode, BarItem::Mode};
  if (barShown(1)) items[1] = {&icon_bar_undo_24, tr(STR_GAMES_UNDO), false, BarItem::Action};
  items[2] = boardShown() && game.inProgress()
                 ? BarItem{&icon_menu_24, tr(STR_GAMES_MENU), false, BarItem::Menu}
                 : BarItem{&icon_bar_new_game_24, tr(STR_GAMES_NEW_GAME), false, BarItem::NewGame};
  drawBar(screen, items, barFocus);
}

void NonogramActivity::drawClues(const Layout& l) const {
  const int n = game.side();
  const int strikeY = l.pitch / 2 + 1;
  const bool framed = cursor >= 0 && keyCursor && barFocus < 0 && game.inProgress();
  char text[4];
  for (int row = 0; row < n; ++row) {
    const nonogram::Clue& clue = game.rowClue(row);
    const uint8_t done = game.rowDone(row);
    const int y = l.gridTop + row * l.cell + (l.cell - l.pitch) / 2;
    int x = l.gridLeft - kClueGap;
    for (int i = std::max<int>(clue.count, 1) - 1; i >= 0; --i) {
      snprintf(text, sizeof(text), "%u", clue.count == 0 ? 0u : clue.runs[i]);
      const int w = renderer.getTextWidth(l.font, text);
      const int cell = numberWidth(l.font, text, l.numberCell);
      x -= cell;
      const int textX = x + (cell - w) / 2;
      renderer.drawText(l.font, textX, y, text);
      if (done & (1u << i)) renderer.fillRect(textX - 2, y + strikeY, w + 4, kStroke, true);
    }
  }
  for (int column = 0; column < n; ++column) {
    const nonogram::Clue& clue = game.columnClue(column);
    const uint8_t done = game.columnDone(column);
    const int x = l.gridLeft + column * l.cell;
    int y = l.gridTop - kClueGap - l.pitch;
    for (int i = std::max<int>(clue.count, 1) - 1; i >= 0; --i) {
      snprintf(text, sizeof(text), "%u", clue.count == 0 ? 0u : clue.runs[i]);
      drawCentered(l.font, x, l.cell, y, text);
      if (done & (1u << i)) {
        const int w = renderer.getTextWidth(l.font, text);
        renderer.fillRect(x + (l.cell - w) / 2 - 2, y + strikeY, w + 4, kStroke, true);
      }
      y -= l.pitch;
    }
  }
  if (framed) {
    const int row = cursor / n;
    const int column = cursor % n;
    renderer.drawRect(l.clueLeft - 2 * kClueFrame, l.gridTop + row * l.cell, l.gridLeft - l.clueLeft + 2 * kClueFrame,
                      l.cell + 1, kClueFrame, true);
    renderer.drawRect(l.gridLeft + column * l.cell, l.clueTop - kClueFrame, l.cell + 1,
                      l.gridTop - l.clueTop + kClueFrame, kClueFrame, true);
  }
}

void NonogramActivity::drawGrid(const Layout& l) const {
  const int n = game.side();
  const int size = l.cell;
  const int span = n * size;
  const bool solved = game.isSolved();
  for (int cell = 0; cell < n * n; ++cell) {
    const int x = l.gridLeft + cell % n * size;
    const int y = l.gridTop + cell / n * size;
    const Mark m = game.mark(cell);
    if (m == nonogram::Filled) {
      renderer.fillRect(x, y, size, size, true);
    } else if (m == nonogram::Crossed && !solved) {
      const int inset = size / 4;
      renderer.drawLine(x + inset, y + inset, x + size - inset, y + size - inset, kStroke, true);
      renderer.drawLine(x + inset, y + size - inset, x + size - inset, y + inset, kStroke, true);
    }
    if (checkShown && game.isWrong(cell)) {
      renderer.drawRect(x + 3, y + 3, size - 5, size - 5, 3, m != nonogram::Filled);
    }
  }
  for (int i = 0; i <= n; ++i) {
    const int width = i % 5 == 0 || i == n ? kStroke : 1;
    const int offset = width == kStroke ? 1 : 0;
    renderer.fillRect(l.gridLeft + i * size - offset, l.gridTop - offset, width, span + 2 * offset + 1, true);
    renderer.fillRect(l.gridLeft - offset, l.gridTop + i * size - offset, span + 2 * offset + 1, width, true);
  }
  // A tapped cell is framed only while white; the key cursor shows on any cell, in white on a filled one.
  const bool filled = cursor >= 0 && game.mark(cursor) == nonogram::Filled;
  if (cursor >= 0 && barFocus < 0 && !solved && (keyCursor || !filled)) {
    renderer.drawRect(l.gridLeft + cursor % n * size, l.gridTop + cursor / n * size, size + 1, size + 1,
                      kCellFrameWidth, !filled);
  }
}

void NonogramActivity::drawCaption(const int edge) const {
  char line[96];
  if (game.isSolved()) {
    char clock[24];
    formatElapsed(game.elapsedMs(), clock, sizeof(clock));
    char solvedIn[48];
    snprintf(solvedIn, sizeof(solvedIn), tr(STR_NONOGRAM_SOLVED), clock);
    const bool picture = game.source() == nonogram::Source::Picture;
    char best[48];
    // The record opens the line of a random puzzle, and follows the time after a picture.
    if (newBest) {
      snprintf(best, sizeof(best), "%s", picture ? tr(STR_NONOGRAM_NEW_BEST) : tr(STR_NONOGRAM_NEW_BEST_LINE));
    } else {
      formatElapsed(record.bestMs(game.source(), game.side()), clock, sizeof(clock));
      snprintf(best, sizeof(best), picture ? tr(STR_NONOGRAM_BEST) : tr(STR_NONOGRAM_BEST_LINE), clock);
    }
    if (picture) {
      snprintf(line, sizeof(line), "%s \xC2\xB7 %s", solvedIn, best);
    } else {
      snprintf(line, sizeof(line), "%s", best);
    }
    drawResult(games::ui::captionTop(renderer, edge, kText12Font), picture ? game.name() : solvedIn, line);
    return;
  }
  if (checkShown) {
    const int mistakes = game.mistakes();
    if (mistakes == 0) {
      snprintf(line, sizeof(line), "%s", tr(STR_NONOGRAM_NO_ERRORS));
    } else {
      snprintf(line, sizeof(line), tr(STR_NONOGRAM_ERRORS), mistakes);
    }
    renderer.drawText(kText10Font, kSidePadding, games::ui::captionTop(renderer, edge, kText10Font), line, true,
                      kSemiBold);
    return;
  }
  const char* hint = mappedInput.hasTouch()
                         ? (crossMode ? tr(STR_NONOGRAM_CROSS_HINT) : tr(STR_NONOGRAM_TAP_HINT))
                         : (stroking ? tr(STR_NONOGRAM_KEYS_STROKE_HINT) : tr(STR_NONOGRAM_KEYS_HINT));
  if (hint != captionFor) {
    captionFor = hint;
    // The keys caption names the Confirm hint.
    char text[160];
    snprintf(text, sizeof(text), hint, tr(STR_NONOGRAM_MARK), tr(STR_NONOGRAM_MARK));
    // A line break in the text starts the second line.
    captionLines.clear();
    for (char* part = text; part != nullptr && captionLines.size() < 2;) {
      char* next = strchr(part, '\n');
      if (next != nullptr) *next++ = '\0';
      for (auto& wrapped : renderer.wrappedText(kText10Font, part, renderer.getScreenWidth() - 2 * kSidePadding,
                                                static_cast<int>(2 - captionLines.size()))) {
        captionLines.push_back(std::move(wrapped));
      }
      part = next;
    }
  }
  int y = games::ui::captionTop(renderer, edge, kText10Font);
  for (const auto& text : captionLines) {
    renderer.drawText(kText10Font, kSidePadding, y, text.c_str());
    y += renderer.getLineHeight(kText10Font);
  }
}
