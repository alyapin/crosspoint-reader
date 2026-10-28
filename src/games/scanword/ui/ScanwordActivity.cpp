#include "ScanwordActivity.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <ScanwordClueLayout.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "components/HeaderBackTapTarget.h"
#include "components/UITheme.h"
#include "games/scanword/ScanwordStore.h"
#include "scanwordIcons.h"

namespace fui = freeink::ui;
using namespace setka;
using scanword::CellKind;
using scanword::EntryResult;
using scanword::Word;

namespace {
constexpr fui::ActionId ACTION_BACK = 1;
constexpr fui::ActionId ACTION_ROW = 2;
constexpr fui::ActionId ACTION_GRID = 3;
constexpr fui::ActionId ACTION_KEY = 4;
constexpr fui::ActionId ACTION_BOX = 5;
constexpr fui::ActionId ACTION_ERASE = 6;
constexpr fui::ActionId ACTION_CHECK = 7;
constexpr fui::ActionId ACTION_HINT = 8;
constexpr fui::ActionId ACTION_DONE = 9;
constexpr fui::ActionId ACTION_CHECK_ALL = 10;

constexpr int kHeaderTop = 16;  // board top line; clears the X4 Pro top bezel
constexpr int kGridTop = 50;
constexpr int kBandMin = 90;  // below the grid: check-all, clue bar, letter strip
constexpr int kRowHeight = 68;
constexpr int kRowGap = 8;
constexpr int kIconSize = 24;
constexpr int kIconGap = 16;
constexpr int kFooterHeight = 56;
constexpr int kStripHeight = 96;
constexpr int kKeyHeight = 50;
constexpr int kActionHeight = 50;
constexpr int kKeyRows = 3;
constexpr int kKeysPerRow = 11;
constexpr int kBoxSize = 44;
constexpr int kBoxGap = 4;
constexpr int kClueBarHeight = 40;
constexpr int kPickerCells = 7;
constexpr int kPickerCell = 48;
constexpr int kMarkSize = 6;
constexpr unsigned long kMenuHoldMs = 600;
constexpr int kWordFrameWidth = 3;  // outline of the selected answer
constexpr int kClueFrameWidth = 2;  // its clue cell
// Every hit takes a long press too: routeTouch() consumes it, so a slow tap
// would otherwise be lost. Only «Буква» gives it a meaning of its own.
const uint16_t kTap = fui::InputTouch | fui::InputLongPress;

// ЙЦУКЕН rows as letter codes (А=1 … Я=33, Ё=7).
constexpr uint8_t kKeys[kKeyRows][kKeysPerRow] = {
    {11, 24, 21, 12, 6, 15, 4, 26, 27, 9, 23},
    {22, 29, 3, 1, 17, 18, 16, 13, 5, 8, 31},
    {33, 25, 19, 14, 10, 20, 30, 2, 32, 7, 28},
};

// Clue cells set the caption face one pixel tighter, so a 60 px cell takes a seven-letter line.
constexpr int8_t kClueTracking = -1;

struct Measure {
  const GfxRenderer* renderer;
  int font;
  EpdFontFamily::Style style;
  int8_t tracking;
};

int measureText(void* ctx, const char* text) {
  const auto* m = static_cast<const Measure*>(ctx);
  return m->renderer->getTextAdvanceX(m->font, text, m->style, m->tracking);
}

uint32_t nowMs() { return static_cast<uint32_t>(millis()); }

int minutes(const uint32_t seconds) { return static_cast<int>((seconds + 59) / 60); }

// "5 букв": Russian picks the form by the number; English uses the last two the same.
void lettersLabel(const int n, char* out, const size_t cap) {
  const int mod10 = n % 10;
  const int mod100 = n % 100;
  StrId id = StrId::STR_SCANWORD_LETTERS_MANY;
  if (mod10 == 1 && mod100 != 11) {
    id = StrId::STR_SCANWORD_LETTERS_ONE;
  } else if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) {
    id = StrId::STR_SCANWORD_LETTERS_FEW;
  }
  snprintf(out, cap, I18N.get(id), n);
}

// "Сканворд 12" from id "0012"; other ids are shown as they are.
void puzzleName(const char* id, char* out, const size_t cap) {
  const char* shown = id;
  bool digits = *id != '\0';
  for (const char* p = id; *p != '\0'; ++p) digits = digits && *p >= '0' && *p <= '9';
  if (digits) {
    while (shown[0] == '0' && shown[1] != '\0') ++shown;
  }
  snprintf(out, cap, tr(STR_SCANWORD_NAMED), shown);
}

void drawRightAligned(const GfxRenderer& renderer, const int font, const int right, const int y, const char* text,
                      const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  renderer.drawText(font, right - renderer.getTextWidth(font, text, style), y, text, true, style);
}

void drawIcon(UiAppHost::UiScreen& screen, const freeink::Icon& icon, const int x, const int y) {
  screen.target().bitmap(fui::makeRect(x, y, kIconSize, kIconSize), fui::bitmapFromIcon(icon), fui::BitmapMode::Center);
}

}  // namespace

