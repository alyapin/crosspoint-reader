#include "UiTabListActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cassert>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/setka/SetkaStyle.h"

namespace fui = freeink::ui;

namespace {
constexpr int16_t TOUCH_TAB_BAR_HEIGHT = 50;
}

UiTabListActivity::UiTabListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const bool wantsTouchLongPress)
    : UiListActivity(name, renderer, mappedInput, wantsTouchLongPress) {}

void UiTabListActivity::onEnter() {
  // Size the per-tab state before the base resets activeNav() (which indexes
  // into it).
  tabNavs.assign(static_cast<size_t>(tabCount()), fui::ListNav{});
  UiListActivity::onEnter();
  app.on(ACTION_TAB, &UiTabListActivity::tabActionTrampoline, this);
}

fui::ListNav& UiTabListActivity::activeNav() {
  if (tabNavs.empty()) return nav;  // pre-onEnter fallback
  // Invariant: subclasses keep activeTab() inside [0, tabCount()), and
  // tabCount() does not change after onEnter() sized tabNavs.
  assert(activeTab() >= 0 && static_cast<size_t>(activeTab()) < tabNavs.size());
  return tabNavs[static_cast<size_t>(activeTab())];
}

int UiTabListActivity::ringPos() const {
  if (tabNavs.empty()) return 0;
  assert(activeTab() >= 0 && static_cast<size_t>(activeTab()) < tabNavs.size());
  return tabNavs[static_cast<size_t>(activeTab())].selected;
}

void UiTabListActivity::tabActionTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiTabListActivity*>(user);
  if (event.value < 0 || event.value >= self->tabCount()) return;
  self->onTabAction(event.value);
}

void UiTabListActivity::onRowAction(const fui::ActionEvent& event) {
  activeNav().selected = event.value + 1;  // ring position, not row index
  if (event.longPress) {
    onRowLongPress(event.value);
    return;
  }
  activateIndex(event.value);
}

void UiTabListActivity::moveRingTo(const int ringIndex) {
#if FREEINK_CAP_TOUCH
  closeSwipeDelete();
#endif
  activeNav().requestSelection(ringIndex);
  requestUpdate();
}

void UiTabListActivity::navigateButtons() {
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
      mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    navigationStartedOnTabs = ringPos() == 0;
  }
  // Buttons walk the tab band (index 0) plus the rows (1..listCount).
  const int ringSize = listCount() + 1;
  buttonNavigator.onNextPress([this, ringSize] { moveRingTo(ButtonNavigator::nextIndex(ringPos(), ringSize)); });
  buttonNavigator.onPreviousPress(
      [this, ringSize] { moveRingTo(ButtonNavigator::previousIndex(ringPos(), ringSize)); });
  buttonNavigator.onNextContinuous([this] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
    } else if (ringPos() == 0 && listCount() > 0) {
      activeNav().selected = 1;
    }
    stepTab(1);
  });
  buttonNavigator.onPreviousContinuous([this] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
    } else if (ringPos() == 0 && listCount() > 0) {
      activeNav().selected = 1;
    }
    stepTab(-1);
  });
}

void UiTabListActivity::syncTabListViewport(UiScreen& screen, fui::ListProps& props) {
  syncListViewport(screen, props, 1);
}

