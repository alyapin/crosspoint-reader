#pragma once

class GfxRenderer;

// Sova branding on the Xteink boards the fork ships for; other boards keep the upstream logo.
#define SOVA_LOGO (FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3)

namespace sova {
// Owl + wordmark in the current render mode, placed identically on the boot and sleep screens.
// In B/W, grayFollows inks every non-white level for a gray pass to refine; otherwise mid-gray is the threshold.
// inverted swaps the levels (white owl) for a black background.
void drawLogo(const GfxRenderer& renderer, bool grayFollows, bool inverted = false);
// Top of the boot screen's credit line under the wordmark.
int creditTop(const GfxRenderer& renderer);
}  // namespace sova
