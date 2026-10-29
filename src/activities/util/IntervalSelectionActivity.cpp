#include "IntervalSelectionActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "components/UITheme.h"
#include "components/UiSliderDialog.h"
#include "components/setka/SetkaStyle.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_SLIDER = 1;
constexpr fui::ActionId ACTION_STEP = 2;
constexpr fui::ActionId ACTION_OK = 4;
constexpr fui::ActionId ACTION_CHROME = 5;  // absorbs taps on the dialog body; no handler
}  // namespace

IntervalSelectionActivity::IntervalSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     const char* activityName, const StrId titleId,
                                                     const int initialValue, const int minValue, const int maxValue,
                                                     const int smallStep, const int largeStep,
                                                     const StrId valueFormatId, const bool readerActivity,
                                                     const StrId maxBoundaryLabelId)
    : Activity(activityName, renderer, mappedInput),
      UiAppHost(renderer),
      titleId(titleId),
      valueFormatId(valueFormatId),
      maxBoundaryLabelId(maxBoundaryLabelId),
      value(initialValue),
      minValue(minValue),
      maxValue(maxValue),
      smallStep(smallStep),
      largeStep(largeStep),
      readerActivity(readerActivity) {}

int IntervalSelectionActivity::clampedValue(const int candidate) const {
  return std::clamp(candidate, minValue, maxValue);
}

void IntervalSelectionActivity::onEnter() {
  Activity::onEnter();
  value = clampedValue(value);
  resetUi();
  app.on(ACTION_SLIDER, &IntervalSelectionActivity::onSliderEvent, this);
  app.on(ACTION_STEP, &IntervalSelectionActivity::onStepEvent, this);
  app.on(ACTION_OK, &IntervalSelectionActivity::onOkEvent, this);
  app.setScreen(&IntervalSelectionActivity::intervalScreen, this);
  requestUpdate();
}

void IntervalSelectionActivity::adjustValue(const int delta) {
  value = clampedValue(value + delta);
  requestUpdate();
}

void IntervalSelectionActivity::setValue(const int candidate) {
  const int clamped = clampedValue(candidate);
  if (clamped == value) return;
  value = clamped;
  requestUpdate();
}

void IntervalSelectionActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void IntervalSelectionActivity::confirm() {
  setResult(IntervalResult{static_cast<uint32_t>(value)});
  finish();
}

void IntervalSelectionActivity::onSliderEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<IntervalSelectionActivity*>(user);
  if (event.dragPermille < 0) return;
  const int range = std::max(1, self->maxValue - self->minValue);
  self->setValue(self->minValue + (static_cast<int>(event.dragPermille) * range + 500) / 1000);
}

void IntervalSelectionActivity::onStepEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<IntervalSelectionActivity*>(user);
  self->adjustValue(event.value * self->smallStep);
}

void IntervalSelectionActivity::onOkEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<IntervalSelectionActivity*>(user);
  self->app.clearTapFlash();  // the tap leaves this screen
  self->confirm();
}

void IntervalSelectionActivity::loop() {
  // Touch goes through the FreeInkApp: render() registered the slider, -/+ zones,
  // and Cancel/OK hit rects; the slider follows the finger via InputDrag. Runs
  // before the Back handler because the release of a drag can also register as a
  // swipe (e.g. the left-edge rightward back gesture) — the drag must consume it
  // so it can't cancel the dialog.
  const auto route = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) {
    if (route.event.dragPermille >= 0) draggingSlider = true;
    return;
  }
  if (routingReady() && draggingSlider) {
    // Drag ended (possibly off the slider): swallow the tap/swipe events it produced.
    if (!route.snap.touchHeld) draggingSlider = false;
    return;
  }
  // Tap released outside the dialog (inside-taps are absorbed by the chrome
  // guard): cancel. Swipe-end releases arrive with -1,-1 coords and fall
  // through — same rule as OptionPopup.
  if (route.routed && route.snap.touchReleased && route.snap.touchX >= 0) {
    cancel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirm();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustValue(-smallStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustValue(smallStep); });

  // On edge-button boards (X3, X4 Pro) the side buttons sit on the left/right edges of the screen rather
  // than as a vertical up/down rocker (X4), so BTN_UP is physically the left button and BTN_DOWN the right
  // one. Flip the large-step direction there so the left button decreases and the right button increases.
  const int upDelta = gpio.hasEdgeSideButtons() ? -largeStep : largeStep;
  const int downDelta = gpio.hasEdgeSideButtons() ? largeStep : -largeStep;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, upDelta] { adjustValue(upDelta); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [this, downDelta] { adjustValue(downDelta); });
}

void IntervalSelectionActivity::formatValue(char* buffer, const size_t size, const int forValue) const {
  if (maxBoundaryLabelId != StrId::STR_NONE_OPT && forValue == maxValue) {
    snprintf(buffer, size, "%s", I18N.get(maxBoundaryLabelId));
  } else if (valueFormatId != StrId::STR_NONE_OPT) {
    snprintf(buffer, size, I18N.get(valueFormatId), static_cast<unsigned int>(forValue));
  } else {
    snprintf(buffer, size, "%d", forValue);
  }
}

void IntervalSelectionActivity::intervalScreen(UiScreen& screen, void* user) {
  static_cast<IntervalSelectionActivity*>(user)->buildIntervalScreen(screen);
}

void IntervalSelectionActivity::formatSteps(char* buffer, const size_t size) const {
  char steps[2][24];
  const int values[2] = {smallStep, largeStep};
  for (int i = 0; i < 2; ++i) {
    if (valueFormatId != StrId::STR_NONE_OPT) {
      snprintf(steps[i], sizeof(steps[i]), I18N.get(valueFormatId), static_cast<unsigned int>(values[i]));
    } else {
      snprintf(steps[i], sizeof(steps[i]), "%d", values[i]);
    }
  }
  snprintf(buffer, size, tr(STR_STEP_HINT_KEYS), steps[0], steps[1]);
}

