#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "util/SleepImagePick.h"

using namespace sleep_pick;

namespace {
std::vector<Source> order(const bool hasChoice) {
  const auto sources = sourceOrder(hasChoice);
  return {sources.begin(), sources.end()};
}
}  // namespace

TEST(SleepImagePick, AcceptsBmpDirectlyInSleepFolders) {
  EXPECT_TRUE(isChoicePath("/sleep/mountains.bmp"));
  EXPECT_TRUE(isChoicePath("/.sleep/CAT.BMP"));
  EXPECT_TRUE(isChoicePath("/sleep/\xD0\xB3\xD0\xBE\xD1\x80\xD1\x8B.bmp"));  // горы.bmp
}

TEST(SleepImagePick, RejectsPathsTheSleepScreenWouldNotPick) {
  EXPECT_FALSE(isChoicePath(""));
  EXPECT_FALSE(isChoicePath("/sleep.bmp"));
  EXPECT_FALSE(isChoicePath("/sleep/"));
  EXPECT_FALSE(isChoicePath("/sleep/.hidden.bmp"));
  EXPECT_FALSE(isChoicePath("/sleep/sub/a.bmp"));
  EXPECT_FALSE(isChoicePath("/sleep/a.png"));
  EXPECT_FALSE(isChoicePath("/sleep/.bmp"));
  EXPECT_FALSE(isChoicePath("/sleeping/a.bmp"));
  EXPECT_FALSE(isChoicePath("/sleep-overlay/a.bmp"));
  EXPECT_FALSE(isChoicePath("sleep/a.bmp"));
  EXPECT_FALSE(isChoicePath("/books/a.bmp"));
  EXPECT_FALSE(isChoicePath("/sleep/a\\b.bmp"));
}

TEST(SleepImagePick, RejectsPathsThatDoNotFitTheSetting) {
  const std::string fits = "/sleep/" + std::string(kMaxPathBytes - 1 - 7 - 4, 'a') + ".bmp";
  ASSERT_EQ(fits.size(), kMaxPathBytes - 1);
  EXPECT_TRUE(isChoicePath(fits));
  EXPECT_FALSE(isChoicePath("/sleep/a" + fits.substr(7)));
}

TEST(SleepImagePick, ChosenPictureFallsBackToRandomThenRootBmp) {
  EXPECT_EQ(order(true), (std::vector<Source>{Source::Chosen, Source::Random, Source::RootBmp}));
}

TEST(SleepImagePick, WithoutChoiceRootBmpKeepsItsPriority) {
  EXPECT_EQ(order(false), (std::vector<Source>{Source::RootBmp, Source::Random}));
}

TEST(SleepImagePick, ThumbKeyChangesWithEveryInput) {
  const uint32_t base = thumbKey("/sleep/a.bmp", 1000, 42, 136, 226, false);
  EXPECT_EQ(base, thumbKey("/sleep/a.bmp", 1000, 42, 136, 226, false));
  EXPECT_NE(base, thumbKey("/sleep/b.bmp", 1000, 42, 136, 226, false));
  EXPECT_NE(base, thumbKey("/.sleep/a.bmp", 1000, 42, 136, 226, false));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1001, 42, 136, 226, false));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1000, 43, 136, 226, false));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1000, 42, 137, 226, false));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1000, 42, 136, 225, false));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1000, 42, 136, 226, true));
}

TEST(SleepImagePick, ScreenSizedImageFillsTheThumb) {
  const auto layout = thumbLayout(480, 800, 480, 800, 136, 226, false);
  EXPECT_EQ(layout.srcX, 0);
  EXPECT_EQ(layout.srcY, 0);
  EXPECT_EQ(layout.srcW, 480);
  EXPECT_EQ(layout.srcH, 800);
  EXPECT_EQ(layout.dstX, 0);
  EXPECT_EQ(layout.dstY, 0);
  EXPECT_EQ(layout.dstW, 136);
  EXPECT_EQ(layout.dstH, 226);
}

TEST(SleepImagePick, SmallImageStaysCenteredAtScreenScale) {
  const auto layout = thumbLayout(240, 400, 480, 800, 136, 226, false);
  EXPECT_EQ(layout.srcW, 240);
  EXPECT_EQ(layout.srcH, 400);
  EXPECT_EQ(layout.dstX, 34);
  EXPECT_EQ(layout.dstY, 57);
  EXPECT_EQ(layout.dstW, 68);
  EXPECT_EQ(layout.dstH, 113);
}

TEST(SleepImagePick, LandscapeFitLetterboxes) {
  const auto layout = thumbLayout(800, 480, 480, 800, 136, 226, false);
  EXPECT_EQ(layout.srcW, 800);
  EXPECT_EQ(layout.srcH, 480);
  EXPECT_EQ(layout.dstX, 0);
  EXPECT_EQ(layout.dstW, 136);
  EXPECT_EQ(layout.dstY, 72);
  EXPECT_EQ(layout.dstH, 82);
}

TEST(SleepImagePick, LandscapeCropTakesTheCenterAndFills) {
  const auto layout = thumbLayout(1600, 960, 480, 800, 136, 226, true);
  EXPECT_EQ(layout.srcX, 512);
  EXPECT_EQ(layout.srcW, 576);
  EXPECT_EQ(layout.srcY, 0);
  EXPECT_EQ(layout.srcH, 960);
  EXPECT_EQ(layout.dstX, 0);
  EXPECT_EQ(layout.dstY, 0);
  EXPECT_EQ(layout.dstW, 136);
  EXPECT_EQ(layout.dstH, 226);
}

TEST(SleepImagePick, LayoutStaysInsideTheThumb) {
  for (const auto& size : std::vector<std::pair<int, int>>{{1, 1}, {2048, 3}, {3, 3072}, {2048, 3072}, {481, 800}}) {
    for (const bool crop : {false, true}) {
      const auto layout = thumbLayout(size.first, size.second, 480, 800, 136, 226, crop);
      EXPECT_GE(layout.dstW, 1);
      EXPECT_GE(layout.dstH, 1);
      EXPECT_LE(layout.dstX + layout.dstW, 136);
      EXPECT_LE(layout.dstY + layout.dstH, 226);
      EXPECT_GE(layout.srcW, 1);
      EXPECT_GE(layout.srcH, 1);
      EXPECT_LE(layout.srcX + layout.srcW, size.first);
      EXPECT_LE(layout.srcY + layout.srcH, size.second);
    }
  }
}
