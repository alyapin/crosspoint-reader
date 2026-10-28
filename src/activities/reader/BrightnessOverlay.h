#pragma once

class GfxRenderer;

namespace brightness_overlay {

// Paints the brightness level panel near the right edge, and a dotted marker
// along the swipe strip, over the page in the framebuffer. No refresh.
void draw(const GfxRenderer& renderer, int level);

}  // namespace brightness_overlay
