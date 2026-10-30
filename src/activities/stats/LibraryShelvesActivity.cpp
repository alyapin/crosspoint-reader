#include "LibraryShelvesActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <LibraryText.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "ShelfActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/icons/listIcons.h"
#include "statsIcons.h"
#include "util/ButtonNavigator.h"

namespace fui = freeink::ui;
using namespace setka;
using statsui::kRowTextX;

namespace {
constexpr fui::ActionId ACTION_TAB = 1;
constexpr fui::ActionId ACTION_ROW = 2;
constexpr fui::ActionId ACTION_MENU = 3;
constexpr fui::ActionId ACTION_DISMISS = 4;

constexpr int kBookRowHeight = 112;
constexpr int kShelfRowHeight = kListPitch;
constexpr int kCoverWidth = 60;
constexpr int kCoverHeight = 88;
constexpr int kTabGap = 26;
constexpr int kTabGapMin = 10;
// The active tab's bar ends this far left of its label.
constexpr int kTabBarGap = 6;
// Gap before the active tab: its bar, plus air on both sides.
constexpr int kTabBarRun = kSelectionBarWidth + 2 * kTabBarGap;
// The other gaps carry a thin divider, so the labels do not read as one phrase;
// the X4 draws the tab row of Все книги, without them.
constexpr int kTabDividerHeight = 16;
constexpr bool kTabDividers = !FREEINK_DEVICE_X4;
// Rows kept per status tab, newest first: each holds its path and title.
constexpr size_t kMaxRows = FREEINK_MCU_C3 ? 80 : 200;

constexpr StrId kTabLabels[] = {StrId::STR_SHELF_TAB_READING, StrId::STR_SHELF_TAB_FINISHED,
                                StrId::STR_SHELF_TAB_PAUSED, StrId::STR_SHELF_TAB_SHELVES};
constexpr StrId kEmptyText[] = {StrId::STR_SHELF_EMPTY_READING, StrId::STR_SHELF_EMPTY_FINISHED,
                                StrId::STR_SHELF_EMPTY_PAUSED};

stats::BookStatus tabStatus(const int tab) {
  switch (tab) {
    case LibraryShelvesActivity::TabFinished:
      return stats::BookStatus::Finished;
    case LibraryShelvesActivity::TabPaused:
      return stats::BookStatus::Paused;
    default:
      return stats::BookStatus::Reading;
  }
}

}  // namespace

void LibraryShelvesActivity::onEnter() {
  // The render task may run as soon as ActivityManager drops its lock; keep it
  // out until the state below is in place (same as LibraryListActivity).
  RenderLock lock(*this);
  Activity::onEnter();
  focus = -1;
  top = 0;
  menu = Menu::None;
  entryRefresh = true;
  reloadLocked();
  resetUi();
  app.on(ACTION_TAB, &LibraryShelvesActivity::onTab, this);
  app.on(ACTION_ROW, &LibraryShelvesActivity::onRow, this);
  app.on(ACTION_MENU, &LibraryShelvesActivity::onMenu, this);
  app.on(ACTION_DISMISS, &LibraryShelvesActivity::onDismiss, this);
  app.setScreen(&LibraryShelvesActivity::screenFn, this);
  requestUpdate();
}

void LibraryShelvesActivity::reload() {
  RenderLock lock(*this);
  reloadLocked();
}

