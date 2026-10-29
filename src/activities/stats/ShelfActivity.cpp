#include "ShelfActivity.h"

#include <BoardConfig.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "components/icons/listIcons.h"
#include "stats/ReadingStats.h"
#include "statsIcons.h"
#include "util/ButtonNavigator.h"

namespace fui = freeink::ui;
using namespace setka;
using statsui::kRowTextX;

namespace {
constexpr fui::ActionId ACTION_CELL = 1;
constexpr fui::ActionId ACTION_PICK = 2;
constexpr fui::ActionId ACTION_MENU = 3;
constexpr fui::ActionId ACTION_DISMISS = 4;

constexpr int kColumns = 3;
constexpr int kCoverWidth = 128;
constexpr int kCoverHeight = 184;
constexpr int kCaptionGap = 8;
constexpr int kGridGap = 20;
constexpr int kFocusRing = 6;  // the focus frame sits this far outside a cover
constexpr int kPickerRowHeight = kNavRowHeight;
// Books listed at most, the most recently read first: each holds its path and title.
constexpr size_t kMaxBooks = FREEINK_MCU_C3 ? 80 : 200;
}  // namespace

void ShelfActivity::onEnter() {
  // The render task may run as soon as ActivityManager drops its lock; keep it
  // out until the state below is in place (same as LibraryListActivity).
  RenderLock lock(*this);
  Activity::onEnter();
  picking = false;
  focus = 0;
  top = 0;
  menuOpen = false;
  entryRefresh = true;
  reloadLocked();
  resetUi();
  app.on(ACTION_CELL, &ShelfActivity::onCell, this);
  app.on(ACTION_PICK, &ShelfActivity::onPick, this);
  app.on(ACTION_MENU, &ShelfActivity::onMenu, this);
  app.on(ACTION_DISMISS, &ShelfActivity::onDismiss, this);
  app.setScreen(&ShelfActivity::screenFn, this);
  requestUpdate();
}

void ShelfActivity::reload() {
  RenderLock lock(*this);
  reloadLocked();
}

void ShelfActivity::reloadLocked() {
  auto names = makeUniqueNoThrow<stats::ShelfNames>();
  books.clear();
  candidates.clear();
  thumbsChecked = false;
  if (names && READING_STATS.loadShelves(*names) && names->used(shelf)) name = names->names[shelf];
  // The picker's list is only held while it is open.
  struct Collect {
    uint32_t bit;
    bool onShelf;
    std::vector<Book>* rows;
    size_t matches;
    size_t finished;
  } collect{1u << shelf, !picking, picking ? &candidates : &books, 0, 0};
  // Counted first, so the list is allocated once; the counts also cover books past the cap.
  READING_STATS.forEachBook(
      [](const stats::BookInfo& info, void* raw) {
        auto* c = static_cast<Collect*>(raw);
        if (((info.shelves & c->bit) != 0) != c->onShelf) return true;
        c->matches++;
        if (info.status == stats::BookStatus::Finished) c->finished++;
        return true;
      },
      &collect);
  if (!picking) {
    shelfTotal = static_cast<unsigned>(collect.matches);
    shelfFinished = static_cast<unsigned>(collect.finished);
  }
  collect.rows->reserve(std::min(collect.matches, kMaxBooks));
  READING_STATS.forEachBook(
      [](const stats::BookInfo& info, void* raw) {
        auto* c = static_cast<Collect*>(raw);
        if (((info.shelves & c->bit) != 0) != c->onShelf) return true;
        Book book;
        book.id = info.id;
        book.order = static_cast<uint32_t>(info.lastReadEpoch);
        book.path = info.path;
        const char* slash = std::strrchr(info.path, '/');
        book.title = info.title[0] ? info.title : slash ? slash + 1 : info.path;
        if (c->rows->size() < kMaxBooks) {
          c->rows->push_back(std::move(book));
          return true;
        }
        auto oldest = std::min_element(c->rows->begin(), c->rows->end(),
                                       [](const Book& a, const Book& b) { return a.order < b.order; });
        if (oldest->order < book.order) *oldest = std::move(book);
        return true;
      },
      &collect);
  std::sort(candidates.begin(), candidates.end(), [](const Book& a, const Book& b) { return a.title < b.title; });
  focus = std::clamp(focus, 0, std::max(0, itemCount() - 1));
}

int ShelfActivity::itemCount() const {
  return picking ? static_cast<int>(candidates.size()) : static_cast<int>(books.size()) + 1;
}

void ShelfActivity::screenFn(UiScreen& screen, void* user) { static_cast<ShelfActivity*>(user)->draw(screen); }

void ShelfActivity::onCell(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<ShelfActivity*>(user);
  // A tap routed against the grid's last published table after the picker opened.
  if (self->picking) return;
  if (event.longPress) {
    self->openMenu(event.value);
  } else {
    self->activate(event.value);
  }
}

void ShelfActivity::onPick(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<ShelfActivity*>(user);
  if (self->picking) self->activate(event.value);
}

