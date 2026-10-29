#include "SetkaListActivity.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>

#include <algorithm>
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

int SetkaListActivity::rowsPerPage() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  return std::max(1, (renderer.getScreenHeight() - metrics.buttonHintsHeight - listTop) / (kRowHeight + kRowGap));
}

bool SetkaListActivity::handleCustomInput() {
  // A vertical swipe turns the page of a list longer than one screen; on a
  // shorter one it has nothing to scroll and must not cost a repaint.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::Up && swipe != MappedInputManager::SwipeDir::Down) return false;
  const int perPage = rowsPerPage();
  const int page = nav.selected / perPage + (swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
  if (page >= 0 && page * perPage < listCount()) moveSelectionTo(page * perPage);
  return true;
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
  const int perPage = rowsPerPage();
  const int first = selected / perPage * perPage;
  for (int i = first; i < std::min(count, first + perPage); ++i) {
    const int top = listTop + (i - first) * (kRowHeight + kRowGap);
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