const char* scanwordListStatus() {
  return scanwordstore::hasPuzzleInProgress() ? tr(STR_SCANWORD_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openScanwords(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<ScanwordActivity>(renderer, mappedInput);
}

ScanwordActivity::ScanwordActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Scanwords", renderer, mappedInput), UiAppHost(renderer) {}

// --- lifecycle ----------------------------------------------------------------

void ScanwordActivity::onEnter() {
  // Keep the render task out until the state below is in place.
  RenderLock lock(*this);
  Activity::onEnter();
  resetUi();
  app.setScreen(&ScanwordActivity::screenFn, this);
  // Allocated once for the whole visit: ~6 KB puzzle, ~6 KB list summary, ~5 KB index.
  puzzle = makeUniqueNoThrow<scanword::Puzzle>();
  shelf = makeUniqueNoThrow<scanword::Shelf>();
  items = makeUniqueNoThrow<ListItem[]>(kMaxPuzzles);
  order = makeUniqueNoThrow<uint16_t[]>(kMaxPuzzles);
  if (puzzle) game = makeUniqueNoThrow<scanword::Game>(*puzzle);
  if (!puzzle || !shelf || !items || !order || !game) {
    LOG_ERR("SCANW", "OOM: %u bytes for the scanword screen",
            static_cast<unsigned>(sizeof(scanword::Puzzle) + sizeof(scanword::Shelf) + sizeof(scanword::Game) +
                                  kMaxPuzzles * (sizeof(ListItem) + sizeof(uint16_t))));
    game.reset();
    puzzle.reset();
    shelf.reset();
    items.reset();
    order.reset();
    finish();
    return;
  }
  // A file that exists but does not read is kept untouched for this visit.
  shelfWritable = scanwordstore::loadShelf(*shelf);
  statsWritable = scanwordstore::loadStats(stats);
  loadList();
  sortList();
  setView(View::List);
}

void ScanwordActivity::onExit() {
  if (openItem >= 0) {
    tickTime();
    saveProgress();
  }
  game.reset();
  puzzle.reset();
  shelf.reset();
  items.reset();
  order.reset();
  Activity::onExit();
}

void ScanwordActivity::prepareForSleep() {
  if (openItem < 0) return;
  tickTime();
  saveProgress();
}

void ScanwordActivity::requestRepaint(const Repaint kind) {
  uint8_t current = repaint.load();
  while (current < kind && !repaint.compare_exchange_weak(current, kind)) {
  }
  requestUpdate();
}

void ScanwordActivity::setView(const View next) {
  view = next;
  if (next == View::Menu) focus = 0;
  checkResult = -1;
  requestRepaint(RepaintEntry);
}

// --- list -----------------------------------------------------------------------

void ScanwordActivity::loadList() {
  struct Ctx {
    ListItem* items;
    uint16_t count;
  } ctx{items.get(), 0};
  scanwordstore::readIndex(
      [](void* p, const scanword::IndexEntry& e) {
        auto* c = static_cast<Ctx*>(p);
        if (c->count >= kMaxPuzzles) return false;
        ListItem& item = c->items[c->count++];
        memcpy(item.id, e.id, sizeof(item.id));
        item.level = e.level;
        return true;
      },
      &ctx);
  itemCount = ctx.count;
}

ScanwordActivity::Status ScanwordActivity::statusOf(const ListItem& item) const {
  const scanword::ShelfEntry* e = shelf->find(item.id);
  if (e == nullptr) return Status::New;
  return e->solved ? Status::Solved : Status::Started;
}

void ScanwordActivity::sortList() {
  uint16_t n = 0;
  for (const Status group : {Status::Started, Status::New, Status::Solved}) {
    for (uint16_t i = 0; i < itemCount; ++i) {
      if (statusOf(items[i]) == group) order[n++] = i;
    }
  }
}

int ScanwordActivity::rowsPerPage() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  return std::max(1, (renderer.getScreenHeight() - kFooterHeight - top) / (kRowHeight + kRowGap));
}

void ScanwordActivity::openPuzzle(const uint16_t item) {
  loadFailed = false;
  if (!scanwordstore::loadPuzzle(items[item].id, *puzzle)) {
    loadFailed = true;
    requestRepaint(RepaintUpdate);
    return;
  }
  const char* id = items[item].id;
  game->reset();
  if (!scanwordstore::loadProgress(id, *game) && scanwordstore::hasProgress(id)) {
    // A read error may pass; opening a fresh game would overwrite the save on exit.
    loadFailed = true;
    requestRepaint(RepaintUpdate);
    return;
  }
  if (!scanwordstore::hasProgress(id) && shelf->find(id) != nullptr) {
    // The summary outlived its save (e.g. a new pack reused the id).
    shelf->remove(id);
    if (shelfWritable) scanwordstore::saveShelf(*shelf);
  }
  openItem = item;
  dirty = false;
  savedSeconds = game->progress().seconds;
  keyboardOpen = false;
  stripOpen = false;
  checkResult = -1;
  pickLetter = 1;
  lastTickMs = nowMs();
  carryMs = 0;
  app.clearTapFlash();
  setView(View::Board);
}

void ScanwordActivity::closePuzzle() {
  tickTime();
  saveProgress();
  const int closed = openItem;
  openItem = -1;
  keyboardOpen = false;
  stripOpen = false;
  sortList();
  for (uint16_t i = 0; i < itemCount; ++i) {
    if (order[i] == closed) focus = i;
  }
  app.clearTapFlash();
  setView(View::List);
}

// --- play -------------------------------------------------------------------------

bool ScanwordActivity::touchBoard() const { return mappedInput.hasTouch(); }

void ScanwordActivity::tickTime() {
  const uint32_t now = nowMs();
  if (openItem >= 0 && !game->solved()) {
    carryMs += now - lastTickMs;
    if (carryMs >= 1000) {
      game->addSeconds(carryMs / 1000);
      carryMs %= 1000;
    }
  }
  lastTickMs = now;
}

void ScanwordActivity::saveProgress() {
  if (openItem < 0) return;
  const char* id = items[openItem].id;
  const uint32_t seconds = game->progress().seconds;
  const bool untouched = shelf->find(id) == nullptr && game->filledCount() == 0 && !dirty;
  if (!untouched && (dirty || seconds != savedSeconds)) {
    if (scanwordstore::saveProgress(id, *game)) {
      dirty = false;
      savedSeconds = seconds;
    }
    const uint16_t letters = puzzle->letterCount();
    const uint32_t percent = letters ? game->filledCount() * 100u / letters : 0;
    shelf->set(id, static_cast<uint8_t>(percent), game->solved(), seconds);
    if (shelfWritable) scanwordstore::saveShelf(*shelf);
  }
  if (statsPending) {
    if (statsWritable) scanwordstore::saveStats(stats);
    statsPending = false;
  }
}

void ScanwordActivity::tapGrid(const int x, const int y) {
  const uint16_t cell = cellAt(x, y);
  if (cell >= puzzle->cellCount()) return;
  // With the keyboard closed no answer is shown, so a tap only selects.
  const bool selecting = touchBoard() && !keyboardOpen;
  if (!game->tapCell(cell, !selecting)) return;
  if (touchBoard() && !game->solved()) keyboardOpen = true;
  checkResult = -1;
  requestRepaint(RepaintUpdate);
}

void ScanwordActivity::typeLetter(const uint8_t letter) {
  const uint8_t selected = game->selectedWord();
  if (selected == scanword::NO_WORD) return;
  const Word& w = puzzle->word(selected);
  // enter() skips revealed cells: find the cell it types into and whether one follows it.
  uint8_t pos = 0;
  while (pos < w.length && puzzle->cellOf(w, pos) != game->cursor()) ++pos;
  while (pos < w.length && (game->flags(puzzle->cellOf(w, pos)) & scanword::FLAG_REVEALED)) ++pos;
  bool atEnd = true;
  for (uint8_t next = pos + 1; next < w.length && atEnd; ++next) {
    atEnd = (game->flags(puzzle->cellOf(w, next)) & scanword::FLAG_REVEALED) != 0;
  }
  afterEntry(game->enter(letter), atEnd);
}

void ScanwordActivity::afterEntry(const EntryResult result, const bool wordWasLast) {
  if (result == EntryResult::Ignored) return;
  dirty = true;
  checkResult = -1;
  if (result == EntryResult::Solved) {
    keyboardOpen = false;
    stripOpen = false;
    stats.record(game->progress());
    statsPending = true;
    savePending = true;
  } else if (wordWasLast && game->wordFilled(game->selectedWord())) {
    closeInput();
  }
  requestRepaint(RepaintUpdate);
}

// Closes the keyboard or the letter strip and saves what changed.
void ScanwordActivity::closeInput() {
  keyboardOpen = false;
  stripOpen = false;
  checkResult = -1;
  savePending = dirty;
}

void ScanwordActivity::showCheck(const uint16_t wrong, const bool wholeGrid) {
  checkResult = static_cast<int16_t>(wrong);
  checkedGrid = wholeGrid;
  dirty = dirty || wrong > 0;  // wrong letters are marked
  requestRepaint(RepaintUpdate);
}

bool ScanwordActivity::wordHasEntry() const {
  const uint8_t selected = game->selectedWord();
  if (selected == scanword::NO_WORD) return false;
  const Word& w = puzzle->word(selected);
  for (uint8_t i = 0; i < w.length; ++i) {
    if (game->entry(puzzle->cellOf(w, i)) != scanword::NO_LETTER) return true;
  }
  return false;
}

// Menu rows that do something right now.
int ScanwordActivity::menuItems(MenuItem* out) const {
  int n = 0;
  const bool hasEntry = wordHasEntry();
  if (hasEntry) out[n++] = MenuItem::CheckWord;
  if (game->filledCount() > 0) out[n++] = MenuItem::CheckAll;
  out[n++] = MenuItem::RevealLetter;
  out[n++] = MenuItem::RevealWord;
  if (hasEntry) out[n++] = MenuItem::Erase;
  if (game->canUndo()) out[n++] = MenuItem::Undo;
  out[n++] = MenuItem::ToList;
  return n;
}

void ScanwordActivity::runMenu(const MenuItem item) {
  switch (item) {
    case MenuItem::CheckWord:
      setView(View::Board);
      showCheck(game->checkWord(), false);
      break;
    case MenuItem::CheckAll:
      setView(View::Board);
      showCheck(game->checkAll(), true);
      break;
    case MenuItem::RevealLetter:
      setView(View::Board);
      afterEntry(game->revealLetter(), false);
      savePending = dirty;
      break;
    case MenuItem::RevealWord:
      setView(View::Board);
      afterEntry(game->revealWord(), false);
      savePending = dirty;
      break;
    case MenuItem::Erase:
      setView(View::Board);
      dirty = game->erase() || dirty;
      break;
    case MenuItem::Undo:
      setView(View::Board);
      dirty = game->undo() || dirty;
      break;
    case MenuItem::ToList:
      closePuzzle();
      break;
  }
}

// --- loop task --------------------------------------------------------------------

void ScanwordActivity::loop() {
  if (!game) return;
  if (view != View::List) tickTime();

  if (const auto touch = routeTouch(mappedInput, true)) {
    RenderLock lock(*this);
    handleAction(touch.event, touch.snap.touchX, touch.snap.touchY);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    RenderLock lock(*this);
    goBack();
  } else if (view == View::List) {
    handleListKeys();
  } else if (view == View::Board) {
    handleBoardKeys();
  } else {
    handleMenuKeys();
  }

  if (savePending) {
    savePending = false;
    saveProgress();
  }
}

void ScanwordActivity::goBack() {
  switch (view) {
    case View::List:
      app.clearTapFlash();
      finish();
      break;
    case View::Menu:
      setView(View::Board);
      break;
    case View::Board:
      if (stripOpen || keyboardOpen) {
        closeInput();
        requestRepaint(RepaintUpdate);
      } else {
        closePuzzle();
      }
      break;
  }
}

void ScanwordActivity::handleAction(const fui::ActionEvent& event, const int x, const int y) {
  // Taps route against the last published frame, which can be one state behind:
  // act only when the tapped control is still on screen.
  const bool board = view == View::Board;
  const bool input = board && keyboardOpen;
  switch (event.action) {
    case ACTION_BACK:
      if (board) {
        closePuzzle();  // "‹ Выйти" leaves even with the keyboard open
      } else {
        goBack();
      }
      break;
    case ACTION_ROW:
      if (view == View::List && event.value < itemCount) {
        focus = event.value;
        openPuzzle(order[event.value]);
      } else if (view == View::Menu) {
        MenuItem rows[kMaxMenuItems];
        if (event.value < menuItems(rows)) runMenu(rows[event.value]);
      }
      break;
    case ACTION_GRID:
      if (board) tapGrid(x, y);
      break;
    case ACTION_KEY: {
      const int row = (y - (keyboardTop() + kStripHeight)) / kKeyHeight;
      const int col = x * kKeysPerRow / renderer.getScreenWidth();
      if (input && row >= 0 && row < kKeyRows && col >= 0 && col < kKeysPerRow) typeLetter(kKeys[row][col]);
      break;
    }
    case ACTION_BOX:
      // The hit carries the box pitch.
      if (input && event.value > 0) {
        const Word& w = puzzle->word(game->selectedWord());
        const int pos = (x - kSidePadding) / event.value;
        if (pos >= 0 && pos < w.length) {
          game->tapCell(puzzle->cellOf(w, static_cast<uint8_t>(pos)), false);
          requestRepaint(RepaintUpdate);
        }
      }
      break;
    case ACTION_ERASE:
      if (input && game->erase()) {
        dirty = true;
        checkResult = -1;
        requestRepaint(RepaintUpdate);
      }
      break;
    case ACTION_CHECK:
      if (input && wordHasEntry()) showCheck(game->checkWord(), false);
      break;
    case ACTION_HINT:
      if (input) {
        afterEntry(event.longPress ? game->revealWord() : game->revealLetter(), false);
        savePending = dirty;
      }
      break;
    case ACTION_DONE:
      if (input) {
        closeInput();
        requestRepaint(RepaintUpdate);
      }
      break;
    case ACTION_CHECK_ALL:
      if (board && !keyboardOpen && game->filledCount() > 0) showCheck(game->checkAll(), true);
      break;
    default:
      break;
  }
}

void ScanwordActivity::handleListKeys() {
  using Button = MappedInputManager::Button;
  if (itemCount == 0) return;
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    RenderLock lock(*this);
    const int per = rowsPerPage();
    const int pages = (itemCount + per - 1) / per;
    int page = focus / per + (swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
    page = std::clamp(page, 0, pages - 1);
    focus = page * per;
    loadFailed = false;
    requestRepaint(RepaintUpdate);
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    RenderLock lock(*this);
    openPuzzle(order[focus]);
    return;
  }
  const int count = itemCount;
  buttonNavigator.onNextPress([this, count] {
    RenderLock lock(*this);
    focus = ButtonNavigator::nextIndex(focus, count);
    loadFailed = false;
    requestRepaint(RepaintUpdate);
  });
  buttonNavigator.onPreviousPress([this, count] {
    RenderLock lock(*this);
    focus = ButtonNavigator::previousIndex(focus, count);
    loadFailed = false;
    requestRepaint(RepaintUpdate);
  });
}

void ScanwordActivity::handleMenuKeys() {
  MenuItem rows[kMaxMenuItems];
  const int count = menuItems(rows);
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    RenderLock lock(*this);
    runMenu(rows[std::min(focus, count - 1)]);
    return;
  }
  buttonNavigator.onNextPress([this, count] {
    RenderLock lock(*this);
    focus = ButtonNavigator::nextIndex(focus, count);
    requestRepaint(RepaintUpdate);
  });
  buttonNavigator.onPreviousPress([this, count] {
    RenderLock lock(*this);
    focus = ButtonNavigator::previousIndex(focus, count);
    requestRepaint(RepaintUpdate);
  });
}