void ShelfActivity::onMenu(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<ShelfActivity*>(user);
  // The menu's one entry: take the book off this shelf.
  if (self->menuCell >= 0 && self->menuCell < static_cast<int>(self->books.size())) {
    READING_STATS.setOnShelf(self->books[self->menuCell].id, self->shelf, false);
  }
  self->closeMenu();
  self->reload();
  self->entryRefresh = true;
}

void ShelfActivity::onDismiss(const fui::ActionEvent&, void* user) { static_cast<ShelfActivity*>(user)->closeMenu(); }

void ShelfActivity::describeMenu(int, const freeink::Icon*& icon, const char*& label, void*) {
  icon = &icon_x_24;
  label = tr(STR_SHELF_REMOVE_FROM);
}

void ShelfActivity::setPicking(const bool on) {
  {
    RenderLock lock(*this);
    picking = on;
    focus = 0;
    top = 0;
  }
  reload();
  entryRefresh = true;
  requestUpdate();
}

void ShelfActivity::moveFocus(const int delta) {
  RenderLock lock(*this);
  buttonsUsed = true;
  const int count = itemCount();
  if (count == 0) return;
  focus = (focus + delta + count) % count;
  const int row = picking ? focus : focus / kColumns;
  const int visible = visibleRows.load();
  if (row < top) top = row;
  if (row >= top + visible) top = row - visible + 1;
  thumbsChecked = false;
  requestUpdate();
}

void ShelfActivity::activate(const int index) {
  if (index < 0 || index >= itemCount()) return;
  app.clearTapFlash();
  if (picking) {
    READING_STATS.setOnShelf(candidates[index].id, shelf, true);
    setPicking(false);
    return;
  }
  if (index == static_cast<int>(books.size())) {
    setPicking(true);
    return;
  }
  activityManager.goToReader(books[index].path);
}

void ShelfActivity::openMenu(const int index) {
  RenderLock lock(*this);
  if (menuOpen || picking || index < 0 || index >= static_cast<int>(books.size())) return;
  menuOpen = true;
  menuCell = index;
  requestUpdate();
}

void ShelfActivity::closeMenu() {
  RenderLock lock(*this);
  menuOpen = false;
  menuCell = -1;
  requestUpdate();
}

void ShelfActivity::loop() {
  const auto route = routeTouch(mappedInput, true);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  if (menuOpen) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closeMenu();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      onMenu(fui::ActionEvent{}, this);
    }
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    RenderLock lock(*this);
    const int visible = visibleRows.load();
    const int rows = picking ? itemCount() : (itemCount() + kColumns - 1) / kColumns;
    top = std::clamp(top + (swipe == MappedInputManager::SwipeDir::Up ? visible : -visible), 0,
                     std::max(0, rows - visible));
    thumbsChecked = false;
    requestUpdate();
    return;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, statsui::kLongPressMs)) {
    // wasLongPressed() also swallows this hold's release, so it cannot pick the menu entry.
    buttonsUsed = true;
    openMenu(focus);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (picking) {
      setPicking(false);
    } else {
      finish();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    buttonsUsed = true;
    activate(focus);
    return;
  }
  ButtonNavigator::onNextPress([this] { moveFocus(1); });
  ButtonNavigator::onPreviousPress([this] { moveFocus(-1); });
}

