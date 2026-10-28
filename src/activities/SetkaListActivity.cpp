#include "SetkaListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <utility>

namespace fui = freeink::ui;

namespace {
constexpr int kHeaderTop = 16;  // clears the X4 Pro top bezel
constexpr int kTitleGap = 20;
constexpr int kRuleGap = 16;
}  // namespace

SetkaListActivity::SetkaListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity(name, renderer, mappedInput) {}

void SetkaListActivity::onEnter() {
  UiListActivity::onEnter();
  entryRefresh = true;
  app.on(ACTION_BACK, &SetkaListActivity::onBack, this);
}

void SetkaListActivity::onBack(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<SetkaListActivity*>(user);
  self->app.clearTapFlash();
  self->finish();
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
  auto& frame = screen.frame();
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(kText12Font);

  const char* back = tr(STR_NAV_BACK);
  renderer.drawText(kText12Font, kSidePadding, kHeaderTop, back);
  frame.hit(
      fui::makeRect(0, 0, renderer.getTextWidth(kText12Font, back) + 2 * kSidePadding, lineHeight + 2 * kHeaderTop),
      ACTION_BACK, 0, fui::InputTouch);
  const int titleTop = kHeaderTop + lineHeight + kTitleGap;
  renderer.drawText(kText12Font, kSidePadding, titleTop, headerTitle(), true, kSemiBold);
  const int ruleY = titleTop + lineHeight + kRuleGap;
  drawRule(renderer, 0, ruleY, width);
  const int listTop = ruleY + kRuleWidth;

  const int count = listCount();
  if (count == 0) {
    if (const char* empty = emptyText()) renderer.drawText(kText12Font, kSidePadding, listTop + kTitleGap, empty);
    return;
  }
  const int rowHeight = std::max(kListRowHeight, lineHeight + 2 * kRuleGap);
  const int selected = nav.selected;
  for (int i = 0; i < count; ++i) {
    const int top = listTop + i * rowHeight;
    const int textTop = top + (rowHeight - lineHeight) / 2;
    const bool focused = i == selected;
    if (focused) drawSelectionBar(renderer, kSelectionBarX, textTop, lineHeight);
    renderer.drawText(kText12Font, kRowTextX, textTop, rowLabel(i), true, focused ? kSemiBold : EpdFontFamily::REGULAR);
    frame.hit(fui::makeRect(0, top, width, rowHeight), ACTION_ROW, static_cast<int16_t>(i), fui::InputTouch);
  }
}

void SetkaListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  renderer.displayBuffer(entryRefresh.exchange(false) ? cadence.entry() : cadence.update());
}