void LibraryShelvesActivity::reloadLocked() {
  books.clear();
  shelfRows.clear();
  thumbsChecked = false;
  if (tab == TabShelves) {
    auto names = makeUniqueNoThrow<stats::ShelfNames>();
    if (!names) {
      LOG_ERR("STAT", "OOM: shelf names");
      return;
    }
    uint16_t counts[stats::MAX_SHELVES] = {};
    READING_STATS.loadShelves(*names);
    READING_STATS.forEachBook(
        [](const stats::BookInfo& book, void* raw) {
          auto* count = static_cast<uint16_t*>(raw);
          for (uint8_t i = 0; i < stats::MAX_SHELVES; ++i) {
            if ((book.shelves >> i) & 1u) count[i]++;
          }
          return true;
        },
        counts);
    for (uint8_t slot = 0; slot < stats::MAX_SHELVES; ++slot) {
      if (names->used(slot)) shelfRows.push_back(ShelfRow{slot, counts[slot], names->names[slot]});
    }
  } else {
    struct Collect {
      stats::BookStatus status;
      std::vector<BookRow>* rows;
      size_t matches;
    } collect{tabStatus(tab), &books, 0};
    // Counted first, so the rows are allocated once.
    READING_STATS.forEachBook(
        [](const stats::BookInfo& book, void* raw) {
          auto* c = static_cast<Collect*>(raw);
          if (book.status == c->status) c->matches++;
          return true;
        },
        &collect);
    books.reserve(std::min(collect.matches, kMaxRows));
    READING_STATS.forEachBook(
        [](const stats::BookInfo& book, void* raw) {
          auto* c = static_cast<Collect*>(raw);
          if (book.status != c->status) return true;
          BookRow row;
          row.id = book.id;
          row.status = book.status;
          row.shelves = book.shelves;
          row.order =
              book.status == stats::BookStatus::Finished ? book.finishedDay : static_cast<uint32_t>(book.lastReadEpoch);
          row.path = book.path;
          const char* slash = std::strrchr(book.path, '/');
          row.title = book.title[0] ? book.title : slash ? slash + 1 : book.path;
          snprintf(row.author, sizeof(row.author), "%s", book.author + library::authorRoleLength(book.author));
          const unsigned percent = (book.progressBp + 50) / 100;
          const uint32_t left = book.status == stats::BookStatus::Reading ? stats::estimateSecondsLeft(book) : 0;
          if (left > 0) {
            char timeText[32];
            statsui::timeLeftText(left, timeText, sizeof(timeText));
            snprintf(row.detail, sizeof(row.detail), "%u%%, %s", percent, timeText);
          } else {
            snprintf(row.detail, sizeof(row.detail), "%u%%", percent);
          }
          if (c->rows->size() < kMaxRows) {
            c->rows->push_back(std::move(row));
            return true;
          }
          // Full: the newest kMaxRows stay.
          auto oldest = std::min_element(c->rows->begin(), c->rows->end(),
                                         [](const BookRow& a, const BookRow& b) { return a.order < b.order; });
          if (oldest->order < row.order) *oldest = std::move(row);
          return true;
        },
        &collect);
    std::sort(books.begin(), books.end(), [](const BookRow& a, const BookRow& b) { return a.order > b.order; });
  }
  const int count = rowCount();
  if (focus >= count) focus = count - 1;
  if (top > std::max(0, count - 1)) top = std::max(0, count - 1);
}

int LibraryShelvesActivity::rowCount() const {
  return tab == TabShelves ? static_cast<int>(shelfRows.size()) + 1 : static_cast<int>(books.size());
}

void LibraryShelvesActivity::screenFn(UiScreen& screen, void* user) {
  static_cast<LibraryShelvesActivity*>(user)->draw(screen);
}

void LibraryShelvesActivity::onTab(const fui::ActionEvent& event, void* user) {
  static_cast<LibraryShelvesActivity*>(user)->selectTab(event.value);
}

void LibraryShelvesActivity::onRow(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<LibraryShelvesActivity*>(user);
  if (event.longPress) {
    self->openMenu(event.value);
  } else {
    self->activateRow(event.value);
  }
}

void LibraryShelvesActivity::onMenu(const fui::ActionEvent& event, void* user) {
  static_cast<LibraryShelvesActivity*>(user)->chooseMenu(event.value);
}

void LibraryShelvesActivity::onDismiss(const fui::ActionEvent&, void* user) {
  static_cast<LibraryShelvesActivity*>(user)->closeMenu();
}

