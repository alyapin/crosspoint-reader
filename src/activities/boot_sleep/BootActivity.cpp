#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"

// Sova branding on the Xteink boards the fork ships for; other boards keep the upstream logo.
#define SOVA_BOOT_LOGO (FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3)

#if SOVA_BOOT_LOGO
#include <BitmapHelpers.h>

#include "activities/reader/ReaderUtils.h"
#include "images/SovaMark.h"
#include "images/SovaWordmark.h"

namespace {
constexpr int kMarkSize = 280;
constexpr int kWordmarkWidth = 276;
constexpr int kWordmarkHeight = 56;
constexpr int kWordmarkGap = 20;
constexpr int kCreditGap = 16;

// 2 bpp image (0 = black .. 3 = white). The B/W pass inks every non-white level
// when a gray overlay follows (the overlay lightens them), else thresholds at mid-gray.
void drawGrayImage(const GfxRenderer& renderer, const uint8_t* data, const int x, const int y, const int width,
                   const int height, const bool grayFollows) {
  const int rowBytes = (width + 3) / 4;
  const auto mode = renderer.getRenderMode();
  for (int row = 0; row < height; row++) {
    for (int col = 0; col < width; col++) {
      const uint8_t level = data[row * rowBytes + col / 4] >> (6 - (col % 4) * 2) & 0x3;
      if (mode == GfxRenderer::BW) {
        if (level < (grayFollows ? 3 : 2)) renderer.drawPixel(x + col, y + row);
      } else {
        const auto pixel = grayPlanePixel(level, mode == GfxRenderer::GRAYSCALE_MSB, renderer.grayPlanesAreAbsolute());
        if (pixel.write) renderer.drawPixel(x + col, y + row, pixel.black);
      }
    }
  }
}
}  // namespace
#else
#include "images/Logo120.h"
#endif

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
#if SOVA_BOOT_LOGO
  const int creditHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int markY = (pageHeight - (kMarkSize + kWordmarkGap + kWordmarkHeight + kCreditGap + creditHeight)) / 2;
  const int wordmarkY = markY + kMarkSize + kWordmarkGap;
  const bool gray = renderer.grayscaleCapabilities().supported();
  const auto drawLogo = [&] {
    drawGrayImage(renderer, SovaMark, (pageWidth - kMarkSize) / 2, markY, kMarkSize, kMarkSize, gray);
    drawGrayImage(renderer, SovaWordmark, (pageWidth - kWordmarkWidth) / 2, wordmarkY, kWordmarkWidth, kWordmarkHeight,
                  gray);
  };
  drawLogo();
  renderer.drawCenteredText(SMALL_FONT_ID, wordmarkY + kWordmarkHeight + kCreditGap, tr(STR_BASED_ON_CROSSPOINT));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  if (!gray) {
    renderer.displayBuffer();
    return;
  }
  renderer.displayGrayscaleBase();
  ReaderUtils::renderAntiAliased(renderer, drawLogo);
#else
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
#endif
}
