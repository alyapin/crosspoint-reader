#include "SetkaHomeActivity.h"

#include <Epub.h>
#include <FreeInkUIIcon.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdio>
#include <memory>

#include "CrossPointSettings.h"
#include "MoreActivity.h"
#include "activities/stats/StatsScreens.h"
#include "components/UITheme.h"
#include "components/icons/listIcons.h"
#include "components/setka/setkaIcons.h"
#include "util/ButtonNavigator.h"

namespace fui = freeink::ui;
using namespace setka;

namespace {
constexpr fui::ActionId ACTION_BOOK = 1;
constexpr fui::ActionId ACTION_TAB = 2;
constexpr fui::ActionId ACTION_OPEN_LIBRARY = 3;

constexpr int kStatusTop = 12;  // clears the X4 Pro top bezel
constexpr int kGap = 16;        // between the stacked sections
constexpr int kDigitsGap = 12;  // cover to its percent
constexpr int kMinCoverGap = 12;
constexpr int kCoverRing = 6;  // the focus frame sits this far outside a cover
// Mockup "3в": the current cover 2:3 and about a quarter of the portrait
// screen tall; the others are smaller and bottom-aligned with it.
constexpr int kCurrentCoverHeight = 225;
constexpr int kOtherCoverPercent = 85;
// Tabs show a 32 px icon above the label.
constexpr int kTabBarHeight = 86;
constexpr int kTabIconSize = 32;
constexpr int kTabIconGap = 4;

struct TabSpec {
  StrId label;
  const freeink::Icon* icon;
};
const TabSpec kTabs[] = {{StrId::STR_LIBRARY, &icon_library_32},
                         {StrId::STR_STATS_TITLE, &icon_chart_column_32},
                         {StrId::STR_MORE, &icon_layout_grid_32},
                         {StrId::STR_SETTINGS_TITLE, &icon_sliders_32}};

// Russian plural forms. Recents hold at most 10 books, where the rule picks
// the singular only for 1, which is also right for English.
StrId bookCountFormat(const int count) {
  const int mod10 = count % 10;
  const int mod100 = count % 100;
  if (mod10 == 1 && mod100 != 11) return StrId::STR_BOOK_COUNT_ONE;
  if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return StrId::STR_BOOK_COUNT_FEW;
  return StrId::STR_BOOK_COUNT_MANY;
}

// One parser at a time, off the task stack.
bool generateThumb(RecentBook& book, const int height) {
  if (FsHelpers::hasReflowableBookExtension(book.path)) {
    auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("HOME", "OOM: cover EPUB");
      return false;
    }
    if (!epub->generateThumbBmpFromSource(height)) return false;
    book.coverBmpPath = epub->getThumbBmpPath();
    return true;
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("HOME", "OOM: cover XTC");
      return false;
    }
    if (!xtc->load() || !xtc->generateThumbBmp(height)) return false;
    book.coverBmpPath = xtc->getThumbBmpPath();
    return true;
  }
  return false;
}
}  // namespace

void SetkaHomeActivity::onEnter() {
  // The render task may run as soon as ActivityManager drops its lock; keep it
  // out until the state below is in place (same as LibraryListActivity).
  RenderLock lock(*this);
  Activity::onEnter();
  loadBooks();
  focus = 0;
  entryRefresh = true;
  thumbsChecked = false;
  resetUi();
  app.on(ACTION_BOOK, &SetkaHomeActivity::onBook, this);
  app.on(ACTION_TAB, &SetkaHomeActivity::onTab, this);
  app.on(ACTION_OPEN_LIBRARY, &SetkaHomeActivity::onOpenLibrary, this);
  app.setScreen(&SetkaHomeActivity::screenFn, this);
  requestUpdate();
}