void LibraryShelvesActivity::selectTab(const int next) {
  {
    RenderLock lock(*this);
    tab = (next % TabCount + TabCount) % TabCount;
    top = 0;
    if (focus >= 0) focus = 0;
    menu = Menu::None;
  }
  reload();
  entryRefresh = true;
  requestUpdate();
}

void LibraryShelvesActivity::moveFocus(const int delta) {
  RenderLock lock(*this);
  buttonsUsed = true;
  if (menu != Menu::None) {
    if (menuCount > 0) menuFocus = (menuFocus + delta + menuCount) % menuCount;
    requestUpdate();
    return;
  }
  // The ring: the tab strip, then every row.
  const int ring = rowCount() + 1;
  focus = (focus + 1 + delta + ring) % ring - 1;
  const int visible = visibleRows.load();
  if (focus >= 0 && focus < top) top = focus;
  if (focus >= top + visible) top = focus - visible + 1;
  if (focus < 0) top = 0;
  thumbsChecked = false;
  requestUpdate();
}

void LibraryShelvesActivity::jumpPage(const int direction) {
  RenderLock lock(*this);
  const int count = rowCount();
  if (count == 0) return;
  buttonsUsed = true;
  const int visible = std::max(1, visibleRows.load());
  const int from = std::max(0, focus);
  focus = direction > 0 ? ButtonNavigator::nextPageIndex(from, count, visible)
                        : ButtonNavigator::previousPageIndex(from, count, visible);
  top = std::min(focus, std::max(0, count - visible));
  thumbsChecked = false;
  requestUpdate();
}

void LibraryShelvesActivity::activateRow(const int index) {
  if (index < 0 || index >= rowCount()) return;
  app.clearTapFlash();
  if (tab != TabShelves) {
    activityManager.goToReader(books[index].path);
    return;
  }
  if (index == static_cast<int>(shelfRows.size())) {
    askShelfName(-1);
    return;
  }
  auto shelf = makeUniqueNoThrow<ShelfActivity>(renderer, mappedInput, shelfRows[index].slot);
  if (!shelf) {
    LOG_ERR("STAT", "OOM: shelf screen");
    return;
  }
  startActivityForResult(std::move(shelf), [this](const ActivityResult&) {
    reload();
    entryRefresh = true;
  });
}

void LibraryShelvesActivity::openMenu(const int index) {
  RenderLock lock(*this);
  if (menu != Menu::None || index < 0 || index >= rowCount()) return;
  menuCount = 0;
  menuFocus = 0;
  menuRow = index;
  if (tab == TabShelves) {
    if (index >= static_cast<int>(shelfRows.size())) return;
    menu = Menu::Shelf;
    menuEntries[menuCount++] = {Item::Rename, 0};
    menuEntries[menuCount++] = {Item::Delete, 0};
  } else {
    menu = Menu::Book;
    const stats::BookStatus status = books[index].status;
    if (status != stats::BookStatus::Reading) menuEntries[menuCount++] = {Item::Reading, 0};
    if (status != stats::BookStatus::Finished) menuEntries[menuCount++] = {Item::Finished, 0};
    if (status == stats::BookStatus::Reading) menuEntries[menuCount++] = {Item::Paused, 0};
    menuEntries[menuCount++] = {Item::ToShelf, 0};
  }
  requestUpdate();
}

void LibraryShelvesActivity::openShelfPicker() {
  auto names = makeUniqueNoThrow<stats::ShelfNames>();
  if (!names || !READING_STATS.loadShelves(*names)) {
    LOG_ERR("STAT", "Cannot list shelves");
    return;
  }
  RenderLock lock(*this);
  shelfRows.clear();
  menu = Menu::PickShelf;
  menuCount = 0;
  menuFocus = 0;
  for (uint8_t slot = 0; slot < stats::MAX_SHELVES; ++slot) {
    if (!names->used(slot)) continue;
    shelfRows.push_back(ShelfRow{slot, 0, names->names[slot]});
    menuEntries[menuCount++] = {Item::Shelf, slot};
  }
  menuEntries[menuCount++] = {Item::NewShelf, 0};
  requestUpdate();
}