// Buttons. Side buttons pick the answer, Left/Right the cell, Confirm opens the
// letter strip (Left/Right choose, Confirm types and moves on, Back closes it),
// a long Confirm opens the action menu. On touch boards Left/Right step through
// the answers with the keyboard open.
void ScanwordActivity::handleBoardKeys() {
  using Button = MappedInputManager::Button;
  if (game->solved()) return;
  const bool left = mappedInput.wasPressed(Button::Left);
  const bool right = mappedInput.wasPressed(Button::Right);
  const bool up = mappedInput.wasPressed(Button::Up);
  const bool down = mappedInput.wasPressed(Button::Down);

  if (touchBoard()) {
    if (!left && !right) return;
    RenderLock lock(*this);
    if (keyboardOpen) game->nextWord(right ? 1 : -1);
    keyboardOpen = true;
    checkResult = -1;
    requestRepaint(RepaintUpdate);
    return;
  }

  if (mappedInput.wasLongPressed(Button::Confirm, kMenuHoldMs)) {
    RenderLock lock(*this);
    stripOpen = false;
    setView(View::Menu);
    return;
  }
  if (stripOpen) {
    if (mappedInput.wasReleased(Button::Confirm)) {
      RenderLock lock(*this);
      typeLetter(pickLetter);
      return;
    }
    // Held keys keep stepping: the strip holds 33 letters.
    const auto step = [this](const int delta) {
      RenderLock lock(*this);
      pickLetter = static_cast<uint8_t>((pickLetter - 1 + delta + scanword::LETTER_COUNT) % scanword::LETTER_COUNT + 1);
      requestRepaint(RepaintUpdate);
    };
    buttonNavigator.onPressAndContinuous({Button::Left, Button::Up}, [&step] { step(-1); });
    buttonNavigator.onPressAndContinuous({Button::Right, Button::Down}, [&step] { step(1); });
    return;
  }

  if (mappedInput.wasReleased(Button::Confirm)) {
    RenderLock lock(*this);
    stripOpen = true;
    if (const uint8_t entry = game->entry(game->cursor())) pickLetter = entry;
    requestRepaint(RepaintUpdate);
    return;
  }
  if (up || down) {
    RenderLock lock(*this);
    game->nextWord(down ? 1 : -1);
    checkResult = -1;
    requestRepaint(RepaintUpdate);
    return;
  }
  if (left || right) {
    RenderLock lock(*this);
    const Word& w = puzzle->word(game->selectedWord());
    int pos = 0;
    while (pos < w.length && puzzle->cellOf(w, static_cast<uint8_t>(pos)) != game->cursor()) ++pos;
    pos += right ? 1 : -1;
    if (pos < 0 || pos >= w.length) {
      game->nextWord(right ? 1 : -1);
    } else {
      game->tapCell(puzzle->cellOf(w, static_cast<uint8_t>(pos)), false);
    }
    checkResult = -1;
    requestRepaint(RepaintUpdate);
  }
}

