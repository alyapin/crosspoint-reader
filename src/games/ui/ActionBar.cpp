#include "ActionBar.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "components/UITheme.h"
#include "components/setka/SetkaStyle.h"

namespace fui = freeink::ui;
using namespace setka;

namespace games::ui {

namespace {
constexpr int kIconSize = 24;
constexpr int kIconGap = 4;
constexpr int kResultLineGap = 4;
constexpr const char* kStatusJoin = "\xC2\xB7";  // between spaces: a trailing space adds no width

// Walks the status left to right from `x`; draws when `draw`. Returns the x after it.
int layoutStatus(const GfxRenderer& renderer, int x, const int y, const StatusItem* items, const int count,
                 const bool draw) {
  const int space = renderer.getSpaceWidth(kText12Font);
  const auto text = [&](const char* s, const EpdFontFamily::Style style) {
    if (draw) renderer.drawText(kText12Font, x, y, s, true, style);
    x += renderer.getTextWidth(kText12Font, s, style);
  };
  for (int i = 0; i < count; ++i) {
    const StatusItem& item = items[i];
    if (i > 0) {
      x += space;
      text(kStatusJoin, EpdFontFamily::REGULAR);
      x += space;
    }
    if (item.label != nullptr) text(item.label, EpdFontFamily::REGULAR);
    if (item.label != nullptr && item.value != nullptr) x += space;
    if (item.value != nullptr) text(item.value, kSemiBold);
  }
  return x;
}
}  // namespace

int statusWidth(const GfxRenderer& renderer, const StatusItem* items, const int count) {
  return layoutStatus(renderer, 0, 0, items, count, false);
}

void drawStatus(const GfxRenderer& renderer, const int right, const int y, const StatusItem* items, const int count) {
  layoutStatus(renderer, right - statusWidth(renderer, items, count), y, items, count, true);
}

void drawActionBar(const GfxRenderer& renderer, UiAppHost::UiScreen& screen, const int top, const ActionSlot* slots,
                   const int count, const int focused, const uint16_t input) {
  const int width = renderer.getScreenWidth();
  // Ends above the button hints on button boards.
  const int bottom = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight;
  drawRule(renderer, 0, top, width);
  const int cellTop = top + kRuleWidth;
  const int cellHeight = bottom - cellTop;
  const int captionHeight = renderer.getLineHeight(kCaptionFont);
  // A short bar closes the gap under the icon first.
  const int gap = std::clamp((cellHeight - kIconSize - captionHeight) / 3, 0, kIconGap);
  const int iconTop = cellTop + (cellHeight - kIconSize - gap - captionHeight) / 2;
  const bool dividers = std::count_if(slots, slots + count, [](const ActionSlot& s) { return s.label; }) > 1;
  for (int i = 0; i < count; ++i) {
    const int x0 = i * width / count;
    const int x1 = (i + 1) * width / count;
    if (i > 0 && dividers) renderer.fillRect(x0, cellTop, 1, cellHeight, true);
    const ActionSlot& slot = slots[i];
    if (slot.label == nullptr) continue;
    const int iconX = x0 + (x1 - x0 - kIconSize) / 2;
    if (slot.icon != nullptr) {
      screen.target().bitmap(fui::makeRect(iconX, iconTop, kIconSize, kIconSize), fui::bitmapFromIcon(*slot.icon),
                             fui::BitmapMode::Center);
    }
    const int textWidth = renderer.getTextWidth(kCaptionFont, slot.label);
    renderer.drawText(kCaptionFont, x0 + (x1 - x0 - textWidth) / 2, iconTop + kIconSize + gap, slot.label);
    if (slot.active) drawCellFrame(renderer, x0, cellTop, x1 - x0, cellHeight);
    if (i == focused) drawSelectionBar(renderer, iconX - (kRowTextX - kSelectionBarX), iconTop, kIconSize);
    screen.frame().hit(fui::makeRect(x0, cellTop, x1 - x0, cellHeight), slot.action, slot.value, input);
  }
}

int drawResult(const GfxRenderer& renderer, const int top, const char* result, const char* detail, const char* cue) {
  int y = top;
  renderer.drawText(kText12Font, kSidePadding, y, result, true, kSemiBold);
  y += renderer.getLineHeight(kText12Font);
  for (const char* line : {detail, cue}) {
    if (line == nullptr || *line == '\0') continue;
    y += kResultLineGap;
    renderer.drawText(kText10Font, kSidePadding, y, line);
    y += renderer.getLineHeight(kText10Font);
  }
  return y;
}

int resultHeight(const GfxRenderer& renderer, const bool detail, const bool cue) {
  return renderer.getLineHeight(kText12Font) +
         (static_cast<int>(detail) + static_cast<int>(cue)) * (kResultLineGap + renderer.getLineHeight(kText10Font));
}

}  // namespace games::ui
