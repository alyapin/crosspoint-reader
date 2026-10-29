#include "ValuePickerActivity.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <Memory.h>

#include <algorithm>
#include <utility>

#include "components/UITheme.h"
#include "components/setka/setkaIcons.h"

namespace fui = freeink::ui;

namespace {
constexpr int kIconSize = 24;
constexpr int kIconGap = 16;
}  // namespace

ValuePickerActivity::ValuePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                                         std::vector<std::string> labels, const int current)
    : UiListActivity("ValuePicker", renderer, mappedInput),
      title(std::move(title)),
      labels(std::move(labels)),
      current(current) {}

void ValuePickerActivity::open(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::string title, std::vector<std::string> labels, const int current,
                               std::function<void(int)> onSelect) {
  auto picker =
      makeUniqueNoThrow<ValuePickerActivity>(renderer, mappedInput, std::move(title), std::move(labels), current);
  if (!picker) {
    LOG_ERR("PICK", "OOM: ValuePickerActivity");
    return;
  }
  host.startActivityForResult(std::move(picker), [onSelect = std::move(onSelect)](const ActivityResult& result) {
    if (result.isCancelled || !onSelect) return;
    if (const auto* picked = std::get_if<IntervalResult>(&result.data)) onSelect(static_cast<int>(picked->value));
  });
}

void ValuePickerActivity::open(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const StrId title, const StrId* options, const int count, const int current,
                               std::function<void(int)> onSelect) {
  std::vector<std::string> labels;
  labels.reserve(count);
  for (int i = 0; i < count; ++i) labels.emplace_back(I18N.get(options[i]));
  open(host, renderer, mappedInput, I18N.get(title), std::move(labels), current, std::move(onSelect));
}

void ValuePickerActivity::onEnter() {
  UiListActivity::onEnter();
  pageTop = 0;
  pageShift = 0;
  entryRefresh = true;
  if (current >= 0 && current < listCount()) nav.requestSelection(current);
}

void ValuePickerActivity::activateIndex(const int index) {
  app.clearTapFlash();
  setResult(ActivityResult(IntervalResult{static_cast<uint32_t>(index)}));
  finish();
}

bool ValuePickerActivity::handleCustomInput() {
  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::Up && swipe != MappedInputManager::SwipeDir::Down) return false;
  if (listCount() > pageRows) {
    pageShift += swipe == MappedInputManager::SwipeDir::Up ? 1 : -1;
    requestUpdate();
  }
  return true;
}

void ValuePickerActivity::navigateButtons() {
  const auto moveTo = [this](const int index) {
    nav.requestSelection(index);
    requestUpdate();
  };
  buttonNavigator.onNextPress([&] { moveTo(ButtonNavigator::nextIndex(nav.selected, listCount())); });
  buttonNavigator.onPreviousPress([&] { moveTo(ButtonNavigator::previousIndex(nav.selected, listCount())); });
  buttonNavigator.onNextContinuous(
      [&] { moveTo(ButtonNavigator::nextPageIndex(nav.selected, listCount(), pageRows)); });
  buttonNavigator.onPreviousContinuous(
      [&] { moveTo(ButtonNavigator::previousPageIndex(nav.selected, listCount(), pageRows)); });
}

void ValuePickerActivity::buildScreen(UiScreen& screen) {
  using namespace setka;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = renderer.getScreenHeight() - (mappedInput.hasTouch() ? 0 : metrics.buttonHintsHeight);
  const int rows = std::max(1, (bottom - listTop) / kListRowHeight);
  pageRows = rows;

  const int count = listCount();
  // Buttons pull the page to the focus; a swipe pages away from it.
  const bool follow = nav.followOnBuild.exchange(false);
  const int selected = nav.selected;
  int first = pageTop + pageShift.exchange(0) * rows;
  if (follow && selected >= 0 && (selected < first || selected >= first + rows)) first = selected / rows * rows;
  first = std::clamp(first, 0, std::max(0, count - 1) / rows * rows);
  pageTop = first;

  const int lineHeight = renderer.getLineHeight(kText12Font);
  const int textX = kRowTextX + kIconSize + kIconGap;
  for (int i = first; i < std::min(count, first + rows); ++i) {
    const int top = listTop + (i - first) * kListRowHeight;
    const bool focused = i == selected;
    if (focused) drawSelectionBar(renderer, kSelectionBarX, top, kListRowHeight);
    screen.target().bitmap(fui::makeRect(kRowTextX, top + (kListRowHeight - kIconSize) / 2, kIconSize, kIconSize),
                           fui::bitmapFromIcon(i == current ? icon_radio_on_24 : icon_radio_24),
                           fui::BitmapMode::Center);
    const auto style = focused ? kSemiBold : EpdFontFamily::REGULAR;
    renderer.drawText(
        kText12Font, textX, top + (kListRowHeight - lineHeight) / 2,
        renderer.truncatedText(kText12Font, labels[i].c_str(), width - kSidePadding - textX, style).c_str(), true,
        style);
    screen.frame().hit(fui::makeRect(0, top, width, kListRowHeight), ACTION_ROW, static_cast<int16_t>(i),
                       fui::InputTouch);
  }
}

void ValuePickerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  renderer.displayBuffer(entryRefresh.exchange(false) ? cadence.entry() : cadence.update());
}
