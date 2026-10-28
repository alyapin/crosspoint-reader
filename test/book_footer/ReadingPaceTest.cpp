#include <gtest/gtest.h>

#include <string>

#include "ReadingPace.h"

namespace {

std::string timeLeft(const uint32_t seconds) {
  char out[40];
  formatTimeLeft(seconds, "ещё %u мин", "ещё %u ч", "ещё %u ч %u мин", out, sizeof(out));
  return out;
}

TEST(ReadingPace, StartsFromTheDefault) {
  ReadingPace pace;
  EXPECT_EQ(pace.charsPerMinute(), ReadingPace::DEFAULT_CHARS_PER_MINUTE);
}

TEST(ReadingPace, BlendsTowardMeasuredSpeed) {
  ReadingPace pace;
  ASSERT_TRUE(pace.addPage(1500, 60000));  // 1500 chars/min
  EXPECT_EQ(pace.charsPerMinute(), (1500 + 7 * ReadingPace::DEFAULT_CHARS_PER_MINUTE) / 8);
  for (int i = 0; i < 7; i++) ASSERT_TRUE(pace.addPage(1500, 60000));
  EXPECT_EQ(pace.charsPerMinute(), 1500);
}

TEST(ReadingPace, IgnoresFlippingIdlingAndSparsePages) {
  ReadingPace pace;
  EXPECT_FALSE(pace.addPage(1500, 1000));            // flipped past
  EXPECT_FALSE(pace.addPage(1500, 11 * 60 * 1000));  // put down for 11 minutes
  EXPECT_FALSE(pace.addPage(120, 30000));            // chapter tail
  EXPECT_FALSE(pace.addPage(1500, 5000));            // 18000 chars/min: skimming
  EXPECT_EQ(pace.sampleCount(), 0);
}

TEST(ReadingPace, RejectsOutliersOnceItKnowsTheReader) {
  ReadingPace pace;
  for (int i = 0; i < ReadingPace::MIN_SAMPLES_FOR_OUTLIERS; i++) ASSERT_TRUE(pace.addPage(1200, 60000));
  EXPECT_FALSE(pace.addPage(1200, 9 * 60 * 1000));  // 133 chars/min: away from the device
  EXPECT_FALSE(pace.addPage(1200, 15000));          // 4800 chars/min: over 3 x median
  EXPECT_TRUE(pace.addPage(1200, 45000));           // 1600 chars/min: a quicker page
}

TEST(ReadingPace, CentralMeanShrugsOffSingleExtremes) {
  ReadingPace pace;
  for (int i = 0; i < 7; i++) ASSERT_TRUE(pace.addPage(1000, 60000));
  ASSERT_TRUE(pace.addPage(2800, 60000));  // accepted (under 3 x median) but trimmed
  EXPECT_EQ(pace.charsPerMinute(), 1000);
}

TEST(ReadingPace, WindowForgetsOldSpeed) {
  ReadingPace pace;
  for (int i = 0; i < 16; i++) ASSERT_TRUE(pace.addPage(800, 60000));
  for (int i = 0; i < 16; i++) ASSERT_TRUE(pace.addPage(1600, 60000));
  EXPECT_EQ(pace.charsPerMinute(), 1600);
}

TEST(ReadingPace, RoundTripsAndRejectsDamage) {
  ReadingPace pace;
  for (int i = 0; i < 5; i++) ASSERT_TRUE(pace.addPage(1300, 60000));
  uint8_t saved[ReadingPace::ENCODED_SIZE];
  pace.encode(saved);

  ReadingPace loaded;
  ASSERT_TRUE(loaded.decode(saved, sizeof(saved)));
  EXPECT_EQ(loaded.sampleCount(), 5);
  EXPECT_EQ(loaded.charsPerMinute(), pace.charsPerMinute());

  EXPECT_FALSE(loaded.decode(saved, sizeof(saved) - 1));
  saved[6] = 2;  // next slot must follow a partial window's samples
  ReadingPace damaged;
  EXPECT_FALSE(damaged.decode(saved, sizeof(saved)));
  EXPECT_EQ(damaged.sampleCount(), 0);
}

TEST(ReadingPace, SecondsLeftScalesWithPagesAndSpeed) {
  EXPECT_EQ(readingSecondsLeft(10, 1500, 1500), 600u);
  EXPECT_EQ(readingSecondsLeft(0, 1500, 1500), 0u);
  EXPECT_EQ(readingSecondsLeft(10, 1500, 0), 0u);
}

TEST(ReadingPace, FormatsTimeLeft) {
  EXPECT_EQ(timeLeft(0), "ещё 1 мин");
  EXPECT_EQ(timeLeft(8 * 60 + 1), "ещё 9 мин");
  EXPECT_EQ(timeLeft(59 * 60), "ещё 59 мин");
  EXPECT_EQ(timeLeft(59 * 60 + 30), "ещё 1 ч");
  EXPECT_EQ(timeLeft(80 * 60), "ещё 1 ч 20 мин");
  EXPECT_EQ(timeLeft(82 * 60), "ещё 1 ч 20 мин");
  EXPECT_EQ(timeLeft(83 * 60), "ещё 1 ч 25 мин");
  EXPECT_EQ(timeLeft(118 * 60), "ещё 2 ч");
  EXPECT_EQ(timeLeft(11 * 3600 + 40 * 60), "ещё 12 ч");
}

}  // namespace