void LibraryShelvesActivity::chooseMenu(const int index) {
  if (menu == Menu::None || index < 0 || index >= menuCount || menuRow < 0) return;
  const MenuEntry entry = menuEntries[index];
  const int bookId = tab != TabShelves && menuRow < static_cast<int>(books.size()) ? books[menuRow].id : -1;
  switch (entry.item) {
    case Item::Reading:
    case Item::Finished:
    case Item::Paused: {
      const stats::BookStatus status = entry.item == Item::Reading    ? stats::BookStatus::Reading
                                       : entry.item == Item::Finished ? stats::BookStatus::Finished
                                                                      : stats::BookStatus::Paused;
      READING_STATS.setStatus(bookId, status, stats::today());
      break;
    }
    case Item::ToShelf:
      openShelfPicker();
      return;
    case Item::Shelf:
      READING_STATS.setOnShelf(bookId, entry.shelf, ((books[menuRow].shelves >> entry.shelf) & 1u) == 0);
      break;
    case Item::NewShelf:
      askShelfName(-1);
      return;
    case Item::Rename:
      askShelfName(shelfRows[menuRow].slot);
      return;
    case Item::Delete:
      READING_STATS.deleteShelf(shelfRows[menuRow].slot);
      break;
  }
  closeMenu();
  reload();
  entryRefresh = true;
}

void LibraryShelvesActivity::closeMenu() {
  RenderLock lock(*this);
  menu = Menu::None;
  menuRow = -1;
  menuCount = 0;
  if (tab != TabShelves) shelfRows.clear();
  requestUpdate();
}

void LibraryShelvesActivity::askShelfName(const int renameSlot) {
  const int bookId =
      menu == Menu::PickShelf && menuRow >= 0 && menuRow < static_cast<int>(books.size()) ? books[menuRow].id : -1;
  std::string initial;
  if (renameSlot >= 0) {
    for (const ShelfRow& row : shelfRows) {
      if (row.slot == renameSlot) initial = row.name;
    }
  }
  closeMenu();
  app.clearTapFlash();
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SHELF_NAME), initial,
                                                           stats::SHELF_NAME_BYTES - 1);
  if (!keyboard) {
    LOG_ERR("STAT", "OOM: shelf name keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, renameSlot, bookId](const ActivityResult& result) {
    const auto* entry = std::get_if<KeyboardResult>(&result.data);
    if (!result.isCancelled && entry) {
      if (renameSlot >= 0) {
        READING_STATS.renameShelf(static_cast<uint8_t>(renameSlot), entry->text.c_str());
      } else {
        const int slot = READING_STATS.createShelf(entry->text.c_str());
        if (slot >= 0 && bookId >= 0) READING_STATS.setOnShelf(bookId, static_cast<uint8_t>(slot), true);
      }
    }
    reload();
    entryRefresh = true;
  });
}

void LibraryShelvesActivity::loop() {
  const auto route = routeTouch(mappedInput, true);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  if (menu != Menu::None) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closeMenu();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      chooseMenu(menuFocus);
    } else {
      ButtonNavigator::onNextPress([this] { moveFocus(1); });
      ButtonNavigator::onPreviousPress([this] { moveFocus(-1); });
    }
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    RenderLock lock(*this);
    const int visible = visibleRows.load();
    const int last = std::max(0, rowCount() - visible);
    top = std::clamp(top + (swipe == MappedInputManager::SwipeDir::Up ? visible : -visible), 0, last);
    thumbsChecked = false;
    requestUpdate();
    return;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, statsui::kLongPressMs)) {
    // wasLongPressed() also swallows this hold's release, so it cannot pick a menu entry.
    buttonsUsed = true;
    if (focus >= 0) openMenu(focus);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (focus < 0) {
      finish();
      return;
    }
    // From a row, Back first returns focus to the tab strip.
    RenderLock lock(*this);
    focus = -1;
    top = 0;
    thumbsChecked = false;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    buttonsUsed = true;
    if (focus < 0) {
      selectTab(tab + 1);
    } else {
      activateRow(focus);
    }
    return;
  }
  ButtonNavigator::onNextPress([this] { moveFocus(1); });
  ButtonNavigator::onPreviousPress([this] { moveFocus(-1); });
  buttonNavigator.onNextContinuous([this] { jumpPage(1); });
  buttonNavigator.onPreviousContinuous([this] { jumpPage(-1); });
}

