#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "Fb2Position.h"
#include "Fb2ToEpub.h"

namespace {

using Fb2Position::Saved;

TEST(Fb2Position, ParsesEveryProgressLayout) {
  const uint8_t data[10] = {0x02, 0x01, 0x05, 0x00, 0x0A, 0x00, 1, 2, 3, 4};
  Saved pos;
  ASSERT_TRUE(Fb2Position::parseProgress(data, 10, pos));
  EXPECT_EQ(pos.spine, 0x102);
  EXPECT_EQ(pos.page, 5);
  EXPECT_EQ(pos.pageCount, 10);
  ASSERT_TRUE(Fb2Position::parseProgress(data, 4, pos));
  EXPECT_EQ(pos.pageCount, 0);
  EXPECT_FALSE(Fb2Position::parseProgress(data, 5, pos));
}

TEST(Fb2Position, BookFractionMatchesTheReader) {
  // Spine items of 100, 300 and 600 bytes.
  const std::vector<uint32_t> cumulative = {100, 400, 1000};
  const auto sizes = [&](const int i) { return cumulative[i]; };
  EXPECT_FLOAT_EQ(Fb2Position::bookFraction({1, 5, 10}, 3, sizes), (100 + 0.5f * 300) / 1000);
  EXPECT_FLOAT_EQ(Fb2Position::bookFraction({0, 0, 4}, 3, sizes), 0.0f);
  // Unknown page count: the chapter's start. The end-of-book spine: the end.
  EXPECT_FLOAT_EQ(Fb2Position::bookFraction({2, 7, 0}, 3, sizes), 0.4f);
  EXPECT_FLOAT_EQ(Fb2Position::bookFraction({3, 0, 0}, 3, sizes), 1.0f);
  // The last-page sentinel and a page past a stale count stay within the chapter.
  EXPECT_FLOAT_EQ(Fb2Position::bookFraction({1, UINT16_MAX, 10}, 3, sizes), 0.1f);
  EXPECT_FLOAT_EQ(Fb2Position::bookFraction({2, 12, 10}, 3, sizes), 1.0f);
  EXPECT_FLOAT_EQ(Fb2Position::bookFraction({0, 0, 0}, 0, sizes), 0.0f);
}

TEST(Fb2Position, StampDecidesReconversion) {
  char buf[8];
  const size_t n = Fb2Position::formatStamp(buf, sizeof(buf));
  ASSERT_GT(n, 0u);
  EXPECT_EQ(Fb2Position::parseStamp(buf, n), Fb2ToEpub::FORMAT_VERSION);
  EXPECT_FALSE(Fb2Position::reconvert(Fb2Position::parseStamp(buf, n)));
  // Archives from before the stamp, or with a damaged one, are converted again.
  EXPECT_TRUE(Fb2Position::reconvert(Fb2Position::parseStamp("", 0)));
  EXPECT_TRUE(Fb2Position::reconvert(Fb2Position::parseStamp("x", 1)));
  EXPECT_TRUE(Fb2Position::reconvert(Fb2Position::parseStamp("1\n", 2)));
}

TEST(Fb2Position, PendingFractionRoundTrips) {
  char buf[32];
  const size_t n = Fb2Position::formatPending(buf, sizeof(buf), 0.4123456f, 1);
  ASSERT_GT(n, 0u);
  float fraction = -1;
  uint8_t version = 0;
  ASSERT_TRUE(Fb2Position::parsePending(buf, n, fraction, version));
  EXPECT_NEAR(fraction, 0.412346f, 1e-6);
  EXPECT_EQ(version, 1);
  EXPECT_FALSE(Fb2Position::parsePending("1.5 2", 5, fraction, version));
  EXPECT_FALSE(Fb2Position::parsePending("0.5", 3, fraction, version));
  EXPECT_FALSE(Fb2Position::parsePending("", 0, fraction, version));
}

TEST(Fb2Position, ExactPositionWinsOnlyForTheSameConversion) {
  const uint8_t current = Fb2ToEpub::FORMAT_VERSION;
  // Cache deleted under this converter: the saved page is exact.
  EXPECT_FALSE(Fb2Position::restoreByFraction(current, true));
  EXPECT_FALSE(Fb2Position::bookmarksMoved(current));
  // Without a saved page, or after a converter change, the fraction places the reader.
  EXPECT_TRUE(Fb2Position::restoreByFraction(current, false));
  EXPECT_TRUE(Fb2Position::restoreByFraction(current - 1, true));
  EXPECT_TRUE(Fb2Position::bookmarksMoved(0));
}

}  // namespace
