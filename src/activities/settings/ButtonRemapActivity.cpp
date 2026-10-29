#include "ButtonRemapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/setka/SetkaStyle.h"

namespace fui = freeink::ui;

namespace {
// UI steps correspond to logical roles in order: Back, Confirm, Left, Right.
constexpr uint8_t kRoleCount = 4;
// Marker used when a role has not been assigned yet.
constexpr uint8_t kUnassigned = 0xFF;
// Duration to show temporary error text when reassigning a button.
constexpr unsigned long kErrorDisplayMs = 1500;
// Gap between the header and the prompt, and between the notes and the hints.
constexpr int kPromptGap = 12;
}  // namespace

void ButtonRemapActivity::onEnter() {
  Activity::onEnter();

  // Start with all roles unassigned to avoid duplicate blocking.
  currentStep = 0;
  resetAsked = false;
  tempMapping[0] = kUnassigned;
  tempMapping[1] = kUnassigned;
  tempMapping[2] = kUnassigned;
  tempMapping[3] = kUnassigned;
  errorMessage.clear();
  errorUntil = 0;
  for (uint8_t i = 0; i < kRoleCount; ++i) {
    rowItems[i].label = getRoleName(i);
  }
  resetUi();
  app.setScreen(&ButtonRemapActivity::screenTrampoline, this);
  requestUpdate();
}

void ButtonRemapActivity::loop() {
  // Clear any temporary warning after its timeout.
  if (errorUntil > 0 && millis() > errorUntil) {
    errorMessage.clear();
    errorUntil = 0;
    requestUpdate();
    return;
  }

  // Side buttons (fixed, so they work under any front mapping):
  // - Up: ask, then on a second Up reset mapping to defaults and exit.
  // - Down: cancel without saving, or take back the reset question.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (!resetAsked) {
      resetAsked = true;
      requestUpdate();
      return;
    }
    // Persist default mapping immediately so the user can recover quickly.
    SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
    SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
    SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
    SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
    SETTINGS.saveToFile();
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (resetAsked) {
      resetAsked = false;
      requestUpdate();
      return;
    }
    // Exit without changing settings.
    finish();
    return;
  }

  {
    // Make sure UI done rendering before accepting another assignment.
    // This avoids rapid double-presses that can advance the step without a visible redraw.
    RenderLock lock(*this);

    // Wait for a front button press to assign to the current role.
    const int pressedButton = mappedInput.getPressedFrontButton();
    if (pressedButton < 0) {
      return;
    }
    resetAsked = false;

    // Update temporary mapping and advance the remap step.
    // Only accept the press if this hardware button isn't already assigned elsewhere.
    if (!validateUnassigned(static_cast<uint8_t>(pressedButton))) {
      requestUpdate();
      return;
    }
    tempMapping[currentStep] = static_cast<uint8_t>(pressedButton);
    currentStep++;

    if (currentStep >= kRoleCount) {
      // All roles assigned; save to settings and exit.
      applyTempMapping();
      SETTINGS.saveToFile();
      finish();
      return;
    }

    requestUpdate();
  }
}