int LibraryShelvesActivity::drawTabs(UiScreen& screen, const int stripTop, const int width) {
  constexpr int kStripPad = 12;
  const int firstX = kListBarX + kSelectionBarWidth + kTabBarGap;
  const int room = width - firstX - kSidePadding;
  // Regular labels plus the widest SemiBold gain pick the font, so it does not
  // change with the active tab; the text role never drops to the caption.
  int font = kText10Font;
  int regularWidth = 0;
  for (const int candidate : {kText12Font, kText10Font}) {
    int boldGain = 0;
    regularWidth = 0;
    for (const StrId label : kTabLabels) {
      const int regular = renderer.getTextWidth(candidate, I18N.get(label));
      regularWidth += regular;
      boldGain = std::max(boldGain, renderer.getTextWidth(candidate, I18N.get(label), kSemiBold) - regular);
    }
    font = candidate;
    if (regularWidth + boldGain + kTabBarRun + (TabCount - 2) * kTabGapMin <= room) break;
  }
  // Only the active label is SemiBold. The gap before it also holds its bar
  // clear of the previous label; the other gaps share what is left.
  const char* activeLabel = I18N.get(kTabLabels[tab]);
  const int textWidth =
      regularWidth + renderer.getTextWidth(font, activeLabel, kSemiBold) - renderer.getTextWidth(font, activeLabel);
  const int spare = room - textWidth - (tab > 0 ? kTabBarRun : 0);
  const int plainGaps = tab > 0 ? TabCount - 2 : TabCount - 1;
  const int tabGap = std::clamp(spare / plainGaps, kTabGapMin, kTabGap);
  const int lineHeight = renderer.getLineHeight(font);
  const int stripHeight = std::max(kMinTouch, lineHeight + 2 * kStripPad);
  const int textTop = stripTop + (stripHeight - lineHeight) / 2;
  // The bar marks the focused element only: with a row focused, the active tab is underlined.
  const bool listFocused = buttonsUsed && focus >= 0 && menu == Menu::None;
  int x = firstX;
  for (int i = 0; i < TabCount; ++i) {
    const bool active = i == tab;
    const auto style = active ? kSemiBold : EpdFontFamily::REGULAR;
    const char* label = I18N.get(kTabLabels[i]);
    const int labelWidth = renderer.getTextWidth(font, label, style);
    if (active && listFocused) {
      renderer.fillRect(x, textTop + lineHeight, labelWidth, kTabUnderline, true);
    } else if (active) {
      drawSelectionBar(renderer, x - kTabBarGap - kSelectionBarWidth, textTop, lineHeight);
    }
    renderer.drawText(font, x, textTop, label, true, style);
    screen.frame().hit(fui::makeRect(x - tabGap / 2, stripTop, labelWidth + tabGap, stripHeight), ACTION_TAB,
                       static_cast<int16_t>(i), fui::InputTouch);
    if (kTabDividers && i + 1 < TabCount && i + 1 != tab) {
      renderer.fillRect(x + labelWidth + tabGap / 2, textTop + (lineHeight - kTabDividerHeight) / 2, 1,
                        kTabDividerHeight, true);
    }
    x += labelWidth + (i + 1 == tab ? std::max(tabGap, kTabBarRun) : tabGap);
  }
  drawRule(renderer, 0, stripTop + stripHeight, width);
  return stripTop + stripHeight + kRuleWidth;
}

