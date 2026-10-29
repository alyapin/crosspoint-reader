#include "AboutActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalFrontlight.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <esp_mac.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
enum MenuItem {
  ITEM_DEVICE = 0,
  ITEM_FIRMWARE,
  ITEM_CHIP,
  ITEM_FLASH,
  ITEM_DISPLAY,
  ITEM_RESOLUTION,
  ITEM_TOUCH,
  ITEM_FRONTLIGHT,
  ITEM_RTC,
  ITEM_IMU,
  ITEM_MAC,
};

constexpr StrId menuNames[AboutActivity::ITEM_COUNT] = {
    StrId::STR_ABOUT_DEVICE,  StrId::STR_ABOUT_FIRMWARE,   StrId::STR_ABOUT_CHIP,  StrId::STR_ABOUT_FLASH,
    StrId::STR_ABOUT_DISPLAY, StrId::STR_ABOUT_RESOLUTION, StrId::STR_ABOUT_TOUCH, StrId::STR_FRONTLIGHT,
    StrId::STR_CLOCK,         StrId::STR_ABOUT_TILT,       StrId::STR_ABOUT_MAC,
};

// Chip part numbers, not user prose — deliberately untranslated.
const char* displayControllerName(const BoardConfig::DisplayController c) {
  switch (c) {
    case BoardConfig::DisplayController::SSD1677:
      return "SSD1677";
    case BoardConfig::DisplayController::UC8253:
      return "UC8253";
    case BoardConfig::DisplayController::ED2208:
      return "ED2208";
    case BoardConfig::DisplayController::LgfxEpd:
      return "LovyanGFX EPD";
    case BoardConfig::DisplayController::IT8951:
      return "IT8951";
    case BoardConfig::DisplayController::UC8279:
      return "UC8279";
    case BoardConfig::DisplayController::UC8179:
      return "UC8179";
    case BoardConfig::DisplayController::UC8279C:
      return "UC8279C";
  }
  return "?";
}

const char* touchControllerName(const BoardConfig::TouchController c) {
  switch (c) {
    case BoardConfig::TouchController::None:
      return nullptr;
    case BoardConfig::TouchController::Chsc6x:
      return "CHSC6X";
    case BoardConfig::TouchController::Gt911:
      return "GT911";
    case BoardConfig::TouchController::Ft5x06:
      return "FT5x06";
    case BoardConfig::TouchController::Ft6336u:
      return "FT6336U";
    case BoardConfig::TouchController::Gslx680:
      return "GSLX680";
  }
  return nullptr;
}
}  // namespace

AboutActivity::AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("About", renderer, mappedInput) {}

const char* AboutActivity::headerTitle() const { return tr(STR_ABOUT); }

void AboutActivity::onEnter() {
  RenderLock lock(*this);  // the base requests the first render before the rows are filled
  UiListActivity::onEnter();
  nav.reset(-1);
  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }

  char buf[32];
  // Hardware and firmware information is fixed after boot; fill it once here.
  // BoardConfig::ACTIVE reflects RUNTIME detection: selectDevice() picked the
  // profile and applyXteinkDisplayController() may have promoted the display
  // controller to the panel actually found on the bus.
  rowValues_[ITEM_DEVICE] = BoardConfig::ACTIVE.name;
  rowValues_[ITEM_FIRMWARE] = CROSSPOINT_VERSION;
  snprintf(buf, sizeof(buf), "%s rev %u", ESP.getChipModel(), static_cast<unsigned>(ESP.getChipRevision()));
  rowValues_[ITEM_CHIP] = buf;
  snprintf(buf, sizeof(buf), tr(STR_SIZE_MB), static_cast<unsigned>(ESP.getFlashChipSize() / (1024u * 1024u)));
  rowValues_[ITEM_FLASH] = buf;
  rowValues_[ITEM_DISPLAY] = displayControllerName(BoardConfig::ACTIVE.displayController);
  snprintf(buf, sizeof(buf), "%u × %u",
           static_cast<unsigned>(std::min(BoardConfig::ACTIVE.displayWidth, BoardConfig::ACTIVE.displayHeight)),
           static_cast<unsigned>(std::max(BoardConfig::ACTIVE.displayWidth, BoardConfig::ACTIVE.displayHeight)));
  rowValues_[ITEM_RESOLUTION] = buf;
  const char* touch = touchControllerName(BoardConfig::ACTIVE.touch.controller);
  rowValues_[ITEM_TOUCH] = touch ? touch : tr(STR_NO);
  rowValues_[ITEM_FRONTLIGHT] = Frontlight.present() ? tr(STR_YES) : tr(STR_NO);
  rowValues_[ITEM_RTC] = halClock.isAvailable() ? tr(STR_YES) : tr(STR_NO);
  rowValues_[ITEM_IMU] = halTiltSensor.isAvailable() ? tr(STR_YES) : tr(STR_NO);
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  rowValues_[ITEM_MAC] = buf;
}

void AboutActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].value = rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = ITEM_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  // Label at the value's size, like the other label + value lists; maxLines=2
  // also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void AboutActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