void UiTabListActivity::buildTabBar(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Tabs. The selected pill dims to a dither when the selection is down in
  // the list (the legacy focused/unfocused tab distinction).
  // Stack array, not a heap vector: this runs on every render and the tab
  // count is small and fixed.
  constexpr int MAX_TABS = 8;
  const int count = tabCount() < MAX_TABS ? tabCount() : MAX_TABS;
  fui::TabItem tabs[MAX_TABS];
  for (int i = 0; i < count; i++) {
    tabs[i].label = tabLabel(i);
    tabs[i].value = static_cast<int16_t>(i);
    tabs[i].selected = activeTab() == i;
    tabs[i].indicator = tabIndicator(i);
  }
  fui::TabBarProps tabProps;
  tabProps.tabs = tabs;
  tabProps.count = static_cast<uint16_t>(count);
  tabProps.action = ACTION_TAB;
  tabProps.inputMask = fui::InputTouch;
  // Pill shape and label size are theme-driven. Lyra uses equal-width slots
  // with small labels so wide text (e.g. "Controls") still fits at large UI scales.
  // Full-slot (RoundedRaff): the pill fills its slot like the legacy layout
  // (slot minus a 4px frame, 8px clearance above the divider) with
  // body-size labels; zero horizontal contentInset disables the tabBar's
  // label-width shrink.
  const bool tabsFocused = ringPos() == 0;
  if (metrics.tabPillFullSlot) {
    tabProps.text = screen.theme().bodyText;
    tabProps.tabInset = fui::Insets{4, 4, 7, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  } else {
    tabProps.text = screen.theme().smallText;
    tabProps.gap = static_cast<int16_t>(metrics.tabSpacing);
    // Unfocused state: no bottom inset, so the pill (and the 2px selected
    // underline drawn along its bottom edge) reaches the band's 1px divider —
    // legacy Lyra drew the underline sitting on that rule, not floating above.
    tabProps.tabInset = tabsFocused ? fui::Insets{2, 4, 4, 4} : fui::Insets{2, 4, 0, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  }
  const int16_t tabLineHeight = screen.target().lineHeight(tabProps.text.font);
  const int16_t preferredTabHeight =
      mappedInput.hasTouch() ? TOUCH_TAB_BAR_HEIGHT : static_cast<int16_t>(metrics.tabBarHeight);
  const int16_t tabBand = preferredTabHeight > tabLineHeight + 10 ? preferredTabHeight : tabLineHeight + 10;
  if (screen.theme().listSelectionStyle == fui::SelectionStyle::LeftBar) {
    buildSetkaTabStrip(screen, tabBand);
    return;
  }

  if (tabPillMaxPad > 0) {
    // Cap each pill at its label plus this padding: the equal-width slots (and
    // so the tab positions) stay exactly where they were, only the pill stops
    // stretching across the whole slot. The SDK shrinks the pill to content
    // width and centers it in its slot when the horizontal contentInset is
    // nonzero.
    tabProps.contentInset.left = tabPillMaxPad;
    tabProps.contentInset.right = tabPillMaxPad;
  }

  // Legacy Lyra two-state treatment: with the selection on the tab band, the
  // band fills gray and the active tab is a solid pill; with the selection
  // down in the list, the band is plain and the active tab keeps a gray box
  // with an underline. The 1px rule under the band is always there, drawn
  // full-width below (not by tabBar, whose rect is inset for side padding).
  fui::StyleSet tabStyles;
  tabStyles.explicitlySet = true;
  tabStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  if (tabsFocused) {
    tabStyles.selected.background = fui::Paint::solid(fui::Color::Black);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = screen.theme().listRowRadius;
  } else if (metrics.tabPillFullSlot) {
    // Legacy RoundedRaff unfocused treatment: same pill, dimmed to dark gray,
    // text stays inverted; no underline.
    tabStyles.selected.background = fui::Paint::dither(fui::Color::DarkGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = screen.theme().listRowRadius;
  } else {
    tabStyles.selected.background = fui::Paint::dither(fui::Color::LightGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::Black);
    tabProps.selectedUnderline = 2;
  }
  // Focus/flash states keep the pill instead of falling back to an unset
  // (blank) style.
  tabStyles.focused = tabStyles.selected;
  tabStyles.active = tabStyles.selected;
  tabProps.tabStyles = tabStyles;
  const fui::Rect contentTabRect = screen.takeTop(tabBand);
  const fui::Rect frameRect = screen.frame().screen();
  const fui::Rect safeRect = screen.frame().safeRect();
  // The tab background reaches the display edges, but stops before the button labels.
  const fui::Rect tabRect{static_cast<int16_t>(contentTabRect.x - safeRect.x), contentTabRect.y,
                          static_cast<int16_t>(contentTabRect.width + frameRect.width - safeRect.width),
                          contentTabRect.height};
  // Focused band wash is the Lyra treatment; legacy RoundedRaff keeps the
  // band plain in both states.
  if (tabsFocused && !metrics.tabPillFullSlot) {
    screen.target().fill(tabRect, fui::Paint::dither(fui::Color::LightGray));
  }
  // Leave space on both sides of the tab labels and selection highlights.
  // Draw the line below the tabs across the full background width.
  const auto divider = static_cast<int16_t>(std::max(1, metrics.headerUnderlineSize));
  const auto side = static_cast<int16_t>(metrics.contentSidePadding);
  const fui::Rect slotsRect{static_cast<int16_t>(contentTabRect.x + side), tabRect.y,
                            static_cast<int16_t>(contentTabRect.width - 2 * side),
                            static_cast<int16_t>(tabRect.height - divider)};
  tabProps.divider = false;
  fui::tabBar(screen.frame(), slotsRect, tabProps);
  screen.target().fill(fui::Rect{tabRect.x, static_cast<int16_t>(tabRect.bottom() - divider), tabRect.width, divider},
                       fui::Paint::solid(fui::Color::Black));
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
}

// Setka tabs, as in the Library: labels from the left in the UI 9 text role,
// the active one SemiBold with the 10 px bar before it, the rule below. A
// sort indicator follows its label as an arrow. The bar marks the focus: with
// a list row focused by keys, the active tab is underlined instead.
void UiTabListActivity::buildSetkaTabStrip(UiScreen& screen, const int16_t tabBand) {
  constexpr int kBarGap = 6;  // the bar ends this far left of its label
  constexpr int kTabGap = 26;
  constexpr int kTabGapMin = setka::kSelectionBarWidth + 2 * kBarGap;  // still clears the bar
  const auto& metrics = UITheme::getInstance().getMetrics();
  auto& target = screen.target();
  const fui::Rect band = screen.takeTop(tabBand);
  const fui::Rect frameRect = screen.frame().screen();
  const fui::Rect safeRect = screen.frame().safeRect();
  fui::TextStyle regular = screen.theme().smallText;
  regular.maxLines = 1;
  fui::TextStyle active = regular;
  active.bold = true;
  const auto styleFor = [&](const int i) -> const fui::TextStyle& { return i == activeTab() ? active : regular; };
  const auto labelFor = [this](const int i, char (&buf)[48]) -> const char* {
    const fui::TabIndicator indicator = tabIndicator(i);
    if (indicator == fui::TabIndicator::None) return tabLabel(i);
    snprintf(buf, sizeof(buf), "%s %s", tabLabel(i),
             indicator == fui::TabIndicator::Up ? "\xE2\x86\x91" : "\xE2\x86\x93");
    return buf;
  };
  char buf[48];

  const int count = tabCount();
  const int firstX = band.x + setka::kSelectionBarX + setka::kSelectionBarWidth + kBarGap;
  int textWidth = 0;
  for (int i = 0; i < count; ++i) textWidth += target.measureText(regular.font, labelFor(i, buf), styleFor(i)).width;
  const int spare = band.right() - metrics.contentSidePadding - firstX - textWidth;
  const int gap = count > 1 ? std::clamp(spare / (count - 1), kTabGapMin, kTabGap) : kTabGap;
  const auto divider = static_cast<int16_t>(std::max(1, metrics.headerUnderlineSize));
  const int stripBottom = band.bottom() - divider;
  const int16_t lineHeight = target.lineHeight(regular.font);
  const auto textTop = static_cast<int16_t>(band.y + (stripBottom - band.y - lineHeight) / 2);
  const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
  const bool listFocused = !mappedInput.hasTouch() && ringPos() != 0;
  int x = firstX;
  for (int i = 0; i < count; ++i) {
    const char* label = labelFor(i, buf);
    const auto labelWidth = static_cast<int16_t>(target.measureText(regular.font, label, styleFor(i)).width);
    if (i == activeTab() && listFocused) {
      target.fill(fui::makeRect(x, textTop + lineHeight, labelWidth, setka::kTabUnderline), ink);
    } else if (i == activeTab()) {
      target.fill(
          fui::makeRect(x - kBarGap - setka::kSelectionBarWidth, textTop, setka::kSelectionBarWidth, lineHeight), ink);
    }
    target.text(fui::makeRect(x, textTop, labelWidth, lineHeight), label, styleFor(i));
    const int hitLeft = std::max<int>(band.x, x - gap / 2);
    const int hitRight = std::min<int>(band.right(), x + labelWidth + gap / 2);
    screen.frame().hit(fui::makeRect(hitLeft, band.y, hitRight - hitLeft, stripBottom - band.y), ACTION_TAB,
                       static_cast<int16_t>(i), fui::InputTouch);
    x += labelWidth + gap;
  }
  // The rule runs to the display edges, like the Lyra band's divider.
  target.fill(fui::makeRect(band.x - safeRect.x, stripBottom, band.width + frameRect.width - safeRect.width, divider),
              ink);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
}
