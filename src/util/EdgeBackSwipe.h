#pragma once

#include <cstdlib>

// Android-style Back: a flick that starts at the left or right edge and moves
// inward. Pure geometry over the SDK swipe endpoints (logical px), so host
// tests can drive it.
namespace edge_back_swipe {

// Start band from either side edge: Android's 24 dp (3.8 mm, about 33 px on
// this ~220 ppi glass) plus the 7 px the X4 Pro bezel overlaps.
constexpr int kEdgeBandPx = 40;
// The SDK's flick floor (InputManager::TOUCH_SWIPE_MIN_PX), so no edge flick
// falls between Back and a plain swipe.
constexpr int kMinTravelPx = 60;

inline bool isBackSwipe(const int sx, const int sy, const int ex, const int ey, const int screenW) {
  const int dx = ex - sx;
  const int adx = std::abs(dx);
  const bool fromLeft = sx < kEdgeBandPx && dx > 0;
  const bool fromRight = sx >= screenW - kEdgeBandPx && dx < 0;
  return (fromLeft || fromRight) && adx >= kMinTravelPx && adx > 2 * std::abs(ey - sy);
}

}  // namespace edge_back_swipe
