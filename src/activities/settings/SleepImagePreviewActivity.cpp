#include "SleepImagePreviewActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <utility>

#include "MappedInputManager.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "components/UITheme.h"
#include "components/setka/SetkaStyle.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_BAR = 1;  // value: 0 = back, 1 = set
constexpr int kBarHeight = 72;
constexpr int kLabelGap = 10;  // between the selection bar and "Set"

int barTop(const GfxRenderer& renderer) { return renderer.getScreenHeight() - kBarHeight; }
}  // namespace

SleepImagePreviewActivity::SleepImagePreviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     std::string path)
    : Activity("SleepImagePreview", renderer, mappedInput), UiAppHost(renderer), path(std::move(path)) {}

void SleepImagePreviewActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_BAR, &SleepImagePreviewActivity::onAction, this);
  app.setScreen(&SleepImagePreviewActivity::screenFn, this);
  requestUpdate();
}

void SleepImagePreviewActivity::screenFn(UiScreen& screen, void* user) {
  // Hit rects only: the bar is painted with the picture (paintBar), in every
  // plane of the gray pipeline.
  const auto& self = *static_cast<SleepImagePreviewActivity*>(user);
  const int width = self.renderer.getScreenWidth();
  const int top = barTop(self.renderer);
  screen.frame().hit(fui::makeRect(0, top, width / 2, kBarHeight), ACTION_BAR, 0, fui::InputTouch);
  screen.frame().hit(fui::makeRect(width / 2, top, width - width / 2, kBarHeight), ACTION_BAR, 1, fui::InputTouch);
}

void SleepImagePreviewActivity::paintBar(GfxRenderer& renderer, const bool grayPlane, void* user) {
  using namespace setka;
  const auto& self = *static_cast<const SleepImagePreviewActivity*>(user);
  const int width = renderer.getScreenWidth();
  if (!self.mappedInput.hasTouch()) {
    // Button boards: a white band under the usual front-button hints, which
    // handle the gray planes themselves; the picture ends above it.
    const int hintsHeight = UITheme::getInstance().getMetrics().buttonHintsHeight;
    renderer.fillRect(0, renderer.getScreenHeight() - hintsHeight, width, hintsHeight,
                      grayPlane && !renderer.grayPlanesAreAbsolute());
    const auto labels = self.mappedInput.mapLabels(tr(STR_BACK), self.failed ? "" : tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  const int top = barTop(renderer);
  if (grayPlane && !renderer.grayPlanesAreAbsolute()) {
    // Overlay planes mark gray pixels only; clear them under the bar.
    renderer.fillRect(0, top, width, kBarHeight, true);
    return;
  }
  renderer.fillRect(0, top, width, kBarHeight, false);
  drawRule(renderer, 0, top, width);
  const int lineHeight = renderer.getLineHeight(kText12Font);
  const int textTop = top + kRuleWidth + (kBarHeight - kRuleWidth - lineHeight) / 2;
  renderer.drawText(kText12Font, kSidePadding, textTop, tr(STR_SLEEP_IMAGE_PREVIEW_BACK));
  if (self.failed) return;
  const char* set = tr(STR_SLEEP_IMAGE_SET);
  const int setX = width - kSidePadding - renderer.getTextWidth(kText12Font, set, kSemiBold);
  drawSelectionBar(renderer, setX - kLabelGap - kSelectionBarWidth, textTop, lineHeight);
  renderer.drawText(kText12Font, setX, textTop, set, true, kSemiBold);
}

void SleepImagePreviewActivity::onAction(const fui::ActionEvent& event, void* user) {
  static_cast<SleepImagePreviewActivity*>(user)->close(event.value == 1);
}

void SleepImagePreviewActivity::close(const bool set) {
  if (set && !ready) return;
  app.clearTapFlash();
  ActivityResult result;
  result.isCancelled = !set;
  setResult(std::move(result));
  finish();
}

void SleepImagePreviewActivity::loop() {
  if (routeTouch(mappedInput)) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    close(false);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) close(true);
}

void SleepImagePreviewActivity::render(RenderLock&&) {
  ready = false;
  if (SleepActivity::renderPreview(renderer, path, &SleepImagePreviewActivity::paintBar, this)) {
    renderUi();
    ready = true;
    return;
  }
  failed = true;
  renderer.clearScreen();
  renderer.drawCenteredText(setka::kText12Font, barTop(renderer) / 2, tr(STR_FILE_OPEN_FAILED));
  paintBar(renderer, false, this);
  renderUi();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