// --- geometry -------------------------------------------------------------------

int ScanwordActivity::cellSize() const {
  const int areaHeight = renderer.getScreenHeight() - kBandMin - kGridTop;
  return std::min(renderer.getScreenWidth() / puzzle->width(), areaHeight / puzzle->height());
}

int ScanwordActivity::gridLeft() const { return (renderer.getScreenWidth() - puzzle->width() * cellSize()) / 2; }

int ScanwordActivity::gridBottom() const { return kGridTop + puzzle->height() * cellSize(); }

int ScanwordActivity::keyboardTop() const {
  return renderer.getScreenHeight() - kStripHeight - kKeyRows * kKeyHeight - kActionHeight;
}

uint16_t ScanwordActivity::cellAt(const int x, const int y) const {
  const int size = cellSize();
  const int col = (x - gridLeft()) / size;
  const int row = (y - kGridTop) / size;
  if (x < gridLeft() || y < kGridTop || col >= puzzle->width() || row >= puzzle->height()) return scanword::MAX_CELLS;
  return static_cast<uint16_t>(row * puzzle->width() + col);
}

void ScanwordActivity::cellOrigin(const uint16_t cell, int& x, int& y) const {
  const int size = cellSize();
  x = gridLeft() + (cell % puzzle->width()) * size;
  y = kGridTop + (cell / puzzle->width()) * size;
}

// --- render task ------------------------------------------------------------------

void ScanwordActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (view == View::Board) {
    HeaderBackTapTarget::clear();  // the board has its own "‹ Выйти"
  } else {
    const auto& metrics = UITheme::getInstance().getMetrics();
    char page[16] = "";
    const int per = rowsPerPage();
    const int pages = (itemCount + per - 1) / per;
    if (view == View::List && pages > 1) snprintf(page, sizeof(page), "%d/%d", focus / per + 1, pages);
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   view == View::List ? tr(STR_SCANWORDS) : tr(STR_SCANWORD_MENU), page[0] ? page : nullptr);
  }
  renderUi();
  const uint8_t kind = repaint.exchange(RepaintNone);
  renderer.displayBuffer(kind == RepaintEntry ? cadence.entry() : cadence.update());
}