void SetkaHomeActivity::loadBooks() {
  slotCount = 0;
  readingCount = 0;
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (RecentBooksStore::isMissing(book)) continue;
    ++readingCount;
    if (slotCount < kMaxCovers) slots[slotCount++] = Slot{book};
  }
  for (int i = 0; i < slotCount; ++i) {
    Slot& slot = slots[i];
    if (slot.book.title.empty()) slot.book.title = slot.book.path.substr(slot.book.path.find_last_of('/') + 1);
    slot.position = loadBookPosition(slot.book.path);
    if (slot.position.percent >= 0) snprintf(slot.percentText, sizeof(slot.percentText), "%d%%", slot.position.percent);
  }
  currentSubtitle.clear();
  if (slotCount == 0) return;
  const BookPosition& current = slots[0].position;
  if (current.chapter > 0) {
    char text[64];
    snprintf(text, sizeof(text), tr(STR_CHAPTER_OF), current.chapter, current.chapterCount);
    currentSubtitle = text;
  } else {
    currentSubtitle = slots[0].book.author;
  }
}

void SetkaHomeActivity::screenFn(UiScreen& screen, void* user) { static_cast<SetkaHomeActivity*>(user)->draw(screen); }

void SetkaHomeActivity::onBook(const fui::ActionEvent& event, void* user) {
  static_cast<SetkaHomeActivity*>(user)->openBook(event.value);
}

void SetkaHomeActivity::onTab(const fui::ActionEvent& event, void* user) {
  static_cast<SetkaHomeActivity*>(user)->openTab(event.value);
}

void SetkaHomeActivity::onOpenLibrary(const fui::ActionEvent&, void* user) {
  static_cast<SetkaHomeActivity*>(user)->openAllBooks();
}

void SetkaHomeActivity::openBook(const int index) {
  if (index < 0 || index >= slotCount) return;
  app.clearTapFlash();
  activityManager.goToReader(slots[index].book.path);
}

void SetkaHomeActivity::openTab(const int tab) {
  app.clearTapFlash();
  switch (tab) {
    case TabLibrary:
      openLibraryTabs(renderer, mappedInput);
      break;
    case TabStatistics:
      openStatistics(renderer, mappedInput);
      break;
    case TabMore:
      activityManager.replaceActivity(std::make_unique<MoreActivity>(renderer, mappedInput));
      break;
    case TabSettings:
      activityManager.goToSettings();
      break;
    default:
      break;
  }
}

void SetkaHomeActivity::openAllBooks() {
  app.clearTapFlash();
  activityManager.goToLibrary();
}

void SetkaHomeActivity::moveFocus(const int delta) {
  const int count = bookFocusCount() + TabCount;
  focus = (focus + delta + count) % count;
  requestUpdate();
}

void SetkaHomeActivity::activateFocus() {
  const int index = focus;
  if (index >= bookFocusCount()) {
    openTab(index - bookFocusCount());
  } else if (slotCount == 0) {
    openAllBooks();
  } else {
    openBook(index);
  }
}

void SetkaHomeActivity::loop() {
  if (routeTouch(mappedInput)) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Home has nothing to go back to: Back resumes the current book.
    if (slotCount > 0) openBook(0);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateFocus();
    return;
  }
  ButtonNavigator::onNextPress([this] { moveFocus(1); });
  ButtonNavigator::onPreviousPress([this] { moveFocus(-1); });
}

void SetkaHomeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  // Button boards only (drawButtonHints skips touch boards); draw() keeps the tabs above them.
  const auto labels =
      mappedInput.mapLabels(slotCount > 0 ? tr(STR_RESUME) : "", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(entryRefresh ? cadence.entry() : cadence.update());
  entryRefresh = false;
  // The layout pass above set each thumb's height; make the missing ones and
  // repaint once with them, HALF so the placeholders do not ghost through.
  if (!thumbsChecked) {
    thumbsChecked = true;
    if (generateMissingThumbs()) {
      entryRefresh = true;
      requestUpdate();
    }
  }
}