void ShelfActivity::drawGrid(UiScreen& screen, const int areaTop, const int bottom, const int width) {
  const int captionHeight = renderer.getLineHeight(kCaptionFont);
  const int line12 = renderer.getLineHeight(kText12Font);
  const int pitch = kCoverHeight + kCaptionGap + captionHeight + kGridGap;
  const int gap = (width - 2 * kSidePadding - kColumns * kCoverWidth) / (kColumns - 1);
  const int gridTop = areaTop + kGridGap;
  const int gridBottom = bottom - line12 - kGridGap;
  const int visible = std::max(1, (gridBottom - gridTop + kGridGap) / pitch);
  visibleRows = visible;
  const int count = itemCount();
  const int first = top * kColumns;
  const int last = std::min(count, (top + visible) * kColumns);
  for (int i = first; i < last; ++i) {
    const int x = kSidePadding + (i % kColumns) * (kCoverWidth + gap);
    const int y = gridTop + (i / kColumns - top) * pitch;
    if (i < static_cast<int>(books.size())) {
      Book& book = books[i];
      if (!book.thumbResolved) {
        book.thumb = statsui::thumbPath(book.path, kCoverHeight);
        book.thumbResolved = true;
      }
      covers.draw(renderer, book.thumb, book.title.c_str(), x, y, kCoverWidth, kCoverHeight);
      renderer.drawText(kCaptionFont, x, y + kCoverHeight + kCaptionGap,
                        renderer.truncatedText(kCaptionFont, book.title.c_str(), kCoverWidth).c_str());
    } else {
      statsui::drawDashedRect(renderer, x, y, kCoverWidth, kCoverHeight);
      constexpr int kPlus = 32;
      screen.target().bitmap(fui::makeRect(x + (kCoverWidth - kPlus) / 2, y + (kCoverHeight - kPlus) / 2, kPlus, kPlus),
                             fui::bitmapFromIcon(icon_plus_32), fui::BitmapMode::Center);
    }
    if (buttonsUsed && i == focus && !menuOpen) {
      drawCellFrame(renderer, x - kFocusRing, y - kFocusRing, kCoverWidth + 2 * kFocusRing,
                    kCoverHeight + 2 * kFocusRing);
    }
    screen.frame().hit(fui::makeRect(x, y, kCoverWidth, kCoverHeight + kCaptionGap + captionHeight), ACTION_CELL,
                       static_cast<int16_t>(i), fui::InputTouch | fui::InputLongPress);
  }

  const unsigned total = shelfTotal;
  const unsigned finished = shelfFinished;
  char summary[64];
  int length = snprintf(summary, sizeof(summary),
                        I18N.get(statsui::plural(total, StrId::STR_STATS_BOOKS_ONE, StrId::STR_STATS_BOOKS_FEW,
                                                 StrId::STR_STATS_BOOKS_MANY)),
                        total);
  if (finished > 0 && length > 0 && length < static_cast<int>(sizeof(summary))) {
    snprintf(summary + length, sizeof(summary) - length,
             I18N.get(statsui::plural(finished, StrId::STR_SHELF_FINISHED_ONE, StrId::STR_SHELF_FINISHED_FEW,
                                      StrId::STR_SHELF_FINISHED_MANY)),
             finished);
  }
  const int rows = (last - first + kColumns - 1) / kColumns;
  const int summaryTop = std::min(gridTop + rows * pitch, bottom - line12);
  renderer.drawText(kText12Font, kSidePadding, summaryTop, summary);
}

void ShelfActivity::drawPicker(UiScreen& screen, const int areaTop, const int bottom, const int width) {
  const int visible = std::max(1, (bottom - areaTop) / kPickerRowHeight);
  visibleRows = visible;
  if (candidates.empty()) {
    const int line12 = renderer.getLineHeight(kText12Font);
    int y = areaTop + kSidePadding;
    for (const auto& line :
         renderer.wrappedText(kText12Font, tr(STR_SHELF_PICK_EMPTY), width - kRowTextX - kSidePadding, 3)) {
      renderer.drawText(kText12Font, kRowTextX, y, line.c_str());
      y += line12;
    }
    return;
  }
  const int last = std::min(static_cast<int>(candidates.size()), top + visible);
  for (int i = top; i < last; ++i) {
    const int rowTop = areaTop + (i - top) * kPickerRowHeight;
    statsui::drawIconRow(renderer, screen.target(), &icon_book_24, candidates[i].title.c_str(), nullptr, kRowTextX,
                         rowTop, width - kRowTextX - kSidePadding, kPickerRowHeight, buttonsUsed && i == focus);
    screen.frame().hit(fui::makeRect(0, rowTop, width, kPickerRowHeight), ACTION_PICK, static_cast<int16_t>(i),
                       fui::InputTouch);
  }
}

void ShelfActivity::draw(UiScreen& screen) {
  const auto bands = statsui::drawHeader(renderer, picking ? tr(STR_SHELF_ADD_BOOK) : name.c_str(), true);
  if (picking) {
    drawPicker(screen, bands.contentTop, bands.contentBottom, bands.width);
    return;
  }
  drawGrid(screen, bands.contentTop, bands.contentBottom, bands.width);
  if (menuOpen && menuCell >= 0 && menuCell < static_cast<int>(books.size())) {
    const int pitch = kCoverHeight + kCaptionGap + renderer.getLineHeight(kCaptionFont) + kGridGap;
    statsui::drawMenuBox(renderer, screen, books[menuCell].title.c_str(), 1, 0, buttonsUsed, &describeMenu, this,
                         bands.contentTop, bands.contentBottom, bands.width, ACTION_MENU, ACTION_DISMISS, pitch);
  }
}

bool ShelfActivity::generateMissingThumbs() {
  bool generated = false;
  const int first = top * kColumns;
  const int last = std::min(static_cast<int>(books.size()), (top + visibleRows.load()) * kColumns);
  for (int i = first; i < last; ++i) {
    Book& book = books[i];
    if (book.thumb.empty() || Storage.exists(book.thumb.c_str())) continue;
    if (statsui::generateThumb(book.path, kCoverHeight)) {
      generated = true;
    } else {
      book.thumb.clear();  // no usable cover: never retried while the screen lives
    }
  }
  return generated;
}

void ShelfActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  statsui::drawHints(renderer, mappedInput, tr(STR_SELECT));
  renderer.displayBuffer(entryRefresh ? cadence.entry() : cadence.update());
  entryRefresh = false;
  if (!thumbsChecked && !picking) {
    thumbsChecked = true;
    if (generateMissingThumbs()) requestUpdate();
  }
}