void ScanwordActivity::screenFn(UiScreen& screen, void* user) { static_cast<ScanwordActivity*>(user)->build(screen); }

void ScanwordActivity::build(UiScreen& screen) {
  if (!game) return;
  switch (view) {
    case View::List:
      buildList(screen);
      break;
    case View::Board:
      buildBoard(screen);
      break;
    case View::Menu:
      buildMenu(screen);
      break;
  }
}

void ScanwordActivity::drawRow(UiScreen& screen, const int top, const int index, const freeink::Icon* icon,
                               const char* label, const char* status) const {
  const int width = renderer.getScreenWidth();
  const bool focused = index == focus;
  if (focused) drawSelectionBar(renderer, kSelectionBarX, top, kRowHeight);
  int x = kRowTextX;
  if (icon != nullptr) {
    drawIcon(screen, *icon, x, top + (kRowHeight - kIconSize) / 2);
    x += kIconSize + kIconGap;
  }
  const int textTop = top + (kRowHeight - renderer.getLineHeight(kText12Font)) / 2;
  renderer.drawText(kText12Font, x, textTop, label, true, focused ? kSemiBold : EpdFontFamily::REGULAR);
  if (status != nullptr && *status != '\0') {
    // Shares the label's baseline.
    const int statusTop =
        textTop + renderer.getFontAscenderSize(kText12Font) - renderer.getFontAscenderSize(kText10Font);
    drawRightAligned(renderer, kText10Font, width - kSidePadding, statusTop, status);
  }
  screen.frame().hit(fui::makeRect(0, top, width, kRowHeight), ACTION_ROW, static_cast<int16_t>(index), kTap);
}

void ScanwordActivity::buildList(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  if (itemCount == 0) {
    int y = listTop + kRowGap;
    for (const auto& line : renderer.wrappedText(kText12Font, tr(STR_SCANWORD_EMPTY), width - 2 * kSidePadding, 6)) {
      renderer.drawText(kText12Font, kSidePadding, y, line.c_str());
      y += renderer.getLineHeight(kText12Font);
    }
    return;
  }
  const int per = rowsPerPage();
  const int first = focus / per * per;
  const int last = std::min<int>(first + per, itemCount);
  for (int i = first; i < last; ++i) {
    const ListItem& item = items[order[i]];
    char label[40];
    puzzleName(item.id, label, sizeof(label));
    char status[40] = "";
    const freeink::Icon* icon = &icon_scanword_new_24;
    const scanword::ShelfEntry* e = shelf->find(item.id);
    if (e != nullptr && e->solved) {
      icon = &icon_scanword_solved_24;
      snprintf(status, sizeof(status), tr(STR_SCANWORD_SOLVED_IN), minutes(e->seconds));
    } else if (e != nullptr) {
      icon = &icon_scanword_24;
      snprintf(status, sizeof(status), tr(STR_SCANWORD_PROGRESS), e->percent);
    } else {
      const StrId levels[] = {StrId::STR_SCANWORD_EASY, StrId::STR_SCANWORD_MEDIUM, StrId::STR_SCANWORD_HARD};
      snprintf(status, sizeof(status), "%s", I18N.get(levels[std::clamp<int>(item.level, 1, 3) - 1]));
    }
    drawRow(screen, listTop + (i - first) * (kRowHeight + kRowGap), i, icon, label, status);
  }

  const int footerTop = height - kFooterHeight;
  drawRule(renderer, 0, footerTop, width);
  const int textTop = footerTop + (kFooterHeight - renderer.getLineHeight(kText10Font)) / 2;
  char left[64];
  if (loadFailed) {
    snprintf(left, sizeof(left), "%s", tr(STR_SCANWORD_LOAD_FAILED));
  } else {
    int solved = 0;
    for (uint16_t i = 0; i < itemCount; ++i) solved += statusOf(items[i]) == Status::Solved ? 1 : 0;
    snprintf(left, sizeof(left), tr(STR_SCANWORD_SOLVED_COUNT), solved, itemCount);
  }
  renderer.drawText(kText10Font, kSidePadding, textTop, left);
  if (!loadFailed && stats.bestSeconds > 0) {
    char best[48];
    snprintf(best, sizeof(best), tr(STR_SCANWORD_BEST), minutes(stats.bestSeconds));
    drawRightAligned(renderer, kText10Font, width - kSidePadding, textTop, best);
  }
}

void ScanwordActivity::buildMenu(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  MenuItem rows[kMaxMenuItems];
  const int count = menuItems(rows);
  for (int i = 0; i < count; ++i) {
    const freeink::Icon* icon = &icon_scanword_to_list_24;
    StrId label = StrId::STR_SCANWORD_TO_LIST;
    switch (rows[i]) {
      case MenuItem::CheckWord:
        icon = &icon_scanword_check_24;
        label = StrId::STR_SCANWORD_CHECK_WORD;
        break;
      case MenuItem::CheckAll:
        icon = &icon_scanword_check_all_24;
        label = StrId::STR_SCANWORD_CHECK_ALL;
        break;
      case MenuItem::RevealLetter:
        icon = &icon_scanword_hint_24;
        label = StrId::STR_SCANWORD_REVEAL_LETTER;
        break;
      case MenuItem::RevealWord:
        icon = &icon_scanword_reveal_word_24;
        label = StrId::STR_SCANWORD_REVEAL_WORD;
        break;
      case MenuItem::Erase:
        icon = &icon_scanword_erase_24;
        label = StrId::STR_SCANWORD_ERASE;
        break;
      case MenuItem::Undo:
        icon = &icon_scanword_undo_24;
        label = StrId::STR_SCANWORD_UNDO;
        break;
      case MenuItem::ToList:
        break;
    }
    drawRow(screen, listTop + i * (kRowHeight + kRowGap), i, icon, I18N.get(label), nullptr);
  }
}

void ScanwordActivity::buildBoard(UiScreen& screen) {
  drawTopLine(screen);
  const bool keyboard = touchBoard() && keyboardOpen;
  drawGrid(screen, keyboard ? keyboardTop() : gridBottom());
  if (keyboard) {
    drawKeyboard(screen);
  } else {
    drawBand(screen);
  }
}