bool SetkaHomeActivity::generateMissingThumbs() {
  bool generated = false;
  for (int i = 0; i < slotCount; ++i) {
    Slot& slot = slots[i];
    if (slot.thumbHeight <= 0 || (!slot.thumbPath.empty() && Storage.exists(slot.thumbPath.c_str()))) continue;
    if (!generateThumb(slot.book, slot.thumbHeight)) continue;
    slot.thumbPath = UITheme::getCoverThumbPath(slot.book.coverBmpPath, slot.thumbHeight);
    generated = true;
  }
  return generated;
}

void SetkaHomeActivity::draw(UiScreen& screen) {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  int insetTop = 0;
  int insetRight = 0;
  int insetBottom = 0;
  int insetLeft = 0;
  renderer.getOrientedViewableTRBL(&insetTop, &insetRight, &insetBottom, &insetLeft);

  drawStatusLine(kStatusTop, width);
  const int headingTop = kStatusTop + renderer.getLineHeight(kText10Font) + kGap;
  renderer.drawText(kText12Font, kSidePadding, headingTop, tr(STR_READING_NOW), true, kSemiBold);
  if (readingCount > 0) {
    char count[32];
    snprintf(count, sizeof(count), I18N.get(bookCountFormat(readingCount)), readingCount);
    // Shares the heading's baseline.
    const int countTop =
        headingTop + renderer.getFontAscenderSize(kText12Font) - renderer.getFontAscenderSize(kText10Font);
    renderer.drawText(kText10Font, width - kSidePadding - renderer.getTextWidth(kText10Font, count), countTop, count);
  }

  const int tabBottom = height - insetBottom - UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int tabTop = tabBottom - kTabBarHeight;
  const int bodyTop = headingTop + renderer.getLineHeight(kText12Font) + kGap;
  if (slotCount > 0) {
    drawBooks(screen, bodyTop, tabTop, width);
  } else {
    drawEmpty(screen, bodyTop, width);
  }
  drawTabs(screen, tabTop, tabBottom, width);
}

void SetkaHomeActivity::drawStatusLine(const int top, const int width) const {
  char text[12];
  if (SETTINGS.clockShowInHeader && halClock.isAvailable() &&
      halClock.formatTime(text, sizeof(text), SETTINGS.clockFormat == 1)) {
    renderer.drawText(kText10Font, kSidePadding, top, text);
  }
  if (SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS) {
    snprintf(text, sizeof(text), "%u%%", std::min<unsigned>(powerManager.getBatteryPercentage(), 100));
    renderer.drawText(kText10Font, width - kSidePadding - renderer.getTextWidth(kText10Font, text), top, text);
  }
}

