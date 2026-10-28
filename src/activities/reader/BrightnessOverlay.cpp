#include "BrightnessOverlay.h"

#include <GfxRenderer.h>

#include <cstdio>

#include "BrightnessSwipe.h"
#include "components/icons/customListIcons.h"
#include "components/setka/SetkaStyle.h"

namespace {
// Measured off the approved 480x800 mockup. Vertical placement scales with the
// screen height so landscape keeps the proportions.
constexpr int kPanelWidth = 80;
constexpr int kPanelRightMargin = 20;
constexpr int kPanelTopPercent = 20;
constexpr int kPanelHeightPercent = 55;
constexpr int kNumberTop = 24;  // digit tops below the panel top
constexpr int kBarWidth = 24;
constexpr int kBarTop = 52;
constexpr int kBarBottomGap = 68;  // bar bottom above the panel bottom
constexpr int kSunBottomGap = 31;  // icon bottom above the panel bottom
constexpr int kMarkerInset = 7;    // strip marker dots, from the right edge
constexpr int kMarkerDot = 3;
constexpr int kMarkerPitch = 6;

// freeink::Icon rows are MSB-first with 0 = ink, not pre-rotated: plotting
// through drawPixel() keeps it upright in every orientation.
void drawIcon(const GfxRenderer& renderer, const freeink::Icon& icon, const int x, const int y) {
  const int rowBytes = (icon.w + 7) / 8;
  for (int row = 0; row < icon.h; ++row) {
    for (int col = 0; col < icon.w; ++col) {
      if (((icon.bits[row * rowBytes + (col >> 3)] >> (7 - (col & 7))) & 1) == 0) {
        renderer.drawPixel(x + col, y + row, true);
      }
    }
  }
}
}  // namespace

void brightness_overlay::draw(const GfxRenderer& renderer, const int level) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  const int panelX = screenW - kPanelRightMargin - kPanelWidth;
  const int panelY = screenH * kPanelTopPercent / 100;
  const int panelH = screenH * kPanelHeightPercent / 100;
  // Clear the panel and everything right of it, and a lane under the marker,
  // so neither sits on glyph slivers when the page margin is narrow.
  const auto strip = brightness_swipe::strip(screenW, screenH);
  const int markerX = screenW - kMarkerInset;
  renderer.fillRect(markerX - 1, strip.top, kMarkerDot + 2, strip.bottom - strip.top, false);
  renderer.fillRect(panelX, panelY, screenW - panelX, panelH, false);
  for (int y = strip.top + kMarkerPitch; y + kMarkerDot < strip.bottom; y += kMarkerPitch) {
    renderer.fillRect(markerX, y, kMarkerDot, kMarkerDot, true);
  }
  renderer.drawRect(panelX, panelY, kPanelWidth, panelH, setka::kRuleWidth, true);

  char text[8];
  snprintf(text, sizeof(text), "%d", level);
  const int textW = renderer.getTextWidth(setka::kHeroMiniFont, text);
  setka::drawHeroNumber(renderer, setka::kHeroMiniFont, panelX + (kPanelWidth - textW) / 2, panelY + kNumberTop, text);

  const int barX = panelX + (kPanelWidth - kBarWidth) / 2;
  setka::drawProgressBarVertical(renderer, barX, panelY + kBarTop, kBarWidth, panelH - kBarTop - kBarBottomGap, level);

  const auto& sun = icon_sun_filled_24;
  drawIcon(renderer, sun, panelX + (kPanelWidth - sun.w) / 2, panelY + panelH - kSunBottomGap - sun.h);
}