void ScanwordActivity::drawTopLine(UiScreen& screen) const {
  const int width = renderer.getScreenWidth();
  const char* exitLabel = tr(STR_SCANWORD_EXIT);
  renderer.drawText(kText12Font, kSidePadding, kHeaderTop, exitLabel);
  const int exitWidth = renderer.getTextWidth(kText12Font, exitLabel) + 2 * kSidePadding;
  screen.frame().hit(fui::makeRect(0, 0, exitWidth, kGridTop), ACTION_BACK, 0, kTap);
  char count[16];
  snprintf(count, sizeof(count), "%u/%u", static_cast<unsigned>(game->filledCount()),
           static_cast<unsigned>(puzzle->letterCount()));
  drawRightAligned(renderer, kText12Font, width - kSidePadding, kHeaderTop, count, kSemiBold);
}

// Letters are the text face's glyphs scaled to a cap height, so no large font is needed.
void ScanwordActivity::drawLetter(const uint8_t letter, const int x, const int y, const int width, const int height,
                                  const int capHeight) const {
  const uint32_t cp = scanword::letterCodepoint(letter);
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

void ScanwordActivity::drawGrid(UiScreen& screen, const int visibleBottom) const {
  const int size = cellSize();
  const int left = gridLeft();
  const int right = left + puzzle->width() * size;
  const int bottom = gridBottom();
  for (uint16_t cell = 0; cell < puzzle->cellCount(); ++cell) {
    int x = 0;
    int y = 0;
    cellOrigin(cell, x, y);
    if (y < visibleBottom) drawCell(cell, x, y);
  }
  const int lastX = std::min(right, renderer.getScreenWidth() - 1);
  for (int c = 0; c <= puzzle->width(); ++c) {
    const int x = std::min(left + c * size, lastX);
    renderer.drawLine(x, kGridTop, x, bottom);
  }
  for (int r = 0; r <= puzzle->height(); ++r) renderer.drawLine(left, kGridTop + r * size, lastX, kGridTop + r * size);
  const bool showSelection = (!touchBoard() || keyboardOpen) && !game->solved();
  if (showSelection) drawSelection();
  screen.frame().hit(fui::makeRect(left, kGridTop, right - left, std::min(bottom, visibleBottom) - kGridTop),
                     ACTION_GRID, 0, kTap);
}

void ScanwordActivity::drawCell(const uint16_t cell, const int x, const int y) const {
  const int size = cellSize();
  switch (puzzle->kind(cell)) {
    case CellKind::Blank:
      renderer.fillRectDither(x + 1, y + 1, size - 1, size - 1, Color::LightGray);
      return;
    case CellKind::Clue:
      drawClueCell(cell, x, y);
      return;
    case CellKind::Letter:
      break;
  }
  if (const uint8_t entry = game->entry(cell)) drawLetter(entry, x, y, size, size, size / 2);
  const uint8_t flags = game->flags(cell);
  if (flags & scanword::FLAG_WRONG) renderer.drawLine(x + 6, y + size - 6, x + size - 6, y + 6, 2, true);
  if (flags & scanword::FLAG_REVEALED) {
    renderer.fillRect(x + size - kMarkSize - 4, y + size - kMarkSize - 4, kMarkSize, kMarkSize, true);
  }
  drawArrows(cell, x, y);
}

void ScanwordActivity::drawClueCell(const uint16_t cell, const int x, const int y) const {
  const int size = cellSize();
  uint8_t ids[2];
  const uint8_t n = puzzle->cluesAt(cell, ids);
  const int halfHeight = n == 2 ? size / 2 : size;
  // Three lines in a cell, two per half of a two-clue cell, at a pitch that fits.
  const int font = kCaptionFont;
  const uint8_t maxLines = n == 2 ? 2 : scanword::CLUE_MAX_LINES;
  const int lineHeight = std::min(renderer.getLineHeight(font), (halfHeight - 2) / maxLines);
  Measure measure{&renderer, font, EpdFontFamily::REGULAR, kClueTracking};
  for (uint8_t i = 0; i < n; ++i) {
    const std::string_view clue = puzzle->clue(ids[i]);
    char text[scanword::MAX_CLUE_BYTES + 1];
    const size_t len = std::min(clue.size(), sizeof(text) - 1);
    memcpy(text, clue.data(), len);
    text[len] = '\0';
    scanword::ClueLines lines;
    scanword::layoutClue(text, size - 3, maxLines, measureText, &measure, lines);
    // Glyphs hang from the line top; centre the lines' ink, not their boxes.
    const int ink = (lines.count - 1) * lineHeight + renderer.getFontAscenderSize(font);
    const int top = y + i * halfHeight + (halfHeight - ink) / 2 - (renderer.getLineHeight(font) - lineHeight) / 2;
    for (uint8_t l = 0; l < lines.count; ++l) {
      const int w = measureText(&measure, lines.line[l]);
      renderer.drawText(font, x + (size - w) / 2 + 1, top + l * lineHeight, lines.line[l], true, EpdFontFamily::REGULAR,
                        BidiUtils::BidiBaseDir::AUTO, kClueTracking);
    }
  }
  if (n == 2) renderer.drawLine(x, y + halfHeight, std::min(x + size, renderer.getScreenWidth() - 1), y + halfHeight);
  const uint8_t selected = game->selectedWord();
  const bool shown = (!touchBoard() || keyboardOpen) && !game->solved();
  if (shown && selected != scanword::NO_WORD && puzzle->word(selected).clueCell == cell) {
    renderer.drawRect(x + 1, y + 1, size - 1, size - 1, kClueFrameWidth, true);
  }
}

void ScanwordActivity::drawArrows(const uint16_t cell, const int x, const int y) const {
  const int size = cellSize();
  const int s = size / 7;
  for (uint8_t i = 0; i < puzzle->wordCount(); ++i) {
    const Word& w = puzzle->word(i);
    if (w.start != cell) continue;
    uint8_t ids[2];
    const bool split = puzzle->cluesAt(w.clueCell, ids) == 2;
    switch (w.arrow) {
      case scanword::Arrow::Right: {
        const int ay = y + (split ? (w.half == 0 ? size / 4 : 3 * size / 4) : size / 2);
        const int xs[] = {x + 1, x + 1, x + s};
        const int ys[] = {ay - s / 2, ay + s / 2, ay};
        renderer.fillPolygon(xs, ys, 3);
        break;
      }
      case scanword::Arrow::Down: {
        const int ax = x + size / 2;
        const int xs[] = {ax - s / 2, ax + s / 2, ax};
        const int ys[] = {y + 1, y + 1, y + s};
        renderer.fillPolygon(xs, ys, 3);
        break;
      }
      case scanword::Arrow::DownRight: {
        const int ax = x + size / 4;
        renderer.drawLine(ax, y + 1, ax, y + s, 2, true);
        renderer.drawLine(ax, y + s, ax + s, y + s, 2, true);
        const int xs[] = {ax + s, ax + s, ax + s + s / 2};
        const int ys[] = {y + s - s / 2, y + s + s / 2, y + s};
        renderer.fillPolygon(xs, ys, 3);
        break;
      }
      case scanword::Arrow::RightDown: {
        const int ay = y + (split ? (w.half == 0 ? size / 6 : size / 2) : size / 4);
        renderer.drawLine(x + 1, ay, x + s, ay, 2, true);
        renderer.drawLine(x + s, ay, x + s, ay + s, 2, true);
        const int xs[] = {x + s - s / 2, x + s + s / 2, x + s};
        const int ys[] = {ay + s, ay + s, ay + s + s / 2};
        renderer.fillPolygon(xs, ys, 3);
        break;
      }
    }
  }
}

void ScanwordActivity::drawSelection() const {
  const uint8_t selected = game->selectedWord();
  if (selected == scanword::NO_WORD) return;
  const Word& w = puzzle->word(selected);
  const int size = cellSize();
  int x = 0;
  int y = 0;
  cellOrigin(w.start, x, y);
  const int width = w.dir == scanword::Dir::Across ? w.length * size : size;
  const int height = w.dir == scanword::Dir::Across ? size : w.length * size;
  // Lines that leave the screen are dropped whole, so clamp the frame to it.
  const int x0 = std::max(0, x - 1);
  const int y0 = std::max(0, y - 1);
  const int x1 = std::min(renderer.getScreenWidth(), x + width + 2);
  const int y1 = std::min(renderer.getScreenHeight(), y + height + 2);
  renderer.drawRect(x0, y0, x1 - x0, y1 - y0, kWordFrameWidth, true);
  cellOrigin(game->cursor(), x, y);
  drawCellFrame(renderer, x + 1, y + 1, size - 1, size - 1);
}

void ScanwordActivity::drawWordLine(const int top, const int height) const {
  // Clue of the selected answer with its length (or the last check result) on the right.
  const int width = renderer.getScreenWidth();
  const uint8_t selected = game->selectedWord();
  if (selected == scanword::NO_WORD) return;
  const Word& w = puzzle->word(selected);
  char right[32];
  if (checkResult == 0 && !checkedGrid) {
    snprintf(right, sizeof(right), "%s", tr(STR_SCANWORD_ALL_RIGHT));
  } else if (checkResult > 0 && !checkedGrid) {
    snprintf(right, sizeof(right), tr(STR_SCANWORD_ERRORS), checkResult);
  } else {
    lettersLabel(w.length, right, sizeof(right));
  }
  const int textTop = top + (height - renderer.getLineHeight(kText12Font)) / 2;
  const int rightWidth = renderer.getTextWidth(kText10Font, right);
  drawRightAligned(renderer, kText10Font, width - kSidePadding,
                   textTop + renderer.getFontAscenderSize(kText12Font) - renderer.getFontAscenderSize(kText10Font),
                   right);
  const std::string_view clue = puzzle->clue(selected);
  char text[scanword::MAX_CLUE_BYTES + 1];
  const size_t len = std::min(clue.size(), sizeof(text) - 1);
  memcpy(text, clue.data(), len);
  text[len] = '\0';
  // One line with an ellipsis if needed, without a heap string.
  Measure measure{&renderer, kText12Font, kSemiBold, 0};
  scanword::ClueLines fitted;
  scanword::layoutClue(text, width - 2 * kSidePadding - rightWidth - kIconGap, 1, measureText, &measure, fitted);
  if (fitted.count > 0) renderer.drawText(kText12Font, kSidePadding, textTop, fitted.line[0], true, kSemiBold);
}

void ScanwordActivity::drawAnswerStrip(UiScreen& screen, const int top) const {
  const int width = renderer.getScreenWidth();
  drawWordLine(top + 4, 36);
  const uint8_t selected = game->selectedWord();
  if (selected == scanword::NO_WORD) return;
  const Word& w = puzzle->word(selected);
  const int avail = width - 2 * kSidePadding;
  const int box = std::min(kBoxSize, (avail + kBoxGap) / w.length - kBoxGap);
  const int boxTop = top + 44;
  for (uint8_t i = 0; i < w.length; ++i) {
    const uint16_t cell = puzzle->cellOf(w, i);
    const int x = kSidePadding + i * (box + kBoxGap);
    renderer.drawRect(x, boxTop, box, box, true);
    if (const uint8_t entry = game->entry(cell)) drawLetter(entry, x, boxTop, box, box, box * 11 / 20);
    const uint8_t flags = game->flags(cell);
    if (flags & scanword::FLAG_WRONG) renderer.drawLine(x + 5, boxTop + box - 5, x + box - 5, boxTop + 5, 2, true);
    if (flags & scanword::FLAG_REVEALED) {
      renderer.fillRect(x + box - kMarkSize - 3, boxTop + box - kMarkSize - 3, kMarkSize, kMarkSize, true);
    }
    if (cell == game->cursor()) drawCellFrame(renderer, x, boxTop, box, box);
  }
  screen.frame().hit(fui::makeRect(kSidePadding, boxTop, w.length * (box + kBoxGap), box), ACTION_BOX, box + kBoxGap,
                     kTap);
}

void ScanwordActivity::drawKeyboard(UiScreen& screen) const {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int top = keyboardTop();
  renderer.fillRect(0, top, width, height - top, false);
  drawRule(renderer, 0, top, width);
  drawAnswerStrip(screen, top + kRuleWidth);

  const int keysTop = top + kStripHeight;
  drawRule(renderer, 0, keysTop - kRuleWidth, width);
  for (int r = 0; r < kKeyRows; ++r) {
    const int y = keysTop + r * kKeyHeight;
    for (int k = 0; k < kKeysPerRow; ++k) {
      const int x0 = k * width / kKeysPerRow;
      const int x1 = (k + 1) * width / kKeysPerRow;
      drawLetter(kKeys[r][k], x0, y, x1 - x0, kKeyHeight, 16);
      if (k > 0) renderer.drawLine(x0, y, x0, y + kKeyHeight - 1);
    }
    renderer.drawLine(0, y + kKeyHeight - 1, width - 1, y + kKeyHeight - 1);
  }
  // One hit for all keys (the frame holds few interactions); handleAction finds the key.
  screen.frame().hit(fui::makeRect(0, keysTop, width, kKeyRows * kKeyHeight), ACTION_KEY, 0, kTap);

  const int actTop = keysTop + kKeyRows * kKeyHeight;
  drawRule(renderer, 0, actTop, width);
  struct Act {
    const freeink::Icon* icon;
    StrId label;
    fui::ActionId action;
  };
  const Act acts[] = {{&icon_scanword_erase_24, StrId::STR_SCANWORD_ERASE, ACTION_ERASE},
                      {&icon_scanword_check_24, StrId::STR_SCANWORD_CHECK, ACTION_CHECK},
                      {&icon_scanword_hint_24, StrId::STR_SCANWORD_HINT, ACTION_HINT},
                      {&icon_scanword_done_24, StrId::STR_SCANWORD_DONE, ACTION_DONE}};
  constexpr int kActs = 4;
  // Icon above a caption, like the bottom tabs.
  const int cellTop = actTop + kRuleWidth;
  const int cellHeight = height - cellTop;
  const int captionHeight = renderer.getLineHeight(kCaptionFont);
  const int iconTop = cellTop + (cellHeight - kIconSize - captionHeight) / 2;
  for (int i = 0; i < kActs; ++i) {
    const int x0 = i * width / kActs;
    const int x1 = (i + 1) * width / kActs;
    const char* label = I18N.get(acts[i].label);
    drawIcon(screen, *acts[i].icon, x0 + (x1 - x0 - kIconSize) / 2, iconTop);
    renderer.drawText(kCaptionFont, x0 + (x1 - x0 - renderer.getTextWidth(kCaptionFont, label)) / 2,
                      iconTop + kIconSize, label);
    if (i > 0) renderer.drawLine(x0, cellTop, x0, height - 1);
    screen.frame().hit(fui::makeRect(x0, cellTop, x1 - x0, cellHeight), acts[i].action, 0, kTap);
  }
}

void ScanwordActivity::drawLetterStrip(const int top) const {
  const int width = renderer.getScreenWidth();
  const int rowWidth = kPickerCells * kPickerCell;
  const int left = (width - rowWidth) / 2;
  for (int i = 0; i < kPickerCells; ++i) {
    const int offset = i - kPickerCells / 2;
    const auto letter =
        static_cast<uint8_t>((pickLetter - 1 + offset + 2 * scanword::LETTER_COUNT) % scanword::LETTER_COUNT + 1);
    const int x = left + i * kPickerCell;
    drawLetter(letter, x, top, kPickerCell, kPickerCell, offset == 0 ? 22 : 18);
    if (offset == 0) drawCellFrame(renderer, x, top, kPickerCell, kPickerCell);
  }
  const int arrowTop = top + (kPickerCell - renderer.getLineHeight(kText12Font)) / 2;
  renderer.drawText(kText12Font, left - 24, arrowTop, "\xE2\x80\xB9");
  renderer.drawText(kText12Font, left + rowWidth + 12, arrowTop, "\xE2\x80\xBA");
}

void ScanwordActivity::drawBand(UiScreen& screen) const {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int top = gridBottom();
  drawRule(renderer, 0, top, width);
  const int bandTop = top + kRuleWidth;

  if (game->solved()) {
    char time[40];
    snprintf(time, sizeof(time), tr(STR_SCANWORD_SOLVED_BANNER), minutes(game->progress().seconds));
    char hints[32] = "";
    if (game->progress().hints > 0) snprintf(hints, sizeof(hints), tr(STR_SCANWORD_HINTS_USED), game->progress().hints);
    char banner[80];
    snprintf(banner, sizeof(banner), hints[0] ? "%s, %s" : "%s%s", time, hints);
    renderer.drawText(kText12Font, kSidePadding, bandTop + (height - bandTop - renderer.getLineHeight(kText12Font)) / 2,
                      banner, true, kSemiBold);
    return;
  }

  if (!touchBoard()) {
    drawWordLine(bandTop, kClueBarHeight);
    const int stripTop = bandTop + kClueBarHeight;
    renderer.drawLine(0, stripTop, width - 1, stripTop);
    if (stripOpen) {
      drawLetterStrip(stripTop + (height - stripTop - kPickerCell) / 2);
    } else {
      // The hint, or the result of checking the whole grid from the menu.
      char line[48];
      if (checkResult == 0 && checkedGrid) {
        snprintf(line, sizeof(line), "%s", tr(STR_SCANWORD_ALL_RIGHT));
      } else if (checkResult > 0 && checkedGrid) {
        snprintf(line, sizeof(line), tr(STR_SCANWORD_ERRORS), checkResult);
      } else {
        snprintf(line, sizeof(line), "%s", tr(STR_SCANWORD_PICK_HINT));
      }
      const int textTop = stripTop + (height - stripTop - renderer.getLineHeight(kText10Font)) / 2;
      renderer.drawText(kText10Font, kSidePadding, textTop, line);
    }
    return;
  }

  // Touch, keyboard closed: check the whole grid once something is filled, and its result.
  if (game->filledCount() == 0) return;
  const int rowHeight = height - bandTop;
  const int textTop = bandTop + (rowHeight - renderer.getLineHeight(kText12Font)) / 2;
  drawIcon(screen, icon_scanword_check_all_24, kSidePadding, bandTop + (rowHeight - kIconSize) / 2);
  const char* label = tr(STR_SCANWORD_CHECK_ALL);
  renderer.drawText(kText12Font, kSidePadding + kIconSize + 10, textTop, label);
  const int labelRight = kSidePadding + kIconSize + 10 + renderer.getTextWidth(kText12Font, label);
  screen.frame().hit(fui::makeRect(0, bandTop, labelRight + kSidePadding, rowHeight), ACTION_CHECK_ALL, 0, kTap);
  if (checkResult < 0 || !checkedGrid) return;
  char right[48];
  if (checkResult == 0) {
    snprintf(right, sizeof(right), "%s", tr(STR_SCANWORD_ALL_RIGHT));
  } else {
    snprintf(right, sizeof(right), tr(STR_SCANWORD_ERRORS), checkResult);
  }
  const int rightTop = textTop + renderer.getFontAscenderSize(kText12Font) - renderer.getFontAscenderSize(kText10Font);
  drawRightAligned(renderer, kText10Font, width - kSidePadding, rightTop, right);
}