void SetkaHomeActivity::drawBooks(UiScreen& screen, const int top, const int bottom, const int width) {
  const int line10 = renderer.getLineHeight(kText10Font);
  const int line12 = renderer.getLineHeight(kText12Font);
  const int digitsHeight = heroCapHeight(renderer, kDigitsFont);
  const int textWidth = width - kRowTextX - kSidePadding;
  const char* title = slots[0].book.title.c_str();

  // The current cover is as tall as the mockup's where it fits, and shrinks
  // (landscape) so the percent, the rule and the current-book row stay above the tabs.
  const int contentWidth = width - 2 * kSidePadding;
  int below = kDigitsGap + digitsHeight + kGap + kRuleWidth + kGap + line12 + line10 + kGap;
  // A long title takes a second line where that leaves the cover its full height.
  const bool twoLineTitle = renderer.getTextWidth(kText12Font, title, kSemiBold) > textWidth &&
                            bottom - top - below - line12 >= kCurrentCoverHeight;
  if (twoLineTitle) below += line12;
  const int titleHeight = twoLineTitle ? 2 * line12 : line12;
  const int rowHeight = titleHeight + line10;
  const int heightByWidth =
      (contentWidth - 2 * kMinCoverGap) * 3 * 100 / (2 * (100 + (kMaxCovers - 1) * kOtherCoverPercent));
  const int currentHeight = std::max(1, std::min({kCurrentCoverHeight, bottom - top - below, heightByWidth}));
  const int otherHeight = currentHeight * kOtherCoverPercent / 100;
  const int currentWidth = currentHeight * 2 / 3;
  const int otherWidth = otherHeight * 2 / 3;
  const int gap =
      std::max(kMinCoverGap, (contentWidth - currentWidth - (kMaxCovers - 1) * otherWidth) / (kMaxCovers - 1));
  const int coversBottom = top + currentHeight;
  const int digitsTop = coversBottom + kDigitsGap;
  const int focused = focus;

  int x = kSidePadding;
  for (int i = 0; i < slotCount; ++i) {
    Slot& slot = slots[i];
    const int coverWidth = i == 0 ? currentWidth : otherWidth;
    const int coverHeight = i == 0 ? currentHeight : otherHeight;
    const int y = coversBottom - coverHeight;
    if (slot.thumbHeight != coverHeight) {
      slot.thumbHeight = coverHeight;
      slot.thumbPath = UITheme::getCoverThumbPath(slot.book.coverBmpPath, coverHeight);
      thumbsChecked = false;
    }
    drawCover(slot, x, y, coverWidth, coverHeight);
    // Focus on the current book shows as the bar on its row below; the others get a frame.
    if (i > 0 && i == focused) {
      drawCellFrame(renderer, x - kCoverRing, y - kCoverRing, coverWidth + 2 * kCoverRing,
                    coverHeight + 2 * kCoverRing);
    }
    if (slot.position.percent >= 0) {
      drawHeroNumber(renderer, i == 0 ? kDigitsFont : kDigitsSmallFont, x, digitsTop, slot.percentText);
    }
    screen.frame().hit(fui::makeRect(x, y, coverWidth, digitsTop + digitsHeight - y), ACTION_BOOK,
                       static_cast<int16_t>(i), fui::InputTouch);
    x += coverWidth + gap;
  }

  const int ruleY = digitsTop + digitsHeight + kGap;
  drawRule(renderer, 0, ruleY, width);
  const int rowTop = ruleY + kRuleWidth + kGap;
  if (focused == 0) drawSelectionBar(renderer, kSelectionBarX, rowTop, rowHeight);
  if (twoLineTitle) {
    int lineTop = rowTop;
    for (const auto& line : renderer.wrappedText(kText12Font, title, textWidth, 2, kSemiBold)) {
      renderer.drawText(kText12Font, kRowTextX, lineTop, line.c_str(), true, kSemiBold);
      lineTop += line12;
    }
  } else {
    renderer.drawText(kText12Font, kRowTextX, rowTop,
                      renderer.truncatedText(kText12Font, title, textWidth, kSemiBold).c_str(), true, kSemiBold);
  }
  if (!currentSubtitle.empty()) {
    renderer.drawText(kText10Font, kRowTextX, rowTop + titleHeight,
                      renderer.truncatedText(kText10Font, currentSubtitle.c_str(), textWidth).c_str());
  }
  screen.frame().hit(fui::makeRect(0, ruleY + kRuleWidth, width, rowHeight + 2 * kGap), ACTION_BOOK, 0,
                     fui::InputTouch);
}

void SetkaHomeActivity::drawEmpty(UiScreen& screen, const int top, const int width) {
  const int line12 = renderer.getLineHeight(kText12Font);
  int y = top;
  for (const auto& line : renderer.wrappedText(kText12Font, tr(STR_HOME_EMPTY), width - 2 * kSidePadding, 3)) {
    renderer.drawText(kText12Font, kSidePadding, y, line.c_str());
    y += line12;
  }
  const int ruleY = y + kGap;
  drawRule(renderer, 0, ruleY, width);
  const int rowTop = ruleY + kRuleWidth + kGap;
  if (focus == 0) drawSelectionBar(renderer, kSelectionBarX, rowTop, line12);
  renderer.drawText(kText12Font, kRowTextX, rowTop, tr(STR_OPEN_LIBRARY), true, kSemiBold);
  screen.frame().hit(fui::makeRect(0, ruleY + kRuleWidth, width, line12 + 2 * kGap), ACTION_OPEN_LIBRARY, 0,
                     fui::InputTouch);
}

