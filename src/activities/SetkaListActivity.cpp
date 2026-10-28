#include "SetkaListActivity.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>

#include <utility>

#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
// Setka navigation row pitch.
constexpr int kRowHeight = setka::kNavRowHeight;
constexpr int kRowGap = 0;
constexpr int kIconSize = 24;
constexpr int kIconGap = 16;
}  // namespace

SetkaListActivity::SetkaListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity(name, renderer, mappedInput) {}

void SetkaListActivity::onEnter() {
  UiListActivity::onEnter();
  entryRefresh = true;
}

void SetkaListActivity::openChild(std::unique_ptr<Activity> child) {
  startActivityForResult(std::move(child), [this](const ActivityResult&) { entryRefresh = true; });
}

bool SetkaListActivity::handleCustomInput() {
  // The rows fit one screen: a vertical swipe has nothing to scroll, so it
  // must not cost a repaint.
  const auto swipe = mappedInput.wasSwipe();
  return swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down;
}

void SetkaListActivity::buildScreen(UiScreen& screen) {
  using namespace setka;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(kText12Font);
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  const int count = listCount();
  if (count == 0) {
    if (const char* empty = emptyText()) renderer.drawText(kText12Font, kSidePadding, listTop + kRowGap, empty);
    return;
  }
  const int selected = nav.selected;
  for (int i = 0; i < count; ++i) {
    const int top = listTop + i * (kRowHeight + kRowGap);
    const bool focused = i == selected;
    if (focused) drawSelectionBar(renderer, kSelectionBarX, top, kRowHeight);
    int x = kRowTextX;
    if (const freeink::Icon* icon = rowIcon(i)) {
      screen.target().bitmap(fui::makeRect(x, top + (kRowHeight - kIconSize) / 2, kIconSize, kIconSize),
                             fui::bitmapFromIcon(*icon), fui::BitmapMode::Center);
      x += kIconSize + kIconGap;
    }
    const int textTop = top + (kRowHeight - lineHeight) / 2;
    renderer.drawText(kText12Font, x, textTop, rowLabel(i), true, focused ? kSemiBold : EpdFontFamily::REGULAR);
    if (const char* status = rowStatus(i); status && *status) {
      // Shares the label's baseline.
      const int statusTop =
          textTop + renderer.getFontAscenderSize(kText12Font) - renderer.getFontAscenderSize(kText10Font);
      renderer.drawText(kText10Font, width - kSidePadding - renderer.getTextWidth(kText10Font, status), statusTop,
                        status);
    }
    screen.frame().hit(fui::makeRect(0, top, width, kRowHeight), ACTION_ROW, static_cast<int16_t>(i), fui::InputTouch);
  }
}

void SetkaListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  renderer.displayBuffer(entryRefresh.exchange(false) ? cadence.entry() : cadence.update());
}