void IntervalSelectionActivity::buildIntervalScreen(UiScreen& screen) {
  char readout[64];
  formatValue(readout, sizeof(readout), value);

  char minText[32];
  formatValue(minText, sizeof(minText), minValue);
  char maxText[32];
  formatValue(maxText, sizeof(maxText), maxValue);

  UiSliderDialogSpec spec;
  spec.title = I18N.get(titleId);
  spec.readout = readout;
  spec.value = value - minValue;
  spec.max = std::max(1, maxValue - minValue);
  spec.minLabel = minText;
  spec.maxLabel = maxText;
  spec.sliderAction = ACTION_SLIDER;
  spec.stepAction = ACTION_STEP;
  spec.okAction = ACTION_OK;
  spec.chromeAction = ACTION_CHROME;
  buildSliderDialogScreen(screen, uiTarget, mappedInput, spec);
}

// The value as the shared dialog form draws it: its number in the Unbounded
// digits, any words of the format (or the boundary label) in the UI face.
void IntervalSelectionActivity::drawValue(const int x, const int capTop) const {
  using namespace setka;
  const int baseline = capTop + heroCapHeight(renderer, kDigitsFont);
  const int textTop = baseline - renderer.getFontAscenderSize(kText12Font);
  const char* format = valueFormatId != StrId::STR_NONE_OPT ? I18N.get(valueFormatId) : "%u";
  const char* number = strstr(format, "%u");
  if ((maxBoundaryLabelId != StrId::STR_NONE_OPT && value == maxValue) || number == nullptr) {
    char text[64];
    formatValue(text, sizeof(text), value);
    renderer.drawText(kText12Font, x, textTop, text, true, kSemiBold);
    return;
  }
  char part[32];
  snprintf(part, sizeof(part), "%.*s", static_cast<int>(number - format), format);
  int left = x;
  if (*part) {
    renderer.drawText(kText12Font, left, textTop, part, true, kSemiBold);
    left += renderer.getTextWidth(kText12Font, part, kSemiBold);
  }
  snprintf(part, sizeof(part), "%d", value);
  left += drawHeroNumber(renderer, kDigitsFont, left, capTop, part);
  renderer.drawText(kText12Font, left, textTop, number + 2, true, kSemiBold);
}

// Button boards: the shared dialog form (as «Сбросить текст?»): the screen
// cleared to the status line, a full-width ruled band, a square card on it.
void IntervalSelectionActivity::drawKeyDialog() const {
  using namespace setka;
  constexpr int kPadding = 24;     // OptionPopup kDialogPadding
  constexpr int kTextInset = 32;   // OptionPopup kDialogSidePadding
  constexpr int kBandMargin = 20;  // OptionPopup kBandMargin
  constexpr int kGap = 12;
  const int width = renderer.getScreenWidth();
  const int cardX = kSidePadding;
  const int cardWidth = width - 2 * kSidePadding;
  const int textX = cardX + kTextInset;
  const int textWidth = cardWidth - 2 * kTextInset;
  const int titleLine = renderer.getLineHeight(kText12Font);
  const int captionLine = renderer.getLineHeight(kCaptionFont);
  const int valueHeight = heroCapHeight(renderer, kDigitsFont);
  // The steps are a key hint: the dense face, one line where it fits.
  char steps[96];
  formatSteps(steps, sizeof(steps));
  const auto stepLines = renderer.wrappedText(kDenseFont, steps, textWidth, 2);
  const int stepLine = renderer.getLineHeight(kDenseFont);
  const int cardHeight = kPadding + titleLine + kGap + valueHeight + 2 * kGap + captionLine + 4 + kProgressHeight +
                         kGap + static_cast<int>(stepLines.size()) * stepLine + kPadding;
  const int cardY = (renderer.getScreenHeight() - cardHeight) / 2;

  drawRule(renderer, 0, cardY - kBandMargin - kRuleWidth, width);
  drawRule(renderer, 0, cardY + cardHeight + kBandMargin, width);
  renderer.drawRect(cardX, cardY, cardWidth, cardHeight, kRuleWidth, true);

  int y = cardY + kPadding;
  renderer.drawText(kText12Font, textX, y,
                    renderer.truncatedText(kText12Font, I18N.get(titleId), textWidth, kSemiBold).c_str(), true,
                    kSemiBold);
  y += titleLine + kGap;
  drawValue(textX, y);
  y += valueHeight + 2 * kGap;

  char text[64];
  formatValue(text, sizeof(text), minValue);
  renderer.drawText(kCaptionFont, textX, y, text);
  formatValue(text, sizeof(text), maxValue);
  renderer.drawText(kCaptionFont, textX + textWidth - renderer.getTextWidth(kCaptionFont, text), y, text);
  y += captionLine + 4;
  const int range = std::max(1, maxValue - minValue);
  drawProgressBar(renderer, textX, y, textWidth, (value - minValue) * 100 / range);
  y += kProgressHeight + kGap;

  // Front buttons do the small step, side buttons the large step.
  for (const auto& line : stepLines) {
    renderer.drawText(kDenseFont, textX, y, line.c_str());
    y += stepLine;
  }
}

void IntervalSelectionActivity::render(RenderLock&&) {
  if (mappedInput.hasTouch()) {
    // No clearScreen: the dialog is a popup — it dims the frame it opened over
    // and draws the card on top. Everything renders through the app so the
    // interactive elements register touch hit rects.
    renderUi();
  } else {
    renderer.clearScreen();
    setka::drawStatusLine(renderer);
    drawKeyDialog();
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "\xE2\x88\x92", "+");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
