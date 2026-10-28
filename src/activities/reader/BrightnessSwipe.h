#pragma once

#include <FreeInkUICore.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

// Frontlight brightness by a vertical drag along the right edge of the page.
// Pure geometry and gesture state, so host tests can drive it.
//
// Touch zones on the reading surface (logical px, every orientation):
//   left 25%       right-swipe from the edge = Back (the reader ignores it)
//   top 14%        down-swipe = control center
//   bottom 14%     up-swipe = Home, or the reader menu on Home-key boards
//   center third   tap = reader menu
//   elsewhere      tap or horizontal swipe = page turn, hold = clipping
//   right 12% between the top and bottom bands: vertical drag = brightness
// The strip begins strictly inside the top/bottom bands, so no edge swipe can
// start in it. Inside it taps, holds and horizontal swipes keep their meaning:
// a contact only becomes a brightness drag once it has moved kEngagePx and at
// least twice as far vertically as sideways.
namespace brightness_swipe {

constexpr int kStripWidthPercent = 12;
// The whole 0..100 range takes this share of the screen height.
constexpr int kTravelPercent = 70;
// Vertical travel that claims the contact. Larger than the SDK's 28 px tap
// slop, so a claimed contact can no longer fire a long-press.
constexpr int kEngagePx = 32;

struct Strip {
  int left;
  int top;     // exclusive
  int bottom;  // exclusive
};

inline Strip strip(const int screenW, const int screenH) {
  // Same band as fui::edgeSwipe() uses for the top and bottom edges.
  const int band = static_cast<int>(screenH * freeink::ui::EDGE_SWIPE_TOP_BOTTOM_FRAC);
  return {screenW - screenW * kStripWidthPercent / 100, band, screenH - band};
}

inline bool inStrip(const int x, const int y, const int screenW, const int screenH) {
  const Strip s = strip(screenW, screenH);
  return x >= s.left && x < screenW && y > s.top && y < s.bottom;
}

// Level after the finger moved travelPx up (negative = down) from where it
// touched down. The claim threshold is not travel, so the level starts moving
// from where the drag engaged instead of jumping.
inline int levelFor(const int startLevel, const int travelPx, const int screenH, const int minLevel) {
  const int span = std::max(1, screenH * kTravelPercent / 100);
  int effective = 0;
  if (travelPx > kEngagePx) effective = travelPx - kEngagePx;
  if (travelPx < -kEngagePx) effective = travelPx + kEngagePx;
  const int delta = (effective * 100 + (effective >= 0 ? span / 2 : -span / 2)) / span;
  return std::clamp(startLevel + delta, minLevel, 100);
}

class Gesture {
 public:
  // None: not a brightness drag, the reader handles the frame. Every other
  // event means the drag owns the frame, End included: it is the release that
  // would otherwise read as a tap or a swipe.
  enum class Event : uint8_t { None, Start, Move, Hold, End };

  // Touch-down edge: arms only for a contact that starts inside the strip.
  void press(const int x, const int y, const int screenW, const int screenH) {
    phase = inStrip(x, y, screenW, screenH) ? Phase::Armed : Phase::Idle;
    startX = x;
    startY = y;
    lastY = y;
  }

  void cancel() { phase = Phase::Idle; }

  // One loop frame: held = the finger is down at (x, y).
  Event track(const bool held, const int x, const int y) {
    switch (phase) {
      case Phase::Idle:
        return Event::None;
      case Phase::Armed: {
        if (!held) {
          phase = Phase::Idle;
          return Event::None;
        }
        const int dx = std::abs(x - startX);
        const int dy = std::abs(y - startY);
        if (dy >= kEngagePx && dy >= 2 * dx) {
          phase = Phase::Dragging;
          lastY = y;
          return Event::Start;
        }
        if (dx >= kEngagePx) phase = Phase::Idle;  // sideways: a page-turn swipe
        return Event::None;
      }
      case Phase::Dragging:
        if (!held) {
          phase = Phase::Idle;
          return Event::End;
        }
        if (y == lastY) return Event::Hold;
        lastY = y;
        return Event::Move;
    }
    return Event::None;
  }

  bool dragging() const { return phase == Phase::Dragging; }
  // Upward travel from the touch-down point (negative = down).
  int travel() const { return startY - lastY; }

 private:
  enum class Phase : uint8_t { Idle, Armed, Dragging };
  Phase phase = Phase::Idle;
  int startX = 0;
  int startY = 0;
  int lastY = 0;
};

}  // namespace brightness_swipe