void LibraryShelvesActivity::drawBookRow(UiScreen& screen, const int index, const int rowTop, const int width) {
  BookRow& book = books[index];
  if (!book.thumbResolved) {
    book.thumb = statsui::thumbPath(book.path, kCoverHeight);
    book.thumbResolved = true;
  }
  const int line10 = renderer.getLineHeight(kText10Font);
  const int coverY = rowTop + (kBookRowHeight - kRuleWidth - kCoverHeight) / 2;
  covers.draw(renderer, book.thumb, book.title.c_str(), kRowTextX, coverY, kCoverWidth, kCoverHeight);
  if (buttonsUsed && focus == index && menu == Menu::None) {
    drawSelectionBar(renderer, kListBarX, coverY, kCoverHeight);
  }
  const int textX = kRowTextX + kCoverWidth + 16;
  const int textWidth = width - textX - kSidePadding;
  // Title in the text role, SemiBold, up to two lines; under it "author · progress".
  const auto titleLines = renderer.wrappedText(kText10Font, book.title.c_str(), textWidth, 2, kSemiBold);
  const int titleHeight = std::max(1, static_cast<int>(titleLines.size())) * line10;
  const int textTop = rowTop + (kBookRowHeight - kRuleWidth - titleHeight - line10) / 2;
  int lineTop = textTop;
  for (const auto& line : titleLines) {
    renderer.drawText(kText10Font, textX, lineTop, line.c_str(), true, kSemiBold);
    lineTop += line10;
  }
  std::string detail = book.detail;
  if (book.author[0] != '\0') {
    // The author gives way first, so the progress always shows.
    static constexpr char kDot[] = " \u00B7 ";
    const int room = textWidth - renderer.getTextWidth(kText10Font, (std::string(kDot) + book.detail).c_str());
    detail = renderer.truncatedText(kText10Font, book.author, std::max(0, room)) + kDot + book.detail;
  }
  renderer.drawText(kText10Font, textX, lineTop,
                    renderer.truncatedText(kText10Font, detail.c_str(), textWidth).c_str());
  drawRule(renderer, 0, rowTop + kBookRowHeight - kRuleWidth, width);
  screen.frame().hit(fui::makeRect(0, rowTop, width, kBookRowHeight), ACTION_ROW, static_cast<int16_t>(index),
                     fui::InputTouch | fui::InputLongPress);
}

void LibraryShelvesActivity::describeMenu(const int index, const freeink::Icon*& icon, const char*& label, void* ctx) {
  const auto* self = static_cast<const LibraryShelvesActivity*>(ctx);
  const MenuEntry entry = self->menuEntries[index];
  switch (entry.item) {
    case Item::Finished:
      icon = &icon_check_24;
      label = tr(STR_SHELF_MARK_FINISHED);
      break;
    case Item::Paused:
      icon = &icon_pause_24;
      label = tr(STR_SHELF_PAUSE);
      break;
    case Item::Reading:
      icon = &icon_book_open_24;
      label = tr(STR_SHELF_MARK_READING);
      break;
    case Item::ToShelf:
      icon = &icon_library_24;
      label = tr(STR_SHELF_ADD_TO);
      break;
    case Item::Rename:
      icon = &icon_pencil_24;
      label = tr(STR_RENAME);
      break;
    case Item::Delete:
      icon = &icon_trash_2_24;
      label = tr(STR_DELETE);
      break;
    case Item::Shelf: {
      for (const ShelfRow& row : self->shelfRows) {
        if (row.slot == entry.shelf) label = row.name.c_str();
      }
      const int row = self->menuRow;
      const bool onShelf =
          row >= 0 && row < static_cast<int>(self->books.size()) && ((self->books[row].shelves >> entry.shelf) & 1u);
      icon = onShelf ? &icon_check_24 : &icon_library_24;
      break;
    }
    case Item::NewShelf:
      icon = &icon_plus_24;
      label = tr(STR_SHELF_NEW);
      break;
  }
}

