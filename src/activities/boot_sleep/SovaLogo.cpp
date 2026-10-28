#include "SovaLogo.h"

#if SOVA_LOGO
#include <BitmapHelpers.h>
#include <GfxRenderer.h>

#include "fontIds.h"
#include "images/SovaMark.h"
#include "images/SovaWordmark.h"

namespace {
constexpr int kMarkSize = 280;
constexpr int kWordmarkWidth = 276;
constexpr int kWordmarkHeight = 56;
constexpr int kWordmarkGap = 20;
constexpr int kCreditGap = 16;

// Owl, wordmark and boot credit line centered as one group.
int markTop(const GfxRenderer& renderer) {
  const int groupHeight =
      kMarkSize + kWordmarkGap + kWordmarkHeight + kCreditGap + renderer.getLineHeight(SMALL_FONT_ID);
  return (renderer.getScreenHeight() - groupHeight) / 2;
}

// 2 bpp image (0 = black .. 3 = white).
void drawGrayImage(const GfxRenderer& renderer, const uint8_t* data, const int x, const int y, const int width,
                   const int height, const bool grayFollows, const bool inverted) {
  const int rowBytes = (width + 3) / 4;
  const auto mode = renderer.getRenderMode();
  for (int row = 0; row < height; row++) {
    for (int col = 0; col < width; col++) {
      const uint8_t stored = data[row * rowBytes + col / 4] >> (6 - (col % 4) * 2) & 0x3;
      const uint8_t level = inverted ? 3 - stored : stored;
      if (mode == GfxRenderer::BW) {
        renderer.drawPixel(x + col, y + row, level < (grayFollows ? 3 : 2));
      } else {
        const auto pixel = grayPlanePixel(level, mode == GfxRenderer::GRAYSCALE_MSB, renderer.grayPlanesAreAbsolute());
        if (pixel.write) renderer.drawPixel(x + col, y + row, pixel.black);
      }
    }
  }
}
}  // namespace

namespace sova {
void drawLogo(const GfxRenderer& renderer, const bool grayFollows, const bool inverted) {
  const int pageWidth = renderer.getScreenWidth();
  const int top = markTop(renderer);
  drawGrayImage(renderer, SovaMark, (pageWidth - kMarkSize) / 2, top, kMarkSize, kMarkSize, grayFollows, inverted);
  drawGrayImage(renderer, SovaWordmark, (pageWidth - kWordmarkWidth) / 2, top + kMarkSize + kWordmarkGap,
                kWordmarkWidth, kWordmarkHeight, grayFollows, inverted);
}

int creditTop(const GfxRenderer& renderer) {
  return markTop(renderer) + kMarkSize + kWordmarkGap + kWordmarkHeight + kCreditGap;
}
}  // namespace sova
#endif
