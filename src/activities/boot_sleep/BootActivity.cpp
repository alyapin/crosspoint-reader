#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "SovaLogo.h"
#include "fontIds.h"

#if SOVA_LOGO
#include "activities/reader/ReaderUtils.h"
#else
#include "images/Logo120.h"
#endif

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
#if SOVA_LOGO
  const bool gray = renderer.grayscaleCapabilities().supported();
  const auto drawLogo = [&] { sova::drawLogo(renderer, gray); };
  drawLogo();
  renderer.drawCenteredText(SMALL_FONT_ID, sova::creditTop(renderer), tr(STR_BASED_ON_CROSSPOINT));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  if (!gray) {
    renderer.displayBuffer();
    return;
  }
  renderer.displayGrayscaleBase();
  ReaderUtils::renderAntiAliased(renderer, drawLogo);
#else
  const auto pageWidth = renderer.getScreenWidth();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
#endif
}