void SetkaHomeActivity::drawTabs(UiScreen& screen, const int top, const int bottom, const int width) {
  drawRule(renderer, 0, top, width);
  // Captions under the icons: four body-size labels overflow the row in Unbounded.
  const int captionHeight = renderer.getLineHeight(kCaptionFont);
  const int blockHeight = kTabIconSize + kTabIconGap + captionHeight;
  const int blockTop = top + kRuleWidth + (bottom - top - kRuleWidth - blockHeight) / 2;
  // Four labels do not fit equal quarters, so each column is its SemiBold
  // label plus an equal share of the spare width.
  std::array<int, TabCount> columnWidths{};
  int spare = width;
  for (int tab = 0; tab < TabCount; ++tab) {
    columnWidths[tab] =
        std::max(kTabIconSize, renderer.getTextWidth(kCaptionFont, I18N.get(kTabs[tab].label), kSemiBold));
    spare -= columnWidths[tab];
  }
  const int focusedTab = focus - bookFocusCount();
  int columnX = 0;
  for (int tab = 0; tab < TabCount; ++tab) {
    const bool focused = tab == focusedTab;
    const auto style = focused ? kSemiBold : EpdFontFamily::REGULAR;
    const char* label = I18N.get(kTabs[tab].label);
    const int columnWidth = tab + 1 < TabCount ? columnWidths[tab] + std::max(0, spare) / TabCount : width - columnX;
    const int iconX = columnX + (columnWidth - kTabIconSize) / 2;
    const int textX = columnX + (columnWidth - renderer.getTextWidth(kCaptionFont, label, style)) / 2;
    screen.target().bitmap(fui::makeRect(iconX, blockTop, kTabIconSize, kTabIconSize),
                           fui::bitmapFromIcon(*kTabs[tab].icon), fui::BitmapMode::Center);
    // Same bar-to-content distance as the list rows, beside the icon: the
    // labels leave no room for it.
    if (focused) drawSelectionBar(renderer, iconX - (kRowTextX - kSelectionBarX), blockTop, kTabIconSize);
    renderer.drawText(kCaptionFont, textX, blockTop + kTabIconSize + kTabIconGap, label, true, style);
    screen.frame().hit(fui::makeRect(columnX, top, columnWidth, bottom - top), ACTION_TAB, static_cast<int16_t>(tab),
                       fui::InputTouch);
    columnX += columnWidth;
  }
}

void SetkaHomeActivity::drawCover(Slot& slot, const int x, const int y, const int width, const int height) {
  bool drawn = false;
  if (!slot.thumbPath.empty() && Storage.exists(slot.thumbPath.c_str()) &&
      Storage.openFileForRead("HOME", slot.thumbPath, coverFile)) {
    if (coverBitmap.parseHeaders() == BmpReaderError::Ok && coverBitmap.getWidth() > 0 && coverBitmap.getHeight() > 0) {
      drawn = GUI.drawCoverThumbFill(renderer, coverBitmap, Rect{x, y, width, height}, 0);
    }
    coverFile.close();
  }
  if (!drawn) {
    // Placeholder: dithered card with the title on a white label (text never
    // sits on a dither). Caption size, so long words fit the narrow covers.
    constexpr int kPad = 6;
    const int lineHeight = renderer.getLineHeight(kCaptionFont);
    renderer.fillRectDither(x, y, width, height, Color::LightGray);
    const auto lines = renderer.wrappedText(kCaptionFont, slot.book.title.c_str(), width - 4 * kPad, 5);
    renderer.fillRect(x + kPad, y + kPad, width - 2 * kPad, static_cast<int>(lines.size()) * lineHeight + 2 * kPad,
                      false);
    int textTop = y + 2 * kPad;
    for (const auto& line : lines) {
      renderer.drawText(kCaptionFont, x + 2 * kPad, textTop, line.c_str());
      textTop += lineHeight;
    }
  }
  renderer.drawRect(x, y, width, height);
}
