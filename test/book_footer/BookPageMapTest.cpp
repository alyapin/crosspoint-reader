#include <gtest/gtest.h>

#include <vector>

#include "BookPageMap.h"

namespace {

// Five chapters: a 1 KB title page, then 40, 20, 60 and 30 KB of text.
BookPageMap fiveChapters(const uint32_t key = 7) {
  BookPageMap map;
  map.reset({1000, 41000, 61000, 121000, 151000}, key);
  return map;
}

TEST(BookPageMap, ExactWhenEveryChapterIsKnown) {
  auto map = fiveChapters();
  const uint16_t counts[] = {1, 16, 8, 24, 12};
  for (size_t i = 0; i < 5; i++) map.setPages(map.layoutKey(), i, counts[i], 0);

  const auto pos = map.locate(2, 3, 8, true);
  EXPECT_TRUE(pos.exact);
  EXPECT_EQ(pos.page, 1u + 16u + 4u);
  EXPECT_EQ(pos.total, 61u);
}

TEST(BookPageMap, EstimatesUnknownChaptersFromKnownDensity) {
  auto map = fiveChapters();
  map.setPages(map.layoutKey(), 0, 1, 0);
  map.setPages(map.layoutKey(), 1, 16, 0);  // 40 KB -> 16 pages: 2500 bytes per page

  // Reading chapter 1, page 10 of 16; chapters 2..4 hold 110 KB -> 44 pages.
  const auto pos = map.locate(1, 9, 16, true);
  EXPECT_FALSE(pos.exact);
  EXPECT_EQ(pos.page, 11u);
  EXPECT_EQ(pos.total, 1u + 16u + 8u + 24u + 12u);
}

TEST(BookPageMap, TinyChaptersDoNotSkewTheDensity) {
  auto map = fiveChapters();
  map.setPages(map.layoutKey(), 0, 3, 0);  // a 1 KB title page that lays out as 3 pages

  // The building chapter's own estimate (20 pages for 40 KB) sets the density.
  const auto pos = map.locate(1, 0, 20, false);
  EXPECT_FALSE(pos.exact);
  EXPECT_EQ(pos.page, 4u);
  EXPECT_EQ(pos.total, 3u + 20u + 10u + 30u + 15u);
}

TEST(BookPageMap, FallsBackToDefaultDensityWithoutData) {
  BookPageMap map;
  map.reset({500, 25500}, 1);
  const auto pos = map.locate(0, 0, 1, false);
  EXPECT_EQ(pos.page, 1u);
  EXPECT_EQ(pos.total, 1u + 10u);  // 25 KB at 2500 bytes per page
}

TEST(BookPageMap, UnknownChaptersHaveAtLeastOnePage) {
  BookPageMap map;
  map.reset({40000, 40100, 40100}, 1);  // a 100-byte chapter and an empty one
  map.setPages(map.layoutKey(), 0, 16, 0);
  const auto pos = map.locate(0, 15, 16, true);
  EXPECT_EQ(pos.page, 16u);
  EXPECT_EQ(pos.total, 17u);
}

TEST(BookPageMap, CurrentChapterNeverShorterThanThePageShown) {
  auto map = fiveChapters();
  const auto pos = map.locate(1, 30, 12, false);  // the estimate lags the page reached
  EXPECT_EQ(pos.page, 1u + 31u);
  EXPECT_GT(pos.total, pos.page);
}

TEST(BookPageMap, MapCountWinsOverABuildingSectionsEstimate) {
  auto map = fiveChapters();
  for (size_t i = 0; i < 5; i++) map.setPages(map.layoutKey(), i, 10, 0);
  const auto pos = map.locate(2, 0, 7, false);
  EXPECT_TRUE(pos.exact);
  EXPECT_EQ(pos.total, 50u);
}

TEST(BookPageMap, NextUnknownWrapsAndSkipsTheCurrentChapter) {
  auto map = fiveChapters();
  map.setPages(map.layoutKey(), 3, 5, 0);
  EXPECT_EQ(map.nextUnknown(2), 4);
  EXPECT_EQ(map.nextUnknown(4), 0);
  for (size_t i = 0; i < 5; i++) map.setPages(map.layoutKey(), i, 5, 0);
  EXPECT_EQ(map.nextUnknown(0), -1);

  auto single = fiveChapters();
  for (size_t i = 0; i < 5; i++) {
    if (i != 2) single.setPages(single.layoutKey(), i, 5, 0);
  }
  EXPECT_EQ(single.nextUnknown(2), -1);  // only the current one is left
}

TEST(BookPageMap, RoundTripsForTheSameLayoutOnly) {
  auto map = fiveChapters(42);
  map.setPages(map.layoutKey(), 1, 16, 24000);
  map.setPages(map.layoutKey(), 4, 12, 16500);
  const std::vector<uint8_t> saved = map.encode();

  auto sameLayout = fiveChapters(42);
  ASSERT_TRUE(sameLayout.decode(saved.data(), saved.size()));
  EXPECT_TRUE(sameLayout.known(1));
  EXPECT_TRUE(sameLayout.known(4));
  EXPECT_FALSE(sameLayout.known(2));
  EXPECT_EQ(sameLayout.pagesOf(4), 12u);
  EXPECT_EQ(sameLayout.charsPerPage(), map.charsPerPage());

  auto otherLayout = fiveChapters(43);
  EXPECT_FALSE(otherLayout.decode(saved.data(), saved.size()));
  EXPECT_FALSE(otherLayout.known(1));

  BookPageMap otherBook;
  otherBook.reset({1000, 2000}, 42);
  EXPECT_FALSE(otherBook.decode(saved.data(), saved.size()));

  auto truncated = fiveChapters(42);
  EXPECT_FALSE(truncated.decode(saved.data(), saved.size() - 1));
}

TEST(BookPageMap, CharsPerPageCountsFullPagesOnly) {
  auto map = fiveChapters();
  map.setPages(map.layoutKey(), 1, 6, 0);
  EXPECT_EQ(map.charsPerPage(), 0u);  // five full pages are too few to trust
  map.setPages(map.layoutKey(), 1, 6, 7500);
  map.setPages(map.layoutKey(), 2, 7, 9600);  // 1600 chars per page
  map.setPages(map.layoutKey(), 0, 1, 0);     // a one-page chapter has no full page
  EXPECT_EQ(map.charsPerPage(), (7500u + 9600u) / 11u);
}

TEST(BookPageMap, IgnoresCountsFromAnotherLayout) {
  auto map = fiveChapters(1);
  map.relayout(2);  // the text settings changed while chapter 3 was being laid out for layout 1
  EXPECT_FALSE(map.setPages(1, 3, 40, 52000));
  EXPECT_FALSE(map.known(3));
  EXPECT_TRUE(map.setPages(2, 3, 30, 39000));
  EXPECT_EQ(map.pagesOf(3), 30u);
}

TEST(BookPageMap, RelayoutForgetsCounts) {
  auto map = fiveChapters(1);
  map.setPages(map.layoutKey(), 1, 16, 0);
  map.relayout(2);
  EXPECT_EQ(map.layoutKey(), 2u);
  EXPECT_FALSE(map.known(1));
}

}  // namespace
