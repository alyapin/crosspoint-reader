#include <gtest/gtest.h>

#include <vector>

#include "activities/reader/BrightnessSwipe.h"
#include "util/EdgeBackSwipe.h"

namespace bs = brightness_swipe;
namespace eb = edge_back_swipe;
namespace fui = freeink::ui;

namespace {
constexpr int kW = 480;
constexpr int kH = 800;
// Main loop pass and the SDK flick limits (InputManager::TOUCH_SWIPE_MIN_PX / _MAX_MS).
constexpr int kFrameMs = 10;
constexpr int kFlickMinPx = 60;
constexpr int kFlickMaxMs = 700;

enum class Outcome { None, Brightness, Back, Left, Right, Up, Down };

struct Point {
  int x;
  int y;
};
using Track = std::vector<Point>;  // one sample per loop frame, finger down throughout

Track line(const Point a, const Point b, const int frames) {
  Track t;
  for (int i = 0; i <= frames; ++i) t.push_back({a.x + (b.x - a.x) * i / frames, a.y + (b.y - a.y) * i / frames});
  return t;
}

Track operator+(Track a, const Track& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// One contact on the book page, polled in ReaderActivity's order: the
// brightness drag owns the frames it claims, through the release; otherwise
// the release is an SDK flick, Back first, then a page-turn swipe.
Outcome classify(const Track& t, const bool backHere = true, const int w = kW, const int h = kH) {
  bs::Gesture g;
  g.press(t.front().x, t.front().y, w, h);
  for (const Point p : t) g.track(true, p.x, p.y);
  const Point s = t.front();
  const Point e = t.back();
  if (g.track(false, e.x, e.y) != bs::Gesture::Event::None) return Outcome::Brightness;
  const int heldMs = static_cast<int>(t.size() - 1) * kFrameMs;
  if (heldMs > kFlickMaxMs || (std::abs(e.x - s.x) < kFlickMinPx && std::abs(e.y - s.y) < kFlickMinPx)) {
    return Outcome::None;
  }
  if (backHere && eb::isBackSwipe(s.x, s.y, e.x, e.y, w)) return Outcome::Back;
  switch (fui::swipeDirection(s.x, s.y, e.x, e.y)) {
    case fui::SwipeDir::Left:
      return Outcome::Left;
    case fui::SwipeDir::Right:
      return Outcome::Right;
    case fui::SwipeDir::Up:
      return Outcome::Up;
    case fui::SwipeDir::Down:
      return Outcome::Down;
    default:
      return Outcome::None;
  }
}
}  // namespace

TEST(EdgeBackSwipe, InwardFlickFromEitherEdgeIsBack) {
  EXPECT_EQ(classify(line({5, 400}, {200, 380}, 15)), Outcome::Back);
  EXPECT_EQ(classify(line({475, 400}, {280, 420}, 15)), Outcome::Back);
  // Landscape: the logical side edges, whatever the panel's mount.
  EXPECT_EQ(classify(line({795, 240}, {600, 250}, 15), true, kH, kW), Outcome::Back);
}

TEST(EdgeBackSwipe, StartBandIsFortyPixelsOnEachSide) {
  EXPECT_TRUE(eb::isBackSwipe(39, 400, 139, 400, kW));
  EXPECT_FALSE(eb::isBackSwipe(40, 400, 140, 400, kW));
  EXPECT_TRUE(eb::isBackSwipe(440, 400, 340, 400, kW));
  EXPECT_FALSE(eb::isBackSwipe(439, 400, 339, 400, kW));
  EXPECT_TRUE(eb::isBackSwipe(760, 240, 660, 240, kH));
  EXPECT_FALSE(eb::isBackSwipe(759, 240, 659, 240, kH));
}

TEST(EdgeBackSwipe, NeedsTravelInwardAndTwiceAsSidewaysAsVertical) {
  EXPECT_TRUE(eb::isBackSwipe(10, 400, 70, 400, kW));
  EXPECT_FALSE(eb::isBackSwipe(10, 400, 69, 400, kW));
  EXPECT_TRUE(eb::isBackSwipe(10, 400, 111, 450, kW));
  EXPECT_FALSE(eb::isBackSwipe(10, 400, 110, 450, kW));   // |dx| == 2|dy|
  EXPECT_FALSE(eb::isBackSwipe(470, 400, 479, 400, kW));  // outward
  EXPECT_FALSE(eb::isBackSwipe(10, 400, 0, 400, kW));
}

TEST(EdgeBackSwipe, SwipesAwayFromTheEdgesStayPageTurns) {
  EXPECT_EQ(classify(line({300, 400}, {100, 400}, 15)), Outcome::Left);
  // The old left-edge Back took a quarter of the width; this is a page turn now.
  EXPECT_EQ(classify(line({100, 400}, {300, 400}, 15)), Outcome::Right);
  // Inside the brightness strip but past the Back band.
  EXPECT_EQ(classify(line({430, 400}, {250, 400}, 15)), Outcome::Left);
}

TEST(EdgeBackSwipe, DiagonalEdgeFlickIsAPageTurn) {
  EXPECT_EQ(classify(line({10, 300}, {160, 400}, 15)), Outcome::Right);
  EXPECT_EQ(classify(line({470, 500}, {320, 400}, 15)), Outcome::Left);
}

TEST(EdgeBackSwipe, TapsShortAndSlowEdgeContactsDoNothing) {
  EXPECT_EQ(classify(line({8, 400}, {12, 404}, 8)), Outcome::None);
  EXPECT_EQ(classify(line({8, 400}, {58, 400}, 8)), Outcome::None);
  // Past the SDK flick window the release is no swipe at all, as for page turns.
  EXPECT_EQ(classify(line({8, 400}, {300, 400}, 80)), Outcome::None);
}

TEST(EdgeBackSwipe, OffOnThisScreenLeavesEdgeFlicksToPageTurns) {
  EXPECT_EQ(classify(line({5, 400}, {200, 380}, 15), false), Outcome::Right);
  EXPECT_EQ(classify(line({475, 400}, {280, 420}, 15), false), Outcome::Left);
}

TEST(EdgeBackSwipe, VerticalRightEdgeDragStaysBrightness) {
  EXPECT_EQ(classify(line({470, 500}, {470, 250}, 25)), Outcome::Brightness);
  EXPECT_EQ(classify(line({470, 300}, {465, 650}, 25)), Outcome::Brightness);
  // Claimed going up, then pulled inward: the brightness drag keeps the release.
  EXPECT_EQ(classify(line({470, 500}, {468, 440}, 6) + line({468, 440}, {250, 445}, 10)), Outcome::Brightness);
}

TEST(EdgeBackSwipe, BackAndBrightnessNeverShareAContact) {
  for (const int startX : {440, 455, 479}) {
    for (int dy = -120; dy <= 120; dy += 8) {
      for (const int dx : {-80, -150, -220}) {
        const Track t = line({startX, 400}, {startX + dx, 400 + dy}, 15);
        const Outcome o = classify(t);
        const bool back = eb::isBackSwipe(startX, 400, startX + dx, 400 + dy, kW);
        if (back) EXPECT_EQ(o, Outcome::Back) << startX << " " << dx << "," << dy;
        if (o == Outcome::Brightness) EXPECT_FALSE(back) << startX << " " << dx << "," << dy;
      }
    }
  }
}

// A Back swipe is horizontal, so it is never the top-edge control center or
// the bottom-edge Home / reader-menu swipe, even from a corner.
TEST(EdgeBackSwipe, NeverOneOfTheTopOrBottomEdgeGestures) {
  for (const int w : {kW, kH}) {
    const int h = w == kW ? kH : kW;
    for (const int sx : {0, 20, 39, w - 40, w - 1}) {
      for (const int sy : {0, 50, h / 2, h - 50, h - 1}) {
        const int dir = sx < w / 2 ? 1 : -1;
        for (int dy = -60; dy <= 60; dy += 5) {
          const int ex = sx + dir * 130;
          const int ey = sy + dy;
          if (!eb::isBackSwipe(sx, sy, ex, ey, w)) continue;
          EXPECT_FALSE(fui::edgeSwipe(fui::ScreenEdge::Top, sx, sy, ex, ey, w, h));
          EXPECT_FALSE(fui::edgeSwipe(fui::ScreenEdge::Bottom, sx, sy, ex, ey, w, h));
        }
      }
    }
  }
}
