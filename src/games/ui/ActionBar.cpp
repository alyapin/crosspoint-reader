#include "ActionBar.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "components/setka/SetkaStyle.h"

namespace fui = freeink::ui;
using namespace setka;

namespace games::ui {

namespace {
constexpr int kIconSize = 24;
constexpr int kIconGap = 4;
}  // namespace

void drawActionBar(const GfxRenderer& renderer, UiAppHost::UiScreen& screen, const int top, const ActionSlot* slots,
                   const int count, const int focused, const uint16_t input) {
  const int width = renderer.getScreenWidth();
  const int bottom = renderer.getScreenHeight();
  drawRule(renderer, 0, top, width);
  const int cellTop = top + kRuleWidth;
  const int cellHeight = bottom - cellTop;
  const int captionHeight = renderer.getLineHeight(kCaptionFont);
  // A short bar (the scanword keyboard's) closes the gap under the icon first.
  const int gap = std::clamp((cellHeight - kIconSize - captionHeight) / 3, 0, kIconGap);
  const int iconTop = cellTop + (cellHeight - kIconSize - gap - captionHeight) / 2;
  for (int i = 0; i < count; ++i) {
    const int x0 = i * width / count;
    const int x1 = (i + 1) * width / count;
    if (i > 0) renderer.fillRect(x0, cellTop, 1, cellHeight, true);
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

}  // namespace games::ui
