#include <DayWindow.h>
#include <ReadingTracker.h>
#include <StatsCalendar.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>

namespace {

uint16_t day(const int y, const unsigned m, const unsigned d) {
  return static_cast<uint16_t>(stats::daysFromCivil(y, m, d));
}

void setTz(const char* rule) {
  setenv("TZ", rule, 1);
  tzset();
}

constexpr uint32_t MIN = 60;

}  // namespace

TEST(StatsCalendar, CivilRoundTrip) {
  EXPECT_EQ(stats::daysFromCivil(1970, 1, 1), 0);
  EXPECT_EQ(stats::daysFromCivil(2026, 10, 28), 20754);
  for (int32_t d = 18000; d < 22000; d += 37) {
    int y;
    unsigned m, dd;
    stats::civilFromDays(d, y, m, dd);
    EXPECT_EQ(stats::daysFromCivil(y, m, dd), d);
  }
}

TEST(StatsCalendar, WeekMonthYearStarts) {
  const uint16_t wed = day(2026, 10, 28);
  EXPECT_EQ(stats::weekday(wed), 2u);
  EXPECT_EQ(stats::weekStart(wed), day(2026, 10, 26));
  EXPECT_EQ(stats::weekStart(day(2026, 11, 1)), day(2026, 10, 26));  // Sunday ends the week
  EXPECT_EQ(stats::monthStart(wed), day(2026, 10, 1));
  EXPECT_EQ(stats::yearStart(wed), day(2026, 1, 1));
  EXPECT_EQ(stats::monthStart(day(2028, 2, 29)), day(2028, 2, 1));
}

TEST(StatsCalendar, LocalDayTurnsAtLocalMidnight) {
  setTz("MSK-3");
  // 2026-10-28 20:59:59 UTC is 23:59:59 in Moscow; one second later is the next day.
  const int64_t beforeMidnight = static_cast<int64_t>(day(2026, 10, 28)) * 86400 + 20 * 3600 + 59 * 60 + 59;
  EXPECT_EQ(stats::localDay(beforeMidnight), day(2026, 10, 28));
  EXPECT_EQ(stats::localDay(beforeMidnight + 1), day(2026, 10, 29));
  EXPECT_EQ(stats::localDay(0), stats::NO_DAY);

  setTz("UTC0");
  EXPECT_EQ(stats::localDay(beforeMidnight + 1), day(2026, 10, 28));
}

TEST(StatsCalendar, LocalDayFollowsDaylightSaving) {
  setTz("CET-1CEST,M3.5.0,M10.5.0/3");
  // Summer: UTC+2, so 22:30 UTC is already the next day; winter: UTC+1, still the same day.
  const int64_t summer = static_cast<int64_t>(day(2026, 7, 1)) * 86400 + 22 * 3600 + 30 * 60;
  const int64_t winter = static_cast<int64_t>(day(2026, 12, 1)) * 86400 + 22 * 3600 + 30 * 60;
  EXPECT_EQ(stats::localDay(summer), day(2026, 7, 2));
  EXPECT_EQ(stats::localDay(winter), day(2026, 12, 1));
  setTz("UTC0");
}

TEST(DayWindow, StreakCountsDaysFromFiveMinutes) {
  DayWindow w;
  const uint16_t today = day(2026, 10, 28);
  w.add(today - 3, 10 * MIN);
  w.add(today - 2, 5 * MIN);
  w.add(today - 1, 4 * MIN + 59);  // too short: breaks the run
  EXPECT_EQ(w.currentStreak(today), 0u);
  w.add(today - 1, 1);
  EXPECT_EQ(w.currentStreak(today), 3u);  // yesterday still counts before today is read
  w.add(today, 6 * MIN);
  EXPECT_EQ(w.currentStreak(today), 4u);
  EXPECT_EQ(w.currentStreak(today + 1), 4u);
  EXPECT_EQ(w.currentStreak(today + 2), 0u);
  EXPECT_EQ(w.bestStreak(), 4u);
}

TEST(DayWindow, RecordSurvivesTheWindowSliding) {
  DayWindow w;
  const uint16_t start = day(2025, 1, 1);
  for (uint16_t i = 0; i < 20; i++) w.add(start + i, 30 * MIN);
  w.add(start + DayWindow::DAYS + 100, 30 * MIN);
  EXPECT_EQ(w.secondsOn(start), 0u);
  EXPECT_EQ(w.bestStreak(), 20u);
  EXPECT_EQ(w.currentStreak(start + DayWindow::DAYS + 100), 1u);
}

TEST(DayWindow, LongCurrentStreakCountsDaysBeforeTheWindow) {
  DayWindow w;
  const uint16_t start = day(2025, 1, 1);
  const uint16_t days = DayWindow::DAYS + 50;
  for (uint16_t i = 0; i < days; i++) w.add(start + i, 6 * MIN);
  EXPECT_EQ(w.currentStreak(start + days - 1), days);
  EXPECT_EQ(w.bestStreak(), days);
  // A day older than the window is dropped, never shifting the others.
  w.add(start, 60 * MIN);
  EXPECT_EQ(w.secondsOn(start + days - 1), 6 * MIN);
}