void ButtonRemapActivity::render(RenderLock&&) {
  const auto labelForHardware = [&](uint8_t hardwareIndex) -> const char* {
    for (uint8_t i = 0; i < kRoleCount; i++) {
      if (tempMapping[i] == hardwareIndex) {
        return getRoleName(i);
      }
    }
    // A key still waiting for its role has no hint.
    return "";
  };

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CAT_CONTROLS));
  renderer.drawText(setka::kCaptionFont, setka::kSidePadding, metrics.topPadding + metrics.headerHeight + kPromptGap,
                    tr(STR_REMAP_PROMPT));
  renderUi();

  // Side-button notes above the hints, bottom up: the reset question replaces both.
  const int line = renderer.getLineHeight(setka::kCaptionFont);
  int y = pageHeight - metrics.buttonHintsHeight - kPromptGap - line;
  if (resetAsked) {
    renderer.drawText(setka::kCaptionFont, setka::kSidePadding, y, tr(STR_REMAP_RESET_CONFIRM_HINT));
  } else {
    renderer.drawText(setka::kCaptionFont, setka::kSidePadding, y, tr(STR_REMAP_CANCEL_HINT));
    y -= line;
    renderer.drawText(setka::kCaptionFont, setka::kSidePadding, y, tr(STR_REMAP_RESET_HINT));
  }
  // Temporary warning for a key that already has a role.
  if (!errorMessage.empty()) {
    renderer.drawText(setka::kCaptionFont, setka::kSidePadding, y - line - kPromptGap, errorMessage.c_str());
  }

  // Live preview of logical labels under front buttons.
  // This mirrors the on-device front button order: Back, Confirm, Left, Right.
  GUI.drawButtonHints(renderer, labelForHardware(CrossPointSettings::FRONT_HW_BACK),
                      labelForHardware(CrossPointSettings::FRONT_HW_CONFIRM),
                      labelForHardware(CrossPointSettings::FRONT_HW_LEFT),
                      labelForHardware(CrossPointSettings::FRONT_HW_RIGHT));
  renderer.displayBuffer();
}

void ButtonRemapActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<ButtonRemapActivity*>(user)->buildScreen(screen);
}

void ButtonRemapActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int topOffset = metrics.topPadding + metrics.headerHeight + kPromptGap +
                        renderer.getLineHeight(setka::kCaptionFont) + metrics.verticalSpacing;
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + topOffset),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width) + metrics.verticalSpacing),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.verticalSpacing),
                  static_cast<int16_t>(safe.x + metrics.verticalSpacing)});

  for (uint8_t i = 0; i < kRoleCount; ++i) {
    const uint8_t assignedButton = tempMapping[i];
    rowItems[i].value = assignedButton == kUnassigned ? tr(STR_UNASSIGNED) : getHardwareName(assignedButton);
  }

  fui::ListProps props;
  props.items = rowItems;
  props.count = kRoleCount;
  props.selectedIndex = currentStep;
  props.inputMask = fui::InputNone;
  props.scrollIndicator = false;
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  screen.list(props);
}

void ButtonRemapActivity::applyTempMapping() {
  // Commit temporary mapping into settings (logical role -> hardware).
  SETTINGS.frontButtonBack = tempMapping[0];
  SETTINGS.frontButtonConfirm = tempMapping[1];
  SETTINGS.frontButtonLeft = tempMapping[2];
  SETTINGS.frontButtonRight = tempMapping[3];
}

bool ButtonRemapActivity::validateUnassigned(const uint8_t pressedButton) {
  // Block reusing a hardware button already assigned to another role.
  for (uint8_t i = 0; i < kRoleCount; i++) {
    if (tempMapping[i] == pressedButton && i != currentStep) {
      errorMessage = tr(STR_ALREADY_ASSIGNED);
      errorUntil = millis() + kErrorDisplayMs;
      return false;
    }
  }
  return true;
}

const char* ButtonRemapActivity::getRoleName(const uint8_t roleIndex) {
  switch (roleIndex) {
    case 0:
      return tr(STR_BACK);
    case 1:
      return tr(STR_SELECT);
    case 2:
      return tr(STR_DIR_LEFT);
    case 3:
    default:
      return tr(STR_DIR_RIGHT);
  }
}

const char* ButtonRemapActivity::getHardwareName(const uint8_t buttonIndex) const {
  switch (buttonIndex) {
    case CrossPointSettings::FRONT_HW_BACK:
      return tr(STR_HW_BACK_LABEL);
    case CrossPointSettings::FRONT_HW_CONFIRM:
      return tr(STR_HW_CONFIRM_LABEL);
    case CrossPointSettings::FRONT_HW_LEFT:
      return tr(STR_HW_LEFT_LABEL);
    case CrossPointSettings::FRONT_HW_RIGHT:
      return tr(STR_HW_RIGHT_LABEL);
    default:
      return "Unknown";
  }
}
