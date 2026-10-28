#include <gtest/gtest.h>

#include "activities/reader/BrightnessSwipe.h"

namespace bs = brightness_swipe;
namespace fui = freeink::ui;
using Event = bs::Gesture::Event;

namespace {
struct Size {
  int w;
  int h;
};
constexpr Size kPortrait{480, 800};
constexpr Size kLandscape{800, 480};
}  // namespace

TEST(BrightnessSwipeZone, PortraitStripIsRightTwelvePercentBetweenEdgeBands) {
  const auto s = bs::strip(kPortrait.w, kPortrait.h);
  EXPECT_EQ(s.left, 423);
  EXPECT_EQ(s.top, 112);
  EXPECT_EQ(s.bottom, 688);
  EXPECT_TRUE(bs::inStrip(423, 113, kPortrait.w, kPortrait.h));
  EXPECT_TRUE(bs::inStrip(479, 687, kPortrait.w, kPortrait.h));
  EXPECT_FALSE(bs::inStrip(422, 400, kPortrait.w, kPortrait.h));
  EXPECT_FALSE(bs::inStrip(479, 112, kPortrait.w, kPortrait.h));
  EXPECT_FALSE(bs::inStrip(479, 688, kPortrait.w, kPortrait.h));
}

// No swipe that starts in the strip can be one of the global edge gestures,
// and the strip stays clear of the center menu tap target.
TEST(BrightnessSwipeZone, StripNeverOverlapsOtherGestureZones) {
  for (const Size size : {kPortrait, kLandscape}) {
    const auto s = bs::strip(size.w, size.h);
    EXPECT_GE(s.left, size.w - size.w / 3) << "center menu third";
    for (int x = s.left; x < size.w; x += 3) {
      for (int y = s.top + 1; y < s.bottom; y += 3) {
        ASSERT_TRUE(bs::inStrip(x, y, size.w, size.h));
        EXPECT_FALSE(fui::edgeSwipe(fui::ScreenEdge::Top, x, y, x, y + 300, size.w, size.h)) << x << "," << y;
        EXPECT_FALSE(fui::edgeSwipe(fui::ScreenEdge::Bottom, x, y, x, y - 300, size.w, size.h)) << x << "," << y;
        EXPECT_FALSE(fui::edgeSwipe(fui::ScreenEdge::Left, x, y, x + 300, y, size.w, size.h)) << x << "," << y;
      }
    }
  }
}

TEST(BrightnessSwipeGesture, TapInStripStaysAPageTurn) {
  bs::Gesture g;
  g.press(450, 400, kPortrait.w, kPortrait.h);
  EXPECT_EQ(g.track(true, 450, 400), Event::None);
  EXPECT_EQ(g.track(true, 455, 410), Event::None);
  EXPECT_EQ(g.track(false, 455, 410), Event::None);
  EXPECT_FALSE(g.dragging());
}

TEST(BrightnessSwipeGesture, VerticalDragOwnsTheContactThroughRelease) {
  bs::Gesture g;
  g.press(450, 500, kPortrait.w, kPortrait.h);
  EXPECT_EQ(g.track(true, 452, 480), Event::None);  // inside the claim threshold
  EXPECT_EQ(g.track(true, 455, 460), Event::Start);
  EXPECT_TRUE(g.dragging());
  EXPECT_EQ(g.travel(), 40);
  EXPECT_EQ(g.track(true, 455, 460), Event::Hold);
  EXPECT_EQ(g.track(true, 300, 700), Event::Move);  // once claimed, the finger may leave the strip
  EXPECT_EQ(g.travel(), -200);
  EXPECT_EQ(g.track(false, 300, 700), Event::End);
  EXPECT_FALSE(g.dragging());
  EXPECT_EQ(g.track(false, 300, 700), Event::None);
}

TEST(BrightnessSwipeGesture, HorizontalSwipeFromStripIsLeftToPageTurns) {
  bs::Gesture g;
  g.press(470, 400, kPortrait.w, kPortrait.h);
  EXPECT_EQ(g.track(true, 435, 405), Event::None);
  EXPECT_EQ(g.track(true, 300, 330), Event::None);  // a later vertical slide does not reclaim it
  EXPECT_EQ(g.track(false, 200, 330), Event::None);
}

TEST(BrightnessSwipeGesture, DiagonalDragNeverEngages) {
  bs::Gesture g;
  g.press(470, 500, kPortrait.w, kPortrait.h);
  for (int d = 4; d <= 120; d += 4) EXPECT_EQ(g.track(true, 470 - d, 500 - d), Event::None) << d;
  EXPECT_EQ(g.track(false, 350, 380), Event::None);
}

TEST(BrightnessSwipeGesture, ContactsOutsideTheStripAreIgnored) {
  bs::Gesture g;
  for (const auto [x, y] : {std::pair{300, 400}, std::pair{470, 60}, std::pair{470, 760}}) {
    g.press(x, y, kPortrait.w, kPortrait.h);
    EXPECT_EQ(g.track(true, x, y + 200), Event::None);
    EXPECT_EQ(g.track(false, x, y + 200), Event::None);
  }
}

TEST(BrightnessSwipeLevel, FullRangeOverSeventyPercentOfTheHeight) {
  const int h = kPortrait.h;
  const int span = h * 70 / 100;
  EXPECT_EQ(bs::levelFor(60, 0, h, 1), 60);
  EXPECT_EQ(bs::levelFor(60, bs::kEngagePx, h, 1), 60);  // the claim threshold is not travel
  EXPECT_EQ(bs::levelFor(60, -bs::kEngagePx, h, 1), 60);
  EXPECT_EQ(bs::levelFor(0, bs::kEngagePx + span, h, 0), 100);
  EXPECT_EQ(bs::levelFor(100, -bs::kEngagePx - span, h, 0), 0);
  EXPECT_EQ(bs::levelFor(20, bs::kEngagePx + span / 2, h, 1), 70);
  EXPECT_EQ(bs::levelFor(60, -bs::kEngagePx - span / 4, h, 1), 35);
}

TEST(BrightnessSwipeLevel, ClampsToFloorAndCeiling) {
  EXPECT_EQ(bs::levelFor(90, 700, kPortrait.h, 1), 100);
  EXPECT_EQ(bs::levelFor(10, -700, kPortrait.h, 1), 1);              // lit: the slider's 1% floor
  EXPECT_EQ(bs::levelFor(0, -700, kPortrait.h, 0), 0);               // started off: 0 keeps it off
  EXPECT_EQ(bs::levelFor(0, bs::kEngagePx + 6, kPortrait.h, 0), 1);  // the first few px up switch it on
}

TEST(BrightnessSwipeLevel, MonotonicInTravel) {
  for (const Size size : {kPortrait, kLandscape}) {
    int previous = -1;
    for (int travel = -size.h; travel <= size.h; ++travel) {
      const int level = bs::levelFor(50, travel, size.h, 0);
      ASSERT_GE(level, previous) << travel;
      previous = level;
    }
  }
}
