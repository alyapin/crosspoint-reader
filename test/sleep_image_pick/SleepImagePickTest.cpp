#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "util/SleepImagePick.h"

using namespace sleep_pick;

namespace {
std::vector<Source> order(const bool hasChoice, const bool randomPicked) {
  const auto sources = sourceOrder(hasChoice, randomPicked);
  return {sources.begin(), sources.end()};
}

// What the sleep screen shows: the first source that renders, as
// SleepActivity::renderCustomSleepScreen walks them (nullopt: the default screen).
std::optional<Source> shown(const bool hasChoice, const bool randomPicked, const std::vector<Source>& available) {
  for (const auto source : sourceOrder(hasChoice, randomPicked)) {
    if (std::find(available.begin(), available.end(), source) != available.end()) return source;
  }
  return std::nullopt;
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
  const std::vector<Source> expected{Source::Chosen, Source::Random, Source::RootBmp};
  EXPECT_EQ(order(true, false), expected);
  EXPECT_EQ(order(true, true), expected);
  EXPECT_EQ(shown(true, false, {Source::Chosen, Source::RootBmp, Source::Random}), Source::Chosen);
  EXPECT_EQ(shown(true, false, {Source::RootBmp, Source::Random}), Source::Random);
  EXPECT_EQ(shown(true, false, {Source::RootBmp}), Source::RootBmp);
}

// Unset: nothing picked yet (settings saved before the Random pick was stored,
// or after "Set sleep cover"), so /sleep.bmp keeps its old first place.
TEST(SleepImagePick, UnsetKeepsRootBmpFirst) {
  EXPECT_EQ(order(false, false), (std::vector<Source>{Source::RootBmp, Source::Random}));
  EXPECT_EQ(shown(false, false, {Source::RootBmp, Source::Random}), Source::RootBmp);
  EXPECT_EQ(shown(false, false, {Source::Random}), Source::Random);
  EXPECT_EQ(shown(false, false, {}), std::nullopt);
}

TEST(SleepImagePick, ExplicitRandomGoesBeforeRootBmp) {
  EXPECT_EQ(order(false, true), (std::vector<Source>{Source::Random, Source::RootBmp}));
  EXPECT_EQ(shown(false, true, {Source::RootBmp, Source::Random}), Source::Random);
  // /sleep.bmp stays the fallback while the folders have nothing to show.
  EXPECT_EQ(shown(false, true, {Source::RootBmp}), Source::RootBmp);
  EXPECT_EQ(shown(false, true, {}), std::nullopt);
}

TEST(SleepImagePick, ThumbKeyChangesWithEveryInput) {
  const uint32_t base = thumbKey("/sleep/a.bmp", 1000, 42, 136, 226, false, 0);
  EXPECT_EQ(base, thumbKey("/sleep/a.bmp", 1000, 42, 136, 226, false, 0));
  EXPECT_NE(base, thumbKey("/sleep/b.bmp", 1000, 42, 136, 226, false, 0));
  EXPECT_NE(base, thumbKey("/.sleep/a.bmp", 1000, 42, 136, 226, false, 0));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1001, 42, 136, 226, false, 0));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1000, 43, 136, 226, false, 0));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1000, 42, 137, 226, false, 0));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1000, 42, 136, 225, false, 0));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1000, 42, 136, 226, true, 0));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1000, 42, 136, 226, false, 1));
  EXPECT_NE(base, thumbKey("/sleep/a.bmp", 1000, 42, 136, 226, false, 2));
  EXPECT_NE(thumbKey("/sleep/a.bmp", 1000, 42, 136, 226, false, 1),
            thumbKey("/sleep/a.bmp", 1000, 42, 136, 226, false, 2));
}

// Keys from before the filter was hashed (format 1: the crop byte, then the
// format byte) must not name a current thumbnail, so old cache files are
// regenerated and pruned instead of shown under the wrong filter.
TEST(SleepImagePick, ThumbKeyDiffersFromFormatOne) {
  const auto formatOne = [](const std::string& path, const bool crop) {
    uint32_t hash = 2166136261u;
    const auto mix = [&hash](const uint8_t byte) { hash = (hash ^ byte) * 16777619u; };
    for (const char c : path) mix(static_cast<uint8_t>(c));
    mix(0);
    for (const uint32_t value : {1000u, 42u, 136u, 226u}) {
      for (int shift = 0; shift < 32; shift += 8) mix(static_cast<uint8_t>(value >> shift));
    }
    mix(crop ? 1 : 0);
    mix(1);
    return hash;
  };
  for (const bool crop : {false, true}) {
    for (const uint8_t filter : {0, 1, 2}) {
      EXPECT_NE(formatOne("/sleep/a.bmp", crop), thumbKey("/sleep/a.bmp", 1000, 42, 136, 226, crop, filter));
    }
  }
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