TEST(DayWindow, SumsAndRoundTrip) {
  DayWindow w;
  const uint16_t today = day(2026, 10, 28);
  w.add(today, 70);
  w.add(today, 50);
  w.add(day(2026, 10, 26), 600);
  w.add(day(2026, 9, 30), 60);
  w.add(day(2025, 12, 31), 99);
  w.add(stats::NO_DAY, 1000);
  EXPECT_EQ(w.secondsOn(today), 120u);
  EXPECT_EQ(w.secondsBetween(stats::weekStart(today), today), 720u);
  EXPECT_EQ(w.secondsBetween(stats::monthStart(today), today), 720u);
  EXPECT_EQ(w.secondsBetween(stats::yearStart(today), today), 780u);
  uint8_t bytes[DayWindow::ENCODED_SIZE];
  w.encode(bytes);
  DayWindow copy;
  ASSERT_TRUE(copy.decode(bytes, sizeof(bytes)));
  EXPECT_EQ(copy.secondsOn(day(2025, 12, 31)), 99u);
  EXPECT_FALSE(copy.decode(bytes, sizeof(bytes) - 1));
}

TEST(ReadingTracker, IdleGapsDoNotCount) {
  setTz("UTC0");
  ReadingTracker t;
  const uint16_t d = day(2026, 10, 28);
  const int64_t epoch = static_cast<int64_t>(d) * 86400 + 12 * 3600;
  t.onPageShown(0, epoch, 100, false);
  t.noteTurn(true, true);
  t.onPageShown(60000, epoch + 60, 200, false);
  t.noteTurn(true, true);
  t.onPageShown(60000 + ReadingTracker::IDLE_MS + 1, epoch + 400, 300, false);  // away: not counted
  t.noteTurn(false, true);
  t.onPageShown(60000 + ReadingTracker::IDLE_MS + 31000, epoch + 430, 250, false);
  t.close(60000 + ReadingTracker::IDLE_MS + 31000 + ReadingTracker::IDLE_MS + 5);  // slept on the page
  ASSERT_TRUE(t.hasReady());
  const ReadingSegment s = t.takeReady();
  EXPECT_EQ(s.seconds, 90u);
  EXPECT_EQ(s.pages, 2u);  // backward turns are not pages read
  EXPECT_EQ(s.day, d);
  EXPECT_EQ(s.startEpoch, epoch);
  EXPECT_EQ(s.progressBp, 250u);
  EXPECT_FALSE(s.continues);
  EXPECT_FALSE(t.takeFinished());
}

TEST(ReadingTracker, CutsSegmentsAtMidnightAndEveryFiveMinutes) {
  setTz("MSK-3");
  ReadingTracker t;
  const uint16_t d = day(2026, 10, 28);
  // Local midnight in Moscow is 21:00 UTC the day before.
  int64_t epoch = static_cast<int64_t>(d) * 86400 + 21 * 3600 - 200;
  uint32_t ms = 0;
  t.onPageShown(ms, epoch, 0, false);
  ms += 150000;
  epoch += 150;
  t.onPageShown(ms, epoch, 0, false);
  EXPECT_FALSE(t.hasReady());
  ms += 100000;
  epoch += 100;
  t.onPageShown(ms, epoch, 0, false);  // 50 s past midnight: the gap is split there
  ASSERT_TRUE(t.hasReady());
  ReadingSegment s = t.takeReady();
  EXPECT_EQ(s.day, d);
  EXPECT_EQ(s.seconds, 200u);
  for (int i = 0; i < 3; i++) {
    ms += 100000;
    epoch += 100;
    t.onPageShown(ms, epoch, 0, false);
  }
  ASSERT_TRUE(t.hasReady());
  s = t.takeReady();
  EXPECT_EQ(s.day, d + 1);
  EXPECT_TRUE(s.continues);
  EXPECT_EQ(s.startEpoch, epoch - 350);  // starts at midnight
  EXPECT_EQ(s.seconds, 350u);
  setTz("UTC0");
}

TEST(ReadingTracker, AccidentalOpenIsDroppedAndFinishIsReported) {
  setTz("UTC0");
  ReadingTracker t;
  const uint16_t d = day(2026, 10, 28);
  const int64_t epoch = static_cast<int64_t>(d) * 86400;
  t.onPageShown(0, epoch, 9900, false);
  t.close(5000);
  EXPECT_FALSE(t.hasReady());
  EXPECT_FALSE(t.takeFinished());  // opened near the end, not read to it

  t.onPageShown(10000, epoch + 10, 9700, false);
  t.noteTurn(true, true);
  t.onPageShown(40000, epoch + 40, 9810, false);
  EXPECT_TRUE(t.takeFinished());
  EXPECT_FALSE(t.takeFinished());
  t.onPageShown(41000, epoch + 41, 10000, true);
  EXPECT_TRUE(t.takeFinished());
}

TEST(ReadingTracker, UnknownClockStillCountsTime) {
  ReadingTracker t;
  t.onPageShown(0, 0, 0, false);
  t.noteTurn(true, true);
  t.onPageShown(30000, 0, 0, false);
  t.close(31000);
  ASSERT_TRUE(t.hasReady());
  const ReadingSegment s = t.takeReady();
  EXPECT_EQ(s.day, stats::NO_DAY);
  EXPECT_EQ(s.seconds, 31u);
}