void LibraryShelvesActivity::drawMenu(UiScreen& screen, const int areaTop, const int bottom, const int width) {
  const char* title = "";
  if (menu == Menu::Shelf && menuRow < static_cast<int>(shelfRows.size())) {
    title = shelfRows[menuRow].name.c_str();
  } else if (menu != Menu::Shelf && menuRow < static_cast<int>(books.size())) {
    title = books[menuRow].title.c_str();
  }
  statsui::drawMenuBox(renderer, screen, title, menuCount, menuFocus, buttonsUsed, &describeMenu, this, areaTop, bottom,
                       width, ACTION_MENU, ACTION_DISMISS, tab == TabShelves ? kShelfRowHeight : kBookRowHeight);
}

void LibraryShelvesActivity::draw(UiScreen& screen) {
  const auto bands = statsui::drawHeader(renderer, tr(STR_LIBRARY), true);
  const int width = bands.width;
  const int listTop = drawTabs(screen, bands.contentTop, width);
  const int rowHeight = tab == TabShelves ? kShelfRowHeight : kBookRowHeight;
  const int visible = std::max(1, (bands.contentBottom - listTop) / rowHeight);
  visibleRows = visible;
  const int count = rowCount();
  if (count == 0) {
    renderer.drawText(kText12Font, kRowTextX, listTop + kSidePadding, I18N.get(kEmptyText[tab]));
  }
  for (int i = top; i < std::min(count, top + visible); ++i) {
    const int rowTop = listTop + (i - top) * rowHeight;
    if (tab != TabShelves) {
      drawBookRow(screen, i, rowTop, width);
      continue;
    }
    const bool focused = buttonsUsed && focus == i && menu == Menu::None;
    if (i == static_cast<int>(shelfRows.size())) {
      statsui::drawIconRow(renderer, screen.target(), &icon_plus_24, tr(STR_SHELF_NEW), nullptr, kListIconX, rowTop,
                           width - kListIconX - kListValueInset, rowHeight, focused);
    } else {
      const ShelfRow& shelf = shelfRows[i];
      char countText[32];
      snprintf(countText, sizeof(countText),
               I18N.get(statsui::plural(shelf.books, StrId::STR_STATS_BOOKS_ONE, StrId::STR_STATS_BOOKS_FEW,
                                        StrId::STR_STATS_BOOKS_MANY)),
               static_cast<unsigned>(shelf.books));
      statsui::drawIconRow(renderer, screen.target(), &icon_library_24, shelf.name.c_str(), countText, kListIconX,
                           rowTop, width - kListIconX - kListValueInset, rowHeight, focused);
    }
    screen.frame().hit(fui::makeRect(0, rowTop, width, rowHeight), ACTION_ROW, static_cast<int16_t>(i),
                       fui::InputTouch | fui::InputLongPress);
  }
  if (menu != Menu::None) drawMenu(screen, listTop, bands.contentBottom, width);
}

bool LibraryShelvesActivity::generateMissingThumbs() {
  bool generated = false;
  const int last = std::min(static_cast<int>(books.size()), top + visibleRows.load());
  for (int i = top; i < last; ++i) {
    BookRow& book = books[i];
    if (book.thumb.empty() || Storage.exists(book.thumb.c_str())) continue;
    if (statsui::generateThumb(book.path, kCoverHeight)) {
      generated = true;
    } else {
      book.thumb.clear();  // no usable cover: never retried while the screen lives
    }
  }
  return generated;
}

void LibraryShelvesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  statsui::drawHints(renderer, mappedInput, focus < 0 ? tr(STR_SHELF_TAB_NEXT) : tr(STR_SELECT));
  renderer.displayBuffer(entryRefresh ? cadence.entry() : cadence.update());
  entryRefresh = false;
  // Visible covers without a thumb at this height get one, then a single repaint.
  if (!thumbsChecked && tab != TabShelves) {
    thumbsChecked = true;
    if (generateMissingThumbs()) requestUpdate();
  }
}
